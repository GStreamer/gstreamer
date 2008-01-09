/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#define ROUND_UP_2(x) (((x) + 1) & ~1)
#define ROUND_UP_4(x) (((x) + 3) & ~3)
#define ROUND_UP_8(x) (((x) + 7) & ~7)

#define GST_CAT_DEFAULT gst_alpha_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

struct _GstAlpha
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* caps */
  gint in_width, in_height;
  gint out_width, out_height;
  gboolean ayuv;

  gdouble alpha;

  guint target_r;
  guint target_g;
  guint target_b;

  GstAlphaMethod method;

  gfloat angle;
  gfloat noise_level;

  gfloat y;                     /* chroma color */
  gint8 cb, cr;
  gint8 kg;
  gfloat accept_angle_cos;
  gfloat accept_angle_sin;
  guint8 accept_angle_tg;
  guint8 accept_angle_ctg;
  guint8 one_over_kc;
  guint8 kfgy_scale;

  GstSegment segment;
};

struct _GstAlphaClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static const GstElementDetails gst_alpha_details =
GST_ELEMENT_DETAILS ("Alpha filter",
    "Filter/Effect/Video",
    "Adds an alpha channel to video",
    "Wim Taymans <wim@fluendo.com>");


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

enum
{
  ARG_0,
  ARG_METHOD,
  ARG_ALPHA,
  ARG_TARGET_R,
  ARG_TARGET_G,
  ARG_TARGET_B,
  ARG_ANGLE,
  ARG_NOISE_LEVEL,
  /* FILL ME */
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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")
        ";" GST_VIDEO_CAPS_YUV ("I420")
    )
    );


static void gst_alpha_base_init (gpointer g_class);
static void gst_alpha_class_init (GstAlphaClass * klass);
static void gst_alpha_init (GstAlpha * alpha);
static void gst_alpha_init_params (GstAlpha * alpha);

static void gst_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_alpha_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_alpha_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_alpha_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_alpha_sink_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn gst_alpha_change_state (GstElement * element,
    GstStateChange transition);


static GstElementClass *parent_class = NULL;

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

/* static guint gst_alpha_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_alpha_get_type (void)
{
  static GType alpha_type = 0;

  if (!alpha_type) {
    static const GTypeInfo alpha_info = {
      sizeof (GstAlphaClass),
      gst_alpha_base_init,
      NULL,
      (GClassInitFunc) gst_alpha_class_init,
      NULL,
      NULL,
      sizeof (GstAlpha),
      0,
      (GInstanceInitFunc) gst_alpha_init,
    };

    alpha_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAlpha", &alpha_info, 0);
  }
  return alpha_type;
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
}
static void
gst_alpha_class_init (GstAlphaClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_alpha_set_property;
  gobject_class->get_property = gst_alpha_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
      g_param_spec_enum ("method", "Method",
          "How the alpha channels should be created", GST_TYPE_ALPHA_METHOD,
          DEFAULT_METHOD, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "The value for the alpha channel",
          0.0, 1.0, DEFAULT_ALPHA,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TARGET_R,
      g_param_spec_uint ("target_r", "Target Red", "The Red target", 0, 255,
          DEFAULT_TARGET_R,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TARGET_G,
      g_param_spec_uint ("target_g", "Target Green", "The Green target", 0, 255,
          DEFAULT_TARGET_G,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TARGET_B,
      g_param_spec_uint ("target_b", "Target Blue", "The Blue target", 0, 255,
          DEFAULT_TARGET_B,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ANGLE,
      g_param_spec_float ("angle", "Angle", "Size of the colorcube to change",
          0.0, 90.0, DEFAULT_ANGLE,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NOISE_LEVEL,
      g_param_spec_float ("noise_level", "Noise Level", "Size of noise radius",
          0.0, 64.0, DEFAULT_NOISE_LEVEL,
          (GParamFlags) G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gstelement_class->change_state = gst_alpha_change_state;
}

static void
gst_alpha_init (GstAlpha * alpha)
{
  /* create the sink and src pads */
  alpha->sinkpad =
      gst_pad_new_from_static_template (&gst_alpha_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (alpha), alpha->sinkpad);
  gst_pad_set_chain_function (alpha->sinkpad, gst_alpha_chain);
  gst_pad_set_setcaps_function (alpha->sinkpad, gst_alpha_sink_setcaps);
  gst_pad_set_event_function (alpha->sinkpad, gst_alpha_sink_event);

  alpha->srcpad =
      gst_pad_new_from_static_template (&gst_alpha_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (alpha), alpha->srcpad);

  alpha->alpha = DEFAULT_ALPHA;
  alpha->method = DEFAULT_METHOD;
  alpha->target_r = DEFAULT_TARGET_R;
  alpha->target_g = DEFAULT_TARGET_G;
  alpha->target_b = DEFAULT_TARGET_B;
  alpha->angle = DEFAULT_ANGLE;
  alpha->noise_level = DEFAULT_NOISE_LEVEL;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "alpha", 0, "Alpha adding element");
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
    case ARG_METHOD:
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
    case ARG_ALPHA:
      alpha->alpha = g_value_get_double (value);
      break;
    case ARG_TARGET_R:
      alpha->target_r = g_value_get_uint (value);
      gst_alpha_init_params (alpha);
      break;
    case ARG_TARGET_G:
      alpha->target_g = g_value_get_uint (value);
      gst_alpha_init_params (alpha);
      break;
    case ARG_TARGET_B:
      alpha->target_b = g_value_get_uint (value);
      gst_alpha_init_params (alpha);
      break;
    case ARG_ANGLE:
      alpha->angle = g_value_get_float (value);
      gst_alpha_init_params (alpha);
      break;
    case ARG_NOISE_LEVEL:
      alpha->noise_level = g_value_get_float (value);
      gst_alpha_init_params (alpha);
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
    case ARG_METHOD:
      g_value_set_enum (value, alpha->method);
      break;
    case ARG_ALPHA:
      g_value_set_double (value, alpha->alpha);
      break;
    case ARG_TARGET_R:
      g_value_set_uint (value, alpha->target_r);
      break;
    case ARG_TARGET_G:
      g_value_set_uint (value, alpha->target_g);
      break;
    case ARG_TARGET_B:
      g_value_set_uint (value, alpha->target_b);
      break;
    case ARG_ANGLE:
      g_value_set_float (value, alpha->angle);
      break;
    case ARG_NOISE_LEVEL:
      g_value_set_float (value, alpha->noise_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_alpha_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAlpha *alpha;
  GstStructure *structure;
  gboolean ret;
  guint32 fourcc;

  alpha = GST_ALPHA (GST_PAD_PARENT (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
    switch (fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        alpha->ayuv = FALSE;
        break;
      case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
        alpha->ayuv = TRUE;
        break;
      default:
        return FALSE;
    }
  } else {
    return FALSE;
  }

  ret = gst_structure_get_int (structure, "width", &alpha->in_width);
  ret &= gst_structure_get_int (structure, "height", &alpha->in_height);

  return TRUE;
}

static void
gst_alpha_set_ayuv (guint8 * src, guint8 * dest, gint width, gint height,
    gdouble alpha)
{
  gint b_alpha = (gint) (alpha * 255);
  gint i, j;
  gint size;
  gint stride;
  gint wrap;

  width = ROUND_UP_2 (width);
  height = ROUND_UP_2 (height);

  stride = ROUND_UP_4 (width);
  size = stride * height;

  wrap = stride - width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = (*src++ * b_alpha) >> 8;
      *dest++ = *src++;
      *dest++ = *src++;
      *dest++ = *src++;
    }
    src += wrap;
    dest += wrap;
  }
}

static void
gst_alpha_set_i420 (guint8 * src, guint8 * dest, gint width, gint height,
    gdouble alpha)
{
  gint b_alpha = (gint) (alpha * 255);
  guint8 *srcY;
  guint8 *srcU;
  guint8 *srcV;
  gint i, j;
  gint size, size2;
  gint stride, stride2;
  gint wrap, wrap2;

  width = ROUND_UP_2 (width);
  height = ROUND_UP_2 (height);

  stride = ROUND_UP_4 (width);
  size = stride * height;
  stride2 = ROUND_UP_8 (width) / 2;
  size2 = stride2 * height / 2;

  wrap = stride - 2 * (width / 2);
  wrap2 = stride2 - width / 2;

  srcY = src;
  srcU = srcY + size;
  srcV = srcU + size2;

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
    if (i % 2 == 0) {
      srcU -= width / 2;
      srcV -= width / 2;
    } else {
      srcU += wrap2;
      srcV += wrap2;
    }
    srcY += wrap;
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
  gint size;
  gint stride;
  gint wrap;
  gint tmp, tmp1;
  gint x1, y1;

  width = ROUND_UP_2 (width);
  height = ROUND_UP_2 (height);

  stride = ROUND_UP_4 (width);
  size = stride * height;

  src1 = src;
  dest1 = dest;

  wrap = stride - width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      a = *src1++ * (alpha->alpha);
      y = *src1++;
      u = *src1++ - 128;
      v = *src1++ - 128;

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

      u += 128;
      v += 128;

      *dest1++ = b_alpha;
      *dest1++ = y;
      *dest1++ = u;
      *dest1++ = v;
    }
    dest1 += wrap;
    src1 += wrap;
  }
}

/* based on http://www.cs.utah.edu/~michael/chroma/
 */
static void
gst_alpha_chroma_key_i420 (guint8 * src, guint8 * dest, gint width, gint height,
    GstAlpha * alpha)
{
  gint b_alpha;
  guint8 *srcY1, *srcY2, *srcU, *srcV;
  guint8 *dest1, *dest2;
  gint i, j;
  gint x, z, u, v, y11, y12, y21, y22, a;
  gint size, size2;
  gint stride, stride2;
  gint wrap, wrap2, wrap3;
  gint tmp, tmp1;
  gint x1, y1;

  width = ROUND_UP_2 (width);
  height = ROUND_UP_2 (height);

  stride = ROUND_UP_4 (width);
  size = stride * height;
  stride2 = ROUND_UP_8 (width) / 2;
  size2 = stride2 * height / 2;

  srcY1 = src;
  srcY2 = src + stride;
  srcU = srcY1 + size;
  srcV = srcU + size2;

  dest1 = dest;
  dest2 = dest + width * 4;

  wrap = 2 * stride - 2 * (width / 2);
  wrap2 = stride2 - width / 2;
  wrap3 = 8 * width - 8 * (width / 2);

  a = 255 * alpha->alpha;

  for (i = 0; i < height / 2; i++) {
    for (j = 0; j < width / 2; j++) {
      y11 = *srcY1++;
      y12 = *srcY1++;
      y21 = *srcY2++;
      y22 = *srcY2++;
      u = *srcU++ - 128;
      v = *srcV++ - 128;

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
    dest1 += wrap3;
    dest2 += wrap3;
    srcY1 += wrap;
    srcY2 += wrap;
    srcU += wrap2;
    srcV += wrap2;
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
gst_alpha_sink_event (GstPad * pad, GstEvent * event)
{
  GstAlpha *alpha;
  gboolean ret;

  alpha = GST_ALPHA (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&alpha->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_NEWSEGMENT:{
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      gst_segment_set_newsegment_full (&alpha->segment, update, rate, arate,
          format, start, stop, time);
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (alpha->srcpad, event);

  return ret;
}

static GstFlowReturn
gst_alpha_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAlpha *alpha;
  GstBuffer *outbuf;
  gint new_width, new_height;
  GstFlowReturn ret;
  GstClockTime timestamp;

  alpha = GST_ALPHA (GST_PAD_PARENT (pad));

  new_width = alpha->in_width;
  new_height = alpha->in_height;

  if (new_width != alpha->out_width ||
      new_height != alpha->out_height || !GST_PAD_CAPS (alpha->srcpad)) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy (gst_pad_get_negotiated_caps (alpha->sinkpad));
    gst_caps_set_simple (newcaps,
        "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("AYUV"),
        "width", G_TYPE_INT, new_width, "height", G_TYPE_INT, new_height, NULL);

    gst_pad_set_caps (alpha->srcpad, newcaps);

    alpha->out_width = new_width;
    alpha->out_height = new_height;
  }

  outbuf =
      gst_buffer_new_and_alloc (ROUND_UP_2 (new_width) *
      ROUND_UP_2 (new_height) * 4);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (alpha->srcpad));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);
  timestamp = gst_segment_to_stream_time (&alpha->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));
  GST_LOG ("Got stream time of %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (alpha), timestamp);

  switch (alpha->method) {
    case ALPHA_METHOD_SET:
      if (alpha->ayuv) {
        gst_alpha_set_ayuv (GST_BUFFER_DATA (buffer),
            GST_BUFFER_DATA (outbuf), new_width, new_height, alpha->alpha);
      } else {
        gst_alpha_set_i420 (GST_BUFFER_DATA (buffer),
            GST_BUFFER_DATA (outbuf), new_width, new_height, alpha->alpha);
      }
      break;
    case ALPHA_METHOD_GREEN:
    case ALPHA_METHOD_BLUE:
    case ALPHA_METHOD_CUSTOM:
      if (alpha->ayuv) {
        gst_alpha_chroma_key_ayuv (GST_BUFFER_DATA (buffer),
            GST_BUFFER_DATA (outbuf), new_width, new_height, alpha);
      } else {
        gst_alpha_chroma_key_i420 (GST_BUFFER_DATA (buffer),
            GST_BUFFER_DATA (outbuf), new_width, new_height, alpha);
      }
      break;
    default:
      break;
  }

  gst_buffer_unref (buffer);

  /* Update last stop position in segment */
  if (GST_BUFFER_TIMESTAMP (outbuf) != GST_CLOCK_TIME_NONE) {
    GstClockTime last_stop = GST_BUFFER_TIMESTAMP (outbuf);

    if (GST_BUFFER_DURATION (outbuf) != GST_CLOCK_TIME_NONE)
      last_stop += GST_BUFFER_DURATION (outbuf);

    gst_segment_set_last_stop (&alpha->segment, GST_FORMAT_TIME, last_stop);
  }

  ret = gst_pad_push (alpha->srcpad, outbuf);

  return ret;
}

static GstStateChangeReturn
gst_alpha_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstAlpha *alpha;

  alpha = GST_ALPHA (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&alpha->segment, GST_FORMAT_UNDEFINED);
      gst_alpha_init_params (alpha);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return res;
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
    "adds an alpha channel to video",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
