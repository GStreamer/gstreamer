/* RTP muxer element for GStreamer
 *
 * gstrtpmux.h:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTP_MUX_H__
#define __GST_RTP_MUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_RTP_MUX (gst_rtp_mux_get_type())
#define GST_RTP_MUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_MUX, GstRTPMux))
#define GST_RTP_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_MUX, GstRTPMuxClass))
#define GST_RTP_MUX_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTP_MUX, GstRTPMuxClass))
#define GST_IS_RTP_MUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_MUX))
#define GST_IS_RTP_MUX_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_MUX))
typedef struct _GstRTPMux GstRTPMux;
typedef struct _GstRTPMuxClass GstRTPMuxClass;


typedef struct
{
  gboolean have_clock_base;
  guint clock_base;

  GstCaps *out_caps;

  GstSegment segment;

  gboolean priority;
} GstRTPMuxPadPrivate;


/**
 * GstRTPMux:
 *
 * The opaque #GstRTPMux structure.
 */
struct _GstRTPMux
{
  GstElement element;

  /* pad */
  GstPad *srcpad;

  guint32 ts_base;
  guint16 seqnum_base;

  gint32 ts_offset;
  gint16 seqnum_offset;
  guint16 seqnum;               /* protected by object lock */
  guint ssrc;
  guint current_ssrc;

  gboolean segment_pending;

  GstClockTime last_stop;
};

struct _GstRTPMuxClass
{
  GstElementClass parent_class;

  gboolean (*accept_buffer_locked) (GstRTPMux *rtp_mux,
      GstRTPMuxPadPrivate * padpriv, GstBuffer * buffer);

  gboolean (*src_event) (GstRTPMux *rtp_mux, GstEvent *event);
};


GType gst_rtp_mux_get_type (void);
gboolean gst_rtp_mux_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_RTP_MUX_H__ */
