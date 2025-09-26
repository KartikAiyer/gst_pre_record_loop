# Data Model: Pre-Record Loop Baseline

## Entities

### QueueItem
| Field | Type | Description |
|-------|------|-------------|
| type | enum { BUFFER, EVENT } | Distinguishes stored object kind |
| mini_object | GstMiniObject* | Underlying buffer or event (owned until release/push) |
| pts | GstClockTime | Presentation timestamp (GST_CLOCK_TIME_NONE allowed) |
| dts | GstClockTime | Decode timestamp if provided |
| duration | GstClockTime | Buffer duration (0 if unknown) |
| keyframe | gboolean | TRUE if buffer starts a GOP (not delta) |
| gop_id | guint | GOP identifier (increment on keyframe) |
| size_bytes | gsize | Cached memory size (approx; optional) |
| is_gap | gboolean | TRUE if represents a GAP event |

### RingBuffer
| Field | Type | Description |
|-------|------|-------------|
| deque | GstVecDeque* | Container of QueueItem entries |
| current_duration | GstClockTime | Sum of durations (approx; may accumulate delta) |
| max_time_sec | guint | Configured capacity (seconds) |
| adaptive_floor | guint | Constant 2 (minimum GOPs) |
| gop_count | guint | Number of distinct GOPs currently stored |
| head_gop_id | guint | Oldest GOP id present |
| tail_gop_id | guint | Most recent GOP id |

### StateMachine
| Field | Type | Description |
|-------|------|-------------|
| mode | enum { BUFFERING, PASS_THROUGH } | Current operational mode |
| draining | gboolean | TRUE while flushing due to `prerecord-flush` |
| rearm_pending | gboolean | Future use (not set when manual re-arm done immediately) |

### Properties
| Name | Type | Default | Description |
|------|------|---------|-------------|
| max-time | guint (seconds) | 10 | Max buffered duration (soft with 2-GOP floor) |
| flush-on-eos | enum { auto, always, never } | auto | EOS flushing policy |
| silent | gboolean | FALSE | Suppress info-level logging |

## Relationships
- QueueItem instances are owned by RingBuffer until either dropped (unref) or pushed downstream (ownership transfer to pad).
- StateMachine governs RingBuffer mutation policies (dropping only allowed in BUFFERING mode).

## Invariants
1. GOP IDs strictly increase; no reuse.
2. No partial GOP at emission start after flush.
3. While MODE=PASS_THROUGH, new buffers are forwarded immediately and not enqueued.
4. If `gop_count < adaptive_floor` pruning is skipped.
5. `current_duration` floored to integral seconds prior to capacity comparison.

## Derived Behaviors
- Pruning Trigger: On enqueue of keyframe or buffer causing `current_duration > max_time_sec` and `gop_count >= adaptive_floor`.
- Concurrent Flush: `draining` prevents new flush logic re-entry.
- Re-Arm: On `prerecord-arm`, mode set to BUFFERING only if currently PASS_THROUGH and not draining.

## Open Modeling Considerations (Deferred)
- Potential future per-GOP structure for faster aggregate drop metrics.
- Memory accounting improvements using GstBuffer pool stats.
