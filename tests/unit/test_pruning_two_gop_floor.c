#include "test_utils.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T011: Enforce 2-GOP floor during pruning
 * Intent: After pruning due to max-time overflow, at least two full GOPs
 * must remain buffered (until explicit flush). Current implementation does
 * NOT guarantee this, so we force a failure (TDD red phase).
 */

static int fail(const char *msg) {
  fprintf(stderr, "T011 FAIL (expected while unimplemented): %s\n", msg);
  return 1;
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t011-pipeline")) return fail("pipeline creation failed");

  /* Push several GOPs until prune expected. Implementation currently does not
   * enforce 2-GOP floor: real assertion will be added after pruning logic.
   */
  const guint64 delta = 3 * GST_SECOND; // coarse spacing
  guint64 ts = 0;
  for (int gop = 0; gop < 4; ++gop) {
    if (!prerec_push_gop(tp.appsrc, 3, &ts, delta, NULL))
      return fail("push gop failed");
  }

  g_error("T011 forced fail: 2-GOP floor invariant not yet enforced");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
