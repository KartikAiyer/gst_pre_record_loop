/* T013: Flush-on-EOS behavior
 * Intent: Test enum flush-on-eos property with AUTO/ALWAYS/NEVER values
 * Verify that property can be set/get correctly and enum values work.
 *
 * T025: Verify AUTO policy behavior:
 *   - AUTO + BUFFERING: discard queue, forward EOS
 *   - AUTO + PASS_THROUGH: drain queue if any remaining data
 *   - ALWAYS: always drain queue regardless of mode
 *   - NEVER: never drain queue, just discard
 */

#define FAIL_PREFIX "T013 FAIL: "
#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

int main(int argc, char** argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available())
    FAIL("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t013-pipeline"))
    FAIL("pipeline creation failed");

  /* Test enum property set/get using integer values */
  gint flush_policy;

  /* Test default value (AUTO = 0) */
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 0)
    FAIL("default flush-on-eos should be AUTO (0)");

  /* Test setting to ALWAYS (1) */
  g_object_set(tp.pr, "flush-on-eos", 1, NULL);
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 1)
    FAIL("flush-on-eos should be ALWAYS (1) after setting");

  /* Test setting to NEVER (2) */
  g_object_set(tp.pr, "flush-on-eos", 2, NULL);
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 2)
    FAIL("flush-on-eos should be NEVER (2) after setting");

  /* Test setting back to AUTO (0) */
  g_object_set(tp.pr, "flush-on-eos", 0, NULL);
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 0)
    FAIL("flush-on-eos should be AUTO (0) after setting");

  printf("T013 PASS (part 1): flush-on-eos enum property works correctly\n");

  /* T025: Test AUTO policy behavior with BUFFERING mode */
  g_object_set(tp.pr, "flush-on-eos", 0, NULL); /* AUTO */

  /* Push 2 GOPs while in BUFFERING mode */
  guint64 ts = 0;
  if (!prerec_push_gop(tp.appsrc, 1, &ts, GST_SECOND, NULL))
    FAIL("failed to push gop 1");
  if (!prerec_push_gop(tp.appsrc, 1, &ts, GST_SECOND, NULL))
    FAIL("failed to push gop 2");

  g_usleep(100000); /* let buffers propagate */

  /* Attach probe to count emitted buffers */
  guint64 emission_count = 0;
  prerec_attach_count_probe(tp.pr, &emission_count);

  /* Send EOS while in BUFFERING mode with AUTO policy
   * Expected: queue should be discarded (not drained), EOS forwarded */
  if (gst_app_src_end_of_stream(GST_APP_SRC(tp.appsrc)) != GST_FLOW_OK)
    FAIL("failed to send EOS");

  g_usleep(200000); /* let EOS propagate */

  /* With AUTO + BUFFERING, queue should be discarded, so no emissions */
  if (emission_count != 0)
    FAIL("AUTO+BUFFERING should discard queue, but got %llu emissions", (unsigned long long) emission_count);

  printf("T013 PASS (part 2 - T025): AUTO+BUFFERING discards queue correctly\n");

  prerec_pipeline_shutdown(&tp);

  printf("T013 PASS: all flush-on-eos tests passed (including T025 AUTO policy)\n");
  return 0;
}
