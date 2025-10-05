#include <test_utils.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>

/* T013: Flush-on-EOS behavior
 * Intent: Test enum flush-on-eos property with AUTO/ALWAYS/NEVER values
 * Verify that property can be set/get correctly and enum values work.
 */

static int fail(const char *msg) {
  g_critical("T013 FAIL: %s", msg);
  return 1;
}

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) return fail("factory not available");

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "t013-pipeline")) return fail("pipeline creation failed");

  /* Test enum property set/get using integer values */
  gint flush_policy;
  
  /* Test default value (AUTO = 0) */
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 0)
    return fail("default flush-on-eos should be AUTO (0)");
  
  /* Test setting to ALWAYS (1) */
  g_object_set(tp.pr, "flush-on-eos", 1, NULL);
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 1)
    return fail("flush-on-eos should be ALWAYS (1) after setting");
    
  /* Test setting to NEVER (2) */  
  g_object_set(tp.pr, "flush-on-eos", 2, NULL);
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 2)
    return fail("flush-on-eos should be NEVER (2) after setting");

  /* Test setting back to AUTO (0) */
  g_object_set(tp.pr, "flush-on-eos", 0, NULL);
  g_object_get(tp.pr, "flush-on-eos", &flush_policy, NULL);
  if (flush_policy != 0)
    return fail("flush-on-eos should be AUTO (0) after setting");

  printf("T013 PASS: flush-on-eos enum property works correctly\n");
  
  prerec_pipeline_shutdown(&tp);
  return 0;
}
