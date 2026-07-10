#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <ndpi/ndpi_main.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_hash_crc.h>
#include <rte_malloc.h>

#define MAX_FLOWS 65536
#define FLOW_TIMEOUT_MS 30000 // 30 seconds
#define CLEANUP_INTERVAL_MS 5000 // 5 seconds

struct flow_key {
    uint32_t ip_src;
    uint32_t ip_dst;
    uint16_t port_src;
    uint16_t port_dst;
    uint8_t  proto;
} __attribute__((packed));

struct flow_value {
    struct ndpi_flow_struct *ndpi_flow;
    uint64_t last_packet_time_ms;
    struct flow_key key;
};

static struct ndpi_detection_module_struct *ndpi_info_mod = NULL;
static struct rte_hash *flow_table = NULL;
static uint64_t last_cleanup_time_ms = 0;

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static inline uint64_t get_current_time_ms(void) {
    return rte_get_tsc_cycles() * 1000 / rte_get_tsc_hz();
}

static void cleanup_expired_flows(void) {
    uint64_t now = get_current_time_ms();
    if (now - last_cleanup_time_ms < CLEANUP_INTERVAL_MS) return;

    const void *next_key;
    void *next_data;
    uint32_t iter = 0;
    int32_t ret;
    uint32_t expired_count = 0;

    while ((ret = rte_hash_iterate(flow_table, &next_key, &next_data, &iter)) >= 0) {
        struct flow_value *val = (struct flow_value *)next_data;
        if (now - val->last_packet_time_ms > FLOW_TIMEOUT_MS) {
            rte_hash_del_key(flow_table, next_key);
            ndpi_flow_free(val->ndpi_flow);
            rte_free(val);
            expired_count++;
        }
    }
    
    if (expired_count > 0) {
        printf("Cleaned up %u expired flows. Total active: %d\n", expired_count, rte_hash_count(flow_table));
    }
    last_cleanup_time_ms = now;
}

static inline int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = {0};
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port)) return -1;
    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) return retval;

    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0) return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) return retval;

    retval = rte_eth_rx_queue_setup(port, 0, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) return retval;

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    retval = rte_eth_tx_queue_setup(port, 0, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0) return retval;

    retval = rte_eth_dev_start(port);
    if (retval < 0) return retval;

    rte_eth_promiscuous_enable(port);
    return 0;
}

static void process_packet(struct rte_mbuf *m) {
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    struct rte_tcp_hdr *tcp_hdr;
    struct rte_udp_hdr *udp_hdr;
    struct flow_key key = {0};
    struct flow_value *val = NULL;
    int32_t ret;
    uint64_t now = get_current_time_ms();

    eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) return;

    ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    key.ip_src = ipv4_hdr->src_addr;
    key.ip_dst = ipv4_hdr->dst_addr;
    key.proto = ipv4_hdr->next_proto_id;

    if (key.proto == IPPROTO_TCP) {
        tcp_hdr = (struct rte_tcp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
        key.port_src = tcp_hdr->src_port;
        key.port_dst = tcp_hdr->dst_port;
    } else if (key.proto == IPPROTO_UDP) {
        udp_hdr = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
        key.port_src = udp_hdr->src_port;
        key.port_dst = udp_hdr->dst_port;
    }

    // Canonicalize key for bidirectional tracking
    if (key.ip_src > key.ip_dst || (key.ip_src == key.ip_dst && key.port_src > key.port_dst)) {
        uint32_t temp_ip = key.ip_src; key.ip_src = key.ip_dst; key.ip_dst = temp_ip;
        uint16_t temp_port = key.port_src; key.port_src = key.port_dst; key.port_dst = temp_port;
    }

    ret = rte_hash_lookup_data(flow_table, &key, (void **)&val);
    if (ret < 0) {
        val = rte_zmalloc("flow_value", sizeof(struct flow_value), 0);
        if (!val) return;
        val->ndpi_flow = ndpi_flow_malloc(sizeof(struct ndpi_flow_struct));
        if (!val->ndpi_flow) { rte_free(val); return; }
        memset(val->ndpi_flow, 0, sizeof(struct ndpi_flow_struct));
        val->key = key;
        val->last_packet_time_ms = now;
        rte_hash_add_key_data(flow_table, &key, val);
    } else {
        val->last_packet_time_ms = now;
    }

    uint8_t *packet_data = (uint8_t *)ipv4_hdr;
    uint16_t packet_len = rte_pktmbuf_data_len(m) - sizeof(struct rte_ether_hdr);
    ndpi_protocol proto = ndpi_detection_process_packet(ndpi_info_mod, val->ndpi_flow, packet_data, packet_len, now, NULL);
    
    if (proto.proto.master_protocol != 0 || proto.proto.app_protocol != 0) {
        printf("[%u.%u.%u.%u:%u <-> %u.%u.%u.%u:%u] Protocol: %s\n",
               (key.ip_src >> 0) & 0xFF, (key.ip_src >> 8) & 0xFF, (key.ip_src >> 16) & 0xFF, (key.ip_src >> 24) & 0xFF, rte_be_to_cpu_16(key.port_src),
               (key.ip_dst >> 0) & 0xFF, (key.ip_dst >> 8) & 0xFF, (key.ip_dst >> 16) & 0xFF, (key.ip_dst >> 24) & 0xFF, rte_be_to_cpu_16(key.port_dst),
               ndpi_get_proto_name(ndpi_info_mod, proto.proto.app_protocol ? proto.proto.app_protocol : proto.proto.master_protocol));
    }

    cleanup_expired_flows();
}

static __rte_noreturn void lcore_main(void) {
    uint16_t port;
    printf("\nCore %u processing packets. [Ctrl+C to quit]\n", rte_lcore_id());
    for (;;) {
        RTE_ETH_FOREACH_DEV(port) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
            if (unlikely(nb_rx == 0)) { cleanup_expired_flows(); continue; }
            for (int i = 0; i < nb_rx; i++) {
                process_packet(bufs[i]);
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    ndpi_info_mod = ndpi_init_detection_module(NULL);
    if (!ndpi_info_mod) rte_exit(EXIT_FAILURE, "Failed to init nDPI\n");
    ndpi_finalize_initialization(ndpi_info_mod);

    struct rte_hash_parameters hash_params = {
        .name = "flow_table",
        .entries = MAX_FLOWS,
        .key_len = sizeof(struct flow_key),
        .hash_func = rte_jhash,
        .hash_func_init_val = 0,
        .socket_id = (int)rte_socket_id(),
    };
    flow_table = rte_hash_create(&hash_params);
    if (!flow_table) rte_exit(EXIT_FAILURE, "Cannot create flow hash table\n");

    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    uint16_t portid;
    RTE_ETH_FOREACH_DEV(portid) {
        if (port_init(portid, mbuf_pool) != 0) rte_exit(EXIT_FAILURE, "Cannot init port %u\n", portid);
    }

    lcore_main();
    return 0;
}
