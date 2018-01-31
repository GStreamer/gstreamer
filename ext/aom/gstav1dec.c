/* GStreamer
 * Copyright (C) <2017> Sean DuBois <sean@siobud.com>
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
/**
 * SECTION:element-av1dec
 *
 * AV1 Decoder.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.webm ! matroskademux ! av1dec ! videoconvert ! videoscale ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstav1dec.h"

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_av1_dec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1")
    );

static GstStaticPadTemplate gst_av1_dec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) \"I420\", "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 4, MAX ], " "height = (int) [ 4, MAX ]")
    );

GST_DEBUG_CATEGORY_STATIC (av1_dec_debug);
#define GST_CAT_DEFAULT av1_dec_debug

static void gst_av1_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_av1_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_av1_dec_start (GstVideoDecoder * dec);
static gboolean gst_av1_dec_stop (GstVideoDecoder * dec);
static gboolean gst_av1_dec_set_format (GstVideoDecoder * dec,
    GstVideoCodecState * state);
static gboolean gst_av1_dec_flush (GstVideoDecoder * dec);
static GstFlowReturn
gst_av1_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static void gst_av1_dec_image_to_buffer (GstAV1Dec * dec,
    const aom_image_t * img, GstBuffer * buffer);
static GstFlowReturn gst_av1_dec_open_codec (GstAV1Dec * av1dec,
    GstVideoCodecFrame * frame);

#define gst_av1_dec_parent_class parent_class
G_DEFINE_TYPE (GstAV1Dec, gst_av1_dec, GST_TYPE_VIDEO_DECODER);

static void
gst_av1_dec_class_init (GstAV1DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoDecoderClass *vdec_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  vdec_class = (GstVideoDecoderClass *) klass;


  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_av1_dec_set_property;
  gobject_class->get_property = gst_av1_dec_get_property;

  gst_element_class_add_static_pad_template (element_class,
      &gst_av1_dec_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_av1_dec_sink_pad_template);
  gst_element_class_set_static_metadata (element_class, "AV1 Decoder",
      "Codec/Decoder/Video", "Decode AV1 video streams",
      "Sean DuBois <sean@siobud.com>");

  vdec_class->start = GST_DEBUG_FUNCPTR (gst_av1_dec_start);
  vdec_class->stop = GST_DEBUG_FUNCPTR (gst_av1_dec_stop);
  vdec_class->flush = GST_DEBUG_FUNCPTR (gst_av1_dec_flush);

  vdec_class->set_format = GST_DEBUG_FUNCPTR (gst_av1_dec_set_format);
  vdec_class->handle_frame = GST_DEBUG_FUNCPTR (gst_av1_dec_handle_frame);

  klass->codec_algo = &aom_codec_av1_dx_algo;
  GST_DEBUG_CATEGORY_INIT (av1_dec_debug, "av1dec", 0, "AV1 decoding element");
}

static void
gst_av1_dec_init (GstAV1Dec * av1dec)
{
  GstVideoDecoder *dec = (GstVideoDecoder *) av1dec;

  GST_DEBUG_OBJECT (dec, "gst_av1_dec_init");
  gst_video_decoder_set_packetized (dec, TRUE);
  gst_video_decoder_set_needs_format (dec, TRUE);
  gst_video_decoder_set_use_default_pad_acceptcaps (dec, TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (dec));
}

static void
gst_av1_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_av1_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_av1_dec_start (GstVideoDecoder * dec)
{
  GstAV1Dec *av1dec = GST_AV1_DEC_CAST (dec);

  av1dec->decoder_inited = FALSE;
  av1dec->output_state = NULL;
  av1dec->input_state = NULL;

  return TRUE;
}

static gboolean
gst_av1_dec_stop (GstVideoDecoder * dec)
{
  GstAV1Dec *av1dec = GST_AV1_DEC_CAST (dec);

  if (av1dec->output_state) {
    gst_video_codec_state_unref (av1dec->output_state);
    av1dec->output_state = NULL;
  }

  if (av1dec->input_state) {
    gst_video_codec_state_unref (av1dec->input_state);
    av1dec->input_state = NULL;
  }

  if (av1dec->decoder_inited) {
    aom_codec_destroy (&av1dec->decoder);
  }
  av1dec->decoder_inited = FALSE;

  return TRUE;
}

static gboolean
gst_av1_dec_set_format (GstVideoDecoder * dec, GstVideoCodecState * state)
{
  GstAV1Dec *av1dec = GST_AV1_DEC_CAST (dec);

  if (av1dec->decoder_inited) {
    aom_codec_destroy (&av1dec->decoder);
  }
  av1dec->decoder_inited = FALSE;

  if (av1dec->output_state) {
    gst_video_codec_state_unref (av1dec->output_state);
    av1dec->output_state = NULL;
  }

  if (av1dec->input_state) {
    gst_video_codec_state_unref (av1dec->input_state);
  }

  av1dec->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static gboolean
gst_av1_dec_flush (GstVideoDecoder * dec)
{
  GstAV1Dec *av1dec = GST_AV1_DEC_CAST (dec);

  if (av1dec->output_state) {
    gst_video_codec_state_unref (av1dec->output_state);
    av1dec->output_state = NULL;
  }

  if (av1dec->decoder_inited) {
    aom_codec_destroy (&av1dec->decoder);
  }
  av1dec->decoder_inited = FALSE;

  return TRUE;
}

static GstFlowReturn
gst_av1_dec_open_codec (GstAV1Dec * av1dec, GstVideoCodecFrame * frame)
{
  aom_codec_err_t status;
  GstAV1DecClass *av1class = GST_AV1_DEC_GET_CLASS (av1dec);

  status = aom_codec_dec_init (&av1dec->decoder, av1class->codec_algo, NULL, 0);
  if (status != AOM_CODEC_OK) {
    GST_ELEMENT_ERROR (av1dec, LIBRARY, INIT,
        ("Failed to initialize AOM decoder"), ("%s", ""));
    return GST_FLOW_ERROR;
  }

  av1dec->decoder_inited = TRUE;
  return GST_FLOW_OK;
}

static void
gst_av1_dec_handle_resolution_change (GstAV1Dec * av1dec, aom_image_t * img,
    GstVideoFormat fmt)
{
  if (!av1dec->output_state ||
      av1dec->output_state->info.finfo->format != fmt ||
      av1dec->output_state->info.width != img->d_w ||
      av1dec->output_state->info.height != img->d_h) {

    if (av1dec->output_state)
      gst_video_codec_state_unref (av1dec->output_state);

    av1dec->output_state =
        gst_video_decoder_set_output_state (GST_VIDEO_DECODER (av1dec),
        fmt, img->d_w, img->d_h, av1dec->input_state);
    gst_video_decoder_negotiate (GST_VIDEO_DECODER (av1dec));
  }


}

static void
gst_av1_dec_image_to_buffer (GstAV1Dec * dec, const aom_image_t * img,
    GstBuffer * buffer)
{
  int deststride, srcstride, height, width, line, comp;
  guint8 *dest, *src;
  GstVideoFrame frame;
  GstVideoInfo *info = &dec->output_state->info;

  if (!gst_video_frame_map (&frame, info, buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (dec, "Could not map video buffer");
    return;
  }

  for (comp = 0; comp < 3; comp++) {
    dest = GST_VIDEO_FRAME_COMP_DATA (&frame, comp);
    src = img->planes[comp];
    width = GST_VIDEO_FRAME_COMP_WIDTH (&frame, comp)
        * GST_VIDEO_FRAME_COMP_PSTRIDE (&frame, comp);
    height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, comp);
    deststride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, comp);
    srcstride = img->stride[comp];

    if (srcstride == deststride) {
      GST_TRACE_OBJECT (dec, "Stride matches. Comp %d: %d, copying full plane",
          comp, srcstride);
      memcpy (dest, src, srcstride * height);
    } else {
      GST_TRACE_OBJECT (dec, "Stride mismatch. Comp %d: %d != %d, copying "
          "line by line.", comp, srcstride, deststride);
      for (line = 0; line < height; line++) {
        memcpy (dest, src, width);
        dest += deststride;
        src += srcstride;
      }
    }
  }

  gst_video_frame_unmap (&frame);
}

static GstFlowReturn
gst_av1_dec_handle_frame (GstVideoDecoder * dec, GstVideoCodecFrame * frame)
{
  GstAV1Dec *av1dec = GST_AV1_DEC_CAST (dec);
  GstFlowReturn ret;
  GstMapInfo minfo;
  aom_codec_err_t status;
  aom_image_t *img;
  aom_codec_iter_t iter = NULL;

  if (!av1dec->decoder_inited) {
    ret = gst_av1_dec_open_codec (av1dec, frame);
    if (ret == GST_FLOW_CUSTOM_SUCCESS_1) {
      gst_video_decoder_drop_frame (dec, frame);
      return GST_FLOW_OK;
    } else if (ret != GST_FLOW_OK) {
      gst_video_codec_frame_unref (frame);
      return ret;
    }
  }

  if (!gst_buffer_map (frame->input_buffer, &minfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (dec, "Failed to map input buffer");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  status = aom_codec_decode (&av1dec->decoder, minfo.data, minfo.size, NULL);

  gst_buffer_unmap (frame->input_buffer, &minfo);

  if (status) {
    GST_ELEMENT_ERROR (av1dec, LIBRARY, INIT,
        ("Failed to decode frame"), ("%s", ""));
    gst_video_codec_frame_unref (frame);
    return ret;
  }

  img = aom_codec_get_frame (&av1dec->decoder, &iter);
  if (img) {
    gst_av1_dec_handle_resolution_change (av1dec, img, GST_VIDEO_FORMAT_I420);

    ret = gst_video_decoder_allocate_output_frame (dec, frame);
    if (ret == GST_FLOW_OK) {
      gst_av1_dec_image_to_buffer (av1dec, img, frame->output_buffer);
      ret = gst_video_decoder_finish_frame (dec, frame);
    } else {
      gst_video_decoder_drop_frame (dec, frame);
    }

    aom_img_free (img);
    while ((img = aom_codec_get_frame (&av1dec->decoder, &iter))) {
      GST_WARNING_OBJECT (dec, "Multiple decoded frames... dropping");
      aom_img_free (img);
    }
  } else {
    GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);
    gst_video_decoder_finish_frame (dec, frame);
  }


  return ret;
}
