# Implementation Plan: Pre-Record Loop Baseline

**Branch**: `spec-kit` (non-standard; feature branch expected `000-prerecordloop-baseline`) | **Date**: 2025-09-25 | **Spec**: /Users/kartikaiyer/fun/gst_my_filter/specs/000-prerecordloop-baseline/spec.md  
**Input**: Feature specification from `/specs/000-prerecordloop-baseline/spec.md`

## Execution Flow (/plan command scope)
```
1. Load feature spec from Input path ✅
2. Fill Technical Context ✅
3. Fill the Constitution Check section ✅
4. Evaluate Constitution Check section ✅ (no violations)
5. Execute Phase 0 → research.md ✅ (all clarifications resolved; added performance & observability research items)
6. Execute Phase 1 → contracts/, data-model.md, quickstart.md ✅
7. Re-evaluate Constitution Check section ✅ (still passes)
8. Plan Phase 2 → Describe task generation approach ✅
9. STOP - Ready for /tasks command
```

**IMPORTANT**: This plan stops before creating `tasks.md` (that is done by /tasks).

## Summary
A GStreamer element (`prerecloop`) that buffers encoded video in time-based ring fashion, preserving GOP integrity and allowing flush-on-trigger with adaptive two-GOP minimum retention. Supports manual re-arming via `prerecord-arm`, ignores concurrent `prerecord-flush` during drain, integer-second `max-time`, AUTO conditional `flush-on-eos` behavior.

Primary technical approach:
- Single mutex + condvars guarding a deque of queue items (buffers, events)
- GOP tracking via incrementing ID on keyframes; adaptive pruning enforcing time cap with 2-GOP floor
- Custom downstream event `prerecord-flush` initiates drain; upstream event `prerecord-arm` re-arms buffering
- Properties to implement: `max-time` (guint, seconds), `flush-on-eos` (enum: auto/always/never), `silent` (existing)
- Observability via GST_DEBUG categories + counters (planned)

## Technical Context
**Language/Version**: C (C23 allowed by compiler)  
**Primary Dependencies**: GStreamer 1.26.x (core, base), GLib, spdlog (optional logging), uvw/libuv (present but not core to element), fmt  
**Storage**: In-memory ring (GstVecDeque) only  
**Testing**: gst-check for unit tests; gst-launch / custom harness for integration; valgrind for leak tests; potential Python (PyGObject) script for custom event dispatch  
**Target Platform**: macOS (development), Linux target (CI future)  
**Project Type**: single  
**Performance Goals**: <5ms added latency per buffer; zero additional copies; prune decision O(1) amortized  
**Constraints**: Memory footprint < (time_window * bitrate) with early pruning; integer-second capacity; thread-safe re-arm  
**Scale/Scope**: Typical window 5–30 seconds; GOP duration ~0.5–2s

## Constitution Check
*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### I. Thread-Safe GStreamer Integration
- [x] Threading model follows GStreamer best practices
- [x] Proper GMutex/GCond usage planned for shared state
- [x] Concurrent sink/source pad access handled safely

### II. GOP-Aware Frame Processing  
- [x] GOP boundary respect in buffer management design
- [x] Keyframe-based playback start confirmed
- [x] Complete GOP dropping strategy documented (adaptive floor rule)

### III. Zero-Copy Performance Architecture
- [x] GStreamer reference counting utilized
- [x] Ring buffer implementation avoids unnecessary copies
- [x] Critical path memory allocation minimized

### IV. GStreamer API Compliance
- [x] Plugin follows GStreamer development guidelines
- [x] Caps negotiation properly implemented (verify unit tests)
- [x] Required GStreamer events (EOS, FLUSH, SEEK) handled (SEEK passthrough test planned)
- [x] GObject property system used for configuration (to be extended)

### V. Event-Driven State Management
- [x] Clear BUFFERING/PASS_THROUGH state transitions
- [x] External trigger event handling designed
- [x] State synchronization and buffer continuity planned

## Project Structure
(Existing repository keeps element in `gstprerecordloop/`; tests to be added under `tests/` structure.)

**Structure Decision**: Single project layout retained; add `tests/unit`, `tests/integration`, `tests/perf`.

## Phase 0: Outline & Research (Completed)
Research items & resolutions are in `/Users/kartikaiyer/fun/gst_my_filter/specs/000-prerecordloop-baseline/research.md`.

## Phase 1: Design & Contracts (Completed)
Artifacts: data-model.md, contracts/custom-events.md, quickstart.md

## Phase 2: Task Planning Approach
Will map each Functional Requirement and Clarification to tasks. Categories:
- Properties: implement `max-time`, `flush-on-eos`
- Core logic fixes: adaptive floor pruning, ignore concurrent flush, re-arm handling
- Memory safety: remove buffer list code, enforce queue node ownership
- Observability: add counters (buffers_dropped, gops_dropped, flush_count, rearm_count)
- Tests: unit (enum state, pruning), integration (flush, re-arm, EOS modes), performance (latency, pruning cost)
- Docs: README usage, gtk-doc annotations

Estimated tasks: ~32 (8 setup/tests, 10 implementation, 6 observability & properties, 4 performance tests, 4 docs & polish)

## Phase 3+: Future Implementation
Out of scope here.

## Complexity Tracking
| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| Adaptive 2-GOP floor vs strict time | Prevent unusable single GOP capture | Strict cutoff risks zero usable pre-roll |
| Custom upstream re-arm event | Explicit control over reuse | Automatic re-arm could surprise pipelines |

## Progress Tracking
**Phase Status**:
- [x] Phase 0: Research complete (/plan command)
- [x] Phase 1: Design complete (/plan command)
- [x] Phase 2: Task planning complete (/plan command - describe approach only)
- [ ] Phase 3: Tasks generated (/tasks command)
- [ ] Phase 4: Implementation complete
- [ ] Phase 5: Validation passed

**Gate Status**:
- [x] Initial Constitution Check: PASS
- [x] Post-Design Constitution Check: PASS
- [x] All NEEDS CLARIFICATION resolved
- [x] Complexity deviations documented

---
*Based on Constitution v1.0.0 - See `.specify/memory/constitution.md`*
