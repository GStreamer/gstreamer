/* VP9
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2008,2009,2010 Entropy Wave Inc
 * Copyright (C) 2010-2013 Sebastian Dröge <slomo@circular-chaos.org>
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
/**
 * SECTION:element-vp9dec
 * @title: vp9dec
 * @see_also: vp9enc, matroskademux
 *
 * This element decodes VP9 streams into raw video.
 * [VP9](http://www.webmproject.org) is a royalty-free video codec maintained by
 * [Google](http://www.google.com/) It's the successor of On2 VP3, which was the
 * base of the Theora video codec.
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.webm ! matroskademux ! vp9dec ! videoconvert ! videoscale ! autovideosink
 * ]| This example pipeline will decode a WebM stream and decodes the VP9 video.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VP9_DECODER

#include <string.h>

#include "gstvpxelements.h"
#include "gstvp8utils.h"
#include "gstvp9dec.h"

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

GST_DEBUG_CATEGORY_STATIC (gst_vp9dec_debug);
#define GST_CAT_DEFAULT gst_vp9dec_debug

#define VP9_DECODER_VIDEO_TAG "VP9 video"

static void gst_vp9_dec_set_stream_info (GstVPXDec * dec,
    vpx_codec_stream_info_t * stream_info);
static gboolean gst_vp9_dec_get_valid_format (GstVPXDec * dec,
    vpx_image_t * img, GstVideoFormat * fmt);
static void gst_vp9_dec_handle_resolution_change (GstVPXDec * dec,
    vpx_image_t * img, GstVideoFormat fmt);
static gboolean gst_vp9_dec_get_needs_sync_point (GstVPXDec * dec);

static GstStaticPadTemplate gst_vp9_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9")
    );

#define GST_VP9_DEC_VIDEO_FORMATS_8BIT "I420, YV12, Y42B, Y444, GBR"
#define GST_VP9_DEC_VIDEO_FORMATS_HIGHBIT \
    "I420_10LE, I420_12LE, I422_10LE, I422_12LE, Y444_10LE, Y444_12LE, GBR_10LE, GBR_12LE"

#define parent_class gst_vp9_dec_parent_class
G_DEFINE_TYPE (GstVP9Dec, gst_vp9_dec, GST_TYPE_VPX_DEC);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (vp9dec, "vp9dec", GST_RANK_PRIMARY,
    gst_vp9_dec_get_type (), vpx_element_init (plugin));

static GstCaps *
gst_vp9_dec_get_src_caps (void)
{
#define CAPS_8BIT GST_VIDEO_CAPS_MAKE ("{ " GST_VP9_DEC_VIDEO_FORMATS_8BIT " }")
#define CAPS_HIGHBIT GST_VIDEO_CAPS_MAKE ( "{ " GST_VP9_DEC_VIDEO_FORMATS_8BIT ", " \
    GST_VP9_DEC_VIDEO_FORMATS_HIGHBIT "}")

  return gst_caps_from_string ((vpx_codec_get_caps (&vpx_codec_vp9_dx_algo)
          & VPX_CODEC_CAP_HIGHBITDEPTH) ? CAPS_HIGHBIT : CAPS_8BIT);
}

static void
gst_vp9_dec_class_init (GstVP9DecClass * klass)
{
  GstElementClass *element_class;
  GstVPXDecClass *vpx_class;
  GstCaps *caps;

  element_class = GST_ELEMENT_CLASS (klass);
  vpx_class = GST_VPX_DEC_CLASS (klass);

  caps = gst_vp9_dec_get_src_caps ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
  gst_clear_caps (&caps);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vp9_dec_sink_template);

  gst_element_class_set_static_metadata (element_class,
      "On2 VP9 Decoder",
      "Codec/Decoder/Video",
      "Decode VP9 video streams", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  vpx_class->video_codec_tag = VP9_DECODER_VIDEO_TAG;
  vpx_class->codec_algo = &vpx_codec_vp9_dx_algo;
  vpx_class->set_stream_info = GST_DEBUG_FUNCPTR (gst_vp9_dec_set_stream_info);
  vpx_class->get_frame_format =
      GST_DEBUG_FUNCPTR (gst_vp9_dec_get_valid_format);
  vpx_class->handle_resolution_change =
      GST_DEBUG_FUNCPTR (gst_vp9_dec_handle_resolution_change);
  vpx_class->get_needs_sync_point =
      GST_DEBUG_FUNCPTR (gst_vp9_dec_get_needs_sync_point);

  GST_DEBUG_CATEGORY_INIT (gst_vp9dec_debug, "vp9dec", 0, "VP9 Decoder");
}

static void
gst_vp9_dec_init (GstVP9Dec * gst_vp9_dec)
{
  GST_DEBUG_OBJECT (gst_vp9_dec, "gst_vp9_dec_init");
}

static void
gst_vp9_dec_set_stream_info (GstVPXDec * dec,
    vpx_codec_stream_info_t * stream_info)
{
  /* FIXME: peek_stream_info() does not return valid values, take input caps */
  stream_info->w = dec->input_state->info.width;
  stream_info->h = dec->input_state->info.height;
  return;
}

static gboolean
gst_vp9_dec_get_valid_format (GstVPXDec * dec, vpx_image_t * img,
    GstVideoFormat * fmt)
{
  switch ((gst_vpx_img_fmt_t) img->fmt) {
    case GST_VPX_IMG_FMT_I420:
      *fmt = GST_VIDEO_FORMAT_I420;
      return TRUE;

    case GST_VPX_IMG_FMT_YV12:
      *fmt = GST_VIDEO_FORMAT_YV12;
      return TRUE;

    case GST_VPX_IMG_FMT_I422:
      *fmt = GST_VIDEO_FORMAT_Y42B;
      return TRUE;

    case GST_VPX_IMG_FMT_I444:
      if (img->cs == VPX_CS_SRGB)
        *fmt = GST_VIDEO_FORMAT_GBR;
      else
        *fmt = GST_VIDEO_FORMAT_Y444;
      return TRUE;
    case GST_VPX_IMG_FMT_I440:
      /* Planar, half height, full width U/V */
      GST_FIXME_OBJECT (dec, "Please add a 4:4:0 planar frame format");
      GST_ELEMENT_WARNING (dec, STREAM, NOT_IMPLEMENTED,
          (NULL), ("Unsupported frame format - 4:4:0 planar"));
      return FALSE;
    case GST_VPX_IMG_FMT_I42016:
      if (img->bit_depth == 10) {
        *fmt = GST_VIDEO_FORMAT_I420_10LE;
        return TRUE;
      } else if (img->bit_depth == 12) {
        *fmt = GST_VIDEO_FORMAT_I420_12LE;
        return TRUE;
      }
      GST_ELEMENT_WARNING (dec, STREAM, NOT_IMPLEMENTED,
          (NULL), ("Unsupported frame format - %d-bit 4:2:0 planar",
              img->bit_depth));
      return FALSE;
    case GST_VPX_IMG_FMT_I42216:
      if (img->bit_depth == 10) {
        *fmt = GST_VIDEO_FORMAT_I422_10LE;
        return TRUE;
      } else if (img->bit_depth == 12) {
        *fmt = GST_VIDEO_FORMAT_I422_12LE;
        return TRUE;
      }
      GST_ELEMENT_WARNING (dec, STREAM, NOT_IMPLEMENTED,
          (NULL), ("Unsupported frame format - %d-bit 4:2:2 planar",
              img->bit_depth));
      return FALSE;
    case GST_VPX_IMG_FMT_I44416:
      if (img->cs == VPX_CS_SRGB) {
        if (img->bit_depth == 10) {
          *fmt = GST_VIDEO_FORMAT_GBR_10LE;
          return TRUE;
        } else if (img->bit_depth == 12) {
          *fmt = GST_VIDEO_FORMAT_GBR_12LE;
          return TRUE;
        }
      } else {
        if (img->bit_depth == 10) {
          *fmt = GST_VIDEO_FORMAT_Y444_10LE;
          return TRUE;
        } else if (img->bit_depth == 12) {
          *fmt = GST_VIDEO_FORMAT_Y444_12LE;
          return TRUE;
        }
      }
      GST_ELEMENT_WARNING (dec, STREAM, NOT_IMPLEMENTED,
          (NULL), ("Unsupported frame format - %d-bit 4:4:4 planar",
              img->bit_depth));
      return FALSE;
    case GST_VPX_IMG_FMT_I44016:
      GST_FIXME_OBJECT (dec, "Please add 16-bit 4:4:0 planar frame format");
      GST_ELEMENT_WARNING (dec, STREAM, NOT_IMPLEMENTED,
          (NULL), ("Unsupported frame format - 16-bit 4:4:0 planar"));
      return FALSE;
    default:
      return FALSE;
  }
}

static void
gst_vp9_dec_handle_resolution_change (GstVPXDec * dec, vpx_image_t * img,
    GstVideoFormat fmt)
{
  GstVPXDecClass *vpxclass = GST_VPX_DEC_GET_CLASS (dec);

  if (!dec->output_state || dec->output_state->info.finfo->format != fmt ||
      dec->output_state->info.width != img->d_w ||
      dec->output_state->info.height != img->d_h) {
    gboolean send_tags = !dec->output_state;

    if (dec->output_state)
      gst_video_codec_state_unref (dec->output_state);

    dec->output_state =
        gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
        fmt, img->d_w, img->d_h, dec->input_state);
    gst_video_decoder_negotiate (GST_VIDEO_DECODER (dec));

    if (send_tags)
      vpxclass->send_tags (dec);
  }
}

static gboolean
gst_vp9_dec_get_needs_sync_point (GstVPXDec * dec)
{
  return TRUE;
}

#endif /* HAVE_VP9_DECODER */
