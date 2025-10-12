/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2025  <kartik.aiyer@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-prerecordloop
 * @title: PreRecordLoop
 * @short_description: GOP-aware ring buffer for pre-event video capture
 *
 * The prerecordloop element continuously buffers encoded video data in a 
 * time-based ring buffer, preserving GOP (Group of Pictures) boundaries.
 * It operates in two modes: BUFFERING and PASS_THROUGH.
 *
 * ## Operating Modes
 *
 * - **BUFFERING Mode** (initial state): Incoming encoded video is queued in 
 *   a ring buffer up to #GstPreRecordLoop:max-time seconds. When the buffer 
 *   exceeds capacity, the oldest complete GOPs are dropped, always maintaining 
 *   at least 2 complete GOPs for playback continuity.
 *
 * - **PASS_THROUGH Mode**: Video flows directly through without buffering. 
 *   Entered after flush completion or when #GstPreRecordLoop:flush-on-eos 
 *   triggers on EOS.
 *
 * ## Custom Events
 *
 * The element responds to two custom GStreamer events for external control:
 *
 * **prerecord-flush** (downstream custom event):
 * Triggers flush of buffered content. When received in BUFFERING mode:
 * - All queued GOPs are drained to downstream in order
 * - Element transitions to PASS_THROUGH mode
 * - Subsequent buffers flow through without queueing
 * - Event structure name configurable via #GstPreRecordLoop:flush-trigger-name
 * - Concurrent flush requests during drain are ignored
 *
 * Example: Send flush trigger from application
 * |[<!-- language="C" -->
 * GstStructure *s = gst_structure_new_empty("prerecord-flush");
 * GstEvent *flush = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);
 * gst_element_send_event(prerecordloop, flush);
 * ]|
 *
 * **prerecord-arm** (upstream custom event):
 * Re-arms the element back to BUFFERING mode from PASS_THROUGH:
 * - Clears any buffered frames
 * - Resets GOP tracking and timing state
 * - Begins buffering incoming video again
 * - Allows reuse of element for multiple capture events
 *
 * Example: Re-arm for next recording event
 * |[<!-- language="C" -->
 * GstStructure *s = gst_structure_new_empty("prerecord-arm");
 * GstEvent *arm = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, s);
 * gst_element_send_event(prerecordloop, arm);
 * ]|
 *
 * ## GOP Boundary Handling
 *
 * The element respects GOP boundaries for all operations:
 * - Keyframes (non-delta-unit buffers) start new GOPs
 * - Pruning always removes complete GOPs, never partial frames
 * - Minimum 2-GOP retention ensures playback continuity
 * - Even if a single GOP exceeds #GstPreRecordLoop:max-time, it's retained
 *
 * ## EOS Behavior
 *
 * End-of-stream handling is controlled by #GstPreRecordLoop:flush-on-eos:
 * - AUTO (default): Flush only if in PASS_THROUGH mode
 * - ALWAYS: Always flush buffered content on EOS
 * - NEVER: Pass EOS through without flushing buffer
 *
 * ## Example Pipelines
 *
 * Basic pre-record capture:
 * |[
 * gst-launch-1.0 videotestsrc ! x264enc ! h264parse ! \
 *   prerecordloop max-time=10 ! fakesink
 * ]|
 *
 * Motion-triggered recording with custom flush event:
 * |[
 * gst-launch-1.0 v4l2src ! x264enc ! h264parse ! \
 *   prerecordloop max-time=30 flush-trigger-name=motion-detected ! \
 *   filesink location=output.h264
 * ]|
 *
 * Since: 1.0
 */

#include <gst/gstcapsfeatures.h>
#include <gst/gstclock.h>
#include <gst/gstelement.h>
#include <gst/gstevent.h>
#include <gst/gstinfo.h>
#include <gst/gstminiobject.h>
#include <gst/gstpad.h>
#include <stdlib.h>
#include <time.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

#include <gstprerecordloop/gstprerecordloop.h>

/* Instrumentation helper: log every explicit mini-object unref we perform.
 * This does NOT alter refcount semantics, just provides a breadcrumb trail
 * to correlate with GST_REFCOUNTING category output. */
#ifndef PREREC_ENABLE_LIFE_DIAG
#define PREREC_ENABLE_LIFE_DIAG 0 /* default off; enable with -DPREREC_ENABLE_LIFE_DIAG=1 */
#endif

#if PREREC_ENABLE_LIFE_DIAG
  #ifndef PREREC_UNREF
  #define PREREC_UNREF(obj, why)                                                      \
    G_STMT_START {                                                                    \
      static gatomicrefcount __seq = 0;                                               \
      if (G_LIKELY(obj)) {                                                            \
        guint s = g_atomic_int_add(&__seq, 1) + 1;                                    \
        gint __rc = GST_MINI_OBJECT_REFCOUNT_VALUE(obj);                              \
        GST_CAT_LOG(prerec_debug,                                                     \
                    "PREREC_UNREF seq=%u why=%s obj=%p type=%s ref(before)=%d",     \
                    s, (why), (obj),                                                  \
                    GST_IS_BUFFER(obj)?"buffer":(GST_IS_EVENT(obj)?"event":"other"), \
                    __rc);                                                           \
        prerec_track_unref(NULL, GST_MINI_OBJECT_CAST(obj), why);                     \
        gst_mini_object_unref(GST_MINI_OBJECT_CAST(obj));                             \
      }                                                                               \
    } G_STMT_END
  #endif
#else
  #ifndef PREREC_UNREF
  #define PREREC_UNREF(obj, why) do { if (obj) gst_mini_object_unref(GST_MINI_OBJECT_CAST(obj)); } while(0)
  #endif
#endif /* PREREC_ENABLE_LIFE_DIAG */

/* GType registration for flush-on-eos enum */
GType
gst_prerec_flush_on_eos_get_type (void)
{
  static GType flush_on_eos_type = 0;
  static const GEnumValue flush_on_eos_types[] = {
    {GST_PREREC_FLUSH_ON_EOS_AUTO, "Auto (flush only in pass-through mode)", "auto"},
    {GST_PREREC_FLUSH_ON_EOS_ALWAYS, "Always flush on EOS", "always"},
    {GST_PREREC_FLUSH_ON_EOS_NEVER, "Never flush on EOS", "never"},
    {0, NULL, NULL}
  };

  if (!flush_on_eos_type) {
    flush_on_eos_type = g_enum_register_static ("GstPreRecFlushOnEos", flush_on_eos_types);
  }
  return flush_on_eos_type;
}

GST_DEBUG_CATEGORY_STATIC(prerec_debug);
#define GST_CAT_DEFAULT prerec_debug
GST_DEBUG_CATEGORY_STATIC(prerec_dataflow);

/* T038: Optional metric logging toggle via environment variable */
static gboolean prerec_metrics_enabled = FALSE;

static inline gboolean prerec_metrics_are_enabled(void) {
  static gboolean checked = FALSE;
  if (!checked) {
    const gchar *env = g_getenv("GST_PREREC_METRICS");
    prerec_metrics_enabled = (env && (g_strcmp0(env, "1") == 0 || g_ascii_strcasecmp(env, "true") == 0));
    checked = TRUE;
  }
  return prerec_metrics_enabled;
}

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum { PROP_0, PROP_SILENT, PROP_FLUSH_ON_EOS, PROP_FLUSH_TRIGGER_NAME, PROP_MAX_TIME };

/* default property values */
#define DEFAULT_MAX_SIZE_BUFFERS 200               /* 200 buffers */
#define DEFAULT_MAX_SIZE_BYTES (300 * 1024 * 1024) /* 300 MB       */
#define DEFAULT_MAX_SIZE_TIME 10 * GST_SECOND      /* 10 seconds    */

#define GST_PREREC_MUTEX_LOCK(loop)                                            \
  G_STMT_START { g_mutex_lock(&loop->lock); }                                  \
  G_STMT_END

#define GST_PREREC_MUTEX_LOCK_CHECK_F(loop, expr, label)                       \
  G_STMT_START {                                                               \
    GST_PREREC_MUTEX_LOCK(loop);                                               \
    if (!(expr)) {                                                             \
      goto label;                                                              \
    }                                                                          \
  }

#define GST_PREREC_MUTEX_LOCK_CHECK(loop, label)                               \
  G_STMT_START {                                                               \
    GST_PREREC_MUTEX_LOCK(loop);                                               \
    if (loop->srcresult != GST_FLOW_OK)                                        \
      goto label;                                                              \
  }                                                                            \
  G_STMT_END

#define GST_PREREC_MUTEX_UNLOCK(loop)                                          \
  G_STMT_START { g_mutex_unlock(&loop->lock); }                                \
  G_STMT_END

#define GST_PREREC_WAIT_DEL_CHECK(loop, label)                                 \
  G_STMT_START {                                                               \
    loop->waiting_del = TRUE;                                                  \
    g_cond_wait(&loop->item_del, &loop->lock);                                 \
    loop->waiting_del = FALSE;                                                 \
    if (loop->srcresult != GST_FLOW_OK) {                                      \
      goto label;                                                              \
    }                                                                          \
  }                                                                            \
  G_STMT_END

#define GST_PREREC_WAIT_ADD_CHECK(loop, label)                                 \
  G_STMT_START {                                                               \
    loop->waiting_add = TRUE;                                                  \
    g_cond_wait(&loop->item_add, &loop->lock);                                 \
    loop->waiting_add = FALSE;                                                 \
    if (loop->srcresult != GST_FLOW_OK) {                                      \
      goto label;                                                              \
    }                                                                          \
  }                                                                            \
  G_STMT_END

#define GST_PREREC_SIGNAL_DEL(loop)                                            \
  G_STMT_START {                                                               \
    if (loop->waiting_del) {                                                   \
      g_cond_signal(&loop->item_del);                                          \
    }                                                                          \
  }                                                                            \
  G_STMT_END

#define GST_PREREC_SIGNAL_ADD(loop)                                            \
  G_STMT_START {                                                               \
    if (loop->waiting_add) {                                                   \
      g_cond_signal(&loop->item_add);                                          \
    }                                                                          \
  }                                                                            \
  G_STMT_END

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-h264; video/x-h265"));

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("video/x-h264; video/x-h265"));

#define gst_pre_record_loop_parent_class parent_class
G_DEFINE_TYPE(GstPreRecordLoop, gst_pre_record_loop, GST_TYPE_ELEMENT);

GST_ELEMENT_REGISTER_DEFINE(pre_record_loop, "pre_record_loop", GST_RANK_NONE,
                            GST_TYPE_PRERECORDLOOP);

static void gst_pre_record_loop_finalize(GObject *object);

static void gst_pre_record_loop_set_property(GObject *object, guint prop_id,
                                             const GValue *value,

                                             GParamSpec *pspec);
static void gst_pre_record_loop_get_property(GObject *object, guint prop_id,
                                             GValue *value, GParamSpec *pspec);

static gboolean gst_pre_record_loop_src_activate_mode(GstPad *pad,
                                                      GstObject *parent,
                                                      GstPadMode mode,
                                                      gboolean active);

static gboolean gst_pre_record_loop_sink_activate_mode(GstPad *pad,
                                                       GstObject *parent,
                                                       GstPadMode mode,
                                                       gboolean active);
static gboolean gst_pre_record_loop_sink_query(GstPad *pad, GstObject *parent,
                                               GstQuery *query);
static gboolean gst_pre_record_loop_sink_event(GstPad *pad, GstObject *parent,
                                               GstEvent *event);

static gboolean gst_pre_record_loop_src_event(GstPad *pad, GstObject *parent,
                                              GstEvent *event);

static gboolean gst_pre_record_loop_src_query(GstPad *pad, GstObject *parent,
                                              GstQuery *query);

static GstFlowReturn gst_pre_record_loop_chain(GstPad *pad, GstObject *parent,
                                               GstBuffer *buf);

/* Internal static helper forward decl */
static void gst_prerec_get_stats(GstPreRecordLoop *loop, GstPreRecStats *out_stats);

typedef struct {
  GstMiniObject *item;
  gsize size;
  gboolean is_query;
  gboolean is_keyframe;
  guint gop_id;
} GstQueueItem;

/* Tracking data structures only compiled when diagnostics enabled */
#if PREREC_ENABLE_LIFE_DIAG
  /* Sticky event tracking */
  typedef struct _PrerecStickyTrackEntry {
    gpointer event_ptr;
    guint    store_count;
  } PrerecStickyTrackEntry;
  static GHashTable *prerec_sticky_tracker = NULL; /* key=event ptr */

  /* Lifecycle tracking of pushed objects */
  typedef struct _PrerecLifeEntry {
    gpointer obj;             /* GstMiniObject* */
    gboolean is_event;
    gboolean pushed;
    gboolean unref_logged;
    guint push_seq;
    guint unref_seq;
  } PrerecLifeEntry;
  static GHashTable *prerec_life = NULL; /* key=obj */
  static gatomicrefcount prerec_push_seq = 0;
  static gatomicrefcount prerec_unref_seq = 0;
#endif /* PREREC_ENABLE_LIFE_DIAG */

static void prerec_life_init(void) {
#if PREREC_ENABLE_LIFE_DIAG
  if (G_UNLIKELY(!prerec_life))
    prerec_life = g_hash_table_new(g_direct_hash, g_direct_equal);
#endif
}

static void prerec_track_push(GstPreRecordLoop *loop, GstMiniObject *obj, gboolean is_event, const char *why) {
#if PREREC_ENABLE_LIFE_DIAG
  prerec_life_init();
  PrerecLifeEntry *e = g_hash_table_lookup(prerec_life, obj);
  if (!e) {
    e = g_new0(PrerecLifeEntry, 1);
    e->obj = obj;
    e->is_event = is_event;
    g_hash_table_insert(prerec_life, obj, e);
  }
  e->pushed = TRUE;
  e->push_seq = g_atomic_int_add(&prerec_push_seq, 1) + 1;
  GST_CAT_LOG(prerec_debug,
    "LIFE-PUSH obj=%p type=%s ref=%d why=%s seq=%u", obj, is_event?"event":"buffer",
    (int)GST_MINI_OBJECT_REFCOUNT_VALUE(obj), why, e->push_seq);
#else
  (void)loop; (void)obj; (void)is_event; (void)why;
#endif
}

static void prerec_track_unref(GstPreRecordLoop *loop, GstMiniObject *obj, const char *why) {
#if PREREC_ENABLE_LIFE_DIAG
  if (!prerec_life || !obj) return;
  PrerecLifeEntry *e = g_hash_table_lookup(prerec_life, obj);
  if (!e) return; /* Only interested if it was pushed */
  if (!e->unref_logged) {
    e->unref_logged = TRUE;
    e->unref_seq = g_atomic_int_add(&prerec_unref_seq, 1) + 1;
    GST_CAT_LOG(prerec_debug,
      "LIFE-UNREF obj=%p type=%s ref(before)=%d why=%s push_seq=%u unref_seq=%u",
      obj, e->is_event?"event":"buffer", (int)GST_MINI_OBJECT_REFCOUNT_VALUE(obj), why,
      e->push_seq, e->unref_seq);
  } else {
    GST_CAT_WARNING(prerec_debug,
      "LIFE-UNREF-DUP obj=%p type=%s ref(before)=%d why=%s push_seq=%u first_unref_seq=%u",
      obj, e->is_event?"event":"buffer", (int)GST_MINI_OBJECT_REFCOUNT_VALUE(obj), why,
      e->push_seq, e->unref_seq);
  }
#else
  (void)loop; (void)obj; (void)why;
#endif
}

static void prerec_dump_life(GstPreRecordLoop *loop, const char *phase) {
#if PREREC_ENABLE_LIFE_DIAG
  if (!prerec_life) return;
  GHashTableIter iter; gpointer key, value; guint pending = 0, total = 0;
  g_hash_table_iter_init(&iter, prerec_life);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    PrerecLifeEntry *e = value;
    if (e->pushed && !e->unref_logged) {
      pending++;
      GST_CAT_WARNING_OBJECT(prerec_debug, loop,
        "LIFE-DUMP[%s] obj=%p type=%s push_seq=%u NO-UNREF ref=%d",
        phase, e->obj, e->is_event?"event":"buffer", e->push_seq,
        (int)GST_MINI_OBJECT_REFCOUNT_VALUE(e->obj));
    }
    total++;
  }
  GST_CAT_LOG_OBJECT(prerec_debug, loop,
    "LIFE-DUMP[%s] summary total=%u pending=%u", phase, total, pending);
#else
  (void)loop; (void)phase;
#endif
}

static void prerec_sticky_tracker_init(void) {
#if PREREC_ENABLE_LIFE_DIAG
  if (G_UNLIKELY(prerec_sticky_tracker == NULL)) {
    prerec_sticky_tracker = g_hash_table_new(g_direct_hash, g_direct_equal);
  }
  #endif
}

static void prerec_track_sticky(GstPreRecordLoop *loop, GstEvent *event, const char *why) {
#if PREREC_ENABLE_LIFE_DIAG
  prerec_sticky_tracker_init();
  gpointer key = event;
  PrerecStickyTrackEntry *e = g_hash_table_lookup(prerec_sticky_tracker, key);
  if (!e) {
    e = g_new0(PrerecStickyTrackEntry, 1);
    e->event_ptr = key;
    e->store_count = 1;
    g_hash_table_insert(prerec_sticky_tracker, key, e);
    GST_CAT_LOG_OBJECT(prerec_debug, loop,
      "STICKY-TRACK add event=%p type=%s ref=%d why=%s count=%u", event,
      GST_EVENT_TYPE_NAME(event), (int)GST_MINI_OBJECT_REFCOUNT_VALUE(event), why, e->store_count);
  } else {
    e->store_count++;
    GST_CAT_WARNING_OBJECT(prerec_debug, loop,
      "STICKY-TRACK duplicate store event=%p type=%s ref=%d why=%s count=%u", event,
      GST_EVENT_TYPE_NAME(event), (int)GST_MINI_OBJECT_REFCOUNT_VALUE(event), why, e->store_count);
  }
#else
  (void)loop; (void)event; (void)why;
#endif
}

static inline void clear_level(GstPreRecSize *level) {
  level->buffers = 0;
  level->bytes = 0;
  level->time = 0;
}

/** Finalize Function */

static void gst_pre_record_loop_finalize(GObject *object) {
  GstPreRecordLoop *prerec = GST_PRERECORDLOOP(object);
  GstQueueItem *qitem;

  GST_DEBUG_OBJECT(prerec, "finalize pre rec loop");

  while ((qitem = gst_vec_deque_pop_head_struct(prerec->queue))) {
    if (qitem->item) {
      PREREC_UNREF(qitem->item, "finalize pop");
      qitem->item = NULL;
    }
  }
  prerec_dump_life(prerec, "finalize");
  gst_vec_deque_free(prerec->queue);

  g_mutex_clear(&prerec->lock);
  g_cond_clear(&prerec->item_add);
  g_cond_clear(&prerec->item_del);
  g_free(prerec->flush_trigger_name);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* GObject vmethod implementations */

static void gst_pre_record_loop_set_property(GObject *object, guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec) {
  GstPreRecordLoop *filter = GST_PRERECORDLOOP(object);

  switch (prop_id) {
  case PROP_SILENT:
    filter->silent = g_value_get_boolean(value);
    break;
  case PROP_FLUSH_ON_EOS:
    filter->flush_on_eos = (GstPreRecFlushOnEos) g_value_get_enum(value);
    break;
  case PROP_FLUSH_TRIGGER_NAME: {
    const gchar *s = g_value_get_string(value);
    g_free(filter->flush_trigger_name);
    filter->flush_trigger_name = s ? g_strdup(s) : NULL;
    break;
  }
  case PROP_MAX_TIME: {
    /* integer seconds, clamp >= 0; convert to nanoseconds */
    gint secs = g_value_get_int(value);
    if (secs < 0)
      secs = 0;
    filter->max_size.time = (guint64) secs * GST_SECOND;
    break;
  }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_pre_record_loop_get_property(GObject *object, guint prop_id,
                                             GValue *value, GParamSpec *pspec) {
  GstPreRecordLoop *filter = GST_PRERECORDLOOP(object);

  switch (prop_id) {
  case PROP_SILENT:
    g_value_set_boolean(value, filter->silent);
    break;
  case PROP_FLUSH_ON_EOS:
    g_value_set_enum(value, filter->flush_on_eos);
    break;
  case PROP_FLUSH_TRIGGER_NAME:
    g_value_set_string(value, filter->flush_trigger_name);
    break;
  case PROP_MAX_TIME:
    /* Return current max seconds (rounded down) */
    g_value_set_int(value, (gint) (filter->max_size.time / GST_SECOND));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/* GstElement vmethod implementations */

static gboolean gst_loop_is_filled(GstPreRecordLoop *loop) {
  return (loop->max_size.time > 0 &&
          loop->cur_level.time >= loop->max_size.time);
}

static gboolean gst_loop_is_empty(GstPreRecordLoop *loop) {
  return (loop->cur_level.time == 0 || loop->cur_level.buffers == 0);
}

/* Convenience function */
static inline GstClockTimeDiff segment_to_running_time(GstSegment *segment,
                                                       GstClockTime val) {
  GstClockTimeDiff res = GST_CLOCK_STIME_NONE;

  if (GST_CLOCK_TIME_IS_VALID(val)) {
    gboolean sign =
        gst_segment_to_running_time_full(segment, GST_FORMAT_TIME, val, &val);
    if (sign > 0)
      res = val;
    else if (sign < 0)
      res = -val;
  }
  return res;
}

static void update_time_level(GstPreRecordLoop *loop) {
  gint64 sink_time, src_time, sink_start_time;

  if (loop->sink_tainted) {
    GST_LOG_OBJECT(loop, "update sink time");
    loop->sinktime = segment_to_running_time(&loop->sink_segment,
                                             loop->sink_segment.position);
    loop->sink_tainted = FALSE;
  }
  sink_time = loop->sinktime;
  sink_start_time = loop->sink_start_time;

  if (loop->src_tainted) {
    GST_LOG_OBJECT(loop, "update src time");
    loop->srctime =
        segment_to_running_time(&loop->src_segment, loop->src_segment.position);
    loop->src_tainted = FALSE;
  }
  src_time = loop->srctime;

  GST_LOG_OBJECT(loop,
                 "sink %" GST_STIME_FORMAT ", src %" GST_STIME_FORMAT
                 ", sink-start-time %" GST_STIME_FORMAT,
                 GST_STIME_ARGS(sink_time), GST_STIME_ARGS(src_time),
                 GST_STIME_ARGS(sink_start_time));

  if (GST_CLOCK_STIME_IS_VALID(sink_time)) {
    if (!GST_CLOCK_STIME_IS_VALID(src_time) &&
        GST_CLOCK_STIME_IS_VALID(sink_start_time) &&
        sink_time >= sink_start_time) {
      loop->cur_level.time = sink_time - sink_start_time;
    } else if (GST_CLOCK_STIME_IS_VALID(src_time) && sink_time >= src_time) {
      loop->cur_level.time = sink_time - src_time;
    } else {
      loop->cur_level.time = 0;
    }
  } else {
    loop->cur_level.time = 0;
  }
}

/**
 * locked_apply_segment:
 * @loop: The GstPreRecordLoop instance
 * @event: The GstEvent containing segment information
 * @segment: The GstSegment to update with values from the event
 * @is_sink: TRUE if this is for the sink pad, FALSE for src pad
 *
 * Takes a SEGMENT event and applies the values to the provided segment.
 * If the segment format is not GST_FORMAT_TIME, it will be converted to
 * GST_FORMAT_TIME with default values, as time-based tracking is required
 * for internal buffer management.
 */
static void locked_apply_segment(GstPreRecordLoop *loop, GstEvent *event,
                                 GstSegment *segment, gboolean is_sink) {
  gst_event_copy_segment(event, segment);

  /* now configure the values, we use these to track timestamps on the
   * sinkpad. */
  if (segment->format != GST_FORMAT_TIME) {
    /* non-time format, pretent the current time segment is closed with a
     * 0 start and unknown stop time. */
    segment->format = GST_FORMAT_TIME;
    segment->start = 0;
    segment->stop = -1;
    segment->time = 0;
  }

  /* Will be updated on buffer flows */
  if (is_sink) {
    loop->sink_tainted = FALSE;
  } else {
    loop->src_tainted = FALSE;
  }

  GST_DEBUG_OBJECT(loop, "configured SEGMENT %" GST_SEGMENT_FORMAT, segment);
}

static void locked_apply_gap(GstPreRecordLoop *loop, GstEvent *event,
                             GstSegment *segment, gboolean is_sink) {
  GstClockTime timestamp;
  GstClockTime duration;

  gst_event_parse_gap(event, &timestamp, &duration);

  g_return_if_fail(GST_CLOCK_TIME_IS_VALID(timestamp));

  if (is_sink && !GST_CLOCK_STIME_IS_VALID(loop->sink_start_time)) {
    loop->sink_start_time = segment_to_running_time(segment, timestamp);
    GST_DEBUG_OBJECT(loop, "Start time updated to %" GST_STIME_FORMAT,
                     GST_STIME_ARGS(loop->sink_start_time));
  }

  if (GST_CLOCK_TIME_IS_VALID(duration)) {
    timestamp += duration;
  }

  segment->position = timestamp;

  if (is_sink)
    loop->sink_tainted = TRUE;
  else
    loop->src_tainted = TRUE;

  /* calc diff with other end */
  update_time_level(loop);
}

/* Apply buffer timestamp/duration to update time level accounting */
static void locked_apply_buffer(GstPreRecordLoop *loop, GstBuffer *buffer,
                                GstSegment *segment, gboolean is_sink) {
  GstClockTime duration = GST_BUFFER_DURATION(buffer);
  GstClockTime timestamp = GST_BUFFER_DTS_OR_PTS(buffer);

  /* if no timestamp is set, assume it didn't change compared to the previous
   * buffer and simply return here */
  if (timestamp == GST_CLOCK_TIME_NONE)
    return;

  if (is_sink && !GST_CLOCK_STIME_IS_VALID(loop->sink_start_time) &&
      GST_CLOCK_TIME_IS_VALID(timestamp)) {
    loop->sink_start_time = segment_to_running_time(segment, timestamp);
    GST_DEBUG_OBJECT(loop, "Start time updated to %" GST_STIME_FORMAT,
                     GST_STIME_ARGS(loop->sink_start_time));
  }

  if (duration != GST_CLOCK_TIME_NONE) {
    timestamp += duration;
  }

  GST_LOG_OBJECT(loop, "%s position updated to %" GST_TIME_FORMAT,
                 is_sink ? "sink" : "src", GST_TIME_ARGS(timestamp));

  segment->position = timestamp;
  if (is_sink) {
    loop->sink_tainted = TRUE;
  } else {
    loop->src_tainted = TRUE;
  }

  update_time_level(loop);
}

/* Dequeue next item from queue (FR-015: avoid returning internal node pointers)
 * Returns: TRUE if item was dequeued, FALSE if queue is empty
 * out_item: filled with dequeued item data (copied by value to avoid pointer aliasing) */
static gboolean gst_prerec_locked_dequeue(GstPreRecordLoop* loop, GstQueueItem *out_item) {
  GstQueueItem*  qitem_ptr;
  GstMiniObject* item;
  gsize          buf_size;

  g_return_val_if_fail(out_item != NULL, FALSE);

  /* Ownership model (see data-model.md: Queue Ownership):
   *  - Each GstQueueItem holds exactly one owned reference to a GstMiniObject.
   *  - Buffers: no extra ref was taken at enqueue time; this is the original upstream ref.
   *             Dequeue + push transfers that single ref to downstream (gst_pad_push or gst_pad_push_event).
   *  - SEGMENT/GAP events: an extra ref was explicitly taken before enqueue (so default handler can still consume
   *                        the original). Dequeue + push consumes the queue's retained ref.
   *  - Other events: not enqueued; either observed (sticky) or forwarded immediately.
   *  - If a dequeued item is not pushed (e.g. during drop/flush), we must unref it exactly once here or in the
   *    caller (flush/drop paths call PREREC_UNREF).
   */

  qitem_ptr = gst_vec_deque_pop_head_struct(loop->queue);
  if (qitem_ptr == NULL) {
    GST_CAT_DEBUG_OBJECT(prerec_dataflow, loop, "the prerec loop is empty");
    return FALSE;
  }

  /* Copy to output parameter to avoid returning internal queue storage pointer (FR-015) */
  *out_item = *qitem_ptr;
  item = out_item->item;
  buf_size = out_item->size;

  if (item) {
    GST_CAT_LOG_OBJECT(prerec_debug, loop,
      "DEQUEUE item=%p kind=%s ref=%d gop=%u size=%zu", item,
      GST_IS_BUFFER(item)?"buffer":(GST_IS_EVENT(item)?"event":"other"),
      (int)GST_MINI_OBJECT_REFCOUNT_VALUE(item), out_item->gop_id, (size_t)out_item->size);
  }

  if (GST_IS_BUFFER(item)) {
    GstBuffer* buffer = GST_BUFFER_CAST(item);

    GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "retrieved buffer %p from prerec loop", buffer);
    loop->cur_level.buffers--;
    loop->cur_level.bytes -= buf_size;
    locked_apply_buffer(loop, buffer, &loop->src_segment, FALSE);

    if (loop->cur_level.buffers == 0) {
      loop->cur_level.time = 0;
    }
  } else if (GST_IS_EVENT(item)) {
    GstEvent* event = GST_EVENT_CAST(item);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_SEGMENT:
      if (G_LIKELY(!loop->newseg_applied_to_src)) {
        /* Let default handler own sticky storage; just track */
        prerec_track_sticky(loop, event, "dequeue-segment-observe");
        locked_apply_segment(loop, event, &loop->src_segment, FALSE);
      } else {
        loop->newseg_applied_to_src = FALSE;
      }
      break;
    case GST_EVENT_GAP:
      locked_apply_gap(loop, event, &loop->src_segment, FALSE);
      break;
    default:
      break;
    }
  } else {
    g_warning("Unexpected item %p dequeued from queue %s (refcounting problem?)", item, GST_OBJECT_NAME(loop));
    PREREC_UNREF(item, "dequeue unexpected");
    memset(out_item, 0, sizeof(GstQueueItem));
    out_item->item = NULL;
  }
  GST_PREREC_SIGNAL_DEL(loop);
  return TRUE; /* caller must clear out_item->item or unref/push ownership */
}

static void gst_prerec_locked_flush(GstPreRecordLoop* loop, gboolean full) {
  GstQueueItem* qitem;
  while ((qitem = gst_vec_deque_pop_head_struct(loop->queue))) {
    /* Flush queue item:
     *  - We never manually re-store sticky events here (handled by GStreamer core).
     *  - On FULL flush we also reset segment/timing state after the loop below.
     *  - On PARTIAL flush we preserve segment/timing and just drop queued items. */
    if (qitem->item) {
      /* This unref matches the single owned reference described in the ownership model. */
      GST_CAT_LOG_OBJECT(prerec_debug, loop, "FLUSH item=%p kind=%s ref(before)=%d full=%d", qitem->item,
                         GST_IS_BUFFER(qitem->item) ? "buffer" : (GST_IS_EVENT(qitem->item) ? "event" : "other"),
                         (int) GST_MINI_OBJECT_REFCOUNT_VALUE(qitem->item), full);
      PREREC_UNREF(qitem->item, full ? "flush full" : "flush partial");
    }
    memset(qitem, 0, sizeof(GstQueueItem));
  }
  clear_level(&loop->cur_level);
  if (full) {
    gst_segment_init(&loop->sink_segment, GST_FORMAT_TIME);
    gst_segment_init(&loop->src_segment, GST_FORMAT_TIME);
    loop->head_needs_discont = loop->tail_needs_discont = FALSE;
    loop->sinktime = loop->srctime = GST_CLOCK_STIME_NONE;
    loop->sink_start_time          = GST_CLOCK_STIME_NONE;
    loop->sink_tainted = loop->src_tainted = FALSE;
  } else {
    GST_CAT_LOG_OBJECT(prerec_debug, loop, "Partial flush: preserving segment timing state");
  }

  GST_PREREC_SIGNAL_DEL(loop);
}

static inline void gst_prerec_locked_enqueue_buffer(GstPreRecordLoop* loop, gpointer item) {
  GstQueueItem qitem;
  GstBuffer*   buffer = GST_BUFFER_CAST(item);
  gsize        bsize  = gst_buffer_get_size(buffer);

  /* Ownership: buffer enters with upstream refcount = 1 (exclusive ownership by caller).
   * We do NOT gst_buffer_ref() here; the queue assumes ownership of that single ref.
   * On subsequent push (trigger/EOS) we transfer ownership to downstream. If dropped, we unref in flush/drop paths. */

  qitem.item        = item;
  qitem.is_query    = FALSE;
  qitem.is_keyframe = !(GST_BUFFER_FLAGS(buffer) & GST_BUFFER_FLAG_DELTA_UNIT);
    if (qitem.is_keyframe) {
      loop->current_gop_id += 1; /* new GOP enters; implicit count via id diff */
    }
  qitem.gop_id = loop->current_gop_id;
  qitem.size   = bsize;
  if (gst_vec_deque_get_length(loop->queue) == 0 || loop->cur_level.buffers == 0) {
    if (!qitem.is_keyframe) {
        GST_CAT_ERROR_OBJECT(
          prerec_dataflow, loop,
          "Adding first buffer to queue but it is not a keyframe");
    }
    loop->last_gop_id = loop->current_gop_id;
  }

  /* add buffer to the statustics */
  loop->cur_level.buffers++;
  loop->cur_level.bytes += bsize;
  locked_apply_buffer(loop, buffer, &loop->sink_segment, TRUE);

  gst_vec_deque_push_tail_struct(loop->queue, &qitem);
  GST_PREREC_SIGNAL_ADD(loop);
}

static inline void gst_prerec_locked_enqueue_event(GstPreRecordLoop* loop, gpointer item) {
  GstQueueItem qitem;
  GstEvent*    event = GST_EVENT_CAST(item);

  /* Ownership: caller passed an event we have just gst_event_ref()'d for SEGMENT/GAP in sink_event handler.
   * This function only queues SEGMENT and GAP events (EOS is handled directly in sink_event handler).
   * Every enqueued event has exactly one owned reference here. */

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_SEGMENT:
    locked_apply_segment(loop, event, &loop->sink_segment, TRUE);
    /* If the queue is empty, apply sink segment on the source */
    if (gst_vec_deque_is_empty(loop->queue)) {
      GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "Apply segment on srcpad");
      locked_apply_segment(loop, event, &loop->src_segment, FALSE);
      loop->newseg_applied_to_src = TRUE;
    }
    break;
  case GST_EVENT_GAP:
    locked_apply_gap(loop, event, &loop->sink_segment, TRUE);
    break;
  default:
    GST_CAT_WARNING_OBJECT(prerec_dataflow, loop, 
      "Unexpected event type %s in enqueue_event", GST_EVENT_TYPE_NAME(event));
    break;
  }
  qitem.item        = item;
  qitem.is_query    = FALSE;
  qitem.is_keyframe = FALSE;
  qitem.size = 0;
  gst_vec_deque_push_tail_struct(loop->queue, &qitem);
  GST_PREREC_SIGNAL_ADD(loop);
}

static void drop_last_item(GstPreRecordLoop* loop) {
  GstQueueItem qitem; /* stack-allocated to avoid pointer aliasing (FR-015) */
  if (!gst_prerec_locked_dequeue(loop, &qitem))
    return;
  GstMiniObject *item = qitem.item;
  if (item) {
    if (GST_IS_EVENT(item)) {
      GstEvent *event = GST_EVENT_CAST(item);
      /* Always unref events (including SEGMENT) here so finalize doesn't */
      PREREC_UNREF(event, "drop_last_item event");
    } else if (GST_IS_BUFFER(item)) {
      PREREC_UNREF(item, "drop_last_item buffer");
    } else {
      PREREC_UNREF(item, "drop_last_item other");
    }
    qitem.item = NULL;
  }
}

static void gst_prerec_locked_drop(GstPreRecordLoop *loop) {
  gboolean done = FALSE;
  guint id_to_remove = loop->last_gop_id;
  guint events_dropped = 0;
  guint buffers_dropped = 0;
  // Lets get to the starting point
  gboolean at_first = FALSE;
  GST_CAT_INFO(prerec_debug, "Will Attempt to drop items");
  do {
    GstQueueItem *qitem = gst_vec_deque_peek_head_struct(loop->queue);
    if (qitem && qitem->item) {
      GstMiniObject *item = qitem->item;
      if (GST_IS_EVENT(item)) {
        drop_last_item(loop);
        events_dropped++;
      }
      if (GST_IS_BUFFER(item)) {
        GstBuffer *buffer = GST_BUFFER_CAST(item);
        gboolean remove = FALSE;
        if (!qitem->is_keyframe) {
          GST_CAT_ERROR_OBJECT(prerec_dataflow, loop,
                               "Expecting a key frame but its not gop_id: %d",
                               qitem->gop_id);
          remove = TRUE;
        }
        if (qitem->gop_id != loop->last_gop_id) {
          GST_CAT_ERROR_OBJECT(prerec_dataflow, loop,
                               "Looking for first, but at the end has gop %d "
                               "not the expected %d",
                               qitem->gop_id, loop->last_gop_id);
          // Removing this frame
          remove = TRUE;
        }
        if (remove) {
          drop_last_item(loop);
          buffers_dropped++;
        } else {
          at_first = TRUE;
        }
      }
    } else {
      done = TRUE;
    }
  } while (!at_first && !done);

  if (gst_loop_is_empty(loop)) {
    GST_CAT_ERROR_OBJECT(prerec_dataflow, loop,
                         "Couldn't find a starting point and queue is empty");
    return;
  }
  GST_CAT_LOG_OBJECT(
      prerec_debug, loop,
      "Dropped %d events and %d buffers trying to get to start of gop for drop",
      events_dropped, buffers_dropped);
  // Okay we have a starting point. Lets examine the top of the queue and see
  // if its a buffer within the target gop id. Remove it if it is. Remove events
  // that might also be inbetween
  do {
    GstQueueItem *qitem = gst_vec_deque_peek_head_struct(loop->queue);
    if (!qitem) {
      done = TRUE;
      break;
    }
    GstMiniObject *item = qitem->item;
    if (item) {
      if (GST_IS_EVENT(item)) {
        drop_last_item(loop);
        events_dropped++;
      } else {
        if (qitem->gop_id == loop->last_gop_id) {
          drop_last_item(loop);
          buffers_dropped++;
        } else {
          if (!qitem->is_keyframe) {
            GST_CAT_ERROR_OBJECT(
                prerec_dataflow, loop,
                "Expecting a key frame on gop ID transition, but not found");
          }
          GST_CAT_DEBUG_OBJECT(prerec_dataflow, loop, "Droppped a Gop");
          loop->last_gop_id = qitem->gop_id;
          done = TRUE;
        }
      }
    } else {
      done = TRUE;
    }
  } while (!done);
  GST_CAT_LOG_OBJECT(prerec_debug, loop, "Dropped %d events and %d buffers",
                     events_dropped, buffers_dropped);

  /* Update stats (under lock already) */
  loop->stats.drops_events += events_dropped;
  loop->stats.drops_buffers += buffers_dropped;
  loop->stats.drops_gops += 1; /* we attempted a GOP level pruning */
  /* Approximate queued GOPs: difference between current and last id */
  loop->stats.queued_buffers_cur = loop->cur_level.buffers;
  if (loop->current_gop_id >= loop->last_gop_id)
    loop->stats.queued_gops_cur = loop->current_gop_id - loop->last_gop_id + 1;
  
  /* T038: Optional metric logging for production monitoring */
  if (G_UNLIKELY(prerec_metrics_are_enabled())) {
    GST_INFO_OBJECT(loop, "[METRIC] Pruning: dropped_gop_count=1 "
                    "dropped_buffers=%d dropped_events=%d queued_gops=%u queued_buffers=%u "
                    "total_drops_gops=%u total_drops_buffers=%u",
                    buffers_dropped, events_dropped,
                    loop->stats.queued_gops_cur, loop->stats.queued_buffers_cur,
                    loop->stats.drops_gops, loop->stats.drops_buffers);
  }
}

/* Heuristic GOP count: relies on invariants that queue always begins at a
 * keyframe boundary (drop/flush logic enforces) and each enqueued keyframe
 * monotonically increments current_gop_id while last_gop_id tracks the GOP id
 * at the head of the queue. Thus number of queued GOPs = current - last + 1.
 */
static inline guint gst_prerec_queued_gops(GstPreRecordLoop *loop) {
  if (loop->cur_level.buffers == 0)
    return 0;
  if (loop->current_gop_id >= loop->last_gop_id)
    return (loop->current_gop_id - loop->last_gop_id + 1);
  /* Should not happen, but return 0 defensively */
  return 0;
}
static inline gboolean gst_prerec_should_prune(GstPreRecordLoop *loop) {
  if (!gst_loop_is_filled(loop))
    return FALSE;
  return gst_prerec_queued_gops(loop) > 2; /* enforce 2-GOP floor */
}


/* chain function
 * this function does the actual processing
 */
static GstFlowReturn gst_pre_record_loop_chain(GstPad *pad, GstObject *parent,
                                               GstBuffer *buffer) {
  GstPreRecordLoop *loop = GST_PREREC_CAST(parent);
  GstClockTime duration, timestamp;

  GST_PREREC_MUTEX_LOCK_CHECK(loop, out_flushing);
  GST_CAT_INFO_OBJECT(prerec_debug, loop, "Chain Function");

  if (loop->eos) {
    GST_CAT_INFO(prerec_debug, "Going to EOS");
    goto out_eos;
  }
  if (loop->unexpected) {
    GST_CAT_INFO(prerec_debug, "Going to EOS(Unexpected)");
    goto out_eos;
  }

  timestamp = GST_BUFFER_DTS_OR_PTS(buffer);
  duration = GST_BUFFER_DURATION(buffer);
  gboolean is_keyframe =
      !(GST_BUFFER_FLAGS(buffer) & GST_BUFFER_FLAG_DELTA_UNIT);

  GST_CAT_LOG_OBJECT(
      prerec_dataflow, loop,
      "received buffer %p of size %" G_GSIZE_FORMAT ", time %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT ", keyframe=%s",
      buffer, gst_buffer_get_size(buffer), GST_TIME_ARGS(timestamp),
      GST_TIME_ARGS(duration), is_keyframe ? "YES" : "NO");

  switch (loop->mode) {
    case GST_PREREC_MODE_PASS_THROUGH: {
      /* True pass-through: forward buffer immediately without queuing. */
      GstFlowReturn fret;
      GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "Pass-through mode - pushing buffer directly");
      GST_PREREC_MUTEX_UNLOCK(loop); /* release lock before downstream push */
      fret = gst_pad_push(loop->srcpad, buffer); /* consumes buffer ref */
      return fret;
    }

    case GST_PREREC_MODE_BUFFERING:
      GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "Buffering mode - storing buffer");
      
      // Add buffer to ring buffer
      gst_prerec_locked_enqueue_buffer(loop, buffer);
      
      // Check if buffer is full and drop old GOPs if needed while staying above 2-GOP floor
      while (gst_prerec_should_prune(loop)) {
        guint before = gst_prerec_queued_gops(loop);
        GST_CAT_LOG_OBJECT(prerec_debug, loop, "Prune loop start: queued_gops=%u", before);
        gst_prerec_locked_drop(loop);
        guint after = gst_prerec_queued_gops(loop);
        GST_CAT_LOG_OBJECT(prerec_debug, loop, "Prune loop end: queued_gops=%u", after);
        if (after <= 2) break; /* safety net */
        if (after >= before) break; /* no progress safeguard */
      }

      /* Update live stats snapshot after enqueue/prune */
      loop->stats.queued_buffers_cur = loop->cur_level.buffers;
      loop->stats.queued_gops_cur = gst_prerec_queued_gops(loop);
      
      GST_PREREC_MUTEX_UNLOCK(loop);
      return GST_FLOW_OK;
      break;

    default:
      GST_CAT_ERROR_OBJECT(prerec_debug, loop, "Unknown mode: %d", loop->mode);
      goto out_eos;
  }

out_flushing:
  GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                     "exit because task paused, reason: %s",
                     gst_flow_get_name(loop->srcresult));
  GST_PREREC_MUTEX_UNLOCK(loop);
  gst_buffer_unref(buffer);
  return loop->srcresult;

out_eos:
  GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "exit because we received EOS");
  GST_PREREC_MUTEX_UNLOCK(loop);
  gst_buffer_unref(buffer);
  return GST_FLOW_EOS;
}

/* this function handles sink events */
static gboolean gst_pre_record_loop_sink_event(GstPad *pad, GstObject *parent,
                                               GstEvent *event) {
  GstPreRecordLoop *loop;
  gboolean ret = FALSE;

  loop = GST_PRERECORDLOOP(parent);
  GST_LOG_OBJECT(loop, "Received %s event: %" GST_PTR_FORMAT,
                 GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_EOS:
    GST_PREREC_MUTEX_LOCK(loop);
    /* FR-023: AUTO policy flushes remaining buffered data only if already in PASS_THROUGH;
     * otherwise buffered data is discarded and EOS forwarded.
     * ALWAYS: always drain queue regardless of mode
     * NEVER: never drain, just discard and forward EOS */
    gboolean should_drain = (loop->flush_on_eos == GST_PREREC_FLUSH_ON_EOS_ALWAYS ||
                             (loop->flush_on_eos == GST_PREREC_FLUSH_ON_EOS_AUTO && 
                              loop->mode == GST_PREREC_MODE_PASS_THROUGH));
    
    if (should_drain) {
      GST_CAT_LOG_OBJECT(prerec_dataflow, loop, 
        "EOS: draining queue (policy=%d mode=%d)", loop->flush_on_eos, loop->mode);
      GstQueueItem qitem; /* stack-allocated (FR-015) */
      while (gst_prerec_locked_dequeue(loop, &qitem)) {
        if (qitem.item) {
          if (GST_IS_BUFFER(qitem.item)) {
            GstBuffer *buf = GST_BUFFER_CAST(qitem.item);
            GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
              "PUSH(EOS flush) buffer=%p ref=%d", buf,
              (int)GST_MINI_OBJECT_REFCOUNT_VALUE(buf));
            prerec_track_push(loop, GST_MINI_OBJECT_CAST(buf), FALSE, "eos-flush");
            gst_pad_push(loop->srcpad, buf); /* consumes ref */
          } else if (GST_IS_EVENT(qitem.item)) {
            GstEvent *ev = GST_EVENT_CAST(qitem.item);
            GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
              "PUSH(EOS flush) event=%p type=%s ref=%d", ev,
              GST_EVENT_TYPE_NAME(ev), (int)GST_MINI_OBJECT_REFCOUNT_VALUE(ev));
            prerec_track_push(loop, GST_MINI_OBJECT_CAST(ev), TRUE, "eos-flush");
            gst_pad_push_event(loop->srcpad, ev); /* consumes ref */
          } else {
            PREREC_UNREF(qitem.item, "eos flush unknown item");
          }
          qitem.item = NULL;
        }
      }
      /* Reset GOP tracking after draining queue completely */
      loop->current_gop_id = loop->last_gop_id = 0;
      /* Update stats to reflect empty queue */
      loop->stats.queued_gops_cur = 0;
      loop->stats.queued_buffers_cur = 0;
    } else if (!gst_vec_deque_is_empty(loop->queue)) {
      GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
        "EOS: discarding queue (policy=%d mode=%d)", loop->flush_on_eos, loop->mode);
      gst_prerec_locked_flush(loop, TRUE);
      /* GOP IDs already reset by gst_prerec_locked_flush with full=TRUE via segment reset,
       * but explicitly reset here for clarity */
      loop->current_gop_id = loop->last_gop_id = 0;
      /* Update stats to reflect empty queue */
      loop->stats.queued_gops_cur = 0;
      loop->stats.queued_buffers_cur = 0;
    }
    GST_PREREC_MUTEX_UNLOCK(loop);
    gst_pad_push_event(loop->srcpad, event);
    break;

  case GST_EVENT_CAPS: {
    GstCaps *caps;

    gst_event_parse_caps(event, &caps);
    /* do something with the caps */
    if (caps && gst_caps_is_fixed(caps)) {
      const GstStructure *str = gst_caps_get_structure(caps, 0);
      const gchar *media_type = gst_structure_get_name(str);
      GST_LOG_OBJECT(loop, "Media Type: %s", media_type);
      GST_INFO_OBJECT(loop, "Received caps: %" GST_PTR_FORMAT, caps);
    }
    /* Forward CAPS to src pad (FR-012: sticky event propagation) */
    ret = gst_pad_push_event(loop->srcpad, event);
    break;
  }
  
  /* T034a: FLUSH_START/FLUSH_STOP handling (FR-006) */
  case GST_EVENT_FLUSH_START: {
    GST_CAT_INFO_OBJECT(prerec_debug, loop, "Handling FLUSH_START (mode=%s)",
                        loop->mode == GST_PREREC_MODE_BUFFERING ? "BUFFERING" : "PASS_THROUGH");
    GST_PREREC_MUTEX_LOCK(loop);
    
    /* Clear queue - buffered frames become invalid after seek */
    gst_prerec_locked_flush(loop, TRUE);
    
    /* Reset GOP tracking */
    loop->current_gop_id = 0;
    loop->last_gop_id = 0;
    
    /* Reset stats counters for queue state */
    loop->stats.queued_gops_cur = 0;
    loop->stats.queued_buffers_cur = 0;
    
    /* Set srcresult to FLUSHING to stop any pending operations */
    loop->srcresult = GST_FLOW_FLUSHING;
    
    GST_PREREC_MUTEX_UNLOCK(loop);
    
    /* Forward FLUSH_START downstream */
    ret = gst_pad_push_event(loop->srcpad, event);
    GST_CAT_INFO_OBJECT(prerec_debug, loop, "Forwarded FLUSH_START downstream (ret=%d)", ret);
    break;
  }
  
  case GST_EVENT_FLUSH_STOP: {
    gboolean reset_time;
    gst_event_parse_flush_stop(event, &reset_time);
    
    GST_CAT_INFO_OBJECT(prerec_debug, loop, "Handling FLUSH_STOP (reset_time=%d mode=%s)",
                        reset_time, loop->mode == GST_PREREC_MODE_BUFFERING ? "BUFFERING" : "PASS_THROUGH");
    GST_PREREC_MUTEX_LOCK(loop);
    
    /* Reset srcresult to OK - ready to accept new data */
    loop->srcresult = GST_FLOW_OK;
    
    /* Reinitialize segments to TIME format if reset_time is TRUE */
    if (reset_time) {
      gst_segment_init(&loop->sink_segment, GST_FORMAT_TIME);
      gst_segment_init(&loop->src_segment, GST_FORMAT_TIME);
      loop->sinktime = loop->srctime = GST_CLOCK_STIME_NONE;
      loop->sink_start_time = GST_CLOCK_STIME_NONE;
      loop->sink_tainted = loop->src_tainted = FALSE;
    }
    
    /* Mode stays the same (BUFFERING or PASS_THROUGH) */
    
    GST_PREREC_MUTEX_UNLOCK(loop);
    
    /* Forward FLUSH_STOP downstream */
    ret = gst_pad_push_event(loop->srcpad, event);
    GST_CAT_INFO_OBJECT(prerec_debug, loop, "Forwarded FLUSH_STOP downstream (ret=%d, reset_time=%d)", ret, reset_time);
    break;
  }
  
  case GST_EVENT_CUSTOM_DOWNSTREAM: {
    const GstStructure *structure = gst_event_get_structure(event);
    const gchar *expected = loop->flush_trigger_name ? loop->flush_trigger_name : "prerecord-flush";
    if (structure && gst_structure_has_name(structure, expected)) {
      GST_CAT_INFO_OBJECT(prerec_debug, loop, "Received flush trigger '%s'", expected);
      GST_PREREC_MUTEX_LOCK(loop);
      if (loop->mode == GST_PREREC_MODE_BUFFERING) {
        /* Increment flush counter (T026) */
        loop->stats.flush_count++;
        GstQueueItem qitem; /* stack-allocated (FR-015) */
        while (gst_prerec_locked_dequeue(loop, &qitem)) {
          if (qitem.item) {
            if (GST_IS_BUFFER(qitem.item)) {
              GstBuffer *buf = GST_BUFFER_CAST(qitem.item);
              GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                "PUSH(trigger flush) buffer=%p ref=%d", buf,
                (int)GST_MINI_OBJECT_REFCOUNT_VALUE(buf));
              prerec_track_push(loop, GST_MINI_OBJECT_CAST(buf), FALSE, "trigger-flush");
              gst_pad_push(loop->srcpad, buf);
            } else if (GST_IS_EVENT(qitem.item)) {
              GstEvent *ev = GST_EVENT_CAST(qitem.item);
              GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                "PUSH(trigger flush) event=%p type=%s ref=%d", ev,
                GST_EVENT_TYPE_NAME(ev), (int)GST_MINI_OBJECT_REFCOUNT_VALUE(ev));
              prerec_track_push(loop, GST_MINI_OBJECT_CAST(ev), TRUE, "trigger-flush");
              gst_pad_push_event(loop->srcpad, ev);
            } else {
              PREREC_UNREF(qitem.item, "trigger flush unknown item");
            }
            qitem.item = NULL;
          }
        }
        loop->mode = GST_PREREC_MODE_PASS_THROUGH; /* marks drain complete and future triggers ignored */
        /* T027: Log state transition with stats snapshot */
        GST_CAT_INFO_OBJECT(prerec_debug, loop, 
          "STATE TRANSITION: BUFFERING → PASS_THROUGH | "
          "Stats: drops_gops=%u drops_buffers=%u drops_events=%u "
          "flush_count=%u rearm_count=%u",
          loop->stats.drops_gops, loop->stats.drops_buffers, loop->stats.drops_events,
          loop->stats.flush_count, loop->stats.rearm_count);
        
        /* T038: Optional metric logging for production monitoring */
        if (G_UNLIKELY(prerec_metrics_are_enabled())) {
          GST_INFO_OBJECT(loop, "[METRIC] Mode transition: BUFFERING -> PASS_THROUGH "
                          "flush_count=%u queued_gops=%u queued_buffers=%u",
                          loop->stats.flush_count, loop->stats.queued_gops_cur,
                          loop->stats.queued_buffers_cur);
        }
        GST_CAT_INFO_OBJECT(prerec_debug, loop, "Switched to passthrough mode after trigger");
      }
      GST_PREREC_MUTEX_UNLOCK(loop);
      gst_event_unref(event);
      ret = TRUE;
    } else {
      ret = gst_pad_event_default(pad, parent, event);
    }
    break;
  }
  default:
    GST_PREREC_MUTEX_LOCK(loop);
    if (GST_EVENT_IS_SERIALIZED(event)) {
      if (event->type == GST_EVENT_SEGMENT || event->type == GST_EVENT_GAP) {
        /* T034b: Only queue SEGMENT/GAP events in BUFFERING mode.
         * In PASS_THROUGH mode, events go directly downstream without queuing.
         * This prevents:
         *   - Duplicate emission (queued event + direct forward)
         *   - Memory waste from storing unused events
         *   - Stale events appearing after mode transitions
         * 
         * Ownership rationale (when queuing in BUFFERING mode):
         *  - We pass the original event to gst_pad_event_default() which will keep/use it (SEGMENT is sticky).
         *  - We also need a queued copy for deferred emission during flush/trigger.
         *  - Therefore we take an extra ref here; the queue later transfers ownership when pushing.
         *  - We never manually store sticky events ourselves (avoids double store/double unref).
         */
        if (loop->mode == GST_PREREC_MODE_BUFFERING) {
          gst_event_ref(event);
          gst_prerec_locked_enqueue_event(loop, event);
          GST_CAT_LOG_OBJECT(prerec_dataflow, loop, 
            "Queued %s event in BUFFERING mode", GST_EVENT_TYPE_NAME(event));
        } else {
          GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
            "Skipping queue for %s event in PASS_THROUGH mode", GST_EVENT_TYPE_NAME(event));
        }
      } else if (GST_EVENT_IS_STICKY(event)) {
        /* Observe only; default handler performs sticky storage. */
        prerec_track_sticky(loop, event, "observe-serialized-sticky");
      }
    }
    GST_PREREC_MUTEX_UNLOCK(loop);
    GST_INFO_OBJECT(parent, "%s Sending to Default Handler",
                    GST_EVENT_TYPE_NAME(event));
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  return ret;
}

static gboolean gst_pre_record_loop_src_event(GstPad *pad, GstObject *parent,
                                              GstEvent *event) {
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(parent);

#ifndef GST_DISABLE_GST_DEBUG
  GST_CAT_DEBUG_OBJECT(prerec_dataflow, loop, "got event %p (%d)", event,
                       GST_EVENT_TYPE(event));
#endif

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_RECONFIGURE: {
    GST_PREREC_MUTEX_LOCK(loop);
    if (loop->srcresult == GST_FLOW_NOT_LINKED) {
      loop->srcresult = GST_FLOW_OK; /* assume downstream relinked */
    }
    GST_PREREC_MUTEX_UNLOCK(loop);
    return gst_pad_push_event(loop->sinkpad, event);
  }
  case GST_EVENT_CUSTOM_UPSTREAM: {
    const GstStructure *st = gst_event_get_structure(event);
    if (st && gst_structure_has_name(st, "prerecord-arm")) {
      GST_PREREC_MUTEX_LOCK(loop);
      if (loop->mode == GST_PREREC_MODE_PASS_THROUGH) {
        /* Increment rearm counter (T026) */
        loop->stats.rearm_count++;
        loop->mode = GST_PREREC_MODE_BUFFERING;
        loop->current_gop_id = 0;
        loop->last_gop_id = 0;
        loop->cur_level.time = 0;
        loop->cur_level.buffers = 0;
        loop->cur_level.bytes = 0;
        gst_segment_init(&loop->sink_segment, GST_FORMAT_TIME);
        gst_segment_init(&loop->src_segment, GST_FORMAT_TIME);
        loop->sinktime = loop->srctime = GST_CLOCK_STIME_NONE;
        loop->sink_start_time = GST_CLOCK_STIME_NONE;
        loop->sink_tainted = loop->src_tainted = FALSE;
        /* T027: Log state transition with stats snapshot */
        GST_CAT_INFO_OBJECT(prerec_debug, loop, 
          "STATE TRANSITION: PASS_THROUGH → BUFFERING | "
          "Stats: drops_gops=%u drops_buffers=%u drops_events=%u "
          "flush_count=%u rearm_count=%u",
          loop->stats.drops_gops, loop->stats.drops_buffers, loop->stats.drops_events,
          loop->stats.flush_count, loop->stats.rearm_count);
        
        /* T038: Optional metric logging for production monitoring */
        if (G_UNLIKELY(prerec_metrics_are_enabled())) {
          GST_INFO_OBJECT(loop, "[METRIC] Mode transition: PASS_THROUGH -> BUFFERING "
                          "rearm_count=%u gop_baseline_reset=TRUE",
                          loop->stats.rearm_count);
        }
        GST_CAT_INFO_OBJECT(prerec_debug, loop, "Received prerecord-arm: re-entering BUFFERING mode");
      } else {
        GST_CAT_INFO_OBJECT(prerec_debug, loop, "Received prerecord-arm while already BUFFERING - ignoring");
      }
      GST_PREREC_MUTEX_UNLOCK(loop);
      gst_event_unref(event);
      return TRUE; /* consumed */
    }
    /* Not our custom upstream event: fall through to default handler */
    return gst_pad_event_default(pad, parent, event);
  }
  default:
    return gst_pad_event_default(pad, parent, event);
  }
}

static gboolean gst_pre_record_loop_src_query(GstPad *pad, GstObject *parent,
                                              GstQuery *query) {
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(parent);
  if (GST_QUERY_TYPE(query) == GST_QUERY_CUSTOM) {
    const GstStructure *in_s = gst_query_get_structure(query);
    if (in_s && gst_structure_has_name(in_s, "prerec-stats")) {
      GstPreRecStats stats;
      gst_prerec_get_stats(loop, &stats);
      /* We can't mutate the structure name in-place easily; instead build a temp
       * structure and insert its fields into the existing one (which already has
       * desired name). */
      GstStructure *w = (GstStructure*)in_s; /* cast away const for field updates */
      gst_structure_set(w,
        "drops-gops", G_TYPE_UINT, stats.drops_gops,
        "drops-buffers", G_TYPE_UINT, stats.drops_buffers,
        "drops-events", G_TYPE_UINT, stats.drops_events,
        "queued-gops", G_TYPE_UINT, stats.queued_gops_cur,
        "queued-buffers", G_TYPE_UINT, stats.queued_buffers_cur,
        "flush-count", G_TYPE_UINT, stats.flush_count,
        "rearm-count", G_TYPE_UINT, stats.rearm_count,
        NULL);
      return TRUE;
    }
  }
  return gst_pad_query_default(pad, parent, query);
}

static gboolean gst_pre_record_loop_src_activate_mode(GstPad *pad,
                                                      GstObject *parent,
                                                      GstPadMode mode,
                                                      gboolean active) {
  gboolean result = FALSE;
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(parent);

  switch (mode) {
  case GST_PAD_MODE_PUSH:
    if (active) {
      GST_CAT_INFO(prerec_debug, "Source pad activated - no task needed for passthrough");
      GST_PREREC_MUTEX_LOCK(loop);
      loop->srcresult = GST_FLOW_OK;
      loop->eos = FALSE;
      GST_PREREC_MUTEX_UNLOCK(loop);
      result = TRUE;
    } else {
      GST_CAT_INFO(prerec_debug, "Source pad deactivated");
      GST_PREREC_MUTEX_LOCK(loop);
      loop->srcresult = GST_FLOW_FLUSHING;
      gst_prerec_locked_flush(loop, FALSE);
      GST_PREREC_MUTEX_UNLOCK(loop);
      result = TRUE;
    }
    break;
  default:
    result = FALSE;
    break;
  }
  return result;
}

static gboolean gst_pre_record_loop_sink_activate_mode(GstPad *pad,
                                                       GstObject *parent,
                                                       GstPadMode mode,
                                                       gboolean active) {
  gboolean result;
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(parent);

  switch (mode) {
  case GST_PAD_MODE_PUSH:
    if (active) {
      GST_PREREC_MUTEX_LOCK(loop);
      loop->srcresult = GST_FLOW_OK;
      loop->eos = FALSE;
      GST_PREREC_MUTEX_UNLOCK(loop);
    } else {
      /* step 1, unblock the chan function */
      GST_PREREC_MUTEX_LOCK(loop);
      loop->srcresult = GST_FLOW_FLUSHING;
      /* Unblock with a signal on the del*/
      GST_PREREC_SIGNAL_DEL(loop);
      GST_PREREC_MUTEX_UNLOCK(loop);

      /* step 2, wait until streaming thread stopped and flush queue */
      GST_PAD_STREAM_LOCK(pad);
      GST_PREREC_MUTEX_LOCK(loop);
      gst_prerec_locked_flush(loop, TRUE);
      GST_PREREC_MUTEX_UNLOCK(loop);
      GST_PAD_STREAM_UNLOCK(pad);
    }
    result = TRUE;
    break;
  default:
    result = FALSE;
    break;
  }
  return result;
}

static GstStateChangeReturn
gst_pre_record_loop_change_state(GstElement *element,
                                 GstStateChange transition) {

  GstStateChangeReturn ret;
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(element);

  switch (transition) {
  case GST_STATE_CHANGE_NULL_TO_READY:
    loop->preroll_sent = FALSE;
    break;
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS(gst_pre_record_loop_parent_class)
            ->change_state(element, transition);
  return ret;
}

static gboolean gst_pre_record_loop_sink_query(GstPad *pad, GstObject *parent,
                                               GstQuery *query) {
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(parent);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE(query)) {
  case GST_QUERY_CAPS: {
    GstCaps *filter, *caps;
    
    gst_query_parse_caps(query, &filter);
    caps = gst_pad_get_pad_template_caps(pad);
    
    if (filter) {
      GstCaps *intersection = gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref(caps);
      caps = intersection;
    }
    
    gst_query_set_caps_result(query, caps);
    gst_caps_unref(caps);
    ret = TRUE;
    break;
  }
  case GST_QUERY_ACCEPT_CAPS: {
    GstCaps *caps, *template_caps;
    
    gst_query_parse_accept_caps(query, &caps);
    template_caps = gst_pad_get_pad_template_caps(pad);
    ret = gst_caps_is_subset(caps, template_caps);
    gst_caps_unref(template_caps);
    gst_query_set_accept_caps_result(query, ret);
    break;
  }
  default:
    ret = gst_pad_query_default(pad, parent, query);
    break;
  }
  
  return ret;
}

/* initialize the prerecordloop's class */
static void gst_pre_record_loop_class_init(GstPreRecordLoopClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

  gobject_class->set_property = gst_pre_record_loop_set_property;
  gobject_class->get_property = gst_pre_record_loop_get_property;

  /**
   * GstPreRecordLoop:silent:
   *
   * Enable/disable verbose output logging. When %FALSE (default), the element
   * may produce additional debug information via GST_INFO and GST_DEBUG.
   * When %TRUE, suppresses non-critical logging.
   *
   * Note: This property is legacy and may be deprecated in favor of 
   * GST_DEBUG environment variable control.
   */
  g_object_class_install_property(
      gobject_class, PROP_SILENT,
      g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
                           FALSE, G_PARAM_READWRITE));

  gobject_class->finalize = gst_pre_record_loop_finalize;

  /**
   * GstPreRecordLoop:flush-on-eos:
   *
   * Policy for handling buffered content when EOS (End-Of-Stream) is received.
   *
   * - AUTO (default): Flush buffered GOPs only if currently in PASS_THROUGH mode.
   *   In BUFFERING mode, EOS passes through without flushing.
   * - ALWAYS: Always drain buffered content to downstream before forwarding EOS,
   *   regardless of current mode.
   * - NEVER: Forward EOS immediately without flushing buffer, even in PASS_THROUGH.
   *
   * This allows fine control over pre-roll behavior at stream end, particularly
   * useful for ensuring capture of trailing video in event-driven scenarios.
   */
  g_object_class_install_property(
      gobject_class, PROP_FLUSH_ON_EOS,
      g_param_spec_enum("flush-on-eos", "Flush On EOS",
                        "Policy for flushing queued buffers when EOS is received",
                        GST_TYPE_PREREC_FLUSH_ON_EOS, GST_PREREC_FLUSH_ON_EOS_AUTO,
                        G_PARAM_READWRITE));

  /**
   * GstPreRecordLoop:flush-trigger-name:
   *
   * Customizable event structure name for the flush trigger event.
   * Default is "prerecord-flush".
   *
   * When a downstream custom event with matching structure name is received,
   * the element drains all buffered GOPs and transitions to PASS_THROUGH mode.
   * This allows integration with application-specific event sources (motion
   * detection, external triggers, etc.) without hardcoding event names.
   *
   * Example: Use custom trigger name for motion detection
   * |[<!-- language="C" -->
   * g_object_set(prerecordloop, "flush-trigger-name", "motion-detected", NULL);
   * ]|
   *
   * Set to %NULL to use default "prerecord-flush" name.
   */
  g_object_class_install_property(
      gobject_class, PROP_FLUSH_TRIGGER_NAME,
      g_param_spec_string("flush-trigger-name", "Flush Trigger Name",
                          "Custom downstream custom-event structure name that triggers flush (default: prerecord-flush)",
                          NULL, G_PARAM_READWRITE));

  /**
   * GstPreRecordLoop:max-time:
   *
   * Maximum buffered duration in whole seconds before pruning old GOPs.
   *
   * When the total queued time exceeds this limit, the oldest complete GOPs
   * are dropped to maintain the time window. However, the element enforces
   * a 2-GOP minimum floor - even if max-time is exceeded, at least 2 complete
   * GOPs are always retained to ensure playback continuity.
   *
   * - Positive values: Time limit in seconds (integer only, no sub-second precision)
   * - Zero or negative: Unlimited buffering (pruning disabled)
   * - Sub-second values: Effectively floored to whole seconds
   *
   * Example: 30-second pre-roll window
   * |[
   * gst-launch-1.0 ... ! prerecordloop max-time=30 ! ...
   * ]|
   *
   * Default: 0 (unlimited)
   */
  g_object_class_install_property(
      gobject_class, PROP_MAX_TIME,
      g_param_spec_int("max-time", "Max Time (s)",
                       "Maximum buffered duration in whole seconds before pruning; "
                       "non-positive means unlimited. "
                       "Integer-only: sub-second precision not supported (effectively floored to whole seconds). "
                       "Negative values are clamped to 0.",
                       G_MININT, /* min: allow negative so setter can clamp to 0 */
                       G_MAXINT, /* max */
                       (gint)(DEFAULT_MAX_SIZE_TIME / GST_SECOND), /* default */
                       G_PARAM_READWRITE));

  gst_element_class_set_details_simple(
      gstelement_class, "PreRecordLoop", "Generic",
      "Capture data in ring buffer and flush onwards on event",
      "Kartik Aiyer <kartik.aiyer@gmail.com>");

  gst_element_class_add_pad_template(gstelement_class,
                                     gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(
      gstelement_class, gst_static_pad_template_get(&sink_factory));

  gstelement_class->change_state = gst_pre_record_loop_change_state;

  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_finalize);
  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_sink_activate_mode);
  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_sink_event);
  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_sink_query);
  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_src_activate_mode);
  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_src_event);
  GST_DEBUG_REGISTER_FUNCPTR(gst_pre_record_loop_src_query);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void gst_pre_record_loop_init(GstPreRecordLoop *filter) {

  filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(filter->sinkpad, gst_pre_record_loop_sink_event);
  gst_pad_set_chain_function(filter->sinkpad, gst_pre_record_loop_chain);

  gst_pad_set_query_function(filter->sinkpad, gst_pre_record_loop_sink_query);
  gst_pad_set_activatemode_function(filter->sinkpad,
                                    gst_pre_record_loop_sink_activate_mode);
  gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  gst_pad_set_event_function(filter->srcpad, gst_pre_record_loop_src_event);
  gst_pad_set_query_function(filter->srcpad, gst_pre_record_loop_src_query);
  gst_pad_set_activatemode_function(filter->srcpad,
                                    gst_pre_record_loop_src_activate_mode);
  gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

  filter->silent = FALSE;
  /* Starting in buffering mode: accept buffers immediately */
  filter->srcresult = GST_FLOW_OK;

  g_mutex_init(&filter->lock);
  g_cond_init(&filter->item_add);
  g_cond_init(&filter->item_del);

  filter->queue = gst_vec_deque_new_for_struct(
      sizeof(GstQueueItem), DEFAULT_MAX_SIZE_BUFFERS * 3 / 2);
  
  // Initialize buffer size limits
  filter->max_size.buffers = DEFAULT_MAX_SIZE_BUFFERS;
  filter->max_size.bytes = DEFAULT_MAX_SIZE_BYTES;
  filter->max_size.time = DEFAULT_MAX_SIZE_TIME;
  clear_level(&filter->cur_level);
  
  // Initialize segments
  gst_segment_init(&filter->sink_segment, GST_FORMAT_TIME);
  gst_segment_init(&filter->src_segment, GST_FORMAT_TIME);
  
  // Initialize timing
  filter->sinktime = filter->srctime = GST_CLOCK_STIME_NONE;
  filter->sink_start_time = GST_CLOCK_STIME_NONE;
  filter->sink_tainted = filter->src_tainted = FALSE;
  
  GST_DEBUG_OBJECT(filter, "Initialized PreRecLoop");
  filter->mode = GST_PREREC_MODE_BUFFERING;

  filter->current_gop_id = 0;
  filter->gop_size = 0;
  filter->last_gop_id = 0;
  filter->num_gops = 0;
  filter->preroll_sent = FALSE;
  /* init stats */
  memset(&filter->stats, 0, sizeof(filter->stats));
  filter->flush_on_eos = GST_PREREC_FLUSH_ON_EOS_AUTO;
  filter->flush_trigger_name = NULL;
}

static void gst_prerec_get_stats(GstPreRecordLoop *loop, GstPreRecStats *out_stats) {
  g_return_if_fail(loop != NULL);
  g_return_if_fail(out_stats != NULL);
  GST_PREREC_MUTEX_LOCK(loop);
  *out_stats = loop->stats; /* shallow copy */
  GST_PREREC_MUTEX_UNLOCK(loop);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean prerecordloop_init(GstPlugin *prerecordloop) {
  /* debug category for filtering log messages
   *
   * exchange the string 'Template prerecordloop' with your description
   */
  /* Renamed categories to match tasks/spec naming (T003) */
  GST_DEBUG_CATEGORY_INIT(prerec_debug, "pre_record_loop",
                          GST_DEBUG_FG_YELLOW | GST_DEBUG_BOLD,
                          "pre capture ring bufffer element");
  GST_DEBUG_CATEGORY_INIT(prerec_dataflow, "pre_record_loop_dataflow",
                          GST_DEBUG_FG_CYAN | GST_DEBUG_BOLD,
                          "dataflow inside the prerec loop");
  return GST_ELEMENT_REGISTER(pre_record_loop, prerecordloop);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstprerecordloop"
#endif

/* gstreamer looks for this structure to register prerecordloops
 *
 * exchange the string 'Template prerecordloop' with your prerecordloop
 * description
 */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, prerecordloop,
                  "Pre Record Loop", prerecordloop_init, "1.19", "MIT",
                  "Pre Record Loop", "https://github.com/KartikAiyer");
