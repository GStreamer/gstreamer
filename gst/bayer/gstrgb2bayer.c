/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@entropywave.com>
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
#include "gstrgb2bayer.h"

#define GST_CAT_DEFAULT gst_rgb2bayer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void gst_rgb2bayer_dispose (GObject * object);
static void gst_rgb2bayer_finalize (GObject * object);

static GstCaps *gst_rgb2bayer_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean
gst_rgb2bayer_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size);
static gboolean
gst_rgb2bayer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_rgb2bayer_start (GstBaseTransform * trans);
static gboolean gst_rgb2bayer_stop (GstBaseTransform * trans);
static gboolean gst_rgb2bayer_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_rgb2bayer_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_rgb2bayer_src_event (GstBaseTransform * trans,
    GstEvent * event);

static GstStaticPadTemplate gst_rgb2bayer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_ARGB)
    );

#if 0
/* do these later */
static GstStaticPadTemplate gst_rgb2bayer_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_ARGB ";"
        GST_VIDEO_CAPS_BGRA ";" GST_VIDEO_CAPS_ABGR)
    );
#endif

static GstStaticPadTemplate gst_rgb2bayer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-bayer,"
        "format=(string){bggr,gbrg,grbg,rggb},"
        "width=[1,MAX],height=[1,MAX]," "framerate=(fraction)[0/1,MAX]")
    );

/* class initialization */

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_rgb2bayer_debug, "rgb2bayer", 0, "rgb2bayer element");

GST_BOILERPLATE_FULL (GstRGB2Bayer, gst_rgb2bayer, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_rgb2bayer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rgb2bayer_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rgb2bayer_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RGB to Bayer converter",
      "Filter/Converter/Video",
      "Converts video/x-raw-rgb to video/x-raw-bayer",
      "David Schleef <ds@entropywave.com>");
}

static void
gst_rgb2bayer_class_init (GstRGB2BayerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->dispose = gst_rgb2bayer_dispose;
  gobject_class->finalize = gst_rgb2bayer_finalize;
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_rgb2bayer_transform_caps);
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_rgb2bayer_get_unit_size);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_rgb2bayer_set_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_rgb2bayer_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_rgb2bayer_stop);
  base_transform_class->event = GST_DEBUG_FUNCPTR (gst_rgb2bayer_event);
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_rgb2bayer_transform);
  base_transform_class->src_event = GST_DEBUG_FUNCPTR (gst_rgb2bayer_src_event);

}

static void
gst_rgb2bayer_init (GstRGB2Bayer * rgb2bayer,
    GstRGB2BayerClass * rgb2bayer_class)
{

}

void
gst_rgb2bayer_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_rgb2bayer_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_rgb2bayer_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstStructure *structure;
  GstStructure *new_structure;
  GstCaps *newcaps;
  const GValue *value;

  GST_DEBUG_OBJECT (trans, "transforming caps (from) %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  if (direction == GST_PAD_SRC) {
    newcaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
  } else {
    newcaps = gst_caps_new_simple ("video/x-raw-bayer", NULL);
  }
  new_structure = gst_caps_get_structure (newcaps, 0);

  value = gst_structure_get_value (structure, "width");
  gst_structure_set_value (new_structure, "width", value);

  value = gst_structure_get_value (structure, "height");
  gst_structure_set_value (new_structure, "height", value);

  value = gst_structure_get_value (structure, "framerate");
  gst_structure_set_value (new_structure, "framerate", value);

  GST_DEBUG_OBJECT (trans, "transforming caps (into) %" GST_PTR_FORMAT,
      newcaps);

  return newcaps;
}

static gboolean
gst_rgb2bayer_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstStructure *structure;
  int width;
  int height;
  int pixsize;
  const char *name;

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    name = gst_structure_get_name (structure);
    /* Our name must be either video/x-raw-bayer video/x-raw-rgb */
    if (g_str_equal (name, "video/x-raw-bayer")) {
      *size = width * height;
      return TRUE;
    } else {
      /* For output, calculate according to format */
      if (gst_structure_get_int (structure, "bpp", &pixsize)) {
        *size = width * height * (pixsize / 8);
        return TRUE;
      }
    }

  }

  return FALSE;
}

static gboolean
gst_rgb2bayer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRGB2Bayer *rgb2bayer = GST_RGB_2_BAYER (trans);
  GstStructure *structure;
  const char *format;

  GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps,
      outcaps);

  structure = gst_caps_get_structure (outcaps, 0);

  gst_structure_get_int (structure, "width", &rgb2bayer->width);
  gst_structure_get_int (structure, "height", &rgb2bayer->height);

  format = gst_structure_get_string (structure, "format");
  if (g_str_equal (format, "bggr")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_BGGR;
  } else if (g_str_equal (format, "gbrg")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_GBRG;
  } else if (g_str_equal (format, "grbg")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_GRBG;
  } else if (g_str_equal (format, "rggb")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_RGGB;
  } else {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_rgb2bayer_start (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_rgb2bayer_stop (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_rgb2bayer_event (GstBaseTransform * trans, GstEvent * event)
{

  return TRUE;
}

static GstFlowReturn
gst_rgb2bayer_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstRGB2Bayer *rgb2bayer = GST_RGB_2_BAYER (trans);
  guint8 *dest;
  guint8 *src;
  int i, j;
  int height = rgb2bayer->height;
  int width = rgb2bayer->width;

  dest = GST_BUFFER_DATA (outbuf);
  src = GST_BUFFER_DATA (inbuf);

  for (j = 0; j < height; j++) {
    guint8 *dest_line = dest + width * j;
    guint8 *src_line = src + width * 4 * j;

    for (i = 0; i < width; i++) {
      int is_blue = ((j & 1) << 1) | (i & 1);
      if (is_blue == rgb2bayer->format) {
        dest_line[i] = src_line[i * 4 + 3];
      } else if ((is_blue ^ 3) == rgb2bayer->format) {
        dest_line[i] = src_line[i * 4 + 1];
      } else {
        dest_line[i] = src_line[i * 4 + 2];
      }
    }
  }

  return GST_FLOW_OK;
}

static gboolean
gst_rgb2bayer_src_event (GstBaseTransform * trans, GstEvent * event)
{

  return TRUE;
}
