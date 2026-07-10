# Performance Analysis: Cuckoo Hash vs. Per-Packet Allocation

This document analyzes the expected performance impact of integrating a **Cuckoo Hash**-based flow table (using DPDK's `rte_hash`) compared to the previous per-packet allocation method for `ndpi_flow_struct` in the DPDK + nDPI lab application.

## 1. Introduction

Network traffic analysis at high speeds (10 Gbps and beyond) demands efficient packet processing and state management. Deep Packet Inspection (DPI) libraries like nDPI require maintaining flow state to accurately identify protocols that span multiple packets. The initial implementation of this lab allocated a new `ndpi_flow_struct` for every incoming packet, leading to significant overhead. The introduction of a Cuckoo Hash flow table aims to address this by providing a high-performance mechanism to store and retrieve flow-specific state.

## 2. Benchmarking Methodologies for DPDK/nDPI

Performance evaluation in DPDK and nDPI environments typically focuses on several key metrics [1] [2]:

- **Throughput**: Measured in packets per second (pps) or bits per second (bps), indicating the maximum rate at which the system can process traffic without dropping packets.
- **Latency**: The time taken for a packet to traverse the system, from ingress to egress or from capture to classification result.
- **CPU Cycles per Packet (CPP)**: A fine-grained metric that quantifies the computational cost of processing a single packet. Lower CPP indicates higher efficiency.
- **Memory Usage**: The amount of RAM consumed by the application, particularly for flow tables and packet buffers.
- **Detection Accuracy**: For DPI, this involves verifying that protocols are correctly identified, especially for complex or encrypted flows.

Tools commonly used for benchmarking include DPDK's `pktgen-dpdk` for traffic generation, `perf` for CPU profiling, and custom application-level counters.

## 3. Expected Performance Impact of Cuckoo Hash

The Cuckoo Hash implementation is expected to provide substantial performance improvements over the stateless, per-packet allocation approach. This is primarily due to the efficient management and reuse of flow state.

### 3.1. CPU Cycle Consumption

**Previous Method (Per-Packet Allocation)**:
- For every IPv4 packet, `ndpi_flow_malloc` and `memset` were called to allocate and initialize a new `ndpi_flow_struct`.
- After processing, `ndpi_flow_free` was called to deallocate the structure.
- These memory allocation/deallocation operations are computationally expensive and introduce significant overhead, especially under high packet rates.

**Cuckoo Hash Method (Stateful)**:
- For established flows, the `ndpi_flow_struct` is allocated only once when the flow is first seen and stored in the `rte_hash` table.
- Subsequent packets belonging to the same flow involve an `rte_hash_lookup_data` operation, which offers an average-case **O(1)** time complexity [3].
- This eliminates the repeated overhead of `malloc`/`free` for the majority of packets in a continuous flow.
- **Expected Impact**: Significant reduction in CPU cycles per packet, leading to higher overall throughput and lower latency.

### 3.2. Memory Usage

**Previous Method**:
- While `ndpi_flow_struct` instances were short-lived, the constant allocation and deallocation could lead to memory fragmentation and increased system call overhead.

**Cuckoo Hash Method**:
- The `rte_hash` table itself consumes memory to store flow keys and pointers to `flow_value` structures (which contain `ndpi_flow_struct`).
- The `MAX_FLOWS` constant (currently 65536) directly impacts the memory footprint of the hash table.
- **Expected Impact**: A fixed memory overhead for the hash table, plus memory for active `ndpi_flow_struct` instances. While the total memory footprint might be higher for a large number of concurrent flows, it is more predictable and avoids the dynamic allocation/deallocation thrashing of the previous method.

### 3.3. Throughput and Latency

- **Expected Impact**: By reducing CPU cycles per packet and providing efficient state retrieval, the Cuckoo Hash implementation is expected to increase the overall packet processing throughput and reduce latency, especially for long-lived flows. This aligns with the requirements for high-speed network analysis [4].

### 3.4. nDPI Detection Accuracy

- **Expected Impact**: The stateful nature of the Cuckoo Hash implementation allows nDPI to maintain a continuous context for each flow. This is critical for accurate protocol detection, as many application-layer protocols (e.g., HTTP, TLS) require inspecting multiple packets within a flow to be correctly identified. The previous stateless approach would struggle with such protocols, potentially leading to lower accuracy or delayed detection.

## 4. Future Work: Practical Benchmarking

To quantitatively validate these expected impacts, the following practical benchmarking steps are recommended:

1.  **Traffic Generation**: Use `pktgen-dpdk` to generate various traffic patterns (e.g., short-lived flows, long-lived flows, mixed traffic) at different rates.
2.  **Profiling**: Utilize DPDK's built-in statistics and Linux `perf` tools to measure CPU cycles per packet, cache misses, and memory access patterns.
3.  **Comparison**: Run the application with both the original (for baseline, if feasible) and the Cuckoo Hash implementations and compare the measured metrics.
4.  **Scalability Testing**: Evaluate performance with increasing numbers of concurrent flows and different core configurations.

## 5. References

1.  [nDPI packet processing performance question · Issue #2784 - GitHub](https://github.com/ntop/nDPI/issues/2784)
2.  [Using nDPI over DPDK to Classify and Block Unwanted Network Traffic - Luca Deri, ntop (PDF)](http://luca.ntop.org/gr2021/altre_slides/nDPI_DPDK_122018.pdf)
3.  [2. Hash Library - DPDK Documentation](https://doc.dpdk.org/guides/prog_guide/hash_lib.html)
4.  [A Hardware-Accelerated Intrusion Prevention System for Attack Mitigation Using DPUs (IEEE INFOCOM 2025)](https://ieeexplore.ieee.org/abstract/document/11152916/)
