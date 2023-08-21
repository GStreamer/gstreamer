/*
 * GStreamer
 *
 * Copyright (C) 2023 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2023 Jonas Danielsson <jonas.danielsson@spiideo.com>
 *
 * gstrtppassthroughpay.h:
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
 */

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_RTP_PASSTHROUGH_PAY (gst_rtp_passthrough_pay_get_type())
#define GST_RTP_PASSTHROUGH_PAY(obj)                                           \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RTP_PASSTHROUGH_PAY,             \
                              GstRtpPassthroughPay))
#define GST_RTP_PASSTHROUGH_PAY_CLASS(klass)                                   \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RTP_PASSTHROUGH_PAY,              \
                           GstRtpPassthroughPayClass))
#define GST_IS_RTP_PASSTHROUGH_PAY(obj)                                        \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RTP_PASSTHROUGH_PAY))
#define GST_IS_RTP_PASSTHROUGH_PAY_CLASS(klass)                                \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RTP_PASSTHROUGH_PAY))
#define GST_RTP_PASSTHROUGH_PAY_GET_CLASS(obj)                                 \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_RTP_PASSTHROUGH_PAY,              \
                             GstRtpPassthroughPayClass))
typedef struct _GstRtpPassthroughPay GstRtpPassthroughPay;
typedef struct _GstRtpPassthroughPayClass GstRtpPassthroughPayClass;

struct _GstRtpPassthroughPayClass
{
  GstElementClass parent_class;
};

struct _GstRtpPassthroughPay
{
  GstElement parent;

  GstPad *sinkpad, *srcpad;

  GstCaps *caps;
  GstSegment segment;

  guint clock_rate;
  guint pt;
  gboolean pt_override;
  guint ssrc;
  gboolean ssrc_set;
  guint timestamp;
  guint timestamp_offset;
  gboolean timestamp_offset_set;
  guint seqnum;
  guint seqnum_offset;
  GstClockTime pts_or_dts;
};

GType gst_rtp_passthrough_pay_get_type (void);

G_END_DECLS
