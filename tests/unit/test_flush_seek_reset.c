/* T034a: FLUSH_START / FLUSH_STOP handling validation
 *
 * Validates FR-006 requirements for standard flush events:
 * 1. FLUSH_START clears buffered GOPs and resets internal state
 * 2. FLUSH_START / FLUSH_STOP are forwarded downstream
 * 3. Buffers pushed between FLUSH_START and FLUSH_STOP are rejected (GST_FLOW_FLUSHING)
 * 4. Post-seek buffers (after FLUSH_STOP + new SEGMENT) do not mix with
 *    pre-seek buffers when flushing
 *
 * Expected initial behavior: Test MUST FAIL until T034a implementation complete.
 */

#include <gst/gst.h>
#define FAIL_PREFIX "T034a FAIL: "
#include <test_utils.h>

static gboolean flush_start_seen = FALSE;
static gboolean flush_stop_seen  = FALSE;

static GstPadProbeReturn downstream_event_probe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if (GST_PAD_PROBE_INFO_TYPE(info) & (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH)) {
    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
    if (event) {
      switch (GST_EVENT_TYPE(event)) {
      case GST_EVENT_FLUSH_START:
        flush_start_seen = TRUE;
        g_print("T034a: FLUSH_START forwarded downstream\n");
        break;
      case GST_EVENT_FLUSH_STOP:
        flush_stop_seen = TRUE;
        g_print("T034a: FLUSH_STOP forwarded downstream\n");
        break;
      default:
        break;
      }
    }
  }
  return GST_PAD_PROBE_OK;
}

int main(int argc, char* argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    FAIL("Could not locate plugin factory");
  }

  PrerecTestPipeline p;
  if (!prerec_pipeline_create(&p, "flush_seek")) {
    FAIL("Failed to create test pipeline");
  }

  GstPad* sink = gst_element_get_static_pad(p.pr, "sink");
  GstPad* src  = gst_element_get_static_pad(p.pr, "src");
  if (!sink || !src) {
    if (sink)
      gst_object_unref(sink);
    if (src)
      gst_object_unref(src);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to get pads");
  }

  gst_pad_add_probe(src, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH, downstream_event_probe,
                    NULL, NULL);

  guint64 flushed_buffers = 0;
  gulong  buffer_probe    = prerec_attach_count_probe(p.pr, &flushed_buffers);

  guint64 pts = 0;
  if (!prerec_push_gop(p.appsrc, 2, &pts, GST_SECOND, NULL)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push initial GOP");
  }

  if (!prerec_wait_for_stats(p.pr, 1, 0, 1000)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Initial GOP did not queue");
  }

  GstQuery* q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(p.pr, q)) {
    gst_query_unref(q);
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to query initial stats");
  }

  const GstStructure* s           = gst_query_get_structure(q);
  guint               queued_gops = 0, queued_buffers = 0;
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_structure_get_uint(s, "queued-buffers", &queued_buffers);
  gst_query_unref(q);

  g_print("T034a: Initial queue -> gops=%u buffers=%u\n", queued_gops, queued_buffers);
  if (queued_gops == 0 || queued_buffers == 0) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Queue did not accumulate initial GOP");
  }

  GstEvent* flush_start = gst_event_new_flush_start();
  if (!gst_pad_send_event(sink, flush_start)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("FLUSH_START not accepted by sink pad");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(p.pr, q)) {
    gst_query_unref(q);
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to query stats after FLUSH_START");
  }

  s = gst_query_get_structure(q);
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_structure_get_uint(s, "queued-buffers", &queued_buffers);
  gst_query_unref(q);

  if (queued_gops != 0 || queued_buffers != 0) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("FLUSH_START did not clear queue (gops=%u buffers=%u)", queued_gops, queued_buffers);
  }

  if (!flush_start_seen) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("FLUSH_START was not forwarded downstream");
  }

  GstEvent* flush_stop = gst_event_new_flush_stop(TRUE);
  if (!gst_pad_send_event(sink, flush_stop)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("FLUSH_STOP not accepted by sink pad");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  if (!flush_stop_seen) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("FLUSH_STOP was not forwarded downstream");
  }

  /* TEST: Verify buffers pushed during FLUSH state are rejected */
  g_print("T034a: Testing buffer push during FLUSH state...\n");

  /* Reset flags for next flush cycle */
  flush_start_seen = FALSE;
  flush_stop_seen  = FALSE;

  /* Send another FLUSH_START */
  flush_start = gst_event_new_flush_start();
  if (!gst_pad_send_event(sink, flush_start)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Second FLUSH_START not accepted");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  if (!flush_start_seen) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Second FLUSH_START was not forwarded downstream");
  }

  /* NOW: Try to push a GOP while in FLUSHING state - should be rejected */
  g_print("T034a: Pushing GOP during FLUSHING state (should be rejected)...\n");
  GstFlowReturn push_result = GST_FLOW_OK;

  /* Push a keyframe buffer directly to test chain function behavior */
  GstBuffer* test_buf           = gst_buffer_new_allocate(NULL, 1024, NULL);
  GST_BUFFER_PTS(test_buf)      = pts;
  GST_BUFFER_DTS(test_buf)      = pts;
  GST_BUFFER_DURATION(test_buf) = GST_SECOND;
  /* Mark as keyframe (no DELTA_UNIT flag) */
  GST_BUFFER_FLAG_UNSET(test_buf, GST_BUFFER_FLAG_DELTA_UNIT);
  pts += GST_SECOND;

  /* Push buffer to sink pad - should return FLUSHING */
  push_result = gst_pad_chain(sink, test_buf);

  if (push_result != GST_FLOW_FLUSHING) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Buffer push during FLUSH returned %s instead of FLUSHING", gst_flow_get_name(push_result));
  }

  g_print("T034a: Buffer correctly rejected with GST_FLOW_FLUSHING\n");

  /* Verify queue is still empty (buffer was not queued) */
  q = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
  if (!gst_element_query(p.pr, q)) {
    gst_query_unref(q);
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to query stats during FLUSH");
  }

  s = gst_query_get_structure(q);
  gst_structure_get_uint(s, "queued-gops", &queued_gops);
  gst_structure_get_uint(s, "queued-buffers", &queued_buffers);
  gst_query_unref(q);

  if (queued_gops != 0 || queued_buffers != 0) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Buffer was queued during FLUSH state (gops=%u buffers=%u)", queued_gops, queued_buffers);
  }

  g_print("T034a: Queue correctly remained empty during FLUSH\n");

  /* Send FLUSH_STOP to exit flushing state */
  flush_stop = gst_event_new_flush_stop(TRUE);
  if (!gst_pad_send_event(sink, flush_stop)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Second FLUSH_STOP not accepted");
  }

  g_usleep(50000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  if (!flush_stop_seen) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Second FLUSH_STOP was not forwarded downstream");
  }

  GstSegment segment;
  gst_segment_init(&segment, GST_FORMAT_TIME);
  segment.start           = 0;
  segment.time            = 0;
  segment.position        = 0;
  GstEvent* segment_event = gst_event_new_segment(&segment);
  if (!gst_pad_send_event(sink, segment_event)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to send new SEGMENT event");
  }

  if (!prerec_push_gop(p.appsrc, 2, &pts, GST_SECOND, NULL)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Failed to push post-seek GOP");
  }

  if (!prerec_wait_for_stats(p.pr, 1, 0, 1000)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Post-seek GOP did not queue");
  }

  flushed_buffers              = 0;
  GstStructure* trigger_struct = gst_structure_new_empty("prerecord-flush");
  GstEvent*     flush_trigger  = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, trigger_struct);
  if (!gst_pad_send_event(sink, flush_trigger)) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Flush trigger event rejected");
  }

  g_usleep(100000);
  while (g_main_context_iteration(NULL, FALSE))
    ;

  if (flushed_buffers != 3) {
    gst_object_unref(src);
    gst_object_unref(sink);
    prerec_pipeline_shutdown(&p);
    FAIL("Flush emitted unexpected buffer count (%" G_GUINT64_FORMAT ")", flushed_buffers);
  }

  prerec_remove_probe(p.pr, buffer_probe);
  gst_object_unref(src);
  gst_object_unref(sink);
  prerec_pipeline_shutdown(&p);

  g_print("T034a PASS\n");
  return 0;
}
