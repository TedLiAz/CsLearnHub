# Lossless Signaling Priority via Fast-Path Tail Drop

When the ingress pipeline queue and local spillover buffer reach saturation under sustained backpressure, the data plane actively tail-drops incoming best-effort data packets to immediately carve out queue slots for in-band DrainSentinels. Control signal losslessness strictly takes precedence over best-effort packet delivery to prevent deadlocking flow retirement.

