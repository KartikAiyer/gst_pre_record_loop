# GitHub Copilot Instructions: prerecordloop GStreamer Plugin

## Project Overview
This is a **GStreamer 1.0 video filter plugin** (`prerecordloop`) implementing a GOP-aware ring buffer for pre-event video capture. The element buffers encoded video continuously and, upon receiving a flush trigger event, emits buffered frames followed by live pass-through. Critical for event-driven recording scenarios (motion detection, external triggers).

## Architecture & Data Flow

### Core Components
- **Element**: `gstprerecordloop/src/gstprerecordloop.c` (~1500 LOC) - single-file element implementation
- **Modes**: BUFFERING (queue incoming GOPs) ↔ PASS_THROUGH (forward live after flush)
- **Queue**: Custom `GstVecDeque` of `GstQueueItem` (buffer or event mini-objects with metadata)
- **GOP Tracking**: Keyframe detection (`GST_BUFFER_FLAG_DELTA_UNIT`) assigns GOP IDs; pruning operates on whole GOPs

### Event-Driven State Machine
```
BUFFERING --[prerecord-flush]-> drain queue -> PASS_THROUGH
PASS_THROUGH --[prerecord-arm]-> reset baseline -> BUFFERING
```

**Custom Events** (defined in element code, not separate headers):
- Downstream: `prerecord-flush` (structure name matched via `flush-trigger-name` property)
- Upstream: `prerecord-arm` (handled in `GST_EVENT_CUSTOM_UPSTREAM` case)

### Critical Invariants
1. **2-GOP Floor**: Pruning never drops below 2 complete GOPs, even if oversized GOP exceeds `max-time`
2. **Refcount Discipline**: Queue holds owned references; SEGMENT/GAP events require explicit `gst_event_ref()` before enqueue
3. **Mutex-Guarded**: All queue ops and mode transitions serialized via `GST_PREREC_MUTEX_LOCK`

## Build & Test Workflow

### Quick Start (Debug Build)
```bash
# First-time setup with Conan
conan install . --build=missing --settings=build_type=Debug
cmake --preset=conan-debug
cmake --build --preset=conan-debug

# Subsequent rebuilds (after code changes)
cmake --build build/Debug --parallel 6
```

### Running Tests (CTest auto-injects plugin path)
```bash
# All tests
ctest --test-dir build/Debug

# Specific test with verbose output
ctest --test-dir build/Debug -R prerec_unit_queue_pruning -V

# With GStreamer debug logs
GST_DEBUG=*:4,pre_record_loop:7 ctest --test-dir build/Debug -R prerec_unit_rearm_sequence -V
```

**Key**: CTest sets `GST_PLUGIN_PATH` automatically (see `tests/CMakeLists.txt` ENVIRONMENT property). When running test binaries directly, export manually:
```bash
export GST_PLUGIN_PATH="$(pwd)/build/Debug/gstprerecordloop:$GST_PLUGIN_PATH"
./build/Debug/tests/unit_test_rearm_sequence
```

### Diagnostics Build Flag
For lifecycle/refcount debugging, enable `PREREC_ENABLE_LIFE_DIAG`:
```bash
cmake -S . -B build/Debug -DPREREC_ENABLE_LIFE_DIAG=1
cmake --build build/Debug --target gstprerecordloop
GST_DEBUG=prerec_lifecycle:7,prerec_dataflow:5 ctest -R prerec_unit_no_refcount_critical -V
```
**Default is OFF** (zero runtime overhead). Enable only for ownership forensics.

## Code Patterns & Conventions

### Property Definitions
- Integer properties use `G_TYPE_UINT64` for time values (nanoseconds internally, seconds exposed)
- Enum properties require `GType` registration via `g_enum_register_static()`
- Example: `flush-on-eos` (enum: AUTO/ALWAYS/NEVER), `max-time` (uint64), `flush-trigger-name` (string)

### Event Handling (Sink vs Src Pads)
- **Sink pad** (`gst_pre_record_loop_sink_event`): Handles downstream events (EOS, SEGMENT, CUSTOM_DOWNSTREAM for flush)
- **Src pad** (`gst_pre_record_loop_src_event`): Handles upstream events (RECONFIGURE, CUSTOM_UPSTREAM for re-arm)
- Custom events checked via `gst_structure_has_name(gst_event_get_structure(event), "event-name")`

### Queue Operations (Reference Ownership)
```c
// CORRECT: Take ref before enqueue (queue owns it)
gst_event_ref(event);
gst_prerec_locked_enqueue_event(loop, event);

// WRONG: Double storage hazard
gst_pad_store_sticky_event(pad, event);  // Don't do this!
gst_pad_event_default(pad, parent, event);  // Default handler already stores
```

### Stats Query Pattern (Custom Introspection)
Tests use synchronous custom query for validation:
```c
GstQuery *q = gst_query_new_custom(GST_QUERY_CUSTOM, 
                                   gst_structure_new_empty("prerec-stats"));
gst_element_query(prerecordloop_element, q);
const GstStructure *s = gst_query_get_structure(q);
guint queued_gops, drops_gops;
gst_structure_get_uint(s, "queued-gops", &queued_gops);
gst_structure_get_uint(s, "drops-gops", &drops_gops);
```
See `gstprerecordloop.c` src query handler for returned fields.

## Testing Strategy (TDD Workflow)

### Test Organization
- **Unit** (`tests/unit/`): Element behavior in isolation (properties, pruning, events)
- **Integration** (`tests/integration/`): Full pipelines with appsrc/fakesink
- **Perf** (`tests/perf/`): Latency benchmarks (skeleton)
- **Memory** (`tests/memory/`): Valgrind leak checks (script-based)

### Test Utilities (`tests/unit/test_utils.{h,c}`)
- `PrerecTestPipeline`: Standard appsrc→prerecordloop→fakesink harness
- `prerec_push_gop(appsrc, delta_count, pts, duration)`: Push keyframe + N deltas
- `prerec_wait_for_stats(element, min_gops, min_drops, timeout_ms)`: Poll stats query until conditions met
- `prerec_attach_count_probe(element, &counter)`: Src pad probe for emission counting

### Adding a New Test
1. Create `tests/unit/test_<feature>.c` with forced-fail assertion
2. Register in `tests/CMakeLists.txt` via `prerec_add_gst_exec_test(unit <feature> unit/test_<feature>.c)`
3. Run test to confirm failure: `ctest -R prerec_unit_<feature> -V`
4. Implement feature in `gstprerecordloop.c`
5. Rebuild and verify pass

## Spec-Driven Development

### Spec Location
`specs/000-prerecordloop-baseline/` contains:
- `spec.md`: Requirements, user scenarios, clarifications (source of truth)
- `tasks.md`: Phased implementation checklist (T001–T040)
- `data-model.md`: Queue ownership semantics
- `contracts/`: Event/property contracts

### Task Workflow
- Prefix commits with task ID: `git commit -m "T023: implement prerecord-arm event handling"`
- Mark tasks complete in `tasks.md` only after tests pass
- Phase 3.2 (Tests First) MUST precede Phase 3.3 (Implementation)

## Common Pitfalls & Solutions

### Refcount Crashes
**Symptom**: `gst_mini_object_unref: assertion '... > 0' failed`  
**Cause**: Queue sharing references without explicit ref  
**Fix**: Always `gst_event_ref()` / `gst_buffer_ref()` before enqueue if object will be used elsewhere

### Flush Not Draining Queue
**Check**:
1. Event structure name matches `flush-trigger-name` property (default: `"prerecord-flush"`)
2. Mode is BUFFERING (not already in PASS_THROUGH)
3. Concurrent flush ignored if `loop->mode == GST_PREREC_MODE_PASS_THROUGH` guard active

### Test Timing Flakiness
**Solution**: Use `prerec_wait_for_stats()` polling instead of fixed `g_usleep()`. Stats query is synchronous and mutex-guarded.

### Plugin Not Found in Tests
**Symptom**: `no such element factory "pre_record_loop"`  
**Fix**: Use `ctest` (auto-injects path) or set `GST_PLUGIN_PATH` manually if running test binary directly

## Environment Dependencies

- **GStreamer**: 1.26.2 (Homebrew install required, pkgconfig paths hardcoded in `CMakeLists.txt`)
- **Conan**: Package manager for spdlog/uvw/catch2 dependencies
- **CMake**: 3.27+ with preset support
- **macOS-specific**: Paths assume `/opt/homebrew/Cellar/` structure

## Key Files Quick Reference
| File | Purpose |
|------|---------|
| `gstprerecordloop/src/gstprerecordloop.c` | Main element implementation (~1500 LOC) |
| `tests/unit/test_utils.{h,c}` | Shared test harness & GOP helpers |
| `specs/000-prerecordloop-baseline/spec.md` | Requirements & acceptance scenarios |
| `specs/000-prerecordloop-baseline/tasks.md` | Implementation roadmap with phase gates |
| `tests/CMakeLists.txt` | Test registration via `prerec_add_gst_exec_test()` |
| `.ci/run-tests.sh` | CI script (dual-config builds, gst-inspect validation) |

## Debug Categories
- `pre_record_loop`: Element lifecycle & decisions
- `pre_record_loop_dataflow`: Buffer/event flow
- `prerec_lifecycle`: Lifecycle tracking (requires `PREREC_ENABLE_LIFE_DIAG=1`)
- `prerec_dataflow`: Detailed push/unref correlation (requires `PREREC_ENABLE_LIFE_DIAG=1`)

## When Making Changes
1. Check `specs/000-prerecordloop-baseline/spec.md` for requirements context
2. Write failing test first (Phase 3.2 discipline)
3. Implement behind mutex if touching queue or mode
4. Rebuild with `cmake --build build/Debug --parallel 6`
5. Run affected tests: `ctest --test-dir build/Debug -R <pattern> -V`
6. Verify no refcount regressions: `ctest -R prerec_unit_no_refcount_critical`
7. Update `tasks.md` checklist and commit with task ID

## Next Priorities (Pending Tasks)
- T024: Sub-second max-time flooring
- T025: AUTO EOS policy refinement
- T026: Extended stats counters (flush_count, rearm_count)
- T027: State transition debug logging
- Integration tests (T030–T033) after core complete
