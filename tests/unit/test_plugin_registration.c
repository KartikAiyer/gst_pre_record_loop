#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* T009: Plugin registration test
 * Validates that the prerecord loop element factory is discoverable
 * under at least one of the expected names. (Spec references "pre_record_loop")
 *
 * T030: Caps negotiation verification
 * Validates that element correctly handles caps negotiation:
 * - Accepts supported formats (video/x-h264, video/x-h265)
 * - Reports correct capabilities via CAPS query
 * - Properly propagates caps from sink to src pad
 */

#define FAIL_PREFIX "T009/T030 FAIL: "
#include <test_utils.h>

/* Test helper: verify element accepts given caps string
 * Returns TRUE on success, FALSE on failure (with error message printed)
 */
static gboolean test_caps_acceptance(GstElement* el, const char* caps_str, gboolean should_accept) {
  GstPad* sink = gst_element_get_static_pad(el, "sink");
  GstPad* src = gst_element_get_static_pad(el, "src");
  gboolean result = FALSE;

  GstCaps* caps = gst_caps_from_string(caps_str);
  if (!caps) {
    g_critical("Failed to parse caps string: %s", caps_str);
    goto cleanup_pads;
  }

  /* Test via caps query to check if element supports these caps */
  GstCaps* sink_caps = gst_pad_query_caps(sink, NULL);
  gboolean sink_accepts = gst_caps_can_intersect(sink_caps, caps);

  GstCaps* src_caps = gst_pad_query_caps(src, NULL);
  gboolean src_accepts = gst_caps_can_intersect(src_caps, caps);

  if (should_accept) {
    if (!sink_accepts || !src_accepts) {
      g_critical("Element should accept caps '%s' but sink_accepts=%d src_accepts=%d", caps_str, sink_accepts,
                 src_accepts);
      gst_caps_unref(caps);
      gst_caps_unref(sink_caps);
      gst_caps_unref(src_caps);
      goto cleanup_pads;
    }
  } else {
    if (sink_accepts || src_accepts) {
      g_critical("Element should reject caps '%s' but sink_accepts=%d src_accepts=%d", caps_str, sink_accepts,
                 src_accepts);
      gst_caps_unref(caps);
      gst_caps_unref(sink_caps);
      gst_caps_unref(src_caps);
      goto cleanup_pads;
    }
  }

  result = TRUE;
  gst_caps_unref(caps);
  gst_caps_unref(sink_caps);
  gst_caps_unref(src_caps);

cleanup_pads:
  gst_object_unref(sink);
  gst_object_unref(src);
  return result;
}

int main(int argc, char* argv[]) {
  prerec_test_init(&argc, &argv);

  if (!prerec_factory_available()) {
    FAIL("Could not locate plugin factory (expected one of pre_record_loop/prerecloop). Ensure GST_PLUGIN_PATH "
         "includes build dir.");
  }

  /* Basic sanity: create the element via helper */
  GstElement* el = prerec_create_element();
  if (!el) {
    FAIL("Factory exists but element instantiation failed");
  }

  /* Check pads existence */
  GstPad* sink = gst_element_get_static_pad(el, "sink");
  GstPad* src = gst_element_get_static_pad(el, "src");
  if (!sink || !src) {
    if (sink)
      gst_object_unref(sink);
    if (src)
      gst_object_unref(src);
    gst_object_unref(el);
    FAIL("Element missing expected static pads 'sink' and 'src'");
  }

  g_print("T009: Element has both sink/src pads\n");

  /* T030: Test supported caps - video/x-h264 */
  if (!test_caps_acceptance(el, "video/x-h264,stream-format=byte-stream,alignment=au", TRUE)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(el);
    FAIL("Caps acceptance test failed for video/x-h264");
  }
  g_print("T030: Element accepts video/x-h264 caps\n");

  /* T030: Test supported caps - video/x-h265 */
  if (!test_caps_acceptance(el, "video/x-h265,stream-format=byte-stream,alignment=au", TRUE)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(el);
    FAIL("Caps acceptance test failed for video/x-h265");
  }
  g_print("T030: Element accepts video/x-h265 caps\n");

  /* T030: Test unsupported caps - raw video should be rejected */
  if (!test_caps_acceptance(el, "video/x-raw,format=I420,width=640,height=480", FALSE)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(el);
    FAIL("Caps rejection test failed for video/x-raw");
  }
  g_print("T030: Element correctly rejects video/x-raw caps\n");

  /* T030: Test unsupported caps - audio should be rejected */
  if (!test_caps_acceptance(el, "audio/x-raw,format=S16LE,rate=44100,channels=2", FALSE)) {
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(el);
    FAIL("Caps rejection test failed for audio/x-raw");
  }
  g_print("T030: Element correctly rejects audio caps\n");

  /* T030: Verify caps query returns expected caps on both pads */
  GstCaps* query_caps_sink = gst_pad_query_caps(sink, NULL);
  GstCaps* query_caps_src = gst_pad_query_caps(src, NULL);

  if (!query_caps_sink || !query_caps_src) {
    if (query_caps_sink)
      gst_caps_unref(query_caps_sink);
    if (query_caps_src)
      gst_caps_unref(query_caps_src);
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(el);
    FAIL("CAPS query failed on one or both pads");
  }

  /* Verify returned caps contain h264 and h265 */
  GstCaps* h264_caps = gst_caps_from_string("video/x-h264");
  GstCaps* h265_caps = gst_caps_from_string("video/x-h265");

  gboolean has_h264_sink = gst_caps_can_intersect(query_caps_sink, h264_caps);
  gboolean has_h265_sink = gst_caps_can_intersect(query_caps_sink, h265_caps);
  gboolean has_h264_src = gst_caps_can_intersect(query_caps_src, h264_caps);
  gboolean has_h265_src = gst_caps_can_intersect(query_caps_src, h265_caps);

  if (!has_h264_sink || !has_h265_sink || !has_h264_src || !has_h265_src) {
    gst_caps_unref(h264_caps);
    gst_caps_unref(h265_caps);
    gst_caps_unref(query_caps_sink);
    gst_caps_unref(query_caps_src);
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(el);
    FAIL("CAPS query should return h264/h265 support but got: sink(h264=%d,h265=%d) src(h264=%d,h265=%d)",
         has_h264_sink, has_h265_sink, has_h264_src, has_h265_src);
  }

  g_print("T030: CAPS query returns correct supported formats\n");

  gst_caps_unref(h264_caps);
  gst_caps_unref(h265_caps);
  gst_caps_unref(query_caps_sink);
  gst_caps_unref(query_caps_src);
  gst_object_unref(sink);
  gst_object_unref(src);
  gst_object_unref(el);

  g_print("T009 PASS\n");
  g_print("T030 PASS\n");
  return 0;
}
