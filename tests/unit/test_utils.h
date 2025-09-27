#ifndef PREREC_TEST_UTILS_H
#define PREREC_TEST_UTILS_H

#include <gst/gst.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes GStreamer once per process. Safe to call multiple times. */
void prerec_test_init(int *argc, char ***argv);

/* Returns TRUE if the prerecord loop factory can be found (any known name). */
bool prerec_factory_available(void);

/* Attempts to create the prerecord loop element. Returns new ref or NULL. */
GstElement *prerec_create_element(void);

/* Helper to assemble a simple pipeline string and parse it. (No run) */
GstElement *prerec_build_pipeline(const char *launch, GError **err);


/* (Optional) Attach a probe to element src pad to count buffers emitted. */
gulong prerec_attach_count_probe(GstElement *el, guint64 *counter_out);
void prerec_remove_probe(GstElement *el, gulong id);

#ifdef __cplusplus
}
#endif

#endif /* PREREC_TEST_UTILS_H */
