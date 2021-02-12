/* GStreamer
 * Copyright (C) 2020 Collabora Ltd.
 *  Author: Guillaume Desmottes <guillaume.desmottes@collabora.com>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */


#ifndef __GST_RTP_ISAC_PAY_H__
#define __GST_RTP_ISAC_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_ISAC_PAY gst_rtp_isac_pay_get_type ()

G_DECLARE_FINAL_TYPE(GstRtpIsacPay, gst_rtp_isac_pay, GST, RTP_ISAC_PAY, GstRTPBasePayload);

G_END_DECLS

#endif /* __GST_RTP_ISAC_PAY_H__ */
