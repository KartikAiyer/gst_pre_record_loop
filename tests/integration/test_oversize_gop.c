#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T016: Integration - oversize GOP retention
 * Scenario: A very long GOP (keyframe + many deltas) should not be partially
 * pruned mid-GOP; pruning should drop whole GOPs only. Forced fail until pruning logic solid.
 */

static int fail(const char *msg) { fprintf(stderr, "T016 FAIL (expected): %s\n", msg); return 1; }

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t016-oversize-gop")) return fail("pipeline create failed");

  guint64 pts = 0; const guint64 dur = 500 * GST_MSECOND;
  /* Single large GOP: key + 15 deltas */
  if (!prerec_push_gop(tp.appsrc, 15, &pts, dur, NULL)) return fail("oversize gop push failed");

  /* Additional GOP to trigger pruning area */
  if (!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL)) return fail("second gop push failed");

  g_error("T016 forced fail: oversize GOP retention logic not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
