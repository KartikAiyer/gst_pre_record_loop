#include "test_utils.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <assert.h>

/* T010: Queue pruning invariants
 * Intent: Ensure that when buffers exceed configured max-time, the element
 * will prune whole GOPs but retain at least 2 GOPs (adaptive floor).
 * Current implementation is expected NOT to enforce 2-GOP floor yet, so we
 * assert the desired behavior to produce a failing test (TDD).
 */

static int fail(const char *msg) {
  fprintf(stderr, "T010 FAIL (expected while unimplemented): %s\n", msg);
  return 1;
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t010-pipeline")) return fail("pipeline creation failed");

  /* Push synthetic GOPs (3 GOPs, each keyframe + 4 deltas) */
  guint64 ts = 0;
  const guint64 delta = 4 * GST_SECOND;
  for (int gop = 0; gop < 3; ++gop) {
    if (!prerec_push_gop(tp.appsrc, 4, &ts, delta, NULL))
      return fail("push gop failed");
  }

  /* Force failure until pruning logic + inspection arrive */
  g_error("T010 forced fail: pruning invariants not yet implemented");

  prerec_pipeline_shutdown(&tp);
  return 0;
}
