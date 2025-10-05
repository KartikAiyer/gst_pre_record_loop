#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T020a: Validate custom flush-trigger-name causes queued buffers to flush
 * Steps:
 *  1. Create pipeline and set flush-trigger-name to custom value.
 *  2. Push a small GOP while element is buffering (initial state assumed BUFFERING).
 *  3. Send a custom downstream event with matching structure name.
 *  4. Expect buffers to be emitted (count increases) and mode switches to pass-through
 *     (cannot directly read mode yet, so we infer by lack of further buffering + successful push).
 */

static int fail(const char *msg) {
  fprintf(stderr, "T020a FAIL: %s\n", msg);
  return 1;
}

static GstEvent * make_custom_trigger(const char *name) {
  GstStructure *s = gst_structure_new_empty(name);
  return gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t020a-pipeline")) return fail("pipeline creation failed");

  const char *custom_name = "my-prerec-flush";
  g_object_set(tp.pr, "flush-trigger-name", custom_name, NULL);

  guint64 emitted = 0;  /* count buffers on src */
  gulong probe_id = prerec_attach_count_probe(tp.pr, &emitted);
  if (!probe_id) return fail("failed to attach count probe");

  /* Phase 1: push initial GOP (buffering mode retains it; expect no emission yet). */
  guint64 ts = 0;
  if (!prerec_push_gop(tp.appsrc, 2, &ts, GST_SECOND, NULL)) return fail("push initial gop failed");
  guint64 baseline = emitted; /* expect 0 prior to flush trigger */

  /* Phase 2: push additional GOP to enlarge buffered set (still expect no emission) */
  if (!prerec_push_gop(tp.appsrc, 2, &ts, GST_SECOND, NULL)) return fail("push second gop failed");
  guint stabilize = 0;
  while (stabilize < 10) {
    gst_bus_timed_pop_filtered(gst_element_get_bus(tp.pipeline), 5 * GST_MSECOND, GST_MESSAGE_ANY);
    stabilize++;
  }
  if (emitted != baseline) fprintf(stderr, "T020a INFO: unexpected emission before trigger (baseline=%llu now=%llu)\n", (unsigned long long)baseline, (unsigned long long)emitted);

  /* Inject custom downstream event that should trigger flush.
   * Proper direction: originate upstream (appsrc) so it travels downstream. */
  GstEvent *ev = make_custom_trigger(custom_name);
  if (!gst_element_send_event(tp.appsrc, ev))
    return fail("failed to send custom flush event");

  /* Give a small iteration */
  gst_bus_timed_pop_filtered(gst_element_get_bus(tp.pipeline), 100 * GST_MSECOND, GST_MESSAGE_ANY);

  /* Wait briefly for flush to drain queued buffers */
  guint attempts = 0;
  while (attempts < 20 && emitted == baseline) {
    gst_bus_timed_pop_filtered(gst_element_get_bus(tp.pipeline), 10 * GST_MSECOND, GST_MESSAGE_ANY);
    attempts++;
  }
  if (emitted == baseline)
    return fail("flush trigger produced no emitted buffers");
  if (emitted - baseline < 3)
    fprintf(stderr, "T020a WARN: expected >=3 emitted buffers after flush, got %llu (baseline=%llu)\n", (unsigned long long)emitted, (unsigned long long)baseline);

  printf("T020a PASS: custom flush-trigger-name flushed %" G_GUINT64_FORMAT " buffers\n", emitted);

  prerec_remove_probe(tp.pr, probe_id);
  prerec_pipeline_shutdown(&tp);
  return 0;
}
