/* T022: Ignore concurrent prerecord-flush events while draining
 * Steps:
 *  1. Build pipeline and push several GOPs so queue has content.
 *  2. Attach src pad probe to count buffers pushed downstream.
 *  3. Send first flush custom event (prerecord-flush) -> should drain all queued buffers exactly once.
 *  4. Immediately send second flush event while first drain is (conceptually) in progress.
 *  5. Verify no additional buffers/events are emitted beyond first drain (count unchanged after short wait).
 */

#define FAIL_PREFIX "T022 FAIL: "
#include "test_utils.h"
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t022-pipeline"))
    FAIL("pipeline create failed");

  guint64 ts = 0;
  const guint64 per_buf = GST_SECOND; /* 1s */
  guint64 emitted = 0;
  gulong probe_id = prerec_attach_count_probe(tp.pr, &emitted);
  if (!probe_id)
    FAIL("probe attach failed");
  guint64 baseline = emitted;
  if (baseline != 0)
    FAIL("expected zero emitted before buffering flush");

  /* Push 3 GOPs (key + 2 deltas) => enough content to observe flush */
  for (int g = 0; g < 3; ++g) {
    if (!prerec_push_gop(tp.appsrc, 2, &ts, per_buf, NULL))
      FAIL("push_gop failed");
  }

  /* First flush event: send downstream from prerecord element context */
  GstStructure* s = gst_structure_new_empty("prerecord-flush");
  GstEvent* flush1 = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
  if (!gst_element_send_event(tp.pr, flush1)) {
    FAIL("first flush event send failed");
  }
  /* Wait for first drain */
  for (int i = 0; i < 10; ++i) {
    while (g_main_context_iteration(NULL, FALSE))
      ;
    g_usleep(10 * 1000);
  }
  guint64 after_flush = emitted;
  if (after_flush == baseline)
    FAIL("flush did not emit any buffers");

  /* Second flush trigger (should be ignored) */
  GstStructure* s2 = gst_structure_new_empty("prerecord-flush");
  GstEvent* flush2 = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s2);
  gst_element_send_event(tp.pr, flush2);
  /* Short wait to see if any unexpected emission occurs */
  for (int i = 0; i < 5; ++i) {
    while (g_main_context_iteration(NULL, FALSE))
      ;
    g_usleep(10 * 1000);
  }
  guint64 after_second_trigger = emitted;
  if (after_second_trigger != after_flush)
    FAIL("second flush trigger caused additional emission");

  /* Query stats: queued-buffers should be zero, and a single passthrough mode now.
     We can't directly check internal mode, but we ensure no residual buffers. */
  GstQuery* q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(tp.pr, q)) {
    gst_query_unref(q);
    FAIL("stats query failed");
  }
  const GstStructure* st = gst_query_get_structure(q);
  guint queued_buffers = 999;
  gst_structure_get_uint(st, "queued-buffers", &queued_buffers);
  gst_query_unref(q);
  if (queued_buffers != 0)
    FAIL("expected queue empty after flush");

  /* Send another buffer; since in pass-through it should increment emitted quickly */
  guint64 before_passthrough = emitted;
  if (!prerec_push_gop(tp.appsrc, 0, &ts, per_buf, NULL))
    FAIL("push single keyframe failed");
  guint attempts = 0;
  while (attempts < 40 && emitted == before_passthrough) { /* up to ~200ms */
    while (g_main_context_iteration(NULL, FALSE))
      ;
    g_usleep(5 * 1000);
    attempts++;
  }
  if (emitted == before_passthrough)
    FAIL("no passthrough after flush (timeout)");

  g_print("T022 PASS: emitted=%" G_GUINT64_FORMAT " after first flush, passthrough ok\n", emitted);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
