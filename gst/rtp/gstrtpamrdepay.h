/* Gnome-Streamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTP_AMR_DEC_H__
#define __GST_RTP_AMR_DEC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_AMR_DEC \
  (gst_rtpamrdec_get_type())
#define GST_RTP_AMR_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_AMR_DEC,GstRtpAMRDec))
#define GST_RTP_AMR_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_AMR_DEC,GstRtpAMRDec))
#define GST_IS_RTP_AMR_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_AMR_DEC))
#define GST_IS_RTP_AMR_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_AMR_DEC))

typedef struct _GstRtpAMRDec GstRtpAMRDec;
typedef struct _GstRtpAMRDecClass GstRtpAMRDecClass;

struct _GstRtpAMRDec
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean negotiated;

  gboolean octet_align;
  guint8   mode_set;
  gint     mode_change_period;
  gboolean mode_change_neighbor;
  gint     maxptime;
  gboolean crc;
  gboolean robust_sorting;
  gboolean interleaving;
  gint     ptime;
  gint     channels;
  gint     rate;
};

struct _GstRtpAMRDecClass
{
  GstElementClass parent_class;
};

gboolean gst_rtpamrdec_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_AMR_DEC_H__ */
