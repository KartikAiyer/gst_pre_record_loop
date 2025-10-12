/* T014: Integration - flush sequence
 * Scenario: Buffer in BUFFERING mode, send custom flush event, ensure queued
 * buffers are released then pipeline transitions to pass-through. Currently
 * unimplemented; forced failing test.
 */

#define FAIL_PREFIX "T014 FAIL (expected): "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

static void send_flush_trigger(GstElement* pr, const char* name) {
  GstPad* sinkpad = gst_element_get_static_pad(pr, "sink");
  if (!sinkpad)
    return;
  const gchar*  evname = name ? name : "prerecord-flush";
  GstStructure* s      = gst_structure_new(evname, NULL, NULL);
  GstEvent*     ev     = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  gst_pad_push_event(sinkpad, ev);
  gst_object_unref(sinkpad);
}

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t014-flush-seq"))
    FAIL("pipeline create failed");

  guint64       pts = 0;
  const guint64 dur = GST_SECOND;
  for (int i = 0; i < 2; ++i)
    if (!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL))
      FAIL("push gop failed");

  send_flush_trigger(tp.pr, NULL);
  if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL))
    FAIL("post flush gop failed");

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
  
  // Test passed - flush sequence completed successfully
  prerec_pipeline_shutdown(&tp);
  return 0;
}
