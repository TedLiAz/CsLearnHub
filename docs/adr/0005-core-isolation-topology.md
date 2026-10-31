# Core Isolation Topology with SMT Exclusion

To eliminate LLC thrashing and thread migration across multi-stage lock-free pipelines without causing compute starvation, we pin data-plane stages (Ingress, Worker, Terminal) to dedicated physical CPU cores sharing an L3 cache on the same NUMA node, while strictly excluding SMT sibling hyperthreads. Slow-path control threads yield dedicated cores and share a management core to allow graceful execution on platforms with fewer than 4 physical cores.

