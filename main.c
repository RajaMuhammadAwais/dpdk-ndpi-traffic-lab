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

#define MAX_FLOWS 65536 // Maximum number of concurrent flows

// Structure to define the key for our flow table (5-tuple)
struct flow_key {
    uint32_t ip_src;
    uint32_t ip_dst;
    uint16_t port_src;
    uint16_t port_dst;
    uint8_t  proto;
};

// Structure to store flow-related data
struct flow_value {
    struct ndpi_flow_struct *ndpi_flow;
    uint64_t last_packet_time;
    // Add other flow stats here if needed
};

static struct ndpi_detection_module_struct *ndpi_info_mod = NULL;
static struct rte_hash *flow_table = NULL;

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

// Custom hash function for flow_key (optional, but good for performance)
static inline uint32_t
flow_key_hash(const void *key, uint32_t key_len, uint32_t init_val)
{
    return rte_hash_crc(key, key_len, init_val);
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
    uint8_t *packet_data;
    uint16_t packet_len;
    struct flow_key current_flow_key;
    struct flow_value *current_flow_value = NULL;
    int32_t ret;

    eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    
    if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
        return;
    }

    ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    
    // Populate flow key
    current_flow_key.ip_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    current_flow_key.ip_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
    current_flow_key.proto = ipv4_hdr->next_proto_id;

    if (current_flow_key.proto == IPPROTO_TCP) {
        tcp_hdr = (struct rte_tcp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
        current_flow_key.port_src = rte_be_to_cpu_16(tcp_hdr->src_port);
        current_flow_key.port_dst = rte_be_to_cpu_16(tcp_hdr->dst_port);
    } else if (current_flow_key.proto == IPPROTO_UDP) {
        udp_hdr = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
        current_flow_key.port_src = rte_be_to_cpu_16(udp_hdr->src_port);
        current_flow_key.port_dst = rte_be_to_cpu_16(udp_hdr->dst_port);
    } else {
        current_flow_key.port_src = 0;
        current_flow_key.port_dst = 0;
    }

    // Look up flow in hash table
    ret = rte_hash_lookup_data(flow_table, (const void *)&current_flow_key, (void **)&current_flow_value);

    if (ret < 0) { // Flow not found, create new
        struct ndpi_flow_struct *ndpi_flow = ndpi_flow_malloc(sizeof(struct ndpi_flow_struct));
        if(!ndpi_flow) return;
        memset(ndpi_flow, 0, sizeof(struct ndpi_flow_struct));

        current_flow_value = (struct flow_value *)malloc(sizeof(struct flow_value));
        if(!current_flow_value) {
            ndpi_flow_free(ndpi_flow);
            return;
        }
        current_flow_value->ndpi_flow = ndpi_flow;
        current_flow_value->last_packet_time = rte_get_tsc_cycles() * 1000 / rte_get_tsc_hz();

        ret = rte_hash_add_key_data(flow_table, (const void *)&current_flow_key, (void *)current_flow_value);
        if (ret < 0) {
            printf("Failed to add flow to hash table\n");
            ndpi_flow_free(ndpi_flow);
            free(current_flow_value);
            return;
        }
    } else { // Flow found, update timestamp
        current_flow_value->last_packet_time = rte_get_tsc_cycles() * 1000 / rte_get_tsc_hz();
    }

    packet_data = (uint8_t *)ipv4_hdr;
    packet_len = rte_pktmbuf_data_len(m) - sizeof(struct rte_ether_hdr);

    uint64_t time_ms = rte_get_tsc_cycles() * 1000 / rte_get_tsc_hz();

    ndpi_protocol proto = ndpi_detection_process_packet(ndpi_info_mod, current_flow_value->ndpi_flow, packet_data, packet_len, time_ms, NULL);
    
    if (proto.proto.master_protocol != 0 || proto.proto.app_protocol != 0) {
        printf("Protocol: %s | Master: %s\n", 
               ndpi_get_proto_name(ndpi_info_mod, proto.proto.app_protocol),
               ndpi_get_proto_name(ndpi_info_mod, proto.proto.master_protocol));
    }

    // ndpi_flow_free(ndpi_flow); // This is now handled by flow aging/cleanup (not implemented yet)
}

static __rte_noreturn void lcore_main(void) {
    uint16_t port;
    printf("\nCore %u processing packets. [Ctrl+C to quit]\n", rte_lcore_id());

    for (;;) {
        RTE_ETH_FOREACH_DEV(port) {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
            if (unlikely(nb_rx == 0)) continue;

            for (int i = 0; i < nb_rx; i++) {
                process_packet(bufs[i]);
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    struct rte_mempool *mbuf_pool;
    uint16_t portid;

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    // Initialize nDPI with default protocols (all enabled by default in init)
    ndpi_info_mod = ndpi_init_detection_module(NULL);
    if (ndpi_info_mod == NULL) rte_exit(EXIT_FAILURE, "Failed to init nDPI\n");
    
    // Finalize nDPI initialization
    ndpi_finalize_initialization(ndpi_info_mod);

    // Initialize flow table
    struct rte_hash_parameters hash_params = {
        .name = "flow_table",
        .entries = MAX_FLOWS,
        .key_len = sizeof(struct flow_key),
        .hash_func = flow_key_hash,
        .hash_func_init_val = 0,
        .socket_id = rte_socket_id(),
    };
    flow_table = rte_hash_create(&hash_params);
    if (flow_table == NULL) rte_exit(EXIT_FAILURE, "Cannot create flow hash table\n");

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    RTE_ETH_FOREACH_DEV(portid) {
        if (port_init(portid, mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", portid);
    }

    lcore_main();

    ndpi_exit_detection_module(ndpi_info_mod);
    rte_eal_cleanup();
    return 0;
}
