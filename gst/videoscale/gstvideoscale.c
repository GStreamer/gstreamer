/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
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
 * SECTION:element-videoscale
 * @see_also: videorate, videoconvert
 *
 * This element resizes video frames. By default the element will try to
 * negotiate to the same size on the source and sinkpad so that no scaling
 * is needed. It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 *
 * This element supports a wide range of color spaces including various YUV and
 * RGB formats and is therefore generally able to operate anywhere in a
 * pipeline.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoconvert ! videoscale ! autovideosink
 * ]| Decode an Ogg/Theora and display the video. If the video sink chosen
 * cannot perform scaling, the video scaling will be performed by videoscale
 * when you resize the video window.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * |[
 * gst-launch-1.0 -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoconvert ! videoscale ! video/x-raw,width=100 ! autovideosink
 * ]| Decode an Ogg/Theora and display the video with a width of 100.
 * </refsect2>
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

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstvideoscale.h"

#define GST_CAT_DEFAULT video_scale_debug
GST_DEBUG_CATEGORY_STATIC (video_scale_debug);
GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);

#define DEFAULT_PROP_METHOD       GST_VIDEO_SCALE_BILINEAR
#define DEFAULT_PROP_ADD_BORDERS  TRUE
#define DEFAULT_PROP_SHARPNESS    1.0
#define DEFAULT_PROP_SHARPEN      0.0
#define DEFAULT_PROP_DITHER       FALSE
#define DEFAULT_PROP_SUBMETHOD    1
#define DEFAULT_PROP_ENVELOPE     2.0
#define DEFAULT_PROP_GAMMA_DECODE FALSE

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
  PROP_GAMMA_DECODE,
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

static GstStaticCaps gst_video_scale_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));

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

static GstCaps *
gst_video_scale_get_capslist (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_video_scale_format_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_video_scale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_video_scale_get_capslist ());
}

static GstPadTemplate *
gst_video_scale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_video_scale_get_capslist ());
}


static void gst_video_scale_finalize (GstVideoScale * videoscale);
static gboolean gst_video_scale_src_event (GstBaseTransform * trans,
    GstEvent * event);

/* base transform vmethods */
static GstCaps *gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_video_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static gboolean gst_video_scale_set_info (GstVideoFilter * filter,
    GstCaps * in, GstVideoInfo * in_info, GstCaps * out,
    GstVideoInfo * out_info);
static GstFlowReturn gst_video_scale_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in, GstVideoFrame * out);

static void gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_video_scale_parent_class parent_class
G_DEFINE_TYPE (GstVideoScale, gst_video_scale, GST_TYPE_VIDEO_FILTER);

static void
gst_video_scale_class_init (GstVideoScaleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *filter_class = (GstVideoFilterClass *) klass;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_video_scale_finalize;
  gobject_class->set_property = gst_video_scale_set_property;
  gobject_class->get_property = gst_video_scale_get_property;

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
      g_param_spec_boolean ("dither", "Dither",
          "Add dither (only used for Lanczos method)",
          DEFAULT_PROP_DITHER,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#if 0
  /* I am hiding submethod for now, since it's poorly named, poorly
   * documented, and will probably just get people into trouble. */
  g_object_class_install_property (gobject_class, PROP_SUBMETHOD,
      g_param_spec_int ("submethod", "submethod",
          "submethod", 0, 3, DEFAULT_PROP_SUBMETHOD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_ENVELOPE,
      g_param_spec_double ("envelope", "Envelope",
          "Size of filter envelope", 1.0, 5.0, DEFAULT_PROP_ENVELOPE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GAMMA_DECODE,
      g_param_spec_boolean ("gamma-decode", "Gamma Decode",
          "Decode gamma before scaling", DEFAULT_PROP_GAMMA_DECODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_set_static_metadata (element_class,
      "Video scaler", "Filter/Converter/Video/Scaler",
      "Resizes video", "Wim Taymans <wim.taymans@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_video_scale_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_video_scale_src_template_factory ());

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_scale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_scale_fixate_caps);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_video_scale_src_event);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_scale_set_info);
  filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_scale_transform_frame);
}

static void
gst_video_scale_init (GstVideoScale * videoscale)
{
  videoscale->method = DEFAULT_PROP_METHOD;
  videoscale->add_borders = DEFAULT_PROP_ADD_BORDERS;
  videoscale->submethod = DEFAULT_PROP_SUBMETHOD;
  videoscale->sharpness = DEFAULT_PROP_SHARPNESS;
  videoscale->sharpen = DEFAULT_PROP_SHARPEN;
  videoscale->dither = DEFAULT_PROP_DITHER;
  videoscale->envelope = DEFAULT_PROP_ENVELOPE;
  videoscale->gamma_decode = DEFAULT_PROP_GAMMA_DECODE;
}

static void
gst_video_scale_finalize (GstVideoScale * videoscale)
{
  if (videoscale->convert)
    gst_video_converter_free (videoscale->convert);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (videoscale));
}

static void
gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoScale *vscale = GST_VIDEO_SCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      GST_OBJECT_LOCK (vscale);
      vscale->method = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_ADD_BORDERS:
      GST_OBJECT_LOCK (vscale);
      vscale->add_borders = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (vscale);
      gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM_CAST (vscale));
      break;
    case PROP_SHARPNESS:
      GST_OBJECT_LOCK (vscale);
      vscale->sharpness = g_value_get_double (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_SHARPEN:
      GST_OBJECT_LOCK (vscale);
      vscale->sharpen = g_value_get_double (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_DITHER:
      GST_OBJECT_LOCK (vscale);
      vscale->dither = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_SUBMETHOD:
      GST_OBJECT_LOCK (vscale);
      vscale->submethod = g_value_get_int (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_ENVELOPE:
      GST_OBJECT_LOCK (vscale);
      vscale->envelope = g_value_get_double (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_GAMMA_DECODE:
      GST_OBJECT_LOCK (vscale);
      vscale->gamma_decode = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_scale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoScale *vscale = GST_VIDEO_SCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      GST_OBJECT_LOCK (vscale);
      g_value_set_enum (value, vscale->method);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_ADD_BORDERS:
      GST_OBJECT_LOCK (vscale);
      g_value_set_boolean (value, vscale->add_borders);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_SHARPNESS:
      GST_OBJECT_LOCK (vscale);
      g_value_set_double (value, vscale->sharpness);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_SHARPEN:
      GST_OBJECT_LOCK (vscale);
      g_value_set_double (value, vscale->sharpen);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_DITHER:
      GST_OBJECT_LOCK (vscale);
      g_value_set_boolean (value, vscale->dither);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_SUBMETHOD:
      GST_OBJECT_LOCK (vscale);
      g_value_set_int (value, vscale->submethod);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_ENVELOPE:
      GST_OBJECT_LOCK (vscale);
      g_value_set_double (value, vscale->envelope);
      GST_OBJECT_UNLOCK (vscale);
      break;
    case PROP_GAMMA_DECODE:
      GST_OBJECT_LOCK (vscale);
      g_value_set_boolean (value, vscale->gamma_decode);
      GST_OBJECT_UNLOCK (vscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstStructure *structure;
  GstCapsFeatures *features;
  gint i, n;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, structure, features))
      continue;

    /* make copy */
    structure = gst_structure_copy (structure);

    /* If the features are non-sysmem we can only do passthrough */
    if (!gst_caps_features_is_any (features)
        && gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
      gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
        gst_structure_set (structure, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }
    gst_caps_append_structure_full (ret, structure,
        gst_caps_features_copy (features));
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_video_scale_set_info (GstVideoFilter * filter, GstCaps * in,
    GstVideoInfo * in_info, GstCaps * out, GstVideoInfo * out_info)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (filter);
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;

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

  videoscale->borders_w = videoscale->borders_h = 0;
  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (videoscale->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d,
              out_info->par_d, out_info->par_n, &n, &d)) {
        to_h = gst_util_uint64_scale_int (out_info->width, d, n);
        if (to_h <= out_info->height) {
          videoscale->borders_h = out_info->height - to_h;
          videoscale->borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (out_info->height, n, d);
          g_assert (to_w <= out_info->width);
          videoscale->borders_h = 0;
          videoscale->borders_w = out_info->width - to_w;
        }
      } else {
        GST_WARNING_OBJECT (videoscale, "Can't calculate borders");
      }
    } else {
      GST_WARNING_OBJECT (videoscale, "Can't keep DAR!");
    }
  }

  if (in_info->width == out_info->width && in_info->height == out_info->height
      && videoscale->borders_w == 0 && videoscale->borders_h == 0) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
  } else {
    GstStructure *options;
    GST_CAT_DEBUG_OBJECT (CAT_PERFORMANCE, filter, "setup videoscaling");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

    options = gst_structure_new_empty ("videoscale");

    switch (videoscale->method) {
      case GST_VIDEO_SCALE_NEAREST:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_NEAREST,
            NULL);
        break;
      case GST_VIDEO_SCALE_BILINEAR:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LINEAR,
            GST_VIDEO_RESAMPLER_OPT_MAX_TAPS, G_TYPE_INT, 2, NULL);
        break;
      case GST_VIDEO_SCALE_4TAP:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_SINC,
            GST_VIDEO_RESAMPLER_OPT_MAX_TAPS, G_TYPE_INT, 4, NULL);
        break;
      case GST_VIDEO_SCALE_LANCZOS:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LANCZOS,
            NULL);
        break;
      case GST_VIDEO_SCALE_BILINEAR2:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_LINEAR,
            NULL);
        break;
      case GST_VIDEO_SCALE_SINC:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_SINC,
            NULL);
        break;
      case GST_VIDEO_SCALE_HERMITE:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 0.0,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 0.0,
            NULL);
        break;
      case GST_VIDEO_SCALE_SPLINE:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 1.0,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 0.0,
            NULL);
        break;
      case GST_VIDEO_SCALE_CATROM:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 0.0,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 0.5,
            NULL);
        break;
      case GST_VIDEO_SCALE_MITCHELL:
        gst_structure_set (options,
            GST_VIDEO_CONVERTER_OPT_RESAMPLER_METHOD,
            GST_TYPE_VIDEO_RESAMPLER_METHOD, GST_VIDEO_RESAMPLER_METHOD_CUBIC,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_B, G_TYPE_DOUBLE, (gdouble) 1.0 / 3.0,
            GST_VIDEO_RESAMPLER_OPT_CUBIC_C, G_TYPE_DOUBLE, (gdouble) 1.0 / 3.0,
            NULL);
        break;
    }
    gst_structure_set (options,
        GST_VIDEO_RESAMPLER_OPT_ENVELOPE, G_TYPE_DOUBLE, videoscale->envelope,
        GST_VIDEO_RESAMPLER_OPT_SHARPNESS, G_TYPE_DOUBLE, videoscale->sharpness,
        GST_VIDEO_RESAMPLER_OPT_SHARPEN, G_TYPE_DOUBLE, videoscale->sharpen,
        GST_VIDEO_CONVERTER_OPT_DEST_X, G_TYPE_INT, videoscale->borders_w / 2,
        GST_VIDEO_CONVERTER_OPT_DEST_Y, G_TYPE_INT, videoscale->borders_h / 2,
        GST_VIDEO_CONVERTER_OPT_DEST_WIDTH, G_TYPE_INT,
        out_info->width - videoscale->borders_w,
        GST_VIDEO_CONVERTER_OPT_DEST_HEIGHT, G_TYPE_INT,
        out_info->height - videoscale->borders_h,
        GST_VIDEO_CONVERTER_OPT_MATRIX_MODE, GST_TYPE_VIDEO_MATRIX_MODE,
        GST_VIDEO_MATRIX_MODE_NONE, GST_VIDEO_CONVERTER_OPT_DITHER_METHOD,
        GST_TYPE_VIDEO_DITHER_METHOD, GST_VIDEO_DITHER_NONE,
        GST_VIDEO_CONVERTER_OPT_CHROMA_MODE, GST_TYPE_VIDEO_CHROMA_MODE,
        GST_VIDEO_CHROMA_MODE_NONE, NULL);

    if (videoscale->gamma_decode) {
      gst_structure_set (options,
          GST_VIDEO_CONVERTER_OPT_GAMMA_MODE, GST_TYPE_VIDEO_GAMMA_MODE,
          GST_VIDEO_GAMMA_MODE_REMAP, NULL);
    }

    if (videoscale->convert)
      gst_video_converter_free (videoscale->convert);
    videoscale->convert = gst_video_converter_new (in_info, out_info, options);
  }

  GST_DEBUG_OBJECT (videoscale, "from=%dx%d (par=%d/%d dar=%d/%d), size %"
      G_GSIZE_FORMAT " -> to=%dx%d (par=%d/%d dar=%d/%d borders=%d:%d), "
      "size %" G_GSIZE_FORMAT,
      in_info->width, in_info->height, in_info->par_n, in_info->par_d,
      from_dar_n, from_dar_d, in_info->size, out_info->width,
      out_info->height, out_info->par_n, out_info->par_d, to_dar_n, to_dar_d,
      videoscale->borders_w, videoscale->borders_h, out_info->size);

  return TRUE;
}

static GstCaps *
gst_video_scale_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
  0,};

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (base, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the height that was nearest to the orignal
       * height and the nearest possible width. This changes the DAR but
       * there's not much else to do here.
       */
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

#define GET_LINE(frame, line) \
    (gpointer)(((guint8*)(GST_VIDEO_FRAME_PLANE_DATA (frame, 0))) + \
     GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0) * (line))

static GstFlowReturn
gst_video_scale_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE_CAST (filter);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_CAT_DEBUG_OBJECT (CAT_PERFORMANCE, filter, "doing video scaling");

  gst_video_converter_frame (videoscale->convert, in_frame, out_frame);

  return ret;
}

static gboolean
gst_video_scale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE_CAST (trans);
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  gboolean ret;
  gdouble a;
  GstStructure *structure;

  GST_DEBUG_OBJECT (videoscale, "handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      if (filter->in_info.width != filter->out_info.width ||
          filter->in_info.height != filter->out_info.height) {
        event =
            GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

        structure = (GstStructure *) gst_event_get_structure (event);
        if (gst_structure_get_double (structure, "pointer_x", &a)) {
          gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
              a * filter->in_info.width / filter->out_info.width, NULL);
        }
        if (gst_structure_get_double (structure, "pointer_y", &a)) {
          gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
              a * filter->in_info.height / filter->out_info.height, NULL);
        }
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "videoscale", GST_RANK_NONE,
          GST_TYPE_VIDEO_SCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (video_scale_debug, "videoscale", 0,
      "videoscale element");
  GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoscale,
    "Resizes video", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
