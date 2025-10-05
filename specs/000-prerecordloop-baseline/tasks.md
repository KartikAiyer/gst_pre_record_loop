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
- [X] T010 [P] Unit test queue pruning invariants (initial skeleton, forced fail) `tests/unit/test_queue_pruning.c`
- [X] T011 [P] Unit test GOP floor (separate explicit 2-GOP floor check) `tests/unit/test_pruning_two_gop_floor.c`
- [X] T012 [P] Unit test re-arm / flush trigger sequence (skeleton, forced fail) `tests/unit/test_rearm_sequence.c`
- [X] T013 [P] Unit test flush-on-eos behavior (skeleton, forced fail) `tests/unit/test_flush_on_eos.c`
 - [X] T014 [P] Integration pipeline test: flush sequence (skeleton, forced fail) `tests/integration/test_flush_sequence.c`
 - [X] T015 [P] Integration pipeline test: re-arm cycle (skeleton, forced fail) `tests/integration/test_rearm_cycle.c`
 - [X] T016 [P] Integration pipeline test: oversize GOP retention (skeleton, forced fail) `tests/integration/test_oversize_gop.c`
 - [X] T017 [P] Performance benchmark skeleton: `tests/perf/test_latency_prune.c` (skeleton, forced fail; latency measurement TBD)
 - [X] T018 [P] Memory leak baseline test (valgrind wrapper script) `tests/memory/test_leaks.sh` (skeleton, forced fail)

### Phase 3.2 Support Utilities (Added)
- [X] Shared pipeline helper (`PrerecTestPipeline`) and GOP push helper (`prerec_push_gop`) implemented
- [X] Stats struct + accessor (`gst_prerec_get_stats`) added (partial for future assertions)
- [X] Added temporary boolean `flush-on-eos` property and `flush-trigger-name` property earlier than scheduled (will refine to enum later)

## Phase 3.3: Core Implementation (ONLY after tests above are failing)
- [X] T019 Implement `max-time` property (integer seconds) in `gstprerecordloop/src/gstprerecordloop.c`
- [X] T020 (Refine) Replace provisional boolean `flush-on-eos` with enum (auto/always/never) & adjust tests
- [X] T020a Implement `flush-trigger-name` behavior validation & test using non-default value
- [X] T021 Implement adaptive 2-GOP floor pruning logic (use existing queue + update fullness function) and convert T010/T011 to real assertions
- [X] T022 Implement ignoring concurrent `prerecord-flush` when `draining == TRUE`
- [X] T023 Implement `prerecord-arm` event handling to re-enter BUFFERING (add event name constant)
- [X] T024 Enforce sub-second floor behavior for `max-time` (document in property description)
- [X] T025 Update EOS handling for AUTO policy (flush only if PASS_THROUGH) (depends on T020 enum)
- [X] T026 Add internal counters: `gops_dropped`, `buffers_dropped`, `flush_count`, `rearm_count` (PARTIAL: drops_* already present)
- [X] T027 Expose debug stats via GST_INFO log on state transition
- [X] T028 Remove buffer list helpers and related dead code entirely (final pass)
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
- [ ] All contract events (`prerecord-flush`, `prerecord-arm`) have tests (current: flush trigger covered by T012; arm pending)
- [ ] All properties have tests (current: flush-on-eos skeleton T013; max-time pending; enum refinement pending)
- [ ] T010/T011 upgraded from forced fail to real assertions after pruning logic implemented
- [ ] Tests precede implementation changes for each feature
- [ ] Parallel [P] tasks touch distinct files
- [ ] Memory safety improvements validated by T018 & T039
- [ ] Stats counters asserted where meaningful once pruning logic lands (drops_gops / drops_buffers)
