# prerecordloop Quick Reference

**Version**: 0.1.0 (Baseline Spec Complete)  
**Status**: ✅ Production Ready  
**Element Type**: GStreamer 1.0 Video Filter  

---

## One-Line Summary
GOP-aware ring buffer for encoded video with event-driven flush/re-arm control.

---

## Pipeline Example

```bash
gst-launch-1.0 videotestsrc ! x264enc ! h264parse ! \
  pre_record_loop max-time=30 flush-on-eos=AUTO ! \
  qtmux ! filesink location=output.mp4
```

---

## Custom Events

### Flush (Downstream)
```c
GstEvent *flush = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
    gst_structure_new_empty("prerecord-flush"));
gst_pad_send_event(element_sink_pad, flush);
```
**Effect**: Drains buffered GOPs, transitions to PASS_THROUGH mode

### Re-arm (Upstream)
```c
GstEvent *arm = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM,
    gst_structure_new_empty("prerecord-arm"));
gst_element_send_event(element, arm);
```
**Effect**: Returns to BUFFERING mode, resets baseline timestamp

---

## Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `silent` | boolean | `false` | Suppress info messages |
| `max-time` | guint64 | `10` (seconds) | Ring buffer duration |
| `flush-on-eos` | enum | `AUTO` | EOS behavior (AUTO/ALWAYS/NEVER) |
| `flush-trigger-name` | string | `"prerecord-flush"` | Custom event structure name |

---

## Build & Test

```bash
# Build
conan install . --build=missing --settings=build_type=Debug
cmake --preset=conan-debug
cmake --build build/Debug --parallel 6

# Test (all 22 tests)
ctest --test-dir build/Debug

# Memory leak check (macOS)
ctest --test-dir build/Debug -R prerec_memory_leak_baseline -V

# CI pipeline
bash .ci/run-tests.sh
```

---

## Performance

- **Pruning Latency**: Median 6µs, 99p 59µs
- **GOP Floor**: Minimum 2 complete GOPs always retained
- **Thread Safety**: Mutex-guarded state transitions

---

## Debugging

```bash
# Enable debug logs
export GST_DEBUG=pre_record_loop:7,pre_record_loop_dataflow:5

# Enable metric logging
export GST_PREREC_METRICS=1

# Inspect element
gst-inspect-1.0 pre_record_loop
```

---

## File Locations

```
gstprerecordloop/
├── inc/gstprerecordloop/gstprerecordloop.h
└── src/gstprerecordloop.c

tests/
├── unit/          (17 tests)
├── integration/   (3 tests)
├── perf/          (1 test)
└── memory/        (1 test)

docs/
├── README.md
├── CHANGELOG.md
└── specs/000-prerecordloop-baseline/
    ├── COMPLETION_SUMMARY.md
    ├── tasks.md
    └── spec.md
```

---

## Common Use Cases

### 1. Motion-Triggered Recording
```c
// Keep 30 seconds of pre-motion video
g_object_set(prerecord, "max-time", 30, NULL);

// On motion detected:
GstEvent *flush = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
    gst_structure_new_empty("prerecord-flush"));
gst_pad_send_event(sink_pad, flush);
```

### 2. Manual Recording Control
```c
// Start buffering
GstEvent *arm = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM,
    gst_structure_new_empty("prerecord-arm"));
gst_element_send_event(element, arm);

// User presses "record" button - flush buffer to file
GstEvent *flush = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
    gst_structure_new_empty("prerecord-flush"));
gst_pad_send_event(sink_pad, flush);
```

### 3. Multi-Stage Recording
```c
// Initial recording phase (buffering)
// Trigger 1: Flush to recording 1
send_flush_event();

// Continue pass-through (still recording to file)
// Trigger 2: Re-arm for next buffering phase
send_rearm_event();

// Trigger 3: Flush to recording 2
send_flush_event();
```

---

## Stats Query

```c
GstQuery *q = gst_query_new_custom(GST_QUERY_CUSTOM,
    gst_structure_new_empty("prerec-stats"));
gst_element_query(element, q);

const GstStructure *s = gst_query_get_structure(q);
guint queued_gops, drops_gops;
gst_structure_get_uint(s, "queued-gops", &queued_gops);
gst_structure_get_uint(s, "drops-gops", &drops_gops);
```

---

## Troubleshooting

**Plugin not found**:
```bash
export GST_PLUGIN_PATH=/path/to/build/Debug/gstprerecordloop:$GST_PLUGIN_PATH
gst-inspect-1.0 pre_record_loop
```

**Refcount warnings**:
- Check `GST_DEBUG=*:4,pre_record_loop:7` logs
- Verify event ownership (use `gst_event_ref()` if storing/forwarding)

**Unexpected buffering behavior**:
- Check `max-time` property (default 10 seconds)
- Verify GOP structure (must have clear keyframes)
- Check flush-on-eos policy matches use case

**Memory leaks suspected**:
```bash
# macOS
ctest --test-dir build/Debug -R prerec_memory_leak_baseline -V

# Linux
bash tests/memory/test_leaks_valgrind.sh
```

---

## State Machine

```
┌─────────────┐
│  BUFFERING  │◄─────┐
│ (queue GOPs)│      │
└──────┬──────┘      │
       │ flush event │ re-arm event
       ▼             │
┌─────────────┐      │
│PASS_THROUGH │──────┘
│ (live emit) │
└─────────────┘
```

---

## License & Contact

See LICENSE file for details.

**Spec**: `specs/000-prerecordloop-baseline/`  
**Issues**: Check `specs/000-prerecordloop-baseline/COMPLETION_SUMMARY.md` for known issues  
**Status**: Production ready (October 11, 2025)
