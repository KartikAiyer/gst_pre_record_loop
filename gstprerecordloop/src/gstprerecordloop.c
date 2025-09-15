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
 *
 * FIXME:Describe prerecordloop here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! prerecordloop ! fakesink silent=TRUE
 * ]|
 * </refsect2>
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

GST_DEBUG_CATEGORY_STATIC(prerec_debug);
#define GST_CAT_DEFAULT prerec_debug
GST_DEBUG_CATEGORY_STATIC(prerec_dataflow);

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum { PROP_0, PROP_SILENT };

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

// Removed gst_pre_record_loop_src_pad_task as we're not using it anymore

typedef struct {
  GstMiniObject *item;
  gsize size;
  gboolean is_query;
  gboolean is_keyframe;
  guint gop_id;
} GstQueueItem;

static inline void clear_level(GstPreRecSize *level) {
  level->buffers = 0;
  level->time = 0;
  level->time = 0;
}

/** Finalize Function */

static void gst_pre_record_loop_finalize(GObject *object) {
  GstPreRecordLoop *prerec = GST_PRERECORDLOOP(object);
  GstQueueItem *qitem;

  GST_DEBUG_OBJECT(prerec, "finalize pre rec loop");

  while ((qitem = gst_vec_deque_pop_head_struct(prerec->queue))) {
    gst_mini_object_unref(qitem->item);
  }
  gst_vec_deque_free(prerec->queue);

  g_mutex_clear(&prerec->lock);
  g_cond_clear(&prerec->item_add);
  g_cond_clear(&prerec->item_del);

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

/** TODO */
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
    GST_DEBUG_OBJECT(loop, "Start time updated to $" GST_STIME_FORMAT,
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

typedef struct {
  GstClockTime first_timestamp;
  GstClockTime timestamp;
} BufListData;

static gboolean buffer_list_apply_time(GstBuffer **buf, guint idx,
                                       gpointer user_data) {
  BufListData *data = user_data;
  GstClockTime btime;

  GST_TRACE("buffer %u has pts %" GST_TIME_FORMAT " dts %" GST_TIME_FORMAT
            " duration %" GST_TIME_FORMAT,
            idx, GST_TIME_ARGS(GST_BUFFER_DTS(*buf)),
            GST_TIME_ARGS(GST_BUFFER_PTS(*buf)),
            GST_TIME_ARGS(GST_BUFFER_DURATION(*buf)));

  btime = GST_BUFFER_DTS_OR_PTS(*buf);
  if (GST_CLOCK_TIME_IS_VALID(btime)) {
    if (!GST_CLOCK_TIME_IS_VALID(data->first_timestamp)) {
      data->first_timestamp = btime;
    }
    data->timestamp = btime;
  }
  if (GST_BUFFER_DURATION_IS_VALID(*buf) &&
      GST_CLOCK_TIME_IS_VALID(data->timestamp)) {
    data->timestamp += GST_BUFFER_DURATION(*buf);
  }
  GST_TRACE("ts now %" GST_TIME_FORMAT, GST_TIME_ARGS(data->timestamp));
  return TRUE;
}

static void locked_apply_buffer_list(GstPreRecordLoop *loop,
                                     GstBufferList *buffer_list,
                                     GstSegment *segment, gboolean is_sink) {
  BufListData data = {.first_timestamp = GST_CLOCK_TIME_NONE,
                      .timestamp = GST_CLOCK_TIME_NONE};

  gst_buffer_list_foreach(buffer_list, buffer_list_apply_time, &data);

  if (is_sink && !GST_CLOCK_STIME_IS_VALID(loop->sink_start_time) &&
      GST_CLOCK_TIME_IS_VALID(data.first_timestamp)) {
    loop->sink_start_time =
        segment_to_running_time(segment, data.first_timestamp);
    GST_DEBUG_OBJECT(loop, "Start time updated to %" GST_STIME_FORMAT,
                     GST_STIME_ARGS(loop->sink_start_time));
  }

  GST_DEBUG_OBJECT(loop, "position updated to %" GST_TIME_FORMAT,
                   GST_TIME_ARGS(data.timestamp));

  segment->position = data.timestamp;

  if (is_sink) {
    loop->sink_tainted = TRUE;
  } else {
    loop->src_tainted = TRUE;
  }
  update_time_level(loop);
}

static GstQueueItem *gst_prerec_locked_dequeue(GstPreRecordLoop *loop) {
  GstQueueItem *qitem;
  GstMiniObject *item;
  gsize buf_size;

  qitem = gst_vec_deque_pop_head_struct(loop->queue);
  if (qitem == NULL) {
    GST_CAT_DEBUG_OBJECT(prerec_dataflow, loop, "the prerec loop is empty");
    return NULL;
  }

  item = qitem->item;
  buf_size = qitem->size;

  if (GST_IS_BUFFER(item)) {
    GstBuffer *buffer = GST_BUFFER_CAST(item);

    GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                       "retrieved buffer %p from prerec loop", buffer);
    loop->cur_level.buffers--;
    loop->cur_level.bytes -= buf_size;
    locked_apply_buffer(loop, buffer, &loop->src_segment, FALSE);

    if (loop->cur_level.buffers == 0) {
      loop->cur_level.time = 0;
    }
  } else if (GST_IS_EVENT(item)) {
    GstEvent *event = GST_EVENT_CAST(item);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_SEGMENT:
      if (G_LIKELY(!loop->newseg_applied_to_src)) {
        gst_pad_store_sticky_event(loop->srcpad, event);
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
    g_warning(
        "Unexpected item %p dequeued from queue %s (refcounting problem?)",
        item, GST_OBJECT_NAME(loop));
    gst_mini_object_unref(item);
    memset(qitem, 0, sizeof(GstQueueItem));
    item = NULL;
    qitem = NULL;
  }
  GST_PREREC_SIGNAL_DEL(loop);
  return qitem;
}

static void gst_prerec_locked_flush(GstPreRecordLoop *loop, gboolean full) {
  GstQueueItem *qitem;
  while ((qitem = gst_vec_deque_pop_head_struct(loop->queue))) {
    if (!full && !GST_IS_QUERY(qitem->item) && GST_IS_EVENT(qitem->item) &&
        GST_EVENT_IS_STICKY(qitem->item) &&
        GST_EVENT_TYPE(qitem->item) != GST_EVENT_SEGMENT &&
        GST_EVENT_TYPE(qitem->item) != GST_EVENT_EOS) {
      gst_pad_store_sticky_event(loop->srcpad, GST_EVENT_CAST(qitem->item));
    }
    gst_mini_object_unref(qitem->item);
    memset(qitem, 0, sizeof(GstQueueItem));
  }
  clear_level(&loop->cur_level);
  gst_segment_init(&loop->sink_segment, GST_FORMAT_TIME);
  gst_segment_init(&loop->src_segment, GST_FORMAT_TIME);
  loop->head_needs_discont = loop->tail_needs_discont = FALSE;

  loop->sinktime = loop->srctime = GST_CLOCK_STIME_NONE;
  loop->sink_start_time = GST_CLOCK_STIME_NONE;
  loop->sink_tainted = loop->src_tainted = FALSE;

  GST_PREREC_SIGNAL_DEL(loop);
}

static inline void gst_prerec_locked_enqueue_buffer(GstPreRecordLoop *loop,
                                                    gpointer item) {
  GstQueueItem qitem;
  GstBuffer *buffer = GST_BUFFER_CAST(item);
  gsize bsize = gst_buffer_get_size(buffer);

  qitem.item = item;
  qitem.is_query = FALSE;
  qitem.is_keyframe = !(GST_BUFFER_FLAGS(buffer) & GST_BUFFER_FLAG_DELTA_UNIT);
  loop->current_gop_id += (qitem.is_keyframe) ? 1 : 0;
  qitem.gop_id = loop->current_gop_id;
  qitem.size = bsize;
  if (gst_vec_deque_get_length(loop->queue) == 0 ||
      loop->cur_level.buffers == 0) {
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

static inline void gst_prerec_locked_enqueue_event(GstPreRecordLoop *loop,
                                                   gpointer item) {
  GstQueueItem qitem;
  GstEvent *event = GST_EVENT_CAST(item);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_EOS:
    GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "got EOS from upstream");
    if (loop->flush_on_eos) {
      gst_prerec_locked_flush(loop, FALSE);
    }
    loop->eos = TRUE;
    break;
  case GST_EVENT_SEGMENT:
    locked_apply_segment(loop, event, &loop->sink_segment, TRUE);
    /* If the queue is empty, qpply sink segment on the source */
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
    break;
  }
  qitem.item = item;
  qitem.is_query = FALSE;
  qitem.is_keyframe = FALSE;
  qitem.size = 0;
  gst_vec_deque_push_tail_struct(loop->queue, &qitem);
  GST_PREREC_SIGNAL_ADD(loop);
}

static void drop_last_item(GstPreRecordLoop *loop) {
  GstQueueItem *qitem = gst_prerec_locked_dequeue(loop);
  GstMiniObject *item = qitem->item;
  if (item && GST_IS_EVENT(item)) {
    GstEvent *event = GST_EVENT_CAST(item);
    if (event->type != GST_EVENT_SEGMENT) {
      // unref the events
      gst_event_unref(event);
    }
  } else {
    gst_buffer_unref(GST_BUFFER_CAST(item));
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
    GstMiniObject *item = qitem->item;
    if (item) {
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
}

static GstFlowReturn gst_buffer_or_list_chain(GstPad *pad, GstObject *parent,
                                              GstMiniObject *obj,
                                              gboolean is_list) {
  GstPreRecordLoop *loop = GST_PREREC_CAST(parent);

  GST_PREREC_MUTEX_LOCK_CHECK(loop, out_flushing);

  GstPreRecSize prev_level = loop->cur_level;

  if (loop->eos)
    goto out_eos;
  if (loop->unexpected)
    goto out_eos;

  if (!is_list) {
    GstClockTime duration, timestamp;
    GstBuffer *buffer = GST_BUFFER_CAST(obj);

    timestamp = GST_BUFFER_DTS_OR_PTS(buffer);
    duration = GST_BUFFER_DURATION(buffer);
    gboolean is_keyframe =
        !(GST_BUFFER_FLAGS(buffer) & GST_BUFFER_FLAG_DELTA_UNIT);

    GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                       "received buffer %p of size %" G_GSIZE_FORMAT
                       ", time %" GST_TIME_FORMAT
                       ", duration %" GST_TIME_FORMAT,
                       buffer, gst_buffer_get_size(buffer),
                       GST_TIME_ARGS(timestamp), GST_TIME_ARGS(duration));
  } else {
    GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                       "received buffer list %p with %u buffers", obj,
                       gst_buffer_list_length(GST_BUFFER_LIST_CAST(obj)));
    /* Make space if full */
    while (gst_loop_is_filled(loop)) {
      // Remove a frame from the end of the buffer
      gst_prerec_locked_drop(loop);
    }
  }
out_unref:
  GST_PREREC_MUTEX_UNLOCK(loop);

  gst_mini_object_unref(obj);

  return GST_FLOW_OK;

out_flushing:
  GST_CAT_LOG_OBJECT(prerec_dataflow, loop,
                     "exit beccause task paused, reason: %s",
                     gst_flow_get_name(loop->srcresult));
  GST_PREREC_MUTEX_UNLOCK(loop);
  gst_mini_object_unref(obj);
  return loop->srcresult;

out_eos:
  GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "exit because we received EOS");
  GST_PREREC_MUTEX_UNLOCK(loop);
  gst_mini_object_unref(obj);
  return GST_FLOW_EOS;
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
    case GST_PREREC_MODE_PASS_THROUGH:
      // During preroll, we need to send at least one buffer downstream to complete preroll
      if (!loop->preroll_sent) {
        GST_CAT_INFO_OBJECT(prerec_debug, loop, "Sending preroll buffer downstream");
        loop->preroll_sent = TRUE;
        // Switch to buffering mode after preroll
        loop->mode = GST_PREREC_MODE_BUFFERING;
        GstFlowReturn ret = gst_pad_push(loop->srcpad, buffer);
        GST_PREREC_MUTEX_UNLOCK(loop);
        return ret;
      } else {
        // Normal passthrough mode (after custom event)
        GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "Passthrough mode - forwarding buffer");
        GstFlowReturn ret = gst_pad_push(loop->srcpad, buffer);
        GST_PREREC_MUTEX_UNLOCK(loop);
        return ret;
      }
      break;

    case GST_PREREC_MODE_BUFFERING:
      GST_CAT_LOG_OBJECT(prerec_dataflow, loop, "Buffering mode - storing buffer");
      
      // Add buffer to ring buffer
      gst_prerec_locked_enqueue_buffer(loop, buffer);
      
      // Check if buffer is full and drop old frames if needed
      while (gst_loop_is_filled(loop)) {
        GST_CAT_LOG_OBJECT(prerec_debug, loop, "Buffer full, dropping oldest GOP");
        gst_prerec_locked_drop(loop);
      }
      
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
    // EOS will be sent downstream. If we are buffering, we will flush
    GST_PREREC_MUTEX_LOCK(loop);
    if (loop->mode == GST_PREREC_MODE_BUFFERING) {
      gst_prerec_locked_flush(loop, TRUE);
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
    /* and forward */
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  case GST_EVENT_CUSTOM_DOWNSTREAM: {
    // Check if this is our flush trigger event
    const GstStructure *structure = gst_event_get_structure(event);
    if (structure && gst_structure_has_name(structure, "prerecord-flush")) {
      GST_CAT_INFO_OBJECT(prerec_debug, loop, "Received flush trigger event!");
      
      GST_PREREC_MUTEX_LOCK(loop);
      if (loop->mode == GST_PREREC_MODE_BUFFERING) {
        // Flush all buffered frames downstream
        GST_CAT_INFO_OBJECT(prerec_debug, loop, "Flushing %d buffered frames", 
                           (int)gst_vec_deque_get_length(loop->queue));
        
        GstQueueItem *qitem;
        while ((qitem = gst_prerec_locked_dequeue(loop))) {
          if (GST_IS_BUFFER(qitem->item)) {
            GstBuffer *buf = GST_BUFFER_CAST(qitem->item);
            gst_pad_push(loop->srcpad, buf);
          } else if (GST_IS_EVENT(qitem->item)) {
            GstEvent *ev = GST_EVENT_CAST(qitem->item);
            gst_pad_push_event(loop->srcpad, ev);
          }
          g_free(qitem);
        }
        
        // Switch to passthrough mode
        loop->mode = GST_PREREC_MODE_PASS_THROUGH;
        GST_CAT_INFO_OBJECT(prerec_debug, loop, "Switched to passthrough mode");
      }
      GST_PREREC_MUTEX_UNLOCK(loop);
      
      // Don't forward the custom event downstream
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
      // I'm only pushing the SEGMENT and the GAP event on the queue
      if (event->type == GST_EVENT_SEGMENT || event->type == GST_EVENT_GAP) {
        gst_prerec_locked_enqueue_event(loop, event);
      } else {
        if (GST_EVENT_IS_STICKY(event)) {
          // Send the sticky event to the array
          gst_pad_store_sticky_event(loop->srcpad, event);
        }
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
  gboolean result = TRUE;
  GstPreRecordLoop *loop = GST_PRERECORDLOOP(parent);

#ifndef GST_DISABLE_GST_DEBUG
  GST_CAT_DEBUG_OBJECT(prerec_dataflow, loop, "got event %p (%d)", event,
                       GST_EVENT_TYPE(event));
#endif

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_RECONFIGURE:
    GST_PREREC_MUTEX_LOCK(loop);
    if (loop->srcresult == GST_FLOW_NOT_LINKED) {
      /* when we got not linked, assume downstream is linked again now and we
       * can try to start pushing again */
      loop->srcresult = GST_FLOW_OK;
    }
    GST_PREREC_MUTEX_UNLOCK(loop);

    result = gst_pad_push_event(loop->sinkpad, event);
    break;
  default:
    result = gst_pad_event_default(pad, parent, event);
    break;
  }
  return result;
}

static gboolean gst_pre_record_loop_src_query(GstPad *pad, GstObject *parent,
                                              GstQuery *query) {
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

  g_print("Class Init");
  gobject_class->set_property = gst_pre_record_loop_set_property;
  gobject_class->get_property = gst_pre_record_loop_get_property;

  g_object_class_install_property(
      gobject_class, PROP_SILENT,
      g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
                           FALSE, G_PARAM_READWRITE));

  gobject_class->finalize = gst_pre_record_loop_finalize;

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
  g_print("Init\n");

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
  filter->srcresult = GST_FLOW_FLUSHING;

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
  // We will start in the PASSTHROUGH mode so that when we move to the PAUSED
  // We will pre-roll the downstream elements with one frame.
  filter->mode = GST_PREREC_MODE_PASS_THROUGH;

  filter->current_gop_id = 0;
  filter->gop_size = 0;
  filter->last_gop_id = 0;
  filter->num_gops = 0;
  filter->preroll_sent = FALSE;
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
  GST_DEBUG_CATEGORY_INIT(prerec_debug, "prerecloop",
                          GST_DEBUG_FG_YELLOW | GST_DEBUG_BOLD,
                          "pre capture ring bufffer element");
  GST_DEBUG_CATEGORY_INIT(prerec_dataflow, "prerecloop_dataflow",
                          GST_DEBUG_FG_CYAN | GST_DEBUG_BOLD,
                          "dataflow inside the prerec loop");
  g_print("Plugin Init\n");
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
