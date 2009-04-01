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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VDPAU_MPEG_DECODER_H__
#define __GST_VDPAU_MPEG_DECODER_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "gstvdpaudecoder.h"

G_BEGIN_DECLS

#define GST_TYPE_VDPAU_MPEG_DECODER            (gst_vdpau_mpeg_decoder_get_type())
#define GST_VDPAU_MPEG_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDPAU_MPEG_DECODER,GstVdpauMpegDecoder))
#define GST_VDPAU_MPEG_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDPAU_MPEG_DECODER,GstVdpauMpegDecoderClass))
#define GST_IS_VDPAU_MPEG_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDPAU_MPEG_DECODER))
#define GST_IS_VDPAU_MPEG_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDPAU_MPEG_DECODER))

typedef struct _GstVdpauMpegDecoder      GstVdpauMpegDecoder;
typedef struct _GstVdpauMpegDecoderClass GstVdpauMpegDecoderClass;

struct _GstVdpauMpegDecoder
{
  GstVdpauDecoder dec;

  gboolean silent;

  gint version;
  
  VdpDecoder decoder;
  VdpPictureInfoMPEG1Or2 vdp_info;
  
  GstAdapter *adapter;
  gint slices;

};

struct _GstVdpauMpegDecoderClass 
{
  GstVdpauDecoderClass parent_class;
};

GType gst_vdpau_mpeg_decoder_get_type (void);

G_END_DECLS

#endif /* __GST_VDPAU_MPEG_DECODER_H__ */
