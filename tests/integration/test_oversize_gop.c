/* T016: Integration - oversize GOP retention
 * Scenario: A very long GOP (keyframe + many deltas) should not be partially
 * pruned mid-GOP; pruning should drop whole GOPs only. Forced fail until pruning logic solid.
 */

#define FAIL_PREFIX "T016 FAIL (expected): "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t016-oversize-gop"))
    FAIL("pipeline create failed");

  guint64       pts = 0;
  const guint64 dur = 500 * GST_MSECOND;
  /* Single large GOP: key + 15 deltas */
  if (!prerec_push_gop(tp.appsrc, 15, &pts, dur, NULL))
    FAIL("oversize gop push failed");

  /* Additional GOP to trigger pruning area */
  if (!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL))
    FAIL("second gop push failed");

  g_error("T016 forced fail: oversize GOP retention logic not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
