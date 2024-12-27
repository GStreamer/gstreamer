/* GStreamer SVT JPEG XS encoder
 * Copyright (C) 2024 Tim-Philipp Müller <tim centricular com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION:element-svtjpegxsenc
 *
 * The svtjpegxsenc element does JPEG XS encoding using the Scalable
 * Video Technology for JPEG_XS Encoder (SVT JPEG XS Encoder) library.
 *
 * See https://jpeg.org/jpegxs/ for more information about the JPEG XS format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -e videotestsrc ! svtjpegxsenc ! mpegtsmux ! filesink location=out.ts
 * ]|
 * Encodes test video input into a JPEG XS compressed image stream which is
 * then packaged into an MPEG-TS container.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsvtjpegxsenc.h"

GST_DEBUG_CATEGORY_STATIC (svtjpegxsenc_debug);
#define GST_CAT_DEFAULT svtjpegxsenc_debug

static const uint8_t BLOCKING = 1;

#define GST_SVT_JPEG_XS_ENC_TYPE_QUANT_MODE (gst_svt_jpeg_xs_enc_quant_mode_get_type())
static GType
gst_svt_jpeg_xs_enc_quant_mode_get_type (void)
{
  static GType quant_mode_type = 0;
  static const GEnumValue quant_modes[] = {
    {0, "Deadzone", "deadzone"},
    {1, "Uniform", "uniform"},
    {0, NULL, NULL},
  };

  if (!quant_mode_type) {
    quant_mode_type =
        g_enum_register_static ("GstSvtJpegXsEncQuantModeType", quant_modes);
  }
  return quant_mode_type;
}

#define GST_SVT_JPEG_XS_ENC_TYPE_RATE_CONTROL_MODE (gst_svt_jpeg_xs_enc_rate_control_mode_get_type())
static GType
gst_svt_jpeg_xs_enc_rate_control_mode_get_type (void)
{
  static GType rc_mode_type = 0;
  static const GEnumValue rc_modes[] = {
    {0, "CBR budget per precinct", "cbr-precinct"},
    {1, "CBR budget per precinct move padding", "cbr-precinct-move-padding"},
    {2, "CBR budget per slice", "cbr-slice"},
    // Not implemented yet in library
    // {3, "CBR budget per slice with max rate size", "cbr-slice-with-max-rate-size"},
    {0, NULL, NULL},
  };

  if (!rc_mode_type) {
    rc_mode_type =
        g_enum_register_static ("GstSvtJpegXsEncRateControlModeType", rc_modes);
  }
  return rc_mode_type;
}

#define GST_SVT_JPEG_XS_ENC_TYPE_CODING_SIGNS (gst_svt_jpeg_xs_enc_coding_signs_get_type())
static GType
gst_svt_jpeg_xs_enc_coding_signs_get_type (void)
{
  static GType cs_type = 0;
  static const GEnumValue coding_signs[] = {
    {0, "Disable", "disable"},
    {1, "Fast", "fast"},
    {2, "Full", "full"},
    {0, NULL, NULL},
  };

  if (!cs_type) {
    cs_type =
        g_enum_register_static ("GstSvtJpegXsEncCodingSignsType", coding_signs);
  }
  return cs_type;
}

typedef struct _GstSvtJpegXsEnc
{
  GstVideoEncoder video_encoder;

  // SVT JPEG XS encoder handle
  svt_jpeg_xs_encoder_api_t *jxs_encoder;

  uint32_t bytes_per_frame;

  // Video encoder base class codec state
  GstVideoCodecState *state;

  // Properties
  double bits_per_pixel;
  int decomp_v;
  int decomp_h;
  int slice_height;
  int threads;
  int quant_mode;
  int rate_control_mode;
  int coding_signs;
} GstSvtJpegXsEnc;

static void gst_svt_jpeg_xs_enc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_svt_jpeg_xs_enc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_svt_jpeg_xs_enc_finalize (GObject * object);

static gboolean gst_svt_jpeg_xs_enc_start (GstVideoEncoder * encoder);
static gboolean gst_svt_jpeg_xs_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_svt_jpeg_xs_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_svt_jpeg_xs_enc_handle_frame (GstVideoEncoder *
    encoder, GstVideoCodecFrame * frame);
static gboolean gst_svt_jpeg_xs_enc_propose_allocation (GstVideoEncoder * venc,
    GstQuery * query);

enum
{
  PROP_BITS_PER_PIXEL = 1,
  PROP_DECOMP_H,
  PROP_DECOMP_V,
  PROP_SLICE_HEIGHT,
  PROP_THREADS,
  PROP_QUANT_MODE,
  PROP_RATE_CONTROL_MODE,
  PROP_CODING_SIGNS,
};

#define DEFAULT_BITS_PER_PIXEL 3        // or add an auto default for bpp?
#define DEFAULT_DECOMP_H 5
#define DEFAULT_DECOMP_V 2
#define DEFAULT_SLICE_HEIGHT 16
#define DEFAULT_THREADS 0
#define DEFAULT_QUANT_MODE 0
#define DEFAULT_RATE_CONTROL_MODE 0
#define DEFAULT_CODING_SIGNS 0

#define FORMATS_8_BIT "Y444, Y42B, I420"

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define FORMATS_10_BIT "Y444_10LE, I422_10LE, I420_10LE"
#define FORMATS_12_BIT "Y444_12LE, I422_12LE, I420_12LE"
#else
#define FORMATS_10_BIT "Y444_10BE, I422_10BE, I420_10BE"
#define FORMATS_12_BIT "Y444_12BE, I422_12BE, I420_12BE"
#endif

#define SUPPORTED_FORMATS FORMATS_8_BIT ", " FORMATS_10_BIT ", " FORMATS_12_BIT

// FIXME: add 4:2:2 and 4:4:4 packed formats
// Only handle progressive mode for now
static GstStaticPadTemplate sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = { " SUPPORTED_FORMATS " },"
        "interlace-mode = progressive, "
        "width = (int) [16, 16384], "
        "height = (int) [16, 16384], "
        "framerate = (fraction) [0, MAX]; "
        "video/x-raw, "
        "format = { " SUPPORTED_FORMATS " },"
        "interlace-mode = interleaved, "
        "field-order = { top-field-first, bottom-field-first }, "
        "width = (int) [16, 16384], " "height = (int) [16, 16384], "
        "framerate = (fraction) [0, MAX]"));

static GstStaticPadTemplate src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jxsc, alignment = frame, "
        "width = (int) [16, 16384], height = (int) [16, 16384], "
        "interlace-mode = { progressive, fields }, "
        "sampling = { YCbCr-4:4:4, YCbCr-4:2:2, YCbCr-4:2:0 }, "
        "framerate = (fraction) [0, MAX]"));

#define gst_svt_jpeg_xs_enc_parent_class parent_class

G_DEFINE_TYPE_WITH_CODE (GstSvtJpegXsEnc, gst_svt_jpeg_xs_enc,
    GST_TYPE_VIDEO_ENCODER, GST_DEBUG_CATEGORY_INIT (svtjpegxsenc_debug,
        "svtjpegxsenc", 0, "SVT JPEG XS encoder element"));

GST_ELEMENT_REGISTER_DEFINE (svtjpegxsenc, "svtjpegxsenc", GST_RANK_SECONDARY,
    gst_svt_jpeg_xs_enc_get_type ());

static void
gst_svt_jpeg_xs_enc_class_init (GstSvtJpegXsEncClass * klass)
{
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_pad_template);

  gst_element_class_add_static_pad_template (element_class, &sink_pad_template);

  gst_element_class_set_static_metadata (element_class,
      "SVT JPEG XS encoder",
      "Codec/Encoder/Video",
      "Scalable Video Technology for JPEG XS Encoder",
      "Tim-Philipp Müller <tim centricular com>");

  gobject_class->set_property = gst_svt_jpeg_xs_enc_set_property;
  gobject_class->get_property = gst_svt_jpeg_xs_enc_get_property;
  gobject_class->finalize = gst_svt_jpeg_xs_enc_finalize;

  video_encoder_class->start = gst_svt_jpeg_xs_enc_start;
  video_encoder_class->stop = gst_svt_jpeg_xs_enc_stop;
  video_encoder_class->set_format = gst_svt_jpeg_xs_enc_set_format;
  video_encoder_class->handle_frame = gst_svt_jpeg_xs_enc_handle_frame;
  video_encoder_class->propose_allocation =
      gst_svt_jpeg_xs_enc_propose_allocation;

  // ToDo: allow change at runtime
  g_object_class_install_property (gobject_class,
      PROP_BITS_PER_PIXEL,
      g_param_spec_double ("bits-per-pixel",
          "Bits per pixel",
          "Bits per pixel (can be a fractional number, e.g. 3.75)",
          0.001,
          100.00,
          DEFAULT_BITS_PER_PIXEL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DECOMP_H,
      g_param_spec_int ("decomp-h",
          "Horizontal Decomposition Level",
          "Horizontal decomposition (has to be great or equal to decomp-v)",
          0, 5, DEFAULT_DECOMP_H, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DECOMP_V,
      g_param_spec_int ("decomp-v",
          "Vertical Decomposition Level",
          "Vertical decomposition",
          0, 2, DEFAULT_DECOMP_V, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SLICE_HEIGHT,
      g_param_spec_int ("slice-height",
          "Slice Height",
          "The height of each slice in pixel lines (per thread processing unit)",
          1,
          16,
          DEFAULT_SLICE_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_THREADS,
      g_param_spec_int ("threads",
          "Threads",
          "Number of threads to use (0 = automatic)",
          0,
          G_MAXINT,
          DEFAULT_THREADS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_QUANT_MODE,
      g_param_spec_enum ("quant-mode",
          "Quantization Mode",
          "Quantization Mode",
          GST_SVT_JPEG_XS_ENC_TYPE_QUANT_MODE,
          DEFAULT_QUANT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RATE_CONTROL_MODE,
      g_param_spec_enum ("rate-control-mode",
          "Rate Control Mode",
          "Rate Control Mode",
          GST_SVT_JPEG_XS_ENC_TYPE_RATE_CONTROL_MODE,
          DEFAULT_QUANT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_CODING_SIGNS,
      g_param_spec_enum ("coding-signs",
          "Coding Signs Handling Strategy",
          "Coding signs handling strategy",
          GST_SVT_JPEG_XS_ENC_TYPE_CODING_SIGNS,
          DEFAULT_QUANT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_type_mark_as_plugin_api (GST_SVT_JPEG_XS_ENC_TYPE_QUANT_MODE, 0);
  gst_type_mark_as_plugin_api (GST_SVT_JPEG_XS_ENC_TYPE_RATE_CONTROL_MODE, 0);
  gst_type_mark_as_plugin_api (GST_SVT_JPEG_XS_ENC_TYPE_CODING_SIGNS, 0);
}

static void
gst_svt_jpeg_xs_enc_finalize (GObject * object)
{
  // Nothing to do here yet

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_svt_jpeg_xs_enc_init (GstSvtJpegXsEnc * jxsenc)
{
  jxsenc->bits_per_pixel = DEFAULT_BITS_PER_PIXEL;
  jxsenc->decomp_h = DEFAULT_DECOMP_H;
  jxsenc->decomp_v = DEFAULT_DECOMP_V;
  jxsenc->slice_height = DEFAULT_SLICE_HEIGHT;
  jxsenc->quant_mode = DEFAULT_QUANT_MODE;
  jxsenc->rate_control_mode = DEFAULT_RATE_CONTROL_MODE;
  jxsenc->coding_signs = DEFAULT_CODING_SIGNS;
  jxsenc->threads = DEFAULT_THREADS;
}

static gboolean
gst_svt_jpeg_xs_enc_start (GstVideoEncoder * encoder)
{
  svt_jpeg_xs_encoder_api_t dummy_encoder = { 0, };
  SvtJxsErrorType_t ret;

  // Sanity check to catch problems as early as possible, during state change
  ret = svt_jpeg_xs_encoder_load_default_parameters (SVT_JPEGXS_API_VER_MAJOR,
      SVT_JPEGXS_API_VER_MINOR, &dummy_encoder);

  if (ret == SvtJxsErrorNone)
    return TRUE;

  GST_ELEMENT_ERROR (encoder,
      LIBRARY, INIT,
      (NULL),
      ("encoder_load_default_parameters failed with error 0x%08x", ret));

  return FALSE;
}

static gboolean
gst_svt_jpeg_xs_enc_stop (GstVideoEncoder * encoder)
{
  GstSvtJpegXsEnc *jxsenc = GST_SVT_JPEG_XS_ENC (encoder);

  GST_DEBUG_OBJECT (jxsenc, "Stopping");

  if (jxsenc->state) {
    gst_video_codec_state_unref (jxsenc->state);
    jxsenc->state = NULL;
  }

  if (jxsenc->jxs_encoder != NULL) {
    svt_jpeg_xs_encoder_close (jxsenc->jxs_encoder);
    g_free (jxsenc->jxs_encoder);
    jxsenc->jxs_encoder = NULL;
  }
  jxsenc->bytes_per_frame = 0;

  return TRUE;
}

static gboolean
gst_svt_jpeg_xs_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstSvtJpegXsEnc *jxsenc = GST_SVT_JPEG_XS_ENC (encoder);

  GST_DEBUG_OBJECT (jxsenc, "New input caps: %" GST_PTR_FORMAT, state->caps);

  if (jxsenc->state != NULL) {
    // Has the input format changed?
    if (gst_video_info_is_equal (&jxsenc->state->info, &state->info))
      return TRUE;

    // Yes, update encoder instance with new parameters
    gst_svt_jpeg_xs_enc_stop (encoder);
    gst_svt_jpeg_xs_enc_start (encoder);
  }
  jxsenc->state = gst_video_codec_state_ref (state);

  // Ensure encoder API struct
  {
    g_assert (jxsenc->jxs_encoder == NULL);

    jxsenc->jxs_encoder = g_new0 (svt_jpeg_xs_encoder_api_t, 1);
  }

  // Init encoder with default parameters
  {
    SvtJxsErrorType_t ret;

    ret = svt_jpeg_xs_encoder_load_default_parameters (SVT_JPEGXS_API_VER_MAJOR,
        SVT_JPEGXS_API_VER_MINOR, jxsenc->jxs_encoder);

    if (ret != SvtJxsErrorNone) {
      GST_ELEMENT_ERROR (encoder,
          LIBRARY, INIT,
          (NULL),
          ("encoder load_default_parameters failed with error 0x%08x", ret));
      return FALSE;
    }
  }

  svt_jpeg_xs_encoder_api_t *enc = jxsenc->jxs_encoder;

  // Fill in encode parameters from properties
  {
    int num, denom;

    GST_OBJECT_LOCK (jxsenc);

    gst_util_double_to_fraction (jxsenc->bits_per_pixel, &num, &denom);
    enc->bpp_numerator = num;
    enc->bpp_denominator = denom;

    enc->ndecomp_h = jxsenc->decomp_h;
    enc->ndecomp_v = jxsenc->decomp_v;

    enc->slice_height = jxsenc->slice_height;
    enc->quantization = jxsenc->quant_mode;

    enc->threads_num = jxsenc->threads;

    enc->rate_control_mode = jxsenc->rate_control_mode;

    enc->coding_signs_handling = jxsenc->coding_signs;

    GST_OBJECT_UNLOCK (jxsenc);
  }

  // Hardcoded encode parameters
  {
    // Codestream packetization mode (i.e. output entire JPEG XS picture segment)
    enc->slice_packetization_mode = 0;

    // Would be better if there was a callback for the messages from the library.
    // Not sure how to prevent the SvtMalloc spam.
    GstDebugLevel level = gst_debug_category_get_threshold (svtjpegxsenc_debug);
    if (level < GST_LEVEL_WARNING) {
      enc->verbose = VERBOSE_ERRORS;
    } else if (level == GST_LEVEL_WARNING) {
      enc->verbose = VERBOSE_WARNINGS;
    } else {
      enc->verbose = VERBOSE_SYSTEM_INFO;
    }
  }

  guint n_fields = !!GST_VIDEO_INFO_IS_INTERLACED (&state->info) + 1;

  const char *sampling = NULL;

  // Fill in video format parameters
  {
    enc->source_width = GST_VIDEO_INFO_WIDTH (&state->info);
    enc->source_height = GST_VIDEO_INFO_HEIGHT (&state->info) / n_fields;

    switch (GST_VIDEO_INFO_FORMAT (&state->info)) {
      case GST_VIDEO_FORMAT_I420:
        enc->input_bit_depth = 8;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        sampling = "YCbCr-4:2:0";
        break;
      case GST_VIDEO_FORMAT_Y42B:
        enc->input_bit_depth = 8;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        sampling = "YCbCr-4:2:2";
        break;
      case GST_VIDEO_FORMAT_Y444:
        enc->input_bit_depth = 8;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        sampling = "YCbCr-4:4:4";
        break;
      case GST_VIDEO_FORMAT_I420_10BE:
      case GST_VIDEO_FORMAT_I420_10LE:
        enc->input_bit_depth = 10;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        sampling = "YCbCr-4:2:0";
        break;
      case GST_VIDEO_FORMAT_I422_10BE:
      case GST_VIDEO_FORMAT_I422_10LE:
        enc->input_bit_depth = 10;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        sampling = "YCbCr-4:2:2";
        break;
      case GST_VIDEO_FORMAT_Y444_10BE:
      case GST_VIDEO_FORMAT_Y444_10LE:
        enc->input_bit_depth = 10;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        sampling = "YCbCr-4:4:4";
        break;
      case GST_VIDEO_FORMAT_I420_12BE:
      case GST_VIDEO_FORMAT_I420_12LE:
        enc->input_bit_depth = 12;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
        sampling = "YCbCr-4:2:0";
        break;
      case GST_VIDEO_FORMAT_I422_12BE:
      case GST_VIDEO_FORMAT_I422_12LE:
        enc->input_bit_depth = 12;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
        sampling = "YCbCr-4:2:2";
        break;
      case GST_VIDEO_FORMAT_Y444_12BE:
      case GST_VIDEO_FORMAT_Y444_12LE:
        enc->input_bit_depth = 12;
        enc->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        sampling = "YCbCr-4:4:4";
        break;
      default:
        g_error ("Unexpected input video format %s!",
            gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&state->info)));
        return FALSE;
    }
  }

  // Init encoder
  {
    SvtJxsErrorType_t ret;

    // This call takes quite some time (1.8s here)
    ret = svt_jpeg_xs_encoder_init (SVT_JPEGXS_API_VER_MAJOR,
        SVT_JPEGXS_API_VER_MINOR, enc);

    if (ret != SvtJxsErrorNone) {
      GST_ELEMENT_ERROR (encoder,
          LIBRARY, INIT,
          (NULL), ("encoder initialisation failed with error 0x%08x", ret));
      return FALSE;
    }
  }

  // Query size of encoded frames
  {
    SvtJxsErrorType_t ret;
    svt_jpeg_xs_image_config_t img_config;
    uint32_t bytes_per_frame_or_field = 0;

    ret = svt_jpeg_xs_encoder_get_image_config (SVT_JPEGXS_API_VER_MAJOR,
        SVT_JPEGXS_API_VER_MINOR, enc, &img_config, &bytes_per_frame_or_field);

    if (ret != SvtJxsErrorNone || bytes_per_frame_or_field == 0) {
      GST_ELEMENT_ERROR (encoder,
          LIBRARY, INIT,
          (NULL),
          ("Couldn't query encoder output image config, error 0x%08x", ret));
      return FALSE;
    }

    if (n_fields == 2) {
      GST_DEBUG_OBJECT (jxsenc, "Encoded field size: %u bytes",
          bytes_per_frame_or_field);
    }

    GST_DEBUG_OBJECT (jxsenc, "Encoded frame size: %u bytes",
        bytes_per_frame_or_field * n_fields);

    jxsenc->bytes_per_frame = bytes_per_frame_or_field * n_fields;
  }

  GstCaps *src_caps = gst_static_pad_template_get_caps (&src_pad_template);

  src_caps = gst_caps_make_writable (src_caps);

  // ToDo: add more things to the caps?
  gst_caps_set_simple (src_caps,        //
      "sampling", G_TYPE_STRING, sampling,      //
      "depth", G_TYPE_INT, enc->input_bit_depth,        //
      "codestream-length", G_TYPE_INT, jxsenc->bytes_per_frame, //
      NULL);

  GstVideoCodecState *output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder), src_caps,
      jxsenc->state);

  if (n_fields == 2) {
    // input will be interleaved, but we output interlace-mode=fields
    GST_VIDEO_INFO_INTERLACE_MODE (&output_state->info) =
        GST_VIDEO_INTERLACE_MODE_FIELDS;
  }

  if (!gst_video_encoder_negotiate (encoder)) {
    gst_video_codec_state_unref (output_state);
    return FALSE;
  }

  GST_INFO_OBJECT (jxsenc, "Output caps: %" GST_PTR_FORMAT, output_state->caps);
  gst_video_codec_state_unref (output_state);

  return TRUE;
}

// The codestream data is either a full progressive image or a single field.
static GstFlowReturn
gst_svt_jpeg_xs_enc_encode_codestream (GstSvtJpegXsEnc * jxsenc,
    guint field, guint n_fields, GstVideoFrame * video_frame,
    svt_jpeg_xs_bitstream_buffer_t * bitstream_buffer)
{
  // Encoder input/output frame struct
  svt_jpeg_xs_frame_t encoder_frame;

  // Set up encoder input image struct
  {
    svt_jpeg_xs_image_buffer_t img = { {0,}
    };

    img.data_yuv[0] = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (video_frame, 0)
        + field * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 0);
    img.data_yuv[1] = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (video_frame, 1)
        + field * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 1);
    img.data_yuv[2] = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (video_frame, 2)
        + field * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 2);

    // Note: wants stride in pixels not in bytes (might need tweaks for 10-bit)
    img.stride[0] = n_fields * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 0)
        / GST_VIDEO_FRAME_COMP_PSTRIDE (video_frame, 0);
    img.stride[1] = n_fields * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 1)
        / GST_VIDEO_FRAME_COMP_PSTRIDE (video_frame, 1);
    img.stride[2] = n_fields * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 2)
        / GST_VIDEO_FRAME_COMP_PSTRIDE (video_frame, 2);

    // svt-jpegxs returns an error if we specify the size correctly,
    // probably because of lazy assumption in some input check.
    // See https://github.com/OpenVisualCloud/SVT-JPEG-XS/pull/5
    // Remove once there's a new release with the fix.
    img.alloc_size[0] = GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 0)
        * GST_VIDEO_FRAME_COMP_HEIGHT (video_frame, 0);
    //  - field * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 0);
    img.alloc_size[1] = GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 1)
        * GST_VIDEO_FRAME_COMP_HEIGHT (video_frame, 1);
    //  - field * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 1);
    img.alloc_size[2] = GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 2)
        * GST_VIDEO_FRAME_COMP_HEIGHT (video_frame, 2);
    //  - field * GST_VIDEO_FRAME_COMP_STRIDE (video_frame, 2);

    for (int i = 0; i < 3; ++i) {
      GST_TRACE_OBJECT (jxsenc, "img stride[%u] = %u, alloc_size[%u]: %u",
          i, img.stride[i], i, img.alloc_size[i]);
    }

    encoder_frame.image = img;
  }

  encoder_frame.bitstream = *bitstream_buffer;

  encoder_frame.user_prv_ctx_ptr = NULL;

  SvtJxsErrorType_t enc_ret;

  // Encode!
  {
    enc_ret =
        svt_jpeg_xs_encoder_send_picture (jxsenc->jxs_encoder, &encoder_frame,
        BLOCKING);

    if (enc_ret != SvtJxsErrorNone)
      goto send_picture_error;
  }

  memset (&encoder_frame, 0, sizeof (svt_jpeg_xs_frame_t));

  // Wait for encoded frame..
  {
    enc_ret =
        svt_jpeg_xs_encoder_get_packet (jxsenc->jxs_encoder, &encoder_frame,
        BLOCKING);

    if (enc_ret != SvtJxsErrorNone)
      goto get_packet_error;
  }

  *bitstream_buffer = encoder_frame.bitstream;

  GST_TRACE_OBJECT (jxsenc, "Codestream length: %u (%s)",
      encoder_frame.bitstream.used_size, (n_fields == 2) ? "field" : "frame");

  return GST_FLOW_OK;

/* Errors */
send_picture_error:
  {
    GST_ELEMENT_ERROR (jxsenc, LIBRARY, ENCODE, (NULL),
        ("Error encoding image (%s): 0x%08x", "send_picture", enc_ret));
    return GST_FLOW_ERROR;
  }

get_packet_error:
  {
    GST_ELEMENT_ERROR (jxsenc, LIBRARY, ENCODE, (NULL),
        ("Error encoding image (%s): 0x%08x", "get_packet", enc_ret));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_svt_jpeg_xs_enc_handle_frame (GstVideoEncoder * vencoder,
    GstVideoCodecFrame * frame)
{
  GstSvtJpegXsEnc *jxsenc = GST_SVT_JPEG_XS_ENC (vencoder);

  GST_LOG_OBJECT (jxsenc, "Frame to encode");

  if (jxsenc->jxs_encoder == NULL || jxsenc->state == NULL) {
    GST_ERROR_OBJECT (jxsenc,
        "Encoder not initialised yet. No input caps set?");
    return GST_FLOW_NOT_NEGOTIATED;
  }
  // Map input buffer
  GstVideoFrame video_frame;

  if (!gst_video_frame_map (&video_frame, &jxsenc->state->info,
          frame->input_buffer, GST_MAP_READ))
    goto map_error;

  GstFlowReturn flow;

  // Allocate output buffer
  {
    // Could use a bufferpool here, since output frames are all the same size.
    flow =
        gst_video_encoder_allocate_output_frame (vencoder, frame,
        jxsenc->bytes_per_frame);

    if (flow != GST_FLOW_OK)
      goto allocate_output_frame_failure;
  }

  guint n_fields = !!GST_VIDEO_FRAME_IS_INTERLACED (&video_frame) + 1;

  // Map output buffer
  GstMapInfo outbuf_map = GST_MAP_INFO_INIT;

  if (!gst_buffer_map (frame->output_buffer, &outbuf_map, GST_MAP_WRITE))
    goto output_buffer_map_write_failure;

  // Encode frame or fields
  gsize offset = 0;

  for (guint field = 0; field < n_fields; ++field) {
    svt_jpeg_xs_bitstream_buffer_t out_buf;

    if (n_fields == 2) {
      GST_TRACE_OBJECT (jxsenc,
          "Encoding field %u of 2 @ %zu", field + 1, offset);
    }

    // Set up encoder output buffer struct
    out_buf.buffer = outbuf_map.data + offset;
    out_buf.allocation_size = outbuf_map.size - offset;
    out_buf.used_size = 0;

    flow =
        gst_svt_jpeg_xs_enc_encode_codestream (jxsenc, field, n_fields,
        &video_frame, &out_buf);

    if (flow != GST_FLOW_OK)
      goto out_unmap;

    offset += out_buf.used_size;
  }

  gst_buffer_unmap (frame->output_buffer, &outbuf_map);

  // Shouldn't happen, but let's play it safe
  if (offset < jxsenc->bytes_per_frame) {
    GST_WARNING_OBJECT (jxsenc, "Short encoder output: %zu < %u bytes",
        offset, jxsenc->bytes_per_frame);
    gst_buffer_set_size (frame->output_buffer, offset);
  }

  GST_LOG_OBJECT (jxsenc, "Output buffer size: %zu bytes, codestreams=%u",
      offset, n_fields);

  // All frames are key frames
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

  // And output!
  flow = gst_video_encoder_finish_frame (vencoder, frame);

  frame = NULL;

out_unmap:

  if (frame != NULL && outbuf_map.memory != NULL)
    gst_buffer_unmap (frame->output_buffer, &outbuf_map);

  gst_video_frame_unmap (&video_frame);

out:

  if (frame != NULL)
    gst_video_codec_frame_unref (frame);

  return flow;

/* ERRORS */
map_error:
  {
    GST_ELEMENT_ERROR (jxsenc, LIBRARY, ENCODE, (NULL),
        ("Couldn't map input frame"));
    flow = GST_FLOW_ERROR;
    goto out;
  }

allocate_output_frame_failure:
  {
    GST_DEBUG_OBJECT (jxsenc, "Couldn't allocate output frame, flow=%s",
        gst_flow_get_name (flow));
    goto out_unmap;
  }

output_buffer_map_write_failure:
  {
    GST_ERROR_OBJECT (jxsenc, "Couldn't map output buffer!");
    flow = GST_FLOW_ERROR;
    goto out_unmap;
  }
}

static gboolean
gst_svt_jpeg_xs_enc_propose_allocation (GstVideoEncoder * venc,
    GstQuery * query)
{
  GstSvtJpegXsEnc *jxsenc = GST_SVT_JPEG_XS_ENC (venc);

  GST_DEBUG_OBJECT (jxsenc, "propose_allocation");

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (venc,
      query);
}

static void
gst_svt_jpeg_xs_enc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSvtJpegXsEnc *jxsenc = GST_SVT_JPEG_XS_ENC (object);

  // ToDo: support reconfiguring on the fly
  if (jxsenc->jxs_encoder != NULL) {
    GST_ERROR_OBJECT (jxsenc,
        "Encoder has been configured already, can't change properties now.");
    return;
  }

  GST_LOG_OBJECT (jxsenc, "Setting property %s", pspec->name);

  switch (property_id) {
    case PROP_BITS_PER_PIXEL:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->bits_per_pixel = g_value_get_double (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_DECOMP_H:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->decomp_h = g_value_get_int (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_DECOMP_V:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->decomp_v = g_value_get_int (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_SLICE_HEIGHT:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->slice_height = g_value_get_int (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_THREADS:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->threads = g_value_get_int (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_QUANT_MODE:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->quant_mode = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_RATE_CONTROL_MODE:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->rate_control_mode = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_CODING_SIGNS:
      GST_OBJECT_LOCK (jxsenc);
      jxsenc->coding_signs = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_svt_jpeg_xs_enc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSvtJpegXsEnc *jxsenc = GST_SVT_JPEG_XS_ENC (object);

  GST_LOG_OBJECT (jxsenc, "Getting property %s", pspec->name);

  switch (property_id) {
    case PROP_BITS_PER_PIXEL:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_double (value, jxsenc->bits_per_pixel);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_DECOMP_H:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_int (value, jxsenc->decomp_h);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_DECOMP_V:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_int (value, jxsenc->decomp_v);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_SLICE_HEIGHT:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_int (value, jxsenc->slice_height);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_THREADS:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_int (value, jxsenc->threads);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_QUANT_MODE:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_enum (value, jxsenc->quant_mode);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_RATE_CONTROL_MODE:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_enum (value, jxsenc->rate_control_mode);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    case PROP_CODING_SIGNS:
      GST_OBJECT_LOCK (jxsenc);
      g_value_set_enum (value, jxsenc->coding_signs);
      GST_OBJECT_UNLOCK (jxsenc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}
