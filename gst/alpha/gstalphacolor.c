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
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include <string.h>

GST_DEBUG_CATEGORY (alpha_color_debug);
#define GST_CAT_DEFAULT alpha_color_debug

#define GST_TYPE_ALPHA_COLOR \
  (gst_alpha_color_get_type())
#define GST_ALPHA_COLOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALPHA_COLOR,GstAlphaColor))
#define GST_ALPHA_COLOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALPHA_COLOR,GstAlphaColorClass))
#define GST_IS_ALPHA_COLOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALPHA_COLOR))
#define GST_IS_ALPHA_COLOR_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALPHA_COLOR))

typedef struct _GstAlphaColor GstAlphaColor;
typedef struct _GstAlphaColorClass GstAlphaColorClass;

struct _GstAlphaColor
{
  GstBaseTransform element;

  /* caps */
  gint in_width, in_height;
  gboolean in_rgba;
  gint out_width, out_height;
};

struct _GstAlphaColorClass
{
  GstBaseTransformClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_alpha_color_details =
GST_ELEMENT_DETAILS ("alpha color filter",
    "Filter/Effect/Video",
    "RGB->YUV colorspace conversion preserving the alpha channels",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_alpha_color_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA)
    );

static GstStaticPadTemplate gst_alpha_color_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );

GST_BOILERPLATE (GstAlphaColor, gst_alpha_color, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static GstCaps *gst_alpha_color_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_alpha_color_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_alpha_color_transform_ip (GstBaseTransform * btrans,
    GstBuffer * inbuf);

static void
gst_alpha_color_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_alpha_color_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_color_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_color_src_template));
}

static void
gst_alpha_color_class_init (GstAlphaColorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_alpha_color_transform_caps);
  gstbasetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_alpha_color_set_caps);
  gstbasetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_alpha_color_transform_ip);

  GST_DEBUG_CATEGORY_INIT (alpha_color_debug, "alphacolor", 0,
      "RGB->YUV colorspace conversion preserving the alpha channels");
}

static void
gst_alpha_color_init (GstAlphaColor * alpha, GstAlphaColorClass * g_class)
{
  GstBaseTransform *btrans = NULL;

  btrans = GST_BASE_TRANSFORM (alpha);

  btrans->always_in_place = TRUE;
}

static GstCaps *
gst_alpha_color_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  GstAlphaColor *alpha = NULL;
  GstCaps *result = NULL;

  alpha = GST_ALPHA_COLOR (btrans);

  if (gst_caps_is_fixed (caps)) {
    GstStructure *structure = NULL;
    gint width, height;
    gdouble fps;

    structure = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
    gst_structure_get_double (structure, "framerate", &fps);

    result = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'),
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height, "framerate", G_TYPE_DOUBLE, fps, NULL);
  } else {
    result = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'),
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
  }

  return result;
}

static gboolean
gst_alpha_color_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAlphaColor *alpha;
  GstStructure *structure;
  gboolean ret;
  gdouble fps;
  gint red_mask;

  alpha = GST_ALPHA_COLOR (btrans);
  structure = gst_caps_get_structure (incaps, 0);

  ret = gst_structure_get_int (structure, "width", &alpha->in_width);
  ret &= gst_structure_get_int (structure, "height", &alpha->in_height);
  ret &= gst_structure_get_double (structure, "framerate", &fps);
  ret &= gst_structure_get_int (structure, "red_mask", &red_mask);

  if (!ret)
    return FALSE;

  alpha->in_rgba = TRUE;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  if (red_mask != 0x000000ff)
#else
  if (red_mask != 0x00ff0000)
#endif
    alpha->in_rgba = FALSE;

  return TRUE;
}

static void
transform_rgb (guint8 * data, gint size)
{
  guint8 y, u, v;

  while (size > 0) {
    y = data[0] * 0.299 + data[1] * 0.587 + data[2] * 0.114 + 0;
    u = data[0] * -0.169 + data[1] * -0.332 + data[2] * 0.500 + 128;
    v = data[0] * 0.500 + data[1] * -0.419 + data[2] * -0.0813 + 128;

    data[0] = data[3];
    data[1] = y;
    data[2] = u;
    data[3] = v;

    data += 4;
    size -= 4;
  }
}

static void
transform_bgr (guint8 * data, gint size)
{
  guint8 y, u, v;

  while (size > 0) {
    y = data[2] * 0.299 + data[1] * 0.587 + data[0] * 0.114 + 0;
    u = data[2] * -0.169 + data[1] * -0.332 + data[0] * 0.500 + 128;
    v = data[2] * 0.500 + data[1] * -0.419 + data[0] * -0.0813 + 128;

    data[0] = data[3];
    data[1] = y;
    data[2] = u;
    data[3] = v;

    data += 4;
    size -= 4;
  }
}

static GstFlowReturn
gst_alpha_color_transform_ip (GstBaseTransform * btrans, GstBuffer * inbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAlphaColor *alpha;

  alpha = GST_ALPHA_COLOR (btrans);

  /* Transform in place */
  if (alpha->in_rgba)
    transform_rgb (GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));
  else
    transform_bgr (GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "alphacolor", GST_RANK_NONE,
      GST_TYPE_ALPHA_COLOR);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alphacolor",
    "RGB->YUV colorspace conversion preserving the alpha channels",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
