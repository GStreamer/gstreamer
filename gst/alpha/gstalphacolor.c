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

/**
 * SECTION:element-alphacolor
 *
 * The alphacolor element does memory-efficient (in-place) colourspace
 * conversion from RGBA to AYUV, preserving the alpha channel.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalphacolor.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (alpha_color_debug);
#define GST_CAT_DEFAULT alpha_color_debug

/* elementfactory information */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );

GST_BOILERPLATE (GstAlphaColor, gst_alpha_color, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

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

  gst_element_class_set_details_simple (element_class, "Alpha color filter",
      "Filter/Effect/Video",
      "RGBA to AYUV colorspace conversion preserving the alpha channel",
      "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_alpha_color_class_init (GstAlphaColorClass * klass)
{
  GstBaseTransformClass *gstbasetransform_class =
      (GstBaseTransformClass *) klass;

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
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (alpha);

  btrans->always_in_place = TRUE;
}

static GstCaps *
gst_alpha_color_transform_caps (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps)
{
  const GstCaps *tmpl_caps = NULL;
  GstCaps *result = NULL, *local_caps = NULL;
  guint i;

  local_caps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (local_caps); i++) {
    GstStructure *structure = gst_caps_get_structure (local_caps, i);

    /* Throw away the structure name and set it to transformed format */
    if (direction == GST_PAD_SINK) {
      gst_structure_set_name (structure, "video/x-raw-yuv");
    } else if (direction == GST_PAD_SRC) {
      gst_structure_set_name (structure, "video/x-raw-rgb");
    }
    /* Remove any specific parameter from the structure */
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
    gst_structure_remove_field (structure, "alpha_mask");
  }

  /* Get the appropriate template */
  if (direction == GST_PAD_SINK) {
    tmpl_caps = gst_static_pad_template_get_caps (&src_template);
  } else if (direction == GST_PAD_SRC) {
    tmpl_caps = gst_static_pad_template_get_caps (&sink_template);
  }

  /* Intersect with our template caps */
  result = gst_caps_intersect (local_caps, tmpl_caps);

  gst_caps_unref (local_caps);
  gst_caps_do_simplify (result);

  GST_LOG_OBJECT (btrans, "transformed %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
      caps, result);

  return result;
}

static gboolean
gst_alpha_color_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstAlphaColor *alpha = GST_ALPHA_COLOR (btrans);
  gboolean ret;
  gint w, h;
  GstVideoFormat format;

  ret = gst_video_format_parse_caps (outcaps, &format, &w, &h);

  if (!ret || (format != GST_VIDEO_FORMAT_ARGB
          && format != GST_VIDEO_FORMAT_BGRA)) {
    GST_DEBUG_OBJECT (alpha, "incomplete or non-RGBA input caps!");
    return FALSE;
  }

  alpha->format = format;
  alpha->width = w;
  alpha->height = h;

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
  GstAlphaColor *alpha = GST_ALPHA_COLOR (btrans);

  /* Transform in place */
  if (alpha->format == GST_VIDEO_FORMAT_ARGB)
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
    "RGBA to AYUV colorspace conversion preserving the alpha channel",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
