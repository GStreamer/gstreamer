/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2013> Luciana Fujii <luciana.fujii@collabora.co.uk>
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
 * SECTION:element-rsvgdec
 * @title: rsvgdec
 *
 * This elements renders SVG graphics.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 filesrc location=image.svg ! rsvgdec ! imagefreeze ! videoconvert ! autovideosink
 * ]| render and show a svg image.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrsvgdec.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (rsvgdec_debug);
#define GST_CAT_DEFAULT rsvgdec_debug

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/svg+xml; image/svg"));

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_RSVG_VIDEO_CAPS GST_VIDEO_CAPS_MAKE ("BGRA")
#define GST_RSVG_VIDEO_FORMAT GST_VIDEO_FORMAT_BGRA
#else
#define GST_RSVG_VIDEO_CAPS GST_VIDEO_CAPS_MAKE ("ARGB")
#define GST_RSVG_VIDEO_FORMAT GST_VIDEO_FORMAT_ARGB
#endif

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_RSVG_VIDEO_CAPS));

#define gst_rsv_dec_parent_class parent_class
G_DEFINE_TYPE (GstRsvgDec, gst_rsvg_dec, GST_TYPE_VIDEO_DECODER);
GST_ELEMENT_REGISTER_DEFINE (rsvgdec, "rsvgdec", GST_RANK_PRIMARY,
    GST_TYPE_RSVG_DEC);

static gboolean gst_rsvg_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_rsvg_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_rsvg_dec_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static GstFlowReturn gst_rsvg_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_rsvg_decode_image (GstRsvgDec * rsvg,
    GstBuffer * buffer, GstVideoCodecFrame * frame);

static void
gst_rsvg_dec_class_init (GstRsvgDecClass * klass)
{
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (rsvgdec_debug, "rsvgdec", 0, "RSVG decoder");

  gst_element_class_set_static_metadata (element_class,
      "SVG image decoder", "Codec/Decoder/Image",
      "Uses librsvg to decode SVG images",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);

  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_rsvg_dec_stop);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_rsvg_dec_set_format);
  video_decoder_class->parse = GST_DEBUG_FUNCPTR (gst_rsvg_dec_parse);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_rsvg_dec_handle_frame);
}

static void
gst_rsvg_dec_init (GstRsvgDec * rsvg)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (rsvg);
  gst_video_decoder_set_packetized (decoder, FALSE);
  gst_video_decoder_set_use_default_pad_acceptcaps (GST_VIDEO_DECODER_CAST
      (rsvg), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (rsvg));
}

#define CAIRO_UNPREMULTIPLY(a,r,g,b) G_STMT_START { \
  b = (a > 0) ? MIN ((b * 255 + a / 2) / a, 255) : 0; \
  g = (a > 0) ? MIN ((g * 255 + a / 2) / a, 255) : 0; \
  r = (a > 0) ? MIN ((r * 255 + a / 2) / a, 255) : 0; \
} G_STMT_END

static void
gst_rsvg_decode_unpremultiply (guint8 * data, gint width, gint height)
{
  gint i, j;
  guint a;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      a = data[3];
      data[0] = (a > 0) ? MIN ((data[0] * 255 + a / 2) / a, 255) : 0;
      data[1] = (a > 0) ? MIN ((data[1] * 255 + a / 2) / a, 255) : 0;
      data[2] = (a > 0) ? MIN ((data[2] * 255 + a / 2) / a, 255) : 0;
#else
      a = data[0];
      data[1] = (a > 0) ? MIN ((data[1] * 255 + a / 2) / a, 255) : 0;
      data[2] = (a > 0) ? MIN ((data[2] * 255 + a / 2) / a, 255) : 0;
      data[3] = (a > 0) ? MIN ((data[3] * 255 + a / 2) / a, 255) : 0;
#endif
      data += 4;
    }
  }
}

static GstFlowReturn
gst_rsvg_decode_image (GstRsvgDec * rsvg, GstBuffer * buffer,
    GstVideoCodecFrame * frame)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (rsvg);
  GstFlowReturn ret = GST_FLOW_OK;
  cairo_t *cr;
  cairo_surface_t *surface;
  RsvgHandle *handle;
  GError *error = NULL;
  RsvgDimensionData dimension;
  gdouble scalex, scaley;
  GstMapInfo minfo;
  GstVideoFrame vframe;
  GstVideoCodecState *output_state;

  GST_LOG_OBJECT (rsvg, "parsing svg");

  if (!gst_buffer_map (buffer, &minfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (rsvg, "Failed to get SVG image");
    return GST_FLOW_ERROR;
  }
  handle = rsvg_handle_new_from_data (minfo.data, minfo.size, &error);
  if (!handle) {
    GST_ERROR_OBJECT (rsvg, "Failed to parse SVG image: %s", error->message);
    g_error_free (error);
    return GST_FLOW_ERROR;
  }

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
  rsvg_handle_get_dimensions (handle, &dimension);
  G_GNUC_END_IGNORE_DEPRECATIONS;

  output_state = gst_video_decoder_get_output_state (decoder);
  if ((output_state == NULL)
      || rsvg->dimension.width != dimension.width
      || rsvg->dimension.height != dimension.height) {
    GstCaps *peer_caps;
    GstCaps *source_caps;
    GstCaps *templ_caps;

    templ_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (rsvg));
    peer_caps =
        gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (rsvg), templ_caps);

    GST_DEBUG_OBJECT (rsvg,
        "Trying to negotiate for SVG resolution %ux%u with downstream caps %"
        GST_PTR_FORMAT, dimension.width, dimension.height, peer_caps);

    source_caps = gst_caps_make_writable (g_steal_pointer (&templ_caps));
    gst_caps_set_simple (source_caps, "width", G_TYPE_INT, dimension.width,
        "height", G_TYPE_INT, dimension.height, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, 1, 1, NULL);

    if (gst_caps_can_intersect (source_caps, peer_caps)
        || gst_caps_is_empty (peer_caps)) {
      GST_DEBUG_OBJECT (rsvg, "Using input resolution");
      if (output_state)
        gst_video_codec_state_unref (output_state);
      output_state =
          gst_video_decoder_set_output_state (decoder, GST_RSVG_VIDEO_FORMAT,
          dimension.width, dimension.height, rsvg->input_state);
      output_state->info.par_n = 1;
      output_state->info.par_d = 1;
    } else {
      GstVideoInfo info;
      GstStructure *s;

      peer_caps = gst_caps_make_writable (peer_caps);
      peer_caps = gst_caps_truncate (peer_caps);
      s = gst_caps_get_structure (peer_caps, 0);
      gst_structure_fixate_field_nearest_int (s, "width", dimension.width);
      gst_structure_fixate_field_nearest_int (s, "height", dimension.height);
      peer_caps = gst_caps_fixate (peer_caps);

      gst_video_info_from_caps (&info, peer_caps);

      if (output_state)
        gst_video_codec_state_unref (output_state);
      output_state =
          gst_video_decoder_set_output_state (decoder, GST_RSVG_VIDEO_FORMAT,
          info.width, info.height, rsvg->input_state);
      GST_DEBUG_OBJECT (rsvg,
          "Using resolution %ux%u with pixel-aspect-ratio %u/%u",
          output_state->info.width, output_state->info.height,
          output_state->info.par_n, output_state->info.par_d);
    }

    gst_clear_caps (&source_caps);
    gst_clear_caps (&peer_caps);

    rsvg->dimension = dimension;
  }

  ret = gst_video_decoder_allocate_output_frame (decoder, frame);

  if (ret != GST_FLOW_OK) {
    g_object_unref (handle);
    gst_video_codec_state_unref (output_state);
    GST_ERROR_OBJECT (rsvg, "Buffer allocation failed %s",
        gst_flow_get_name (ret));
    return ret;
  }

  GST_LOG_OBJECT (rsvg, "render image at %d x %d",
      GST_VIDEO_INFO_HEIGHT (&output_state->info),
      GST_VIDEO_INFO_WIDTH (&output_state->info));


  if (!gst_video_frame_map (&vframe,
          &output_state->info, frame->output_buffer, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (rsvg, "Failed to get SVG image");
    g_object_unref (handle);
    gst_video_codec_state_unref (output_state);
    return GST_FLOW_ERROR;
  }
  surface =
      cairo_image_surface_create_for_data (GST_VIDEO_FRAME_PLANE_DATA (&vframe,
          0), CAIRO_FORMAT_ARGB32, GST_VIDEO_FRAME_WIDTH (&vframe),
      GST_VIDEO_FRAME_HEIGHT (&vframe), GST_VIDEO_FRAME_PLANE_STRIDE (&vframe,
          0));

  cr = cairo_create (surface);
  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
  cairo_paint (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

  scalex = scaley = 1.0;
  if (GST_VIDEO_INFO_WIDTH (&output_state->info) != dimension.width) {
    scalex =
        ((gdouble) GST_VIDEO_INFO_WIDTH (&output_state->info)) /
        ((gdouble) dimension.width);
  }
  if (GST_VIDEO_INFO_HEIGHT (&output_state->info) != dimension.height) {
    scaley =
        ((gdouble) GST_VIDEO_INFO_HEIGHT (&output_state->info)) /
        ((gdouble) dimension.height);
  }

  cairo_scale (cr, scalex, scaley);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
  rsvg_handle_render_cairo (handle, cr);
  G_GNUC_END_IGNORE_DEPRECATIONS;

  g_object_unref (handle);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  /* Now unpremultiply Cairo's ARGB to match GStreamer's */
  gst_rsvg_decode_unpremultiply (GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0),
      GST_VIDEO_FRAME_WIDTH (&vframe), GST_VIDEO_FRAME_HEIGHT (&vframe));

  gst_video_codec_state_unref (output_state);
  gst_buffer_unmap (buffer, &minfo);
  gst_video_frame_unmap (&vframe);

  return ret;
}


static gboolean
gst_rsvg_dec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (decoder);

  if (rsvg->input_state)
    gst_video_codec_state_unref (rsvg->input_state);
  rsvg->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static GstFlowReturn
gst_rsvg_dec_parse (GstVideoDecoder * decoder, GstVideoCodecFrame * frame,
    GstAdapter * adapter, gboolean at_eos)
{
  gboolean completed = FALSE;
  const guint8 *data;
  guint size;
  guint i;

  GST_LOG_OBJECT (decoder, "parse start");
  size = gst_adapter_available (adapter);

  /* "<svg></svg>" */
  if (size < 5 + 6)
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;

  data = gst_adapter_map (adapter, size);
  if (data == NULL) {
    GST_ERROR_OBJECT (decoder, "Unable to map memory");
    return GST_FLOW_ERROR;
  }
  for (i = 0; i < size - 4; i++) {
    if (memcmp (data + i, "<svg", 4) == 0) {
      gst_adapter_flush (adapter, i);

      size = gst_adapter_available (adapter);
      if (size < 5 + 6)
        return GST_VIDEO_DECODER_FLOW_NEED_DATA;
      data = gst_adapter_map (adapter, size);
      if (data == NULL) {
        GST_ERROR_OBJECT (decoder, "Unable to map memory");
        return GST_FLOW_ERROR;
      }
      break;
    }
  }
  /* If start wasn't found: */
  if (i == size - 4) {
    gst_adapter_flush (adapter, size - 4);
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  for (i = size - 6; i >= 5; i--) {
    if (memcmp (data + i, "</svg>", 6) == 0) {
      completed = TRUE;
      size = i + 6;
      break;
    }
    if (memcmp (data + i, "</svg:svg>", 10) == 0) {
      completed = TRUE;
      size = i + 10;
      break;
    }
  }

  if (completed) {

    GST_LOG_OBJECT (decoder, "have complete svg of %u bytes", size);

    gst_video_decoder_add_to_frame (decoder, size);
    return gst_video_decoder_have_frame (decoder);
  }
  return GST_VIDEO_DECODER_FLOW_NEED_DATA;
}

static GstFlowReturn
gst_rsvg_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (decoder);
  gboolean ret;

  ret = gst_rsvg_decode_image (rsvg, frame->input_buffer, frame);
  switch (ret) {
    case GST_FLOW_OK:
      ret = gst_video_decoder_finish_frame (decoder, frame);
      break;
    default:
      gst_video_codec_frame_unref (frame);
      break;
  }

  GST_LOG_OBJECT (rsvg, "Handle frame done");
  return ret;
}

static gboolean
gst_rsvg_dec_stop (GstVideoDecoder * decoder)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (decoder);

  if (rsvg->input_state) {
    gst_video_codec_state_unref (rsvg->input_state);
    rsvg->input_state = NULL;
  }

  memset (&rsvg->dimension, 0, sizeof (RsvgDimensionData));

  return TRUE;
}
