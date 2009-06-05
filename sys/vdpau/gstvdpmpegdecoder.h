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

#ifndef __GST_VDP_MPEG_DECODER_H__
#define __GST_VDP_MPEG_DECODER_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "gstvdpdevice.h"
#include "gstvdpvideobuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_MPEG_DECODER            (gst_vdp_mpeg_decoder_get_type())
#define GST_VDP_MPEG_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_MPEG_DECODER,GstVdpMpegDecoder))
#define GST_VDP_MPEG_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_MPEG_DECODER,GstVdpMpegDecoderClass))
#define GST_IS_VDPAU_MPEG_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_MPEG_DECODER))
#define GST_IS_VDPAU_MPEG_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_MPEG_DECODER))

typedef struct _GstVdpMpegDecoder      GstVdpMpegDecoder;
typedef struct _GstVdpMpegDecoderClass GstVdpMpegDecoderClass;

struct _GstVdpMpegDecoder
{
  GstElement element;

  gchar *display_name;
  GstVdpDevice *device;

  GstPad *src;
  GstPad *sink;
  
  gint width, height;
  gint fps_n, fps_d;
  gboolean interlaced;
  
  gint version;
  
  VdpDecoder decoder;
  VdpPictureInfoMPEG1Or2 vdp_info;
  GstBuffer *f_buffer;
  GstBuffer *b_buffer;
  
  gboolean broken_gop;
  GstClockTime duration;
  GstClockTime next_timestamp;

  gint64 byterate;
  
  GstAdapter *adapter;
};

struct _GstVdpMpegDecoderClass 
{
  GstElementClass element_class;  
};

GType gst_vdp_mpeg_decoder_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_MPEG_DECODER_H__ */
