# Changelog

All notable changes to this project will be documented in this file.

The format is inspired by Keep a Changelog and follows semantic versioning.

## [Unreleased]
### Added

#### Properties (T019, T020, T024)
- **max-time** property: Maximum buffered duration in whole seconds (integer only).
  * Default: 10 seconds
  * Zero/negative: Unlimited buffering
  * Enforces adaptive 2-GOP minimum floor (T021)
  * Sub-second values floored to whole seconds (T024)
  * Range: 0 to G_MAXINT

- **flush-on-eos** property: Enum-based EOS handling policy (T020).
  * Default: AUTO (flush only if in PASS_THROUGH mode)
  * Options: AUTO, ALWAYS, NEVER
  * AUTO: Conditional flush based on current mode (T025)
  * ALWAYS: Always drain buffer before forwarding EOS
  * NEVER: Forward EOS immediately without flush
  * Replaces previous boolean implementation

- **flush-trigger-name** property: Customizable flush event structure name (T020a).
  * Default: "prerecord-flush"
  * Allows application-specific event integration (e.g., "motion-detected")
  * Case-sensitive structure name matching
  * Set to NULL to use default name

- **silent** property: Legacy logging suppression flag.
  * Default: FALSE
  * Note: Prefer GST_DEBUG environment variable for runtime control

#### Custom Events (T023, T012)
- **prerecord-flush** event: Downstream custom event to trigger buffer drain.
  * Event Type: GST_EVENT_CUSTOM_DOWNSTREAM
  * Structure name: Configurable via flush-trigger-name property
  * Behavior: Drains all queued GOPs, transitions BUFFERING → PASS_THROUGH
  * Ignores concurrent flush triggers during active drain (T022)
  
- **prerecord-arm** event: Upstream custom event to re-enter buffering mode (T023).
  * Event Type: GST_EVENT_CUSTOM_UPSTREAM
  * Structure name: "prerecord-arm" (fixed)
  * Behavior: Transitions PASS_THROUGH → BUFFERING, resets GOP tracking
  * Non-destructive to already-forwarded data
  * Ignored if already in BUFFERING mode

#### Core Features
- GOP-aware ring buffer with adaptive 2-GOP minimum floor (T021).
  * Even if single GOP exceeds max-time, element retains it plus preceding GOP
  * Ensures playback continuity and decoding integrity
  * Pruning operates on whole-GOP boundaries only

- Custom stats query: `prerec-stats` structure for runtime introspection.
  * Fields: drops-gops, drops-buffers, drops-events, queued-gops, queued-buffers
  * Query type: GST_QUERY_CUSTOM
  * Synchronous, mutex-guarded snapshot
  * Enables black-box testing without internal symbol exposure

- FLUSH_START/FLUSH_STOP event handling for seek support (T034a).
  * FLUSH_START: Clears queue, resets GOP tracking, sets srcresult=FLUSHING
  * FLUSH_STOP: Resets srcresult=OK, reinitializes segments if reset_time=TRUE
  * Proper integration with GStreamer seek mechanisms

- Internal counters for diagnostics (T026, T027):
  * gops_dropped, buffers_dropped: Pruning statistics
  * flush_count, rearm_count: Mode transition tracking
  * Exposed via GST_INFO logs on state transitions

#### Testing Infrastructure (T001-T018)
- Project scaffolding and initial GStreamer prerecord loop element skeleton.
- Debug categories: `pre_record_loop`, `pre_record_loop_dataflow` (T003).
- Test directory structure: `tests/unit`, `tests/integration`, `tests/perf`, `tests/memory` (T001).
- Test build integration with gst-check (T002).
- Comprehensive unit test suite (17 tests):
  * Plugin registration and caps negotiation (T009, T030)
  * Queue pruning and 2-GOP floor validation (T010, T011)
  * Re-arm and flush trigger sequences (T012)
  * Flush-on-eos policy behavior (T013)
  * Sticky event propagation (T033)
  * GAP event handling (T032)
  * FLUSH_START/STOP and seek support (T034a)
  * Event queuing mode awareness (T034b)
- Integration test suite (T014-T016).
- Performance benchmark skeleton (T017).
- Memory leak testing framework (T018, valgrind wrapper).
- Shared test utilities: PrerecTestPipeline, prerec_push_gop, stats query helpers.

#### Documentation (T034, T035)
- GTK-Doc annotations for all properties and custom events (T034).
- README.md sections:
  * Custom Events: prerecord-flush and prerecord-arm usage with C API examples
  * Properties Reference: Comprehensive table with types, defaults, ranges
  * Event-driven workflow diagram and integration patterns
- Build option: PREREC_ENABLE_LIFE_DIAG for lifecycle diagnostics (default OFF).

#### Build & CI (T005, T006, T007)
- clang-format configuration for code style consistency (T005).
- CI script `.ci/run-tests.sh` with dual-config build validation (T006).
- gtk-doc integration stub (disabled by default) (T007).

### Changed
- Renamed GST_DEBUG categories from `prerecloop` / `prerecloop_dataflow` to `pre_record_loop` / `pre_record_loop_dataflow` (T003).
- Refcount integrity improvements (Refcount / Lifecycle Integrity fix):
  * Removed manual sticky event storage (delegate to gst_pad_event_default)
  * Added explicit gst_event_ref() when queuing SEGMENT/GAP events
  * Fixed ownership semantics: queue holds owned references
  * Added regression test: prerec_unit_no_refcount_critical
- SEGMENT/GAP event queuing now mode-aware (T034b):
  * Events queued ONLY in BUFFERING mode
  * Events forwarded directly in PASS_THROUGH mode
  * Prevents duplicate emission and memory waste
  * Eliminates stale timing/segment state in new cycles

### Fixed
- Queue dequeue path: Avoid freeing internal node pointers (T029).
- Concurrent flush suppression: Ignore redundant triggers during active drain (T022).
- EOS handling AUTO policy: Flush only if in PASS_THROUGH mode (T025).
- Sub-second max-time rounding: Values floored to whole seconds (T024).
- SEGMENT/GAP event duplication: Mode check prevents double emission (T034b).
- Refcount assertions: Fixed double unref of sticky events (mini-object refcount fix).

### Removed
- Buffer list handling code paths completely purged (T008, T028).
- Obsolete buffer list helpers and dead code eliminated.
- Manual sticky event storage calls (replaced by default handler delegation).

## [0.1.0] - YYYY-MM-DD
- Tag to be created after core feature tests & implementation (Phase 3.3) are stable.

