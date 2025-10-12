/* T015: Integration - multi re-arm cycles with emission validation
 *
 * Test Flow (3 cycles):
 *   Cycle 1-3:
 *     Phase A (Buffering): Push 2 GOPs (4 buffers total) → expect 0 emissions
 *     Phase B (Flush): Send flush → expect 4 buffers emitted
 *     Phase C (Pass-through): Push 1 GOP (2 buffers) → expect immediate 2 emissions
 *     Phase D (Re-arm): Send re-arm → back to buffering
 *   Final: Push 1 GOP, flush → expect 2 buffers
 *
 * Validates:
 *   - PTS monotonicity across all cycles
 *   - Correct emission counts per phase
 *   - Buffering vs pass-through behavior transitions
 */

#define FAIL_PREFIX "T015 FAIL: "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

static gboolean send_flush_trigger(GstElement* pr, const char* name) {
  const gchar*  evname = name ? name : "prerecord-flush";
  GstStructure* s      = gst_structure_new_empty(evname);
  GstEvent*     ev     = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  return gst_element_send_event(pr, ev);
}

/* Placeholder for future re-arm custom event sender */
static gboolean send_rearm_event(GstElement* pr) {
  GstStructure* s  = gst_structure_new_empty("prerecord-arm");
  GstEvent*     ev = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, s);
  return gst_element_send_event(pr, ev);
}

typedef struct {
  guint64  emitted;
  guint64  last_pts;
  gboolean pts_monotonic;
} EmissionStats;

static GstPadProbeReturn integration_count_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) && user_data) {
    EmissionStats* st  = (EmissionStats*) user_data;
    GstBuffer*     buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf) {
      GstClockTime pts = GST_BUFFER_PTS(buf);
      if (GST_CLOCK_TIME_IS_VALID(pts)) {
        if (st->emitted > 0 && pts < st->last_pts) {
          fprintf(stderr, "T015: PTS discontinuity at buffer %llu: last=%llu current=%llu\n",
                  (unsigned long long) st->emitted, (unsigned long long) st->last_pts, (unsigned long long) pts);
          st->pts_monotonic = FALSE;
        }
        st->last_pts = pts;
      }
    }
    st->emitted++;
  }
  return GST_PAD_PROBE_OK;
}

/* Wait for emission count to stabilize */
static void wait_for_stable_emission(GstElement* pipeline, guint64* emitted_ptr, guint stable_threshold,
                                     guint max_attempts) {
  guint64 last   = *emitted_ptr;
  guint   stable = 0;
  for (guint i = 0; i < max_attempts && stable < stable_threshold; ++i) {
    gst_bus_timed_pop_filtered(gst_element_get_bus(pipeline), 5 * GST_MSECOND, GST_MESSAGE_ANY);
    while (g_main_context_iteration(NULL, FALSE))
      ;
    if (*emitted_ptr == last) {
      stable++;
    } else {
      last   = *emitted_ptr;
      stable = 0;
    }
  }
}

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t015-rearm-multicycle"))
    FAIL("pipeline create failed");

  /* Larger max-time to ensure no pruning during cycles */
  g_object_set(tp.pr, "max-time", (guint64) (10 * GST_SECOND), NULL);

  /* Attach probe with timestamp continuity tracking */
  GstPad* srcpad = gst_element_get_static_pad(tp.pr, "src");
  if (!srcpad)
    FAIL("no src pad");
  EmissionStats est = {0, GST_CLOCK_TIME_NONE, TRUE};
  gulong        pid = gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, integration_count_probe_cb, &est, NULL);
  gst_object_unref(srcpad);
  if (!pid)
    FAIL("probe attach failed");

  guint64       pts    = 0;
  const guint64 dur    = GST_SECOND;
  const int     cycles = 3;

  for (int cycle = 0; cycle < cycles; ++cycle) {
    g_print("T015: === Cycle %d ===\n", cycle + 1);

    /* === Phase A: Buffering (push 2 GOPs, expect 0 emissions) === */
    g_print("T015: Cycle %d Phase A - Buffering 2 GOPs...\n", cycle + 1);
    guint64 before_buffer = est.emitted;
    for (int g = 0; g < 2; ++g) {
      if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL))
        FAIL("buffer phase gop push failed");
    }
    wait_for_stable_emission(tp.pipeline, &est.emitted, 10, 50);
    guint64 buffered_emissions = est.emitted - before_buffer;
    if (buffered_emissions != 0)
      FAIL("cycle %d phase A expected 0 emissions (buffering), got %llu", cycle + 1,
           (unsigned long long) buffered_emissions);
    g_print("T015: Cycle %d Phase A ✓ - 0 emissions (buffered)\n", cycle + 1);

    /* === Phase B: Flush (expect 4 buffers: 2 GOPs × 2 buffers each) === */
    g_print("T015: Cycle %d Phase B - Flushing...\n", cycle + 1);
    guint64 before_flush = est.emitted;
    if (!send_flush_trigger(tp.pr, NULL))
      FAIL("flush trigger failed");
    wait_for_stable_emission(tp.pipeline, &est.emitted, 10, 100);
    guint64 flushed_emissions = est.emitted - before_flush;
    if (flushed_emissions != 4)
      FAIL("cycle %d phase B expected 4 emissions (2 GOPs), got %llu", cycle + 1,
           (unsigned long long) flushed_emissions);
    g_print("T015: Cycle %d Phase B ✓ - 4 buffers flushed\n", cycle + 1);

    /* === Phase C: Pass-through (push 1 GOP, expect immediate 2 emissions) === */
    g_print("T015: Cycle %d Phase C - Pass-through 1 GOP...\n", cycle + 1);
    guint64 before_passthrough = est.emitted;
    if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL))
      FAIL("passthrough gop failed");
    wait_for_stable_emission(tp.pipeline, &est.emitted, 10, 50);
    guint64 passthrough_emissions = est.emitted - before_passthrough;
    if (passthrough_emissions != 2)
      FAIL("cycle %d phase C expected 2 emissions (pass-through), got %llu", cycle + 1,
           (unsigned long long) passthrough_emissions);
    g_print("T015: Cycle %d Phase C ✓ - 2 buffers emitted (pass-through)\n", cycle + 1);

    /* === Phase D: Re-arm === */
    g_print("T015: Cycle %d Phase D - Re-arming...\n", cycle + 1);
    if (!send_rearm_event(tp.pr))
      FAIL("rearm failed");
    g_print("T015: Cycle %d Phase D ✓ - Re-armed\n", cycle + 1);
  }

  /* === Final: Buffer 1 GOP and flush === */
  g_print("T015: Final - Buffering 1 GOP and flushing...\n");
  guint64 before_final_buffer = est.emitted;
  if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL))
    FAIL("final buffer push failed");
  wait_for_stable_emission(tp.pipeline, &est.emitted, 10, 50);
  if (est.emitted != before_final_buffer)
    FAIL("final buffering emitted %llu buffers (expected 0)", (unsigned long long) (est.emitted - before_final_buffer));

  guint64 before_final_flush = est.emitted;
  if (!send_flush_trigger(tp.pr, NULL))
    FAIL("final flush failed");
  wait_for_stable_emission(tp.pipeline, &est.emitted, 10, 100);
  guint64 final_flush_emissions = est.emitted - before_final_flush;
  if (final_flush_emissions != 2)
    FAIL("final flush expected 2 emissions, got %llu", (unsigned long long) final_flush_emissions);
  g_print("T015: Final ✓ - 2 buffers flushed\n");

  /* Validate timestamp continuity */
  if (!est.pts_monotonic)
    FAIL("PTS discontinuity detected");
  if (est.emitted == 0)
    FAIL("no buffers emitted overall");

  /* Expected total: 3 cycles × (4 flush + 2 pass-through) + 2 final = 20 buffers */
  guint64 expected_total = (3 * (4 + 2)) + 2;
  if (est.emitted != expected_total) {
    fprintf(stderr, "T015 INFO: total emissions=%llu (expected %llu)\n", (unsigned long long) est.emitted,
            (unsigned long long) expected_total);
  }

  g_print("T015 PASS: multi-cycle rearm successful (emitted=%" G_GUINT64_FORMAT ", PTS monotonic)\n", est.emitted);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
