# Tasks: Remove Conan Dependency Manager

**Input**: Design documents from `/Users/kartikaiyer/fun/gst_my_filter/specs/003-remove-conan-currently/`
**Prerequisites**: plan.md, research.md, data-model.md, contracts/, quickstart.md

## Execution Flow (main)
```
1. Load plan.md from feature directory
   → Extract: CMake 3.27+, GStreamer 1.26+ (Homebrew), build system refactoring
   → Structure: Single project (GStreamer plugin)
2. Load design documents:
   → data-model.md: 6 entities (CMakePresets, CI Workflows, Build Script, README, CMakeUserPresets, Conan artifacts)
   → contracts/: CMakePresets.json.example schema
   → quickstart.md: 7-part validation procedure
   → research.md: 10 decision areas
3. Generate tasks by category:
   → Setup: CMakePresets creation, .gitignore update
   → Migration: CI workflows, build scripts, documentation
   → Cleanup: Conan artifact removal
   → Validation: Quickstart execution on macOS and Linux
4. Apply task rules:
   → Different files = mark [P] for parallel
   → Sequential for CI workflows (both reference same preset names)
   → No code changes = no tests needed
5. Number tasks sequentially (T001-T013)
6. Validate completeness:
   → All 6 entities have modification tasks ✅
   → All quickstart parts have validation tasks ✅
   → Rollback procedure documented ✅
7. Return: SUCCESS (tasks ready for execution)
```

## Format: `[ID] [P?] Description`
- **[P]**: Can run in parallel (different files, no dependencies)
- Include exact file paths in descriptions
- **Note**: This is a configuration refactoring, NOT code implementation - no TDD cycle needed

## Path Conventions
Repository root: `/Users/kartikaiyer/fun/gst_my_filter/`
- Build configs: `CMakePresets.json`, `.gitignore`
- CI workflows: `.github/workflows/`
- Build scripts: `.ci/`
- Documentation: `README.md`
- Conan files: `conanfile.py` (to be deleted)

---

## Phase 3.1: Setup & Preparation
**Goal**: Create new CMake preset infrastructure before removing Conan

- [x] **T001** [P] Create repository CMakePresets.json with debug and release presets
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/CMakePresets.json`
  - **Content**: Based on `contracts/CMakePresets.json.example` with:
    - Schema version 6 (CMake 3.27+)
    - configurePresets: `debug` and `release`
    - buildPresets: `debug` and `release`
    - binaryDir: `${sourceDir}/build/Debug` and `${sourceDir}/build/Release`
    - CMAKE_BUILD_TYPE, CMAKE_EXPORT_COMPILE_COMMANDS cache variables
  - **Validation**: File exists, JSON is valid, preset names are lowercase
  - **Dependencies**: None (first task)

- [x] **T002** [P] Update .gitignore to exclude CMakeUserPresets.json
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/.gitignore`
  - **Action**: Add line `CMakeUserPresets.json` (user-specific, not version-controlled)
  - **Rationale**: CMakeUserPresets.json is for local developer customization only
  - **Validation**: `CMakeUserPresets.json` present in .gitignore
  - **Dependencies**: None (parallel with T001)

---

## Phase 3.2: CI Workflow Migration
**Goal**: Remove Conan installation steps from CI, use native CMake presets

- [x] **T003** Update .github/workflows/ci.yml to remove Conan and use native presets
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/.github/workflows/ci.yml`
  - **Actions**:
    1. **Remove** these steps from both `build-and-test-linux` and `build-and-test-macos` jobs:
       - "Install Conan" (pip install conan)
       - "Configure Conan profile" (conan profile detect)
       - "Install Conan dependencies (Debug)" (conan install Debug)
       - "Install Conan dependencies (Release)" (conan install Release)
    2. **Linux job only**: Remove Python virtual environment setup:
       - Remove `python3-venv` from apt-get install list
       - Remove "Setup Python Virtual Environment" step entirely
    3. **Update** "Run CI test script" step to keep script invocation (script itself will be updated in T004)
    4. **Verify** GStreamer installation step remains: `brew install gstreamer`
    5. **Keep** all test execution and artifact upload steps unchanged
  - **Preset References**: Script will use `debug` and `release` (not `conan-debug`, `conan-release`)
  - **Success Criteria**:
    - ✅ FR-012: ubuntu-22.04 workflow has no Conan steps
    - ✅ FR-013: macos-latest workflow has no Conan steps
    - ✅ FR-033: No Python virtual environment setup
    - ✅ FR-034: python3-venv not in apt-get install list
  - **Validation**: No references to "conan" or "venv" in workflow file, GStreamer install preserved
  - **Dependencies**: T001 (needs CMakePresets.json to exist)

---

## Phase 3.3: Build Script Refactoring
**Goal**: Simplify build script to use native CMake presets directly

- [x] **T004** Refactor .ci/run-tests.sh to remove conan_preset() function
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/.ci/run-tests.sh`
  - **Actions**:
    1. **Remove** entire `conan_preset()` function definition
    2. **Replace** `conan_preset Debug` calls with:
       ```bash
       cmake --preset=debug
       cmake --build --preset=debug --parallel 6
       ```
    3. **Replace** `conan_preset Release` calls with:
       ```bash
       cmake --preset=release
       cmake --build --preset=release --parallel 6
       ```
    4. **Update** ctest invocation to use `--test-dir build/Debug` and `--test-dir build/Release`
    5. **Remove** any generator directory checks (no longer needed)
  - **Validation**: Script runs successfully, no "conan" references
  - **Dependencies**: T001 (needs CMakePresets.json to exist), T003 (CI calls this script)

---

## Phase 3.4: Documentation Updates
**Goal**: Update README with Conan-free build workflow

- [x] **T005** [P] Update README.md to document Conan-free build process
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/README.md`
  - **Actions**:
    1. **Update Prerequisites section**:
       - List GStreamer 1.26+ via Homebrew as **first** prerequisite (FR-028)
       - Add installation commands: `brew install gstreamer`
       - Specify CMake 3.27+ requirement (FR-021)
       - Note pkg-config is included with Homebrew
       - **Remove** any Conan installation instructions
    2. **Update Quick Start / Building section**:
       - Replace Conan workflow with:
         ```bash
         cmake --preset=debug
         cmake --build --preset=debug
         ctest --test-dir build/Debug
         ```
       - Document release build similarly
       - **Remove** `conan install` commands
    3. **Update Troubleshooting section**:
       - Add guidance for GStreamer not found (FR-020)
       - Document CMake 3.27+ requirement
       - Remove Conan-related troubleshooting
    4. **Add Optional Customization note**:
       - Mention users can create `CMakeUserPresets.json` locally for custom overrides
       - Note this file is gitignored and not required
  - **Validation**: No "conan" references, clear GStreamer installation steps, preset commands correct
  - **Dependencies**: T001 (documents preset workflow)

---

## Phase 3.5: Conan Artifact Removal
**Goal**: Delete Conan configuration files and cleanup generated artifacts

- [x] **T006** [P] Delete conanfile.py from repository
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/conanfile.py`
  - **Action**: Delete file (git rm)
  - **Rationale**: Conan provides zero dependencies, only generates presets (research.md §1)
  - **Validation**: File no longer exists in repository
  - **Dependencies**: T001, T003, T004, T005 (all Conan usages removed first)

- [x] **T007** [P] Delete CMakeUserPresets.json from repository
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/CMakeUserPresets.json`
  - **Action**: Delete file (git rm)
  - **Rationale**: User-specific file, should not be version-controlled (updated research.md §8)
  - **Note**: Users can recreate locally if they want custom overrides
  - **Validation**: File deleted, present in .gitignore (from T002)
  - **Dependencies**: T002 (.gitignore updated first)

- [x] **T008** [P] Document build directory cleanup in commit message
  - **Action**: Create commit with detailed message documenting:
    - `build/Debug/generators/` directory is no longer generated (Conan artifact)
    - `build/Release/generators/` directory is no longer generated
    - These were gitignored, so no repo changes needed
    - Developers should clean build directories: `rm -rf build/`
  - **Validation**: Commit message includes cleanup instructions
  - **Dependencies**: T006 (conanfile.py deleted)

---

## Phase 3.6: Local Validation (macOS)
**Goal**: Execute quickstart.md validation procedure on macOS

- [x] **T009** Execute quickstart.md Parts 1-4 on macOS development machine
  - **Reference**: `/Users/kartikaiyer/fun/gst_my_filter/specs/003-remove-conan-currently/quickstart.md`
  - **Actions**:
    1. **Part 1**: Verify prerequisites (GStreamer 1.26+, CMake 3.27+, no Conan)
    2. **Part 2**: Configure and build Debug
       - `cmake --preset=debug` succeeds
       - `cmake --build --preset=debug` completes
       - All 22 tests pass: `ctest --test-dir build/Debug`
    3. **Part 3**: Configure and build Release
       - `cmake --preset=release` succeeds
       - `cmake --build --preset=release` completes
       - All 22 tests pass: `ctest --test-dir build/Release`
    4. **Part 4**: Plugin registration validation
       - `gst-inspect-1.0 pre_record_loop` succeeds
       - Plugin metadata correct
  - **Success Criteria**:
    - ✅ FR-001: Builds without Conan
    - ✅ FR-008: All 22 tests pass
    - ✅ FR-017: Plugin registers successfully
  - **Validation**: Document results in quickstart.md sign-off section
  - **Dependencies**: T001-T008 (all changes committed)

- [x] **T010** Execute quickstart.md Part 7 (CI simulation) locally
  - **Reference**: quickstart.md Part 7
  - **Actions**:
    1. Run `.ci/run-tests.sh` locally
    2. Verify both Debug and Release builds complete
    3. Verify all tests pass
    4. Confirm no Conan commands executed
  - **Validation**: Script completes successfully, output shows "✅ All tests passed!"
  - **Dependencies**: T004 (script updated), T009 (basic validation passed)

---

## Phase 3.7: CI Validation (Linux)
**Goal**: Verify changes work in GitHub Actions CI environment

- [x] **T011** Push branch and verify CI workflows succeed on ubuntu-22.04 and macos-latest
  - **Actions**:
    1. Commit all changes (T001-T008) to branch `003-remove-conan-currently`
    2. Push to GitHub
    3. Monitor `.github/workflows/ci.yml` execution
  - **Success Criteria**:
    - ✅ FR-012: ubuntu-22.04 build and test succeeds
    - ✅ FR-013: macos-latest build and test succeeds
    - ✅ FR-015: Both Debug and Release builds complete
    - ✅ FR-016: Code style checks pass
  - **Validation**: All CI checks green, no Conan-related errors
  - **Dependencies**: T001-T010 (all changes committed and tested locally)
  - **Result**: ✅ PASS - All jobs succeeded on both platforms (2025-10-17)

---

## Phase 3.8: CI Performance Optimization
**Goal**: Cache all installed dependencies (apt packages + Homebrew packages) to reduce CI workflow execution time

- [ ] **T012** Add GitHub Actions cache for apt and Homebrew dependencies
  - **File**: `/Users/kartikaiyer/fun/gst_my_filter/.github/workflows/ci.yml`
  - **Actions**:
    
    **For Linux job (ubuntu-22.04)**:
    1. Add apt package cache before "Install system dependencies" step:
       - Cache paths: `/var/cache/apt/archives`, `/var/lib/apt/lists`
       - Cache key: `${{ runner.os }}-apt-${{ hashFiles('.github/workflows/ci.yml') }}`
       - Note: Caches cmake, valgrind, build-essential, pkg-config, clang-format (python3-venv removed in T003)
       - Conditional apt-get: Run only if cache miss or use `apt-cache policy` to verify
    
    2. Add Homebrew cache before "Install GStreamer" step:
       - Cache paths:
         * `/home/linuxbrew/.linuxbrew/Cellar`
         * `/home/linuxbrew/.linuxbrew/lib/pkgconfig`
         * `/home/linuxbrew/.linuxbrew/include`
       - Cache key: `${{ runner.os }}-linuxbrew-${{ hashFiles('.github/workflows/ci.yml') }}`
       - Conditional install: `if: steps.cache-homebrew.outputs.cache-hit != 'true'`
    
    **For macOS job (macos-latest)**:
    3. Add Homebrew cache before "Install Homebrew dependencies" step:
       - Cache paths:
         * `/opt/homebrew/Cellar`
         * `/opt/homebrew/lib/pkgconfig`
         * `/opt/homebrew/include`
       - Cache key: `${{ runner.os }}-homebrew-${{ hashFiles('.github/workflows/ci.yml') }}`
       - Conditional install: `if: steps.cache-homebrew.outputs.cache-hit != 'true'`
    
    4. Use `actions/cache@v4` for all cache steps
    5. Add restore-keys for graceful fallback (OS-based prefix)
    
  - **Success Criteria**:
    - ✅ FR-029: Cache steps added for all installed dependencies
    - ✅ FR-030: Separate cache keys for apt and Homebrew
    - ✅ FR-031: Cache hit skips package installations
    - ✅ FR-032: Cache miss falls back to fresh installs
  - **Validation**: 
    - First workflow run: cache miss, full install (~10 minutes Linux, ~8 minutes macOS)
    - Second workflow run: cache hit, restore only (~1 minute total for both caches)
    - Both runs complete with all tests passing
  - **Dependencies**: T011 (CI validation working)

---

## Phase 3.9: CI Cache Validation
**Goal**: Verify cache performance improvement in real GitHub Actions environment

- [ ] **T013** Validate CI cache performance and measure time savings
  - **Actions**:
    1. Push branch with cache changes to GitHub
    2. Monitor first workflow run (cache miss - baseline)
    3. Trigger second workflow run (cache hit - optimized)
    4. Compare workflow execution times in GitHub Actions logs
    5. Document cache hit/miss times for both apt and Homebrew caches in quickstart.md
    6. Verify cache size is reasonable (<2GB total per platform)
  - **Success Criteria**:
    - ✅ First run: Both caches miss, all packages installed, caches saved
    - ✅ Second run: Both caches hit, packages restored from cache
    - ✅ Time savings Linux: 7-10 minutes (apt: ~3min, Homebrew: ~5-7min)
    - ✅ Time savings macOS: 5-8 minutes (Homebrew: ~5-8min)
    - ✅ All 22 tests pass on both runs
    - ✅ Cache logs show successful restore and save operations
  - **Validation**: GitHub Actions logs show cache restore success, reduced execution time, and reasonable cache sizes
  - **Dependencies**: T012 (cache implementation)

---

## Phase 3.10: Final Documentation Review
**Goal**: Ensure all documentation is consistent and complete

- [ ] **T014** [P] Final review and update spec.md status
  - **Files**:
    - `/Users/kartikaiyer/fun/gst_my_filter/specs/003-remove-conan-currently/spec.md`
    - `/Users/kartikaiyer/fun/gst_my_filter/specs/003-remove-conan-currently/quickstart.md`
  - **Actions**:
    1. Update spec.md Status from "Draft" to "Ready for Review"
    2. Fill out quickstart.md Validation Sign-Off section:
       - Test dates for macOS and Linux
       - Tester name
       - Commit SHA
       - Result: PASS
    3. Verify all functional requirements documented as satisfied
    4. Document CI execution time in PR description (FR: document but don't measure)
  - **Validation**: Spec marked complete, quickstart sign-off filled
  - **Dependencies**: T009, T011, T013 (validation complete with cache optimization)

---

## Dependencies Graph

```
T001 (CMakePresets.json) ──┬─→ T003 (ci.yml)
                           ├─→ T004 (run-tests.sh)
                           └─→ T005 (README.md)

T002 (.gitignore) ──→ T007 (delete CMakeUserPresets)

T003, T004, T005 ──→ T006 (delete conanfile.py) ──→ T008 (cleanup docs)

T001-T008 ──→ T009 (local validation) ──→ T010 (CI simulation)

T009, T010 ──→ T011 (CI validation) ──→ T012 (CI caching) ──→ T013 (cache validation) ──→ T014 (final review)
```

## Parallel Execution Opportunities

### Batch 1: Setup (Independent Files)
```bash
# These can run in parallel - different files, no dependencies
Task T001: "Create CMakePresets.json"
Task T002: "Update .gitignore"
```

### Batch 2: Cleanup (After Migration Complete)
```bash
# These can run in parallel after T001-T006 complete
Task T007: "Delete conanfile.py"
Task T008: "Delete CMakeUserPresets.json"
Task T009: "Document cleanup"
```

### Sequential: Migration & Validation
- T003, T004 must be sequential (interdependent references)
- T005 can run parallel with T003-T004 (different concerns)
- T009-T012 must be sequential (validation chain)

## Validation Checklist
*GATE: Confirm before marking tasks complete*

- [x] All 6 entities from data-model.md have tasks
  - CMakePresets.json (T001)
  - CI Workflows (T003)
  - Build Script (T004)
  - README (T005)
  - CMakeUserPresets (T007)
  - Conan artifacts (T006, T008)
- [x] All quickstart.md parts have validation tasks (T009, T010, T011)
- [x] Each task specifies exact file path
- [x] Parallel tasks ([P]) truly independent (different files)
- [x] No code changes = no test tasks (configuration only)
- [x] Rollback procedure documented in research.md

## Task Summary

**Total Tasks**: 14  
**Parallel Tasks**: 5 (T001, T002, T005, T006, T007, T008)  
**Sequential Tasks**: 9 (T003-T004, T009-T014)  

**Estimated Effort**:
- Setup & Migration (T001-T005): 2-3 hours
- Cleanup (T006-T008): 30 minutes
- Validation (T009-T011): 2-3 hours (includes CI wait time)
- CI Optimization (T012-T013): 1-2 hours (cache implementation + validation)
- Final Review (T014): 30 minutes
- **Total**: 6-9 hours

**Risk Level**: Low (configuration only, easily reversible via git revert)

---

## Notes for Execution

1. **No TDD Cycle**: This is configuration refactoring, not code implementation. No tests need to be written - existing 22 tests validate functionality is preserved.

2. **Rollback Strategy** (from clarifications): 
   - Trigger: Developer consensus after attempting fixes (no time limit)
   - Procedure: `git revert` commits → restore conanfile.py → re-run `conan install`

3. **CMakeUserPresets.json**: Now deleted and gitignored. Users can create locally if needed for custom overrides (e.g., different generator, custom cache variables).

4. **Commit Strategy**: 
   - Commit T001-T002 together (setup)
   - Commit T003-T004 together (migration)
   - Commit T005 separately (documentation)
   - Commit T006-T008 together (cleanup)
   - Final commit after T012 (spec update)

5. **Build Directory Cleanup**: Developers with existing builds should run `rm -rf build/` to remove Conan-generated artifacts.

6. **CI Execution Time**: Document but don't measure baseline (clarification from spec session).

---

**Ready for Execution**: All tasks are specific, have clear file paths, and include validation criteria. Begin with T001 and T002 in parallel.
