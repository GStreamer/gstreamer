/* 
 * Copyright (C) 2012 Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmfcdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_mfc_dec_debug);
#define GST_CAT_DEFAULT gst_mfc_dec_debug

static gboolean gst_mfc_dec_start (GstVideoDecoder * decoder);
static gboolean gst_mfc_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_mfc_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_mfc_dec_reset (GstVideoDecoder * decoder,
    gboolean hard);
static GstFlowReturn gst_mfc_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static GstStaticPadTemplate gst_mfc_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "parsed = (boolean) true, "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au")
    );

static GstStaticPadTemplate gst_mfc_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("NV12"))
    );

GST_BOILERPLATE (GstMFCDec, gst_mfc_dec, GstVideoDecoder,
    GST_TYPE_VIDEO_DECODER);

static void
gst_mfc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = (GstElementClass *) g_class;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mfc_dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mfc_dec_sink_template));

  gst_element_class_set_details_simple (element_class,
      "Samsung Exynos MFC decoder",
      "Codec/Decoder/Video",
      "Decode video streams via Samsung Exynos",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_mfc_dec_class_init (GstMFCDecClass * klass)
{
  GstVideoDecoderClass *video_decoder_class;

  video_decoder_class = (GstVideoDecoderClass *) klass;

  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_mfc_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_mfc_dec_stop);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_mfc_dec_reset);
  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_mfc_dec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mfc_dec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gst_mfc_dec_debug, "mfcdec", 0,
      "Samsung Exynos MFC Decoder");
}

static void
gst_mfc_dec_init (GstMFCDec * self, GstMFCDecClass * klass)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) self;

  gst_video_decoder_set_packetized (decoder, TRUE);
}

static gboolean
gst_mfc_dec_start (GstVideoDecoder * decoder)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Starting");

  return TRUE;
}

static gboolean
gst_mfc_dec_stop (GstVideoDecoder * video_decoder)
{
  GstMFCDec *self = GST_MFC_DEC (video_decoder);

  GST_DEBUG_OBJECT (self, "Stopping");

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  GST_DEBUG_OBJECT (self, "Stopped");

  return TRUE;
}

static gboolean
gst_mfc_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static gboolean
gst_mfc_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting");

  return TRUE;
}

static GstFlowReturn
gst_mfc_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "Handling frame");

  return ret;
}

