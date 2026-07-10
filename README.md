# DPDK + nDPI Real-Time Network Traffic Analysis Lab

![License](https://img.shields.io/github/license/RajaMuhammadAwais/dpdk-ndpi-traffic-lab)
![C](https://img.shields.io/badge/language-C-blue.svg)
![DPDK](https://img.shields.io/badge/DPDK-v23.11-orange)
![nDPI](https://img.shields.io/badge/nDPI-latest-green)
![Build Status](https://img.shields.io/badge/build-passing-brightgreen)

A high-performance network traffic analysis laboratory combining the speed of **DPDK** (Data Plane Development Kit) with the deep packet inspection capabilities of **nDPI**. This project provides a ready-to-use environment for classifying network protocols in real-time at line rate.

## 🚀 Overview

This lab demonstrates how to:
- Initialize the DPDK Environment Abstraction Layer (EAL).
- Capture packets using DPDK Poll Mode Drivers (PMD).
- Process IPv4 traffic through the nDPI protocol detection engine.
- Classify application-layer protocols (e.g., HTTP, TLS, DNS, SSH) with minimal latency.

## 🛠 Prerequisites

- **OS**: Ubuntu 24.04 LTS (Recommended)
- **CPU**: Support for SSE4.2 or higher (standard for modern Intel/AMD CPUs)
- **Privileges**: Root/Sudo access for hardware and memory management

## 📦 Installation

We provide an automated setup script that handles all dependencies, clones the necessary repositories, and builds the libraries.

```bash
git clone https://github.com/RajaMuhammadAwais/dpdk-ndpi-traffic-lab.git
cd dpdk-ndpi-traffic-lab
sudo chmod +x setup.sh
sudo ./setup.sh
```

### Manual Steps
If you prefer manual installation, refer to the [detailed guide](./DPDK%20+%20nDPI%20Real-Time%20Network%20Traffic%20Analysis%20Lab.md).

## 🚦 Usage

Once the setup is complete, you can run the application. By default, it uses the PCAP virtual device to capture traffic from a standard Linux interface (e.g., `eth0`).

```bash
sudo ./main -l 0 --vdev=net_pcap0,iface=eth0 --no-huge --file-prefix=lab1
```

### Arguments:
- `-l 0`: Run the processing loop on CPU core 0.
- `--vdev=net_pcap0,iface=eth0`: Create a virtual device linked to `eth0`.
- `--no-huge`: Disable hugepage requirement (ideal for VMs/testing).
- `--file-prefix=lab1`: Unique identifier for this DPDK instance.

## 📊 Project Structure

- `main.c`: Core application logic (DPDK initialization & nDPI integration).
- `setup.sh`: Automated environment configuration script.
- `RESEARCH.md`: Deep dive into latest advancements (2024-2026) and contribution roadmap.
- `Makefile`: Optimized build configuration using `pkg-config`.
- `DPDK + nDPI...md`: Comprehensive educational guide.

## 🔬 Research & Contributions

This project is actively updated with the latest research in high-performance networking. See [RESEARCH.md](./RESEARCH.md) for details on:
- **nDPI 5.0** Unified Fingerprinting.
- **High-Performance Flow Table**: Implemented using DPDK's `rte_hash` (Cuckoo Hash) for efficient stateful packet processing.
- **DPDK 24.11/25.11** Hardware Flow Offloading.
- **Performance Analysis**: See [PERFORMANCE.md](./PERFORMANCE.md) for a detailed analysis of the Cuckoo Hash implementation.


## 🤝 Contributing

Contributions are welcome! If you'd like to improve the flow management, add a web dashboard, or support physical NIC binding:
1. Fork the repository.
2. Create a feature branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'Add AmazingFeature'`).
4. Push to the branch (`git push origin feature/AmazingFeature`).
5. Open a Pull Request.

## 📜 License

Distributed under the MIT License. See `LICENSE` for more information.

---
*Disclaimer: This project is intended for educational and research purposes in controlled lab environments.*
