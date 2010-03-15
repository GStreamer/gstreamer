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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_YUV ("AYUV"))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_YUV ("AYUV"))
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
      "ARGB from/to AYUV colorspace conversion preserving the alpha channel",
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
      "ARGB<->AYUV colorspace conversion preserving the alpha channels");
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

  local_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure =
        gst_structure_copy (gst_caps_get_structure (caps, i));

    /* Remove any specific parameter from the structure */
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
    gst_structure_remove_field (structure, "alpha_mask");

    gst_structure_set_name (structure, "video/x-raw-yuv");
    gst_caps_append_structure (local_caps, gst_structure_copy (structure));
    gst_structure_set_name (structure, "video/x-raw-rgb");
    gst_caps_append_structure (local_caps, structure);
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
  gint w2, h2;
  GstVideoFormat in_format, out_format;

  ret = gst_video_format_parse_caps (incaps, &in_format, &w, &h);
  ret &= gst_video_format_parse_caps (outcaps, &out_format, &w2, &h2);

  if (!ret || w != w2 || h != h2) {
    GST_DEBUG_OBJECT (alpha, "incomplete or invalid caps!");
    return FALSE;
  }

  alpha->in_format = in_format;
  alpha->out_format = out_format;
  alpha->width = w;
  alpha->height = h;

  if (in_format == out_format)
    gst_base_transform_set_passthrough (btrans, TRUE);

  return TRUE;
}

static void
transform_argb_ayuv (guint8 * data, gint size)
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
transform_bgra_ayuv (guint8 * data, gint size)
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

static void
transform_argb_bgra (guint8 * data, gint size)
{
  guint8 a, r, g;

  while (size > 0) {
    a = data[0];
    r = data[1];
    g = data[2];

    data[0] = data[3];
    data[1] = g;
    data[2] = r;
    data[3] = a;

    data += 4;
    size -= 4;
  }
}

static void
transform_ayuv_argb (guint8 * data, gint size)
{
  guint8 r, g, b;

  while (size > 0) {
    r = data[1] + (0.419 / 0.299) * (data[3] - 128);
    g = data[1] + (-0.114 / 0.331) * (data[2] - 128) +
        (-0.299 / 0.419) * (data[3] - 128);
    b = data[1] + (0.587 / 0.331) * (data[2] - 128);

    data[0] = data[0];
    data[1] = r;
    data[2] = g;
    data[3] = b;

    data += 4;
    size -= 4;
  }
}

static void
transform_ayuv_bgra (guint8 * data, gint size)
{
  guint8 r, g, b;

  while (size > 0) {
    r = data[1] + (0.419 / 0.299) * (data[3] - 128);
    g = data[1] + (-0.114 / 0.331) * (data[2] - 128) +
        (-0.299 / 0.419) * (data[3] - 128);
    b = data[1] + (0.587 / 0.331) * (data[2] - 128);

    data[3] = data[0];
    data[2] = r;
    data[1] = g;
    data[0] = b;

    data += 4;
    size -= 4;
  }
}

static GstFlowReturn
gst_alpha_color_transform_ip (GstBaseTransform * btrans, GstBuffer * inbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAlphaColor *alpha = GST_ALPHA_COLOR (btrans);

  if (G_UNLIKELY (GST_BUFFER_SIZE (inbuf) != 4 * alpha->width * alpha->height)) {
    GST_ERROR_OBJECT (alpha, "Invalid buffer size (was %u, expected %u)",
        GST_BUFFER_SIZE (inbuf), alpha->width * alpha->height);
    return GST_FLOW_ERROR;
  }

  /* Transform in place */
  switch (alpha->in_format) {
    case GST_VIDEO_FORMAT_ARGB:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_ARGB:
          break;
        case GST_VIDEO_FORMAT_BGRA:
          transform_argb_bgra (GST_BUFFER_DATA (inbuf),
              GST_BUFFER_SIZE (inbuf));
          break;
        case GST_VIDEO_FORMAT_AYUV:
          transform_argb_ayuv (GST_BUFFER_DATA (inbuf),
              GST_BUFFER_SIZE (inbuf));
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      break;
    case GST_VIDEO_FORMAT_BGRA:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_BGRA:
          break;
        case GST_VIDEO_FORMAT_ARGB:
          transform_argb_bgra (GST_BUFFER_DATA (inbuf),
              GST_BUFFER_SIZE (inbuf));
          break;
        case GST_VIDEO_FORMAT_AYUV:
          transform_bgra_ayuv (GST_BUFFER_DATA (inbuf),
              GST_BUFFER_SIZE (inbuf));
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      break;
    case GST_VIDEO_FORMAT_AYUV:
      switch (alpha->out_format) {
        case GST_VIDEO_FORMAT_AYUV:
          break;
        case GST_VIDEO_FORMAT_ARGB:
          transform_ayuv_argb (GST_BUFFER_DATA (inbuf),
              GST_BUFFER_SIZE (inbuf));
          break;
        case GST_VIDEO_FORMAT_BGRA:
          transform_ayuv_bgra (GST_BUFFER_DATA (inbuf),
              GST_BUFFER_SIZE (inbuf));
          break;
        default:
          g_assert_not_reached ();
          break;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

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
    "RGBA from/to AYUV colorspace conversion preserving the alpha channel",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
