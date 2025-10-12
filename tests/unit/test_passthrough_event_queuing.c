/* T034b: SEGMENT/GAP event queuing mode validation
 *
 * Validates that SEGMENT and GAP events are only queued in BUFFERING mode,
 * not in PASS_THROUGH mode. This prevents:
 * 1. Duplicate event emission (once during buffering, again after flush)
 * 2. Memory waste from queuing events that will never be used
 * 3. Incorrect state when transitioning back to BUFFERING
 *
 * Test scenarios:
 * - In BUFFERING mode: SEGMENT/GAP events should be queued
 * - In PASS_THROUGH mode: SEGMENT/GAP events should NOT be queued
 * - After flush trigger: Verify events are not duplicated downstream
 *
 * Expected initial behavior: Test MUST FAIL until T034b implementation complete.
 */

#include <gst/gst.h>
#define FAIL_PREFIX "T034b FAIL: "
#include <test_utils.h>

static gint segment_count = 0;
static gint gap_count     = 0;

static GstPadProbeReturn downstream_event_counter(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
    if (event) {
      switch (GST_EVENT_TYPE(event)) {
      case GST_EVENT_SEGMENT:
        segment_count++;
        g_print("T034b: SEGMENT event #%d downstream\n", segment_count);
        break;
      case GST_EVENT_GAP:
        gap_count++;
        g_print("T034b: GAP event #%d downstream\n", gap_count);
        break;
      default:
        break;
      }
    }
  }
  return GST_PAD_PROBE_OK;
}

int main(int argc, char* argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    FAIL("Could not locate plugin factory");
  }

  PrerecTestPipeline p;
  if (!prerec_pipeline_create(&p, "event_queuing")) {
    FAIL("Failed to create test pipeline");
  }

  GstPad* sink = gst_element_get_static_pad(p.pr, "sink");
  GstPad* src  = gst_element_get_static_pad(p.pr, "src");
  if (!sink || !src) {
    if (sink)
      gst_object_unref(sink);
    if (src)
      gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get pads");
  }

  /* Install probe to count events going downstream */
  gst_pad_add_probe(src, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, downstream_event_counter, NULL, NULL);

  guint64 pts = 0;

  /* Phase 1: BUFFERING mode - push GOP and send GAP event */
  g_print("T034b: Phase 1 - Testing event queuing in BUFFERING mode\n");

  if (!prerec_push_gop(p.appsrc, 2, &pts, GST_SECOND, NULL)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push initial GOP");
  }

  if (!prerec_wait_for_stats(p.pr, 1, 0, 1000)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Initial GOP did not queue");
  }

  /* Send a GAP event while in BUFFERING mode - should be queued */
  GstEvent* gap_event = gst_event_new_gap(pts, GST_SECOND);
  pts += GST_SECOND;
  if (!gst_pad_send_event(sink, gap_event)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event not accepted in BUFFERING mode");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  gint buffering_segment_count = segment_count;
  gint buffering_gap_count     = gap_count;

  g_print("T034b: After BUFFERING phase - SEGMENT count=%d, GAP count=%d\n", buffering_segment_count,
          buffering_gap_count);

  /* Phase 2: Trigger flush to transition to PASS_THROUGH */
  g_print("T034b: Phase 2 - Triggering flush to enter PASS_THROUGH mode\n");

  segment_count = 0; /* Reset counter to track flush emissions */
  gap_count     = 0;

  GstStructure* trigger_struct = gst_structure_new_empty("prerecord-flush");
  GstEvent*     flush_trigger  = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, trigger_struct);
  if (!gst_pad_send_event(sink, flush_trigger)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Flush trigger event rejected");
  }

  g_usleep(100000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  gint flush_segment_count = segment_count;
  gint flush_gap_count     = gap_count;

  g_print("T034b: After flush - SEGMENT count=%d, GAP count=%d\n", flush_segment_count, flush_gap_count);

  /* Verify that queued events were emitted during flush */
  if (flush_gap_count == 0) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event was not emitted during flush (should have been queued in BUFFERING)");
  }

  /* Phase 3: PASS_THROUGH mode - send new GAP event, should NOT be queued */
  g_print("T034b: Phase 3 - Testing event queuing in PASS_THROUGH mode\n");

  segment_count = 0;
  gap_count     = 0;

  /* Send GAP event while in PASS_THROUGH - should go through immediately, not queued */
  gap_event = gst_event_new_gap(pts, GST_SECOND);
  pts += GST_SECOND;
  if (!gst_pad_send_event(sink, gap_event)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event not accepted in PASS_THROUGH mode");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  gint passthrough_gap_count = gap_count;

  g_print("T034b: In PASS_THROUGH - GAP count=%d (should be 1, immediate)\n", passthrough_gap_count);

  if (passthrough_gap_count != 1) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event not immediately forwarded in PASS_THROUGH mode (got %d, expected 1)", passthrough_gap_count);
  }

  /* Phase 4: Re-arm and verify no duplicate events */
  g_print("T034b: Phase 4 - Re-arming to BUFFERING and checking for duplicates\n");

  GstStructure* arm_struct = gst_structure_new_empty("prerecord-arm");
  GstEvent*     arm_event  = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, arm_struct);
  if (!gst_element_send_event(p.pr, arm_event)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Re-arm event rejected");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  /* Push new GOP in BUFFERING mode */
  if (!prerec_push_gop(p.appsrc, 2, &pts, GST_SECOND, NULL)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push GOP after re-arm");
  }

  if (!prerec_wait_for_stats(p.pr, 1, 0, 1000)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("GOP did not queue after re-arm");
  }

  /* Send another GAP in BUFFERING mode */
  segment_count = 0;
  gap_count     = 0;

  gap_event = gst_event_new_gap(pts, GST_SECOND);
  pts += GST_SECOND;
  if (!gst_pad_send_event(sink, gap_event)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("GAP event not accepted after re-arm");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  /* Verify the PASS_THROUGH GAP was not queued (shouldn't appear in next flush) */
  segment_count = 0;
  gap_count     = 0;

  trigger_struct = gst_structure_new_empty("prerecord-flush");
  flush_trigger  = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, trigger_struct);
  if (!gst_pad_send_event(sink, flush_trigger)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Second flush trigger rejected");
  }

  g_usleep(100000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  g_print("T034b: After second flush - GAP count=%d\n", gap_count);

  /* Should only see the GAP we sent in BUFFERING mode after re-arm,
   * NOT the one sent during PASS_THROUGH */
  if (gap_count != 1) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Unexpected GAP count after second flush (got %d, expected 1). "
         "PASS_THROUGH GAP may have been incorrectly queued!",
         gap_count);
  }

  gst_object_unref(src);
  gst_object_unref(sink);
  prerec_pipeline_shutdown(&p);

  g_print("T034b PASS: Events correctly queued only in BUFFERING mode\n");
  return 0;
}
