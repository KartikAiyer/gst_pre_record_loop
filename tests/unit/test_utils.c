#include <test_utils.h>
#include <stdio.h>
#include <gst/app/gstappsrc.h>

static gsize g_init_once = 0;

void prerec_test_init(int* argc, char*** argv) {
  if (g_once_init_enter(&g_init_once)) {
    gst_init(argc, argv);
    g_once_init_leave(&g_init_once, 1);
  }
}

bool prerec_factory_available(void) {
  const char* candidates[] = {"pre_record_loop", "prerecloop", NULL};
  for (int i = 0; candidates[i]; ++i) {
    GstElementFactory* f = gst_element_factory_find(candidates[i]);
    if (f) {
      gst_object_unref(f);
      return true;
    }
  }
  return false;
}

GstElement* prerec_create_element(void) {
  const char* candidates[] = {"pre_record_loop", "prerecloop", NULL};
  for (int i = 0; candidates[i]; ++i) {
    GstElement* e = gst_element_factory_make(candidates[i], NULL);
    if (e)
      return e;
  }
  return NULL;
}

GstElement* prerec_build_pipeline(const char* launch, GError** err) {
  return gst_parse_launch(launch, err);
}

gboolean prerec_pipeline_create(PrerecTestPipeline* out, const char* name_prefix) {
  if (!out)
    return FALSE;
  memset(out, 0, sizeof(*out));
  const char* prefix = name_prefix ? name_prefix : "prerec";

  out->pipeline = gst_pipeline_new(prefix);
  out->appsrc   = gst_element_factory_make("appsrc", "src");
  out->pr       = prerec_create_element();
  out->fakesink = gst_element_factory_make("fakesink", NULL);
  if (!out->pipeline || !out->appsrc || !out->pr || !out->fakesink)
    goto fail;
  g_object_set(out->fakesink, "sync", FALSE, NULL);

  gst_bin_add_many(GST_BIN(out->pipeline), out->appsrc, out->pr, out->fakesink, NULL);
  if (!gst_element_link_many(out->appsrc, out->pr, out->fakesink, NULL))
    goto fail;

  GstCaps* caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(out->appsrc, "caps", caps, "is-live", TRUE, "format", GST_FORMAT_TIME, "stream-type",
               GST_APP_STREAM_TYPE_STREAM, "block", FALSE, NULL);
  gst_caps_unref(caps);

  if (gst_element_set_state(out->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    goto fail;
  /* Wait until actually in PLAYING (avoid racing queries/pushes). */
  GstState             cur, pend;
  GstStateChangeReturn scr = gst_element_get_state(out->pipeline, &cur, &pend, 2 * GST_SECOND);
  if (scr == GST_STATE_CHANGE_FAILURE)
    goto fail;
  /* Spin a couple of default main context iterations to allow internal tasks. */
  for (int i = 0; i < 5; ++i) {
    while (g_main_context_iteration(NULL, FALSE))
      ;
  }
  return TRUE;
fail:
  prerec_pipeline_shutdown(out);
  return FALSE;
}

void prerec_pipeline_shutdown(PrerecTestPipeline* p) {
  if (!p)
    return;
  /* Setting pipeline to NULL will also set all children to NULL. The pipeline
   * owns (has references to) its children added via gst_bin_add_many(). When we
   * unref the pipeline, it will drop its references to children which will
   * finalize them if no external refs exist. Manually unref'ing children here
   * after unref'ing the pipeline risks double-unref and segfault. Only unref
   * children individually if the pipeline creation failed before adding them.
   */
  if (p->pipeline) {
    gst_element_set_state(p->pipeline, GST_STATE_NULL);
    gst_object_unref(p->pipeline);
  } else {
    /* Partial init path: free any orphaned elements */
    if (p->appsrc)
      gst_object_unref(p->appsrc);
    if (p->pr)
      gst_object_unref(p->pr);
    if (p->fakesink)
      gst_object_unref(p->fakesink);
  }
  memset(p, 0, sizeof(*p));
}

static GstPadProbeReturn prerec_count_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) && user_data) {
    guint64* ctr = (guint64*) user_data;
    (*ctr)++;
  }
  return GST_PAD_PROBE_OK;
}

gulong prerec_attach_count_probe(GstElement* el, guint64* counter_out) {
  if (!el || !counter_out)
    return 0;
  GstPad* srcpad = gst_element_get_static_pad(el, "src");
  if (!srcpad)
    return 0;
  gulong id = gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, prerec_count_probe_cb, counter_out, NULL);
  gst_object_unref(srcpad);
  return id;
}

void prerec_remove_probe(GstElement* el, gulong id) {
  if (!el || !id)
    return;
  GstPad* srcpad = gst_element_get_static_pad(el, "src");
  if (!srcpad)
    return;
  gst_pad_remove_probe(srcpad, id);
  gst_object_unref(srcpad);
}

gboolean prerec_push_gop(GstElement* appsrc, guint delta_count, guint64* pts_base_ns, guint64 duration_ns,
                         guint64* out_last_pts) {
  if (!appsrc || !pts_base_ns)
    return FALSE;
  guint64 pts = *pts_base_ns;
  // Keyframe
  GstBuffer* k           = gst_buffer_new();
  GST_BUFFER_PTS(k)      = pts;
  GST_BUFFER_DURATION(k) = duration_ns;
  if (gst_app_src_push_buffer(GST_APP_SRC(appsrc), k) != GST_FLOW_OK) {
    return FALSE;
  }
  pts += duration_ns;
  for (guint i = 0; i < delta_count; ++i) {
    GstBuffer* d           = gst_buffer_new();
    GST_BUFFER_PTS(d)      = pts;
    GST_BUFFER_DURATION(d) = duration_ns;
    GST_BUFFER_FLAG_SET(d, GST_BUFFER_FLAG_DELTA_UNIT);
    if (gst_app_src_push_buffer(GST_APP_SRC(appsrc), d) != GST_FLOW_OK) {
      return FALSE;
    }
    pts += duration_ns;
  }
  if (out_last_pts)
    *out_last_pts = pts - duration_ns;
  *pts_base_ns = pts; // advance for caller
  return TRUE;
}

gboolean prerec_wait_for_stats(GstElement* pr, guint min_gops, guint min_drops_gops, guint timeout_ms) {
  if (!pr)
    return FALSE;
  const guint step_ms = 5;
  guint       waited  = 0;
  while (waited <= timeout_ms) {
    GstQuery* q  = gst_query_new_custom(GST_QUERY_CUSTOM, gst_structure_new_empty("prerec-stats"));
    gboolean  ok = gst_element_query(pr, q);
    if (ok) {
      const GstStructure* s  = gst_query_get_structure(q);
      guint               qg = 0, dg = 0;
      gst_structure_get_uint(s, "queued-gops", &qg);
      gst_structure_get_uint(s, "drops-gops", &dg);
      gst_query_unref(q);
      if (qg >= min_gops && dg >= min_drops_gops)
        return TRUE;
    } else {
      gst_query_unref(q);
    }
    /* Allow async tasks to progress */
    g_usleep(step_ms * 1000);
    while (g_main_context_iteration(NULL, FALSE))
      ;
    waited += step_ms;
  }
  return FALSE; /* timeout */
}
