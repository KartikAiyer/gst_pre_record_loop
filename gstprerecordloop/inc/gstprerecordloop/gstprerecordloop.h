/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
 * Copyright (C) 2025  <<user@hostname.org>>
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

#ifndef __GST_PRERECORDLOOP_H__
#define __GST_PRERECORDLOOP_H__

#include <gst/gst.h>
#include <gst/gstvecdeque.h>

G_BEGIN_DECLS

/* Flush-on-EOS policy enumeration */
typedef enum {
  GST_PREREC_FLUSH_ON_EOS_AUTO,   /* flush only in pass-through mode */
  GST_PREREC_FLUSH_ON_EOS_ALWAYS, /* always flush on EOS */
  GST_PREREC_FLUSH_ON_EOS_NEVER   /* never flush on EOS */
} GstPreRecFlushOnEos;

#define GST_TYPE_PREREC_FLUSH_ON_EOS (gst_prerec_flush_on_eos_get_type())
GType gst_prerec_flush_on_eos_get_type(void);

#define GST_TYPE_PRERECORDLOOP (gst_pre_record_loop_get_type())
G_DECLARE_FINAL_TYPE(GstPreRecordLoop, gst_pre_record_loop, GST, PRERECORDLOOP, GstElement)
#define GST_PRERECLOOP(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PRERECORDLOOP, GstPreRecordLoop))
#define GST_PRERECLOOP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PRERECORDLOOP, GstQueueClass))
#define GST_IS_PRERECORDLOOP(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PRERECORDLOOP))
#define GST_PREREC_CAST(obj) ((GstPreRecordLoop*) (obj))

typedef struct _GstPreRecSize {
  guint   buffers;
  guint   bytes;
  guint64 time;
} GstPreRecSize;

typedef enum { GST_PREREC_MODE_PASS_THROUGH, GST_PREREC_MODE_BUFFERING } GstPreRecLoopMode;

/* Future statistics hook (T021/T026): lightweight counters exposed for tests */
typedef struct _GstPreRecStats {
  guint drops_gops;         /* number of whole GOP pruning operations */
  guint drops_buffers;      /* number of individual buffers dropped inside GOP pruning */
  guint drops_events;       /* number of (non-sticky) events discarded during pruning */
  guint queued_gops_cur;    /* current GOPs resident (rough heuristic until full impl) */
  guint queued_buffers_cur; /* current buffer count (mirror of cur_level.buffers) */
  guint flush_count;        /* number of accepted prerecord-flush events (T026) */
  guint rearm_count;        /* number of prerecord-arm events processed (T026) */
} GstPreRecStats;

typedef struct _GstPreRecordLoop {
  GstElement element;

  GstPad *   sinkpad, *srcpad;
  GstSegment sink_segment;
  GstSegment src_segment;

  GstClockTimeDiff sinktime, srctime;
  GstClockTimeDiff sink_start_time;

  /* TRUE if either position needs to be recalculated */
  gboolean sink_tainted, src_tainted;

  /* flowreturn when srcpad is paused */
  GstFlowReturn srcresult;
  gboolean      unexpected;
  gboolean      eos;

  GMutex   lock;
  gboolean waiting_add;
  GCond    item_add;
  gboolean waiting_del;
  GCond    item_del;

  /* the queue of data */
  GstVecDeque* queue;

  gboolean silent;

  GstPreRecSize cur_level;
  GstPreRecSize max_size;

  gboolean newseg_applied_to_src;

  guint current_gop_id;
  guint last_gop_id;
  guint gop_size;
  guint num_gops;

  GstPreRecLoopMode mode;
  gboolean          head_needs_discont;
  gboolean          tail_needs_discont;

  GstPreRecFlushOnEos flush_on_eos;
  gboolean            preroll_sent;

  /* custom downstream event name that triggers flush (allocated) */
  gchar* flush_trigger_name;

  /* stats (incremented under lock; read-only snapshot via helper) */
  GstPreRecStats stats;
} GstPreRecordLoop;

G_END_DECLS

#endif /* __GST_PRERECORDLOOP_H__ */
