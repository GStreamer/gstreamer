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
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvdpvideosrcpad.h"

#include "gstvdpdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_decoder_debug);
#define GST_CAT_DEFAULT gst_vdp_decoder_debug

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_decoder_debug, "vdpdecoder", 0, \
    "VDPAU decoder base class");

GST_BOILERPLATE_FULL (GstVdpDecoder, gst_vdp_decoder, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER, DEBUG_INIT);

static GstFlowReturn
gst_vdp_decoder_shape_output (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf)
{
  GstVdpVideoSrcPad *vdp_pad;

  vdp_pad =
      (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder);

  return gst_vdp_video_src_pad_push (vdp_pad, GST_VDP_VIDEO_BUFFER (buf));
}

static GstPad *
gst_vdp_decoder_create_srcpad (GstBaseVideoDecoder * base_video_decoder,
    GstBaseVideoDecoderClass * base_video_decoder_class)
{
  GstPadTemplate *pad_template;
  GstVdpVideoSrcPad *vdp_pad;

  pad_template = gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (base_video_decoder_class),
      GST_BASE_VIDEO_DECODER_SRC_NAME);

  vdp_pad = gst_vdp_video_src_pad_new (pad_template,
      GST_BASE_VIDEO_DECODER_SRC_NAME);

  return GST_PAD (vdp_pad);
}

GstFlowReturn
gst_vdp_decoder_alloc_buffer (GstVdpDecoder * vdp_decoder,
    GstVdpVideoBuffer ** video_buf)
{
  GstVdpVideoSrcPad *vdp_pad;

  vdp_pad = (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (vdp_decoder);
  return gst_vdp_video_src_pad_alloc_buffer (vdp_pad, video_buf);
}

GstFlowReturn
gst_vdp_decoder_get_device (GstVdpDecoder * vdp_decoder, GstVdpDevice ** device,
    GError ** error)
{
  GstVdpVideoSrcPad *vdp_pad;

  vdp_pad = (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (vdp_decoder);
  return gst_vdp_video_src_pad_get_device (vdp_pad, device, error);
}

static void
gst_vdp_decoder_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GstCaps *src_caps;
  GstPadTemplate *src_template;

  src_caps = gst_vdp_video_buffer_get_caps (TRUE, VDP_CHROMA_TYPE_420);
  src_template = gst_pad_template_new (GST_BASE_VIDEO_DECODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);

  gst_element_class_add_pad_template (element_class, src_template);
}

static void
gst_vdp_decoder_init (GstVdpDecoder * decoder, GstVdpDecoderClass * klass)
{
}

static void
gst_vdp_decoder_class_init (GstVdpDecoderClass * klass)
{
  GstBaseVideoDecoderClass *base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_CLASS (klass);

  base_video_decoder_class->create_srcpad = gst_vdp_decoder_create_srcpad;
  base_video_decoder_class->shape_output = gst_vdp_decoder_shape_output;
}
