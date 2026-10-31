# Heavyweight Frameworks and High-Level Runtimes

This project does not adopt heavyweight third-party runtime frameworks (e.g., Boost, large RPC middleware, or full-featured container runtimes) for core systems algorithms.

## Why this is out of scope

The mission of CsLearnHub is educational transparency and mechanical sympathy at the bare-metal and Linux kernel interface:

- **Visibility of Hardware Realities**: Every lab is structured to reveal exact CPU cache line dynamics, hardware memory barriers, branch predictor impacts, and OS page table interactions.
- **Minimal Dependencies**: Implementations strictly rely on ISO C11 (`stdatomic.h`, `threads.h` or POSIX pthreads) and Linux standard interfaces to ensure zero hidden allocation layers and immediate reproducibility.

