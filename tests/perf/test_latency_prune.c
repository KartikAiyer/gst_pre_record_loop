#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <time.h>

/* T017: Performance benchmark skeleton - latency impact of pruning
 * For now: forced failing placeholder. Later: measure avg/99p latency between
 * push timestamp and (future) downstream emission when flushing.
 */

static int fail(const char *msg) { g_critical("T017 FAIL (expected placeholder): %s", msg); return 1; }

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");
  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t017-latency")) return fail("pipeline create failed");

  guint64 pts = 0; const guint64 dur = GST_MSECOND * 100;
  for (int i=0;i<5;++i) if(!prerec_push_gop(tp.appsrc, 2, &pts, dur, NULL)) return fail("push gop failed");

  g_error("T017 forced fail: benchmark logic not implemented");
  prerec_pipeline_shutdown(&tp);
  return 0;
}
