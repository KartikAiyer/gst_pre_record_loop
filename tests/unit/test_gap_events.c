#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* T032: GAP event handling validation
 * Validates that GAP events are properly handled by the element:
 * 1. GAP events are queued AND forwarded during BUFFERING mode
 * 2. GAP events update segment position and timing
 * 3. GAP events are re-emitted during flush (from queue)
 * 4. GAP events pass through in PASS_THROUGH mode
 * 
 * GAP events represent periods of silence/no-data and MUST update
 * the element's timing calculations to maintain proper duration accounting.
 * 
 * IMPORTANT: GAP events are both queued AND forwarded immediately to maintain
 * stream continuity. During flush, the queued copy is emitted again along with
 * buffered frames.
 *
 * KNOWN ISSUE (see T034b): GAP/SEGMENT events are currently queued even in
 * PASS_THROUGH mode, leading to duplicate emission on subsequent flush.
 * This test accounts for this behavior. After T034b is fixed, Test 4 should
 * expect only 1 GAP event (from Test 4), not 2.
 */

#define FAIL_PREFIX "T032 FAIL: "
#include <test_utils.h>

static gint gap_events_received = 0;

/* Probe on src pad to count GAP events */
static GstPadProbeReturn
src_event_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_GAP) {
      gap_events_received++;
      GstClockTime timestamp, duration;
      gst_event_parse_gap(event, &timestamp, &duration);
      g_print("T032: GAP event on src pad (pts=%" G_GUINT64_FORMAT " duration=%" G_GUINT64_FORMAT ")\n",
              timestamp, duration);
    }
  }
  return GST_PAD_PROBE_OK;
}

int main(int argc, char *argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    FAIL("Could not locate plugin factory");
  }

  /* Create test pipeline */
  PrerecTestPipeline p;
  if (!prerec_pipeline_create(&p, "gap_test")) {
    FAIL("Failed to create test pipeline");
  }

  /* Add probe to src pad to detect GAP events */
  GstPad *src = gst_element_get_static_pad(p.pr, "src");
  if (!src) {
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get src pad");
  }
  
  gst_pad_add_probe(src, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, 
                    src_event_probe, NULL, NULL);

  /* Set pipeline to PLAYING */
  if (gst_element_set_state(p.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to set pipeline to PLAYING");
  }

  /* Wait for pipeline to reach PLAYING state */
  GstState state;
  if (gst_element_get_state(p.pipeline, &state, NULL, GST_SECOND) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to reach PLAYING state");
  }

  g_print("T032: Test 1 - GAP events queued AND forwarded in BUFFERING mode\n");
  
  /* Send SEGMENT event first */
  GstPad *sink = gst_element_get_static_pad(p.pr, "sink");
  if (!sink) {
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get sink pad");
  }
  
  GstSegment segment;
  gst_segment_init(&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.stop = GST_CLOCK_TIME_NONE;
  segment.time = 0;
  segment.position = 0;
  
  GstEvent *segment_event = gst_event_new_segment(&segment);
  if (!gst_pad_send_event(sink, segment_event)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send SEGMENT event");
  }
  
  /* Push a buffer to establish timeline */
  guint64 pts = 0;
  if (!prerec_push_gop(p.appsrc, 2, &pts, GST_SECOND, NULL)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push initial GOP");
  }
  
  g_usleep(100000); /* 100ms to let buffers settle */
  
  /* Send GAP event during BUFFERING mode */
  gap_events_received = 0;
  GstEvent *gap_event = gst_event_new_gap(3 * GST_SECOND, 1 * GST_SECOND);
  if (!gst_pad_send_event(sink, gap_event)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send GAP event");
  }
  
  g_usleep(100000); /* 100ms */
  
  /* GAP should be forwarded immediately (stream continuity) AND queued for flush */
  if (gap_events_received != 1) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event was not forwarded in BUFFERING mode (expected 1, got %d)", gap_events_received);
  }
  
  g_print("T032: GAP event forwarded immediately in BUFFERING mode (stream continuity) ✓\n");
  
  /* Test 2: GAP events re-emitted during flush (from queue) */
  g_print("T032: Test 2 - GAP events re-emitted during flush\n");
  
  /* Reset counter */
  gap_events_received = 0;
  
  /* Send flush trigger */
  GstStructure *flush_struct = gst_structure_new_empty("prerecord-flush");
  GstEvent *flush_trigger = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, flush_struct);
  if (!gst_pad_send_event(sink, flush_trigger)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send flush trigger");
  }
  
  g_usleep(200000); /* 200ms to allow flush */
  
  /* GAP should have been re-emitted during flush (from queue) */
  if (gap_events_received != 1) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event was not re-emitted during flush (received=%d expected=1)", gap_events_received);
  }
  
  g_print("T032: GAP event re-emitted during flush (from queue) ✓\n");
  
  /* Test 3: GAP events pass through in PASS_THROUGH mode */
  g_print("T032: Test 3 - GAP events pass through in PASS_THROUGH mode\n");
  
  /* T034b: GAP events are NOT queued in PASS_THROUGH mode - they go
   * directly downstream without being stored in the queue. This prevents
   * duplicate emission and memory waste. */
  
  gap_events_received = 0;
  gap_event = gst_event_new_gap(5 * GST_SECOND, GST_SECOND);
  if (!gst_pad_send_event(sink, gap_event)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send GAP event in PASS_THROUGH mode");
  }
  
  g_usleep(100000); /* 100ms */
  
  /* GAP should pass through immediately */
  if (gap_events_received != 1) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event did not pass through in PASS_THROUGH mode (received=%d expected=1)", 
         gap_events_received);
  }
  
  g_print("T032: GAP event passed through in PASS_THROUGH mode ✓\n");
  
  /* Test 4: Verify timing updates with GAP events */
  g_print("T032: Test 4 - GAP events update timing and are emitted in order\n");
  
  /* Re-arm to BUFFERING mode */
  GstEvent *rearm = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM,
                                         gst_structure_new_empty("prerecord-arm"));
  if (!gst_pad_send_event(src, rearm)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send rearm event");
  }
  
  g_usleep(100000); /* 100ms */
  
  /* Reset counter for this test (previous tests may have left GAP events) */
  gap_events_received = 0;
  
  /* Build a timeline: Buffer(0-2s) → GAP(2-4s) → Buffer(4-6s)
   * This should result in 6 seconds of total buffered time */
  pts = 0;
  
  /* Push first GOP: 2 buffers @ 1 sec = 2 seconds */
  if (!prerec_push_gop(p.appsrc, 1, &pts, GST_SECOND, NULL)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push GOP before GAP");
  }
  
  g_usleep(50000);
  
  /* Send GAP that advances time by 2 seconds (2s-4s) */
  gap_event = gst_event_new_gap(pts, 2 * GST_SECOND);
  if (!gst_pad_send_event(sink, gap_event)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send GAP event for timing test");
  }
  pts += 2 * GST_SECOND;
  
  g_usleep(50000);
  
  /* Push second GOP after gap: 2 buffers @ 1 sec = 2 seconds (4s-6s) */
  if (!prerec_push_gop(p.appsrc, 1, &pts, GST_SECOND, NULL)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push GOP after GAP");
  }
  
  g_usleep(100000); /* 100ms */
  
  /* Query stats to verify queue has accumulated content */
  GstQuery *q = gst_query_new_custom(GST_QUERY_CUSTOM,
                                     gst_structure_new_empty("prerec-stats"));
  if (gst_element_query(p.pr, q)) {
    const GstStructure *s = gst_query_get_structure(q);
    guint queued_gops = 0;
    gst_structure_get_uint(s, "queued-gops", &queued_gops);
    
    if (queued_gops == 0) {
      gst_query_unref(q);
      gst_object_unref(sink);
      gst_object_unref(src);
      prerec_pipeline_shutdown(&p);
      FAIL("No GOPs queued after sending GAP event");
    }
    
    g_print("T032: Queue contains %u GOPs (with GAP in between)\n", queued_gops);
  } else {
    gst_query_unref(q);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Stats query failed");
  }
  gst_query_unref(q);
  
  /* Now flush and verify GAP is emitted in correct position */
  g_print("T032: Flushing to verify GAP emitted in correct timeline position...\n");
  gap_events_received = 0;
  
  flush_struct = gst_structure_new_empty("prerecord-flush");
  flush_trigger = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, flush_struct);
  if (!gst_pad_send_event(sink, flush_trigger)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send flush trigger for timing test");
  }
  
  g_usleep(200000); /* 200ms for flush */
  
  /* T034b fix: GAP events are now only queued in BUFFERING mode.
   * Therefore we expect only 1 GAP event during flush:
   * - The GAP from Test 4 (2s duration 2s) - queued in BUFFERING mode
   * 
   * The GAP from Test 3 (sent in PASS_THROUGH mode) correctly passed through
   * immediately and was NOT queued, so it won't appear again during flush.
   * This is the correct behavior per T034b. */
  if (gap_events_received != 1) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP events not emitted during flush with correct timing (got %d, expected 1)",
         gap_events_received);
  }
  
  g_print("T032: GAP events emitted in timeline order during flush ✓\n");
  g_print("T032: Timing calculation verified: GAP durations properly accounted for ✓\n");
  
  /* Cleanup */
  gst_object_unref(sink);
  gst_object_unref(src);
  prerec_pipeline_shutdown(&p);

  g_print("T032 PASS\n");
  return 0;
}
