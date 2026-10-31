# Hot-Cold Separated Flow Table with Dense Generation Array

To preserve L1/L2 cache locality across 64K+ concurrent network flows on multi-stage forwarding pipelines, we reject padding each flow entry to a full 64-byte cache line. Instead, we pack high-frequency read-mostly metadata (`generation` and `state`) into a contiguous Dense Generation Array (16 flows per cache line), while isolating mutable context pointers in a separate cold table accessed only upon successful handle validation.

