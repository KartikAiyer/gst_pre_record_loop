/* T026: Validate internal stats counters: flush_count and rearm_count
 * 
 * Test Flow:
 *   Part 1: Initial state → verify flush_count=0, rearm_count=0
 *   Part 2: Send flush #1 → verify flush_count=1
 *   Part 3: Send flush #2 (concurrent, should be ignored) → verify flush_count=1 (unchanged)
 *   Part 4: Send re-arm #1 → verify rearm_count=1
 *   Part 5: Send flush #3 → verify flush_count=2
 *   Part 6: Send re-arm #2 → verify rearm_count=2
 * 
 * Success Criteria:
 *   - flush_count increments on each accepted prerecord-flush event
 *   - Concurrent flush during drain does NOT increment flush_count
 *   - rearm_count increments on each prerecord-arm event
 */

#define FAIL_PREFIX "T026 FAIL: "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

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

/* Query stats and extract flush_count and rearm_count */
static gboolean get_stats_counters(GstElement *pr, guint *flush_count, guint *rearm_count) {
  GstQuery *q = gst_query_new_custom(GST_QUERY_CUSTOM,
                                     gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(pr, q)) {
    gst_query_unref(q);
    return FALSE;
  }
  const GstStructure *s = gst_query_get_structure(q);
  gboolean has_flush = gst_structure_get_uint(s, "flush-count", flush_count);
  gboolean has_rearm = gst_structure_get_uint(s, "rearm-count", rearm_count);
  gst_query_unref(q);
  return has_flush && has_rearm;
}

/* Wait for processing to stabilize */
static void wait_for_processing(GstElement *pipeline) {
  for (int i = 0; i < 20; ++i) {
    gst_bus_timed_pop_filtered(gst_element_get_bus(pipeline), 5 * GST_MSECOND, GST_MESSAGE_ANY);
    while (g_main_context_iteration(NULL, FALSE));
  }
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) FAIL("factory not available");
  
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t026-stats")) FAIL("pipeline creation failed");

  guint64 emitted = 0;
  gulong probe_id = prerec_attach_count_probe(tp.pr, &emitted);
  if (!probe_id) FAIL("failed to attach emission probe");

  guint64 ts = 0;
  const guint64 delta = GST_SECOND;
  guint flush_count = 0, rearm_count = 0;

  /* === PART 1: Initial state verification === */
  g_print("T026: Part 1 - Verifying initial stats...\n");
  if (!get_stats_counters(tp.pr, &flush_count, &rearm_count))
    FAIL("part1: stats query failed");
  if (flush_count != 0)
    FAIL("part1: expected flush_count=0, got %u", flush_count);
  if (rearm_count != 0)
    FAIL("part1: expected rearm_count=0, got %u", rearm_count);
  g_print("T026: Part 1 ✓ - Initial flush_count=0, rearm_count=0\n");

  /* === PART 2: First flush (should increment flush_count to 1) === */
  g_print("T026: Part 2 - Buffering 2 GOPs and sending first flush...\n");
  for (int i = 0; i < 2; ++i) {
    if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
      FAIL("part2: gop push failed");
  }
  wait_for_processing(tp.pipeline);
  
  if (!send_flush(tp.pr)) FAIL("part2: flush send failed");
  wait_for_processing(tp.pipeline);
  
  if (!get_stats_counters(tp.pr, &flush_count, &rearm_count))
    FAIL("part2: stats query failed");
  if (flush_count != 1)
    FAIL("part2: expected flush_count=1, got %u", flush_count);
  if (rearm_count != 0)
    FAIL("part2: expected rearm_count=0, got %u", rearm_count);
  g_print("T026: Part 2 ✓ - flush_count=1 after first flush\n");

  /* === PART 3: Concurrent flush (should be ignored, flush_count unchanged) === */
  g_print("T026: Part 3 - Sending concurrent flush (should be ignored)...\n");
  if (!send_flush(tp.pr)) FAIL("part3: flush send failed");
  wait_for_processing(tp.pipeline);
  
  if (!get_stats_counters(tp.pr, &flush_count, &rearm_count))
    FAIL("part3: stats query failed");
  if (flush_count != 1)
    FAIL("part3: expected flush_count=1 (unchanged), got %u", flush_count);
  g_print("T026: Part 3 ✓ - flush_count=1 (concurrent flush ignored)\n");

  /* === PART 4: First re-arm (should increment rearm_count to 1) === */
  g_print("T026: Part 4 - Sending first re-arm...\n");
  if (!send_rearm(tp.pr)) FAIL("part4: rearm send failed");
  wait_for_processing(tp.pipeline);
  
  if (!get_stats_counters(tp.pr, &flush_count, &rearm_count))
    FAIL("part4: stats query failed");
  if (flush_count != 1)
    FAIL("part4: expected flush_count=1, got %u", flush_count);
  if (rearm_count != 1)
    FAIL("part4: expected rearm_count=1, got %u", rearm_count);
  g_print("T026: Part 4 ✓ - rearm_count=1 after first re-arm\n");

  /* === PART 5: Second flush (should increment flush_count to 2) === */
  g_print("T026: Part 5 - Buffering 1 GOP and sending second flush...\n");
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
    FAIL("part5: gop push failed");
  wait_for_processing(tp.pipeline);
  
  if (!send_flush(tp.pr)) FAIL("part5: flush send failed");
  wait_for_processing(tp.pipeline);
  
  if (!get_stats_counters(tp.pr, &flush_count, &rearm_count))
    FAIL("part5: stats query failed");
  if (flush_count != 2)
    FAIL("part5: expected flush_count=2, got %u", flush_count);
  if (rearm_count != 1)
    FAIL("part5: expected rearm_count=1, got %u", rearm_count);
  g_print("T026: Part 5 ✓ - flush_count=2 after second flush\n");

  /* === PART 6: Second re-arm (should increment rearm_count to 2) === */
  g_print("T026: Part 6 - Sending second re-arm...\n");
  if (!send_rearm(tp.pr)) FAIL("part6: rearm send failed");
  wait_for_processing(tp.pipeline);
  
  if (!get_stats_counters(tp.pr, &flush_count, &rearm_count))
    FAIL("part6: stats query failed");
  if (flush_count != 2)
    FAIL("part6: expected flush_count=2, got %u", flush_count);
  if (rearm_count != 2)
    FAIL("part6: expected rearm_count=2, got %u", rearm_count);
  g_print("T026: Part 6 ✓ - rearm_count=2 after second re-arm\n");

  /* === Cleanup === */
  g_print("T026 PASS: All stats counter validations successful\n");
  prerec_remove_probe(tp.pr, probe_id);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
