/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (c) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
 *      Author : Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (c) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_DAALAENC_H__
#define __GST_DAALAENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <daala/daalaenc.h>

G_BEGIN_DECLS

#define GST_TYPE_DAALA_ENC \
  (gst_daala_enc_get_type())
#define GST_DAALA_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DAALA_ENC,GstDaalaEnc))
#define GST_DAALA_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DAALA_ENC,GstDaalaEncClass))
#define GST_IS_DAALA_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DAALA_ENC))
#define GST_IS_DAALA_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DAALA_ENC))

typedef struct _GstDaalaEnc GstDaalaEnc;
typedef struct _GstDaalaEncClass GstDaalaEncClass;

/**
 * GstDaalaEnc:
 *
 * Opaque data structure.
 */
struct _GstDaalaEnc
{
  GstVideoEncoder element;

  ogg_stream_state vo;

  daala_enc_ctx *encoder;
  daala_info info;
  daala_comment comment;
  gboolean initialised;

  guint packetno;
  guint64 bytes_out;
  guint64 granulepos_offset;
  guint64 timestamp_offset;
  guint64 pfn_offset;

  int quant;
  int keyframe_rate;
  gboolean quant_changed;
  gboolean keyframe_rate_changed;

  GstVideoCodecState *input_state;
};

struct _GstDaalaEncClass
{
  GstVideoEncoderClass parent_class;
};

GType gst_daala_enc_get_type (void);
gboolean gst_daala_enc_register (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_DAALAENC_H__ */

