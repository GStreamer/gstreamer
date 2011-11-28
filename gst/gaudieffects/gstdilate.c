/*
 * GStreamer
 * Copyright (C) 2010 Luis de Bethencourt <luis@debethencourt.com>
 * 
 * Dilate - dilated eye video effect.
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
 * SECTION:element-dilate
 *
 * Dilate adjusts the colors of a video stream in realtime.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! dilate ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of dilate on a test stream
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <math.h>

#include "gstplugin.h"
#include "gstdilate.h"

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (gst_dilate_debug);
#define GST_CAT_DEFAULT gst_dilate_debug

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
  PROP_0,
  PROP_ERODE,
  PROP_SILENT
};

/* Initializations */

#define DEFAULT_ERODE FALSE

static void transform (guint32 * src, guint32 * dest, gint video_area,
    gint width, gint height, gboolean erode);
static inline guint32 get_luminance (guint32 in);

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

GST_BOILERPLATE (GstDilate, gst_dilate, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void gst_dilate_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dilate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_dilate_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_dilate_transform (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf);

/* GObject vmethod implementations */

static void
gst_dilate_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "Dilate",
      "Filter/Effect/Video",
      "Dilate copies the brightest pixel around.",
      "Luis de Bethencourt <luis@debethencourt.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* Initialize the dilate's class. */
static void
gst_dilate_class_init (GstDilateClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_dilate_set_property;
  gobject_class->get_property = gst_dilate_get_property;

  g_object_class_install_property (gobject_class, PROP_ERODE,
      g_param_spec_boolean ("erode", "Erode", "Erode parameter", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_dilate_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_dilate_transform);
}

/* Initialize the element,
 * instantiate pads and add them to element,
 * set pad calback functions, and
 * initialize instance structure.
 */
static void
gst_dilate_init (GstDilate * filter, GstDilateClass * gclass)
{
  filter->erode = DEFAULT_ERODE;
  filter->silent = FALSE;
}

static void
gst_dilate_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDilate *filter = GST_DILATE (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_ERODE:
      filter->erode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dilate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDilate *filter = GST_DILATE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_ERODE:
      g_value_set_boolean (value, filter->erode);
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
gst_dilate_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDilate *filter = GST_DILATE (btrans);
  GstStructure *structure;
  gboolean ret = TRUE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

/* Actual processing. */
static GstFlowReturn
gst_dilate_transform (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  GstDilate *filter = GST_DILATE (btrans);
  gint video_size;
  gboolean erode;
  guint32 *src = (guint32 *) GST_BUFFER_DATA (in_buf);
  guint32 *dest = (guint32 *) GST_BUFFER_DATA (out_buf);
  GstClockTime timestamp;
  gint64 stream_time;

  video_size = filter->width * filter->height;

  /* GstController: update the properties */
  timestamp = GST_BUFFER_TIMESTAMP (in_buf);
  stream_time =
      gst_segment_to_stream_time (&btrans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (filter, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (filter), stream_time);

  GST_OBJECT_LOCK (filter);
  erode = filter->erode;
  GST_OBJECT_UNLOCK (filter);

  transform (src, dest, video_size, filter->width, filter->height, erode);

  return GST_FLOW_OK;
}

/* Entry point to initialize the plug-in.
 * Register the element factories and other features. */
gboolean
gst_dilate_plugin_init (GstPlugin * dilate)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_dilate_debug, "dilate", 0, "Template dilate");

  return gst_element_register (dilate, "dilate", GST_RANK_NONE,
      GST_TYPE_DILATE);
}

/*** Now the image processing work.... ***/

/* Return luminance of the color */
static inline guint32
get_luminance (guint32 in)
{
  guint32 red, green, blue, luminance;

  red = (in >> 16) & 0xff;
  green = (in >> 8) & 0xff;
  blue = (in) & 0xff;

  luminance = ((90 * red) + (115 * green) + (51 * blue));

  return luminance;
}

/* Transform processes each frame. */
static void
transform (guint32 * src, guint32 * dest, gint video_area, gint width,
    gint height, gboolean erode)
{
  guint32 out_luminance, down_luminance, right_luminance;
  guint32 up_luminance, left_luminance;

  guint32 *src_end = src + video_area;
  guint32 *up;
  guint32 *left;
  guint32 *down;
  guint32 *right;

  if (erode) {

    while (src != src_end) {
      guint32 *src_line_start = src;
      guint32 *src_line_end = src + width;
      while (src != src_line_end) {

        up = src - width;
        if (up < src) {
          up = src;
        }

        left = src - 1;
        if (left < src_line_start) {
          left = src;
        }

        down = src + width;
        if (down >= src_end) {
          down = src;
        }

        right = src + 1;
        if (right >= src_line_end) {
          right = src;
        }

        *dest = *src;
        out_luminance = get_luminance (*src);

        down_luminance = get_luminance (*down);
        if (down_luminance < out_luminance) {
          *dest = *down;
          out_luminance = down_luminance;
        }

        right_luminance = get_luminance (*right);
        if (right_luminance < out_luminance) {
          *dest = *right;
          out_luminance = right_luminance;
        }

        up_luminance = get_luminance (*up);
        if (up_luminance < out_luminance) {
          *dest = *up;
          out_luminance = up_luminance;
        }

        left_luminance = get_luminance (*left);
        if (left_luminance < out_luminance) {
          *dest = *left;
          out_luminance = left_luminance;
        }

        src += 1;
        dest += 1;
      }
    }

  } else {

    while (src != src_end) {
      guint32 *src_line_start = src;
      guint32 *src_line_end = src + width;
      while (src != src_line_end) {

        up = src - width;
        if (up < src) {
          up = src;
        }

        left = src - 1;
        if (left < src_line_start) {
          left = src;
        }

        down = src + width;
        if (down >= src_end) {
          down = src;
        }

        right = src + 1;
        if (right >= src_line_end) {
          right = src;
        }

        *dest = *src;
        out_luminance = get_luminance (*src);

        down_luminance = get_luminance (*down);
        if (down_luminance > out_luminance) {
          *dest = *down;
          out_luminance = down_luminance;
        }

        right_luminance = get_luminance (*right);
        if (right_luminance > out_luminance) {
          *dest = *right;
          out_luminance = right_luminance;
        }

        up_luminance = get_luminance (*up);
        if (up_luminance > out_luminance) {
          *dest = *up;
          out_luminance = up_luminance;
        }

        left_luminance = get_luminance (*left);
        if (left_luminance > out_luminance) {
          *dest = *left;
          out_luminance = left_luminance;
        }

        src += 1;
        dest += 1;
      }
    }
  }
}
