# Feature Specification: Pre-Record Loop Baseline

**Feature Branch**: `000-prerecordloop-baseline`  
**Created**: 2025-09-24  
**Status**: Draft (Reverse-engineered)  
**Input**: User description: "Baseline spec extracted from existing README, constitution v1.0.0, and current C implementation"

## User Scenarios & Testing *(mandatory)*

### Primary User Story
As an application that records video around motion or external trigger events, I need a pipeline element that continuously buffers encoded video so that when an event occurs I can also capture several seconds of video that happened before the event.

### Acceptance Scenarios
1. **Given** the element is in BUFFERING mode and receives a flush trigger event, **When** the trigger is processed, **Then** the element MUST first output all buffered GOP-aligned frames in order and then forward live incoming frames.
2. **Given** the buffered data exceeds the configured time capacity, **When** a new frame arrives, **Then** the element MUST drop the oldest complete GOP(s) until total buffered duration is strictly below the time limit.
3. **Given** the pipeline reaches EOS while in PASS_THROUGH mode, **When** EOS propagates, **Then** the element MUST optionally (configurable) flush any remaining buffered data before forwarding EOS downstream.
4. **Given** upstream sends a SEGMENT event, **When** timestamps shift or seeking occurs, **Then** the element MUST update its internal timing so buffered duration accounting remains correct.

### Edge Cases
- Trigger event arrives while already in PASS_THROUGH mode (SHOULD ignore or log at debug level).  
- Upstream sends only delta frames for a period (MUST delay start output until next keyframe when flushing).  
- Custom flush trigger received during state change (MUST serialize via internal mutex; behavior currently UNSPECIFIED).  
- Single GOP duration exceeds configured max-time (NEEDS CLARIFICATION: drop entire GOP vs allow transient overflow).  
- Rapid successive trigger events (NEEDS CLARIFICATION: ignore subsequent vs re-enter buffering after manual reset).  

## Requirements *(mandatory)*

### Functional Requirements (Updated for Time-Only Capacity)
- **FR-001**: Element MUST buffer incoming encoded video data while in BUFFERING mode.
- **FR-002**: Element MUST track GOP boundaries using keyframe identification and associate each buffer with a GOP id.
- **FR-003**: Element MUST enforce a single time-based capacity limit representing maximum buffered duration.
- **FR-004**: Element MUST output buffered data in presentation order upon receiving an external flush/trigger event, then switch to PASS_THROUGH mode.
- **FR-005**: Element MUST maintain zero-copy semantics (no unnecessary buffer duplication) using reference counting.
- **FR-006**: Element MUST handle standard GStreamer events: SEGMENT, CAPS, EOS, FLUSH_START/STOP, and custom trigger events.
- **FR-007**: Element MUST ensure thread-safe queue operations using a mutex and condition variables.
- **FR-008**: Element MUST drop oldest complete GOP(s) when time capacity would be exceeded to admit a new buffer.
- **FR-009**: Element MUST preserve decoding integrity by never emitting partial GOPs at the start of output.
- **FR-010**: Element MUST provide configurable properties for max-time (GstClockTime), silent logging mode, and flush-on-eos.
- **FR-011**: Element MUST forward downstream events while buffering so that state (segments, caps) remains synchronized.
- **FR-012**: Element MUST correctly propagate or store sticky events.
- **FR-013**: Element SHOULD expose a documented custom downstream event name (CURRENT: implicitly "prerecord-flush").
- **FR-014**: Element MUST clean up all queued mini objects on finalize without leaks.
- **FR-015**: Element MUST avoid freeing internal queue node pointers returned by dequeue (BUG path slated for fix).
- **FR-016**: Mode transitions MUST be atomic with respect to queue manipulation.
- **FR-017**: Element SHOULD support optional automatic flush on EOS when configured (flush-on-eos property).
- **FR-018 (REMOVED)**: Multi-metric capacity (buffers/bytes) — intentionally dropped; future additions require new spec & version bump.
- **FR-019 (REMOVED)**: GstBufferList support — intentionally dropped; existing helper code scheduled for removal.

### Ambiguities / Clarifications Needed (Updated)
- **CLAR-001**: Policy when a single GOP duration exceeds configured max-time.
- **CLAR-002**: Whether element ever re-enters BUFFERING automatically after flush (current assumption: one-way until externally reset, NEEDS CLARIFICATION on reset mechanism).
- **CLAR-003**: Expected behavior if trigger event arrives during an ongoing flush (queue draining): ignore or queue a secondary action.
- **CLAR-004**: Required granularity for max-time property (nanoseconds vs seconds rounding).
- **CLAR-005**: Whether flush-on-eos default should be TRUE or FALSE.

### Key Components (Adjusted)
- **Ring Buffer Queue**: Stores `GstQueueItem` entries referencing buffers/events with keyframe flag, GOP id, accumulated duration.
- **GOP Tracker**: Increments GOP id on keyframes, groups buffers for drop and flush operations.
- **Mode Controller**: Maintains buffering vs pass-through state transitions triggered by custom event.
- **Time Capacity Manager**: Calculates total buffered duration; prunes oldest GOP(s) when exceeding max-time.
- **Event Handler**: Intercepts SEGMENT, CAPS, GAP, EOS, FLUSH events, custom flush trigger; manages sticky propagation.

### GStreamer Integration (Adjusted)
- **Pipeline Position**: After encoder + parser: `... ! h264parse ! prerecloop ! mux ! sink`.
- **Pad Templates**: Single sink/src pair for encoded video streams (exact caps derived from upstream negotiation).
- **Events Handled**: SEGMENT, CAPS, EOS, FLUSH_START, FLUSH_STOP, custom downstream event "prerecord-flush", GAP.
- **Properties (Current)**: `silent` (boolean).  
- **Properties (Planned)**: `max-time` (GstClockTime), `flush-on-eos` (boolean).  
- **Removed/Not Planned**: `max-buffers`, `max-bytes`, `GstBufferList` support.
- **Custom Event**: Downstream custom event structure name (inferred) "prerecord-flush" triggers flush and mode switch.

## Deprecations & Removals
- **Buffer List Helpers**: Code paths (`locked_apply_buffer_list`, `gst_buffer_or_list_chain`) marked for removal in next refactor since design excludes GstBufferList.
- **Multi-Metric Capacity Fields**: `max_size.buffers`, `max_size.bytes`, and related logic to be simplified—time only retained.
- **Specification Changes**: Original FR-010 (multi-metric) replaced by time-only FR-010; FR-018/FR-019 removed with rationale above.

## Review & Acceptance Checklist

### Content Quality
- [x] No obsolete references to buffer list support in active requirements
- [x] Capacity model simplified to time-only
- [x] All mandatory sections completed

### Requirement Completeness
- [ ] No [NEEDS CLARIFICATION] markers remain (Clar items listed)
- [x] Requirements testable under revised scope
- [x] Success criteria measurable (max-time, GOP integrity, mode switch)
- [x] Scope bounded to time-only buffering
- [x] Dependencies and assumptions identified

## Execution Status
- [x] Specification updated to reflect design decisions (time-only, remove lists)
- [x] Requirements renumbered/annotated
- [x] Deprecations documented
