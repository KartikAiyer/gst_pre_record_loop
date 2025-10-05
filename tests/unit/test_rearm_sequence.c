/* T012: Validate flush → re-arm → buffer cycle with emission counting.
 * 
 * Test Flow:
 *   Phase 1 (Buffering): Push 3 GOPs → expect 0 emissions (buffered)
 *   Phase 2 (First Flush): Send flush → expect 9 buffers emitted (3 GOPs × 3 buffers each)
 *   Phase 3 (Pass-through): Push 1 GOP → expect immediate emission (2 buffers: 1 keyframe + 1 delta)
 *   Phase 4 (Re-arm): Send re-arm → push 1 GOP → expect 0 new emissions (buffering again)
 *   Phase 5 (Second Flush): Send flush → expect 3 buffers emitted (1 GOP × 3 buffers)
 */
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

static int fail(const char *msg) { g_critical("T012 FAIL: %s", msg); return 1; }

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

/* Wait for emission count to stabilize (no change for stable_threshold consecutive checks) */
static void wait_for_stable_emission(GstElement *pipeline, guint64 *emitted, guint stable_threshold, guint max_attempts) {
  guint64 last = *emitted;
  guint stable = 0;
  for (guint i = 0; i < max_attempts && stable < stable_threshold; ++i) {
    gst_bus_timed_pop_filtered(gst_element_get_bus(pipeline), 5 * GST_MSECOND, GST_MESSAGE_ANY);
    while (g_main_context_iteration(NULL, FALSE));
    if (*emitted == last) {
      stable++;
    } else {
      last = *emitted;
      stable = 0;
    }
  }
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t012-emission")) return fail("pipeline creation failed");

  guint64 emitted = 0;
  gulong probe_id = prerec_attach_count_probe(tp.pr, &emitted);
  if (!probe_id) return fail("failed to attach emission probe");

  guint64 ts = 0;
  const guint64 delta = GST_SECOND;

  /* === PHASE 1: Initial buffering (3 GOPs, expect 0 emissions) === */
  g_print("T012: Phase 1 - Pushing 3 GOPs (buffering mode)...\n");
  for (int i = 0; i < 3; ++i) {
    if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
      return fail("phase1: gop push failed");
  }
  wait_for_stable_emission(tp.pipeline, &emitted, 10, 50);
  if (emitted != 0) {
    fprintf(stderr, "T012 FAIL: phase1 expected 0 emissions (buffering), got %llu\n", (unsigned long long)emitted);
    return 1;
  }
  g_print("T012: Phase 1 ✓ - 0 emissions (buffers queued)\n");

  /* === PHASE 2: First flush (expect 9 buffers: 3 GOPs × 3 buffers) === */
  g_print("T012: Phase 2 - Sending first flush...\n");
  if (!send_flush(tp.pr)) return fail("phase2: flush send failed");
  wait_for_stable_emission(tp.pipeline, &emitted, 10, 100);
  if (emitted != 9) {
    fprintf(stderr, "T012 FAIL: phase2 expected 9 emissions (3 GOPs), got %llu\n", (unsigned long long)emitted);
    return 1;
  }
  g_print("T012: Phase 2 ✓ - 9 buffers flushed\n");

  /* === PHASE 3: Pass-through (push 1 GOP, expect immediate emission of 2 buffers) === */
  g_print("T012: Phase 3 - Pushing 1 GOP in pass-through mode...\n");
  guint64 before_passthrough = emitted;
  if (!prerec_push_gop(tp.appsrc, 1, &ts, delta, NULL))
    return fail("phase3: passthrough push failed");
  wait_for_stable_emission(tp.pipeline, &emitted, 10, 50);
  guint64 passthrough_emitted = emitted - before_passthrough;
  if (passthrough_emitted != 2) {
    fprintf(stderr, "T012 FAIL: phase3 expected 2 emissions (pass-through), got %llu\n", (unsigned long long)passthrough_emitted);
    return 1;
  }
  g_print("T012: Phase 3 ✓ - 2 buffers emitted immediately (pass-through)\n");

  /* === PHASE 4: Re-arm + buffer (push 1 GOP, expect 0 new emissions) === */
  g_print("T012: Phase 4 - Sending re-arm and pushing 1 GOP...\n");
  if (!send_rearm(tp.pr)) return fail("phase4: rearm send failed");
  guint64 before_rearm = emitted;
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
    return fail("phase4: post-rearm push failed");
  wait_for_stable_emission(tp.pipeline, &emitted, 10, 50);
  guint64 rearm_emitted = emitted - before_rearm;
  if (rearm_emitted != 0) {
    fprintf(stderr, "T012 FAIL: phase4 expected 0 emissions (buffering after re-arm), got %llu\n", (unsigned long long)rearm_emitted);
    return 1;
  }
  g_print("T012: Phase 4 ✓ - 0 emissions (buffering resumed)\n");

  /* === PHASE 5: Second flush (expect 3 buffers: 1 GOP × 3 buffers) === */
  g_print("T012: Phase 5 - Sending second flush...\n");
  guint64 before_flush2 = emitted;
  if (!send_flush(tp.pr)) return fail("phase5: second flush send failed");
  wait_for_stable_emission(tp.pipeline, &emitted, 10, 100);
  guint64 flush2_emitted = emitted - before_flush2;
  if (flush2_emitted != 3) {
    fprintf(stderr, "T012 FAIL: phase5 expected 3 emissions (1 GOP), got %llu\n", (unsigned long long)flush2_emitted);
    return 1;
  }
  g_print("T012: Phase 5 ✓ - 3 buffers flushed\n");

  g_print("T012 PASS: Complete re-arm cycle validated (total %llu emissions)\n", (unsigned long long)emitted);
  prerec_remove_probe(tp.pr, probe_id);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
