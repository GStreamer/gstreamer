/*
 * GStreamer
 * Copyright (C) 2010 Luis de Bethencourt <luis@debethencourt.com>
 * 
 * Solarize - curve adjustment video effect.
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
 * SECTION:element-solarize
 *
 * Solarize does a smart inverse in a video stream in realtime.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! solarize ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of solarize on a test stream
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <math.h>

#include "gstplugin.h"
#include "gstsolarize.h"

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (gst_solarize_debug);
#define GST_CAT_DEFAULT gst_solarize_debug

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
  PROP_THRESHOLD,
  PROP_START,
  PROP_END,
  PROP_SILENT
};

/* Initializations */

#define DEFAULT_THRESHOLD 127
#define DEFAULT_START 50
#define DEFAULT_END 185

static gint gate_int (gint value, gint min, gint max);
static void transform (guint32 * src, guint32 * dest, gint video_area,
    gint threshold, gint start, gint end);

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

GST_BOILERPLATE (GstSolarize, gst_solarize, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void gst_solarize_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_solarize_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_solarize_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_solarize_transform (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf);

/* GObject vmethod implementations */

static void
gst_solarize_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "Solarize",
      "Filter/Effect/Video",
      "Solarize tunable inverse in the video signal.",
      "Luis de Bethencourt <luis@debethencourt.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* Initialize the solarize's class. */
static void
gst_solarize_class_init (GstSolarizeClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_solarize_set_property;
  gobject_class->get_property = gst_solarize_get_property;

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_uint ("threshold", "Threshold",
          "Threshold parameter", 0, 256, DEFAULT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_START,
      g_param_spec_uint ("start", "Start",
          "Start parameter", 0, 256, DEFAULT_START,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_END,
      g_param_spec_uint ("end", "End",
          "End parameter", 0, 256, DEFAULT_END,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_solarize_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_solarize_transform);
}

/* Initialize the element,
 * instantiate pads and add them to element,
 * set pad calback functions, and
 * initialize instance structure.
 */
static void
gst_solarize_init (GstSolarize * filter, GstSolarizeClass * gclass)
{
  filter->threshold = DEFAULT_THRESHOLD;
  filter->start = DEFAULT_START;
  filter->end = DEFAULT_END;
  filter->silent = FALSE;
}

static void
gst_solarize_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSolarize *filter = GST_SOLARIZE (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_THRESHOLD:
      filter->threshold = g_value_get_uint (value);
      break;
    case PROP_START:
      filter->start = g_value_get_uint (value);
      break;
    case PROP_END:
      filter->end = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_solarize_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSolarize *filter = GST_SOLARIZE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_THRESHOLD:
      g_value_set_uint (value, filter->threshold);
      break;
    case PROP_START:
      g_value_set_uint (value, filter->start);
      break;
    case PROP_END:
      g_value_set_uint (value, filter->end);
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
gst_solarize_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstSolarize *filter = GST_SOLARIZE (btrans);
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
gst_solarize_transform (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  GstSolarize *filter = GST_SOLARIZE (btrans);
  gint video_size, threshold, start, end;
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
  threshold = filter->threshold;
  start = filter->start;
  end = filter->end;
  GST_OBJECT_UNLOCK (filter);

  video_size = filter->width * filter->height;
  transform (src, dest, video_size, threshold, start, end);

  return GST_FLOW_OK;
}

/* Entry point to initialize the plug-in.
 * Register the element factories and other features. */
gboolean
gst_solarize_plugin_init (GstPlugin * solarize)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_solarize_debug, "solarize",
      0, "Template solarize");

  return gst_element_register (solarize, "solarize", GST_RANK_NONE,
      GST_TYPE_SOLARIZE);
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
transform (guint32 * src, guint32 * dest, gint video_area,
    gint threshold, gint start, gint end)
{
  guint32 in;
  guint32 color[3];
  gint x, c;
  gint floor = 0;
  gint ceiling = 255;

  gint period, up_length, down_length, height_scale, param;

  period = end - start;
  if (period == 0) {
    period = 1;
  }

  up_length = threshold - start;
  if (up_length == 0) {
    up_length = 1;
  }

  down_length = end - threshold;
  if (down_length == 0) {
    down_length = 1;
  }

  height_scale = ceiling - floor;

  /* Loop through pixels. */
  for (x = 0; x < video_area; x++) {
    in = *src++;

    color[0] = (in >> 16) & 0xff;
    color[1] = (in >> 8) & 0xff;
    color[2] = (in) & 0xff;


    /* Loop through colors. */
    for (c = 0; c < 3; c++) {
      param = color[c];
      param += 256;
      param -= start;
      param %= period;

      if (param < up_length) {
        color[c] = param * height_scale;
        color[c] /= up_length;
        color[c] += floor;
      } else {
        color[c] = down_length - (param - up_length);
        color[c] *= height_scale;
        color[c] /= down_length;
        color[c] += floor;
      }
    }

    color[0] = gate_int (color[0], 0, 255);
    color[1] = gate_int (color[1], 0, 255);
    color[2] = gate_int (color[2], 0, 255);

    *dest++ = (color[0] << 16) | (color[1] << 8) | color[2];
  }
}
