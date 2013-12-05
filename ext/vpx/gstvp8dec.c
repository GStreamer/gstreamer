/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2008,2009,2010 Entropy Wave Inc
 * Copyright (C) 2010-2012 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

GST_DEBUG_CATEGORY_STATIC (gst_vp8dec_debug);
#define GST_CAT_DEFAULT gst_vp8dec_debug

#define DEFAULT_POST_PROCESSING FALSE
#define DEFAULT_POST_PROCESSING_FLAGS (VP8_DEBLOCK | VP8_DEMACROBLOCK | VP8_MFQE)
#define DEFAULT_DEBLOCKING_LEVEL 4
#define DEFAULT_NOISE_LEVEL 0
#define DEFAULT_THREADS 1

enum
{
  PROP_0,
  PROP_POST_PROCESSING,
  PROP_POST_PROCESSING_FLAGS,
  PROP_DEBLOCKING_LEVEL,
  PROP_NOISE_LEVEL,
  PROP_THREADS
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
    {C_FLAGS (VP8_MFQE), "Multi-frame quality enhancement", "mfqe"},
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

static gboolean gst_vp8_dec_start (GstVideoDecoder * decoder);
static gboolean gst_vp8_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_vp8_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_vp8_dec_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_vp8_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_vp8_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );

#define parent_class gst_vp8_dec_parent_class
G_DEFINE_TYPE (GstVP8Dec, gst_vp8_dec, GST_TYPE_VIDEO_DECODER);

static void
gst_vp8_dec_class_init (GstVP8DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoDecoderClass *base_video_decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  base_video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

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

  g_object_class_install_property (gobject_class, PROP_THREADS,
      g_param_spec_uint ("threads", "Max Threads",
          "Maximum number of decoding threads",
          1, 16, DEFAULT_THREADS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vp8_dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vp8_dec_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "On2 VP8 Decoder",
      "Codec/Decoder/Video",
      "Decode VP8 video streams", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  base_video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_vp8_dec_start);
  base_video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vp8_dec_stop);
  base_video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_vp8_dec_flush);
  base_video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_vp8_dec_set_format);
  base_video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vp8_dec_handle_frame);
  base_video_decoder_class->decide_allocation = gst_vp8_dec_decide_allocation;

  GST_DEBUG_CATEGORY_INIT (gst_vp8dec_debug, "vp8dec", 0, "VP8 Decoder");
}

static void
gst_vp8_dec_init (GstVP8Dec * gst_vp8_dec)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) gst_vp8_dec;

  GST_DEBUG_OBJECT (gst_vp8_dec, "gst_vp8_dec_init");
  gst_video_decoder_set_packetized (decoder, TRUE);
  gst_vp8_dec->post_processing = DEFAULT_POST_PROCESSING;
  gst_vp8_dec->post_processing_flags = DEFAULT_POST_PROCESSING_FLAGS;
  gst_vp8_dec->deblocking_level = DEFAULT_DEBLOCKING_LEVEL;
  gst_vp8_dec->noise_level = DEFAULT_NOISE_LEVEL;

  gst_video_decoder_set_needs_format (decoder, TRUE);
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
    case PROP_THREADS:
      dec->threads = g_value_get_uint (value);
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
    case PROP_THREADS:
      g_value_set_uint (value, dec->threads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vp8_dec_start (GstVideoDecoder * decoder)
{
  GstVP8Dec *gst_vp8_dec = GST_VP8_DEC (decoder);

  GST_DEBUG_OBJECT (gst_vp8_dec, "start");
  gst_vp8_dec->decoder_inited = FALSE;

  return TRUE;
}

static gboolean
gst_vp8_dec_stop (GstVideoDecoder * base_video_decoder)
{
  GstVP8Dec *gst_vp8_dec = GST_VP8_DEC (base_video_decoder);

  GST_DEBUG_OBJECT (gst_vp8_dec, "stop");

  if (gst_vp8_dec->output_state) {
    gst_video_codec_state_unref (gst_vp8_dec->output_state);
    gst_vp8_dec->output_state = NULL;
  }

  if (gst_vp8_dec->input_state) {
    gst_video_codec_state_unref (gst_vp8_dec->input_state);
    gst_vp8_dec->input_state = NULL;
  }

  if (gst_vp8_dec->decoder_inited)
    vpx_codec_destroy (&gst_vp8_dec->decoder);
  gst_vp8_dec->decoder_inited = FALSE;

  return TRUE;
}

static gboolean
gst_vp8_dec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstVP8Dec *gst_vp8_dec = GST_VP8_DEC (decoder);

  GST_DEBUG_OBJECT (gst_vp8_dec, "set_format");

  if (gst_vp8_dec->decoder_inited)
    vpx_codec_destroy (&gst_vp8_dec->decoder);
  gst_vp8_dec->decoder_inited = FALSE;

  if (gst_vp8_dec->input_state)
    gst_video_codec_state_unref (gst_vp8_dec->input_state);
  gst_vp8_dec->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static gboolean
gst_vp8_dec_flush (GstVideoDecoder * base_video_decoder)
{
  GstVP8Dec *decoder;

  GST_DEBUG_OBJECT (base_video_decoder, "flush");

  decoder = GST_VP8_DEC (base_video_decoder);

  if (decoder->output_state) {
    gst_video_codec_state_unref (decoder->output_state);
    decoder->output_state = NULL;
  }

  if (decoder->decoder_inited)
    vpx_codec_destroy (&decoder->decoder);
  decoder->decoder_inited = FALSE;

  return TRUE;
}

static void
gst_vp8_dec_send_tags (GstVP8Dec * dec)
{
  GstTagList *list;

  list = gst_tag_list_new_empty ();
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_VIDEO_CODEC, "VP8 video", NULL);

  gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (dec),
      gst_event_new_tag (list));
}

static void
gst_vp8_dec_image_to_buffer (GstVP8Dec * dec, const vpx_image_t * img,
    GstBuffer * buffer)
{
  int deststride, srcstride, height, width, line, comp;
  guint8 *dest, *src;
  GstVideoFrame frame;
  GstVideoInfo *info = &dec->output_state->info;

  if (!gst_video_frame_map (&frame, info, buffer, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (dec, "Could not map video buffer");
  }

  for (comp = 0; comp < 3; comp++) {
    dest = GST_VIDEO_FRAME_COMP_DATA (&frame, comp);
    src = img->planes[comp];
    width = GST_VIDEO_FRAME_COMP_WIDTH (&frame, comp);
    height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, comp);
    deststride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, comp);
    srcstride = img->stride[comp];

    /* FIXME (Edward) : Do a plane memcpy is srcstride == deststride instead
     * of copying line by line */
    for (line = 0; line < height; line++) {
      memcpy (dest, src, width);
      dest += deststride;
      src += srcstride;
    }
  }

  gst_video_frame_unmap (&frame);
}

static GstFlowReturn
open_codec (GstVP8Dec * dec, GstVideoCodecFrame * frame)
{
  int flags = 0;
  vpx_codec_stream_info_t stream_info;
  vpx_codec_caps_t caps;
  vpx_codec_dec_cfg_t cfg;
  GstVideoCodecState *state = dec->input_state;
  vpx_codec_err_t status;
  GstMapInfo minfo;

  memset (&stream_info, 0, sizeof (stream_info));
  memset (&cfg, 0, sizeof (cfg));
  stream_info.sz = sizeof (stream_info);

  if (!gst_buffer_map (frame->input_buffer, &minfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (dec, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  status = vpx_codec_peek_stream_info (&vpx_codec_vp8_dx_algo,
      minfo.data, minfo.size, &stream_info);

  gst_buffer_unmap (frame->input_buffer, &minfo);

  if (status != VPX_CODEC_OK) {
    GST_WARNING_OBJECT (dec, "VPX preprocessing error: %s",
        gst_vpx_error_name (status));
    gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);
    return GST_FLOW_CUSTOM_SUCCESS_1;
  }
  if (!stream_info.is_kf) {
    GST_WARNING_OBJECT (dec, "No keyframe, skipping");
    gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);
    return GST_FLOW_CUSTOM_SUCCESS_1;
  }

  g_assert (dec->output_state == NULL);
  dec->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
      GST_VIDEO_FORMAT_I420, stream_info.w, stream_info.h, state);
  gst_video_decoder_negotiate (GST_VIDEO_DECODER (dec));
  gst_vp8_dec_send_tags (dec);

  cfg.w = stream_info.w;
  cfg.h = stream_info.h;
  cfg.threads = dec->threads;

  caps = vpx_codec_get_caps (&vpx_codec_vp8_dx_algo);

  if (dec->post_processing) {
    if (!(caps & VPX_CODEC_CAP_POSTPROC)) {
      GST_WARNING_OBJECT (dec, "Decoder does not support post processing");
    } else {
      flags |= VPX_CODEC_USE_POSTPROC;
    }
  }

  status =
      vpx_codec_dec_init (&dec->decoder, &vpx_codec_vp8_dx_algo, &cfg, flags);
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

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vp8_dec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstVP8Dec *dec;
  GstFlowReturn ret = GST_FLOW_OK;
  vpx_codec_err_t status;
  vpx_codec_iter_t iter = NULL;
  vpx_image_t *img;
  long decoder_deadline = 0;
  GstClockTimeDiff deadline;
  GstMapInfo minfo;

  GST_DEBUG_OBJECT (decoder, "handle_frame");

  dec = GST_VP8_DEC (decoder);

  if (!dec->decoder_inited) {
    ret = open_codec (dec, frame);
    if (ret == GST_FLOW_CUSTOM_SUCCESS_1)
      return GST_FLOW_OK;
    else if (ret != GST_FLOW_OK)
      return ret;
  }

  deadline = gst_video_decoder_get_max_decode_time (decoder, frame);
  if (deadline < 0) {
    decoder_deadline = 1;
  } else if (deadline == G_MAXINT64) {
    decoder_deadline = 0;
  } else {
    decoder_deadline = MAX (1, deadline / GST_MSECOND);
  }

  if (!gst_buffer_map (frame->input_buffer, &minfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (dec, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  status = vpx_codec_decode (&dec->decoder,
      minfo.data, minfo.size, NULL, decoder_deadline);

  gst_buffer_unmap (frame->input_buffer, &minfo);

  if (status) {
    GST_VIDEO_DECODER_ERROR (decoder, 1, LIBRARY, ENCODE,
        ("Failed to decode frame"), ("%s", gst_vpx_error_name (status)), ret);
    return ret;
  }

  img = vpx_codec_get_frame (&dec->decoder, &iter);
  if (img) {
    if (img->fmt != VPX_IMG_FMT_I420) {
      vpx_img_free (img);
      GST_ELEMENT_ERROR (decoder, LIBRARY, ENCODE,
          ("Failed to decode frame"), ("Unsupported color format %d",
              img->fmt));
      return GST_FLOW_ERROR;
    }

    if (deadline < 0) {
      GST_LOG_OBJECT (dec, "Skipping late frame (%f s past deadline)",
          (double) -deadline / GST_SECOND);
      gst_video_decoder_drop_frame (decoder, frame);
    } else {
      ret = gst_video_decoder_allocate_output_frame (decoder, frame);

      if (ret == GST_FLOW_OK) {
        gst_vp8_dec_image_to_buffer (dec, img, frame->output_buffer);
        ret = gst_video_decoder_finish_frame (decoder, frame);
      } else {
        gst_video_decoder_finish_frame (decoder, frame);
      }
    }

    vpx_img_free (img);

    while ((img = vpx_codec_get_frame (&dec->decoder, &iter))) {
      GST_WARNING_OBJECT (decoder, "Multiple decoded frames... dropping");
      vpx_img_free (img);
    }
  } else {
    /* Invisible frame */
    GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);
    gst_video_decoder_finish_frame (decoder, frame);
  }

  return ret;
}

static gboolean
gst_vp8_dec_decide_allocation (GstVideoDecoder * bdec, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (bdec, query))
    return FALSE;

  g_assert (gst_query_get_n_allocation_pools (query) > 0);
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  g_assert (pool != NULL);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}

#endif /* HAVE_VP8_DECODER */
