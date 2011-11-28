/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Michael Sheldon <mike@mikeasoft.com>
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
 * SECTION:element-edgedetect
 *
 * Performs canny edge detection on videos and images
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-0.10 videotestsrc ! decodebin ! ffmpegcolorspace ! edgedetect ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstopencvutils.h"
#include "gstedgedetect.h"

GST_DEBUG_CATEGORY_STATIC (gst_edge_detect_debug);
#define GST_CAT_DEFAULT gst_edge_detect_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_THRESHOLD1,
  PROP_THRESHOLD2,
  PROP_APERTURE,
  PROP_MASK
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

GST_BOILERPLATE (GstEdgeDetect, gst_edge_detect, GstElement, GST_TYPE_ELEMENT);

static void gst_edge_detect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_edge_detect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_edge_detect_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_edge_detect_chain (GstPad * pad, GstBuffer * buf);

/* Clean up */
static void
gst_edge_detect_finalize (GObject * obj)
{
  GstEdgeDetect *filter = GST_EDGE_DETECT (obj);

  if (filter->cvImage != NULL) {
    cvReleaseImage (&filter->cvImage);
    cvReleaseImage (&filter->cvCEdge);
    cvReleaseImage (&filter->cvGray);
    cvReleaseImage (&filter->cvEdge);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* GObject vmethod implementations */
static void
gst_edge_detect_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "edgedetect",
      "Filter/Effect/Video",
      "Performs canny edge detection on videos and images.",
      "Michael Sheldon <mike@mikeasoft.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* initialize the edgedetect's class */
static void
gst_edge_detect_class_init (GstEdgeDetectClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_edge_detect_finalize);
  gobject_class->set_property = gst_edge_detect_set_property;
  gobject_class->get_property = gst_edge_detect_get_property;

  g_object_class_install_property (gobject_class, PROP_MASK,
      g_param_spec_boolean ("mask", "Mask",
          "Sets whether the detected edges should be used as a mask on the original input or not",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_THRESHOLD1,
      g_param_spec_int ("threshold1", "Threshold1",
          "Threshold value for canny edge detection", 0, 1000, 50,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_THRESHOLD2,
      g_param_spec_int ("threshold2", "Threshold2",
          "Second threshold value for canny edge detection", 0, 1000, 150,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_APERTURE,
      g_param_spec_int ("aperture", "Aperture",
          "Aperture size for Sobel operator (Must be either 3, 5 or 7", 3, 7, 3,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_edge_detect_init (GstEdgeDetect * filter, GstEdgeDetectClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_edge_detect_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_edge_detect_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->mask = TRUE;
  filter->threshold1 = 50;
  filter->threshold2 = 150;
  filter->aperture = 3;
}

static void
gst_edge_detect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEdgeDetect *filter = GST_EDGE_DETECT (object);

  switch (prop_id) {
    case PROP_MASK:
      filter->mask = g_value_get_boolean (value);
      break;
    case PROP_THRESHOLD1:
      filter->threshold1 = g_value_get_int (value);
      break;
    case PROP_THRESHOLD2:
      filter->threshold2 = g_value_get_int (value);
      break;
    case PROP_APERTURE:
      filter->aperture = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_edge_detect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEdgeDetect *filter = GST_EDGE_DETECT (object);

  switch (prop_id) {
    case PROP_MASK:
      g_value_set_boolean (value, filter->mask);
      break;
    case PROP_THRESHOLD1:
      g_value_set_int (value, filter->threshold1);
      break;
    case PROP_THRESHOLD2:
      g_value_set_int (value, filter->threshold2);
      break;
    case PROP_APERTURE:
      g_value_set_int (value, filter->aperture);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_edge_detect_set_caps (GstPad * pad, GstCaps * caps)
{
  GstEdgeDetect *filter;
  GstPad *otherpad;
  gint width, height;
  GstStructure *structure;

  filter = GST_EDGE_DETECT (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  filter->cvImage = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 3);
  filter->cvCEdge = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 3);
  filter->cvGray = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 1);
  filter->cvEdge = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 1);

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_edge_detect_chain (GstPad * pad, GstBuffer * buf)
{
  GstEdgeDetect *filter;
  GstBuffer *outbuf;

  filter = GST_EDGE_DETECT (GST_OBJECT_PARENT (pad));

  filter->cvImage->imageData = (char *) GST_BUFFER_DATA (buf);

  cvCvtColor (filter->cvImage, filter->cvGray, CV_RGB2GRAY);
  cvSmooth (filter->cvGray, filter->cvEdge, CV_BLUR, 3, 3, 0, 0);
  cvNot (filter->cvGray, filter->cvEdge);
  cvCanny (filter->cvGray, filter->cvEdge, filter->threshold1,
      filter->threshold2, filter->aperture);

  cvZero (filter->cvCEdge);
  if (filter->mask) {
    cvCopy (filter->cvImage, filter->cvCEdge, filter->cvEdge);
  } else {
    cvCvtColor (filter->cvEdge, filter->cvCEdge, CV_GRAY2RGB);
  }

  outbuf = gst_buffer_new_and_alloc (filter->cvCEdge->imageSize);
  gst_buffer_copy_metadata (outbuf, buf, GST_BUFFER_COPY_ALL);
  memcpy (GST_BUFFER_DATA (outbuf), filter->cvCEdge->imageData,
      GST_BUFFER_SIZE (outbuf));

  gst_buffer_unref (buf);
  return gst_pad_push (filter->srcpad, outbuf);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_edge_detect_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_edge_detect_debug, "edgedetect",
      0, "Performs canny edge detection on videos and images");

  return gst_element_register (plugin, "edgedetect", GST_RANK_NONE,
      GST_TYPE_EDGE_DETECT);
}
