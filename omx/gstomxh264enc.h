/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_H264_ENC_H__
#define __GST_OMX_H264_ENC_H__

#include <gst/gst.h>
#include "gstomxvideoenc.h"

G_BEGIN_DECLS

#define GST_TYPE_OMX_H264_ENC \
  (gst_omx_h264_enc_get_type())
#define GST_OMX_H264_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_H264_ENC,GstOMXH264Enc))
#define GST_OMX_H264_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_H264_ENC,GstOMXH264EncClass))
#define GST_OMX_H264_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_H264_ENC,GstOMXH264EncClass))
#define GST_IS_OMX_H264_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_H264_ENC))
#define GST_IS_OMX_H264_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_H264_ENC))

typedef struct _GstOMXH264Enc GstOMXH264Enc;
typedef struct _GstOMXH264EncClass GstOMXH264EncClass;

struct _GstOMXH264Enc
{
  GstOMXVideoEnc parent;

#ifdef USE_OMX_TARGET_RPI
  gboolean inline_sps_pps_headers;
#endif
  guint32 periodicty_idr;
  guint32 interval_intraframes;
  guint32 b_frames;
  guint32 entropy_mode;
  gboolean constrained_intra_prediction;
  guint32 loop_filter_mode;
  guint8 ref_frames;

  GList *headers;
};

struct _GstOMXH264EncClass
{
  GstOMXVideoEncClass parent_class;
};

GType gst_omx_h264_enc_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_H264_ENC_H__ */

