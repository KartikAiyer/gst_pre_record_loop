/* T010: Queue pruning invariants (Now ACTIVE)
 * Validates that when buffered duration exceeds max-time, the element prunes
 * entire GOPs while retaining at least a 2-GOP floor (adaptive). Also checks
 * stats counters reflect pruning.
 */

#define FAIL_PREFIX "T010 FAIL: "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) FAIL("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t010-pipeline")) FAIL("pipeline creation failed");

  /* Configure max-time small enough to force pruning when 3 GOPs inserted.
   * Each GOP is 5 seconds (1 key + 4 deltas at 1s each). We set max-time = 11s:
   *  - After 2 GOPs: 10s < 11s (no prune)
   *  - After 3 GOPs: 15s >= 11s triggers prune; should drop exactly 1 GOP, leaving 2.
   */
  g_object_set(tp.pr, "max-time", 11, NULL);

  guint64 ts = 0;
  const guint64 per_buf = 1 * GST_SECOND; /* 1s per frame */
  const guint   deltas_per_gop = 4; /* => GOP = 5 seconds */

  /* Pre-roll: push a single keyframe buffer so element switches to BUFFERING
   * before we start counting GOPs for pruning assertions. */
  {
    GstBuffer *preroll = gst_buffer_new();
    GST_BUFFER_PTS(preroll) = ts;
    GST_BUFFER_DURATION(preroll) = per_buf;
    if (gst_app_src_push_buffer(GST_APP_SRC(tp.appsrc), preroll) != GST_FLOW_OK)
      FAIL("preroll push failed");
    ts += per_buf;
  }

  /* Insert 4 GOPs -> total 20s; expect pruning to drop oldest (maybe two) but retain >=2 GOPs */
  for (int g = 0; g < 4; ++g) {
    if (!prerec_push_gop(tp.appsrc, deltas_per_gop, &ts, per_buf, NULL))
      FAIL("push_gop failed");
  }

  /* No sleep needed: pushes are synchronous into downstream element under test. */

  if (!prerec_wait_for_stats(tp.pr, 2, 1, 1500)) {
    FAIL("timed out waiting for pruning stats (need >=2 gops and >=1 drop)");
  }
  GstQuery *q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q)) { gst_query_unref(q); FAIL("stats query failed"); }
  const GstStructure *s = gst_query_get_structure(q);
  guint drops_gops=0,drops_buffers=0,queued_gops=0,queued_buffers=0;
  gst_structure_get_uint(s, "drops-gops", &drops_gops);
  gst_structure_get_uint(s, "drops-buffers", &drops_buffers);
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_structure_get_uint(s, "queued-buffers", &queued_buffers);
  gst_query_unref(q);

  if (queued_gops < 2)
    FAIL("queued_gops < 2 after pruning (floor violated)");
  if (drops_gops == 0)
    FAIL("drops_gops not incremented");
  if (drops_buffers == 0)
    FAIL("drops_buffers not incremented");

  g_print("T010 PASS: gops_cur=%u drops_gops=%u drops_buf=%u buffers_cur=%u\n",
          queued_gops, drops_gops, drops_buffers, queued_buffers);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
