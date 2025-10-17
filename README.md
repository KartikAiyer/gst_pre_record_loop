# Introduction

**Status**: ✅ Production Ready (Baseline Spec Complete - October 11, 2025)

This project implements a GStreamer plugin featuring a ring buffer filter for encoded video capture. The filter addresses a common requirement in event-driven recording applications: capturing video data that occurred before an event was detected.

**Key Features**:
- GOP-aware buffering with 2-GOP minimum retention
- Custom event-driven state machine (BUFFERING ↔ PASS_THROUGH)
- Configurable flush policies and properties
- Sub-millisecond pruning latency (median 6µs)
- Memory-safe with validated refcount handling
- Comprehensive test suite (22 tests, 100% passing)

## How it works

The filter operates as a ring buffer, continuously caching encoded video frames. When an event is triggered, it transitions to pass-through mode, first sending the cached pre-event data downstream, followed by real-time incoming frames.

## Sample Pipeline
```
┌────────┐      ┌───────┐        ┌─────────┐        ┌──────────┐       ┌───────┐     ┌────────┐
│ VidSrc ┼─────►│ xh264 ┼───────►│h264Parse┼───────►│prerecloop┼──────►│  Mux  ┼────►│filesink│
│        │      │       │        │         │        │          │       │       │     │        │
└────────┘      └───────┘        └─────────┘        └──────────┘       └───────┘     └────────┘

The idea is that the prerecloop will buffer video frames until an event is published after which it will push buffered frames and incoming frames 
downstream to the file sink.

# Notes

The filter is GOP aware. i.e it will always start at a key frame and when it drops frames, it will drop an entire GOP.

For detailed queue ownership & refcount semantics (buffers vs SEGMENT/GAP events, sticky handling) see:
`specs/000-prerecordloop-baseline/data-model.md` (Ownership / Refcount Semantics section).

## Custom Events

The `prerecordloop` element responds to two custom GStreamer events for controlling its buffering and flush behavior:

### prerecord-flush (Downstream Event)

**Direction**: Downstream (sent from upstream elements or application)  
**Event Type**: `GST_EVENT_CUSTOM_DOWNSTREAM`  
**Structure Name**: Configurable via `flush-trigger-name` property (default: `"prerecord-flush"`)

**Purpose**: Triggers the element to drain all buffered GOPs and transition from BUFFERING to PASS_THROUGH mode.

**Usage Example** (C API):
```c
GstStructure *s = gst_structure_new_empty("prerecord-flush");
GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
gst_element_send_event(pipeline, event);
```

**Behavior**:
- If in BUFFERING mode: Drains all queued GOPs in order, then switches to PASS_THROUGH
- If already in PASS_THROUGH: Ignored (logged at debug level)
- If already draining from a previous flush: Ignored to prevent duplicate emission

**Custom Trigger Names**:
You can customize the event structure name for application-specific integration:
```c
g_object_set(prerecordloop, "flush-trigger-name", "motion-detected", NULL);
```

Then send events with matching structure name:
```c
GstStructure *s = gst_structure_new_empty("motion-detected");
GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
gst_element_send_event(pipeline, event);
```

### prerecord-arm (Upstream Event)

**Direction**: Upstream (sent from downstream elements or application)  
**Event Type**: `GST_EVENT_CUSTOM_UPSTREAM`  
**Structure Name**: `"prerecord-arm"` (fixed)

**Purpose**: Transitions the element from PASS_THROUGH back to BUFFERING mode to start accumulating a fresh pre-event window.

**Usage Example** (C API):
```c
GstStructure *s = gst_structure_new_empty("prerecord-arm");
GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, s);
gst_element_send_event(pipeline, event);
```

**Behavior**:
- If in PASS_THROUGH mode: Resets GOP tracking baseline and transitions to BUFFERING
- If already in BUFFERING: Ignored (logged at INFO level)
- Does not affect already-forwarded live data (non-destructive re-arm)

**Event-Driven Recording Workflow**:
```
Initial: BUFFERING (accumulate pre-event window)
    ↓
[prerecord-flush event] → Drain buffered GOPs → PASS_THROUGH (forward live)
    ↓
[prerecord-arm event] → Reset baseline → BUFFERING (new window)
    ↓
[prerecord-flush event] → Drain → PASS_THROUGH ...
```

## Properties Reference

The `prerecordloop` element exposes the following configurable properties:

| Property | Type | Default | Range/Options | Description |
|----------|------|---------|---------------|-------------|
| `silent` | Boolean | `FALSE` | TRUE/FALSE | Suppresses non-critical logging when TRUE. Legacy property; prefer `GST_DEBUG` environment variable for runtime control. |
| `flush-on-eos` | Enum | `AUTO` | AUTO, ALWAYS, NEVER | Policy for handling buffered content at EOS:<br>• **AUTO**: Flush only if in PASS_THROUGH mode<br>• **ALWAYS**: Always drain buffer before forwarding EOS<br>• **NEVER**: Forward EOS immediately without flushing |
| `flush-trigger-name` | String | `"prerecord-flush"` | Any string or NULL | Custom event structure name for flush trigger. Allows integration with application-specific events (e.g., `"motion-detected"`). Set to NULL to use default. |
| `max-time` | Integer | `10` | 0 to G_MAXINT (seconds) | Maximum buffered duration in whole seconds. When exceeded, oldest GOPs are pruned while maintaining a 2-GOP minimum floor. Zero or negative = unlimited buffering. Sub-second values are floored to whole seconds. |

**Property Usage Examples**:

Set maximum buffer window to 30 seconds:
```bash
gst-launch-1.0 ... ! prerecordloop max-time=30 ! ...
```

Configure custom flush trigger and EOS policy:
```bash
gst-launch-1.0 ... ! prerecordloop flush-trigger-name=motion-detected flush-on-eos=always ! ...
```

Set properties programmatically (C):
```c
g_object_set(G_OBJECT(prerecordloop),
             "max-time", 15,
             "flush-on-eos", GST_PREREC_FLUSH_ON_EOS_AUTO,
             "flush-trigger-name", "custom-event",
             NULL);
```

**Important Notes**:
- `max-time` enforces a **2-GOP minimum floor**: Even if a single GOP exceeds `max-time`, it and the preceding GOP (if present) are always retained to ensure playback continuity.
- `flush-trigger-name` must match the structure name of the custom downstream event exactly (case-sensitive).
- All properties are readable and writable at runtime via `g_object_get/set` or GStreamer property syntax.

# Prerequisites

Before building, ensure you have the following installed:

## Required Dependencies

1. **GStreamer 1.26+** (via Homebrew)
   ```bash
   brew install gstreamer
   ```
   The build system uses pkg-config to locate GStreamer libraries. Homebrew's GStreamer includes all necessary development headers and pkg-config files.

2. **CMake 3.27+**
   ```bash
   brew install cmake
   ```
   Required for preset support (version 6 schema).

3. **Build Tools**
   - C11 compiler (Clang on macOS, GCC on Linux)
   - pkg-config (included with Homebrew)

## Optional Tools

- **clang-format** (for code style checks in CI)
  ```bash
  brew install clang-format
  ```

# Building the Code

The project uses native CMake presets for configuration and building. No external package managers are required.

The CMake configuration automatically detects GStreamer via pkg-config. The Homebrew installation paths are discovered automatically.

## Quick Start

The project uses CMake presets (defined in `CMakePresets.json`) for streamlined configuration and building.

### Debug Build

1. **Configure**:
   ```bash
   cmake --preset=debug
   ```

2. **Build**:
   ```bash
   cmake --build --preset=debug
   ```

3. **Test**:
   ```bash
   ctest --test-dir build/Debug
   ```

### Release Build

1. **Configure**:
   ```bash
   cmake --preset=release
   ```

2. **Build**:
   ```bash
   cmake --build --preset=release
   ```

3. **Test**:
   ```bash
   ctest --test-dir build/Release
   ```

### Available Presets

View all available CMake presets:
```bash
cmake --list-presets
```

View build presets:
```bash
cmake --list-presets=build
```

## Custom Build Configuration (Optional)

If you need to customize build settings (e.g., different generator, additional cache variables), create a `CMakeUserPresets.json` file in the repository root. This file is gitignored and allows local developer customization without affecting the repository.

Example `CMakeUserPresets.json`:
```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "my-debug",
      "inherits": "debug",
      "cacheVariables": {
        "BUILD_GTK_DOC": "ON",
        "CMAKE_VERBOSE_MAKEFILE": "ON"
      }
    }
  ]
}
```

## Build Options

### `PREREC_ENABLE_LIFE_DIAG`

This CMake option (OFF by default) compiles in additional lightweight lifecycle & dataflow diagnostics for the `prerecordloop` element. When disabled the added code paths are completely compiled out (zero branches added to hot paths).

What it adds when enabled (`-DPREREC_ENABLE_LIFE_DIAG=1`):

- Extra debug categories: `prerec_lifecycle` (state, events, queue ops) and `prerec_dataflow` (buffer movement & flush sequencing).
- Internal tracking helpers for sticky events & object lifecycle (used during the original refcount bug investigation).
- Safer experimentation space for future ownership audits without impacting production builds.

Overhead considerations:
- Disabled (default): no additional runtime cost.
- Enabled: logging cost only when matching GST_DEBUG categories are activated; otherwise minimal (guards + occasional counter increments).

Configure with diagnostics enabled (example Debug build):

```sh
cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug -DPREREC_ENABLE_LIFE_DIAG=1
cmake --build build/Debug --target gstprerecordloop
```

Run with lifecycle/dataflow logs (choose verbosity 1–7):

```sh
GST_DEBUG=prerec_lifecycle:7,prerec_dataflow:5 <your command>
```

Typical investigation recipe (inject refcount tracing too):

```sh
GST_DEBUG=GST_REFCOUNTING:7,prerec_lifecycle:7,prerec_dataflow:5 ctest -R prerec_unit_no_refcount_critical -V
```

If you only want core element logs without diagnostics (always available):

```sh
GST_DEBUG=*:4,pre_record_loop:5,pre_record_loop_dataflow:5 <your command>
```

Verification that diagnostics are compiled in:
- Look for the build line containing `-DPREREC_ENABLE_LIFE_DIAG=1` in your CMake build output, or
- Run with `GST_DEBUG=prerec_lifecycle:1` and confirm you see lifecycle category messages.

## Refcount / Lifecycle Integrity

During development a GStreamer refcount assertion (double unref of a mini-object) was observed when flushing buffered
data. Root cause: manual sticky event storage combined with forwarding the same serialized event to the default handler
and enqueuing SEGMENT/GAP without an owned reference. The fix:

- Removed manual `gst_pad_store_sticky_event()` calls (delegate to `gst_pad_event_default`).
- Added an extra ref when enqueuing SEGMENT / GAP so queue ownership is explicit.
- Added (then gated) verbose lifecycle instrumentation for diagnostics. Diagnostics can now be enabled at build time
	with `-DPREREC_ENABLE_LIFE_DIAG=1` (default is off for normal builds).
- Added regression test `prerec_unit_no_refcount_critical` ensuring no `gst_mini_object_unref` refcount CRITICAL occurs
	in a minimal pipeline scenario.

If you need to troubleshoot ownership again:
```sh
cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug -DPREREC_ENABLE_LIFE_DIAG=1
GST_DEBUG=GST_REFCOUNTING:7,prerec_dataflow:5 ctest -R prerec_unit_flush_trigger_name -V
```
# Running Tests

The project includes unit tests that can be executed using CTest. These tests verify the functionality of the prerecordloop element and help ensure refcount integrity.

## Basic Test Execution

To run all tests:

```sh
ctest --test-dir build/Debug
```

To run tests with verbose output:

```sh
ctest --test-dir build/Debug -V
```

## Running Specific Tests

To run a specific test by name pattern:

```sh
ctest --test-dir build/Debug -R prerec_unit_no_refcount_critical
```

To run with verbose output for a specific test:

```sh
ctest --test-dir build/Debug -R prerec_unit_no_refcount_critical -V
```

### Plugin Discovery (No Manual GST_PLUGIN_PATH Needed with CTest)

CTest automatically injects the plugin search path for every test via the `ENVIRONMENT` property in `tests/CMakeLists.txt`:

```
ENVIRONMENT "GST_PLUGIN_PATH=$<TARGET_FILE_DIR:gstprerecordloop>:$ENV{GST_PLUGIN_PATH}"
```

Therefore you do NOT need to prefix commands with `GST_PLUGIN_PATH=…` when invoking tests through `ctest`; the plugin
module (`libgstprerecordloop.so`) is discovered automatically.

If you run a test binary directly (bypassing CTest), set the variable yourself, for example:

```sh
GST_PLUGIN_PATH=build/Debug/gstprerecordloop ./build/Debug/tests/unit_test_concurrent_flush_ignore
```

Or temporarily export:

```sh
export GST_PLUGIN_PATH="$(pwd)/build/Debug/gstprerecordloop:$GST_PLUGIN_PATH"
./build/Debug/tests/unit_test_queue_pruning
```

If you install the plugin system-wide (e.g. into a directory already on the GStreamer plugin path), neither step is required.

## Test Execution with Debug Logging

For detailed GStreamer logging during test execution:

```sh
GST_DEBUG=*:4,pre_record_loop:7,pre_record_loop_dataflow:7 ctest --test-dir build/Debug -V
```

If built with lifecycle diagnostics enabled (`PREREC_ENABLE_LIFE_DIAG=1`), you can also include:

```sh
GST_DEBUG=prerec_lifecycle:7,prerec_dataflow:5 ctest --test-dir build/Debug -R prerec_unit_no_refcount_critical -V
```

## Test Directory Structure

Tests are organized by configuration:
- Debug builds: Use `--test-dir build/Debug`
- Release builds: Use `--test-dir build/Release`

Make sure to specify the appropriate test directory based on your build configuration.

# Running the test app from the Build directory

In addition to the tests, there is a small [test app described here](testapp/README.md).