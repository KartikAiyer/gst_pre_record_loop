# Tasks: Pre-Record Loop Baseline

**Input**: Design documents from `/specs/000-prerecordloop-baseline/`  
**Prerequisites**: plan.md (required), research.md, data-model.md, contracts/

## Phase 3.1: Setup
- [X] T001 Add `tests/` directory structure: `tests/unit`, `tests/integration`, `tests/perf`, `tests/memory`
- [X] T002 Create base `tests/unit/meson.build` or CMake additions for gst-check integration (extend existing build system)
- [X] T003 [P] Add `GST_DEBUG_CATEGORY` declarations for `pre_record_loop` and `pre_record_loop_dataflow` if missing
- [X] T004 [P] Add initial `docs/` folder with `CHANGELOG.md` skeleton
- [X] T005 Introduce `clang-format` config and (optional) `clang-tidy` preset
- [X] T006 [P] Add CI placeholder script `.ci/run-tests.sh` (enhanced with dual-config build, gst-inspect, style probe)
- [X] T007 Add `gtk-doc` integration stub (CMake options) disabled by default
- [X] T008 [P] Remove obsolete buffer list code paths from `gstprerecordloop/src/gstprerecordloop.c` (marked and purged)

## Phase 3.2: Tests First (TDD) ⚠️ MUST COMPLETE BEFORE 3.3
**CRITICAL: These tests MUST be written and MUST FAIL before ANY implementation changes for those behaviors.**
- [X] T009 [P] Unit test plugin registration: `tests/unit/test_plugin_registration.c`
- [ ] T010 [P] Unit test QUEUE invariants (2-GOP floor pruning logic) `tests/unit/test_queue_pruning.c`
- [ ] T011 [P] Unit test concurrent flush ignore: `tests/unit/test_concurrent_flush.c`
- [ ] T012 [P] Unit test re-arm event transition: `tests/unit/test_rearm.c`
- [ ] T013 [P] Unit test flush-on-eos AUTO semantics: `tests/unit/test_flush_on_eos.c`
- [ ] T014 [P] Integration pipeline test: flush sequence (buffer → flush → pass-through) `tests/integration/test_flush_sequence.c`
- [ ] T015 [P] Integration pipeline test: re-arm cycle `tests/integration/test_rearm_cycle.c`
- [ ] T016 [P] Integration pipeline test: oversize GOP retention `tests/integration/test_oversize_gop.c`
- [ ] T017 [P] Performance benchmark skeleton: `tests/perf/test_latency_prune.c` (measure added per-buffer latency)
- [ ] T018 [P] Memory leak baseline test (valgrind wrapper script) `tests/memory/test_leaks.sh`

## Phase 3.3: Core Implementation (ONLY after tests above are failing)
- [ ] T019 Implement `max-time` property (integer seconds) in `gstprerecordloop/src/gstprerecordloop.c`
- [ ] T020 Implement `flush-on-eos` enum property (auto/always/never) in same file
- [ ] T021 Implement adaptive 2-GOP floor pruning logic (use existing queue + update fullness function)
- [ ] T022 Implement ignoring concurrent `prerecord-flush` when `draining == TRUE`
- [ ] T023 Implement `prerecord-arm` event handling to re-enter BUFFERING
- [ ] T024 Enforce sub-second floor behavior for `max-time` (document in property description)
- [ ] T025 Update EOS handling for AUTO policy (flush only if PASS_THROUGH)
- [ ] T026 Add internal counters: `gops_dropped`, `buffers_dropped`, `flush_count`, `rearm_count`
- [ ] T027 Expose debug stats via GST_INFO log on state transition
- [ ] T028 Remove buffer list helpers and related dead code entirely (final pass)
- [ ] T029 Update queue dequeue path to avoid freeing internal node pointers

## Phase 3.4: Integration
- [ ] T030 Verify caps negotiation & add unit assertions (caps tests share with registration) adjust if needed
- [ ] T031 SEEK passthrough test + minor handling adjustments (if required)
- [ ] T032 Validate GAP event handling (add test if missing) `tests/unit/test_gap_events.c`
- [ ] T033 Validate sticky event propagation (segment/caps) `tests/unit/test_sticky_events.c`

## Phase 3.5: Polish
- [ ] T034 [P] Add GTK-Doc annotations for new properties & events
- [ ] T035 Add README section: custom events usage, properties table synced
- [ ] T036 [P] Add CHANGELOG entry for property + event features
- [ ] T037 Performance measurement refinement: record median/99p latency `tests/perf/test_latency_prune.c`
- [ ] T038 [P] Add metric logging toggle (maybe property or debug env var) (optional)
- [ ] T039 Run valgrind full leak test & fix issues (update `tests/memory/test_leaks.sh`)
- [ ] T040 Add code style check invocation to CI script `.ci/run-tests.sh`

## Dependencies
- Setup (T001-T008) before tests.
- Tests (T009-T018) before implementation changes (T019-T029).
- Properties (T019, T020) before dependent behaviors (T021-T027).
- Pruning logic (T021) before counters relying on drop events (T026).
- Implementation before integration tasks (T030-T033).
- Integration before polish (T034-T040).

## Parallel Execution Examples
```
# Example: run independent unit test skeleton creation in parallel
Task: T009 Unit test plugin registration
Task: T010 Unit test queue pruning
Task: T011 Unit test concurrent flush
Task: T012 Unit test re-arm
Task: T013 Unit test flush-on-eos

# Example: integration tests parallel (different files)
Task: T014 Flush sequence integration
Task: T015 Re-arm cycle integration
Task: T016 Oversize GOP integration
Task: T017 Performance benchmark skeleton
Task: T018 Memory leak baseline script
```

## Notes
- Ensure each test initially fails (e.g., assertions for properties not yet existing) before implementing.
- Keep each task atomic: one file / one conceptual change.
- Commit after each task; reference task ID in commit message.
- Avoid adding optimization until baseline correctness passes.

## Validation Checklist
- [ ] All contract events (`prerecord-flush`, `prerecord-arm`) have tests (T011, T012, T014, T015)
- [ ] All properties have tests (T009 for registration, T013 flush-on-eos, T010/T024 for max-time)
- [ ] Tests precede implementation changes for each feature
- [ ] Parallel [P] tasks touch distinct files
- [ ] Memory safety improvements validated by T018 & T039
