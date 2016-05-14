/* GStreamer split muxer bin
 * Copyright (C) 2014 Jan Schmidt <jan@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SPLITMUXSINK_H__
#define __GST_SPLITMUXSINK_H__

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

G_BEGIN_DECLS

#define GST_TYPE_SPLITMUX_SINK               (gst_splitmux_sink_get_type())
#define GST_SPLITMUX_SINK(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPLITMUX_SINK,GstSplitMuxSink))
#define GST_SPLITMUX_SINK_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPLITMUX_SINK,GstSplitMuxSinkClass))
#define GST_IS_SPLITMUX_SINK(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPLITMUX_SINK))
#define GST_IS_SPLITMUX_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPLITMUX_SINK))

typedef struct _GstSplitMuxSink GstSplitMuxSink;
typedef struct _GstSplitMuxSinkClass GstSplitMuxSinkClass;

GType gst_splitmux_sink_get_type(void);
gboolean register_splitmuxsink (GstPlugin * plugin);

typedef enum _SplitMuxState {
  SPLITMUX_STATE_STOPPED,
  SPLITMUX_STATE_COLLECTING_GOP_START,
  SPLITMUX_STATE_WAITING_GOP_COMPLETE,
  SPLITMUX_STATE_ENDING_FILE,
  SPLITMUX_STATE_START_NEXT_FRAGMENT,
} SplitMuxState;

typedef struct _MqStreamBuf
{
  gboolean keyframe;
  GstClockTime run_ts;
  gsize buf_size;
} MqStreamBuf;

typedef struct _MqStreamCtx
{
  gint refcount;

  GstSplitMuxSink *splitmux;

  guint sink_pad_block_id;
  guint src_pad_block_id;

  gboolean is_reference;

  gboolean flushing;
  gboolean in_eos;
  gboolean out_eos;

  GstSegment in_segment;
  GstSegment out_segment;

  GstClockTime in_running_time;
  GstClockTime out_running_time;

  gsize in_bytes;

  GQueue queued_bufs;

  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean out_blocked;
} MqStreamCtx;

struct _GstSplitMuxSink {
  GstBin parent;

  GMutex lock;
  GCond data_cond;

  SplitMuxState state;
  gdouble mux_overhead;

  GstClockTime threshold_time;
  guint64 threshold_bytes;
  guint max_files;

  guint mq_max_buffers;

  GstElement *mq;
  GstElement *muxer;
  GstElement *sink;

  GstElement *provided_muxer;

  GstElement *provided_sink;
  GstElement *active_sink;

  gchar *location;
  guint fragment_id;

  GList *contexts;

  MqStreamCtx *reference_ctx;
  guint queued_gops;
  GstClockTime max_in_running_time;
  GstClockTime max_out_running_time;

  GstClockTime muxed_out_time;
  gsize muxed_out_bytes;
  gboolean have_muxed_something;

  GstClockTime mux_start_time;
  gsize mux_start_bytes;

  gboolean opening_first_fragment;
};

struct _GstSplitMuxSinkClass {
  GstBinClass parent_class;
};

G_END_DECLS

#endif /* __GST_SPLITMUXSINK_H__ */
