/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2008,2009,2010 Entropy Wave Inc
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 *
 */
/**
 * SECTION:element-vp8dec
 * @see_also: vp8enc, matroskademux
 *
 * This element decodes VP8 streams into raw video.
 * <ulink url="http://www.webmproject.org">VP8</ulink> is a royalty-free
 * video codec maintained by <ulink url="http://www.google.com/">Google
 * </ulink>. It's the successor of On2 VP3, which was the base of the
 * Theora video codec.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v filesrc location=videotestsrc.webm ! matroskademux ! vp8dec ! xvimagesink
 * ]| This example pipeline will decode a WebM stream and decodes the VP8 video.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VP8_DECODER

#include <string.h>

#include "gstvp8dec.h"
#include "gstvp8utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_vp8dec_debug);
#define GST_CAT_DEFAULT gst_vp8dec_debug

#define DEFAULT_POST_PROCESSING FALSE
#define DEFAULT_POST_PROCESSING_FLAGS (VP8_DEBLOCK | VP8_DEMACROBLOCK)
#define DEFAULT_DEBLOCKING_LEVEL 4
#define DEFAULT_NOISE_LEVEL 0

enum
{
  PROP_0,
  PROP_POST_PROCESSING,
  PROP_POST_PROCESSING_FLAGS,
  PROP_DEBLOCKING_LEVEL,
  PROP_NOISE_LEVEL
};

#define C_FLAGS(v) ((guint) v)
#define GST_VP8_DEC_TYPE_POST_PROCESSING_FLAGS (gst_vp8_dec_post_processing_flags_get_type())
static GType
gst_vp8_dec_post_processing_flags_get_type (void)
{
  static const GFlagsValue values[] = {
    {C_FLAGS (VP8_DEBLOCK), "Deblock", "deblock"},
    {C_FLAGS (VP8_DEMACROBLOCK), "Demacroblock", "demacroblock"},
    {C_FLAGS (VP8_ADDNOISE), "Add noise", "addnoise"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_flags_register_static ("GstVP8DecPostProcessingFlags", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

#undef C_FLAGS

static void gst_vp8_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vp8_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_vp8_dec_start (GstBaseVideoDecoder * decoder);
static gboolean gst_vp8_dec_stop (GstBaseVideoDecoder * decoder);
static gboolean gst_vp8_dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state);
static gboolean gst_vp8_dec_reset (GstBaseVideoDecoder * decoder);
static GstFlowReturn gst_vp8_dec_parse_data (GstBaseVideoDecoder * decoder,
    gboolean at_eos);
static GstFlowReturn gst_vp8_dec_handle_frame (GstBaseVideoDecoder * decoder,
    GstVideoFrame * frame);

static GstStaticPadTemplate gst_vp8_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8")
    );

static GstStaticPadTemplate gst_vp8_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

GST_BOILERPLATE (GstVP8Dec, gst_vp8_dec, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER);

static void
gst_vp8_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_vp8_dec_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_vp8_dec_sink_template);

  gst_element_class_set_details_simple (element_class,
      "On2 VP8 Decoder",
      "Codec/Decoder/Video",
      "Decode VP8 video streams", "David Schleef <ds@entropywave.com>");
}

static void
gst_vp8_dec_class_init (GstVP8DecClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseVideoDecoderClass *base_video_decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  base_video_decoder_class = GST_BASE_VIDEO_DECODER_CLASS (klass);

  gobject_class->set_property = gst_vp8_dec_set_property;
  gobject_class->get_property = gst_vp8_dec_get_property;

  g_object_class_install_property (gobject_class, PROP_POST_PROCESSING,
      g_param_spec_boolean ("post-processing", "Post Processing",
          "Enable post processing", DEFAULT_POST_PROCESSING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_POST_PROCESSING_FLAGS,
      g_param_spec_flags ("post-processing-flags", "Post Processing Flags",
          "Flags to control post processing",
          GST_VP8_DEC_TYPE_POST_PROCESSING_FLAGS, DEFAULT_POST_PROCESSING_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEBLOCKING_LEVEL,
      g_param_spec_uint ("deblocking-level", "Deblocking Level",
          "Deblocking level",
          0, 16, DEFAULT_DEBLOCKING_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NOISE_LEVEL,
      g_param_spec_uint ("noise-level", "Noise Level",
          "Noise level",
          0, 16, DEFAULT_NOISE_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  base_video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_vp8_dec_start);
  base_video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vp8_dec_stop);
  base_video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_vp8_dec_reset);
  base_video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_vp8_dec_set_format);
  base_video_decoder_class->parse_data =
      GST_DEBUG_FUNCPTR (gst_vp8_dec_parse_data);
  base_video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vp8_dec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gst_vp8dec_debug, "vp8dec", 0, "VP8 Decoder");
}

static void
gst_vp8_dec_init (GstVP8Dec * gst_vp8_dec, GstVP8DecClass * klass)
{
  GstBaseVideoDecoder *decoder = (GstBaseVideoDecoder *) gst_vp8_dec;

  GST_DEBUG_OBJECT (gst_vp8_dec, "gst_vp8_dec_init");
  decoder->packetized = TRUE;
  gst_vp8_dec->post_processing = DEFAULT_POST_PROCESSING;
  gst_vp8_dec->post_processing_flags = DEFAULT_POST_PROCESSING_FLAGS;
  gst_vp8_dec->deblocking_level = DEFAULT_DEBLOCKING_LEVEL;
  gst_vp8_dec->noise_level = DEFAULT_NOISE_LEVEL;
}

static void
gst_vp8_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVP8Dec *dec;

  g_return_if_fail (GST_IS_VP8_DEC (object));
  dec = GST_VP8_DEC (object);

  GST_DEBUG_OBJECT (object, "gst_vp8_dec_set_property");
  switch (prop_id) {
    case PROP_POST_PROCESSING:
      dec->post_processing = g_value_get_boolean (value);
      break;
    case PROP_POST_PROCESSING_FLAGS:
      dec->post_processing_flags = g_value_get_flags (value);
      break;
    case PROP_DEBLOCKING_LEVEL:
      dec->deblocking_level = g_value_get_uint (value);
      break;
    case PROP_NOISE_LEVEL:
      dec->noise_level = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vp8_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVP8Dec *dec;

  g_return_if_fail (GST_IS_VP8_DEC (object));
  dec = GST_VP8_DEC (object);

  switch (prop_id) {
    case PROP_POST_PROCESSING:
      g_value_set_boolean (value, dec->post_processing);
      break;
    case PROP_POST_PROCESSING_FLAGS:
      g_value_set_flags (value, dec->post_processing_flags);
      break;
    case PROP_DEBLOCKING_LEVEL:
      g_value_set_uint (value, dec->deblocking_level);
      break;
    case PROP_NOISE_LEVEL:
      g_value_set_uint (value, dec->noise_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vp8_dec_start (GstBaseVideoDecoder * decoder)
{
  GstVP8Dec *gst_vp8_dec = GST_VP8_DEC (decoder);

  GST_DEBUG_OBJECT (gst_vp8_dec, "start");
  gst_vp8_dec->decoder_inited = FALSE;

  return TRUE;
}

static gboolean
gst_vp8_dec_stop (GstBaseVideoDecoder * base_video_decoder)
{
  GstVP8Dec *gst_vp8_dec = GST_VP8_DEC (base_video_decoder);

  GST_DEBUG_OBJECT (gst_vp8_dec, "stop");
  if (gst_vp8_dec->decoder_inited)
    vpx_codec_destroy (&gst_vp8_dec->decoder);
  gst_vp8_dec->decoder_inited = FALSE;
  return TRUE;
}

static gboolean
gst_vp8_dec_set_format (GstBaseVideoDecoder * decoder, GstVideoState * state)
{
  GstVP8Dec *gst_vp8_dec = GST_VP8_DEC (decoder);

  GST_DEBUG_OBJECT (gst_vp8_dec, "set_format");
  gst_vp8_dec->decoder_inited = FALSE;

  return TRUE;
}

static gboolean
gst_vp8_dec_reset (GstBaseVideoDecoder * base_video_decoder)
{
  GstVP8Dec *decoder;

  GST_DEBUG_OBJECT (base_video_decoder, "reset");

  decoder = GST_VP8_DEC (base_video_decoder);

  if (decoder->decoder_inited)
    vpx_codec_destroy (&decoder->decoder);
  decoder->decoder_inited = FALSE;

  return TRUE;
}

static GstFlowReturn
gst_vp8_dec_parse_data (GstBaseVideoDecoder * decoder, gboolean at_eos)
{
  return GST_FLOW_OK;
}

static void
gst_vp8_dec_send_tags (GstVP8Dec * dec)
{
  GstTagList *list;

  list = gst_tag_list_new ();
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_VIDEO_CODEC, "VP8 video", NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (dec),
      GST_BASE_VIDEO_CODEC_SRC_PAD (dec), list);
}

static void
gst_vp8_dec_image_to_buffer (GstVP8Dec * dec, const vpx_image_t * img,
    GstBuffer * buffer)
{
  int stride, w, h, i;
  guint8 *d;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (dec)->state;

  d = GST_BUFFER_DATA (buffer) +
      gst_video_format_get_component_offset (state->format, 0,
      state->width, state->height);
  stride = gst_video_format_get_row_stride (state->format, 0, state->width);
  h = gst_video_format_get_component_height (state->format, 0, state->height);
  h = MIN (h, img->h);
  w = gst_video_format_get_component_width (state->format, 0, state->width);
  w = MIN (w, img->w);

  for (i = 0; i < h; i++)
    memcpy (d + i * stride,
        img->planes[VPX_PLANE_Y] + i * img->stride[VPX_PLANE_Y], w);

  d = GST_BUFFER_DATA (buffer) +
      gst_video_format_get_component_offset (state->format, 1,
      state->width, state->height);
  stride = gst_video_format_get_row_stride (state->format, 1, state->width);
  h = gst_video_format_get_component_height (state->format, 1, state->height);
  h = MIN (h, img->h >> img->y_chroma_shift);
  w = gst_video_format_get_component_width (state->format, 1, state->width);
  w = MIN (w, img->w >> img->x_chroma_shift);
  for (i = 0; i < h; i++)
    memcpy (d + i * stride,
        img->planes[VPX_PLANE_U] + i * img->stride[VPX_PLANE_U], w);

  d = GST_BUFFER_DATA (buffer) +
      gst_video_format_get_component_offset (state->format, 2,
      state->width, state->height);
  /* Same stride, height, width as above */
  for (i = 0; i < h; i++)
    memcpy (d + i * stride,
        img->planes[VPX_PLANE_V] + i * img->stride[VPX_PLANE_V], w);
}

static GstFlowReturn
gst_vp8_dec_handle_frame (GstBaseVideoDecoder * decoder, GstVideoFrame * frame)
{
  GstVP8Dec *dec;
  GstFlowReturn ret = GST_FLOW_OK;
  vpx_codec_err_t status;
  vpx_codec_iter_t iter = NULL;
  vpx_image_t *img;
  long decoder_deadline = 0;
  GstClockTimeDiff deadline;

  GST_DEBUG_OBJECT (decoder, "handle_frame");

  dec = GST_VP8_DEC (decoder);

  if (!dec->decoder_inited) {
    int flags = 0;
    vpx_codec_stream_info_t stream_info;
    vpx_codec_caps_t caps;
    GstVideoState *state = &GST_BASE_VIDEO_CODEC (dec)->state;

    memset (&stream_info, 0, sizeof (stream_info));
    stream_info.sz = sizeof (stream_info);

    status = vpx_codec_peek_stream_info (&vpx_codec_vp8_dx_algo,
        GST_BUFFER_DATA (frame->sink_buffer),
        GST_BUFFER_SIZE (frame->sink_buffer), &stream_info);

    if (status != VPX_CODEC_OK || !stream_info.is_kf) {
      GST_WARNING_OBJECT (decoder, "No keyframe, skipping");
      gst_base_video_decoder_finish_frame (decoder, frame);
      return GST_FLOW_OK;
    }

    state->width = stream_info.w;
    state->height = stream_info.h;
    state->format = GST_VIDEO_FORMAT_I420;
    if (state->par_n == 0 || state->par_d == 0) {
      state->par_n = 1;
      state->par_d = 1;
    }
    gst_vp8_dec_send_tags (dec);
    gst_base_video_decoder_set_src_caps (decoder);

    caps = vpx_codec_get_caps (&vpx_codec_vp8_dx_algo);

    if (dec->post_processing) {
      if (!(caps & VPX_CODEC_CAP_POSTPROC)) {
        GST_WARNING_OBJECT (decoder,
            "Decoder does not support post processing");
      } else {
        flags |= VPX_CODEC_USE_POSTPROC;
      }
    }

    status =
        vpx_codec_dec_init (&dec->decoder, &vpx_codec_vp8_dx_algo, NULL, flags);
    if (status != VPX_CODEC_OK) {
      GST_ELEMENT_ERROR (dec, LIBRARY, INIT,
          ("Failed to initialize VP8 decoder"), ("%s",
              gst_vpx_error_name (status)));
      return GST_FLOW_ERROR;
    }

    if ((caps & VPX_CODEC_CAP_POSTPROC) && dec->post_processing) {
      vp8_postproc_cfg_t pp_cfg = { 0, };

      pp_cfg.post_proc_flag = dec->post_processing_flags;
      pp_cfg.deblocking_level = dec->deblocking_level;
      pp_cfg.noise_level = dec->noise_level;

      status = vpx_codec_control (&dec->decoder, VP8_SET_POSTPROC, &pp_cfg);
      if (status != VPX_CODEC_OK) {
        GST_WARNING_OBJECT (dec, "Couldn't set postprocessing settings: %s",
            gst_vpx_error_name (status));
      }
    }

    dec->decoder_inited = TRUE;
  }

  if (!GST_BUFFER_FLAG_IS_SET (frame->sink_buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    gst_base_video_decoder_set_sync_point (decoder);

  deadline = gst_base_video_decoder_get_max_decode_time (decoder, frame);
  if (deadline < 0) {
    decoder_deadline = 1;
  } else if (deadline == G_MAXINT64) {
    decoder_deadline = 0;
  } else {
    decoder_deadline = MAX (1, deadline / GST_MSECOND);
  }

  status = vpx_codec_decode (&dec->decoder,
      GST_BUFFER_DATA (frame->sink_buffer),
      GST_BUFFER_SIZE (frame->sink_buffer), NULL, decoder_deadline);
  if (status) {
    GST_ELEMENT_ERROR (decoder, LIBRARY, ENCODE,
        ("Failed to decode frame"), ("%s", gst_vpx_error_name (status)));
    return GST_FLOW_ERROR;
  }

  img = vpx_codec_get_frame (&dec->decoder, &iter);
  if (img) {
    if (deadline < 0) {
      GST_LOG_OBJECT (dec, "Skipping late frame (%f s past deadline)",
          (double) -deadline / GST_SECOND);
      gst_base_video_decoder_drop_frame (decoder, frame);
    } else {
      ret = gst_base_video_decoder_alloc_src_frame (decoder, frame);

      if (ret == GST_FLOW_OK) {
        gst_vp8_dec_image_to_buffer (dec, img, frame->src_buffer);
        ret = gst_base_video_decoder_finish_frame (decoder, frame);
      } else {
        gst_base_video_decoder_finish_frame (decoder, frame);
      }
    }

    vpx_img_free (img);

    while ((img = vpx_codec_get_frame (&dec->decoder, &iter))) {
      GST_WARNING_OBJECT (decoder, "Multiple decoded frames... dropping");
      vpx_img_free (img);
    }
  } else {
    /* Invisible frame */
    gst_base_video_decoder_finish_frame (decoder, frame);
  }

  return ret;
}

#endif /* HAVE_VP8_DECODER */
