/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTP_PT_DEMUX_H__
#define __GST_RTP_PT_DEMUX_H__

#include <gst/gst.h>

#define GST_TYPE_RTP_PT_DEMUX            (gst_rtp_pt_demux_get_type())
#define GST_RTP_PT_DEMUX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_PT_DEMUX,GstRTPPtDemux))
#define GST_RTP_PT_DEMUX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_PT_DEMUX,GstRTPPtDemuxClass))
#define GST_IS_RTP_PT_DEMUX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_PT_DEMUX))
#define GST_IS_RTP_PT_DEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_PT_DEMUX))

typedef struct _GstRTPPtDemux GstRTPPtDemux;
typedef struct _GstRTPPtDemuxClass GstRTPPtDemuxClass;
typedef struct _GstRTPPtDemuxPad GstRTPPtDemuxPad;

struct _GstRTPPtDemux
{
  GstElement parent;  /**< parent class */

  GstPad *sink;       /**< the sink pad */
  guint16 last_pt;    /**< pt of the last packet 0xFFFF if none */
  GSList *srcpads;    /**< a linked list of GstRTPPtDemuxPad objects */
};

struct _GstRTPPtDemuxClass
{
  GstElementClass parent_class;

  /* get the caps for pt */
  GstCaps* (*request_pt_map)      (GstRTPPtDemux *demux, guint pt);

  /* signal emmited when a new PT is found from the incoming stream */
  void     (*new_payload_type)    (GstRTPPtDemux *demux, guint pt, GstPad * pad);

  /* signal emitted when the payload type changes */
  void     (*payload_type_change) (GstRTPPtDemux *demux, guint pt);

  void     (*clear_pt_map)        (GstRTPPtDemux *demux);
};

GType gst_rtp_pt_demux_get_type (void);

#endif /* __GST_RTP_PT_DEMUX_H__ */
