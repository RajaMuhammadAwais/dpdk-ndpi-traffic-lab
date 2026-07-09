# DPDK + nDPI Real-Time Network Traffic Analysis Lab

This guide provides a step-by-step procedure to set up a high-performance network traffic analysis lab using **DPDK** (Data Plane Development Kit) and **nDPI** (Deep Packet Inspection library) on Ubuntu.

## 1. Environment Preparation

### Prerequisites
- Ubuntu 24.04 (Noble Numbat)
- CPU with DPDK support (Intel/AMD)
- Root privileges

### Install Build Dependencies
```bash
sudo apt update
sudo apt install -y build-essential meson ninja-build pkg-config \
    libnuma-dev python3-pyelftools libpcap-dev libtool \
    autoconf automake git
```

## 2. DPDK Installation (v23.11)

DPDK is the foundation for fast packet processing in user space.

```bash
git clone --depth 1 --branch v23.11 https://github.com/DPDK/dpdk.git ~/dpdk
cd ~/dpdk
meson setup build
ninja -C build
sudo ninja -C build install
sudo ldconfig
```

## 3. nDPI Installation

nDPI provides the protocol detection engine.

```bash
git clone https://github.com/ntop/nDPI.git ~/ndpi
cd ~/ndpi
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
```

## 4. Lab Application Development

The lab application uses DPDK to capture packets and nDPI to classify them in real-time.

### `main.c` (Core Logic)
The application initializes DPDK EAL, sets up a virtual device (PCAP-based for testing), and passes captured IPv4 packets to the nDPI engine.

### `Makefile`
Uses `pkg-config` to automatically find DPDK and nDPI libraries and headers.

```makefile
PKGCONF = pkg-config
CFLAGS += -O3 $(shell $(PKGCONF) --cflags libdpdk libndpi)
LDFLAGS += $(shell $(PKGCONF) --libs libdpdk libndpi)

main: main.c
	gcc $(CFLAGS) -o $@ $< $(LDFLAGS)
```

## 5. Compilation and Execution

### Build the Lab
```bash
cd ~/dpdk_ndpi_lab
make
```

### Run the Lab
To test on a standard Ethernet interface (e.g., `eth0`), use the DPDK PCAP virtual device:

```bash
sudo ./main -l 0 --vdev=net_pcap0,iface=eth0 --no-huge --file-prefix=lab1
```

- `-l 0`: Run on core 0.
- `--vdev=net_pcap0,iface=eth0`: Use `eth0` via the PCAP PMD.
- `--no-huge`: Run without hugepages (useful for virtualized environments).
- `--file-prefix=lab1`: Unique prefix for DPDK runtime files.

## 6. Troubleshooting

| Problem | Solution |
|---------|----------|
| `EAL: FATAL: Cannot init config` | Another DPDK process is running. Use a different `--file-prefix`. |
| `Cannot create mbuf pool` | Not enough memory. Check free memory or reduce `NUM_MBUFS`. |
| `Unknown type name NDPI_PROTOCOL_BITMASK` | nDPI API has changed. Check `/usr/include/ndpi/ndpi_typedefs.h` for the correct type. |
| `tun_alloc(): Unable to open /dev/net/tun` | Missing permissions or kernel module. Use `sudo` and ensure `tun` module is loaded. |

## 7. Next Steps
- **Flow Management**: Implement a hash table to track flows instead of allocating a new flow per packet.
- **Statistics**: Add a real-time console dashboard using `ncurses` or export data to Prometheus/Grafana.
- **Hardware Acceleration**: Bind physical NICs to DPDK (using `vfio-pci`) for multi-gigabit performance.
