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

#ifndef __GST_RTP_MP4V_ENC_H__
#define __GST_RTP_MP4V_ENC_H__

#include <gst/gst.h>
#include <gst/rtp/gstbasertppayload.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_MP4V_ENC \
  (gst_rtpmp4venc_get_type())
#define GST_RTP_MP4V_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_MP4V_ENC,GstRtpMP4VEnc))
#define GST_RTP_MP4V_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_MP4V_ENC,GstRtpMP4VEnc))
#define GST_IS_RTP_MP4V_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_MP4V_ENC))
#define GST_IS_RTP_MP4V_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_MP4V_ENC))

typedef struct _GstRtpMP4VEnc GstRtpMP4VEnc;
typedef struct _GstRtpMP4VEncClass GstRtpMP4VEncClass;

struct _GstRtpMP4VEnc
{
  GstBaseRTPPayload    payload;

  GstAdapter   *adapter;
  GstClockTime  first_ts;

  gint		rate;
  gint		profile;
  GstBuffer    *config;
  gboolean      send_config;
};

struct _GstRtpMP4VEncClass
{
  GstBaseRTPPayloadClass parent_class;
};

gboolean gst_rtpmp4venc_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_MP4V_ENC_H__ */
