# Feature Specification: Pre-Record Loop Baseline

**Feature Branch**: `000-prerecordloop-baseline`  
**Created**: 2025-09-24  
**Status**: Draft (Reverse-engineered)  
**Input**: User description: "Baseline spec extracted from existing README, constitution v1.0.0, and current C implementation"

## Clarifications
### Session 2025-09-25
- Q: What should happen if a single GOP's duration exceeds the configured max-time? → A: Adaptive min: 2 GOPs
- Q: After flush, how can it re-enter buffering? → A: Manual re-arm via event
- Q: If a flush trigger arrives during an active flush? → A: Ignore subsequent flush triggers
- Q: Required granularity for max-time property? → A: Seconds (integer)
- Q: Default value/behavior for flush-on-eos? → A: Auto (flush only if PASS_THROUGH)

*Policy Applied*: The effective time capacity dynamically expands to always accommodate at least two complete GOPs. Enforcement logic: if enforcing max-time would result in fewer than two full GOPs retained, pruning is deferred until a third GOP begins; thereafter oldest GOPs are dropped while ensuring a floor of two complete GOPs remains. Metrics referencing "max-time" now interpreted as soft ceiling with a two-GOP preservation floor.

*Re-Arm Behavior*: Post-flush the element remains in PASS_THROUGH until it receives a custom upstream (or programmatically injected) event named `prerecord-arm` (to be documented). On receiving `prerecord-arm`, it clears any transient state (does NOT discard already forwarded live data) and transitions back to BUFFERING, reinitializing GOP tracking baseline.

*Concurrent Flush Handling*: While draining due to a `prerecord-flush` event, any additional `prerecord-flush` triggers are ignored (logged at debug level) to prevent redundant queue traversal or re-entrant state transitions. This preserves deterministic output order and avoids partial duplicate emission attempts.

*Max-Time Granularity*: The `max-time` property accepts integer seconds only; input values with sub-second resolution MUST be rounded down (floor) to the nearest whole second for capacity accounting to avoid off-by-small-delta pruning jitter.

*Flush-on-EOS Default*: The `flush-on-eos` behavior is AUTO by default: if the element is in PASS_THROUGH at EOS it flushes any residual buffered frames accumulated post-flush transition before forwarding EOS; if still in BUFFERING at EOS (never flushed), it forwards EOS immediately without emitting buffered data (implicit data privacy / incomplete context protection).

## User Scenarios & Testing *(mandatory)*

### Primary User Story
As an application that records video around motion or external trigger events, I need a pipeline element that continuously buffers encoded video so that when an event occurs I can also capture several seconds of video that happened before the event.

### Acceptance Scenarios
1. **Given** the element is in BUFFERING mode and receives a flush trigger event, **When** the trigger is processed, **Then** the element MUST first output all buffered GOP-aligned frames in order and then forward live incoming frames.
2. **Given** the buffered data exceeds the configured time capacity and at least two full GOPs are already buffered, **When** a new frame arrives, **Then** the element MUST drop the oldest complete GOP(s) until total buffered duration is strictly below the time limit while preserving at least two full GOPs.
3. **Given** an oversized GOP alone exceeds configured max-time, **When** it is buffered, **Then** the element MUST retain that GOP and the immediately preceding (if present) so that at least two complete GOPs are available even if this temporarily exceeds max-time.
4. **Given** the element has flushed and is in PASS_THROUGH, **When** it receives a `prerecord-arm` event, **Then** it MUST transition back to BUFFERING and resume accumulation from the next incoming GOP.
5. **Given** upstream sends a SEGMENT event, **When** timestamps shift or seeking occurs, **Then** the element MUST update its internal timing so buffered duration accounting remains correct.
6. **Given** the pipeline reaches EOS while in PASS_THROUGH mode, **When** EOS propagates, **Then** the element MUST flush remaining buffered data (if any) before forwarding EOS downstream.
7. **Given** the element is currently draining due to a prior `prerecord-flush`, **When** another `prerecord-flush` event is received, **Then** the element MUST ignore the new trigger and continue the current drain only.
8. **Given** a user sets `max-time=7.9`, **When** buffering logic evaluates capacity, **Then** the effective limit MUST be treated as 7 seconds.
9. **Given** EOS occurs while still in BUFFERING (no flush ever triggered), **When** EOS is processed, **Then** the element MUST forward EOS without emitting buffered pre-event data.

### Edge Cases
- Trigger event arrives while already in PASS_THROUGH mode (SHOULD ignore or log at debug level).  
- Upstream sends only delta frames for a period (MUST delay start output until next keyframe when flushing).  
- Custom flush trigger received during state change (MUST serialize via internal mutex; behavior currently UNSPECIFIED).  
- Oversized single GOP exceeding max-time: preserve it (and prior GOP if present) — adaptive floor of two GOPs (CLARIFIED).  
- Rapid successive trigger events while flushing: subsequent triggers ignored (CLARIFIED).  
- Re-arm event received while already BUFFERING (SHOULD log and ignore).  
- Sub-second max-time input rounding: floor to integer seconds (CLARIFIED).  
- EOS while buffering (no flush yet): do not leak pre-event data (AUTO policy).  

## Requirements *(mandatory)*

### Functional Requirements (Updated for Time-Only Capacity)
- **FR-001**: Element MUST buffer incoming encoded video data while in BUFFERING mode.
- **FR-002**: Element MUST track GOP boundaries using keyframe identification and associate each buffer with a GOP id.
- **FR-003**: Element MUST enforce a single time-based capacity limit representing maximum buffered duration.
- **FR-004**: Element MUST output buffered data in presentation order upon receiving an external flush/trigger event, then switch to PASS_THROUGH mode.
- **FR-005**: Element MUST maintain zero-copy semantics (no unnecessary buffer duplication) using reference counting.
- **FR-006**: Element MUST handle standard GStreamer events: SEGMENT, CAPS, EOS, FLUSH_START/STOP, and custom trigger events.
- **FR-007**: Element MUST ensure thread-safe queue operations using a mutex and condition variables.
- **FR-008**: Element MUST drop oldest complete GOP(s) when time capacity would be exceeded to admit a new buffer, except when doing so would reduce retained GOPs below two complete GOPs (adaptive floor rule).
- **FR-009**: Element MUST preserve decoding integrity by never emitting partial GOPs at the start of output.
- **FR-010**: Element MUST provide configurable properties for max-time (integer seconds), silent logging mode, and flush-on-eos.
- **FR-011**: Element MUST forward downstream events while buffering so that state (segments, caps) remains synchronized.
- **FR-012**: Element MUST correctly propagate or store sticky events.
- **FR-013**: Element SHOULD expose documented custom downstream flush event name ("prerecord-flush") and upstream re-arm event name ("prerecord-arm").
- **FR-014**: Element MUST clean up all queued mini objects on finalize without leaks.
- **FR-015**: Element MUST avoid freeing internal queue node pointers returned by dequeue (BUG path slated for fix).
- **FR-016**: Mode transitions MUST be atomic with respect to queue manipulation.
- **FR-017**: Element SHOULD support optional automatic flush on EOS when configured (flush-on-eos property).
- **FR-018 (REMOVED)**: Multi-metric capacity (buffers/bytes) — intentionally dropped; future additions require new spec & version bump.
- **FR-019 (REMOVED)**: GstBufferList support — intentionally dropped; existing helper code scheduled for removal.
- **FR-020**: Element MUST transition back to BUFFERING only upon receiving a `prerecord-arm` event after a flush.
- **FR-021**: Element MUST ignore additional `prerecord-flush` triggers while an existing flush drain is in progress.
- **FR-022**: Element MUST floor sub-second max-time inputs to the nearest whole second for enforcement.
- **FR-023**: Default AUTO flush-on-eos policy MUST flush remaining buffered data only if element is already in PASS_THROUGH; otherwise buffered data is discarded and EOS forwarded.

### Ambiguities / Clarifications Needed (Updated)
- **CLAR-001**: (Resolved) Single GOP over max-time handled by adaptive two-GOP floor rule (see Clarifications).
- **CLAR-002**: (Resolved) Re-buffering requires explicit `prerecord-arm` event (manual re-arm); no automatic re-entry.
- **CLAR-003**: (Resolved) Concurrent flush triggers ignored during active drain.
- **CLAR-004**: (Resolved) Max-time granularity = integer seconds; floor sub-second inputs.
- **CLAR-005**: (Resolved) Default flush-on-eos = AUTO (conditional flush only in PASS_THROUGH).

## Implementation Note: Refcount / Lifecycle Integrity (Added 2025-09-27)

During test development a GStreamer refcount assertion (`gst_mini_object_unref: assertion '... > 0' failed`) surfaced
when flushing buffered data. Root cause analysis identified two ownership problems:

1. Manual invocation of `gst_pad_store_sticky_event()` for serialized sticky events while also forwarding the same
	 event to `gst_pad_event_default()` (double storage / over-unref hazard).
2. Enqueuing SEGMENT and GAP events without taking an additional reference before later queue management unrefs.

Resolution steps applied:
- Removed all manual sticky event storage; delegate solely to the default event handler.
- Added an explicit `gst_event_ref()` when queuing SEGMENT/GAP so the queue holds a distinct reference.
- Added (and later compile-gated) verbose lifecycle instrumentation (`PREREC_ENABLE_LIFE_DIAG`) for targeted
	diagnostics; default builds keep it disabled to reduce log noise.
- Introduced regression test `prerec_unit_no_refcount_critical` to assert absence of refcount CRITICAL conditions in a
	minimal pipeline scenario.

Operational guidance:
To re-enable deep lifecycle tracing for debugging future ownership issues, configure with
`-DPREREC_ENABLE_LIFE_DIAG=1` and run relevant unit tests under
`GST_DEBUG=GST_REFCOUNTING:7,prerec_lifecycle:7,prerec_dataflow:5`.

This note ensures future changes respect the invariant: the queue and downstream path must never share a sticky or
serialized event reference without explicit ref duplication and clearly defined single unref responsibility.

Build Option Summary:
`PREREC_ENABLE_LIFE_DIAG` is a standard CMake option (OFF by default) that injects `-DPREREC_ENABLE_LIFE_DIAG=1` into
the plugin target, enabling:
	- Allocation & use of lifecycle/sticky tracking hash tables
	- Verbose LIFE-* and STICKY-TRACK debug messages
	- Sequenced push/unref correlation (push_seq / unref_seq) for forensic analysis
When OFF, all tracking functions collapse to no-ops and the tables are not compiled in (zero runtime overhead).

## Runtime Introspection: Custom Stats Query (Added 2025-09-28)

To enable black-box verification of pruning and adaptive 2-GOP floor behavior without exposing internal symbols,
the element implements a custom upstream query structure named `prerec-stats`.

### Query Direction
Issued downstream → upstream on the element's `src` pad (standard query flow). A test or downstream element calls:

```
GstQuery *q = gst_query_new_custom (GST_QUERY_CUSTOM,
																		gst_structure_new_empty ("prerec-stats"));
gst_element_query (pre_record_loop_element, q);
const GstStructure *s = gst_query_get_structure (q);
```

### Returned Fields
All fields are `guint` (32-bit unsigned):

| Field          | Meaning                                                        |
|----------------|----------------------------------------------------------------|
| drops-gops     | Number of whole GOP pruning operations performed               |
| drops-buffers  | Total individual buffers dropped as part of GOP pruning        |
| drops-events   | Non-sticky (queued) events dropped during pruning              |
| queued-gops    | Current number of complete GOPs resident in the buffer         |
| queued-buffers | Current number of buffers resident (mirrors internal counter)  |

### Guarantees
1. Query is synchronous — results are valid upon return of `gst_element_query()`.
2. Fields represent a snapshot guarded by the element's internal mutex (atomic copy semantics).
3. `queued-gops` respects the adaptive floor: after a pruning cycle it will never report `< 2` when buffers remain.
4. When no buffers are buffered, `queued-gops` and `queued-buffers` both return `0`.

### Error Modes
| Condition                          | Handling                                      |
|------------------------------------|-----------------------------------------------|
| Unsupported structure name         | Falls back to default query handling (FALSE)  |
| Element not in PLAYING/PAUSED      | Returns current counters (may be all zero)    |
| Memory pressure / OOM              | Query allocation failure (caller sees NULL)   |

### Usage Example (C)
```
GstQuery *q = gst_query_new_custom (GST_QUERY_CUSTOM,
																		gst_structure_new_empty ("prerec-stats"));
if (gst_element_query (el, q)) {
	const GstStructure *st = gst_query_get_structure (q);
	guint gops=0; gst_structure_get_uint (st, "queued-gops", &gops);
	g_print ("Current GOPs: %u\n", gops);
}
gst_query_unref (q);
```

### Rationale
This approach avoids exporting internal C functions, keeps ABI surface minimal, and mirrors established GStreamer
patterns (e.g., latency or buffering queries) for introspection. It preserves encapsulation and makes tests independent
of internal symbol linkage changes.

## Concurrent Flush Suppression (Updated 2025-10-04 for T022)
The implementation performs the entire drain of buffered data under the element mutex and switches the mode to
`PASS_THROUGH` exactly once at the end of the drain. Because the second (and subsequent) `prerecord-flush` events acquire
the mutex only after the first finishes (at which point `mode != BUFFERING`), they observe an already completed flush and
do nothing. A separate `draining` flag was prototyped but removed as redundant for the current synchronous design.

Invariant:
BUFFERING --(flush trigger, atomic drain under lock)--> PASS_THROUGH

Any additional triggers while the first holds the lock block; when they proceed they see `mode==PASS_THROUGH` and are
ignored (early return). This satisfies the “ignore concurrent flush” requirement without extra state.

Future optimization note: If draining becomes incremental or asynchronous (lock released between pushes), re-introduce
an explicit `draining` flag to guard re-entrancy.

Testing: Unit test T022 validates only one emission batch occurs and that a subsequent trigger produces no additional
queued emission before a new live buffer arrives in pass-through mode.
