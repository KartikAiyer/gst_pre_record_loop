#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* T033: Sticky event propagation validation
 *
 * Validates that sticky events (SEGMENT, CAPS) are correctly stored and
 * made available by the element according to FR-012:
 * "Element MUST correctly propagate or store sticky events."
 *
 * Test scenarios:
 * 1. SEGMENT events forwarded and available as sticky on src pad
 * 2. CAPS events stored as sticky on src pad
 * 3. SEGMENT events are re-emitted during flush (from queue)
 * 4. SEGMENT/CAPS queryable on src pad after events
 * 5. New sticky events properly update/replace old ones after re-arm
 *
 * GStreamer sticky events remain available on pads even after being sent,
 * allowing late-joining elements to retrieve them. The element must ensure:
 * - Sticky events forwarded via gst_pad_push_event() (for downstream storage)
 * - SEGMENT queued for flush emission (timing context)
 * - No manual sticky storage/clearing (GStreamer handles lifecycle)
 * - Events queryable via gst_pad_get_sticky_event()
 * - New events replace old sticky events automatically
 *
 * Note: Sticky events may not trigger pad probes until data flows through
 * the element. This test focuses on queryability via gst_pad_get_sticky_event()
 * rather than probe detection.
 *
 * Sticky Event Lifecycle:
 * - Set when first pushed through pad (automatic GStreamer behavior)
 * - Persist across state changes and mode transitions
 * - Replaced when new event of same type is pushed
 * - Element does NOT need to clear sticky events manually
 */

#define FAIL_PREFIX "T033 FAIL: "
#include <test_utils.h>

static gint segment_events_received = 0;
static gint caps_events_received = 0;
static GstSegment last_segment;
static GstCaps* last_caps = NULL;

/* Probe on src pad to track sticky events */
static GstPadProbeReturn src_event_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);

    if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
      const GstSegment* segment;
      gst_event_parse_segment(event, &segment);
      gst_segment_copy_into(segment, &last_segment);
      segment_events_received++;
      g_print("T033: SEGMENT event on src pad (format=%d start=%" G_GUINT64_FORMAT " time=%" G_GUINT64_FORMAT ")\n",
              segment->format, segment->start, segment->time);
    } else if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      if (last_caps) {
        gst_caps_unref(last_caps);
      }
      last_caps = gst_caps_ref(caps);
      caps_events_received++;
      gchar* caps_str = gst_caps_to_string(caps);
      g_print("T033: CAPS event on src pad (caps=%s)\n", caps_str);
      g_free(caps_str);
    }
  }
  return GST_PAD_PROBE_OK;
}

int main(int argc, char* argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    FAIL("Could not locate plugin factory");
  }

  /* Initialize last_segment */
  gst_segment_init(&last_segment, GST_FORMAT_UNDEFINED);

  /* Create test pipeline */
  PrerecTestPipeline p;
  if (!prerec_pipeline_create(&p, "sticky_test")) {
    FAIL("Failed to create test pipeline");
  }

  /* Add probe to src pad to detect sticky events */
  GstPad* src = gst_element_get_static_pad(p.pr, "src");
  if (!src) {
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get src pad");
  }

  gst_pad_add_probe(src, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, src_event_probe, NULL, NULL);

  /* Set pipeline to PLAYING */
  if (gst_element_set_state(p.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to set pipeline to PLAYING");
  }

  /* Wait for pipeline to reach PLAYING state */
  GstState state;
  if (gst_element_get_state(p.pipeline, &state, NULL, GST_SECOND) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to reach PLAYING state");
  }

  g_print("T033: Test 1 - SEGMENT event propagation in BUFFERING mode\n");

  /* Get sink pad */
  GstPad* sink = gst_element_get_static_pad(p.pr, "sink");
  if (!sink) {
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get sink pad");
  }

  /* Send SEGMENT event */
  segment_events_received = 0;
  GstSegment segment;
  gst_segment_init(&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.stop = 10 * GST_SECOND;
  segment.time = 0;
  segment.position = 0;

  GstEvent* segment_event = gst_event_new_segment(&segment);
  if (!gst_pad_send_event(sink, segment_event)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send SEGMENT event");
  }

  g_usleep(100000); /* 100ms */

  /* SEGMENT should propagate immediately (sticky event) */
  if (segment_events_received != 1) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("SEGMENT event not propagated in BUFFERING mode (received=%d expected=1)", segment_events_received);
  }

  /* Verify segment values */
  if (last_segment.format != GST_FORMAT_TIME || last_segment.start != 0 || last_segment.stop != 10 * GST_SECOND) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("SEGMENT values incorrect (format=%d start=%" G_GUINT64_FORMAT " stop=%" G_GUINT64_FORMAT ")",
         last_segment.format, last_segment.start, last_segment.stop);
  }

  g_print("T033: SEGMENT event propagated correctly in BUFFERING mode ✓\n");

  /* Test 2: CAPS event stored as sticky event */
  g_print("T033: Test 2 - CAPS event stored as sticky in BUFFERING mode\n");

  /* Send CAPS event to sink pad */
  GstCaps* test_caps = gst_caps_from_string("video/x-h264,stream-format=byte-stream,alignment=au");
  GstEvent* caps_event = gst_event_new_caps(test_caps);

  if (!gst_pad_send_event(sink, caps_event)) {
    gst_caps_unref(test_caps);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send CAPS event");
  }

  g_usleep(100000); /* 100ms */

  /* CAPS should now be queryable as sticky event on src pad
   * Note: CAPS events may not trigger probes until data flows,
   * but they should be stored as sticky events immediately */
  GstEvent* sticky_caps_check = gst_pad_get_sticky_event(src, GST_EVENT_CAPS, 0);
  if (!sticky_caps_check) {
    gst_caps_unref(test_caps);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("CAPS not stored as sticky event on src pad");
  }

  /* Verify caps match what we sent */
  GstCaps* stored_caps;
  gst_event_parse_caps(sticky_caps_check, &stored_caps);
  if (!gst_caps_is_equal(stored_caps, test_caps)) {
    gchar* expected = gst_caps_to_string(test_caps);
    gchar* actual = gst_caps_to_string(stored_caps);
    gst_event_unref(sticky_caps_check);
    gst_caps_unref(test_caps);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Sticky CAPS incorrect (expected=%s actual=%s)", expected, actual);
    g_free(expected);
    g_free(actual);
  }

  gst_event_unref(sticky_caps_check);
  gst_caps_unref(test_caps);
  g_print("T033: CAPS event stored as sticky correctly ✓\n");

  /* Test 3: SEGMENT re-emission during flush */
  g_print("T033: Test 3 - SEGMENT re-emitted during flush\n");

  /* Push some buffers to queue */
  guint64 pts = 0;
  if (!prerec_push_gop(p.appsrc, 2, &pts, GST_SECOND, NULL)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push GOP");
  }

  g_usleep(100000); /* 100ms */

  /* Reset counter and trigger flush */
  segment_events_received = 0;
  GstStructure* flush_struct = gst_structure_new_empty("prerecord-flush");
  GstEvent* flush_trigger = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, flush_struct);

  if (!gst_pad_send_event(sink, flush_trigger)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send flush trigger");
  }

  g_usleep(200000); /* 200ms to allow flush */

  /* SEGMENT should be re-emitted from queue during flush
   * Note: Might be 1 or 2 depending on whether appsrc sends its own SEGMENT */
  if (segment_events_received < 1) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("SEGMENT not re-emitted during flush (received=%d expected>=1)", segment_events_received);
  }

  g_print("T033: SEGMENT re-emitted during flush (%d times) ✓\n", segment_events_received);

  /* Test 4: Sticky events queryable on src pad */
  g_print("T033: Test 4 - Sticky events queryable on src pad\n");

  /* Query sticky SEGMENT from src pad */
  GstEvent* sticky_segment = gst_pad_get_sticky_event(src, GST_EVENT_SEGMENT, 0);
  if (!sticky_segment) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("SEGMENT sticky event not available on src pad");
  }

  const GstSegment* queried_segment;
  gst_event_parse_segment(sticky_segment, &queried_segment);
  if (queried_segment->format != GST_FORMAT_TIME) {
    gst_event_unref(sticky_segment);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Queried SEGMENT has wrong format (format=%d expected=%d)", queried_segment->format, GST_FORMAT_TIME);
  }
  gst_event_unref(sticky_segment);

  /* Query sticky CAPS from src pad */
  GstEvent* sticky_caps = gst_pad_get_sticky_event(src, GST_EVENT_CAPS, 0);
  if (!sticky_caps) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("CAPS sticky event not available on src pad");
  }

  GstCaps* queried_caps;
  gst_event_parse_caps(sticky_caps, &queried_caps);
  if (!gst_caps_is_fixed(queried_caps)) {
    gst_event_unref(sticky_caps);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Queried CAPS are not fixed");
  }
  gst_event_unref(sticky_caps);

  g_print("T033: Sticky events queryable on src pad ✓\n");

  /* Test 5: New sticky events properly update after re-arm */
  g_print("T033: Test 5 - New sticky events update after re-arm\n");

  /* Now in PASS_THROUGH mode, send re-arm to return to BUFFERING */
  GstEvent* rearm = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new_empty("prerecord-arm"));
  if (!gst_pad_send_event(src, rearm)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send rearm event");
  }

  g_usleep(100000); /* 100ms */

  /* Send a NEW SEGMENT with different timestamps */
  GstSegment new_segment;
  gst_segment_init(&new_segment, GST_FORMAT_TIME);
  new_segment.start = 20 * GST_SECOND; /* Different from original (0) */
  new_segment.stop = 30 * GST_SECOND;  /* Different from original (10s) */
  new_segment.time = 20 * GST_SECOND;  /* Different from original (0) */
  new_segment.position = 20 * GST_SECOND;

  GstEvent* new_segment_event = gst_event_new_segment(&new_segment);
  if (!gst_pad_send_event(sink, new_segment_event)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send new SEGMENT event after re-arm");
  }

  g_usleep(100000); /* 100ms */

  /* Query sticky SEGMENT - should have NEW values, not old ones */
  sticky_segment = gst_pad_get_sticky_event(src, GST_EVENT_SEGMENT, 0);
  if (!sticky_segment) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("SEGMENT sticky event not available after re-arm + new event");
  }

  gst_event_parse_segment(sticky_segment, &queried_segment);
  if (queried_segment->start != 20 * GST_SECOND || queried_segment->stop != 30 * GST_SECOND ||
      queried_segment->time != 20 * GST_SECOND) {
    gst_event_unref(sticky_segment);
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Sticky SEGMENT not updated with new values (start=%" G_GUINT64_FORMAT " expected=%" G_GUINT64_FORMAT ")",
         queried_segment->start, (guint64) (20 * GST_SECOND));
  }
  gst_event_unref(sticky_segment);

  /* CAPS should still be available (not replaced) */
  sticky_caps = gst_pad_get_sticky_event(src, GST_EVENT_CAPS, 0);
  if (!sticky_caps) {
    gst_object_unref(sink);
    gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("CAPS sticky event lost after re-arm");
  }
  gst_event_unref(sticky_caps);

  g_print("T033: New sticky events properly update after re-arm ✓\n");

  /* Cleanup */
  if (last_caps) {
    gst_caps_unref(last_caps);
  }
  gst_object_unref(sink);
  gst_object_unref(src);
  prerec_pipeline_shutdown(&p);

  g_print("T033 PASS\n");
  return 0;
}
