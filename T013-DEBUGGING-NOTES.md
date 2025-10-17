# T013 CI Cache Validation - Debugging Notes

## Issue Summary

**Date**: October 17, 2025  
**Branch**: 003-remove-conan-currently  
**Failing Runs**: #18604484036 (both Linux and macOS)

### First Run (Cache Miss)
✅ **SUCCESS** - All packages installed from scratch, tests passed

### Second Run (Cache Hit)  
❌ **FAILED** - Both Linux and macOS workflows failed with different issues

---

## macOS Failure Analysis

### Symptoms
```
CMake Error: Package 'gstreamer-1.0' not found
```

Even though Homebrew reported:
```
Warning: gstreamer 1.26.5_5 is already installed, it's just not linked.
```

### Root Cause
1. **Cache restored successfully** (GStreamer 1.26.2 from cache)
2. **Homebrew found newer version** (1.26.5_5) 
3. **Homebrew downloaded but didn't link** the new version
4. **pkg-config failed** because packages weren't in `/opt/homebrew/opt/` symlinks

### The Problem
When caching specific Homebrew packages:
- Cache contains OLD versions (e.g., 1.26.2)
- Homebrew detects NEWER versions available (e.g., 1.26.5)
- `brew install` downloads new version but says "already installed" due to cache
- New version isn't **linked** (symlinks not created in `/opt/homebrew/opt/`)
- pkg-config can't find the .pc files → CMake fails

---

## Ubuntu Status
The Ubuntu workflow **completed successfully** in the second run, but this was likely because:
- The cache hasn't been populated yet (first run might have been before caching was implemented)
- OR the same linking issue exists but manifests differently

---

## Solutions Attempted

### ❌ Solution 1: Cache entire Homebrew directory
**Problem**: Too large (10-20GB), includes all installed packages

### ❌ Solution 2: Cache specific package paths only  
**Problem**: Version mismatches between cached and current, no linking

### ✅ Solution 3: Add explicit `brew link` after install
**Implementation**:
```bash
brew install gstreamer cmake clang-format
brew link --overwrite gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly glib || true
```

**Why this works**:
- `brew install` handles downloads (skipped if cached)
- `brew link --overwrite` forces symlink creation
- Handles version mismatches gracefully
- `|| true` prevents failures if already linked

---

## Commits

1. **dfefa3a**: Fix T012: Expand Homebrew cache paths to include opt and lib directories
   - Added `/opt` and `/lib` to cache paths
   - Removed conditional installs
   
2. **cfd618f**: Fix T012: Add brew link after cache restore to fix pkg-config detection
   - Added explicit `brew link --overwrite` commands
   - Handles linking after cache restoration

3. **0be9922**: Fix T012: Add libdw-dev to Ubuntu dependencies for GStreamer
   - Added `libdw-dev` package (elfutils) required by GStreamer
   - Fixes pkg-config dependency error on Ubuntu

---

## Third Run Results (#18604872702)

### macOS: ✅ SUCCESS
- Cache hit (66 MB restored)
- `brew link` executed successfully
- **Warnings observed** (harmless):
  - "Formula gst-plugins-* was renamed to gstreamer" (Homebrew consolidated plugins)
  - "Already linked: glib" (expected - glib was already linked correctly)
- All 22 tests passed (Debug + Release)
- Total time: ~3 minutes (including tests)

### Ubuntu: ❌ FAILED
- Cache hit (352 MB restored)
- `brew link` executed successfully
- **Build failed** at CMake configuration:
  - Error: `Package 'libdw', required by 'gstreamer-1.0', not found`
  - Root cause: GStreamer's pkg-config file references `libdw` for debugging symbols
  - Missing package: `libdw-dev` (part of elfutils)

### Fix Applied
Added `libdw-dev` to Ubuntu apt packages in workflow.

---

## Next Steps

1. **Push commits** to GitHub:
   ```bash
   git push origin 003-remove-conan-currently
   ```

2. **Trigger new CI run** (automatic on push)

3. **Validate**:
   - First run: Should cache packages and pass
   - Second run: Should restore cache, link packages, and pass
   - Check timing: Should save 5-8 minutes with cache hit

4. **Document in quickstart.md** (part of T013):
   - Cache hit/miss behavior
   - Expected time savings
   - Link to this debugging session

---

## Lessons Learned

1. **Homebrew caching is tricky**: Package versions can change between runs
2. **Linking is separate from installation**: Cached files ≠ linked files
3. **pkg-config depends on symlinks**: Must be in standard locations (`/opt/homebrew/opt/`)
4. **Cache strategy evolution**:
   - v1: Cache entire Homebrew (too large)
   - v2: Cache specific paths without linking (failed)
   - v3: Cache specific paths + explicit linking (working)

---

## References

- CI Logs: Run #18604484036
- Spec: `specs/003-remove-conan-currently/spec.md` (FR-029 to FR-034)
- Tasks: `specs/003-remove-conan-currently/tasks.md` (T012-T013)
