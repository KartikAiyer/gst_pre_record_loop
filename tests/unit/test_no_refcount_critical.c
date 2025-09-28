/* Uses shared test utilities for consistent plugin discovery and pipeline
 * setup. */
#include <test_utils.h>
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Regression test: ensure no gst_mini_object_unref refcount assertion appears
 * while constructing, exercising minimal preroll, flushing, and tearing down a
 * pipeline using prerecordloop. If a CRITICAL with that signature appears, fail.
 */

static gboolean saw_critical = FALSE;

static void log_func (GstDebugCategory *category, GstDebugLevel level,
                      const gchar *file, const gchar *function, gint line,
                      GObject *object, GstDebugMessage *message, gpointer user_data)
{
  (void)category; (void)file; (void)function; (void)line; (void)object; (void)user_data;
  if (level == GST_LEVEL_ERROR || level == GST_LEVEL_WARNING) {
    const gchar *msg = gst_debug_message_get (message);
    if (msg && strstr(msg, "gst_mini_object_unref: assertion 'GST_MINI_OBJECT_REFCOUNT_VALUE (mini_object) > 0' failed")) {
      saw_critical = TRUE;
    }
  }
}

int main (int argc, char **argv) {
  if (!g_getenv("GST_DEBUG"))
    g_setenv("GST_DEBUG", "prerec_dataflow:3", TRUE);

  prerec_test_init(&argc, &argv);
  gst_debug_add_log_function(log_func, NULL, NULL);

  if (!prerec_factory_available()) {
    fprintf(stderr, "FAIL: prerecordloop factory not available\n");
    return 1;
  }

  PrerecTestPipeline tp;
  if (!prerec_pipeline_create(&tp, "no-critical")) {
    fprintf(stderr, "FAIL: prerec_pipeline_create failed\n");
    return 1;
  }

  /* Let pipeline cycle briefly */
  GstBus *bus = gst_element_get_bus(tp.pipeline);
  for (int i=0; i<20; ++i) {
    (void)gst_bus_timed_pop_filtered(bus, 5 * GST_MSECOND, GST_MESSAGE_ANY);
  }
  gst_object_unref(bus);

  prerec_pipeline_shutdown(&tp);

  if (saw_critical) {
    fprintf(stderr, "FAIL: observed gst_mini_object_unref refcount CRITICAL\n");
    return 1;
  }
  printf("PASS: no gst_mini_object_unref refcount CRITICAL observed\n");
  return 0;
}
