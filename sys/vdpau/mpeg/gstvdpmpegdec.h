/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef __GST_VDP_MPEG_DEC_H__
#define __GST_VDP_MPEG_DEC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "../gstvdpdecoder.h"

G_BEGIN_DECLS
typedef struct _GstVdpMpegStreamInfo GstVdpMpegStreamInfo;

struct _GstVdpMpegStreamInfo
{
  gint width, height;
  gint fps_n, fps_d;
  gint par_n, par_d;
  gboolean interlaced;
  gint version;
  VdpDecoderProfile profile;
};

#define GST_TYPE_VDP_MPEG_DEC            (gst_vdp_mpeg_dec_get_type())
#define GST_VDP_MPEG_DEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_MPEG_DEC,GstVdpMpegDec))
#define GST_VDP_MPEG_DEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_MPEG_DEC,GstVdpMpegDecClass))
#define GST_IS_VDP_MPEG_DEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_MPEG_DEC))
#define GST_IS_VDP_MPEG_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_MPEG_DEC))

typedef enum {
  GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE,
  GST_VDP_MPEG_DEC_STATE_NEED_GOP,
  GST_VDP_MPEG_DEC_STATE_NEED_DATA
} GstVdpMpegDecState;

typedef struct _GstVdpMpegDec      GstVdpMpegDec;
typedef struct _GstVdpMpegDecClass GstVdpMpegDecClass;

struct _GstVdpMpegDec
{
  GstVdpDecoder vdp_decoder;

  VdpDecoder decoder;

  GstVdpMpegStreamInfo stream_info;

  /* decoder state */
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  GstVdpMpegDecState state;
  gint prev_packet;
  
  /* currently decoded frame info */
  VdpPictureInfoMPEG1Or2 vdp_info;
  guint64 frame_nr;

  /* frame_nr from GOP */
  guint64 gop_frame;
  
  /* forward and backward reference */
  GstVideoCodecFrame *f_frame, *b_frame;
};

struct _GstVdpMpegDecClass 
{
  GstVdpDecoderClass vdp_decoder_class;
};

GType gst_vdp_mpeg_dec_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_MPEG_DEC_H__ */
