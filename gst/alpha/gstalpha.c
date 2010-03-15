/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2007> Wim Taymans <wim.taymans@collabora.co.uk>
 * Copyright (C) <2007> Edward Hervey <edward.hervey@collabora.co.uk>
 * Copyright (C) <2007> Jan Schmidt <thaytan@noraisin.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <gst/controller/gstcontroller.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#define GST_TYPE_ALPHA \
  (gst_alpha_get_type())
#define GST_ALPHA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALPHA,GstAlpha))
#define GST_ALPHA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALPHA,GstAlphaClass))
#define GST_IS_ALPHA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALPHA))
#define GST_IS_ALPHA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALPHA))

typedef struct _GstAlpha GstAlpha;
typedef struct _GstAlphaClass GstAlphaClass;

typedef enum
{
  ALPHA_METHOD_SET,
  ALPHA_METHOD_GREEN,
  ALPHA_METHOD_BLUE,
  ALPHA_METHOD_CUSTOM,
}
GstAlphaMethod;

GST_DEBUG_CATEGORY_STATIC (gst_alpha_debug);
#define GST_CAT_DEFAULT gst_alpha_debug

struct _GstAlpha
{
  GstVideoFilter parent;

  /* caps */
  GstVideoFormat format;
  gint width, height;
  gboolean ayuv;

  gdouble alpha;

  guint target_r;
  guint target_g;
  guint target_b;

  GstAlphaMethod method;

  gfloat angle;
  gfloat noise_level;
  guint black_sensitivity;
  guint white_sensitivity;

  gfloat y;                     /* chroma color */
  gint8 cb, cr;
  gint8 kg;
  gfloat accept_angle_cos;
  gfloat accept_angle_sin;
  guint8 accept_angle_tg;
  guint8 accept_angle_ctg;
  guint8 one_over_kc;
  guint8 kfgy_scale;
};

struct _GstAlphaClass
{
  GstVideoFilterClass parent_class;
};

/* elementfactory information */
static const GstElementDetails gst_alpha_details =
GST_ELEMENT_DETAILS ("Alpha filter",
    "Filter/Effect/Video",
    "Adds an alpha channel to video - uniform or via chroma-keying",
    "Wim Taymans <wim@fluendo.com>\n"
    "Edward Hervey <edward.hervey@collabora.co.uk>\n"
    "Jan Schmidt <thaytan@noraisin.net>");

/* Alpha signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_METHOD ALPHA_METHOD_SET
#define DEFAULT_ALPHA 1.0
#define DEFAULT_TARGET_R 0
#define DEFAULT_TARGET_G 255
#define DEFAULT_TARGET_B 0
#define DEFAULT_ANGLE 20.0
#define DEFAULT_NOISE_LEVEL 2.0
#define DEFAULT_BLACK_SENSITIVITY 100
#define DEFAULT_WHITE_SENSITIVITY 100

enum
{
  PROP_0,
  PROP_METHOD,
  PROP_ALPHA,
  PROP_TARGET_R,
  PROP_TARGET_G,
  PROP_TARGET_B,
  PROP_ANGLE,
  PROP_NOISE_LEVEL,
  PROP_BLACK_SENSITIVITY,
  PROP_WHITE_SENSITIVITY,
  PROP_LAST
};

static GstStaticPadTemplate gst_alpha_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );

static GstStaticPadTemplate gst_alpha_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";" GST_VIDEO_CAPS_YUV ("I420")
    )
    );

static gboolean gst_alpha_start (GstBaseTransform * trans);
static gboolean gst_alpha_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, guint * size);
static GstCaps *gst_alpha_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_alpha_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_alpha_transform (GstBaseTransform * btrans,
    GstBuffer * in, GstBuffer * out);

static void gst_alpha_init_params (GstAlpha * alpha);

static void gst_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_alpha_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstAlpha, gst_alpha, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

#define GST_TYPE_ALPHA_METHOD (gst_alpha_method_get_type())
static GType
gst_alpha_method_get_type (void)
{
  static GType alpha_method_type = 0;
  static const GEnumValue alpha_method[] = {
    {ALPHA_METHOD_SET, "Set/adjust alpha channel", "set"},
    {ALPHA_METHOD_GREEN, "Chroma Key green", "green"},
    {ALPHA_METHOD_BLUE, "Chroma Key blue", "blue"},
    {ALPHA_METHOD_CUSTOM, "Chroma Key on target_r/g/b", "custom"},
    {0, NULL, NULL},
  };

  if (!alpha_method_type) {
    alpha_method_type = g_enum_register_static ("GstAlphaMethod", alpha_method);
  }
  return alpha_method_type;
}

static void
gst_alpha_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_alpha_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_src_template));

  GST_DEBUG_CATEGORY_INIT (gst_alpha_debug, "alpha", 0,
      "alpha - Element for adding alpha channel to streams");
}

static void
gst_alpha_class_init (GstAlphaClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *btrans_class;

  gobject_class = (GObjectClass *) klass;
  btrans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_alpha_set_property;
  gobject_class->get_property = gst_alpha_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_METHOD,
      g_param_spec_enum ("method", "Method",
          "How the alpha channels should be created", GST_TYPE_ALPHA_METHOD,
          DEFAULT_METHOD, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "The value for the alpha channel",
          0.0, 1.0, DEFAULT_ALPHA,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TARGET_R,
      g_param_spec_uint ("target_r", "Target Red", "The Red target", 0, 255,
          DEFAULT_TARGET_R,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TARGET_G,
      g_param_spec_uint ("target_g", "Target Green", "The Green target", 0, 255,
          DEFAULT_TARGET_G,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TARGET_B,
      g_param_spec_uint ("target_b", "Target Blue", "The Blue target", 0, 255,
          DEFAULT_TARGET_B,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ANGLE,
      g_param_spec_float ("angle", "Angle", "Size of the colorcube to change",
          0.0, 90.0, DEFAULT_ANGLE,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_NOISE_LEVEL,
      g_param_spec_float ("noise_level", "Noise Level", "Size of noise radius",
          0.0, 64.0, DEFAULT_NOISE_LEVEL,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BLACK_SENSITIVITY, g_param_spec_uint ("black-sensitivity",
          "Black Sensitivity", "Sensitivity to dark colors", 0, 128,
          DEFAULT_BLACK_SENSITIVITY,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_WHITE_SENSITIVITY, g_param_spec_uint ("white-sensitivity",
          "Sensitivity", "Sensitivity to bright colors", 0, 128,
          DEFAULT_WHITE_SENSITIVITY,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));


  btrans_class->start = GST_DEBUG_FUNCPTR (gst_alpha_start);
  btrans_class->transform = GST_DEBUG_FUNCPTR (gst_alpha_transform);
  btrans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_alpha_get_unit_size);
  btrans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_alpha_transform_caps);
  btrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_alpha_set_caps);
}

static void
gst_alpha_init (GstAlpha * alpha, GstAlphaClass * klass)
{
  alpha->alpha = DEFAULT_ALPHA;
  alpha->method = DEFAULT_METHOD;
  alpha->target_r = DEFAULT_TARGET_R;
  alpha->target_g = DEFAULT_TARGET_G;
  alpha->target_b = DEFAULT_TARGET_B;
  alpha->angle = DEFAULT_ANGLE;
  alpha->noise_level = DEFAULT_NOISE_LEVEL;
  alpha->black_sensitivity = DEFAULT_BLACK_SENSITIVITY;
  alpha->white_sensitivity = DEFAULT_WHITE_SENSITIVITY;
}

/* do we need this function? */
static void
gst_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAlpha *alpha;

  g_return_if_fail (GST_IS_ALPHA (object));

  alpha = GST_ALPHA (object);

  switch (prop_id) {
    case PROP_METHOD:
      alpha->method = g_value_get_enum (value);
      switch (alpha->method) {
        case ALPHA_METHOD_GREEN:
          alpha->target_r = 0;
          alpha->target_g = 255;
          alpha->target_b = 0;
          break;
        case ALPHA_METHOD_BLUE:
          alpha->target_r = 0;
          alpha->target_g = 0;
          alpha->target_b = 255;
          break;
        default:
          break;
      }
      gst_alpha_init_params (alpha);
      break;
    case PROP_ALPHA:
      alpha->alpha = g_value_get_double (value);
      break;
    case PROP_TARGET_R:
      alpha->target_r = g_value_get_uint (value);
      gst_alpha_init_params (alpha);
      break;
    case PROP_TARGET_G:
      alpha->target_g = g_value_get_uint (value);
      gst_alpha_init_params (alpha);
      break;
    case PROP_TARGET_B:
      alpha->target_b = g_value_get_uint (value);
      gst_alpha_init_params (alpha);
      break;
    case PROP_ANGLE:
      alpha->angle = g_value_get_float (value);
      gst_alpha_init_params (alpha);
      break;
    case PROP_NOISE_LEVEL:
      alpha->noise_level = g_value_get_float (value);
      gst_alpha_init_params (alpha);
      break;
    case PROP_BLACK_SENSITIVITY:
      alpha->black_sensitivity = g_value_get_uint (value);
      break;
    case PROP_WHITE_SENSITIVITY:
      alpha->white_sensitivity = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_alpha_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAlpha *alpha;

  g_return_if_fail (GST_IS_ALPHA (object));

  alpha = GST_ALPHA (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, alpha->method);
      break;
    case PROP_ALPHA:
      g_value_set_double (value, alpha->alpha);
      break;
    case PROP_TARGET_R:
      g_value_set_uint (value, alpha->target_r);
      break;
    case PROP_TARGET_G:
      g_value_set_uint (value, alpha->target_g);
      break;
    case PROP_TARGET_B:
      g_value_set_uint (value, alpha->target_b);
      break;
    case PROP_ANGLE:
      g_value_set_float (value, alpha->angle);
      break;
    case PROP_NOISE_LEVEL:
      g_value_set_float (value, alpha->noise_level);
      break;
    case PROP_BLACK_SENSITIVITY:
      g_value_set_uint (value, alpha->black_sensitivity);
      break;
    case PROP_WHITE_SENSITIVITY:
      g_value_set_uint (value, alpha->white_sensitivity);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_alpha_get_unit_size (GstBaseTransform * btrans,
    GstCaps * caps, guint * size)
{
  GstVideoFormat format;
  gint width, height;

  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    return FALSE;

  *size = gst_video_format_get_size (format, width, height);

  GST_DEBUG_OBJECT (btrans, "unit size = %d for format %d w %d height %d",
      *size, format, width, height);

  return TRUE;
}

static GstCaps *
gst_alpha_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *ret;
  GstStructure *structure;
  gint i;

  ret = gst_caps_copy (caps);

  /* When going from the SINK pad to the src, we just need to make sure the
   * format is AYUV */
  if (direction == GST_PAD_SINK) {
    for (i = 0; i < gst_caps_get_size (ret); i++) {
      structure = gst_caps_get_structure (ret, i);
      gst_structure_set (structure, "format",
          GST_TYPE_FOURCC, GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'), NULL);
    }
  } else {
    GstCaps *ayuv_caps;

    /* In the other direction, prepend a copy of the caps with format AYUV, 
     * and set the first to I420 */
    ayuv_caps = gst_caps_copy (ret);

    for (i = 0; i < gst_caps_get_size (ret); i++) {
      structure = gst_caps_get_structure (ret, i);
      gst_structure_set (structure, "format",
          GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'), NULL);
    }

    gst_caps_append (ret, ayuv_caps);
  }

  gst_caps_do_simplify (ret);

  return ret;
}

static gboolean
gst_alpha_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstAlpha *alpha = GST_ALPHA (btrans);

  if (!gst_video_format_parse_caps (incaps, &alpha->format,
          &alpha->width, &alpha->height))
    return FALSE;

  if (alpha->format == GST_VIDEO_FORMAT_AYUV)
    alpha->ayuv = TRUE;
  else
    alpha->ayuv = FALSE;

  return TRUE;
}

static void
gst_alpha_set_ayuv (guint8 * src, guint8 * dest, gint width, gint height,
    gdouble alpha)
{
  gint s_alpha = CLAMP ((gint) (alpha * 256), 0, 256);
  gint y, x;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      *dest++ = (*src++ * s_alpha) >> 8;
      *dest++ = *src++;
      *dest++ = *src++;
      *dest++ = *src++;
    }
  }
}

static void
gst_alpha_set_i420 (guint8 * src, guint8 * dest, gint width, gint height,
    gdouble alpha)
{
  gint b_alpha = CLAMP ((gint) (alpha * 255), 0, 255);
  guint8 *srcY;
  guint8 *srcU;
  guint8 *srcV;
  gint i, j;
  gint src_wrap, src_uv_wrap;
  gint y_stride, uv_stride;
  gboolean odd_width;

  y_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 0, width);
  uv_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 1, width);

  src_wrap = y_stride - width;
  src_uv_wrap = uv_stride - (width / 2);

  srcY = src;
  srcU = src + gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
      1, width, height);
  srcV = src + gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
      2, width, height);

  odd_width = (width % 2 != 0);

  for (i = 0; i < height; i++) {
    for (j = 0; j < width / 2; j++) {
      *dest++ = b_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU;
      *dest++ = *srcV;
      *dest++ = b_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU++;
      *dest++ = *srcV++;
    }
    /* Might have one odd column left to do */
    if (odd_width) {
      *dest++ = b_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU;
      *dest++ = *srcV;
    }
    if (i % 2 == 0) {
      srcU -= width / 2;
      srcV -= width / 2;
    } else {
      srcU += src_uv_wrap;
      srcV += src_uv_wrap;
    }
    srcY += src_wrap;
  }
}

static void
gst_alpha_chroma_key_ayuv (guint8 * src, guint8 * dest, gint width, gint height,
    GstAlpha * alpha)
{
  gint b_alpha;
  guint8 *src1;
  guint8 *dest1;
  gint i, j;
  gint x, z, u, v, y, a;
  gint tmp, tmp1;
  gint x1, y1;
  gint smin, smax;

  smin = 128 - alpha->black_sensitivity;
  smax = 128 + alpha->white_sensitivity;

  src1 = src;
  dest1 = dest;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      a = *src1++ * (alpha->alpha);
      y = *src1++;
      u = *src1++ - 128;
      v = *src1++ - 128;

      if (y < smin || y > smax) {
        /* too dark or too bright, keep alpha */
        b_alpha = a;
      } else {
        /* Convert foreground to XZ coords where X direction is defined by
           the key color */
        tmp = ((short) u * alpha->cb + (short) v * alpha->cr) >> 7;
        x = CLAMP (tmp, -128, 127);
        tmp = ((short) v * alpha->cb - (short) u * alpha->cr) >> 7;
        z = CLAMP (tmp, -128, 127);

        /* WARNING: accept angle should never be set greater than "somewhat less
           than 90 degrees" to avoid dealing with negative/infinite tg. In reality,
           80 degrees should be enough if foreground is reasonable. If this seems
           to be a problem, go to alternative ways of checking point position
           (scalar product or line equations). This angle should not be too small
           either to avoid infinite ctg (used to suppress foreground without use of
           division) */

        tmp = ((short) (x) * alpha->accept_angle_tg) >> 4;
        tmp = MIN (tmp, 127);

        if (abs (z) > tmp) {
          /* keep foreground Kfg = 0 */
          b_alpha = a;
        } else {
          /* Compute Kfg (implicitly) and Kbg, suppress foreground in XZ coord
             according to Kfg */
          tmp = ((short) (z) * alpha->accept_angle_ctg) >> 4;
          tmp = CLAMP (tmp, -128, 127);
          x1 = abs (tmp);
          y1 = z;

          tmp1 = x - x1;
          tmp1 = MAX (tmp1, 0);
          b_alpha = (((unsigned char) (tmp1) *
                  (unsigned short) (alpha->one_over_kc)) / 2);
          b_alpha = 255 - CLAMP (b_alpha, 0, 255);
          b_alpha = (a * b_alpha) >> 8;

          tmp = ((unsigned short) (tmp1) * alpha->kfgy_scale) >> 4;
          tmp1 = MIN (tmp, 255);

          tmp = y - tmp1;
          y = MAX (tmp, 0);

          /* Convert suppressed foreground back to CbCr */
          tmp = ((char) (x1) * (short) (alpha->cb) -
              (char) (y1) * (short) (alpha->cr)) >> 7;
          u = CLAMP (tmp, -128, 127);

          tmp = ((char) (x1) * (short) (alpha->cr) +
              (char) (y1) * (short) (alpha->cb)) >> 7;
          v = CLAMP (tmp, -128, 127);

          /* Deal with noise. For now, a circle around the key color with
             radius of noise_level treated as exact key color. Introduces
             sharp transitions.
           */
          tmp = z * (short) (z) + (x - alpha->kg) * (short) (x - alpha->kg);
          tmp = MIN (tmp, 0xffff);

          if (tmp < alpha->noise_level * alpha->noise_level) {
            b_alpha = 0;
          }
        }
      }

      u += 128;
      v += 128;

      *dest1++ = b_alpha;
      *dest1++ = y;
      *dest1++ = u;
      *dest1++ = v;
    }
  }
}

static void
gst_alpha_chromakey_row_i420 (GstAlpha * alpha, guint8 * dest1, guint8 * dest2,
    guint8 * srcY1, guint8 * srcY2, guint8 * srcU, guint8 * srcV, gint width)
{
  gint xpos;
  gint b_alpha;
  gint x, z, u, v, y11, y12, y21, y22, a;
  gint tmp, tmp1;
  gint x1, y1;
  gint smin, smax;

  a = 255 * alpha->alpha;
  smin = 128 - alpha->black_sensitivity;
  smax = 128 + alpha->white_sensitivity;

  for (xpos = 0; xpos < width / 2; xpos++) {
    y11 = *srcY1++;
    y12 = *srcY1++;
    y21 = *srcY2++;
    y22 = *srcY2++;
    u = *srcU++ - 128;
    v = *srcV++ - 128;

    if (y11 < smin || y11 > smax ||
        y12 < smin || y12 > smax ||
        y21 < smin || y21 > smax || y22 < smin || y22 > smax) {
      /* too dark or too bright, make opaque */
      b_alpha = 255;
    } else {
      /* Convert foreground to XZ coords where X direction is defined by
         the key color */
      tmp = ((short) u * alpha->cb + (short) v * alpha->cr) >> 7;
      x = CLAMP (tmp, -128, 127);
      tmp = ((short) v * alpha->cb - (short) u * alpha->cr) >> 7;
      z = CLAMP (tmp, -128, 127);

      /* WARNING: accept angle should never be set greater than "somewhat less
         than 90 degrees" to avoid dealing with negative/infinite tg. In reality,
         80 degrees should be enough if foreground is reasonable. If this seems
         to be a problem, go to alternative ways of checking point position
         (scalar product or line equations). This angle should not be too small
         either to avoid infinite ctg (used to suppress foreground without use of
         division) */

      tmp = ((short) (x) * alpha->accept_angle_tg) >> 4;
      tmp = MIN (tmp, 127);

      if (abs (z) > tmp) {
        /* keep foreground Kfg = 0 */
        b_alpha = 255;
      } else {
        /* Compute Kfg (implicitly) and Kbg, suppress foreground in XZ coord
           according to Kfg */
        tmp = ((short) (z) * alpha->accept_angle_ctg) >> 4;
        tmp = CLAMP (tmp, -128, 127);
        x1 = abs (tmp);
        y1 = z;

        tmp1 = x - x1;
        tmp1 = MAX (tmp1, 0);
        b_alpha = (((unsigned char) (tmp1) *
                (unsigned short) (alpha->one_over_kc)) / 2);
        b_alpha = 255 - CLAMP (b_alpha, 0, 255);
        b_alpha = (a * b_alpha) >> 8;

        tmp = ((unsigned short) (tmp1) * alpha->kfgy_scale) >> 4;
        tmp1 = MIN (tmp, 255);

        tmp = y11 - tmp1;
        y11 = MAX (tmp, 0);
        tmp = y12 - tmp1;
        y12 = MAX (tmp, 0);
        tmp = y21 - tmp1;
        y21 = MAX (tmp, 0);
        tmp = y22 - tmp1;
        y22 = MAX (tmp, 0);

        /* Convert suppressed foreground back to CbCr */
        tmp = ((char) (x1) * (short) (alpha->cb) -
            (char) (y1) * (short) (alpha->cr)) >> 7;
        u = CLAMP (tmp, -128, 127);

        tmp = ((char) (x1) * (short) (alpha->cr) +
            (char) (y1) * (short) (alpha->cb)) >> 7;
        v = CLAMP (tmp, -128, 127);

        /* Deal with noise. For now, a circle around the key color with
           radius of noise_level treated as exact key color. Introduces
           sharp transitions.
         */
        tmp = z * (short) (z) + (x - alpha->kg) * (short) (x - alpha->kg);
        tmp = MIN (tmp, 0xffff);

        if (tmp < alpha->noise_level * alpha->noise_level) {
          /* Uncomment this if you want total suppression within the noise circle */
          b_alpha = 0;
        }
      }
    }

    u += 128;
    v += 128;

    *dest1++ = b_alpha;
    *dest1++ = y11;
    *dest1++ = u;
    *dest1++ = v;
    *dest1++ = b_alpha;
    *dest1++ = y12;
    *dest1++ = u;
    *dest1++ = v;

    *dest2++ = b_alpha;
    *dest2++ = y21;
    *dest2++ = u;
    *dest2++ = v;
    *dest2++ = b_alpha;
    *dest2++ = y22;
    *dest2++ = u;
    *dest2++ = v;
  }
}

/* based on http://www.cs.utah.edu/~michael/chroma/
 */
static void
gst_alpha_chroma_key_i420 (guint8 * src, guint8 * dest, gint width, gint height,
    GstAlpha * alpha)
{
  guint8 *srcY1, *srcY2, *srcU, *srcV;
  guint8 *dest1, *dest2;
  gint ypos;
  gint dest_stride, src_y_stride, src_uv_stride;

  dest_stride =
      gst_video_format_get_row_stride (GST_VIDEO_FORMAT_AYUV, 0, width);
  src_y_stride =
      gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 0, width);
  src_uv_stride =
      gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 1, width);

  srcY1 = src;
  srcY2 = src + src_y_stride;

  srcU = src + gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
      1, width, height);
  srcV = src + gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
      2, width, height);

  dest1 = dest;
  dest2 = dest + dest_stride;

  /* Redefine Y strides to skip 2 lines at a time ... */
  dest_stride *= 2;
  src_y_stride *= 2;

  for (ypos = 0; ypos < height / 2; ypos++) {

    gst_alpha_chromakey_row_i420 (alpha, dest1, dest2,
        srcY1, srcY2, srcU, srcV, width);

    dest1 += dest_stride;
    dest2 += dest_stride;
    srcY1 += src_y_stride;
    srcY2 += src_y_stride;
    srcU += src_uv_stride;
    srcV += src_uv_stride;
  }
}

static void
gst_alpha_init_params (GstAlpha * alpha)
{
  float kgl;
  float tmp;
  float tmp1, tmp2;

  alpha->y =
      0.257 * alpha->target_r + 0.504 * alpha->target_g +
      0.098 * alpha->target_b;
  tmp1 =
      -0.148 * alpha->target_r - 0.291 * alpha->target_g +
      0.439 * alpha->target_b;
  tmp2 =
      0.439 * alpha->target_r - 0.368 * alpha->target_g -
      0.071 * alpha->target_b;
  kgl = sqrt (tmp1 * tmp1 + tmp2 * tmp2);
  alpha->cb = 127 * (tmp1 / kgl);
  alpha->cr = 127 * (tmp2 / kgl);

  alpha->accept_angle_cos = cos (M_PI * alpha->angle / 180);
  alpha->accept_angle_sin = sin (M_PI * alpha->angle / 180);
  tmp = 15 * tan (M_PI * alpha->angle / 180);
  tmp = MIN (tmp, 255);
  alpha->accept_angle_tg = tmp;
  tmp = 15 / tan (M_PI * alpha->angle / 180);
  tmp = MIN (tmp, 255);
  alpha->accept_angle_ctg = tmp;
  tmp = 1 / (kgl);
  alpha->one_over_kc = 255 * 2 * tmp - 255;
  tmp = 15 * (float) (alpha->y) / kgl;
  tmp = MIN (tmp, 255);
  alpha->kfgy_scale = tmp;
  alpha->kg = MIN (kgl, 127);
}

static gboolean
gst_alpha_start (GstBaseTransform * btrans)
{
  GstAlpha *alpha = GST_ALPHA (btrans);

  gst_alpha_init_params (alpha);

  return TRUE;
}

static GstFlowReturn
gst_alpha_transform (GstBaseTransform * btrans, GstBuffer * in, GstBuffer * out)
{
  GstAlpha *alpha = GST_ALPHA (btrans);
  gint width, height;
  GstClockTime timestamp;

  width = alpha->width;
  height = alpha->height;

  GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (in);
  GST_BUFFER_DURATION (out) = GST_BUFFER_DURATION (in);
  timestamp = gst_segment_to_stream_time (&btrans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (in));
  GST_LOG ("Got stream time of %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (alpha), timestamp);

  switch (alpha->method) {
    case ALPHA_METHOD_SET:
      if (alpha->ayuv) {
        gst_alpha_set_ayuv (GST_BUFFER_DATA (in),
            GST_BUFFER_DATA (out), width, height, alpha->alpha);
      } else {
        gst_alpha_set_i420 (GST_BUFFER_DATA (in),
            GST_BUFFER_DATA (out), width, height, alpha->alpha);
      }
      break;
    case ALPHA_METHOD_GREEN:
    case ALPHA_METHOD_BLUE:
    case ALPHA_METHOD_CUSTOM:
      if (alpha->ayuv) {
        gst_alpha_chroma_key_ayuv (GST_BUFFER_DATA (in),
            GST_BUFFER_DATA (out), width, height, alpha);
      } else {
        gst_alpha_chroma_key_i420 (GST_BUFFER_DATA (in),
            GST_BUFFER_DATA (out), width, height, alpha);
      }
      break;
    default:
      break;
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_controller_init (NULL, NULL);

  return gst_element_register (plugin, "alpha", GST_RANK_NONE, GST_TYPE_ALPHA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alpha",
    "adds an alpha channel to video - constant or via chroma-keying",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
