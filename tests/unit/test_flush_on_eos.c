#include "test_utils.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T013: Flush-on-EOS behavior
 * Intent: When EOS received while buffering, element should (optionally)
 * flush queued buffers downstream (depending on future flush_on_eos setting)
 * then forward EOS. Currently functionality not implemented; force failing.
 */

static int fail(const char *msg) {
  fprintf(stderr, "T013 FAIL (expected while unimplemented): %s\n", msg);
  return 1;
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t013-pipeline")) return fail("pipeline creation failed");

  /* Push a small GOP (1 keyframe + 2 deltas) */
  const guint64 delta = GST_SECOND;
  guint64 ts = 0;
  if (!prerec_push_gop(tp.appsrc, 2, &ts, delta, NULL))
    return fail("push gop failed");

  /* Send EOS upstream */
  gst_app_src_end_of_stream(GST_APP_SRC(tp.appsrc));

  g_error("T013 forced fail: flush-on-EOS behavior not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
