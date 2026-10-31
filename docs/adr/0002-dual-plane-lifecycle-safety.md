# Dual-Plane Lifecycle Safety with In-Band Sentinel and Out-of-Band QSBR

In strict FIFO pipeline stages, DrainSentinel deterministically proves queue drainage without needing epoch grace periods for in-pipeline workers. We retain QSBR solely to protect out-of-band references (such as timer-driven flow sweeps, administrative inspection, and cross-core hash table traversals) from accessing freed context memory, with target-epoch advance strictly preceding quarantine reclamation checks.

