/* GStreamer
 *
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
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

#ifndef __GST_VDP_DECODER_H__
#define __GST_VDP_DECODER_H__

#include <gst/gst.h>
#include <vdpau/vdpau.h>

#include <gst/video/gstvideodecoder.h>

#include "gstvdpdevice.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_DECODER		        (gst_vdp_decoder_get_type())
#define GST_VDP_DECODER(obj)		        (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VDP_DECODER, GstVdpDecoder))
#define GST_VDP_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VDP_DECODER, GstVdpDecoderClass))
#define GST_VDP_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_VDP_DECODER, GstVdpDecoderClass))
#define GST_IS_VDP_DECODER(obj)	        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VDP_DECODER))
#define GST_IS_VDP_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VDP_DECODER))

typedef struct _GstVdpDecoder GstVdpDecoder;
typedef struct _GstVdpDecoderClass GstVdpDecoderClass;


struct _GstVdpDecoder {
  GstVideoDecoder video_decoder;

  GstVdpDevice *device;
  VdpDecoder decoder;

  GstVideoInfo info;

  /* properties */
  gchar *display;
};

struct _GstVdpDecoderClass {
  GstVideoDecoderClass video_decoder_class;
};

void
gst_vdp_decoder_post_error (GstVdpDecoder * decoder, GError * error);

GstFlowReturn
gst_vdp_decoder_render (GstVdpDecoder * vdp_decoder, VdpPictureInfo *info,
    guint n_bufs, VdpBitstreamBuffer *bufs, GstVideoCodecFrame *frame);

GstFlowReturn
gst_vdp_decoder_init_decoder (GstVdpDecoder * vdp_decoder,
			      VdpDecoderProfile profile, guint32 max_references,
			      GstVideoCodecState *output_state);

GType gst_vdp_decoder_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_DECODER_H__ */
