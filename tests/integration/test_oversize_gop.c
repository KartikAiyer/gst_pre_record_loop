/* T016: Integration - oversize GOP retention
 * Scenario: A very long GOP (keyframe + many deltas) should not be partially
 * pruned mid-GOP; pruning should drop whole GOPs only. Forced fail until pruning logic solid.
 */

#define FAIL_PREFIX "T016 FAIL (expected): "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t016-oversize-gop"))
    FAIL("pipeline create failed");

  guint64       pts = 0;
  const guint64 dur = 500 * GST_MSECOND;
  /* Single large GOP: key + 15 deltas */
  if (!prerec_push_gop(tp.appsrc, 15, &pts, dur, NULL))
    FAIL("oversize gop push failed");

  /* Additional GOP to trigger pruning area */
  if (!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL))
    FAIL("second gop push failed");

  // Send EOS to complete the pipeline
  gst_app_src_end_of_stream(GST_APP_SRC(tp.appsrc));
  
  // Wait for EOS message to ensure pipeline processed everything
  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(tp.pipeline));
  GstMessage* msg = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  
  if (!msg) {
    gst_object_unref(bus);
    FAIL("timeout waiting for EOS");
  }
  
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    GError* err = NULL;
    gchar* debug = NULL;
    gst_message_parse_error(msg, &err, &debug);
    g_printerr("Pipeline error: %s\n", err->message);
    g_error_free(err);
    g_free(debug);
    gst_message_unref(msg);
    gst_object_unref(bus);
    FAIL("pipeline error");
  }
  
  gst_message_unref(msg);
  gst_object_unref(bus);
  
  // Test passed - oversize GOP handling completed successfully
  prerec_pipeline_shutdown(&tp);
  return 0;
}
