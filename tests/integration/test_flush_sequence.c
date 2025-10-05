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

static void send_flush_trigger(GstElement *pr, const char *name) {
  GstPad *sinkpad = gst_element_get_static_pad(pr, "sink");
  if (!sinkpad) return;
  const gchar *evname = name ? name : "prerecord-flush";
  GstStructure *s = gst_structure_new(evname, NULL, NULL);
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  gst_pad_push_event(sinkpad, ev);
  gst_object_unref(sinkpad);
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) FAIL("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t014-flush-seq")) FAIL("pipeline create failed");

  guint64 pts = 0; const guint64 dur = GST_SECOND;
  for (int i=0;i<2;++i) if(!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL)) FAIL("push gop failed");

  send_flush_trigger(tp.pr, NULL);
  if(!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL)) FAIL("post flush gop failed");

  g_error("T014 forced fail: flush integration behavior not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
