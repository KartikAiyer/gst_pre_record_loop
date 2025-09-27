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

  /* Build pipeline: appsrc name=src is-live=true format=time caps=video/x-h264 ! pre_record_loop name=pr ! fakesink */
  GstElement *pipeline = gst_pipeline_new("t010-pipeline");
  if (!pipeline) return fail("pipeline creation failed");

  GstElement *appsrc = gst_element_factory_make("appsrc", "src");
  GstElement *pr = prerec_create_element();
  if (!appsrc || !pr) return fail("failed to create elements");
  GstElement *fakesink = gst_element_factory_make("fakesink", NULL);
  if (!fakesink) return fail("failed to create fakesink");
  g_object_set(fakesink, "sync", FALSE, NULL);

  gst_bin_add_many(GST_BIN(pipeline), appsrc, pr, fakesink, NULL);
  if (!gst_element_link_many(appsrc, pr, fakesink, NULL)) return fail("failed to link pipeline elements");

  /* Configure appsrc caps */
  GstCaps *caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(appsrc, "caps", caps, "is-live", TRUE, "format", GST_FORMAT_TIME, NULL);
  gst_caps_unref(caps);

  if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    return fail("failed to set pipeline to PLAYING");

  /* Push synthetic GOPs */
  const guint64 delta = 4 * GST_SECOND; // large interval to exceed 10s quickly
  guint64 ts = 0;

  for (int gop = 0; gop < 3; ++gop) {
    // Keyframe buffer
    GstBuffer *kbuf = gst_buffer_new();
    GST_BUFFER_PTS(kbuf) = ts;
    GST_BUFFER_DURATION(kbuf) = delta;
    // keyframe => ensure DELTA not set
    GstFlowReturn fret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), kbuf);
    if (fret != GST_FLOW_OK) return fail("push keyframe failed");
    ts += delta;
    for (int i = 0; i < 4; ++i) {
      GstBuffer *dbuf = gst_buffer_new();
      GST_BUFFER_PTS(dbuf) = ts;
      GST_BUFFER_DURATION(dbuf) = delta;
      GST_BUFFER_FLAG_SET(dbuf, GST_BUFFER_FLAG_DELTA_UNIT);
      fret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), dbuf);
      if (fret != GST_FLOW_OK) return fail("push delta failed");
      ts += delta;
    }
  }

  /* Force failure until pruning logic + inspection arrive */
  g_error("T010 forced fail: pruning invariants not yet implemented");

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return 0;
}
