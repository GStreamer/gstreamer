/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
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
 */

/**
 * SECTION:element-videoscale
 * @see_also: videorate, ffmpegcolorspace
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
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! ffmpegcolorspace ! videoscale ! ximagesink
 * ]| Decode an Ogg/Theora and display the video using ximagesink. Since
 * ximagesink cannot perform scaling, the video scaling will be performed by
 * videoscale when you resize the video window.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * |[
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videoscale ! video/x-raw-yuv, width=50 ! xvimagesink
 * ]| Decode an Ogg/Theora and display the video using xvimagesink with a width
 * of 50.
 * </refsect2>
 *
 * Last reviewed on 2006-03-02 (0.10.4)
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

#include "gstvideoscale.h"
#include "gstvideoscaleorc.h"
#include "vs_image.h"
#include "vs_4tap.h"
#include "vs_fill_borders.h"

/* debug variable definition */
GST_DEBUG_CATEGORY (video_scale_debug);

#define DEFAULT_PROP_METHOD       GST_VIDEO_SCALE_BILINEAR
#define DEFAULT_PROP_ADD_BORDERS  FALSE
#define DEFAULT_PROP_SHARPNESS    1.0
#define DEFAULT_PROP_SHARPEN      0.0
#define DEFAULT_PROP_DITHER       FALSE
#define DEFAULT_PROP_SUBMETHOD    1
#define DEFAULT_PROP_ENVELOPE     2.0

enum
{
  PROP_0,
  PROP_METHOD,
  PROP_ADD_BORDERS,
  PROP_SHARPNESS,
  PROP_SHARPEN,
  PROP_DITHER,
  PROP_SUBMETHOD,
  PROP_ENVELOPE
};

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767]"

static GstStaticCaps gst_video_scale_format_caps[] = {
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_ARGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRA),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_ABGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xBGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y444")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("v308")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y42B")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YVYU")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("UYVY")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YV12")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y41B")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_16),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_15),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_GRAY16 ("BYTE_ORDER")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y16 ")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_GRAY8),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y800")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y8  ")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("GREY")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AY64")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_ARGB_64)
};

#define GST_TYPE_VIDEO_SCALE_METHOD (gst_video_scale_method_get_type())
static GType
gst_video_scale_method_get_type (void)
{
  static GType video_scale_method_type = 0;

  static const GEnumValue video_scale_methods[] = {
    {GST_VIDEO_SCALE_NEAREST, "Nearest Neighbour", "nearest-neighbour"},
    {GST_VIDEO_SCALE_BILINEAR, "Bilinear", "bilinear"},
    {GST_VIDEO_SCALE_4TAP, "4-tap", "4-tap"},
    {GST_VIDEO_SCALE_LANCZOS, "Lanczos", "lanczos"},
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
    gint i;

    g_assert (caps == NULL);

    caps = gst_caps_new_empty ();
    for (i = 0; i < G_N_ELEMENTS (gst_video_scale_format_caps); i++)
      gst_caps_append (caps,
          gst_caps_make_writable
          (gst_static_caps_get (&gst_video_scale_format_caps[i])));
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

static GstPadTemplate *
gst_video_scale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_ref (gst_video_scale_get_capslist ()));
}

static GstPadTemplate *
gst_video_scale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_ref (gst_video_scale_get_capslist ()));
}


static void gst_video_scale_finalize (GstVideoScale * videoscale);
static gboolean gst_video_scale_src_event (GstBaseTransform * trans,
    GstEvent * event);

/* base transform vmethods */
static GstCaps *gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_video_scale_set_caps (GstBaseTransform * trans,
    GstCaps * in, GstCaps * out);
static gboolean gst_video_scale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_video_scale_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);
static void gst_video_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static void gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstVideoScale, gst_video_scale, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void
gst_video_scale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *pad_template;

  gst_element_class_set_details_simple (element_class,
      "Video scaler", "Filter/Converter/Video/Scaler",
      "Resizes video", "Wim Taymans <wim.taymans@chello.be>");

  pad_template = gst_video_scale_sink_template_factory ();
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
  pad_template = gst_video_scale_src_template_factory ();
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
}

static void
gst_video_scale_class_init (GstVideoScaleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

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
          "Sharpness of filter", 0.0, 2.0, DEFAULT_PROP_SHARPNESS,
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
          "Size of filter envelope", 0.0, 5.0, DEFAULT_PROP_ENVELOPE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_scale_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_scale_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_scale_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_video_scale_transform);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_scale_fixate_caps);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_video_scale_src_event);
}

static void
gst_video_scale_init (GstVideoScale * videoscale, GstVideoScaleClass * klass)
{
  videoscale->tmp_buf = NULL;
  videoscale->method = DEFAULT_PROP_METHOD;
  videoscale->add_borders = DEFAULT_PROP_ADD_BORDERS;
  videoscale->submethod = DEFAULT_PROP_SUBMETHOD;
  videoscale->sharpness = DEFAULT_PROP_SHARPNESS;
  videoscale->sharpen = DEFAULT_PROP_SHARPEN;
  videoscale->dither = DEFAULT_PROP_DITHER;
  videoscale->envelope = DEFAULT_PROP_ENVELOPE;
}

static void
gst_video_scale_finalize (GstVideoScale * videoscale)
{
  if (videoscale->tmp_buf)
    g_free (videoscale->tmp_buf);

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
      gst_base_transform_reconfigure (GST_BASE_TRANSFORM_CAST (vscale));
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *ret;
  GstStructure *structure;

  /* this function is always called with a simple caps */
  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_caps_copy (caps);
  structure = gst_structure_copy (gst_caps_get_structure (ret, 0));

  gst_structure_set (structure,
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

  /* if pixel aspect ratio, make a range of it */
  if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
    gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE,
        1, G_MAXINT, G_MAXINT, 1, NULL);
  }
  gst_caps_append_structure (ret, structure);

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_video_scale_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  gboolean ret;
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  gint from_par_n, from_par_d, to_par_n, to_par_d;

  ret =
      gst_video_format_parse_caps (in, &videoscale->format,
      &videoscale->from_width, &videoscale->from_height);
  ret &=
      gst_video_format_parse_caps (out, NULL, &videoscale->to_width,
      &videoscale->to_height);
  if (!ret)
    goto done;

  videoscale->src_size = gst_video_format_get_size (videoscale->format,
      videoscale->from_width, videoscale->from_height);
  videoscale->dest_size = gst_video_format_get_size (videoscale->format,
      videoscale->to_width, videoscale->to_height);

  if (!gst_video_parse_caps_pixel_aspect_ratio (in, &from_par_n, &from_par_d))
    from_par_n = from_par_d = 1;
  if (!gst_video_parse_caps_pixel_aspect_ratio (out, &to_par_n, &to_par_d))
    to_par_n = to_par_d = 1;

  if (!gst_util_fraction_multiply (videoscale->from_width,
          videoscale->from_height, from_par_n, from_par_d, &from_dar_n,
          &from_dar_d)) {
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (videoscale->to_width,
          videoscale->to_height, to_par_n, to_par_d, &to_dar_n, &to_dar_d)) {
    to_dar_n = to_dar_d = -1;
  }

  videoscale->borders_w = videoscale->borders_h = 0;
  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (videoscale->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &n, &d)) {
        to_h = gst_util_uint64_scale_int (videoscale->to_width, d, n);
        if (to_h <= videoscale->to_height) {
          videoscale->borders_h = videoscale->to_height - to_h;
          videoscale->borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (videoscale->to_height, n, d);
          g_assert (to_w <= videoscale->to_width);
          videoscale->borders_h = 0;
          videoscale->borders_w = videoscale->to_width - to_w;
        }
      } else {
        GST_WARNING_OBJECT (videoscale, "Can't calculate borders");
      }
    } else {
      GST_WARNING_OBJECT (videoscale, "Can't keep DAR!");
    }
  }

  if (videoscale->tmp_buf)
    g_free (videoscale->tmp_buf);
  videoscale->tmp_buf = g_malloc (videoscale->to_width * 8 * 4);

  gst_base_transform_set_passthrough (trans,
      (videoscale->from_width == videoscale->to_width
          && videoscale->from_height == videoscale->to_height));

  GST_DEBUG_OBJECT (videoscale, "from=%dx%d (par=%d/%d dar=%d/%d), size %d "
      "-> to=%dx%d (par=%d/%d dar=%d/%d borders=%d:%d), size %d",
      videoscale->from_width, videoscale->from_height, from_par_n, from_par_d,
      from_dar_n, from_dar_d, videoscale->src_size, videoscale->to_width,
      videoscale->to_height, to_par_n, to_par_d, to_dar_n, to_dar_d,
      videoscale->borders_w, videoscale->borders_h, videoscale->dest_size);

done:
  return ret;
}

static gboolean
gst_video_scale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstVideoFormat format;
  gint width, height;

  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    return FALSE;

  *size = gst_video_format_get_size (format, width, height);

  return TRUE;
}

static void
gst_video_scale_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
  0,};

  g_return_if_fail (gst_caps_is_fixed (caps));

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
    g_return_if_fail (gst_value_is_fixed (from_par));

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
}

static void
gst_video_scale_setup_vs_image (VSImage * image, GstVideoFormat format,
    gint component, gint width, gint height, gint b_w, gint b_h, uint8_t * data)
{
  image->real_width =
      gst_video_format_get_component_width (format, component, width);
  image->real_height =
      gst_video_format_get_component_height (format, component, height);
  image->width =
      gst_video_format_get_component_width (format, component, MAX (1,
          width - b_w));
  image->height =
      gst_video_format_get_component_height (format, component, MAX (1,
          height - b_h));
  image->stride = gst_video_format_get_row_stride (format, component, width);

  image->border_top = (image->real_height - image->height) / 2;
  image->border_bottom = image->real_height - image->height - image->border_top;

  if (format == GST_VIDEO_FORMAT_YUY2 || format == GST_VIDEO_FORMAT_YVYU
      || format == GST_VIDEO_FORMAT_UYVY) {
    g_assert (component == 0);

    image->border_left = (image->real_width - image->width) / 2;

    if (image->border_left % 2 == 1)
      image->border_left--;
    image->border_right = image->real_width - image->width - image->border_left;
  } else {
    image->border_left = (image->real_width - image->width) / 2;
    image->border_right = image->real_width - image->width - image->border_left;
  }

  if (format == GST_VIDEO_FORMAT_I420
      || format == GST_VIDEO_FORMAT_YV12
      || format == GST_VIDEO_FORMAT_Y444
      || format == GST_VIDEO_FORMAT_Y42B || format == GST_VIDEO_FORMAT_Y41B) {
    image->real_pixels = data + gst_video_format_get_component_offset (format,
        component, width, height);
  } else {
    g_assert (component == 0);
    image->real_pixels = data;
  }

  image->pixels =
      image->real_pixels + image->border_top * image->stride +
      image->border_left * gst_video_format_get_pixel_stride (format,
      component);
}

static const guint8 *
_get_black_for_format (GstVideoFormat format)
{
  static const guint8 black[][4] = {
    {255, 0, 0, 0},             /*  0 = ARGB, ABGR, xRGB, xBGR */
    {0, 0, 0, 255},             /*  1 = RGBA, BGRA, RGBx, BGRx */
    {255, 16, 128, 128},        /*  2 = AYUV */
    {0, 0, 0, 0},               /*  3 = RGB and BGR */
    {16, 128, 128, 0},          /*  4 = v301 */
    {16, 128, 16, 128},         /*  5 = YUY2, YUYV */
    {128, 16, 128, 16},         /*  6 = UYVY */
    {16, 0, 0, 0},              /*  7 = Y */
    {0, 0, 0, 0}                /*  8 = RGB565, RGB666 */
  };

  switch (format) {
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ARGB64:
      return black[0];
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return black[1];
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_AYUV64:
      return black[2];
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      return black[3];
    case GST_VIDEO_FORMAT_v308:
      return black[4];
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
      return black[5];
    case GST_VIDEO_FORMAT_UYVY:
      return black[6];
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_GRAY8:
      return black[7];
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_Y16:
      return NULL;              /* Handled by the caller */
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      return black[4];          /* Y, U, V, 0 */
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_RGB15:
      return black[8];
    default:
      return NULL;
  }
}

static GstFlowReturn
gst_video_scale_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  GstFlowReturn ret = GST_FLOW_OK;
  VSImage dest = { NULL, };
  VSImage src = { NULL, };
  VSImage dest_u = { NULL, };
  VSImage dest_v = { NULL, };
  VSImage src_u = { NULL, };
  VSImage src_v = { NULL, };
  gint method;
  const guint8 *black = _get_black_for_format (videoscale->format);
  gboolean add_borders;

  GST_OBJECT_LOCK (videoscale);
  method = videoscale->method;
  add_borders = videoscale->add_borders;
  GST_OBJECT_UNLOCK (videoscale);

  if (videoscale->from_width == 1) {
    method = GST_VIDEO_SCALE_NEAREST;
  }
  if (method == GST_VIDEO_SCALE_4TAP &&
      (videoscale->from_width < 4 || videoscale->from_height < 4)) {
    method = GST_VIDEO_SCALE_BILINEAR;
  }

  gst_video_scale_setup_vs_image (&src, videoscale->format, 0,
      videoscale->from_width, videoscale->from_height, 0, 0,
      GST_BUFFER_DATA (in));
  gst_video_scale_setup_vs_image (&dest, videoscale->format, 0,
      videoscale->to_width, videoscale->to_height, videoscale->borders_w,
      videoscale->borders_h, GST_BUFFER_DATA (out));

  if (videoscale->format == GST_VIDEO_FORMAT_I420
      || videoscale->format == GST_VIDEO_FORMAT_YV12
      || videoscale->format == GST_VIDEO_FORMAT_Y444
      || videoscale->format == GST_VIDEO_FORMAT_Y42B
      || videoscale->format == GST_VIDEO_FORMAT_Y41B) {
    gst_video_scale_setup_vs_image (&src_u, videoscale->format, 1,
        videoscale->from_width, videoscale->from_height, 0, 0,
        GST_BUFFER_DATA (in));
    gst_video_scale_setup_vs_image (&src_v, videoscale->format, 2,
        videoscale->from_width, videoscale->from_height, 0, 0,
        GST_BUFFER_DATA (in));
    gst_video_scale_setup_vs_image (&dest_u, videoscale->format, 1,
        videoscale->to_width, videoscale->to_height, videoscale->borders_w,
        videoscale->borders_h, GST_BUFFER_DATA (out));
    gst_video_scale_setup_vs_image (&dest_v, videoscale->format, 2,
        videoscale->to_width, videoscale->to_height, videoscale->borders_w,
        videoscale->borders_h, GST_BUFFER_DATA (out));
  }

  switch (videoscale->format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      if (add_borders)
        vs_fill_borders_RGBA (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_RGBA (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_RGBA (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_RGBA (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_LANCZOS:
          vs_image_scale_lanczos_AYUV (&dest, &src, videoscale->tmp_buf,
              videoscale->sharpness, videoscale->dither, videoscale->submethod,
              videoscale->envelope, videoscale->sharpen);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_ARGB64:
    case GST_VIDEO_FORMAT_AYUV64:
      if (add_borders)
        vs_fill_borders_AYUV64 (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_AYUV64 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_AYUV64 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_AYUV64 (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_v308:
      if (add_borders)
        vs_fill_borders_RGB (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_RGB (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_RGB (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_RGB (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
      if (add_borders)
        vs_fill_borders_YUYV (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_YUYV (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_YUYV (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_YUYV (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_UYVY:
      if (add_borders)
        vs_fill_borders_UYVY (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_UYVY (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_UYVY (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_UYVY (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_Y800:
    case GST_VIDEO_FORMAT_GRAY8:
      if (add_borders)
        vs_fill_borders_Y (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_Y (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_Y (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_Y (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_Y16:
      if (add_borders)
        vs_fill_borders_Y16 (&dest, 0);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_Y16 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_Y16 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_Y16 (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      if (add_borders) {
        vs_fill_borders_Y (&dest, black);
        vs_fill_borders_Y (&dest_u, black + 1);
        vs_fill_borders_Y (&dest_v, black + 2);
      }
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_Y (&dest, &src, videoscale->tmp_buf);
          vs_image_scale_nearest_Y (&dest_u, &src_u, videoscale->tmp_buf);
          vs_image_scale_nearest_Y (&dest_v, &src_v, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_Y (&dest, &src, videoscale->tmp_buf);
          vs_image_scale_linear_Y (&dest_u, &src_u, videoscale->tmp_buf);
          vs_image_scale_linear_Y (&dest_v, &src_v, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_Y (&dest, &src, videoscale->tmp_buf);
          vs_image_scale_4tap_Y (&dest_u, &src_u, videoscale->tmp_buf);
          vs_image_scale_4tap_Y (&dest_v, &src_v, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_LANCZOS:
          vs_image_scale_lanczos_Y (&dest, &src, videoscale->tmp_buf,
              videoscale->sharpness, videoscale->dither, videoscale->submethod,
              videoscale->envelope, videoscale->sharpen);
          vs_image_scale_lanczos_Y (&dest_u, &src_u, videoscale->tmp_buf,
              videoscale->sharpness, videoscale->dither, videoscale->submethod,
              videoscale->envelope, videoscale->sharpen);
          vs_image_scale_lanczos_Y (&dest_v, &src_v, videoscale->tmp_buf,
              videoscale->sharpness, videoscale->dither, videoscale->submethod,
              videoscale->envelope, videoscale->sharpen);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_RGB16:
      if (add_borders)
        vs_fill_borders_RGB565 (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_RGB565 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_RGB565 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_RGB565 (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    case GST_VIDEO_FORMAT_RGB15:
      if (add_borders)
        vs_fill_borders_RGB555 (&dest, black);
      switch (method) {
        case GST_VIDEO_SCALE_NEAREST:
          vs_image_scale_nearest_RGB555 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_BILINEAR:
          vs_image_scale_linear_RGB555 (&dest, &src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_4TAP:
          vs_image_scale_4tap_RGB555 (&dest, &src, videoscale->tmp_buf);
          break;
        default:
          goto unknown_mode;
      }
      break;
    default:
      goto unsupported;
  }

  GST_LOG_OBJECT (videoscale, "pushing buffer of %d bytes",
      GST_BUFFER_SIZE (out));

  return ret;

  /* ERRORS */
unsupported:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported format %d for scaling method %d",
            videoscale->format, method));
    return GST_FLOW_ERROR;
  }
unknown_mode:
  {
    GST_ELEMENT_ERROR (videoscale, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unknown scaling method %d", videoscale->method));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_video_scale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoScale *videoscale = GST_VIDEO_SCALE (trans);
  gboolean ret;
  gdouble a;
  GstStructure *structure;

  GST_DEBUG_OBJECT (videoscale, "handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * videoscale->from_width / videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * videoscale->from_height / videoscale->to_height, NULL);
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
  gst_videoscale_orc_init ();

  if (!gst_element_register (plugin, "videoscale", GST_RANK_NONE,
          GST_TYPE_VIDEO_SCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (video_scale_debug, "videoscale", 0,
      "videoscale element");

  vs_4tap_init ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscale",
    "Resizes video", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
