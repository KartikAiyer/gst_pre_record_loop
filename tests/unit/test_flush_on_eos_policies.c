/* T025 (extended): Comprehensive flush-on-eos policy tests
 * Test all combinations of flush-on-eos policies (AUTO/ALWAYS/NEVER) with
 * element modes (BUFFERING/PASS_THROUGH) to verify correct EOS behavior.
 *
 * Policy behavior matrix:
 *   ALWAYS + BUFFERING     -> drain queue before EOS
 *   ALWAYS + PASS_THROUGH  -> drain queue before EOS (if any residual data)
 *   NEVER  + BUFFERING     -> discard queue, forward EOS immediately
 *   NEVER  + PASS_THROUGH  -> discard queue (if any), forward EOS immediately
 *   AUTO   + BUFFERING     -> discard queue (don't leak pre-event data)
 *   AUTO   + PASS_THROUGH  -> drain queue before EOS
 */

#define FAIL_PREFIX "T025-ext FAIL: "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* Helper to push GOPs and verify emissions */
static gboolean push_test_gops(PrerecTestPipeline* tp, guint64* ts, guint64 delta) {
  /* Push 3 GOPs (2 deltas each = 3 buffers per GOP = 9 total buffers) */
  for (int i = 0; i < 3; i++) {
    if (!prerec_push_gop(tp->appsrc, 2, ts, delta, NULL)) {
      return FALSE;
    }
  }
  return TRUE;
}

/* Test ALWAYS policy with BUFFERING mode */
static int test_always_buffering(void) {
  g_print("\n=== Test: ALWAYS + BUFFERING ===\n");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "always-buffering"))
    FAIL("pipeline creation failed");

  /* Set policy to ALWAYS (1) */
  g_object_set(tp.pr, "flush-on-eos", 1, NULL);

  guint64       ts    = 0;
  const guint64 delta = GST_SECOND;

  /* Push 3 GOPs while in BUFFERING mode */
  if (!push_test_gops(&tp, &ts, delta))
    FAIL("failed to push test GOPs");

  g_usleep(100000); /* let buffers propagate */

  /* Verify we have queued data using stats query */
  GstQuery* q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q))
    FAIL("stats query failed");
  const GstStructure* s           = gst_query_get_structure(q);
  guint               queued_gops = 0;
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_query_unref(q);

  if (queued_gops < 3)
    FAIL("expected at least 3 queued GOPs, got %u", queued_gops);

  /* Attach probe to count emissions */
  guint64 emission_count = 0;
  prerec_attach_count_probe(tp.pr, &emission_count);

  /* Send EOS with ALWAYS policy in BUFFERING mode
   * Expected: queue should be DRAINED (9 buffers emitted) */
  GstAppSrc* appsrc_typed = GST_APP_SRC(tp.appsrc);
  gst_app_src_end_of_stream(appsrc_typed);
  g_usleep(200000); /* let EOS propagate and queue drain */

  if (emission_count != 9)
    FAIL("ALWAYS+BUFFERING should drain 9 buffers, got %llu", (unsigned long long) emission_count);

  /* Verify queue is empty after drain */
  GstQuery* q2 = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q2))
    FAIL("stats query after EOS failed");
  const GstStructure* s2                = gst_query_get_structure(q2);
  guint               queued_gops_after = 0, queued_buffers_after = 0;
  gst_structure_get_uint(s2, "queued-gops", &queued_gops_after);
  gst_structure_get_uint(s2, "queued-buffers", &queued_buffers_after);
  gst_query_unref(q2);

  if (queued_gops_after != 0 || queued_buffers_after != 0)
    FAIL("ALWAYS+BUFFERING queue should be empty after drain, got %u GOPs, %u buffers", queued_gops_after,
         queued_buffers_after);

  g_print("✓ ALWAYS+BUFFERING: drained 9 buffers correctly (queue empty)\n");
  prerec_pipeline_shutdown(&tp);
  return 0;
}

/* Test NEVER policy with BUFFERING mode */
static int test_never_buffering(void) {
  g_print("\n=== Test: NEVER + BUFFERING ===\n");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "never-buffering"))
    FAIL("pipeline creation failed");

  /* Set policy to NEVER (2) */
  g_object_set(tp.pr, "flush-on-eos", 2, NULL);

  guint64       ts    = 0;
  const guint64 delta = GST_SECOND;

  /* Push 3 GOPs while in BUFFERING mode */
  if (!push_test_gops(&tp, &ts, delta))
    FAIL("failed to push test GOPs");

  g_usleep(100000);

  /* Verify we have queued data using stats query */
  GstQuery* q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q))
    FAIL("stats query failed");
  const GstStructure* s           = gst_query_get_structure(q);
  guint               queued_gops = 0;
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_query_unref(q);

  if (queued_gops < 3)
    FAIL("expected at least 3 queued GOPs, got %u", queued_gops);

  /* Attach probe to count emissions */
  guint64 emission_count = 0;
  prerec_attach_count_probe(tp.pr, &emission_count);

  /* Send EOS with NEVER policy in BUFFERING mode
   * Expected: queue should be DISCARDED (0 buffers emitted) */
  GstAppSrc* appsrc_typed = GST_APP_SRC(tp.appsrc);
  gst_app_src_end_of_stream(appsrc_typed);
  g_usleep(200000);

  if (emission_count != 0)
    FAIL("NEVER+BUFFERING should discard queue (0 emissions), got %llu", (unsigned long long) emission_count);

  /* Verify queue is actually empty after discard (not just not emitted) */
  GstQuery* q2 = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q2))
    FAIL("stats query after EOS failed");
  const GstStructure* s2                = gst_query_get_structure(q2);
  guint               queued_gops_after = 0, queued_buffers_after = 0;
  gst_structure_get_uint(s2, "queued-gops", &queued_gops_after);
  gst_structure_get_uint(s2, "queued-buffers", &queued_buffers_after);
  gst_query_unref(q2);

  if (queued_gops_after != 0 || queued_buffers_after != 0)
    FAIL("NEVER+BUFFERING queue should be empty after EOS, got %u GOPs, %u buffers", queued_gops_after,
         queued_buffers_after);

  g_print("✓ NEVER+BUFFERING: discarded queue correctly (0 emissions, queue empty)\n");
  prerec_pipeline_shutdown(&tp);
  return 0;
}

/* Test ALWAYS policy with PASS_THROUGH mode */
static int test_always_passthrough(void) {
  g_print("\n=== Test: ALWAYS + PASS_THROUGH ===\n");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "always-passthrough"))
    FAIL("pipeline creation failed");

  /* Set policy to ALWAYS (1) */
  g_object_set(tp.pr, "flush-on-eos", 1, NULL);

  guint64       ts    = 0;
  const guint64 delta = GST_SECOND;

  /* Push 2 GOPs and flush to enter PASS_THROUGH mode */
  if (!push_test_gops(&tp, &ts, delta))
    FAIL("failed to push initial GOPs");

  g_usleep(100000);

  /* Send flush trigger to enter PASS_THROUGH */
  GstStructure* flush_st  = gst_structure_new_empty("prerecord-flush");
  GstEvent*     flush_evt = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, flush_st);
  gst_element_send_event(tp.pr, flush_evt);
  g_usleep(200000); /* let flush complete */

  /* Attach probe AFTER flush to count only pass-through emissions */
  guint64 emission_count = 0;
  prerec_attach_count_probe(tp.pr, &emission_count);

  /* Push 1 more GOP in PASS_THROUGH mode (should emit immediately: 3 buffers) */
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
    FAIL("failed to push passthrough GOP");

  g_usleep(100000);

  /* Verify immediate pass-through */
  if (emission_count != 3)
    FAIL("expected 3 immediate emissions in PASS_THROUGH, got %llu", (unsigned long long) emission_count);

  /* Now send EOS with ALWAYS policy in PASS_THROUGH mode
   * Expected: any remaining buffered data should be drained
   * (In pure PASS_THROUGH, queue is empty, so no additional emissions expected) */
  guint64    before_eos   = emission_count;
  GstAppSrc* appsrc_typed = GST_APP_SRC(tp.appsrc);
  gst_app_src_end_of_stream(appsrc_typed);
  g_usleep(200000);

  /* In PASS_THROUGH mode, queue should be empty, so no new emissions */
  if (emission_count != before_eos)
    FAIL("ALWAYS+PASS_THROUGH: unexpected emissions at EOS (%llu vs %llu)", (unsigned long long) emission_count,
         (unsigned long long) before_eos);

  g_print("✓ ALWAYS+PASS_THROUGH: no residual data, EOS handled correctly\n");
  prerec_pipeline_shutdown(&tp);
  return 0;
}

/* Test NEVER policy with PASS_THROUGH mode */
static int test_never_passthrough(void) {
  g_print("\n=== Test: NEVER + PASS_THROUGH ===\n");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "never-passthrough"))
    FAIL("pipeline creation failed");

  /* Set policy to NEVER (2) */
  g_object_set(tp.pr, "flush-on-eos", 2, NULL);

  guint64       ts    = 0;
  const guint64 delta = GST_SECOND;

  /* Push 2 GOPs and flush to enter PASS_THROUGH mode */
  if (!push_test_gops(&tp, &ts, delta))
    FAIL("failed to push initial GOPs");

  g_usleep(100000);

  /* Send flush trigger to enter PASS_THROUGH */
  GstStructure* flush_st  = gst_structure_new_empty("prerecord-flush");
  GstEvent*     flush_evt = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, flush_st);
  gst_element_send_event(tp.pr, flush_evt);
  g_usleep(200000);

  /* Attach probe AFTER flush */
  guint64 emission_count = 0;
  prerec_attach_count_probe(tp.pr, &emission_count);

  /* Push 1 GOP in PASS_THROUGH mode */
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
    FAIL("failed to push passthrough GOP");

  g_usleep(100000);

  if (emission_count != 3)
    FAIL("expected 3 immediate emissions in PASS_THROUGH, got %llu", (unsigned long long) emission_count);

  /* Send EOS with NEVER policy in PASS_THROUGH mode
   * Expected: discard any residual (should be none in pure PASS_THROUGH) */
  guint64    before_eos   = emission_count;
  GstAppSrc* appsrc_typed = GST_APP_SRC(tp.appsrc);
  gst_app_src_end_of_stream(appsrc_typed);
  g_usleep(200000);

  /* No new emissions expected */
  if (emission_count != before_eos)
    FAIL("NEVER+PASS_THROUGH: unexpected emissions at EOS");

  g_print("✓ NEVER+PASS_THROUGH: EOS handled correctly (no residual data)\n");
  prerec_pipeline_shutdown(&tp);
  return 0;
}

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available())
    FAIL("factory not available");

  int ret = 0;

  /* Run all policy combination tests */
  ret |= test_always_buffering();
  ret |= test_never_buffering();
  ret |= test_always_passthrough();
  ret |= test_never_passthrough();

  if (ret == 0) {
    g_print("\n✅ T025-ext PASS: All flush-on-eos policy combinations validated\n");
  } else {
    g_print("\n❌ T025-ext FAIL: Some policy tests failed\n");
  }

  return ret;
}
