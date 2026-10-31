# High-Performance Lock-Free Data Plane

The domain model and shared vocabulary for lock-free network pipeline concurrency, dynamic resource lifecycle management, and hardware cache efficiency in multi-stage packet processing architectures.

## Language

**FlowHandle**:
A compact 64-bit value handle composed of an index and a generation counter carried by packets instead of raw pointers.
_Avoid_: Flow pointer, connection descriptor

**Generation**:
A monotonically increasing integer associated with a flow slot that invalidates stale handles upon retirement.
_Avoid_: Version, epoch tag

**DrainSentinel**:
An in-band, lossless marker injected into a FIFO ring buffer to demarcate the termination of packet processing for a specific flow generation.
_Avoid_: Flush packet, drain marker, barrier token

**Control Ring**:
A dedicated lock-free single-producer single-consumer ring buffer through which the control plane asynchronously submits lifecycle events to an ingress core without polling or data-plane interruption.
_Avoid_: Command bus, signal mask

**Dense Generation Array**:
A cache-optimized, contiguous array storing only hot generation counters and state flags packed to fit multiple flows per cache line.
_Avoid_: FCB array, monolithic flow table

**Quarantine Pool**:
A delayed-reclamation FIFO buffer that retains retired flow contexts until all reader threads confirm passage through a quiescent state.
_Avoid_: Trash ring, purgatory, deferred queue

**Quiescent State**:
An observable execution point during which a processing thread holds no register or transient references to shared flow contexts.
_Avoid_: Idle state, sync point

**Core Isolation Topology**:
The pinned allocation of pipeline worker threads to dedicated physical CPU cores sharing an L3 cache while strictly avoiding SMT sibling threads.
_Avoid_: Thread affinity map, CPU pinning

**Bounded Adaptive Backoff**:
A spin-wait throttling mechanism that exponentially scales CPU pause instructions up to a strict hardware limit (max 8 pauses) upon empty queue dequeues without invoking OS yields.
_Avoid_: Yield spin, thread sleep, exponential backoff

**Burst Forwarding**:
The batched enqueuing of processed packet envelopes across pipeline stages with mandatory pre-flush semantics before forwarding any control sentinel.
_Avoid_: Batch enqueuing, packet bunching

