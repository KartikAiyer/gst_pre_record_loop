#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T011: Enforce 2-GOP floor during pruning (ACTIVE)
 * After pruning due to max-time overflow, at least two full GOPs must remain
 * buffered (unless fewer than two have ever been queued). Validate stats.
 */

static int fail(const char *msg) { fprintf(stderr, "T011 FAIL: %s\n", msg); return 1; }

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) {
     return fail("factory not available");
  }

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t011-pipeline")) return fail("pipeline creation failed");

  /* Configure max-time to force pruning after >2 GOPs. GOP: 1 key + 3 deltas (4 frames).
   * Frame duration: 1s -> GOP = 4s. max-time=9s means after 3 GOPs (12s) prune -> leave 2. */
  g_object_set(tp.pr, "max-time", 9, NULL);
  const guint64 per_buf = 1 * GST_SECOND;
  guint64 ts = 0;
  /* Preroll single keyframe */
  {
    GstBuffer *preroll = gst_buffer_new();
    GST_BUFFER_PTS(preroll) = ts;
    GST_BUFFER_DURATION(preroll) = per_buf;
    if (gst_app_src_push_buffer(GST_APP_SRC(tp.appsrc), preroll) != GST_FLOW_OK)
      return fail("preroll push failed");
    ts += per_buf;
  }
  for (int g = 0; g < 4; ++g) { /* push 4 GOPs -> multiple prunes possible */
    if (!prerec_push_gop(tp.appsrc, 3, &ts, per_buf, NULL))
      return fail("push gop failed");
  }
  /* No sleep needed: synchronous buffer handling. */

  if (!prerec_wait_for_stats(tp.pr, 2, 1, 1500)) {
    return fail("timeout waiting for stats floor condition");
  }
  GstQuery *q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q)) { gst_query_unref(q); return fail("stats query failed"); }
  const GstStructure *s = gst_query_get_structure(q);
  guint drops_gops=0,drops_buffers=0,queued_gops=0;
  gst_structure_get_uint(s, "drops-gops", &drops_gops);
  gst_structure_get_uint(s, "drops-buffers", &drops_buffers);
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_query_unref(q);
  if (queued_gops < 2)
    return fail("2-GOP floor violated");
  if (drops_gops == 0)
    return fail("Expected at least one GOP drop");
  g_print("T011 PASS: gops_cur=%u drops_gops=%u drops_buf=%u\n", queued_gops, drops_gops, drops_buffers);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
