#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T015: Integration - re-arm cycle
 * Scenario: BUFFERING -> flush trigger -> PASS_THROUGH -> (future) re-arm event -> BUFFERING.
 * Re-arm event not implemented yet, forced fail.
 */

static int fail(const char *msg) { fprintf(stderr, "T015 FAIL (expected): %s\n", msg); return 1; }

static void send_flush_trigger(GstElement *pr, const char *name) {
  GstPad *sinkpad = gst_element_get_static_pad(pr, "sink");
  if (!sinkpad) return;
  const gchar *evname = name ? name : "prerecord-flush";
  GstStructure *s = gst_structure_new(evname, NULL, NULL);
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  gst_pad_push_event(sinkpad, ev);
  gst_object_unref(sinkpad);
}

/* Placeholder for future re-arm custom event sender */
static void send_rearm_event(GstElement *pr) {
  (void)pr; /* not implemented yet */
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t015-rearm-cycle")) return fail("pipeline create failed");

  guint64 pts = 0; const guint64 dur = GST_SECOND;
  if (!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL)) return fail("initial gop failed");

  send_flush_trigger(tp.pr, NULL);
  if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL)) return fail("post flush gop failed");

  send_rearm_event(tp.pr); /* no-op now */
  if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL)) return fail("post re-arm gop failed");

  g_error("T015 forced fail: re-arm cycle behavior not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
