#include "test_utils.h"
#include <stdio.h>

static gsize g_init_once = 0;

void prerec_test_init(int *argc, char ***argv) {
  if (g_once_init_enter(&g_init_once)) {
    gst_init(argc, argv);
    g_once_init_leave(&g_init_once, 1);
  }
}

bool prerec_factory_available(void) {
  const char *candidates[] = {"pre_record_loop", "prerecloop", NULL};
  for (int i = 0; candidates[i]; ++i) {
    GstElementFactory *f = gst_element_factory_find(candidates[i]);
    if (f) {
      gst_object_unref(f);
      return true;
    }
  }
  return false;
}

GstElement *prerec_create_element(void) {
  const char *candidates[] = {"pre_record_loop", "prerecloop", NULL};
  for (int i = 0; candidates[i]; ++i) {
    GstElement *e = gst_element_factory_make(candidates[i], NULL);
    if (e) return e;
  }
  return NULL;
}

GstElement *prerec_build_pipeline(const char *launch, GError **err) {
  return gst_parse_launch(launch, err);
}


static GstPadProbeReturn prerec_count_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
  if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) && user_data) {
    guint64 *ctr = (guint64 *)user_data;
    (*ctr)++;
  }
  return GST_PAD_PROBE_OK;
}

gulong prerec_attach_count_probe(GstElement *el, guint64 *counter_out) {
  if (!el || !counter_out) return 0;
  GstPad *srcpad = gst_element_get_static_pad(el, "src");
  if (!srcpad) return 0;
  gulong id = gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, prerec_count_probe_cb, counter_out, NULL);
  gst_object_unref(srcpad);
  return id;
}

void prerec_remove_probe(GstElement *el, gulong id) {
  if (!el || !id) return;
  GstPad *srcpad = gst_element_get_static_pad(el, "src");
  if (!srcpad) return;
  gst_pad_remove_probe(srcpad, id);
  gst_object_unref(srcpad);
}
