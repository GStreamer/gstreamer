/* GStreamer SVT JPEG XS decoder
 * Copyright (C) 2024 Tim-Philipp Müller <tim centricular com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION:element-gstsvtjpegxsdec
 *
 * The svtjpegxsdec element does JPEG XS decoding using Scalable
 * Video Technology for JPEG XS Decoder (SVT JPEG XS Decoder).
 *
 * See https://jpeg.org/jpegxs/ for more information about the JPEG XS format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -e filesrc location=jxs.ts ! tsdemux ! svtjpegxsdec ! videoconvertscale ! autovideosink
 * ]|
 * Decodes an JPEG-XS video from an MPEG-TS container.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsvtjpegxsdec.h"

GST_DEBUG_CATEGORY_STATIC (svtjpegxsdec_debug);
#define GST_CAT_DEFAULT svtjpegxsdec_debug

static const uint8_t BLOCKING = 1;

typedef struct _GstSvtJpegXsDec
{
  GstVideoDecoder video_decoder;

  // SVT JPEG XS decoder handle
  svt_jpeg_xs_decoder_api_t *jxs_decoder;

  // Valid if decoder has been initialised (i.e. jxs_decoder is set)
  svt_jpeg_xs_image_config_t img_config;

  uint32_t bytes_per_frame;

  // Video decoder base class codec state
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  // Properties
  int threads;
} GstSvtJpegXsDec;

static void gst_svt_jpeg_xs_dec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_svt_jpeg_xs_dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_svt_jpeg_xs_dec_finalize (GObject * object);

static gboolean gst_svt_jpeg_xs_dec_start (GstVideoDecoder * decoder);
static gboolean gst_svt_jpeg_xs_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_svt_jpeg_xs_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_svt_jpeg_xs_dec_handle_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame);

enum
{
  PROP_THREADS = 1,
};

#define DEFAULT_THREADS 0

#define FORMATS_8_BIT "Y444, Y42B, I420"

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define FORMATS_10_BIT "Y444_10LE, I422_10LE, I420_10LE"
#define FORMATS_12_BIT "Y444_12LE, I422_12LE, I420_12LE"
#define VIDEO_FORMAT_I420_10 GST_VIDEO_FORMAT_I420_10LE
#define VIDEO_FORMAT_I420_12 GST_VIDEO_FORMAT_I420_12LE
#define VIDEO_FORMAT_I422_10 GST_VIDEO_FORMAT_I422_10LE
#define VIDEO_FORMAT_I422_12 GST_VIDEO_FORMAT_I422_12LE
#define VIDEO_FORMAT_Y444_10 GST_VIDEO_FORMAT_Y444_10LE
#define VIDEO_FORMAT_Y444_12 GST_VIDEO_FORMAT_Y444_12LE
#else
#define FORMATS_10_BIT "Y444_10BE, I422_10BE, I420_10BE"
#define FORMATS_12_BIT "Y444_12BE, I422_12BE, I420_12BE"
#define VIDEO_FORMAT_I420_10 GST_VIDEO_FORMAT_I420_10BE
#define VIDEO_FORMAT_I420_12 GST_VIDEO_FORMAT_I420_12BE
#define VIDEO_FORMAT_I422_10 GST_VIDEO_FORMAT_I422_10BE
#define VIDEO_FORMAT_I422_12 GST_VIDEO_FORMAT_I422_12BE
#define VIDEO_FORMAT_Y444_10 GST_VIDEO_FORMAT_Y444_10BE
#define VIDEO_FORMAT_Y444_12 GST_VIDEO_FORMAT_Y444_12BE
#endif

#define SUPPORTED_FORMATS FORMATS_8_BIT ", " FORMATS_10_BIT ", " FORMATS_12_BIT

static GstStaticPadTemplate sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jxsc, alignment = frame, "
        "interlace-mode = progressive, "
        "sampling = { YCbCr-4:4:4, YCbCr-4:2:2, YCbCr-4:2:0 }, "
        "depth = { 8, 10, 12 }"));

// FIXME: add 4:2:2 and 4:4:4 packed formats
// Only handle progressive mode for now
static GstStaticPadTemplate src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "    //
        "format = (string) { " SUPPORTED_FORMATS " },"
        "interlace-mode = progressive, "
        "width = (int) [16, 16384], " "height = (int) [16, 16384], "
        "framerate = (fraction) [0, MAX]"));

#define gst_svt_jpeg_xs_dec_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE (GstSvtJpegXsDec, gst_svt_jpeg_xs_dec,
    GST_TYPE_VIDEO_DECODER, GST_DEBUG_CATEGORY_INIT (svtjpegxsdec_debug,
        "svtjpegxsdec", 0, "SVT JPEG XS decoder element"));

GST_ELEMENT_REGISTER_DEFINE (svtjpegxsdec, "svtjpegxsdec", GST_RANK_SECONDARY,
    gst_svt_jpeg_xs_dec_get_type ());

static void
gst_svt_jpeg_xs_dec_class_init (GstSvtJpegXsDecClass * klass)
{
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_pad_template);

  gst_element_class_add_static_pad_template (element_class, &sink_pad_template);

  gst_element_class_set_static_metadata (element_class,
      "SVT JPEG XS decoder",
      "Codec/Decoder/Video",
      "Scalable Video Technology for JPEG XS Decoder",
      "Tim-Philipp Müller <tim centricular com>");

  gobject_class->set_property = gst_svt_jpeg_xs_dec_set_property;
  gobject_class->get_property = gst_svt_jpeg_xs_dec_get_property;
  gobject_class->finalize = gst_svt_jpeg_xs_dec_finalize;

  video_decoder_class->start = gst_svt_jpeg_xs_dec_start;
  video_decoder_class->stop = gst_svt_jpeg_xs_dec_stop;
  video_decoder_class->set_format = gst_svt_jpeg_xs_dec_set_format;
  video_decoder_class->handle_frame = gst_svt_jpeg_xs_dec_handle_frame;

  g_object_class_install_property (gobject_class,
      PROP_THREADS,
      g_param_spec_int ("threads",
          "Threads",
          "Number of threads to use (0 = automatic)",
          0,
          G_MAXINT,
          DEFAULT_THREADS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_svt_jpeg_xs_dec_finalize (GObject * object)
{
  // Nothing to do here yet

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_svt_jpeg_xs_dec_init (GstSvtJpegXsDec * jxsdec)
{
  // For clarity, technically not needed
  jxsdec->jxs_decoder = NULL;
  jxsdec->input_state = NULL;
  jxsdec->output_state = NULL;
  jxsdec->bytes_per_frame = 0;

  // Property defaults
  jxsdec->threads = DEFAULT_THREADS;

  // Accept caps strategy
  GstVideoDecoder *vdec = GST_VIDEO_DECODER_CAST (jxsdec);
  gst_video_decoder_set_use_default_pad_acceptcaps (vdec, TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (vdec));
}

static gboolean
gst_svt_jpeg_xs_dec_start (GstVideoDecoder * decoder)
{
  // Nothing to do here yet
  return TRUE;
}

static gboolean
gst_svt_jpeg_xs_dec_stop (GstVideoDecoder * decoder)
{
  GstSvtJpegXsDec *jxsdec = GST_SVT_JPEG_XS_DEC (decoder);

  GST_DEBUG_OBJECT (jxsdec, "Stopping");

  if (jxsdec->input_state) {
    gst_video_codec_state_unref (jxsdec->input_state);
    jxsdec->input_state = NULL;
  }

  if (jxsdec->output_state) {
    gst_video_codec_state_unref (jxsdec->output_state);
    jxsdec->output_state = NULL;
  }

  if (jxsdec->jxs_decoder != NULL) {
    svt_jpeg_xs_decoder_close (jxsdec->jxs_decoder);
    g_free (jxsdec->jxs_decoder);
    jxsdec->jxs_decoder = NULL;
  }
  jxsdec->bytes_per_frame = 0;

  return TRUE;
}

static gboolean
gst_svt_jpeg_xs_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstSvtJpegXsDec *jxsdec = GST_SVT_JPEG_XS_DEC (decoder);

  if (jxsdec->input_state) {
    gst_video_codec_state_unref (jxsdec->input_state);

    // Throw away existing decoder, so it's re-created later based on the
    // new input format, which may or may not have changed (hard to tell).
    gst_svt_jpeg_xs_dec_stop (decoder);
    gst_svt_jpeg_xs_dec_start (decoder);
  }

  jxsdec->input_state = gst_video_codec_state_ref (state);

  // In future we could set the output format right away if we have enough
  // info in the caps, but the decoder needs an actual frame / header
  // to initialise itself, so we will have to delay all of this into
  // the handle_frame function.
  GST_DEBUG_OBJECT (decoder, "New input caps: %" GST_PTR_FORMAT, state->caps);

  return TRUE;
}

static const gchar *
format_to_format_name (ColourFormat_t fmt)
{
  switch (fmt) {
    case COLOUR_FORMAT_INVALID:
      return "invalid";
    case COLOUR_FORMAT_PLANAR_YUV400:
      return "YUV400";
    case COLOUR_FORMAT_PLANAR_YUV420:
      return "YUV420";
    case COLOUR_FORMAT_PLANAR_YUV422:
      return "YUV422";
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
      return "YUV444";
    case COLOUR_FORMAT_PLANAR_4_COMPONENTS:
      return "Planar4c";
    case COLOUR_FORMAT_GRAY:
      return "GRAY";
    case COLOUR_FORMAT_PACKED_YUV444_OR_RGB:
      return "PACKED_YUV444_OR_RGB";
    case COLOUR_FORMAT_PLANAR_MAX:
    case COLOUR_FORMAT_PACKED_MIN:
    case COLOUR_FORMAT_PACKED_MAX:
    default:
      break;
  }
  return "unknown";
}

static const uint32_t FAST_SEARCH = 1;
static const uint32_t FRAME_BASED = 0;

static GstFlowReturn
gst_svt_jpeg_xs_dec_init_decoder (GstSvtJpegXsDec * jxsdec,
    const guint8 * data, gsize size)
{
  svt_jpeg_xs_image_config_t img_config;
  SvtJxsErrorType_t dec_ret;
  uint32_t expected_frame_size = 0;

  dec_ret = svt_jpeg_xs_decoder_get_single_frame_size (data, size,
      &img_config, &expected_frame_size, FAST_SEARCH);

  if (dec_ret != SvtJxsErrorNone) {
    GST_ELEMENT_ERROR (jxsdec, STREAM, DECODE, (NULL),
        ("Couldn't probe input frame headers, error code: 0x%08x", dec_ret));
    return GST_FLOW_ERROR;
  }
  // We expect complete frames as input
  if (size != expected_frame_size) {
    GST_ELEMENT_ERROR (jxsdec, STREAM, DECODE, (NULL),
        ("Input frame size does not match expected size, %zu != %u",
            size, expected_frame_size));
    return GST_FLOW_ERROR;
  }
  // Ensure decoder API struct
  jxsdec->jxs_decoder = g_new0 (svt_jpeg_xs_decoder_api_t, 1);

  // Decode parameters from properties
  {
    GST_OBJECT_LOCK (jxsdec);
    jxsdec->jxs_decoder->threads_num = jxsdec->threads;
    GST_OBJECT_UNLOCK (jxsdec);
  }

  // Hardcoded decode parameters
  {
    jxsdec->jxs_decoder->use_cpu_flags = CPU_FLAGS_ALL;

    // Codestream packetization mode (i.e. buffer = entire JPEG XS picture segment)
    jxsdec->jxs_decoder->packetization_mode = FRAME_BASED;

    // Would be better if there was a callback for the messages from the library.
    // Not sure how to prevent the SvtMalloc spam.
    GstDebugLevel level = gst_debug_category_get_threshold (svtjpegxsdec_debug);
    if (level < GST_LEVEL_WARNING) {
      jxsdec->jxs_decoder->verbose = VERBOSE_ERRORS;
    } else if (level == GST_LEVEL_WARNING) {
      jxsdec->jxs_decoder->verbose = VERBOSE_WARNINGS;
    } else {
      jxsdec->jxs_decoder->verbose = VERBOSE_SYSTEM_INFO;
    }
  }

  dec_ret = svt_jpeg_xs_decoder_init (SVT_JPEGXS_API_VER_MAJOR,
      SVT_JPEGXS_API_VER_MINOR, jxsdec->jxs_decoder, data, size,
      &jxsdec->img_config);

  if (dec_ret != SvtJxsErrorNone) {
    g_free (jxsdec->jxs_decoder);
    jxsdec->jxs_decoder = NULL;
    GST_ELEMENT_ERROR (jxsdec, STREAM, DECODE, (NULL),
        ("Decoder failed to initialise, error code: 0x%08x", dec_ret));
    return GST_FLOW_ERROR;
  }

  svt_jpeg_xs_image_config_t *cfg = &jxsdec->img_config;

  GST_INFO_OBJECT (jxsdec, "Output image configuration:");
  GST_INFO_OBJECT (jxsdec, "  width: %u", cfg->width);
  GST_INFO_OBJECT (jxsdec, "  height: %u", cfg->height);
  GST_INFO_OBJECT (jxsdec, "  depth: %u", cfg->bit_depth);
  GST_INFO_OBJECT (jxsdec, "  format: %s", format_to_format_name (cfg->format));
  GST_INFO_OBJECT (jxsdec, "  components: %u", cfg->components_num);
  for (int i = 0; i < cfg->components_num; ++i) {
    GST_INFO_OBJECT (jxsdec, "  comp width: %u", cfg->components[i].width);
    GST_INFO_OBJECT (jxsdec, "  comp height: %u", cfg->components[i].height);
    GST_INFO_OBJECT (jxsdec, "  comp bsize: %u", cfg->components[i].byte_size);
  }

  // Really shouldn't happen, since we specify allowed depth in our sink template
  if (cfg->bit_depth != 8 && cfg->bit_depth != 10 && cfg->bit_depth != 12) {
    GST_ELEMENT_ERROR (jxsdec, STREAM, FORMAT, (NULL),
        ("Image has bit depth of %u, but only a depth of 8, 10 or 12 is supported.",
            cfg->bit_depth));
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;

  switch (cfg->format) {
    case COLOUR_FORMAT_PLANAR_YUV420:
      if (cfg->bit_depth == 8) {
        fmt = GST_VIDEO_FORMAT_I420;
      } else if (cfg->bit_depth == 10) {
        fmt = VIDEO_FORMAT_I420_10;
      } else if (cfg->bit_depth == 12) {
        fmt = VIDEO_FORMAT_I420_12;
      }
      break;
    case COLOUR_FORMAT_PLANAR_YUV422:
      if (cfg->bit_depth == 8) {
        fmt = GST_VIDEO_FORMAT_Y42B;
      } else if (cfg->bit_depth == 10) {
        fmt = VIDEO_FORMAT_I422_10;
      } else if (cfg->bit_depth == 12) {
        fmt = VIDEO_FORMAT_I422_12;
      }
      break;
      // We rely on external signalling (caps) to know what's what
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
      if (cfg->bit_depth == 8) {
        fmt = GST_VIDEO_FORMAT_Y444;
      } else if (cfg->bit_depth == 10) {
        fmt = VIDEO_FORMAT_Y444_10;
      } else if (cfg->bit_depth == 12) {
        fmt = VIDEO_FORMAT_Y444_12;
      }
      break;
      // We rely on external signalling (caps) to know what's what
      // case COLOUR_FORMAT_PACKED_YUV444_OR_RGB:
      // FIXME:  fmt = GST_VIDEO_FORMAT_v308;
      //  break;
    default:
      break;
  }

  if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
    // Really shouldn't happen, since we specify allowed samplings in our
    // sink template, although outputting packed or planar is a decoder
    // choice I suppose.
    GST_ELEMENT_ERROR (jxsdec, STREAM, FORMAT, (NULL),
        ("Unsupported pixel format %s.", format_to_format_name (cfg->format)));
    return GST_FLOW_NOT_NEGOTIATED;
  }
  // Configure output format
  if (jxsdec->output_state)
    gst_video_codec_state_unref (jxsdec->output_state);

  jxsdec->output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (jxsdec), fmt,
      cfg->width, cfg->height, jxsdec->input_state);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_svt_jpeg_xs_dec_handle_frame (GstVideoDecoder * vdecoder,
    GstVideoCodecFrame * frame)
{
  GstSvtJpegXsDec *jxsdec = GST_SVT_JPEG_XS_DEC (vdecoder);
  GstFlowReturn flow = GST_FLOW_OK;

  GST_LOG_OBJECT (jxsdec, "Frame to decode, size: %zu bytes",
      gst_buffer_get_size (frame->input_buffer));

  if (jxsdec->input_state == NULL) {
    GST_WARNING_OBJECT (jxsdec, "No input caps were set?");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GstMapInfo in_map = GST_MAP_INFO_INIT;
  GstVideoFrame video_frame = GST_VIDEO_FRAME_INIT;

  // Map input buffer
  if (!gst_buffer_map (frame->input_buffer, &in_map, GST_MAP_READ))
    goto input_buffer_map_failure;

  if (jxsdec->jxs_decoder == NULL) {
    flow = gst_svt_jpeg_xs_dec_init_decoder (jxsdec, in_map.data, in_map.size);
    if (flow != GST_FLOW_OK)
      goto out_unmap;
  }
  // Decoder input/output frame struct
  svt_jpeg_xs_frame_t decoder_frame;

  // Set up decoder input buffer struct
  {
    svt_jpeg_xs_bitstream_buffer_t in_buf;

    in_buf.buffer = in_map.data;
    in_buf.allocation_size = in_map.size;
    in_buf.used_size = in_map.size;

    decoder_frame.bitstream = in_buf;
  }

  // Allocate output frame
  {
    flow = gst_video_decoder_allocate_output_frame (vdecoder, frame);

    if (flow != GST_FLOW_OK)
      goto allocate_output_frame_failure;
  }

  // Map output frame
  if (!gst_video_frame_map (&video_frame, &jxsdec->output_state->info,
          frame->output_buffer, GST_MAP_WRITE))
    goto output_frame_map_error;

  // Set up decoder output image struct
  {
    svt_jpeg_xs_image_buffer_t img = { {0,}
    };

    img.data_yuv[0] = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, 0);
    img.data_yuv[1] = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, 1);
    img.data_yuv[2] = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, 2);

    // Note: wants stride in pixels not in bytes (might need tweaks for 10-bit)
    img.stride[0] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 0)
        / GST_VIDEO_FRAME_COMP_PSTRIDE (&video_frame, 0);
    img.stride[1] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 1)
        / GST_VIDEO_FRAME_COMP_PSTRIDE (&video_frame, 1);
    img.stride[2] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 2)
        / GST_VIDEO_FRAME_COMP_PSTRIDE (&video_frame, 2);

    img.alloc_size[0] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 0)
        * GST_VIDEO_FRAME_COMP_HEIGHT (&video_frame, 0);
    img.alloc_size[1] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 1)
        * GST_VIDEO_FRAME_COMP_HEIGHT (&video_frame, 1);
    img.alloc_size[2] = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, 2)
        * GST_VIDEO_FRAME_COMP_HEIGHT (&video_frame, 2);

    for (int i = 0; i < 3; ++i) {
      GST_TRACE_OBJECT (jxsdec, "img stride[%u] = %u, alloc_size[%u]: %u",
          i, img.stride[i], i, img.alloc_size[i]);
    }

    decoder_frame.image = img;
  }

  decoder_frame.user_prv_ctx_ptr = NULL;

  GST_TRACE_OBJECT (jxsdec, "Sending frame to decoder ..");

  SvtJxsErrorType_t dec_ret;

  // Decode!
  {
    dec_ret =
        svt_jpeg_xs_decoder_send_frame (jxsdec->jxs_decoder, &decoder_frame,
        BLOCKING);

    if (dec_ret != SvtJxsErrorNone)
      goto send_packet_error;
  }

  // Will get it back from the decoder
  memset (&decoder_frame, 0, sizeof (svt_jpeg_xs_frame_t));

  // Wait for decoded frame..
  {
    dec_ret =
        svt_jpeg_xs_decoder_get_frame (jxsdec->jxs_decoder, &decoder_frame,
        BLOCKING);

    if (dec_ret != SvtJxsErrorNone)
      goto get_frame_error;
  }

  // Unmap output frame and input buffer
  {
    gst_video_frame_unmap (&video_frame);
    video_frame.buffer = NULL;

    gst_buffer_unmap (frame->input_buffer, &in_map);
    in_map.memory = NULL;
  }

  // And output!
  flow = gst_video_decoder_finish_frame (vdecoder, frame);

  frame = NULL;

out_unmap:

  if (video_frame.buffer != NULL)
    gst_video_frame_unmap (&video_frame);

  if (frame != NULL && in_map.memory != NULL)
    gst_buffer_unmap (frame->input_buffer, &in_map);

out:

  if (frame != NULL)
    gst_video_codec_frame_unref (frame);

  return flow;

/* ERRORS */
input_buffer_map_failure:
  {
    GST_ELEMENT_ERROR (jxsdec, STREAM, DECODE, (NULL),
        ("Couldn't map input buffer"));
    flow = GST_FLOW_ERROR;
    goto out;
  }

allocate_output_frame_failure:
  {
    GST_DEBUG_OBJECT (jxsdec, "Couldn't allocate output frame, flow=%s",
        gst_flow_get_name (flow));
    goto out_unmap;
  }

output_frame_map_error:
  {
    GST_ERROR_OBJECT (jxsdec, "Couldn't map output frame!");
    flow = GST_FLOW_ERROR;
    goto out_unmap;
  }

send_packet_error:
  {
    GST_ELEMENT_ERROR (jxsdec, STREAM, DECODE, (NULL),
        ("Error submitting image for decoding: 0x%08x", dec_ret));
    flow = GST_FLOW_ERROR;
    goto out_unmap;
  }

get_frame_error:
  {
    GST_ELEMENT_ERROR (jxsdec, STREAM, DECODE, (NULL),
        ("Error decoding image, error code 0x%08x", dec_ret));
    flow = GST_FLOW_ERROR;
    goto out_unmap;
  }
}

static void
gst_svt_jpeg_xs_dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSvtJpegXsDec *jxsdec = GST_SVT_JPEG_XS_DEC (object);

  if (jxsdec->jxs_decoder != NULL) {
    GST_ERROR_OBJECT (jxsdec,
        "Decoder has been configured already, can't change properties now.");
    return;
  }

  GST_LOG_OBJECT (jxsdec, "Setting property %s", pspec->name);

  switch (property_id) {
    case PROP_THREADS:
      GST_OBJECT_LOCK (jxsdec);
      jxsdec->threads = g_value_get_int (value);
      GST_OBJECT_UNLOCK (jxsdec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_svt_jpeg_xs_dec_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSvtJpegXsDec *jxsdec = GST_SVT_JPEG_XS_DEC (object);

  GST_LOG_OBJECT (jxsdec, "Getting property %s", pspec->name);

  switch (property_id) {
    case PROP_THREADS:
      GST_OBJECT_LOCK (jxsdec);
      g_value_set_int (value, jxsdec->threads);
      GST_OBJECT_UNLOCK (jxsdec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}
