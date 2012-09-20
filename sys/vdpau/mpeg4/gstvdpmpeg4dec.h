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

#ifndef __GST_VDP_MPEG4_DEC_H__
#define __GST_VDP_MPEG4_DEC_H__

#include <gst/gst.h>

#include "../gstvdpdecoder.h"

#include "mpeg4util.h"
#include "gstmpeg4frame.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_MPEG4_DEC            (gst_vdp_mpeg4_dec_get_type())
#define GST_VDP_MPEG4_DEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_MPEG4_DEC,GstVdpMpeg4Dec))
#define GST_VDP_MPEG4_DEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_MPEG4_DEC,GstVdpMpeg4DecClass))
#define GST_IS_VDP_MPEG4_DEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_MPEG4_DEC))
#define GST_IS_VDP_MPEG4_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_MPEG4_DEC))

typedef struct _GstVdpMpeg4Dec      GstVdpMpeg4Dec;
typedef struct _GstVdpMpeg4DecClass GstVdpMpeg4DecClass;

struct _GstVdpMpeg4Dec
{
  GstVdpDecoder vdp_decoder;
  
  gboolean is_configured;
  Mpeg4VideoObjectLayer vol;
  guint32 tframe;

  GstMpeg4Frame *f_frame, *b_frame;
};

struct _GstVdpMpeg4DecClass 
{
  GstVdpDecoderClass vdp_decoder_class;
};

GType gst_vdp_mpeg4_dec_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_MPEG4_DEC_H__ */
