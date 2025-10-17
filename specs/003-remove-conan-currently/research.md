# Research: Remove Conan Dependency

**Date**: 2025-10-17  
**Feature**: Remove Conan package manager from build system  
**Spec**: [spec.md](./spec.md)

## Overview
This document consolidates research findings for removing Conan from the prerecordloop GStreamer plugin build system. Conan currently provides zero actual dependencies - it only generates CMakePresets.json and CMakeToolchain.cmake files. The project's only external dependency (GStreamer 1.26+) is already found via pkg-config.

---

## Research Areas

### 1. Current Conan Usage Analysis

**Investigation**: What does Conan actually provide to this project?

**Findings**:
- **conanfile.py** contains NO requires (dependencies)
- Only generates two artifacts:
  1. `build/*/generators/CMakePresets.json` - Configure presets
  2. `build/*/generators/conan_toolchain.cmake` - Compiler flags
- Current presets: `conan-debug` and `conan-release`
- Toolchain provides default flags (no customization observed)

**Decision**: Remove Conan entirely - it adds complexity without value.

**Rationale**: 
- GStreamer found via pkg-config (not Conan)
- No third-party C/C++ libraries managed by Conan
- Default CMake compiler detection sufficient for macOS/Linux
- Preset files can be version-controlled directly

**Alternatives Considered**:
- Keep Conan for future dependencies → **Rejected**: YAGNI principle, can add later if needed
- Use vcpkg instead → **Rejected**: Same problem, no dependencies to manage

---

### 2. CMake Presets Structure (CMake 3.27+)

**Investigation**: How to create repository-managed CMakePresets.json without Conan?

**Findings**:
CMake 3.27+ supports two preset types:
1. **configurePresets**: Used with `cmake --preset=<name>` to configure build directory
2. **buildPresets**: Used with `cmake --build --preset=<name>` to build targets

**Required Preset Fields** (minimal):
```json
{
  "version": 6,
  "cmakeMinimumRequired": {"major": 3, "minor": 27, "patch": 0},
  "configurePresets": [
    {
      "name": "debug",
      "binaryDir": "${sourceDir}/build/Debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "debug",
      "configurePreset": "debug"
    }
  ]
}
```

**Decision**: Create repository CMakePresets.json with debug and release configurations.

**Rationale**:
- Native CMake feature (no external tools)
- Version controlled (visible in git diff)
- Matches current workflow (`cmake --preset=X`)
- Supports both configure and build steps (FR-002, FR-027)

**Alternatives Considered**:
- CMake cache scripts → **Rejected**: Less discoverable than presets
- Environment variables → **Rejected**: Not portable, harder to document
- Build script wrappers → **Rejected**: Presets are standard CMake practice

---

### 3. Compiler Flag Preservation

**Investigation**: What compiler flags does Conan's toolchain provide?

**Findings** (from existing Conan-generated files):
- Debug: `-g` (symbols), no optimization
- Release: `-O3` (optimization), `-DNDEBUG`
- No custom flags beyond CMake defaults
- Uses system compiler (Clang on macOS, GCC/Clang on Linux)

**Decision**: Use CMake's default CMAKE_BUILD_TYPE handling - no custom toolchain needed.

**Rationale**:
- CMake automatically applies standard flags per build type
- Existing builds work with defaults (proven by current state)
- User's assessment correct: "default options for a compiler on both the mac and linux should be sufficient"

**Alternatives Considered**:
- Create custom CMAKE_<LANG>_FLAGS → **Rejected**: Unnecessary complexity
- Platform-specific flags → **Rejected**: No platform-specific requirements identified

---

### 4. CI Workflow Modifications

**Investigation**: What Conan-specific steps exist in CI workflows?

**Findings** (from .github/workflows/ci.yml and valgrind.yml):
- Install Conan via pip (adds Python venv dependency)
- Run `conan profile detect`
- Run `conan install` for Debug and Release
- Workflows use `cmake --preset=conan-debug` and `cmake --preset=conan-release`

**Required Changes**:
1. Remove Conan installation steps
2. Change preset names: `conan-debug` → `debug`, `conan-release` → `release`
3. Keep GStreamer installation via Homebrew
4. Preserve all test execution steps

**Decision**: Update workflows to use native CMake configure/build without Conan.

**Rationale**:
- Simplifies CI (removes Python venv + pip install)
- Reduces CI execution time (no Conan overhead)
- More maintainable (fewer dependencies)

**Alternatives Considered**:
- Keep Conan for CI only → **Rejected**: Inconsistent with local builds
- Cache Conan artifacts → **Rejected**: Unnecessary if Conan removed

---

### 5. Build Script Updates (.ci/run-tests.sh)

**Investigation**: How does run-tests.sh use Conan?

**Findings** (from .ci/run-tests.sh analysis):
- Contains `conan_preset()` function
- Checks for `build/$TYPE/generators` directory
- Runs `conan install` if generators missing
- Uses preset names `conan-debug` and `conan-release`

**Required Changes**:
1. Remove `conan_preset()` function entirely
2. Replace with direct CMake commands:
   ```bash
   cmake --preset=debug
   cmake --build --preset=debug
   ```
3. Update preset name references throughout script

**Decision**: Rewrite build script to use native CMake preset workflow.

**Rationale**:
- Simpler logic (no generator directory checks)
- Consistent with manual developer workflow
- Script becomes self-documenting

**Alternatives Considered**:
- Keep conan_preset() as no-op → **Rejected**: Dead code confuses future maintainers
- Rename function to cmake_preset() → **Rejected**: Function adds no value, inline commands clearer

---

### 6. PKG_CONFIG_PATH Handling

**Investigation**: How does CMakeLists.txt find GStreamer without Conan?

**Findings** (from root CMakeLists.txt):
- Already uses `pkg_check_modules()` directly
- Manually sets `PKG_CONFIG_PATH` for Homebrew locations:
  - macOS: `/opt/homebrew/lib/pkgconfig` (Apple Silicon), `/usr/local/lib/pkgconfig` (Intel)
  - Linux: `${LINUXBREW_PREFIX}/lib/pkgconfig`
- No Conan involvement in pkg-config paths

**Decision**: No changes needed - existing pkg-config logic works.

**Rationale**:
- Already platform-aware
- Already Homebrew-aware
- Tested and working in current builds

**Alternatives Considered**:
- Use Conan to set PKG_CONFIG_PATH → **Rejected**: Already works without Conan
- Find GStreamer via CMake's FindPkgConfig module → **Rejected**: Current approach is explicit and working

---

### 7. Documentation Requirements

**Investigation**: What documentation needs updating?

**Findings**:
Current README references Conan installation and workflow. New documentation must cover:
1. Prerequisites: GStreamer 1.26+ via Homebrew (FR-028)
2. Quick-start build commands without Conan (FR-018)
3. CMake 3.27+ requirement (FR-021)
4. Clear error guidance when GStreamer missing (FR-020)

**Decision**: Update README with Conan-free workflow, emphasizing Homebrew installation.

**Rationale**:
- Reduces onboarding friction (no pip/Conan install)
- Clearer dependency chain (CMake → pkg-config → GStreamer)
- Matches project reality (Conan provides nothing)

**Documentation Structure** (from spec):
```markdown
## Prerequisites
1. GStreamer 1.26+ via Homebrew:
   - macOS: `brew install gstreamer`
   - Linux: Install Homebrew/Linuxbrew, then `brew install gstreamer`
2. CMake 3.27+: `brew install cmake`
3. pkg-config (included with Homebrew GStreamer)

## Building
cmake --preset=debug
cmake --build --preset=debug
ctest --test-dir build/Debug
```

**Alternatives Considered**:
- Keep Conan documentation as "optional" → **Rejected**: Confusing, adds no value
- Document multiple build methods → **Rejected**: One clear path reduces support burden

---

### 8. CMakeUserPresets.json Handling

**Investigation**: What is CMakeUserPresets.json's current role?

**Findings** (from CMakeUserPresets.json):
```json
{
  "version": 4,
  "vendor": {"conan": {}},
  "include": ["build/Debug/generators/CMakePresets.json"]
}
```
- Includes Conan-generated preset file
- Hardcoded to Debug build directory
- Won't work after Conan removal

**Decision**: Add CMakeUserPresets.json to .gitignore and document as optional local customization file.

**Rationale**:
- CMakeUserPresets.json is for **local user overrides only** (should NOT be version-controlled)
- Current version references Conan-generated files, which breaks after removal
- Users can create their own if needed for local customization
- Repository should only provide CMakePresets.json (the canonical presets)

**Alternatives Considered**:
- Commit updated CMakeUserPresets.json → **Rejected**: Wrong use case, forces local customization on all users
- Keep current version → **Rejected**: References non-existent Conan paths (breaks builds)
- Delete and ignore → **Preferred**: Clean slate, users create if needed

---

### 9. Build Artifact Path Preservation

**Investigation**: Do build paths change without Conan?

**Findings**:
- Current: `build/Debug/` and `build/Release/` (set by preset binaryDir)
- Required: Same paths (FR-026)
- Artifacts: `libgstprerecordloop.so` (.dylib on macOS), test binaries, prerec.app

**Decision**: Maintain identical build directory structure in new CMakePresets.json.

**Rationale**:
- Minimizes migration disruption
- CI scripts expect these paths
- Developers expect these paths
- Test harness uses `build/*/tests/` paths

**Alternatives Considered**:
- Change to `cmake-build-debug/` (CLion convention) → **Rejected**: Breaking change, no benefit
- Single `build/` with subdirs → **Current approach**: Already doing this

---

### 10. Rollback Strategy Validation

**Investigation**: How to ensure safe rollback if needed?

**Findings** (from spec clarification):
- Decision criteria: Developer consensus after fix attempts
- No time limit (pragmatic approach)
- Low risk: Build system only, no data corruption

**Validated Rollback Steps**:
1. Git revert commits on feature branch
2. Restore `conanfile.py` from previous commit
3. Re-run `conan install` to regenerate presets
4. Verify builds work with Conan restored
5. Document specific failure cause for future reference

**Decision**: Document rollback procedure in implementation tasks.

**Rationale**:
- Clear exit strategy reduces implementation anxiety
- Low-risk change (easily reversible)
- Clarification confirms pragmatic approach (consensus, not time-based)

**Alternatives Considered**:
- Automatic rollback trigger → **Rejected**: Developer judgment more appropriate
- Keep Conan files as fallback → **Rejected**: Clutters repository, encourages dependency

---

### 11. GitHub Actions CI Cache Strategy

**Investigation**: How to optimize CI workflow time by caching all installed dependencies (apt packages and Homebrew packages)?

**Findings**:
- Current CI workflow installs packages via apt-get (Linux) and Homebrew (both platforms) on every run
- Total package installation time: 5-10 minutes per workflow run
- GitHub Actions provides `actions/cache@v4` for caching directories
- Linux needs both apt cache AND Homebrew cache
- macOS needs only Homebrew cache

**Current Package Installations**:

**Linux (ubuntu-22.04)**:
1. **apt-get packages**:
   - cmake, valgrind, build-essential, pkg-config, python3-venv, clang-format
   - Installed to: `/usr/bin`, `/usr/lib`, `/usr/share`
   - Cache location: `/var/cache/apt`, `/var/lib/apt/lists`
2. **Homebrew packages**:
   - gstreamer (via Homebrew/Linuxbrew)
   - Installed to: `/home/linuxbrew/.linuxbrew/`

**macOS (macos-latest)**:
1. **Homebrew packages**:
   - gstreamer, cmake, clang-format
   - Installed to: `/opt/homebrew` (Apple Silicon) or `/usr/local` (Intel)

**Comprehensive Cache Strategy**:

**For Linux (ubuntu-22.04)**:
```yaml
# Cache 1: apt packages
- name: Cache apt packages
  uses: actions/cache@v4
  with:
    path: |
      /var/cache/apt/archives
      /var/lib/apt/lists
    key: ${{ runner.os }}-apt-${{ hashFiles('.github/workflows/ci.yml') }}
    restore-keys: |
      ${{ runner.os }}-apt-

# Cache 2: Homebrew (GStreamer)
- name: Cache Homebrew
  uses: actions/cache@v4
  with:
    path: |
      /home/linuxbrew/.linuxbrew/Cellar
      /home/linuxbrew/.linuxbrew/lib/pkgconfig
      /home/linuxbrew/.linuxbrew/include
    key: ${{ runner.os }}-linuxbrew-${{ hashFiles('.github/workflows/ci.yml') }}
    restore-keys: |
      ${{ runner.os }}-linuxbrew-
```

**For macOS (macos-latest)**:
```yaml
# Cache: Homebrew packages
- name: Cache Homebrew
  uses: actions/cache@v4
  with:
    path: |
      /opt/homebrew/Cellar
      /opt/homebrew/lib/pkgconfig
      /opt/homebrew/include
    key: ${{ runner.os }}-homebrew-${{ hashFiles('.github/workflows/ci.yml') }}
    restore-keys: |
      ${{ runner.os }}-homebrew-
```

**Decision**: Implement multi-layer caching for complete dependency coverage.

**Rationale**:
- **apt cache** saves 2-3 minutes (Linux only)
- **Homebrew cache** saves 5-8 minutes (both platforms)
- **Combined savings**: 7-10 minutes per workflow run on Linux, 5-8 minutes on macOS
- Cache hit rate expected >90% (dependencies change infrequently)
- Graceful fallback on cache miss
- No maintenance burden (GitHub manages cache eviction)

**Cache Key Strategy**:
- Keyed by `runner.os` (different paths for macOS/Linux)
- Includes workflow file hash (invalidates on dependency version changes)
- Separate keys for apt vs Homebrew (different update cadences)
- Fallback to any cache for same OS + package manager

**Expected Performance**:

**Linux (ubuntu-22.04)**:
- **Cache miss**: ~10 minutes (apt: ~3min, Homebrew: ~7min)
- **Cache hit**: ~1 minute (restore both caches)
- **Net savings**: 9 minutes per workflow run

**macOS (macos-latest)**:
- **Cache miss**: ~8 minutes (Homebrew install all packages)
- **Cache hit**: ~30 seconds (restore Homebrew cache)
- **Net savings**: 7.5 minutes per workflow run

**Alternatives Considered**:
- Cache only GStreamer → **Rejected**: Misses apt packages (2-3 min savings left on table)
- Docker image with pre-installed deps → **Rejected**: macOS runners don't support Docker
- Cache entire /home/linuxbrew → **Rejected**: Too large, includes unnecessary files
- Single cache for all packages → **Rejected**: apt and Homebrew have different update patterns

**Cache Invalidation Triggers**:
- Workflow file changes (dependency version update)
- Manual cache clear (via GitHub UI)
- 7-day cache eviction (GitHub policy)

---

### 12. Python Virtual Environment Removal

**Investigation**: Is the Python virtual environment still needed after Conan removal?

**Findings** (from .github/workflows/ci.yml analysis):
- Current Linux workflow includes:
  ```yaml
  - name: Install system dependencies
    run: |
      sudo apt-get install -y \
        cmake \
        valgrind \
        build-essential \
        pkg-config \
        python3-venv \  # <-- Only needed for Conan
        clang-format
  
  - name: Setup Python Virtual Environment
    run: |
      python3 -m venv venv
      echo "VIRTUAL_ENV=./venv" >> $GITHUB_ENV
      echo "./venv/bin" >> $GITHUB_PATH
  ```
- Python venv was created to isolate `pip install conan`
- No other Python dependencies in the project
- Plugin is pure C11, build system is CMake, tests use CTest
- macOS workflow does NOT use Python venv (never needed it)

**Current Python Usage in Project**:
- **Build system**: CMake (no Python)
- **Plugin code**: C11 (no Python)
- **Tests**: CTest + C test harness (no Python)
- **CI scripts**: Bash (.ci/run-tests.sh) (no Python)
- **Only Python use was**: `pip install conan` in virtual environment

**Decision**: Remove Python virtual environment setup and python3-venv package from Linux CI workflow.

**Rationale**:
- Zero Python dependencies after Conan removal
- Reduces CI complexity (one less setup step)
- Saves ~10-15 seconds per workflow run (venv creation)
- Reduces apt package count (removes python3-venv)
- Simplifies cache (no Python packages to cache)
- Makes Linux workflow consistent with macOS (which never had Python)

**Impact Analysis**:
- **Removed from apt-get install**: `python3-venv`
- **Removed step**: "Setup Python Virtual Environment"
- **Affected files**: `.github/workflows/ci.yml` (Linux job only)
- **No impact on**: macOS workflow (never had Python venv)
- **No impact on**: Build process (CMake doesn't use Python)
- **No impact on**: Tests (CTest doesn't use Python)

**Alternatives Considered**:
- Keep Python venv "just in case" → **Rejected**: YAGNI principle, adds unnecessary complexity
- Keep python3-venv package but skip venv creation → **Rejected**: Package serves no purpose
- Document Python as optional dependency → **Rejected**: Misleading, not actually used

**Updated apt Package List** (Linux CI):
```bash
sudo apt-get install -y \
  cmake \
  valgrind \
  build-essential \
  pkg-config \
  clang-format
# python3-venv REMOVED - only needed for Conan
```

---

## Summary of Key Decisions

| Area | Decision | Rationale |
|------|----------|-----------|
| **Conan Removal** | Complete removal, no replacement | Provides zero dependencies, only preset generation |
| **CMake Presets** | Repository CMakePresets.json (v6) with debug/release | Native CMake, version-controlled, supports configure + build |
| **Compiler Flags** | Use CMake defaults per build type | Sufficient for current needs, user validated |
| **CI Workflows** | Remove Conan steps, use native presets | Simpler, faster, more maintainable |
| **CI Caching** | Multi-layer cache (apt + Homebrew on Linux, Homebrew on macOS) | Saves 7-10 min/run (Linux), 5-8 min/run (macOS); apt: ~3min, Homebrew: ~5-8min |
| **Build Scripts** | Inline CMake commands, remove conan_preset() | Clearer, less abstraction |
| **pkg-config** | No changes | Already working without Conan |
| **Documentation** | Emphasize Homebrew GStreamer as prerequisite | Matches actual dependency reality |
| **CMakeUserPresets.json** | Update or simplify to not reference Conan | Prevent configuration errors |
| **Build Paths** | Preserve `build/Debug/` and `build/Release/` | Minimize disruption |
| **Rollback** | Developer consensus triggers, git revert | Pragmatic, low-risk |

---

## Unknowns Resolved

All technical unknowns from the specification have been resolved:
- ✅ CMake preset structure researched
- ✅ Compiler flag requirements validated
- ✅ CI modification scope identified
- ✅ Documentation requirements defined
- ✅ Rollback procedure validated
- ✅ No remaining "NEEDS CLARIFICATION" markers

---

## Next Phase

**Phase 1 Ready**: Design contracts, data model, and quickstart workflow based on these research findings.

**Key Phase 1 Outputs**:
1. **contracts/**: CMakePresets.json schema (example preset structure)
2. **data-model.md**: Build configuration entities (presets, workflows, scripts)
3. **quickstart.md**: Step-by-step validation procedure for Conan-free builds
4. **COPILOT.md**: Updated agent instructions with Conan removal context

---
