/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RTP_H264_PAY_H__
#define __GST_RTP_H264_PAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstbasertppayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H264_PAY \
  (gst_rtp_h264_pay_get_type())
#define GST_RTP_H264_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_H264_PAY,GstRtpH264Pay))
#define GST_RTP_H264_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_H264_PAY,GstRtpH264PayClass))
#define GST_IS_RTP_H264_PAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_H264_PAY))
#define GST_IS_RTP_H264_PAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_H264_PAY))

typedef enum
{
  GST_H264_SCAN_MODE_BYTESTREAM,
  GST_H264_SCAN_MODE_MULTI_NAL,
  GST_H264_SCAN_MODE_SINGLE_NAL
} GstH264ScanMode;

typedef struct _GstRtpH264Pay GstRtpH264Pay;
typedef struct _GstRtpH264PayClass GstRtpH264PayClass;

struct _GstRtpH264Pay
{
  GstBaseRTPPayload payload;

  guint profile;
  GList *sps, *pps;

  gboolean packetized;
  gboolean au_alignment;
  guint nal_length_size;
  GArray *queue;

  gchar *sprop_parameter_sets;
  gboolean update_caps;
  GstH264ScanMode scan_mode;

  GstAdapter *adapter;

  guint spspps_interval;
  gboolean send_spspps;
  GstClockTime last_spspps;

  gboolean buffer_list;
};

struct _GstRtpH264PayClass
{
  GstBaseRTPPayloadClass parent_class;
};

GType gst_rtp_h264_pay_get_type (void);

gboolean gst_rtp_h264_pay_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_H264_PAY_H__ */
