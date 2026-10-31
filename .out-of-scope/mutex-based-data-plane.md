# Mutex-Based Synchronization in the Data Plane

This project does not implement or accept mutex-based locking primitives for hot-path data-plane processing.

## Why this is out of scope

CsLearnHub focuses on mechanical sympathy, lock-free concurrency, and kernel-bypass principles. In high-performance data planes (e.g., DPDK-style packet pipelines, telemetry ingestion):

- **Context Switching Overhead**: A contended `pthread_mutex` causes thread descheduling and OS context switches, costing microseconds where budgets are measured in nanoseconds.
- **Priority Inversion & Cache Bouncing**: Mutex locks create cache line bouncing across cores, invalidating L1/L2 caches and introducing severe tail latency.
- **Predictable Progress**: We enforce lock-free, bounded-wait algorithms (SPSC rings, atomic generation counters, and Quiescent-State-Based Reclamation / QSBR).

Mutexes and condition variables are restricted strictly to offline administrative paths (such as thread setup and tear-down). Any pull request or issue proposing mutexes on the packet/message processing path will be closed as out of scope.

