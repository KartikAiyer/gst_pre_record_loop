# T013: GStreamer Plugin Cache Fix

## Problem
The CI workflow was failing when trying to use `gst-inspect` to discover the `pre_record_loop` element, even though:
1. ✅ gst-inspect-1.0 binary was found in PATH
2. ✅ Plugin binary was successfully compiled (libgstprerecordloop.so)
3. ✅ GST_PLUGIN_PATH was correctly set

The error was:
```
[CI][ERROR] gst-inspect could not find 'pre_record_loop'
```

## Root Cause
GStreamer uses a plugin registry cache (stored in `~/.cache/gstreamer-1.0/registry.*.bin`) that's built once and reused. When `GST_PLUGIN_PATH` changes or new plugins are added, GStreamer doesn't automatically rescan unless the cache is cleared.

In our CI environment:
1. First build might cache an empty registry (or one without our plugin)
2. Later builds find the cache and use it, never discovering the new plugin
3. gst-inspect can't find the element because the cache says it doesn't exist

## Solution
Clear the GStreamer plugin registry cache before running gst-inspect:

```bash
# Clear the plugin registry cache
rm -f ~/.cache/gstreamer-1.0/registry.*.bin 2>/dev/null || true

# Force GStreamer to bypass system cache (important for CI)
export GST_PLUGIN_SYSTEM_PATH_1_0=""
```

## Changes Made

### .ci/run-tests.sh (lines 97-108)
- Added cache clearing logic after setting GST_PLUGIN_PATH
- Added `GST_PLUGIN_SYSTEM_PATH_1_0=""` to bypass system cache
- Improved error diagnostics:
  - Added LD_LIBRARY_PATH to debug output
  - Display missing dependencies from ldd
  - Increased GST_DEBUG level to 3
  - Show all debug output (removed grep filter)

## Testing Verification
Run locally to verify the fix works:

```bash
# Build with caching disabled
rm -f ~/.cache/gstreamer-1.0/registry.*.bin
export GST_PLUGIN_SYSTEM_PATH_1_0=""

# Run the CI script
./.ci/run-tests.sh
```

Expected output:
```
[CI] GST_PLUGIN_PATH set to /path/to/build/Debug/gstprerecordloop:...
[CI] Found plugin binary: libgstprerecordloop.so
[CI] Checking plugin dependencies:
[CI] ✓ gst-inspect located element 'pre_record_loop'
```

## Related Issues
- GitHub Actions cache doesn't invalidate when code changes
- Symlinks in Homebrew bin directory are fragile after cache extraction
- Plugin registry is persistent across runs; needs explicit invalidation

## Future Improvements
1. Consider adding `GST_DEBUG=3 gst-inspect-1.0 pre_record_loop` to build output for verification
2. Document GStreamer debugging tips in specs/000-prerecordloop-baseline/
3. Consider caching the plugin registry as part of CI artifacts
