/*
 * GStreamer
 * Copyright (C) 2010 Luis de Bethencourt <luis@debethencourt.com>
 * 
 * Exclusion - color exclusion video effect.
 * Based on Pete Warden's FreeFrame plugin with the same name.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-exclusion
 *
 * Exclusion saturates the colors of a video stream in realtime.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! exclusion ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of exclusion on a test stream
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <math.h>

#include "gstplugin.h"
#include "gstexclusion.h"

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (gst_exclusion_debug);
#define GST_CAT_DEFAULT gst_exclusion_debug

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_RGBx
#else
#define CAPS_STR GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR
#endif

/* Filter signals and args. */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0 = 0,
  PROP_FACTOR,
  PROP_SILENT
};

/* Initializations */

#define DEFAULT_FACTOR 175

static gint gate_int (gint value, gint min, gint max);
static void transform (guint32 * src, guint32 * dest, gint video_area,
    gint factor);

/* The capabilities of the inputs and outputs. */

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

GST_BOILERPLATE (GstExclusion, gst_exclusion, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void gst_exclusion_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_exclusion_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_exclusion_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_exclusion_transform (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf);

/* GObject vmethod implementations */

static void
gst_exclusion_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "Exclusion",
      "Filter/Effect/Video",
      "Exclusion exclodes the colors in the video signal.",
      "Luis de Bethencourt <luis@debethencourt.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* Initialize the exclusion's class. */
static void
gst_exclusion_class_init (GstExclusionClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_exclusion_set_property;
  gobject_class->get_property = gst_exclusion_get_property;

  g_object_class_install_property (gobject_class, PROP_FACTOR,
      g_param_spec_uint ("factor", "Factor",
          "Exclusion factor parameter", 0, 175, DEFAULT_FACTOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_exclusion_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_exclusion_transform);
}

/* Initialize the element,
 * instantiate pads and add them to element,
 * set pad calback functions, and
 * initialize instance structure.
 */
static void
gst_exclusion_init (GstExclusion * filter, GstExclusionClass * gclass)
{
  filter->factor = DEFAULT_FACTOR;
  filter->silent = FALSE;
}

static void
gst_exclusion_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstExclusion *filter = GST_EXCLUSION (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_FACTOR:
      filter->factor = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_exclusion_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstExclusion *filter = GST_EXCLUSION (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_FACTOR:
      g_value_set_uint (value, filter->factor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

/* GstElement vmethod implementations */
/* Handle the link with other elements. */
static gboolean
gst_exclusion_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstExclusion *filter = GST_EXCLUSION (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  GST_OBJECT_LOCK (filter);
  structure = gst_caps_get_structure (incaps, 0);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

/* Actual processing. */
static GstFlowReturn
gst_exclusion_transform (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  GstExclusion *filter = GST_EXCLUSION (btrans);
  gint video_size, factor;
  guint32 *src = (guint32 *) GST_BUFFER_DATA (in_buf);
  guint32 *dest = (guint32 *) GST_BUFFER_DATA (out_buf);
  GstClockTime timestamp;
  gint64 stream_time;

  /* GstController: update the properties */
  timestamp = GST_BUFFER_TIMESTAMP (in_buf);
  stream_time =
      gst_segment_to_stream_time (&btrans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (filter, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (filter), stream_time);

  GST_OBJECT_LOCK (filter);
  factor = filter->factor;
  GST_OBJECT_UNLOCK (filter);

  video_size = filter->width * filter->height;
  transform (src, dest, video_size, factor);

  return GST_FLOW_OK;
}

/* Entry point to initialize the plug-in.
 * Register the element factories and other features. */
gboolean
gst_exclusion_plugin_init (GstPlugin * exclusion)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_exclusion_debug, "exclusion",
      0, "Template exclusion");

  return gst_element_register (exclusion, "exclusion", GST_RANK_NONE,
      GST_TYPE_EXCLUSION);
}

/*** Now the image processing work.... ***/
/* Keep the values inbounds. */
static gint
gate_int (gint value, gint min, gint max)
{
  if (value < min) {
    return min;
  } else if (value > max) {
    return max;
  } else {
    return value;
  }
}

/* Transform processes each frame. */
static void
transform (guint32 * src, guint32 * dest, gint video_area, gint factor)
{
  guint32 in, red, green, blue;
  gint x;

  for (x = 0; x < video_area; x++) {
    in = *src++;

    red = (in >> 16) & 0xff;
    green = (in >> 8) & 0xff;
    blue = (in) & 0xff;

    red = factor -
        (((factor - red) * (factor - red) / factor) + ((green * red) / factor));
    green = factor -
        (((factor - green) * (factor - green) / factor) +
        ((green * green) / factor));
    blue = factor -
        (((factor - blue) * (factor - blue) / factor) +
        ((blue * blue) / factor));

    red = gate_int (red, 0, 255);
    green = gate_int (green, 0, 255);
    blue = gate_int (blue, 0, 255);

    *dest++ = (red << 16) | (green << 8) | blue;
  }
}
