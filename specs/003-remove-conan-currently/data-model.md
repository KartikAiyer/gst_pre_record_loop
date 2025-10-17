# Data Model: Build Configuration

**Feature**: Remove Conan Dependency  
**Date**: 2025-10-17  
**Spec**: [spec.md](./spec.md)

## Overview
This document models the build system configuration entities that will be modified during Conan removal. Unlike typical data models for application features, this represents build configuration artifacts and their relationships.

---

## Entity: CMakePresets Configuration

### Purpose
Defines standardized build configurations that replace Conan-generated presets.

### Attributes
| Attribute | Type | Required | Description | Validation |
|-----------|------|----------|-------------|------------|
| `name` | string | Yes | Preset identifier (e.g., "debug", "release") | Lowercase, alphanumeric |
| `displayName` | string | No | Human-readable name | Any string |
| `description` | string | No | Preset purpose description | Any string |
| `binaryDir` | string | Yes | Build output directory path | Absolute or relative to sourceDir |
| `cacheVariables` | object | Yes | CMake cache variable overrides | Key-value pairs |
| `cacheVariables.CMAKE_BUILD_TYPE` | string | Yes | Build type (Debug/Release) | "Debug" or "Release" |

### Relationships
- **configurePreset** → **buildPreset**: Each buildPreset references a configurePreset (1:1)
- **preset** → **workflow**: CI workflows reference preset names (N:1)
- **preset** → **build script**: .ci/run-tests.sh uses preset names (N:1)

### States
- **Not Exist**: Conan removal initial state (presets are Conan-generated)
- **Defined**: CMakePresets.json created in repository root
- **In Use**: Workflows and scripts reference preset names

### Example
```json
{
  "name": "debug",
  "displayName": "Debug Build",
  "description": "Debug configuration with symbols, no optimization",
  "binaryDir": "${sourceDir}/build/Debug",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Debug",
    "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
  }
}
```

---

## Entity: CI Workflow Configuration

### Purpose
GitHub Actions workflow definition for automated builds.

### Attributes
| Attribute | Type | Required | Description | Validation |
|-----------|------|----------|-------------|------------|
| `job_name` | string | Yes | Job identifier | Alphanumeric, hyphens |
| `platform` | string | Yes | Runner platform | "ubuntu-22.04" or "macos-latest" |
| `steps` | array | Yes | Sequential build/test steps | List of step objects |
| `steps[].name` | string | Yes | Step description | Any string |
| `steps[].run` | string | Yes | Shell command(s) | Valid shell syntax |
| `preset_references` | array | No | CMake preset names used | Must match CMakePresets.json names |

### Relationships
- **workflow** → **preset**: References preset names in `cmake --preset` commands (N:N)
- **workflow** → **build script**: May invoke .ci/run-tests.sh (1:1 optional)

### States
- **With Conan**: Contains Conan installation and `conan install` steps
- **Without Conan**: Conan steps removed, direct CMake preset usage
- **Validated**: Workflow runs successfully in CI

### Modified Files
- `.github/workflows/ci.yml` (main build/test workflow)
- `.github/workflows/valgrind.yml` (memory leak testing)

---

## Entity: Build Script Configuration

### Purpose
Unified build/test script used locally and in CI (.ci/run-tests.sh).

### Attributes
| Attribute | Type | Required | Description | Validation |
|-----------|------|----------|-------------|------------|
| `build_types` | array | Yes | Configurations to build | ["Debug", "Release"] |
| `preset_names` | array | Yes | CMake preset names | Match CMakePresets.json |
| `test_command` | string | Yes | CTest execution command | Valid shell command |
| `style_check` | boolean | No | Whether to run clang-format | true/false |

### Relationships
- **script** → **preset**: Uses preset names in `cmake --preset` and `cmake --build --preset` (N:1)
- **CI workflow** → **script**: Workflows invoke script with environment variables (1:1)

### States
- **With conan_preset()**: Contains Conan-specific logic
- **With native CMake**: Direct preset usage, no Conan logic
- **Tested**: Script runs successfully on macOS and Linux

### Current Functions (to be removed)
```bash
conan_preset() {
  # Check for generators directory
  # Run conan install if missing
}
```

### Replacement Pattern
```bash
# Direct CMake commands
cmake --preset=debug
cmake --build --preset=debug
ctest --test-dir build/Debug
```

---

## Entity: CMakeUserPresets Configuration

### Purpose
User-specific CMake preset overrides for local development customization. **Should NOT be version-controlled** - each developer creates their own if needed.

### Attributes
| Attribute | Type | Required | Description | Validation |
|-----------|------|----------|-------------|------------|
| `version` | integer | Yes | CMake presets schema version | >= 4 |
| `include` | array | No | Paths to preset files to include | Valid file paths |
| `configurePresets` | array | No | User-specific configure overrides | Preset objects |
| `buildPresets` | array | No | User-specific build overrides | Preset objects |

### Relationships
- **user presets** → **repository presets**: Optionally includes/inherits from CMakePresets.json (1:1 optional)

### States
- **Includes Conan** (current): References `build/Debug/generators/CMakePresets.json`
- **Gitignored** (target): Added to .gitignore, not version-controlled
- **User-Created** (optional): Users can create locally for custom overrides

### Current Issue
Currently version-controlled with hardcoded include path to Conan-generated file. After Conan removal:
1. File should be deleted from repository
2. Added to .gitignore
3. Users can create their own if they need local customization (e.g., different generator, custom cache variables)

---

## Entity: README Documentation

### Purpose
Developer-facing documentation for building the project.

### Attributes
| Attribute | Type | Required | Description | Validation |
|-----------|------|----------|-------------|------------|
| `prerequisites_section` | markdown | Yes | Required software/versions | Clear, actionable |
| `build_commands_section` | markdown | Yes | Step-by-step build instructions | Testable commands |
| `troubleshooting_section` | markdown | No | Common errors and solutions | Problem → solution mapping |

### Relationships
- **README** → **CMakePresets**: Documents preset names developers use (N:1)
- **README** → **GStreamer**: Specifies GStreamer 1.26+ via Homebrew (1:1)

### States
- **With Conan**: References `pip install conan` and `conan install` commands
- **Without Conan**: References direct CMake preset workflow
- **Validated**: Commands in README work on clean system

### Required Sections (from FR-018, FR-019, FR-028)
```markdown
## Prerequisites
- GStreamer 1.26+ via Homebrew (required)
- CMake 3.27+ (required)
- pkg-config (included with Homebrew)

## Quick Start
1. Install GStreamer: `brew install gstreamer`
2. Configure: `cmake --preset=debug`
3. Build: `cmake --build --preset=debug`
4. Test: `ctest --test-dir build/Debug`
```

---

## Entity: Conan Configuration (TO BE DELETED)

### Purpose
Conan package manager configuration (scheduled for removal).

### Attributes
| Attribute | Type | Current Value | Deletion Reason |
|-----------|------|---------------|-----------------|
| `requires` | array | `[]` (empty) | No dependencies managed |
| `settings` | array | `["os", "compiler", "build_type", "arch"]` | Unused, CMake detects these |
| `generators` | array | `["CMakeDeps", "CMakeToolchain"]` | Generates files we'll create directly |

### Generated Artifacts (to be removed)
- `build/Debug/generators/CMakePresets.json`
- `build/Debug/generators/conan_toolchain.cmake`
- `build/Debug/generators/*.cmake` (various Conan helper files)
- `build/Release/generators/` (same as Debug)

### Relationships (to be broken)
- **conanfile** → **CI workflow**: Workflows run `conan install`
- **conanfile** → **CMakeUserPresets**: User presets include Conan-generated presets
- **conanfile** → **build script**: Script checks for generators directory

### Deletion Checklist
- [ ] Remove `conanfile.py` file
- [ ] Remove Conan install steps from `.github/workflows/ci.yml`
- [ ] Remove Conan install steps from `.github/workflows/valgrind.yml`
- [ ] Remove `conan_preset()` function from `.ci/run-tests.sh`
- [ ] Delete `CMakeUserPresets.json` from repository (user-specific, not version-controlled)
- [ ] Add `CMakeUserPresets.json` to `.gitignore`
- [ ] Delete `build/*/generators/` directories (gitignored, but document)

---

## Validation Rules

### Global Build System Constraints
1. **Build path consistency**: All configurations MUST use `build/<BuildType>/` pattern (FR-026)
2. **Test preservation**: All 22 existing tests MUST continue to pass (FR-008)
3. **Platform support**: MUST work on macOS (Apple Silicon + Intel) and Linux (ubuntu-22.04) (FR-004, FR-005)
4. **Build option preservation**: BUILD_GTK_DOC, PREREC_ENABLE_LIFE_DIAG, ENABLE_ASAN MUST remain functional (FR-006)

### CMakePresets.json Validation
- Schema version MUST be >= 6 (for CMake 3.27+ compatibility)
- Each buildPreset MUST reference a valid configurePreset
- Preset names MUST match references in CI workflows and build scripts
- binaryDir MUST resolve to `build/Debug/` or `build/Release/`

### CI Workflow Validation
- Workflows MUST NOT reference Conan commands (FR-012, FR-013)
- Workflows MUST install GStreamer via Homebrew (explicit in scenarios)
- Workflows MUST use preset names from CMakePresets.json
- Both Debug and Release builds MUST be tested (FR-015)

### Documentation Validation
- README MUST NOT mention Conan (FR-018)
- README MUST document GStreamer 1.26+ via Homebrew as first prerequisite (FR-028)
- README MUST specify CMake 3.27+ requirement (FR-021)
- Build commands MUST be copy-paste executable

---

## Entity Relationships Diagram

```
┌─────────────────────────┐
│   CMakePresets.json     │  (repository root, version-controlled)
│  ┌─────────────────┐    │
│  │ configurePresets│────┼──→ binaryDir: build/Debug/
│  └─────────────────┘    │
│  ┌─────────────────┐    │
│  │   buildPresets  │    │
│  └─────────────────┘    │
└────────┬────────────────┘
         │ referenced by
         │
    ┌────┴────┐
    │         │
    ▼         ▼
┌─────────┐ ┌──────────────────┐
│ ci.yml  │ │ run-tests.sh     │
│ (CI)    │ │ (build script)   │
└─────────┘ └──────────────────┘
    │               │
    │ invokes       │ uses presets
    │               │
    ▼               ▼
┌─────────────────────────────┐
│  cmake --preset=debug       │
│  cmake --build --preset=... │
│  ctest --test-dir build/... │
└─────────────────────────────┘
            │
            │ produces
            ▼
┌─────────────────────────────┐
│   Build Artifacts           │
│  ├── libgstprerecordloop.so │
│  ├── test binaries          │
│  └── prerec.app             │
└─────────────────────────────┘
```

---

## Migration Impact

### Files Modified
| File | Modification Type | Risk Level |
|------|-------------------|------------|
| `CMakePresets.json` | **CREATE** | Low (new file) |
| `CMakeUserPresets.json` | **DELETE + GITIGNORE** | Low (user-specific, not needed) |
| `.gitignore` | **UPDATE** | Low (add CMakeUserPresets.json) |
| `.github/workflows/ci.yml` | **UPDATE** | Medium (CI dependency) |
| `.github/workflows/valgrind.yml` | **UPDATE** | Low (similar to ci.yml) |
| `.ci/run-tests.sh` | **UPDATE** | Medium (used locally + CI) |
| `README.md` | **UPDATE** | Low (documentation only) |
| `conanfile.py` | **DELETE** | Low (unused) |

### Files Unmodified
- All source code (`gstprerecordloop/src/*.c`, `testapp/src/*.cc`, `tests/**/*.c`)
- Root `CMakeLists.txt` (already uses pkg-config, no Conan references)
- Subdirectory `CMakeLists.txt` files (no Conan references)
- Test utilities (`tests/unit/test_utils.*`)

### Rollback Data Preservation
- Git commit history preserves all deleted files
- No database or user data involved (build system only)
- Rollback = `git revert <commits>` + re-run `conan install`

---

## Summary

This data model defines the build configuration entities affected by Conan removal:

**Created**: CMakePresets.json (repository presets)  
**Updated**: CI workflows, build script, CMakeUserPresets.json, README  
**Deleted**: conanfile.py, Conan-generated artifacts  
**Unchanged**: All source code, CMakeLists.txt files, tests

All entities have clear validation rules ensuring the build system remains functional across platforms while eliminating Conan dependency.

---
