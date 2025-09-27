#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <test_utils.h>

/* T009: Plugin registration test
 * Validates that the prerecord loop element factory is discoverable
 * under at least one of the expected names. (Spec references "pre_record_loop")
 */

static int fail(const char *msg) {
  fprintf(stderr, "T009 FAIL: %s\n", msg);
  return 1;
}

int main(int argc, char *argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    return fail("Could not locate plugin factory (expected one of pre_record_loop/prerecloop). Ensure GST_PLUGIN_PATH includes build dir.");
  }

  /* Basic sanity: create the element via helper */
  GstElement *el = prerec_create_element();
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
