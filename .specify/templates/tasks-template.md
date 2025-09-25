# Tasks: [FEATURE NAME]

**Input**: Design documents from `/specs/[###-feature-name]/`
**Prerequisites**: plan.md (required), research.md, data-model.md, contracts/

## Execution Flow (main)
```
1. Load plan.md from feature directory
   → If not found: ERROR "No implementation plan found"
   → Extract: tech stack, libraries, structure
2. Load optional design documents:
   → data-model.md: Extract entities → model tasks
   → contracts/: Each file → contract test task
   → research.md: Extract decisions → setup tasks
3. Generate tasks by category:
   → Setup: project init, dependencies, linting
   → Tests: contract tests, integration tests
   → Core: models, services, CLI commands
   → Integration: DB, middleware, logging
   → Polish: unit tests, performance, docs
4. Apply task rules:
   → Different files = mark [P] for parallel
   → Same file = sequential (no [P])
   → Tests before implementation (TDD)
5. Number tasks sequentially (T001, T002...)
6. Generate dependency graph
7. Create parallel execution examples
8. Validate task completeness:
   → All contracts have tests?
   → All entities have models?
   → All endpoints implemented?
9. Return: SUCCESS (tasks ready for execution)
```

## Format: `[ID] [P?] Description`
- **[P]**: Can run in parallel (different files, no dependencies)
- Include exact file paths in descriptions

## Path Conventions
- **Single project**: `src/`, `tests/` at repository root
- **Web app**: `backend/src/`, `frontend/src/`
- **Mobile**: `api/src/`, `ios/src/` or `android/src/`
- Paths shown below assume single project - adjust based on plan.md structure

## Phase 3.1: Setup
- [ ] T001 Create project structure per implementation plan
- [ ] T002 Initialize [language] project with [framework] dependencies
- [ ] T003 [P] Configure linting and formatting tools

## Phase 3.2: Tests First (TDD) ⚠️ MUST COMPLETE BEFORE 3.3
**CRITICAL: These tests MUST be written and MUST FAIL before ANY implementation**
- [ ] T004 [P] Plugin registration test in tests/unit/test_plugin_registration.c
- [ ] T005 [P] Thread safety test in tests/unit/test_thread_safety.c  
- [ ] T006 [P] GOP boundary test in tests/unit/test_gop_processing.c
- [ ] T007 [P] Pipeline integration test in tests/integration/test_pipeline.c
- [ ] T008 [P] State transition test in tests/integration/test_state_management.c
- [ ] T009 [P] Performance benchmark in tests/performance/test_buffer_ops.c

## Phase 3.3: Core Implementation (ONLY after tests are failing)
- [ ] T010 [P] Plugin base structure in src/gstprerecordloop.c
- [ ] T011 [P] Ring buffer implementation in src/ring_buffer.c
- [ ] T012 [P] GOP tracking logic in src/gop_manager.c
- [ ] T013 Threading and synchronization primitives
- [ ] T014 GStreamer pad callbacks and event handling
- [ ] T015 State management and mode transitions
- [ ] T016 Property system and configuration
- [ ] T017 Error handling and GStreamer debug categories

## Phase 3.4: Integration
- [ ] T018 Plugin factory registration and metadata
- [ ] T019 Caps negotiation implementation
- [ ] T020 Pipeline event forwarding (EOS, FLUSH, SEEK)
- [ ] T021 Memory management and cleanup

## Phase 3.5: Polish  
- [ ] T022 [P] Valgrind memory leak testing in tests/memory/
- [ ] T023 Performance validation against benchmarks
- [ ] T024 [P] GTK-Doc documentation generation
- [ ] T025 Code style compliance check
- [ ] T026 Integration with common GStreamer elements

## Dependencies
- Tests (T004-T009) before implementation (T010-T017)
- T010 blocks T013, T014, T015
- T011 blocks T015, T021
- T012 blocks T015, T016
- Implementation before integration (T018-T021)
- Integration before polish (T022-T026)

## Parallel Example
```
# Launch T004-T009 together:
Task: "Plugin registration test in tests/unit/test_plugin_registration.c"
Task: "Thread safety test in tests/unit/test_thread_safety.c"
Task: "GOP boundary test in tests/unit/test_gop_processing.c"
Task: "Pipeline integration test in tests/integration/test_pipeline.c"
Task: "State transition test in tests/integration/test_state_management.c"
Task: "Performance benchmark in tests/performance/test_buffer_ops.c"
```

## Notes
- [P] tasks = different files, no dependencies
- Verify tests fail before implementing
- Commit after each task
- Avoid: vague tasks, same file conflicts

## Task Generation Rules
*Applied during main() execution*

1. **From Contracts**:
   - Each contract file → contract test task [P]
   - Each endpoint → implementation task
   
2. **From Data Model**:
   - Each entity → model creation task [P]
   - Relationships → service layer tasks
   
3. **From User Stories**:
   - Each story → integration test [P]
   - Quickstart scenarios → validation tasks

4. **Ordering**:
   - Setup → Tests → Models → Services → Endpoints → Polish
   - Dependencies block parallel execution

## Validation Checklist
*GATE: Checked by main() before returning*

- [ ] All contracts have corresponding tests
- [ ] All entities have model tasks
- [ ] All tests come before implementation
- [ ] Parallel tasks truly independent
- [ ] Each task specifies exact file path
- [ ] No task modifies same file as another [P] task