/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
 * Copyright (C) 2022 Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:element-videoconvertscale
 * @title: videoconvertscale
 *
 * This element resizes video frames and allows changing colorspace. By default
 * the element will try to negotiate to the same size on the source and sinkpad
 * so that no scaling is needed. It is therefore safe to insert this element in
 * a pipeline to get more robust behaviour without any cost if no scaling is
 * needed.
 *
 * This element supports a wide range of color spaces including various YUV and
 * RGB formats and is therefore generally able to operate anywhere in a
 * pipeline.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoconvertscale ! autovideosink
 * ]|
 *  Decode an Ogg/Theora and display the video. If the video sink chosen
 * cannot perform scaling, the video scaling will be performed by videoconvertscale
 * when you resize the video window.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoconvertscale ! video/x-raw,width=100 ! autovideosink
 * ]|
 *  Decode an Ogg/Theora and display the video with a width of 100.
 *
 * Since: 1.22
 */

/*
 * Formulas for PAR, DAR, width and height relations:
 *
 * dar_n   w   par_n
 * ----- = - * -----
 * dar_d   h   par_d
 *
 * par_n    h   dar_n
 * ----- =  - * -----
 * par_d    w   dar_d
 *
 *         dar_n   par_d
 * w = h * ----- * -----
 *         dar_d   par_n
 *
 *         dar_d   par_n
 * h = w * ----- * -----
 *         dar_n   par_d
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <math.h>

#include <gst/video/video.h>

#include "gstvideoconvertscale.h"

typedef struct
{
  /* properties */
  GstVideoScaleMethod method;
  gboolean add_borders;
  double sharpness;
  double sharpen;
  int submethod;
  double envelope;
  gint n_threads;
  gboolean n_threads_set;
  GstVideoDitherMethod dither;
  guint dither_quantization;
  GstVideoResamplerMethod chroma_resampler;
  GstVideoAlphaMode alpha_mode;
  GstVideoChromaMode chroma_mode;
  GstVideoMatrixMode matrix_mode;
  GstVideoGammaMode gamma_mode;
  GstVideoPrimariesMode primaries_mode;
  gdouble alpha_value;
  GstVideoConvertScaleVAlign v_align;
  GstVideoConvertScaleHAlign h_align;
  gboolean use_fastpaths;

  GstVideoConverter *convert;

  GstStructure *converter_config;
  gboolean converter_config_changed;

  GstVideoInfo last_frame_vinfo;

  gint borders_h;
  gint borders_w;
  gint dst_x;
  gint dst_y;

  gint crop_x;
  gint crop_y;
  gint crop_width;
  gint crop_height;

  GstTaskPool *task_pool;
  gboolean task_pool_from_persistent_context;
} GstVideoConvertScalePrivate;

#define gst_video_convert_scale_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVideoConvertScale, gst_video_convert_scale,
    GST_TYPE_VIDEO_FILTER);
GST_ELEMENT_REGISTER_DEFINE (videoconvertscale, "videoconvertscale",
    GST_RANK_SECONDARY, GST_TYPE_VIDEO_CONVERT_SCALE);

#define PRIV(self) gst_video_convert_scale_get_instance_private(((GstVideoConvertScale*) self))

#define GST_CAT_DEFAULT video_convertscale_debug
GST_DEBUG_CATEGORY_STATIC (video_convertscale_debug);
GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);

#define DEFAULT_PROP_METHOD       GST_VIDEO_SCALE_BILINEAR
#define DEFAULT_PROP_ADD_BORDERS  TRUE
#define DEFAULT_PROP_SHARPNESS    1.0
#define DEFAULT_PROP_SHARPEN      0.0
#define DEFAULT_PROP_DITHER      GST_VIDEO_DITHER_BAYER
#define DEFAULT_PROP_ENVELOPE     2.0
#define DEFAULT_PROP_DITHER_QUANTIZATION 1
#define DEFAULT_PROP_CHROMA_RESAMPLER	GST_VIDEO_RESAMPLER_METHOD_LINEAR
#define DEFAULT_PROP_ALPHA_MODE GST_VIDEO_ALPHA_MODE_COPY
#define DEFAULT_PROP_ALPHA_VALUE 1.0
#define DEFAULT_PROP_CHROMA_MODE GST_VIDEO_CHROMA_MODE_FULL
#define DEFAULT_PROP_MATRIX_MODE GST_VIDEO_MATRIX_MODE_FULL
#define DEFAULT_PROP_GAMMA_MODE GST_VIDEO_GAMMA_MODE_NONE
#define DEFAULT_PROP_PRIMARIES_MODE GST_VIDEO_PRIMARIES_MODE_NONE
#define DEFAULT_PROP_N_THREADS 1
#define DEFAULT_PROP_VALIGN GST_VIDEO_CONVERT_SCALE_VALIGN_CENTER
#define DEFAULT_PROP_HALIGN GST_VIDEO_CONVERT_SCALE_HALIGN_CENTER
#define DEFAULT_PROP_USE_FASTPATHS  TRUE

enum
{
  PROP_0,
  PROP_METHOD,
  PROP_ADD_BORDERS,
  PROP_SHARPNESS,
  PROP_SHARPEN,
  PROP_DITHER,
  PROP_SUBMETHOD,
  PROP_ENVELOPE,
  PROP_N_THREADS,
  PROP_DITHER_QUANTIZATION,
  PROP_CHROMA_RESAMPLER,
  PROP_ALPHA_MODE,
  PROP_ALPHA_VALUE,
  PROP_CHROMA_MODE,
  PROP_MATRIX_MODE,
  PROP_GAMMA_MODE,
  PROP_PRIMARIES_MODE,
  PROP_CONVERTER_CONFIG,
  PROP_VALIGN,
  PROP_HALIGN,
  PROP_USE_FASTPATHS,
};

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767]"

/* FIXME: add v210 support
 * FIXME: add v216 support
 * FIXME: add UYVP support
 * FIXME: add A420 support
 * FIXME: add YUV9 support
 * FIXME: add YVU9 support
 * FIXME: add IYU1 support
 * FIXME: add r210 support
 */

#define GST_VIDEO_FORMATS GST_VIDEO_FORMATS_ALL

static GstStaticCaps gst_video_convert_scale_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS_ANY));

static GQuark _size_quark;
static GQuark _scale_quark;
static GQuark _matrix_quark;

#define GST_TYPE_VIDEO_SCALE_METHOD (gst_video_scale_method_get_type())
static GType
gst_video_scale_method_get_type (void)
{
  static GType video_scale_method_type = 0;

  static const GEnumValue video_scale_methods[] = {
    {GST_VIDEO_SCALE_NEAREST, "Nearest Neighbour", "nearest-neighbour"},
    {GST_VIDEO_SCALE_BILINEAR, "Bilinear (2-tap)", "bilinear"},
    {GST_VIDEO_SCALE_4TAP, "4-tap Sinc", "4-tap"},
    {GST_VIDEO_SCALE_LANCZOS, "Lanczos", "lanczos"},
    {GST_VIDEO_SCALE_BILINEAR2, "Bilinear (multi-tap)", "bilinear2"},
    {GST_VIDEO_SCALE_SINC, "Sinc (multi-tap)", "sinc"},
    {GST_VIDEO_SCALE_HERMITE, "Hermite (multi-tap)", "hermite"},
    {GST_VIDEO_SCALE_SPLINE, "Spline (multi-tap)", "spline"},
    {GST_VIDEO_SCALE_CATROM, "Catmull-Rom (multi-tap)", "catrom"},
    {GST_VIDEO_SCALE_MITCHELL, "Mitchell (multi-tap)", "mitchell"},
    {0, NULL, NULL},
  };

  if (!video_scale_method_type) {
    video_scale_method_type =
        g_enum_register_static ("GstVideoScaleMethod", video_scale_methods);
  }
  return video_scale_method_type;
}

#define GST_TYPE_VIDEO_CONVERT_SCALE_VALIGN (gst_video_convert_scale_valign_get_type())
static GType
gst_video_convert_scale_valign_get_type (void)
{
  static GType type = 0;

  static const GEnumValue methods[] = {
    {GST_VIDEO_CONVERT_SCALE_VALIGN_TOP, "Top", "top"},
    {GST_VIDEO_CONVERT_SCALE_VALIGN_CENTER, "Center", "center"},
    {GST_VIDEO_CONVERT_SCALE_VALIGN_BOTTOM, "Bottom", "bottom"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&type)) {
    GType tmp = g_enum_register_static ("GstVideoConvertScaleVAlign", methods);
    g_once_init_leave (&type, tmp);
  }

  return type;
}

#define GST_TYPE_VIDEO_CONVERT_SCALE_HALIGN (gst_video_convert_scale_halign_get_type())
static GType
gst_video_convert_scale_halign_get_type (void)
{
  static GType type = 0;

  static const GEnumValue methods[] = {
    {GST_VIDEO_CONVERT_SCALE_HALIGN_LEFT, "Left", "left"},
    {GST_VIDEO_CONVERT_SCALE_HALIGN_CENTER, "Center", "center"},
    {GST_VIDEO_CONVERT_SCALE_HALIGN_RIGHT, "Right", "right"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&type)) {
    GType tmp = g_enum_register_static ("GstVideoConvertScaleHAlign", methods);
    g_once_init_leave (&type, tmp);
  }

  return type;
}

static GstCaps *
gst_video_convert_scale_get_capslist (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_video_convert_scale_format_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_video_convert_scale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_video_convert_scale_get_capslist ());
}

static GstPadTemplate *
gst_video_convert_scale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_video_convert_scale_get_capslist ());
}


static void gst_video_convert_scale_finalize (GstVideoConvertScale * self);
static void gst_video_convert_scale_dispose (GObject * object);
static gboolean gst_video_convert_scale_src_event (GstBaseTransform * trans,
    GstEvent * event);

/* base transform vmethods */
static GstCaps *gst_video_convert_scale_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_video_convert_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_video_convert_scale_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_video_convert_scale_transform_meta (GstBaseTransform *
    trans, GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);

static gboolean gst_video_convert_scale_set_info (GstVideoFilter * filter,
    GstCaps * in, GstVideoInfo * in_info, GstCaps * out,
    GstVideoInfo * out_info);
static GstFlowReturn gst_video_convert_scale_transform_frame (GstVideoFilter *
    filter, GstVideoFrame * in, GstVideoFrame * out);

static void gst_video_convert_scale_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_video_convert_scale_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_video_convert_scale_set_context (GstElement * element,
    GstContext * context);
static GstStateChangeReturn gst_video_convert_scale_change_state (GstElement *
    element, GstStateChange transition);

static gboolean
gst_video_convert_scale_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  /* propose all metadata upstream */
  return TRUE;
}

static void
gst_video_convert_scale_class_init (GstVideoConvertScaleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *filter_class = (GstVideoFilterClass *) klass;

  GST_DEBUG_CATEGORY_INIT (video_convertscale_debug, "videoconvertscale", 0,
      "videoconvertscale element");
  GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE");

  gobject_class->dispose = gst_video_convert_scale_dispose;
  gobject_class->finalize =
      (GObjectFinalizeFunc) gst_video_convert_scale_finalize;
  gobject_class->set_property = gst_video_convert_scale_set_property;
  gobject_class->get_property = gst_video_convert_scale_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEO_SCALE_METHOD, DEFAULT_PROP_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add black borders if necessary to keep the display aspect ratio",
          DEFAULT_PROP_ADD_BORDERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHARPNESS,
      g_param_spec_double ("sharpness", "Sharpness",
          "Sharpness of filter", 0.5, 1.5, DEFAULT_PROP_SHARPNESS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHARPEN,
      g_param_spec_double ("sharpen", "Sharpen",
          "Sharpening", 0.0, 1.0, DEFAULT_PROP_SHARPEN,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DITHER,
      g_param_spec_enum ("dither", "Dither", "Apply dithering while converting",
          gst_video_dither_method_get_type (), DEFAULT_PROP_DITHER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ENVELOPE,
      g_param_spec_double ("envelope", "Envelope",
          "Size of filter envelope", 1.0, 5.0, DEFAULT_PROP_ENVELOPE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_N_THREADS,
      g_param_spec_uint ("n-threads", "Threads",
          "Maximum number of threads to use", 0, G_MAXUINT,
          DEFAULT_PROP_N_THREADS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DITHER_QUANTIZATION,
      g_param_spec_uint ("dither-quantization", "Dither Quantize",
          "Quantizer to use", 0, G_MAXUINT, DEFAULT_PROP_DITHER_QUANTIZATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CHROMA_RESAMPLER,
      g_param_spec_enum ("chroma-resampler", "Chroma resampler",
          "Chroma resampler method", gst_video_resampler_method_get_type (),
          DEFAULT_PROP_CHROMA_RESAMPLER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ALPHA_MODE,
      g_param_spec_enum ("alpha-mode", "Alpha Mode",
          "Alpha Mode to use", gst_video_alpha_mode_get_type (),
          DEFAULT_PROP_ALPHA_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ALPHA_VALUE,
      g_param_spec_double ("alpha-value", "Alpha Value",
          "Alpha Value to use", 0.0, 1.0,
          DEFAULT_PROP_ALPHA_VALUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CHROMA_MODE,
      g_param_spec_enum ("chroma-mode", "Chroma Mode", "Chroma Resampling Mode",
          gst_video_chroma_mode_get_type (), DEFAULT_PROP_CHROMA_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MATRIX_MODE,
      g_param_spec_enum ("matrix-mode", "Matrix Mode", "Matrix Conversion Mode",
          gst_video_matrix_mode_get_type (), DEFAULT_PROP_MATRIX_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_GAMMA_MODE,
      g_param_spec_enum ("gamma-mode", "Gamma Mode", "Gamma Conversion Mode",
          gst_video_gamma_mode_get_type (), DEFAULT_PROP_GAMMA_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PRIMARIES_MODE,
      g_param_spec_enum ("primaries-mode", "Primaries Mode",
          "Primaries Conversion Mode", gst_video_primaries_mode_get_type (),
          DEFAULT_PROP_PRIMARIES_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVideoConvertScale:converter-config:
   *
   * A #GstStructure describing the configuration that should be used. This
   * configuration, if set, takes precedence over the other similar conversion
   * properties.
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class,
      PROP_CONVERTER_CONFIG, g_param_spec_boxed ("converter-config",
          "Converter configuration",
          "A GstStructure describing the configuration that should be used."
          " This configuration, if set, takes precedence over the other similar conversion properties.",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVideoConvertScale:valignment:
   *
   * Vertical alignment of the video content when adding borders
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_VALIGN,
      g_param_spec_enum ("valignment", "Vertical Alignment",
          "Vertical alignment of the video content when adding borders",
          GST_TYPE_VIDEO_CONVERT_SCALE_VALIGN, DEFAULT_PROP_VALIGN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVideoConvertScale:halignment:
   *
   * Horizontal alignment of the video content when adding borders
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_HALIGN,
      g_param_spec_enum ("halignment", "Horizontal Alignment",
          "Horizontal alignment of the video content when adding borders",
          GST_TYPE_VIDEO_CONVERT_SCALE_HALIGN, DEFAULT_PROP_HALIGN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVideoConvertScale:use-fastpaths:
   *
   * Whether to use fast paths. Fast paths can lead to slightly less accurate
   * conversions.
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_USE_FASTPATHS,
      g_param_spec_boolean ("use-fastpaths",
          "Use fast paths",
          "Whether to use fast paths, which can lead to slightly less accurate conversions",
          DEFAULT_PROP_USE_FASTPATHS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Video colorspace converter and scaler",
      "Filter/Converter/Video/Scaler/Colorspace",
      "Resizes video and converts from one colorspace to another",
      "Wim Taymans <wim.taymans@gmail.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  gst_element_class_add_pad_template (element_class,
      gst_video_convert_scale_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_video_convert_scale_src_template_factory ());

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_set_context);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_change_state);

  _size_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_SIZE_STR);
  _scale_quark = gst_video_meta_transform_scale_get_quark ();
  _matrix_quark = gst_video_meta_transform_matrix_get_quark ();

  gst_type_mark_as_plugin_api (GST_TYPE_VIDEO_SCALE_METHOD, 0);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_fixate_caps);
  trans_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_filter_meta);
  trans_class->src_event =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_src_event);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_transform_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_propose_allocation);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_convert_scale_set_info);
  filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_convert_scale_transform_frame);

  klass->converts = TRUE;
  klass->scales = TRUE;

  gst_type_mark_as_plugin_api (GST_TYPE_VIDEO_CONVERT_SCALE, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_VIDEO_CONVERT_SCALE_VALIGN, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_VIDEO_CONVERT_SCALE_HALIGN, 0);
}

static void
gst_video_convert_scale_init (GstVideoConvertScale * self)
{
  GstVideoConvertScalePrivate *priv = PRIV (self);

  priv->method = DEFAULT_PROP_METHOD;
  priv->add_borders = DEFAULT_PROP_ADD_BORDERS;
  priv->sharpness = DEFAULT_PROP_SHARPNESS;
  priv->sharpen = DEFAULT_PROP_SHARPEN;
  priv->envelope = DEFAULT_PROP_ENVELOPE;
  priv->n_threads = DEFAULT_PROP_N_THREADS;
  priv->n_threads_set = FALSE;
  priv->dither = DEFAULT_PROP_DITHER;
  priv->dither_quantization = DEFAULT_PROP_DITHER_QUANTIZATION;
  priv->chroma_resampler = DEFAULT_PROP_CHROMA_RESAMPLER;
  priv->alpha_mode = DEFAULT_PROP_ALPHA_MODE;
  priv->alpha_value = DEFAULT_PROP_ALPHA_VALUE;
  priv->chroma_mode = DEFAULT_PROP_CHROMA_MODE;
  priv->matrix_mode = DEFAULT_PROP_MATRIX_MODE;
  priv->gamma_mode = DEFAULT_PROP_GAMMA_MODE;
  priv->primaries_mode = DEFAULT_PROP_PRIMARIES_MODE;
  priv->v_align = DEFAULT_PROP_VALIGN;
  priv->h_align = DEFAULT_PROP_HALIGN;
  priv->use_fastpaths = DEFAULT_PROP_USE_FASTPATHS;

  gst_video_info_init (&priv->last_frame_vinfo);

  priv->converter_config = NULL;
  priv->converter_config_changed = FALSE;
}

static void
gst_video_convert_scale_dispose (GObject * object)
{
  GstVideoConvertScalePrivate *priv = PRIV (object);

  gst_clear_object (&priv->task_pool);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_video_convert_scale_finalize (GstVideoConvertScale * self)
{
  GstVideoConvertScalePrivate *priv = PRIV (self);

  if (priv->convert)
    gst_video_converter_free (priv->convert);

  if (priv->converter_config)
    gst_structure_free (priv->converter_config);
  priv->converter_config = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (self));
}

static void
gst_video_convert_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoConvertScalePrivate *priv = PRIV (object);

  GST_OBJECT_LOCK (object);
  switch (prop_id) {
    case PROP_METHOD:
      priv->method = g_value_get_enum (value);
      break;
    case PROP_ADD_BORDERS:
      priv->add_borders = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (object);

      gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM_CAST (object));
      return;
    case PROP_SHARPNESS:
      priv->sharpness = g_value_get_double (value);
      break;
    case PROP_SHARPEN:
      priv->sharpen = g_value_get_double (value);
      break;
    case PROP_SUBMETHOD:
      priv->submethod = g_value_get_int (value);
      break;
    case PROP_ENVELOPE:
      priv->envelope = g_value_get_double (value);
      break;
    case PROP_N_THREADS:
      priv->n_threads = g_value_get_uint (value);
      priv->n_threads_set = TRUE;
      break;
    case PROP_DITHER:
      priv->dither = g_value_get_enum (value);
      break;
    case PROP_CHROMA_RESAMPLER:
      priv->chroma_resampler = g_value_get_enum (value);
      break;
    case PROP_ALPHA_MODE:
      priv->alpha_mode = g_value_get_enum (value);
      break;
    case PROP_ALPHA_VALUE:
      priv->alpha_value = g_value_get_double (value);
      break;
    case PROP_CHROMA_MODE:
      priv->chroma_mode = g_value_get_enum (value);
      break;
    case PROP_MATRIX_MODE:
      priv->matrix_mode = g_value_get_enum (value);
      break;
    case PROP_GAMMA_MODE:
      priv->gamma_mode = g_value_get_enum (value);
      break;
    case PROP_PRIMARIES_MODE:
      priv->primaries_mode = g_value_get_enum (value);
      break;
    case PROP_DITHER_QUANTIZATION:
      priv->dither_quantization = g_value_get_uint (value);
      break;
    case PROP_CONVERTER_CONFIG:
      if (priv->converter_config)
        gst_structure_free (priv->converter_config);
      priv->converter_config = g_value_dup_boxed (value);
      priv->converter_config_changed = TRUE;
      break;
    case PROP_VALIGN:
      priv->v_align = g_value_get_enum (value);
      break;
    case PROP_HALIGN:
      priv->h_align = g_value_get_enum (value);
      break;
    case PROP_USE_FASTPATHS:
      priv->use_fastpaths = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (object);
}

static void
gst_video_convert_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoConvertScalePrivate *priv = PRIV (object);

  GST_OBJECT_LOCK (object);
  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, priv->method);
      break;
    case PROP_ADD_BORDERS:
      g_value_set_boolean (value, priv->add_borders);
      break;
    case PROP_SHARPNESS:
      g_value_set_double (value, priv->sharpness);
      break;
    case PROP_SHARPEN:
      g_value_set_double (value, priv->sharpen);
      break;
    case PROP_SUBMETHOD:
      g_value_set_int (value, priv->submethod);
      break;
    case PROP_ENVELOPE:
      g_value_set_double (value, priv->envelope);
      break;
    case PROP_N_THREADS:
      g_value_set_uint (value, priv->n_threads);
      break;
    case PROP_DITHER:
      g_value_set_enum (value, priv->dither);
      break;
    case PROP_CHROMA_RESAMPLER:
      g_value_set_enum (value, priv->chroma_resampler);
      break;
    case PROP_ALPHA_MODE:
      g_value_set_enum (value, priv->alpha_mode);
      break;
    case PROP_ALPHA_VALUE:
      g_value_set_double (value, priv->alpha_value);
      break;
    case PROP_CHROMA_MODE:
      g_value_set_enum (value, priv->chroma_mode);
      break;
    case PROP_MATRIX_MODE:
      g_value_set_enum (value, priv->matrix_mode);
      break;
    case PROP_GAMMA_MODE:
      g_value_set_enum (value, priv->gamma_mode);
      break;
    case PROP_PRIMARIES_MODE:
      g_value_set_enum (value, priv->primaries_mode);
      break;
    case PROP_DITHER_QUANTIZATION:
      g_value_set_uint (value, priv->dither_quantization);
      break;
    case PROP_CONVERTER_CONFIG:
      g_value_set_boxed (value, priv->converter_config);
      break;
    case PROP_VALIGN:
      g_value_set_enum (value, priv->v_align);
      break;
    case PROP_HALIGN:
      g_value_set_enum (value, priv->h_align);
      break;
    case PROP_USE_FASTPATHS:
      g_value_set_boolean (value, priv->use_fastpaths);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (object);
}

static void
gst_video_convert_scale_set_context (GstElement * element, GstContext * context)
{
  GstVideoConvertScale *self = GST_VIDEO_CONVERT_SCALE (element);
  GstVideoConvertScalePrivate *priv = PRIV (self);

  if (gst_context_has_context_type (context, GST_TASK_POOL_CONTEXT_TYPE)) {
    GstTaskPool *pool = NULL;

    gst_context_get_task_pool (context, &pool);
    GST_DEBUG_OBJECT (self, "Got task pool %" GST_PTR_FORMAT
        " from %spersistent context", pool,
        gst_context_is_persistent (context) ? "" : "non-");
    GST_OBJECT_LOCK (self);
    gst_clear_object (&priv->task_pool);
    priv->task_pool = pool;
    priv->task_pool_from_persistent_context =
        gst_context_is_persistent (context);
    GST_OBJECT_UNLOCK (self);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstStateChangeReturn
gst_video_convert_scale_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVideoConvertScale *self = GST_VIDEO_CONVERT_SCALE (element);
  GstVideoConvertScalePrivate *priv = PRIV (self);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_OBJECT_LOCK (self);
      if (!priv->task_pool_from_persistent_context)
        gst_clear_object (&priv->task_pool);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_video_convert_is_supported_caps_features (const GstCapsFeatures * features)
{
  if (gst_caps_features_is_any (features))
    return FALSE;

  // Check if all features are supported ones
  guint n_features = gst_caps_features_get_size (features);
  for (guint i = 0; i < n_features; i++) {
    const GstIdStr *feature = gst_caps_features_get_nth_id_str (features, i);

    if (gst_id_str_is_equal_to_str (feature,
            GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
      continue;

    if (gst_id_str_is_equal_to_str (feature,
            GST_CAPS_FEATURE_FORMAT_INTERLACED))
      continue;

    if (gst_id_str_is_equal_to_str (feature,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION))
      continue;

    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_video_convert_caps_remove_format_and_rangify_size_info (GstVideoConvertScale
    * self, GstCaps * caps)
{
  GstVideoConvertScaleClass *klass = GST_VIDEO_CONVERT_SCALE_GET_CLASS (self);
  GstCaps *ret;
  GstStructure *structure;
  GstCapsFeatures *features;
  gint i, n;

  ret = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, structure, features))
      continue;

    structure = gst_structure_copy (structure);
    /* Only remove format info for the cases when we can actually convert */
    if (gst_video_convert_is_supported_caps_features (features)) {
      if (klass->scales) {
        gst_structure_set_static_str (structure, "width", GST_TYPE_INT_RANGE, 1,
            G_MAXINT, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
        /* if pixel aspect ratio, make a range of it */
        if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
          gst_structure_set_static_str (structure, "pixel-aspect-ratio",
              GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
        }
      }

      if (klass->converts) {
        gst_structure_remove_fields (structure, "format", "colorimetry",
            "chroma-site", NULL);
      }
    }
    gst_caps_append_structure_full (ret, structure,
        gst_caps_features_copy (features));
  }

  return ret;
}

static GstCaps *
gst_video_convert_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVideoConvertScale *self = GST_VIDEO_CONVERT_SCALE (trans);
  GstCaps *ret;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_video_convert_caps_remove_format_and_rangify_size_info (self, caps);
  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  return ret;
}

static gboolean
gst_video_convert_scale_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  GstVideoFilter *videofilter = GST_VIDEO_FILTER (trans);
  GstVideoConvertScale *self = GST_VIDEO_CONVERT_SCALE (trans);
  GstVideoConvertScalePrivate *priv = PRIV (self);
  const GstMetaInfo *info = meta->info;
  gboolean should_copy = TRUE;
  const gchar *valid_tags[] = {
    GST_META_TAG_VIDEO_STR,
    GST_META_TAG_VIDEO_ORIENTATION_STR,
    GST_META_TAG_VIDEO_SIZE_STR,
    /* don't copy colorspace specific metadata, FIXME, we need a MetaTransform
     * for the colorspace metadata. */
    NULL
  };

  /* Drop it as we apply the crop */
  if (info->api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  should_copy = gst_meta_api_type_tags_contain_only (info->api, valid_tags);

  /* Cant handle the tags in this meta, let the parent class handle it */
  if (!should_copy) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
        outbuf, meta, inbuf);
  }

  /* This meta is size sensitive, try to transform it accordingly */
  if (gst_meta_api_type_has_tag (info->api, _size_quark)) {
    if (info->transform_func) {
      GstVideoMetaTransformMatrix trans_matrix;
      GstVideoMetaTransform trans =
          { &videofilter->in_info, &videofilter->out_info };
      const GstVideoRectangle in_rectangle = { 0, 0,
        GST_VIDEO_INFO_WIDTH (&videofilter->in_info),
        GST_VIDEO_INFO_HEIGHT (&videofilter->in_info)
      };
      const GstVideoRectangle out_rectangle = {
        priv->dst_x, priv->dst_y,
        GST_VIDEO_INFO_WIDTH (&videofilter->out_info) - priv->borders_w,
        GST_VIDEO_INFO_HEIGHT (&videofilter->out_info) - priv->borders_h
      };

      gst_video_meta_transform_matrix_init (&trans_matrix,
          &videofilter->in_info,
          &in_rectangle, &videofilter->out_info, &out_rectangle);

      if (!info->transform_func (outbuf, meta, inbuf, _matrix_quark,
              &trans_matrix))
        info->transform_func (outbuf, meta, inbuf, _scale_quark, &trans);
    }
    return FALSE;
  }

  /* No need to transform, we can safely copy this meta */
  return TRUE;
}

static gboolean
gst_video_convert_scale_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* Passthrough is passthrough */
  if (decide_query == NULL)
    return TRUE;

  /* If we're not passthrough, we can handle crop meta as well */
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

static void
gst_video_convert_scale_post_task_pool_request (GstVideoConvertScale * self)
{
  GstVideoConvertScalePrivate *priv = PRIV (self);
  GstMessage *msg;

  GST_OBJECT_LOCK (self);
  if (priv->task_pool) {
    GST_OBJECT_UNLOCK (self);
    return;
  }
  GST_OBJECT_UNLOCK (self);

  /* Post need-context message to give application a chance to provide one */
  GST_DEBUG_OBJECT (self, "posting need-context message for task pool");
  msg = gst_message_new_need_context (GST_OBJECT_CAST (self),
      GST_TASK_POOL_CONTEXT_TYPE);
  gst_element_post_message (GST_ELEMENT (self), msg);
}

static GstVideoConverter *
gst_video_convert_scale_create_converter (GstVideoConvertScale * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info,
    GstStructure * config)
{
  GstVideoConvertScalePrivate *priv = PRIV (self);
  GstTaskPool *pool = NULL;
  GstVideoConverter *converter;

  /* Post need-context to give application a chance to provide a task pool */
  gst_video_convert_scale_post_task_pool_request (self);

  /* Check if there's a task pool from context */
  GST_OBJECT_LOCK (self);
  if (priv->task_pool) {
    pool = gst_object_ref (priv->task_pool);

    if (GST_IS_SHARED_TASK_POOL (pool)
        && (!priv->n_threads_set || priv->n_threads == 0)) {
      gint n_threads =
          gst_shared_task_pool_get_max_threads (GST_SHARED_TASK_POOL (pool));
      gst_structure_set (config, GST_VIDEO_CONVERTER_OPT_THREADS, G_TYPE_UINT,
          n_threads, NULL);

      GST_DEBUG_OBJECT (self,
          "Using shared task pool max threads %d as n-threads", n_threads);
    }
  }
  GST_OBJECT_UNLOCK (self);

  priv->last_frame_vinfo = *in_info;

  /* Create converter with the task pool (or NULL if not set) */
  if (pool) {
    GST_DEBUG_OBJECT (self, "Using task pool %" GST_PTR_FORMAT
        " for video converter", pool);
    converter =
        gst_video_converter_new_with_pool (&priv->last_frame_vinfo, out_info,
        config, pool);
  } else {
    converter = gst_video_converter_new (&priv->last_frame_vinfo, out_info,
        config);
  }

  /* Release the task pool reference */
  gst_clear_object (&pool);

  return converter;
}

static GstStructure *
gst_video_convert_scale_get_default_config (GstVideoConvertScale * self,
    GstVideoInfo * out_info)
{
  GstVideoConvertScalePrivate *priv = PRIV (self);
  GstStructure *options;

  options = gst_structure_new_static_str_empty ("videoconvertscale");

  switch (priv->method) {
    case GST_VIDEO_SCALE_NEAREST:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_NEAREST,
          NULL);
      break;
    case GST_VIDEO_SCALE_BILINEAR:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LINEAR,
          GST_VIDEO_RESAMPLER_OPT_MAX_TAPS, G_TYPE_INT, 2, NULL);
      break;
    case GST_VIDEO_SCALE_4TAP:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_SINC,
          GST_VIDEO_RESAMPLER_OPT_MAX_TAPS, G_TYPE_INT, 4, NULL);
      break;
    case GST_VIDEO_SCALE_LANCZOS:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LANCZOS,
          NULL);
      break;
    case GST_VIDEO_SCALE_BILINEAR2:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LINEAR,
          NULL);
      break;
    case GST_VIDEO_SCALE_SINC:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_SINC,
          NULL);
      break;
    case GST_VIDEO_SCALE_HERMITE:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 0.0,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 0.0, NULL);
      break;
    case GST_VIDEO_SCALE_SPLINE:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 1.0,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 0.0, NULL);
      break;
    case GST_VIDEO_SCALE_CATROM:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 0.0,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 0.5, NULL);
      break;
    case GST_VIDEO_SCALE_MITCHELL:
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
          GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 1.0 / 3.0,
          GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 1.0 / 3.0,
          NULL);
      break;
  }

  gst_structure_set_static_str (options,
      GST_VIDEO_RESAMPLER_OPT_ENVELOPE, G_TYPE_DOUBLE, priv->envelope,
      GST_VIDEO_RESAMPLER_OPT_SHARPNESS, G_TYPE_DOUBLE, priv->sharpness,
      GST_VIDEO_RESAMPLER_OPT_SHARPEN, G_TYPE_DOUBLE, priv->sharpen,
      GST_VIDEO_CONVERTER_OPT_DEST_X, G_TYPE_INT, priv->dst_x,
      GST_VIDEO_CONVERTER_OPT_DEST_Y, G_TYPE_INT, priv->dst_y,
      GST_VIDEO_CONVERTER_OPT_DEST_WIDTH, G_TYPE_INT,
      out_info->width - priv->borders_w, GST_VIDEO_CONVERTER_OPT_DEST_HEIGHT,
      G_TYPE_INT, out_info->height - priv->borders_h,
      GST_VIDEO_CONVERTER_OPT_DITHER_METHOD, GST_TYPE_VIDEO_DITHER_METHOD,
      priv->dither, GST_VIDEO_CONVERTER_OPT_DITHER_QUANTIZATION, G_TYPE_UINT,
      priv->dither_quantization,
      GST_VIDEO_CONVERTER_OPT_CHROMA_RESAMPLER_METHOD,
      GST_TYPE_VIDEO_RESAMPLER_METHOD, priv->chroma_resampler,
      GST_VIDEO_CONVERTER_OPT_ALPHA_MODE, GST_TYPE_VIDEO_ALPHA_MODE,
      priv->alpha_mode, GST_VIDEO_CONVERTER_OPT_ALPHA_VALUE, G_TYPE_DOUBLE,
      priv->alpha_value, GST_VIDEO_CONVERTER_OPT_CHROMA_MODE,
      GST_TYPE_VIDEO_CHROMA_MODE, priv->chroma_mode,
      GST_VIDEO_CONVERTER_OPT_MATRIX_MODE, GST_TYPE_VIDEO_MATRIX_MODE,
      priv->matrix_mode, GST_VIDEO_CONVERTER_OPT_GAMMA_MODE,
      GST_TYPE_VIDEO_GAMMA_MODE, priv->gamma_mode,
      GST_VIDEO_CONVERTER_OPT_PRIMARIES_MODE, GST_TYPE_VIDEO_PRIMARIES_MODE,
      priv->primaries_mode, GST_VIDEO_CONVERTER_OPT_THREADS, G_TYPE_UINT,
      priv->n_threads, GST_VIDEO_CONVERTER_OPT_USE_FASTPATHS,
      G_TYPE_BOOLEAN, priv->use_fastpaths, NULL);

  return options;
}

static gboolean
gst_video_convert_scale_set_info (GstVideoFilter * filter, GstCaps * in,
    GstVideoInfo * in_info, GstCaps * out, GstVideoInfo * out_info)
{
  GstVideoConvertScale *self = GST_VIDEO_CONVERT_SCALE (filter);
  GstVideoConvertScalePrivate *priv = PRIV (self);
  GstVideoConvertScaleClass *klass = GST_VIDEO_CONVERT_SCALE_GET_CLASS (self);
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  GstVideoInfo tmp_info;
  GstStructure *options;

  /* A scale-only element keeps the input colorimetry. With no colorimetry
   * field on the output caps, out_info holds the resolution-derived default,
   * so take the input's instead. */
  if (!klass->converts
      && !gst_structure_has_field (gst_caps_get_structure (out, 0),
          "colorimetry"))
    out_info->colorimetry = in_info->colorimetry;

  if (priv->convert) {
    gst_video_converter_free (priv->convert);
    priv->convert = NULL;
  }

  if (!gst_util_fraction_multiply (in_info->width,
          in_info->height, in_info->par_n, in_info->par_d, &from_dar_n,
          &from_dar_d)) {
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (out_info->width,
          out_info->height, out_info->par_n, out_info->par_d, &to_dar_n,
          &to_dar_d)) {
    to_dar_n = to_dar_d = -1;
  }

  priv->borders_w = priv->borders_h = 0;
  priv->dst_x = 0;
  priv->dst_y = 0;
  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (priv->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d,
              out_info->par_d, out_info->par_n, &n, &d)) {
        to_h = gst_util_uint64_scale_int (out_info->width, d, n);
        if (to_h <= out_info->height) {
          priv->borders_h = out_info->height - to_h;
          priv->borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (out_info->height, n, d);
          g_assert (to_w <= out_info->width);
          priv->borders_h = 0;
          priv->borders_w = out_info->width - to_w;
        }
      } else {
        GST_WARNING_OBJECT (self, "Can't calculate borders");
      }
    } else {
      GST_DEBUG_OBJECT (self, "Can't keep DAR!");
    }
  }

  switch (priv->h_align) {
    case GST_VIDEO_CONVERT_SCALE_HALIGN_LEFT:
      priv->dst_x = 0;
      break;
    case GST_VIDEO_CONVERT_SCALE_HALIGN_CENTER:
      priv->dst_x = priv->borders_w / 2;
      break;
    case GST_VIDEO_CONVERT_SCALE_HALIGN_RIGHT:
      priv->dst_x = priv->borders_w;
      break;
  }

  switch (priv->v_align) {
    case GST_VIDEO_CONVERT_SCALE_VALIGN_TOP:
      priv->dst_y = 0;
      break;
    case GST_VIDEO_CONVERT_SCALE_VALIGN_CENTER:
      priv->dst_y = priv->borders_h / 2;
      break;
    case GST_VIDEO_CONVERT_SCALE_VALIGN_BOTTOM:
      priv->dst_y = priv->borders_h;
      break;
  }

  /* if present, these must match */
  if (in_info->interlace_mode != out_info->interlace_mode)
    goto format_mismatch;

  /* Reset the crop meta memory */
  priv->crop_x = priv->crop_y = priv->crop_width = priv->crop_height = 0;

  if (priv->converter_config) {
    options = gst_structure_copy (priv->converter_config);
    GST_DEBUG_OBJECT (self,
        "Using user-provided converter-config: %" GST_PTR_FORMAT, options);
    goto build_converter;
  }

  /* if the only thing different in the caps is the transfer function, and
   * we're converting between equivalent transfer functions and not
   * quantizing/dithering or adjusting alpha, then do passthrough */
  tmp_info = *in_info;
  tmp_info.colorimetry.transfer = out_info->colorimetry.transfer;

  gboolean need_dither = (priv->dither_quantization > 1)
      && (priv->dither != GST_VIDEO_DITHER_NONE);
  gboolean need_alpha = GST_VIDEO_INFO_HAS_ALPHA (out_info)
      && ((priv->alpha_mode == GST_VIDEO_ALPHA_MODE_SET) ||
      (priv->alpha_mode == GST_VIDEO_ALPHA_MODE_MULT
          && priv->alpha_value != 1.0));

  if (gst_video_info_is_equal (&tmp_info, out_info) &&
      gst_video_transfer_function_is_equivalent (in_info->colorimetry.transfer,
          in_info->finfo->bits, out_info->colorimetry.transfer,
          out_info->finfo->bits) && !need_dither && !need_alpha) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
  } else {
    GST_CAT_DEBUG_OBJECT (CAT_PERFORMANCE, filter, "setup videoscaling");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

    options = gst_video_convert_scale_get_default_config (self, out_info);

  build_converter:
    priv->convert =
        gst_video_convert_scale_create_converter (self, in_info, out_info,
        options);
    if (priv->convert == NULL)
      goto no_convert;
  }

  GST_DEBUG_OBJECT (filter, "converting format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
  GST_DEBUG_OBJECT (self, "from=%dx%d (par=%d/%d dar=%d/%d), size %"
      G_GSIZE_FORMAT " -> to=%dx%d (par=%d/%d dar=%d/%d borders=%d:%d), "
      "size %" G_GSIZE_FORMAT,
      in_info->width, in_info->height, in_info->par_n, in_info->par_d,
      from_dar_n, from_dar_d, in_info->size, out_info->width,
      out_info->height, out_info->par_n, out_info->par_d, to_dar_n, to_dar_d,
      priv->borders_w, priv->borders_h, out_info->size);

  return TRUE;

  /* ERRORS */
format_mismatch:
  {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }
no_convert:
  {
    GST_ERROR_OBJECT (self, "could not create converter");
    return FALSE;
  }
}

static GstCaps *
gst_video_convert_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstVideoConvertScaleClass *klass = GST_VIDEO_CONVERT_SCALE_GET_CLASS (base);
  GstVideoConvertScaleFixateFlags flags =
      GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_NONE;
  GstCaps *result;

  if (klass->converts)
    flags |= GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT;
  if (klass->scales)
    flags |= GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (base), direction,
      GST_VIDEO_ORIENTATION_IDENTITY, NULL, flags, caps, othercaps, NULL);
  if (!result)
    return othercaps;

  gst_clear_caps (&othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result))
      gst_caps_replace (&result, caps);
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, result);

  return result;
}

#define GET_LINE(frame, line) \
    (gpointer)(((guint8*)(GST_VIDEO_FRAME_PLANE_DATA (frame, 0))) + \
     GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) * (line))

static GstFlowReturn
gst_video_convert_scale_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstVideoConvertScalePrivate *priv = PRIV (filter);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCropMeta *cmeta;
  gboolean update_config = FALSE;

  GST_CAT_DEBUG_OBJECT (CAT_PERFORMANCE, filter, "doing video scaling");

  cmeta = gst_buffer_get_video_crop_meta (in_frame->buffer);
  if (cmeta && (cmeta->x != priv->crop_x ||
          cmeta->y != priv->crop_y ||
          cmeta->width != priv->crop_width ||
          cmeta->height != priv->crop_height))
    update_config = TRUE;
  if (!cmeta && (priv->crop_width != 0 || priv->crop_height != 0))
    update_config = TRUE;

  if (cmeta) {
    priv->crop_x = cmeta->x;
    priv->crop_y = cmeta->y;
    priv->crop_width = cmeta->width;
    priv->crop_height = cmeta->height;
  } else {
    priv->crop_x = priv->crop_y = priv->crop_width = priv->crop_height = 0;
  }

  if (priv->converter_config_changed || update_config ||
      !gst_video_info_is_equal (&priv->last_frame_vinfo, &in_frame->info)) {
    GstStructure *options;
    GstVideoInfo converter_in_info = in_frame->info;

    if (priv->converter_config)
      options = gst_structure_copy (priv->converter_config);
    else if (priv->converter_config_changed)
      /* This is the case where the "converter-config" property was previously
       * set, but has since been unset, so we reset to the default config
       * generated from the other properties.
       */
      options =
          gst_video_convert_scale_get_default_config
          (GST_VIDEO_CONVERT_SCALE (filter), &filter->out_info);
    else
      options =
          gst_structure_copy (gst_video_converter_get_config (priv->convert));

    if (cmeta) {
      gst_structure_set_static_str (options,
          GST_VIDEO_CONVERTER_OPT_SRC_X, G_TYPE_INT, cmeta->x,
          GST_VIDEO_CONVERTER_OPT_SRC_Y, G_TYPE_INT, cmeta->y,
          GST_VIDEO_CONVERTER_OPT_SRC_WIDTH, G_TYPE_INT, cmeta->width,
          GST_VIDEO_CONVERTER_OPT_SRC_HEIGHT, G_TYPE_INT, cmeta->height, NULL);
    } else if (!priv->converter_config) {
      /* The meta may describe padded dimensions. Keep its layout, but use the
       * visible dimensions from caps to avoid resampling the padding. */
      converter_in_info.width = filter->in_info.width;
      converter_in_info.height = filter->in_info.height;
      gst_structure_remove_fields (options,
          GST_VIDEO_CONVERTER_OPT_SRC_X,
          GST_VIDEO_CONVERTER_OPT_SRC_Y,
          GST_VIDEO_CONVERTER_OPT_SRC_WIDTH,
          GST_VIDEO_CONVERTER_OPT_SRC_HEIGHT, NULL);
    }

    gst_video_converter_free (priv->convert);
    priv->convert =
        gst_video_convert_scale_create_converter (GST_VIDEO_CONVERT_SCALE
        (filter), &converter_in_info, &filter->out_info, options);
    priv->last_frame_vinfo = in_frame->info;

    priv->converter_config_changed = FALSE;
  }


  gst_video_converter_frame (priv->convert, in_frame, out_frame);

  return ret;
}

static gboolean
gst_video_convert_scale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoConvertScale *self = GST_VIDEO_CONVERT_SCALE_CAST (trans);
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  gboolean ret;
  gdouble x, y;

  GST_DEBUG_OBJECT (self, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      if (filter->in_info.width != filter->out_info.width ||
          filter->in_info.height != filter->out_info.height) {
        event = gst_event_make_writable (event);

        if (gst_navigation_event_get_coordinates (event, &x, &y)) {
          gst_navigation_event_set_coordinates (event,
              x * filter->in_info.width / filter->out_info.width,
              y * filter->in_info.height / filter->out_info.height);
        }
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}
