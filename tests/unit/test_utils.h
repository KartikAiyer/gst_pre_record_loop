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

/* Structured pipeline helper for prerecord tests */
typedef struct {
	GstElement *pipeline;
	GstElement *appsrc;
	GstElement *pr;       /* pre_record_loop element */
	GstElement *fakesink;
} PrerecTestPipeline;

/* Create standard pipeline: appsrc(name=src,is-live,format=time,caps=h264) ! pre_record_loop ! fakesink(sync=FALSE)
 * Returns TRUE on success; on failure all partially created elements are unref'd and struct zeroed.
 */
gboolean prerec_pipeline_create(PrerecTestPipeline *out, const char *name_prefix);

/* Transition pipeline to NULL and unref all members; safe on partially initialized struct */
void prerec_pipeline_shutdown(PrerecTestPipeline *p);

/* Push a synthetic GOP: one keyframe + (delta_count) delta frames.
 * pts_base_ns is starting PTS for keyframe; each frame gets duration_ns and
 * monotonically increasing PTS = previous PTS + duration.
 * Returns TRUE on success; FALSE if any push fails (remaining buffers skipped).
 * out_last_pts (optional) receives PTS of last pushed frame.
 */
gboolean prerec_push_gop(GstElement *appsrc, guint delta_count,
						 guint64 *pts_base_ns, guint64 duration_ns,
						 guint64 *out_last_pts);

/* Poll the prerecord element's custom stats query until conditions satisfied or timeout.
 * Returns TRUE if (queued_gops >= min_gops && drops_gops >= min_drops_gops) met before timeout_ms elapsed. */
gboolean prerec_wait_for_stats(GstElement *pr, guint min_gops, guint min_drops_gops, guint timeout_ms);


/* (Optional) Attach a probe to element src pad to count buffers emitted. */
gulong prerec_attach_count_probe(GstElement *el, guint64 *counter_out);
void prerec_remove_probe(GstElement *el, gulong id);

/* Macro for test failures with variadic printf-style formatting.
 * Usage: FAIL("expected %d, got %d", expected, actual);
 * Logs critical message with test ID prefix and returns 1 for test failure.
 * Note: Test files should define FAIL_PREFIX before including this header, e.g.:
 *   #define FAIL_PREFIX "T012 FAIL: "
 * If not defined, a default prefix is used.
 */
#ifndef FAIL_PREFIX
#define FAIL_PREFIX "TEST FAIL: "
#endif

#define FAIL(...) \
  do { \
    g_critical(FAIL_PREFIX __VA_ARGS__); \
    return 1; \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* PREREC_TEST_UTILS_H */
