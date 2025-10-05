#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T015: Integration - multi re-arm cycles (ACTIVE)
 * Scenario executed 3 cycles:
 *   For each cycle i:
 *     1. Buffer initial GOP(s)
 *     2. Flush -> expect historical emission
 *     3. Pass-through GOP(s)
 *     4. Re-arm -> back to buffering
 * After final cycle perform final flush to drain last buffered GOPs.
 * Adds timestamp continuity validation: emitted buffer PTS must be non-decreasing.
 */

static int fail(const char *msg) { fprintf(stderr, "T015 FAIL: %s\n", msg); return 1; }

static gboolean send_flush_trigger(GstElement *pr, const char *name) {
  const gchar *evname = name ? name : "prerecord-flush";
  GstStructure *s = gst_structure_new_empty(evname);
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  return gst_element_send_event(pr, ev);
}

/* Placeholder for future re-arm custom event sender */
static gboolean send_rearm_event(GstElement *pr) {
  GstStructure *s = gst_structure_new_empty("prerecord-arm");
  GstEvent *ev = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, s);
  return gst_element_send_event(pr, ev);
}

typedef struct {
  guint64 emitted;
  guint64 last_pts;
  gboolean pts_monotonic;
} EmissionStats;

static GstPadProbeReturn integration_count_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) && user_data) {
    EmissionStats *st = (EmissionStats*)user_data;
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf) {
      GstBuffer *b = buf; /* buffer is not writable here; just read PTS */
      GstClockTime pts = GST_BUFFER_PTS(b);
      if (GST_CLOCK_TIME_IS_VALID(pts)) {
        if (st->emitted > 0 && pts < st->last_pts) st->pts_monotonic = FALSE;
        st->last_pts = pts;
      }
    }
    st->emitted++;
  }
  return GST_PAD_PROBE_OK;
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t015-rearm-multicycle")) return fail("pipeline create failed");

  /* Larger max-time to ensure no pruning during cycles */
  g_object_set(tp.pr, "max-time", (guint64)(10 * GST_SECOND), NULL);

  /* Attach probe with timestamp continuity tracking */
  GstPad *srcpad = gst_element_get_static_pad(tp.pr, "src");
  if (!srcpad) return fail("no src pad");
  EmissionStats est = {0, GST_CLOCK_TIME_NONE, TRUE};
  gulong pid = gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, integration_count_probe_cb, &est, NULL);
  gst_object_unref(srcpad);
  if (!pid) return fail("probe attach failed");

  guint64 pts = 0; const guint64 dur = GST_SECOND;
  const int cycles = 3;
  guint64 emitted_before = 0;
  for (int i = 0; i < cycles; ++i) {
    /* Buffer phase: push 2 GOPs (2 + 1 delta frames per GOP) */
    for (int g = 0; g < 2; ++g) {
      if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL)) return fail("buffer phase gop push failed");
    }
    if (!send_flush_trigger(tp.pr, NULL)) return fail("flush trigger failed");
    if (est.emitted <= emitted_before) return fail("expected emission growth after flush");
    emitted_before = est.emitted;
    /* Pass-through single GOP */
    if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL)) return fail("passthrough gop failed");
    if (!send_rearm_event(tp.pr)) return fail("rearm failed");
  }
  /* Final post-arm buffering before last flush */
  if (!prerec_push_gop(tp.appsrc, 1, &pts, dur, NULL)) return fail("final buffer push failed");
  if (!send_flush_trigger(tp.pr, NULL)) return fail("final flush failed");

  /* Validate timestamp continuity */
  if (!est.pts_monotonic) return fail("PTS discontinuity detected");
  if (est.emitted == 0) return fail("no buffers emitted overall");

  fprintf(stdout, "T015 PASS: multi-cycle rearm successful (emitted=%" G_GUINT64_FORMAT ")\n", est.emitted);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
