# Single-Queue Drain Barrier via RSS Flow Affinity

Rather than broadcasting drain sentinels across all worker queues and contending on atomic reduction barriers, flows strictly observe RSS hash affinity to single worker cores. Drain sentinels are injected solely into the target flow's designated pipeline queue, falling back to multi-queue aggregation only in non-affinity round-robin topologies.

