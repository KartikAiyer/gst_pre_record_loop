#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* T009: Plugin registration test
 * Validates that the prerecord loop element factory is discoverable
 * under at least one of the expected names. (Spec references "pre_record_loop")
 */

static int fail(const char *msg) {
  fprintf(stderr, "T009 FAIL: %s\n", msg);
  return 1;
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  const char *candidates[] = {"pre_record_loop", "prerecloop", NULL};
  gboolean found = FALSE;
  for (int i = 0; candidates[i]; ++i) {
    GstElementFactory *factory = gst_element_factory_find(candidates[i]);
    if (factory) {
      g_print("T009: Found factory '%s'\n", candidates[i]);
      found = TRUE;
      gst_object_unref(factory);
      break;
    }
  }

  if (!found) {
    return fail("Could not locate plugin factory (expected one of pre_record_loop/prerecloop). Ensure GST_PLUGIN_PATH includes build dir.");
  }

  /* Basic sanity: create the element */
  GstElement *el = gst_element_factory_make("pre_record_loop", NULL);
  if (!el) {
    // fallback attempt
    el = gst_element_factory_make("prerecloop", NULL);
  }
  if (!el) {
    return fail("Factory exists but element instantiation failed");
  }

  /* Check pads existence */
  GstPad *sink = gst_element_get_static_pad(el, "sink");
  GstPad *src = gst_element_get_static_pad(el, "src");
  if (!sink || !src) {
    if (sink) gst_object_unref(sink);
    if (src) gst_object_unref(src);
    gst_object_unref(el);
    return fail("Element missing expected static pads 'sink' and 'src'");
  }

  g_print("T009: Element has both sink/src pads\n");

  gst_object_unref(sink);
  gst_object_unref(src);
  gst_object_unref(el);

  g_print("T009 PASS\n");
  return 0;
}
