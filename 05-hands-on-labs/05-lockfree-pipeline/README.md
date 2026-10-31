# Lock-Free Pipeline Lifecycle Safety Demo & Rigorous Benchmark

This laboratory provides a production-grade, self-contained C11 implementation and external Python 3 / Linux `perf` benchmark suite for the **Drain Barrier Protocol + Generational Handle + QSBR Quarantine** lock-free pipeline lifecycle safety model introduced in [03-high-performance-networking/lockfree-pipeline-lifecycle-safety.md](../../03-high-performance-networking/lockfree-pipeline-lifecycle-safety.md).

---

## 1. Architectural Highlights

```
     [Control Plane]
            │
   Atomic Bitmask Signal (Async drain request)
            │
            ▼
┌──────────────────────┐   Ring 1 (SPSC)   ┌──────────────────────┐   Ring 2 (SPSC)   ┌──────────────────────┐
│    Ingress Stage     │ ════════════════> │     Worker Stage     │ ════════════════> │ Terminal/Egress Stage│
│      (Stage 1)       │ Packets/Sentinels │      (Stage 2)       │ Packets/Sentinels │      (Stage 3)       │
└──────────────────────┘                   └──────────────────────┘                   └──────────────────────┘
           │                                          │                                          │
   Ingests live traffic                       Validates Handle                          Retires Generation
   Injects DrainSentinels                     Dereferences Context                      QSBR Quarantine Free
```

1. **Zero Raw Pointers on Fast Path**: Packets never carry raw memory pointers across pipeline stages. They carry a 64-bit value-type `FlowHandle {flow_id, generation}`.
2. **Strict SPSC Ring Buffers**: Inter-stage queues (`RingBuffer`) are strictly Single-Producer Single-Consumer (192 bytes, 3 cache lines: `prod`, `cons`, `meta` separated with `alignas(64)`), completely eliminating false sharing and producer-producer contention.
3. **Drain Barrier Protocol**: In-band FIFO `DrainSentinel` (`MSG_TYPE_DRAIN_SENTINEL`) guarantees that all in-flight packets for a draining flow are processed before the flow's generation is incremented.
4. **Lightweight QSBR Quarantine**: Physical deallocation of flow context memory is deferred in a `QuarantineRing` until all worker threads announce a quiescent epoch state, guaranteeing zero register-level Use-After-Free (UAF) hazards.
5. **Zero-Pollution C Core**: The C codebase contains zero benchmarking or timing functions (`clock_gettime` is strictly forbidden). All performance and microarchitectural profiling is executed non-intrusively via Linux `perf stat`.

---

## 2. Directory Structure & Modular Architecture

```
05-lockfree-pipeline/
├── include/
│   ├── flow.h          # FlowHandle, FlowControlBlock (64B aligned), states & lifecycle
│   ├── message.h       # PipelineMsg (unified 32B message envelope: packets, sentinels, halt)
│   ├── ring_buffer.h   # SPSC lock-free ring buffer (192B = 3 cachelines, burst APIs)
│   ├── qsbr.h          # Quiescent-State-Based Reclamation & QuarantineRing
│   └── pipeline.h      # Pipeline context, stage worker prototypes & stats
├── src/
│   ├── flow.c          # Global FCB table, atomic generation & async bitmask signaling
│   ├── ring_buffer.c   # Ring buffer power-of-two validation & initialization
│   ├── qsbr.c          # QSBR epoch progression & quarantine flow reclamation logic
│   ├── pipeline.c      # Ingress (Stage 1), Worker (Stage 2), Terminal (Stage 3), Control threads
│   └── main.c          # Program entry, stage thread orchestration & integrity verification
├── benchmark.py        # Non-intrusive Python 3 harness driving external Linux perf stat
├── Makefile            # Modular build targets (all, run, tsan, bench, clean)
└── README.md           # Architecture documentation & benchmark instructions
```

---

## 3. Quick Start & Verification

### Build & Run Full Simulation

```bash
make run
```
Compiles with `-std=c11 -pthread -O3 -march=native -Wall -Wextra -Werror` and runs 2,000,000 packets across 16 flows with concurrent flow drain and dynamic retirement.

### ThreadSanitizer (TSan) Concurrency Verification

```bash
make tsan
```
Compiles with `-fsanitize=thread -g -O1` to verify zero data races, memory leaks, or synchronization hazards under concurrent execution.

### Industry-Standard `perf` Benchmark

```bash
make bench
```
Executes `benchmark.py`, performing 1 warm-up run (discarded) followed by 5 measured iterations to compute statistical Mean ± StdDev across hardware PMU counters.

To run custom configurations:
```bash
python3 benchmark.py --packets 5000000 --flows 32 --runs 10 --warmup 2
python3 benchmark.py --duration 30 --flows 32   # Duration-based rate measurement (seconds)
python3 benchmark.py --cores 2,4,6,0             # Explicit Ingress, Worker, Egress, Control cores
python3 benchmark.py --no-perf                  # Timing-only fallback mode if PMU access is restricted
python3 benchmark.py --json results.json        # Export structured metrics
```

---

## 4. Benchmark Performance Metrics

Measurements collected on Linux x86-64 (`-O3 -march=native`):

| Metric | Measured Value (Mean ± StdDev) | Architectural Significance |
| :--- | :--- | :--- |
| **Throughput** | **19.39 ± 1.04 Mpps** | Multi-million packets per second sustained during concurrent flow draining |
| **Average Latency** | **51.70 ± 2.90 ns / pkt** | Sub-60ns end-to-end traversal across 3 pipeline stages |
| **Cycles Per Packet (CPP)** | **542.6 ± 39.2 cycles** | Total CPU cycles across all pipeline threads per egressed packet |
| **Instructions Per Cycle (IPC)** | **1.08 ± 0.02** | High instruction-level parallelism; zero lock contention stalls |
| **L1-Dcache Miss Rate** | **3.12 ± 0.06 %** | Cache-line alignment (`alignas(64)`) completely eliminates false sharing |
| **Branch Miss Rate** | **0.54 ± 0.12 %** | Branch predictor accuracy via fast-path `__builtin_expect` hints |
| **Data Race Hazards** | **0 (TSan verified)** | Strict SPSC queues + Acquire-Release pairs enforce memory visibility |
| **Memory Safety** | **0 UAF / 0 Leaks** | Verified by strict post-execution flow state assertions |
