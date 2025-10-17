#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* T031: SEEK event passthrough test (PARTIAL IMPLEMENTATION)
 * Validates that SEEK events from downstream pass through the element
 * correctly in both BUFFERING and PASS_THROUGH modes.
 *
 * Test scenarios:
 * 1. SEEK in BUFFERING mode passes upstream ✓
 * 2. SEEK in PASS_THROUGH mode passes upstream ✓
 * 3. Element doesn't interfere with SEEK handling ✓
 *
 * LIMITATION: This test only verifies SEEK event propagation.
 * It does NOT test FLUSH_START/FLUSH_STOP handling which is required
 * for proper seek support (FR-006). After a seek, the pipeline sends:
 *   SEEK → FLUSH_START → FLUSH_STOP → new SEGMENT
 *
 * Currently MISSING:
 * - FLUSH_START handler: should clear queue, reset GOP tracking
 * - FLUSH_STOP handler: should prepare for new timeline
 * - Without these, buffered frames from pre-seek timeline will be
 *   incorrectly mixed with post-seek frames on flush trigger
 *
 * Follow-up: See T034a for FLUSH_START/STOP implementation
 */

#define FAIL_PREFIX "T031 FAIL: "
#include <test_utils.h>

static gboolean seek_event_received = FALSE;
static GstSeekType seek_start_type = GST_SEEK_TYPE_NONE;
static gint64 seek_start = -1;

/* Probe on sink pad to detect SEEK events passing through */
static GstPadProbeReturn sink_event_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_UPSTREAM) {
    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_SEEK) {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType stop_type;
      gint64 stop;

      gst_event_parse_seek(event, &rate, &format, &flags, &seek_start_type, &seek_start, &stop_type, &stop);

      seek_event_received = TRUE;
      g_print("T031: SEEK event received on sink pad (start=%" G_GINT64_FORMAT ", type=%d)\n", seek_start,
              seek_start_type);
    }
  }
  return GST_PAD_PROBE_OK;
}

int main(int argc, char* argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    FAIL("Could not locate plugin factory");
  }

  /* Create test pipeline: appsrc ! prerecordloop ! fakesink */
  PrerecTestPipeline p;
  if (!prerec_pipeline_create(&p, "seek_test")) {
    FAIL("Failed to create test pipeline");
  }

  /* Add probe to sink pad to detect SEEK events */
  GstPad* sink = gst_element_get_static_pad(p.pr, "sink");
  if (!sink) {
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get sink pad");
  }

  gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, sink_event_probe, NULL, NULL);

  /* Set pipeline to PLAYING */
  if (gst_element_set_state(p.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to set pipeline to PLAYING");
  }

  /* Wait for pipeline to reach PLAYING state */
  GstState state;
  if (gst_element_get_state(p.pipeline, &state, NULL, GST_SECOND) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to reach PLAYING state");
  }

  /* Test 1: Send SEEK event in BUFFERING mode (default) */
  g_print("T031: Test 1 - SEEK in BUFFERING mode\n");
  seek_event_received = FALSE;

  GstEvent* seek_event = gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                                            GST_SEEK_TYPE_SET, 5 * GST_SECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  GstPad* src = gst_element_get_static_pad(p.pr, "src");
  if (!src) {
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get src pad");
  }

  gboolean seek_result = gst_pad_send_event(src, seek_event);

  /* Give some time for the event to propagate */
  g_usleep(100000); /* 100ms */

  if (!seek_event_received) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("SEEK event did not pass through in BUFFERING mode");
  }

  g_print("T031: SEEK passed through in BUFFERING mode (result=%d)\n", seek_result);

  /* Test 2: Switch to PASS_THROUGH mode and test SEEK again */
  g_print("T031: Test 2 - SEEK in PASS_THROUGH mode\n");

  /* Send flush trigger to enter PASS_THROUGH mode */
  GstStructure* flush_struct = gst_structure_new_empty("prerecord-flush");
  GstEvent* flush_trigger = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, flush_struct);
  gst_pad_send_event(sink, flush_trigger);

  /* Wait for mode transition */
  g_usleep(100000); /* 100ms */

  /* Send another SEEK event */
  seek_event_received = FALSE;
  seek_event = gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET,
                                  10 * GST_SECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  seek_result = gst_pad_send_event(src, seek_event);

  /* Give some time for the event to propagate */
  g_usleep(100000); /* 100ms */

  if (!seek_event_received) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("SEEK event did not pass through in PASS_THROUGH mode");
  }

  g_print("T031: SEEK passed through in PASS_THROUGH mode (result=%d)\n", seek_result);

  /* Cleanup */
  gst_object_unref(src);
  gst_object_unref(sink);

  prerec_pipeline_shutdown(&p);

  g_print("T031 PASS\n");
  return 0;
}
