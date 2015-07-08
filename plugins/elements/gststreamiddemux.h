/*
 * GStreamer streamiddemux eleement
 *
 * Copyright 2013 LGE Corporation.
 *  @author: Hoonhee Lee <hoonhee.lee@lge.com>
 *  @author: Jeongseok Kim <jeongseok.kim@lge.com>
 *  @author: Wonchul Lee <wonchul86.lee@lge.com>
 *
 * gststreamiddemux.h: Simple stream-id-demultiplexer element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __GST_STREAMID_DEMUX_H__
#define __GST_STREAMID_DEMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_STREAMID_DEMUX \
  (gst_streamid_demux_get_type())
#define GST_STREAMID_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_STREAMID_DEMUX, GstStreamidDemux))
#define GST_STREAMID_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_STREAMID_DEMUX, GstStreamidDemuxClass))
#define GST_IS_STREAMID_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_STREAMID_DEMUX))
#define GST_IS_STREAMID_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_STREAMID_DEMUX))
typedef struct _GstStreamidDemux GstStreamidDemux;
typedef struct _GstStreamidDemuxClass GstStreamidDemuxClass;

/**
 * GstStreamidDemux:
 *
 * The opaque #GstStreamidDemux data structure.
 */
struct _GstStreamidDemux
{
  GstElement element;

  GstPad *sinkpad;

  guint nb_srcpads;
  GstPad *active_srcpad;

  /* This table contains srcpad and stream-id */
  GHashTable *stream_id_pairs;
};

struct _GstStreamidDemuxClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_streamid_demux_get_type (void);

G_END_DECLS
#endif /* __GST_STREAMID_DEMUX_H__ */
