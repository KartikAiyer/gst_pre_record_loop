# Feature Specification: Remove Conan Dependency Manager

**Feature Branch**: `003-remove-conan-currently`  
**Created**: 2025-10-17  
**Status**: Draft  
**Input**: User description: "Remove Conan. Currently I'm using conan but don't really have any dependencies. I'm using it to generate cmake preset files and a toolchain file. However it doesn't really seem that I need a toolchain file because it feels that the default options for a compiler on both the mac and linux should be sufficient. I would like to remove the use of conan and make sure that all build steps and test steps and ci work as expected after removing it."

## Execution Flow (main)
```
1. Parse user description from Input
   → Feature: Remove Conan package manager from build system
2. Extract key concepts from description
   → Actors: Developers, CI systems
   → Actions: Build, test, configure CMake
   → Constraints: Must work on macOS and Linux, no dependencies managed by Conan
3. For each unclear aspect:
   → [RESOLVED] Only GStreamer is external dependency (found via pkg-config)
4. Fill User Scenarios & Testing section
   → Developers build locally, CI runs tests
5. Generate Functional Requirements
   → Each requirement testable via build/test execution
6. Identify Key Components
   → CMake configuration, CI workflows, build scripts
7. Run Review Checklist
   → No implementation ambiguities, no tech stack restrictions
8. Return: SUCCESS (spec ready for planning)
```

---

## ⚡ Quick Guidelines
- ✅ Focus on WHAT users need and WHY
- ❌ Avoid HOW to implement (no tech stack, APIs, code structure)
- 👥 Written for business stakeholders, not developers

---

## Clarifications

### Session 2025-10-17
- Q: What is the threshold for triggering a rollback to Conan? → A: Developer consensus after attempting fixes (no time limit)
- Q: What is the current (baseline) CI execution time to use for comparison? → A: Don't measure, just document

---

## User Scenarios & Testing *(mandatory)*

### Primary User Story
As a **developer working on the prerecordloop GStreamer plugin**, I want to **build and test the project without requiring Conan package manager** so that **I can use standard CMake workflows and reduce build complexity when the project has no external dependencies managed by Conan**.

### Acceptance Scenarios

#### Scenario 1: Local Development Build (macOS)
1. **Given** a macOS development machine
2. **And** the developer has installed GStreamer 1.26+ via `brew install gstreamer` (as documented in README)
3. **When** the developer runs CMake configure and build commands
3. **Then** the project configures successfully using CMake presets without Conan-generated files
4. **And** all targets (plugin, tests, test application) build successfully
5. **And** all 22 tests pass when executed via ctest

#### Scenario 2: Local Development Build (Linux)
1. **Given** a Linux development machine
2. **And** the developer has installed GStreamer 1.26+ via Homebrew/Linuxbrew (as documented in README)
3. **When** the developer runs CMake configure and build commands
3. **Then** the project configures successfully using CMake presets without Conan-generated files
4. **And** all targets (plugin, tests, test application) build successfully
5. **And** all 22 tests pass when executed via ctest

#### Scenario 3: CI Build and Test (Ubuntu)
1. **Given** the GitHub Actions CI environment on ubuntu-22.04
2. **When** the CI workflow runs build and test steps
3. **Then** apt-get packages (cmake, valgrind, build-essential, pkg-config, clang-format) are restored from GitHub Actions cache if available
4. **And** Homebrew packages (GStreamer) are restored from GitHub Actions cache if available
5. **Or** packages are installed fresh via apt-get and Homebrew if cache miss
6. **And** no Python virtual environment is created (not needed without Conan)
7. **And** CMake configures the project without Conan installation steps
8. **And** Debug and Release builds complete successfully
9. **And** all tests pass in both configurations
10. **And** code style checks complete successfully
11. **And** all installed packages (apt + Homebrew) are cached for subsequent workflow runs

#### Scenario 4: CI Build and Test (macOS)
1. **Given** the GitHub Actions CI environment on macos-latest
2. **When** the CI workflow runs build and test steps
3. **Then** Homebrew packages (gstreamer, cmake, clang-format) are restored from GitHub Actions cache if available
4. **Or** packages are installed via Homebrew if cache miss
5. **And** CMake configures the project without Conan installation steps
6. **And** Debug and Release builds complete successfully
7. **And** all tests pass in both configurations
8. **And** code style checks complete successfully
9. **And** Homebrew packages are cached for subsequent workflow runs

#### Scenario 5: CI Memory Testing (Valgrind on Linux)
1. **Given** the Valgrind CI workflow on ubuntu-22.04
2. **When** the workflow builds and runs memory tests
3. **Then** the build completes without Conan dependency installation
4. **And** Valgrind leak tests execute and pass

#### Scenario 6: Clean Build from Scratch
1. **Given** a fresh clone of the repository with no build artifacts
2. **When** the developer runs the documented quick-start build commands
3. **Then** no Conan-related errors or warnings appear
4. **And** the build completes successfully with default CMake configuration
5. **And** tests can be run immediately after build

#### Scenario 7: CI Cache Performance
1. **Given** GitHub Actions workflow has run at least once (caches populated)
2. **When** a subsequent workflow run is triggered
3. **Then** the cache restore steps succeed for both apt packages (Linux) and Homebrew packages
4. **And** package installations are skipped (already cached)
5. **And** total workflow time is significantly reduced compared to cache miss (5-10 minutes saved)
6. **And** builds and tests complete successfully using cached dependencies

### Edge Cases
- What happens when GStreamer is not found via pkg-config?
  - CMake MUST fail with a clear error message indicating GStreamer >= 1.26 is required via Homebrew installation
- What happens if a developer has Conan installed but the project doesn't use it?
  - Build MUST succeed without attempting to use Conan or requiring Conan files
- What happens on a system with non-Homebrew GStreamer installation?
  - CMake may fail to find GStreamer; documentation should clearly state Homebrew is the expected installation method
- What happens on a system with non-standard GStreamer installation paths?
  - CMake MUST still find GStreamer via PKG_CONFIG_PATH or standard pkg-config search paths if properly configured
- What happens if CMakeUserPresets.json references removed Conan presets?
  - CMake MUST fail during configuration with an actionable error
- What happens when running the old `.ci/run-tests.sh` script after Conan removal?
  - Script MUST either be updated or fail gracefully with clear error

## Requirements *(mandatory)*

### Functional Requirements

#### Build System Requirements
- **FR-001**: System MUST configure and build successfully using only CMake without requiring Conan package manager
- **FR-002**: System MUST provide CMake configure presets AND build presets for Debug and Release configurations that do not depend on Conan-generated files
- **FR-003**: System MUST detect GStreamer 1.26+ (installed via Homebrew) via pkg-config and fail configuration with clear error message if not found
- **FR-004**: System MUST support building on macOS (Apple Silicon and Intel) without Conan
- **FR-005**: System MUST support building on Linux (x86_64) without Conan
- **FR-006**: System MUST preserve all existing build options (BUILD_GTK_DOC, PREREC_ENABLE_LIFE_DIAG, ENABLE_ASAN)
- **FR-007**: CMake MUST auto-detect GStreamer 1.26+ and glib pkg-config paths from Homebrew installations on macOS and Linux (Linuxbrew)

#### Testing Requirements
- **FR-008**: System MUST execute all 22 existing tests successfully (17 unit, 3 integration, 1 performance, 1 memory)
- **FR-009**: System MUST support running tests via ctest with Debug and Release configurations
- **FR-010**: Memory leak tests MUST continue to work with macOS `leaks` tool and Linux Valgrind
- **FR-011**: Code style checks MUST continue to work with clang-format

#### CI/CD Requirements
- **FR-012**: CI workflows MUST build and test on ubuntu-22.04 without Conan installation steps
- **FR-013**: CI workflows MUST build and test on macos-latest without Conan installation steps
- **FR-014**: Valgrind CI workflow MUST run memory leak tests without Conan
- **FR-015**: CI build scripts MUST complete both Debug and Release builds successfully
- **FR-016**: CI MUST enforce code style checks before running tests
- **FR-017**: CI MUST validate plugin registration with gst-inspect-1.0
- **FR-029**: CI workflows MUST cache all installed dependencies (apt-get packages on Linux, Homebrew packages on macOS/Linux) to avoid reinstalling on every workflow run
- **FR-030**: CI workflows MUST use GitHub Actions cache with separate cache keys for apt packages and Homebrew packages based on runner OS and dependency lockfiles
- **FR-031**: CI cache restoration MUST significantly reduce workflow execution time by skipping package reinstallation when cache is valid (90%+ of dependency installation time)
- **FR-032**: CI workflows MUST handle cache misses gracefully by falling back to fresh package installation via apt-get or Homebrew
- **FR-033**: CI workflows MUST NOT include Python virtual environment setup steps (only needed for Conan, which is removed)
- **FR-034**: Linux CI workflow MUST NOT install python3-venv package (only needed for Conan)

#### Documentation Requirements
- **FR-018**: README MUST document quick-start build commands without Conan references
- **FR-019**: Documentation MUST explain how to install GStreamer 1.26+ via Homebrew (macOS) and Homebrew/Linuxbrew (Linux) as the first prerequisite
- **FR-020**: Build error messages MUST guide users when GStreamer is not found
- **FR-021**: Documentation MUST specify minimum CMake version required (3.27+)

#### File System Requirements
- **FR-022**: System MUST NOT require `conanfile.py` to exist
- **FR-023**: System MUST NOT require `build/*/generators/` directories from Conan
- **FR-024**: System MUST NOT create or reference Conan cache directories
- **FR-025**: CMakeUserPresets.json MUST NOT be version-controlled (user-specific local customization file only)
- **FR-026**: Build artifacts MUST be created in `build/Debug/` and `build/Release/` directories
- **FR-027**: CMakePresets.json MUST include both configurePresets (for cmake --preset) and buildPresets (for cmake --build --preset) for Debug and Release
- **FR-028**: README MUST document that GStreamer 1.26+ must be installed via Homebrew on both macOS and Linux as a build prerequisite

### Key Components *(build system components)*

#### CMake Configuration
- **Purpose**: Primary build system configuration
- **Key Elements**:
  - CMakeLists.txt (root) - main project configuration
  - gstprerecordloop/CMakeLists.txt - plugin library target
  - testapp/CMakeLists.txt - test application target
  - tests/CMakeLists.txt - test executable targets
- **Dependencies**: GStreamer 1.26+, glib 2.0 (found via pkg-config)
- **Presets**: Debug and Release configurations with compiler flags

#### CI Workflows
- **Purpose**: Automated build and test validation
- **Workflows**:
  - `.github/workflows/ci.yml` - main CI pipeline (Ubuntu + macOS)
  - `.github/workflows/valgrind.yml` - memory leak testing (Ubuntu)
- **Build Script**: `.ci/run-tests.sh` - unified build/test execution

#### CMake Presets File
- **Purpose**: Standardized configuration and build profiles for consistent development workflow
- **Location**: CMakePresets.json (repository root, version-controlled, not Conan-generated)
- **Preset Types**: Both configure presets and build presets
- **Configurations**: Debug (with symbols, no optimization) and Release (optimized)
- **Usage**: Developers use `cmake --preset=debug` and `cmake --build --preset=debug` (similarly for release)

#### Build Artifacts
- **Plugin**: `build/*/gstprerecordloop/libgstprerecordloop.so` (or .dylib on macOS)
- **Test App**: `build/*/testapp/prerec.app/Contents/MacOS/prerec`
- **Tests**: `build/*/tests/` directory with executable binaries

---

## Review & Acceptance Checklist
*GATE: Automated checks run during main() execution*

### Content Quality
- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

### Requirement Completeness
- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous  
- [x] Success criteria are measurable (build success, test pass rates)
- [x] Scope is clearly bounded (remove Conan, preserve functionality)
- [x] Dependencies identified (GStreamer 1.26+, pkg-config)

---

## Execution Status
*Updated by main() during processing*

- [x] User description parsed
- [x] Key concepts extracted (remove Conan, CMake presets, no dependencies)
- [x] Ambiguities marked (none - scope is clear)
- [x] User scenarios defined (6 scenarios covering local + CI builds)
- [x] Requirements generated (26 functional requirements)
- [x] Entities identified (CMake config, CI workflows, presets)
- [x] Review checklist passed

---

## Success Metrics

### Build Success Rate
- **Metric**: Percentage of successful builds across platforms
- **Target**: 100% success on macOS and Linux with clean clone
- **Measurement**: CI workflow pass/fail status

### Test Execution
- **Metric**: Number of passing tests
- **Target**: 22/22 tests pass in Debug and Release
- **Measurement**: ctest output showing 100% pass rate

### CI Execution Time
- **Metric**: Time to complete CI build+test workflow
- **Target**: CI must complete successfully; cache hit should significantly reduce time spent on dependency installation (5+ minutes saved per workflow run)
- **Measurement**: Compare workflow duration with cache hit vs cache miss in GitHub Actions logs

### Developer Experience
- **Metric**: Number of build steps required
- **Target**: 3 commands using presets (configure via preset, build via preset, test)
- **Example**: `cmake --preset=debug`, `cmake --build --preset=debug`, `ctest --test-dir build/Debug`
- **Measurement**: README quick-start section

---

## Assumptions and Dependencies

### Assumptions
1. GStreamer 1.26+ is installed via Homebrew (macOS) or Homebrew/Linuxbrew (Linux) as documented in README
2. pkg-config utility is installed on development and CI systems (typically included with Homebrew)
3. CMake 3.27+ is available (required for preset support)
4. No third-party C/C++ libraries require package management (only GStreamer)
5. Default compiler flags are sufficient for Debug and Release builds
6. CMake presets (configure and build) will be maintained in repository for consistent workflow
7. Existing test suite adequately validates functionality

### External Dependencies
1. **GStreamer 1.26+**: Core dependency, detected via pkg-config
2. **glib 2.0**: GStreamer dependency, detected via pkg-config
3. **pkg-config**: Build-time tool for finding libraries
4. **CMake 3.27+**: Build system generator
5. **clang-format**: Code style validation (optional in CI)
6. **Valgrind**: Memory leak detection on Linux (optional)

### Internal Dependencies
1. All existing code remains unchanged (no code refactoring required)
2. Test utilities and framework remain unchanged
3. Plugin registration and GStreamer integration unchanged

---

## Risk Assessment

### Technical Risks
1. **Risk**: CMake preset configuration may not replicate Conan toolchain settings
   - **Mitigation**: Use standard CMake compiler detection and platform defaults
   - **Impact**: Low - no custom toolchain needed for this project

2. **Risk**: CI workflows may fail due to missing Conan setup steps
   - **Mitigation**: Test CI changes in feature branch before merging
   - **Impact**: Medium - can be fixed before merge

3. **Risk**: Developers with existing builds may have stale Conan artifacts
   - **Mitigation**: Document clean build steps in PR description
   - **Impact**: Low - developers can clean build directory

### Process Risks
1. **Risk**: Documentation may not be updated comprehensively
   - **Mitigation**: Review all README sections and CI docs
   - **Impact**: Medium - incomplete docs can confuse users

2. **Risk**: Some CI checks may be overlooked during removal
   - **Mitigation**: Run full CI matrix before marking complete
   - **Impact**: Low - CI will catch issues

---

## Rollback Plan

If the Conan removal causes unforeseen issues:

**Decision Criteria**: Rollback is triggered by developer consensus after attempting forward fixes. There is no fixed time limit - the team will assess whether issues are solvable within reasonable effort or indicate fundamental problems requiring Conan restoration.

**Rollback Procedure**:
1. **Immediate**: Revert the feature branch commits
2. **Short-term**: Restore `conanfile.py` and Conan CI steps from previous commit
3. **Investigation**: Document the specific incompatibility or missing configuration that caused rollback
4. **Resolution**: Create new spec addressing root cause before re-attempting removal

**Risk Level**: Low - this is purely build system configuration with no data loss risk. Failed builds block development but don't corrupt artifacts.

---
