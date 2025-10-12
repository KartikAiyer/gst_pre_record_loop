# Test Application for prerecordloop Element

## Overview
This test application demonstrates the prerecordloop element's flush trigger functionality in a real-world scenario, including:
- **Ring buffer behavior** with automatic GOP-based pruning
- **Event-driven recording** triggered at a specific frame count
- **Pre-event capture** of the last N seconds before trigger
- **Live pass-through** after trigger for post-event capture

## Key Demonstration
- **Total duration**: 30 seconds (900 frames @ 30fps)
- **Buffer capacity**: 10 seconds (default `max-time` setting)
- **Flush trigger**: Sent at frame 600 (20 seconds into stream)
- **Expected output**: ~581 frames (19.37 seconds)
  - Last ~10 seconds before trigger (buffered frames with pruning)
  - 10 seconds after trigger (live pass-through)
  
This simulates a security camera that continuously buffers video and saves pre-event + live footage when motion is detected.

## Pipeline Architecture
```
videotestsrc (900 frames @ 30fps, 30s total)
  → vtenc_h264 (H.264 encoding)
  → h264parse
  → prerecordloop (max-time=10s default)
  → qtmux
  → filesink (output_prerecord.mp4)
```

## Flush Trigger Implementation

### Frame Counting Probe
A GStreamer pad probe is attached to the videotestsrc's src pad to count outgoing buffers:
- Counts each buffer (frame) that passes through
- Logs progress every 30 frames
- Triggers flush event at frame 600 (2/3 through 900 total frames)

### Probe Callback Logic
```c
static GstPadProbeReturn frame_counter_probe(
    GstPad* pad, 
    GstPadProbeInfo* info, 
    gpointer user_data)
{
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        ProbeData* data = (ProbeData*)user_data;
        data->frame_count++;
        
        if (data->frame_count == 600 && !data->flush_sent) {
            // Create and send flush event
            GstEvent* flush = gst_event_new_custom(
                GST_EVENT_CUSTOM_DOWNSTREAM,
                gst_structure_new_empty("prerecord-flush")
            );
            gst_element_send_event(data->prerecordloop, flush);
            data->flush_sent = TRUE;
        }
    }
    return GST_PAD_PROBE_OK; // Pass buffer through
}
```

## Expected Behavior

### Timeline
| Time Range | Frames | prerecordloop Mode | Output Behavior |
|------------|--------|-------------------|------------------|
| 0s - 10s | 1-300 | BUFFERING | Frames queued in ring buffer |
| 10s - 20s | 301-600 | BUFFERING (with pruning) | Old GOPs dropped to maintain 10s window |
| 20s (flush trigger) | 600 | BUFFERING → PASS_THROUGH | Queue drained (emits ~281 buffered frames) |
| 20s - 30s | 601-900 | PASS_THROUGH | Frames passed through live |

### Output File Contents
The output file `output_prerecord.mp4` contains **581 frames** (19.37 seconds):
1. **~281 buffered frames**: The last ~10 seconds before flush (frames ~320-600)
   - Frames 1-319 were **pruned** during buffering to maintain the 10-second `max-time` limit
2. **300 live frames**: Content passed through after flush (frames 601-900)

**Key Points**:
- The flush trigger **drains** the buffer (emits buffered frames), it does **not** discard them
- The ring buffer continuously **prunes** old GOPs when buffer duration exceeds `max-time`
- This demonstrates motion-triggered recording where you want:
  - Pre-event footage (last N seconds before motion detected)
  - Live footage (frames after motion detected)

## Verification

### Build and Run
```bash
# Build the test application
cmake --build build/Debug --target prerec --parallel 6

# Set GST_PLUGIN_PATH to include the prerecordloop plugin
export GST_PLUGIN_PATH="$(pwd)/build/Debug/gstprerecordloop:$GST_PLUGIN_PATH"

# Verify plugin is discoverable
gst-inspect-1.0 pre_record_loop

# Run the application
cd build/Debug/testapp
./prerec.app/Contents/MacOS/prerec
```

**Note**: The test application relies on `GST_PLUGIN_PATH` environment variable to locate the `prerecordloop` plugin. Make sure to set it before running the application.

### Expected Console Output
```
Successfully obtained prerecordloop element reference
Successfully obtained videotestsrc element reference
Added frame counter probe to videotestsrc src pad
Registered signal handler for Ctrl-C. Press Ctrl-C to stop recording.
Processed 30 frames...
Processed 60 frames...
...
Processed 570 frames...
Frame 600 reached - Sending flush trigger to prerecordloop!
Flush event sent successfully!
Processed 600 frames...
...
Processed 900 frames...
End-Of-Stream reached.
```

### Verify Output File
```bash
# Check file exists
ls -lh output_prerecord.mp4

# Count frames (should be 581 frames)
ffprobe -v error -select_streams v:0 -count_packets \
  -show_entries stream=nb_read_packets -of csv=p=0 \
  output_prerecord.mp4

# Check duration (should be ~19.37 seconds)
gst-discoverer-1.0 output_prerecord.mp4 2>/dev/null | grep Duration
```

### Debug Logs
Enable GStreamer debug logs to see state transitions and pruning:
```bash
GST_DEBUG=pre_record_loop:6 ./prerec.app/Contents/MacOS/prerec 2>&1 | \
  grep -E "(Pruning|drops_gops|flush|mode|BUFFERING|PASS_THROUGH)"
```

Expected log output shows continuous pruning after 10 seconds, then flush at frame 600:
```
# Pruning begins after buffer fills (10 seconds)
0:00:10.278993292 Prune loop start: queued_gops=11
0:00:10.279858084 Dropped 1 events and 29 buffers
0:00:10.279865084 Prune loop end: queued_gops=10
...
# Flush trigger at 20 seconds (frame 600)
Received flush trigger 'prerecord-flush'
STATE TRANSITION: BUFFERING → PASS_THROUGH | Stats: drops_gops=11 drops_buffers=319 drops_events=1 flush_count=1 rearm_count=0
Switched to passthrough mode after trigger
```

## Use Case Demonstration

This test demonstrates the **motion-triggered recording** pattern:

1. **Continuous Buffering**: Camera continuously records into ring buffer
2. **Motion Detected**: External system detects motion and sends flush trigger
3. **Capture Pre-Event**: Buffered footage (before motion) is saved to file
4. **Capture Live**: Live footage (during/after motion) is saved to file
5. **Re-arm**: Send `prerecord-arm` event to return to buffering mode

### Real-World Analogy
Think of a security camera that:
- Continuously buffers the last **10 seconds** of video (ring buffer with `max-time=10`)
- Old footage is automatically discarded as new frames arrive (pruning)
- When motion is detected at 20 seconds, it saves:
  - The buffered 10 seconds (what led up to the motion - frames from ~10s-20s mark)
  - Live video for the next 10 seconds (what happens during/after motion - frames 20s-30s)
- Total saved: ~19.4 seconds (581 frames)
  - Pre-event context: ~9.4 seconds (281 frames)
  - Post-event capture: 10 seconds (300 frames)

## Code Structure

### Main Components
```c
// ProbeData structure tracks state across probe callbacks
typedef struct {
    guint frame_count;      // Current frame number
    GstElement* prerecordloop; // Reference to send events
    gboolean flush_sent;    // Prevent duplicate flush
} ProbeData;

// Pipeline creation (with element naming, using default max-time=10)
"... pre_record_loop name=prerecordloop ! ..."

// Element reference retrieval
GstElement* prerecordloop = gst_bin_get_by_name(
    GST_BIN(pipeline), "prerecordloop");

// Probe attachment
GstPad* src_pad = gst_element_get_static_pad(videotestsrc, "src");
gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, 
    frame_counter_probe, &probe_data, NULL);
```

### Resource Management
```c
// Cleanup (in order)
1. gst_object_unref(bus);
2. gst_object_unref(prerecordloop);  // Element reference
3. gst_element_set_state(pipeline, GST_STATE_NULL);
4. gst_object_unref(pipeline);
```

## Extending the Test

### Alternative Behaviors

#### Discard Buffered Content
To discard buffered frames instead of emitting them, send `prerecord-arm` immediately after flush:
```c
// Flush (drains buffer)
GstEvent* flush = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
    gst_structure_new_empty("prerecord-flush"));
gst_element_send_event(prerecordloop, flush);

// Re-arm (resets buffer, discards old content)
GstEvent* arm = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM,
    gst_structure_new_empty("prerecord-arm"));
gst_element_send_event(prerecordloop, arm);
```

#### Multiple Event Cycles
Test flush → pass-through → re-arm → buffering cycles:
```c
// Frame 210: First flush (save frames 1-210)
// Frame 280: Re-arm (return to buffering)
// Frame 350: Second flush (save frames 281-350)
// Frame 420: EOS
```

#### External Trigger Simulation
Replace frame counter with external input:
- Listen on UDP port for trigger messages
- Monitor file system for trigger file
- Subscribe to MQTT topic for motion events

## Performance Characteristics

Based on performance testing (T037):
- **Latency**: Median 0.006ms, 99th percentile 0.059ms
- **Overhead**: Sub-millisecond queue operations
- **Memory**: Bounded by max-time setting (10s buffer ≈ 120MB @ 1080p H.264)
- **Pruning**: Automatic GOP-based pruning maintains constant memory usage

## Troubleshooting

### Issue: "Failed to get prerecordloop element from pipeline"
**Cause**: Element name mismatch  
**Fix**: Ensure pipeline uses `name=prerecordloop` in description

### Issue: Flush event not received
**Cause**: Wrong event structure name  
**Fix**: Verify `gst_structure_new_empty("prerecord-flush")` matches `flush-trigger-name` property

### Issue: Output has fewer frames than expected
**Cause**: This is **correct behavior** - ring buffer prunes old GOPs when duration exceeds `max-time`  
**Details**: With default `max-time=10s`, only the most recent 10 seconds are retained in buffer  
**Fix**: Increase `max-time` property if you need longer pre-event footage

### Issue: All frames always in output (no pruning)
**Cause**: `max-time` setting is larger than the time until flush trigger  
**Fix**: Use default `max-time=10s` or set explicitly to a value smaller than flush trigger time

### Issue: Frame count off by one
**Cause**: Probe counting buffers vs. display counting from 1  
**Fix**: This is expected - first buffer is frame 1, not frame 0

## Related Documentation
- [QUICK_REFERENCE.md](../QUICK_REFERENCE.md) - API reference
- [COMPLETION_SUMMARY.md](../COMPLETION_SUMMARY.md) - Full baseline spec report
- [specs/000-prerecordloop-baseline/spec.md](../specs/000-prerecordloop-baseline/spec.md) - Requirements
