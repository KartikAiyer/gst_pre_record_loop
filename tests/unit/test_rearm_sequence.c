#include "test_utils.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T012: Re-arm / custom flush event sequence
 * Intent: After initial buffering and a custom flush trigger (future event
 * like prerecord-flush), element should switch to pass-through without
 * dropping post-flush GOP boundaries. Currently behavior not implemented,
 * so we force fail.
 */

static int fail(const char *msg) {
  fprintf(stderr, "T012 FAIL (expected while unimplemented): %s\n", msg);
  return 1;
}

static void send_flush_trigger(GstElement *pr) {
  GstPad *sinkpad = gst_element_get_static_pad(pr, "sink");
  if (!sinkpad) return;
  GstStructure *s = gst_structure_new("prerecord-flush", NULL, NULL);
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  gst_pad_push_event(sinkpad, ev);
  gst_object_unref(sinkpad);
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t012-pipeline")) return fail("pipeline creation failed");

  /* Buffer some GOPs */
  const guint64 delta = 2 * GST_SECOND;
  guint64 ts = 0;
  for (int gop = 0; gop < 2; ++gop) {
    if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
      return fail("push gop failed");
  }

  /* Future: trigger flush + push more buffers and assert ordering + mode switch. */
  send_flush_trigger(tp.pr);

  /* Now push another GOP to represent post-flush pass-through */
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
    return fail("push post-flush gop failed");

  g_error("T012 forced fail: re-arm / pass-through invariants not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
