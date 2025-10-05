#include <gst/gst.h>
#include <stdio.h>
/* T019: max-time property test (initial failing skeleton)
 * Goal: Ensure the element exposes integer seconds property "max-time" that
 * maps to internal max_size.time (in nanoseconds) with floor at 0 and
 * sub-second values rounded/clamped per later T024 refinement.
 *
 * T024 refinement: The property is defined as g_param_spec_int, which enforces
 * integer values at the GObject property level. This means sub-second values
 * like 7.9 cannot be set (GObject will coerce to integer). The property
 * description documents this as "integer-only: sub-second precision not
 * supported (effectively floored to whole seconds)". This test validates
 * the integer-only behavior by confirming proper set/get with whole seconds
 * and negative value clamping.
 */

#define FAIL_PREFIX "T019 FAIL (expected): "
#include <test_utils.h>

int main(int argc, char **argv) {
  prerec_test_init(&argc, &argv);
  if (!prerec_factory_available()) FAIL("factory not available");

  GstElement *el = prerec_create_element();
  if (!el) FAIL("could not create element");

  GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(el), "max-time");
  if (!pspec) {
    g_error("T019 forced fail: 'max-time' property missing");
  }

  /* Read default (sanity) */
  gint v = -1;
  g_object_get(G_OBJECT(el), "max-time", &v, NULL);
  g_print("T019: default max-time = %d s\n", v);

  /* Set to 5s and verify */
  g_object_set(G_OBJECT(el), "max-time", 5, NULL);
  v = -1; g_object_get(G_OBJECT(el), "max-time", &v, NULL);
  if (v != 5) g_error("T019 FAIL: expected max-time 5, got %d", v);

  /* Set to 0 (unlimited) and verify */
  g_object_set(G_OBJECT(el), "max-time", 0, NULL);
  v = -1; g_object_get(G_OBJECT(el), "max-time", &v, NULL);
  if (v != 0) g_error("T019 FAIL: expected max-time 0, got %d", v);

  /* Set a large value and verify */
  g_object_set(G_OBJECT(el), "max-time", 3600, NULL);
  v = -1; g_object_get(G_OBJECT(el), "max-time", &v, NULL);
  if (v != 3600) g_error("T019 FAIL: expected max-time 3600, got %d", v);

  /* Negative value should clamp to 0 */
  g_object_set(G_OBJECT(el), "max-time", -7, NULL);
  v = -1; g_object_get(G_OBJECT(el), "max-time", &v, NULL);
  if (v != 0) g_error("T019 FAIL: expected max-time clamp to 0 for negative set, got %d", v);

  g_print("T019 PASS: max-time set/get behavior OK\n");
  g_object_unref(el);
  return 0;
}
