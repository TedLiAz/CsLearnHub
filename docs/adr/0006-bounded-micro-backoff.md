# Bounded Adaptive Micro-Backoff over System Call Yields

To prevent catastrophic P99 tail-latency inflation while reducing bus contention during empty-queue spin-waits, we reject operating system yields (`sched_yield` or `nanosleep`) and unconstrained exponential backoffs. Instead, we constrain backoff exclusively to CPU `_mm_pause` sequences scaling exponentially from 1 to a hardware-bounded ceiling of 8 iterations, resetting to zero immediately upon packet receipt to guarantee sub-microsecond responsiveness.

