/* GStreamer
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
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
 
#ifndef __GST_RTP_SPEEX_DEC_H__
#define __GST_RTP_SPEEX_DEC_H__

#include <gst/gst.h>
#include <gst/rtp/gstbasertpdepayload.h>

G_BEGIN_DECLS

typedef struct _GstRtpSPEEXDec GstRtpSPEEXDec;
typedef struct _GstRtpSPEEXDecClass GstRtpSPEEXDecClass;

#define GST_TYPE_RTP_SPEEX_DEC \
  (gst_rtpspeexdec_get_type())
#define GST_RTP_SPEEX_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_SPEEX_DEC,GstRtpSPEEXDec))
#define GST_RTP_SPEEX_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_SPEEX_DEC,GstRtpSPEEXDec))
#define GST_IS_RTP_SPEEX_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_SPEEX_DEC))
#define GST_IS_RTP_SPEEX_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_SPEEX_DEC))

struct _GstRtpSPEEXDec
{
  GstBaseRTPDepayload depayload;
};

struct _GstRtpSPEEXDecClass
{
  GstBaseRTPDepayloadClass parent_class;
};

gboolean gst_rtpspeexdec_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_SPEEX_DEC_H__ */
