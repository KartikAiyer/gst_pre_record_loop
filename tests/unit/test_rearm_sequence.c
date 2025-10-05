/* T012 Simplified: Validate we can flush, re-arm, and buffer again (stats-based). */
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

static int fail(const char *msg) { fprintf(stderr, "T012 FAIL: %s\n", msg); return 1; }

static gboolean send_flush(GstElement *pr) {
  GstStructure *s = gst_structure_new_empty("prerecord-flush");
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  return gst_element_send_event(pr, ev);
}
static gboolean send_rearm(GstElement *pr) {
  GstStructure *s = gst_structure_new_empty("prerecord-arm");
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, s);
  return gst_element_send_event(pr, ev);
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t012-simplified")) return fail("pipeline creation failed");

  guint64 ts = 0; const guint64 delta = GST_SECOND;
  for (int i=0;i<3;++i) if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL)) return fail("initial gop push failed");
  if (!prerec_wait_for_stats(tp.pr, 1, 0, 500)) return fail("initial stats timeout");
  if (!send_flush(tp.pr)) return fail("first flush send failed");
  /* Push passthrough GOP */
  if (!prerec_push_gop(tp.appsrc, 1, &ts, delta, NULL)) return fail("passthrough push failed");
  if (!send_rearm(tp.pr)) return fail("rearm send failed");
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL)) return fail("post-arm gops failed");
  if (!prerec_wait_for_stats(tp.pr, 1, 0, 500)) return fail("post-arm stats timeout");
  if (!send_flush(tp.pr)) return fail("second flush send failed");
  fprintf(stdout, "T012 PASS: simplified re-arm cycle complete\n");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
