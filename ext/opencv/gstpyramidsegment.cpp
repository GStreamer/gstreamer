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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-pyramidsegment
 *
 * Applies pyramid segmentation to a video or image.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! pyramidsegment ! videoconvert ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstpyramidsegment.h"

#define BLOCK_SIZE 1000

GST_DEBUG_CATEGORY_STATIC (gst_pyramid_segment_debug);
#define GST_CAT_DEFAULT gst_pyramid_segment_debug

using namespace cv;
/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_THRESHOLD1,
  PROP_THRESHOLD2,
  PROP_LEVEL
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

G_DEFINE_TYPE (GstPyramidSegment, gst_pyramid_segment,
    GST_TYPE_OPENCV_VIDEO_FILTER);

static void gst_pyramid_segment_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pyramid_segment_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_pyramid_segment_transform_ip (GstOpencvVideoFilter *
    base, GstBuffer * buf, IplImage * img);

/* Clean up */
static void
gst_pyramid_segment_finalize (GObject * obj)
{
  GstPyramidSegment *filter = GST_PYRAMID_SEGMENT (obj);

  cvReleaseMemStorage (&filter->storage);

  G_OBJECT_CLASS (gst_pyramid_segment_parent_class)->finalize (obj);
}

/* initialize the pyramidsegment's class */
static void
gst_pyramid_segment_class_init (GstPyramidSegmentClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pyramid_segment_finalize);
  gobject_class->set_property = gst_pyramid_segment_set_property;
  gobject_class->get_property = gst_pyramid_segment_get_property;

  gstopencvbasefilter_class->cv_trans_ip_func =
      gst_pyramid_segment_transform_ip;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD1,
      g_param_spec_double ("threshold1", "Threshold1",
          "Error threshold for establishing links", 0, 1000, 50,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD2,
      g_param_spec_double ("threshold2", "Threshold2",
          "Error threshold for segment clustering", 0, 1000, 60,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_LEVEL,
      g_param_spec_int ("level", "Level",
          "Maximum level of the pyramid segmentation", 1, 4, 4,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "pyramidsegment",
      "Filter/Effect/Video",
      "Applies pyramid segmentation to a video or image.",
      "Michael Sheldon <mike@mikeasoft.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_pyramid_segment_init (GstPyramidSegment * filter)
{
  filter->storage = cvCreateMemStorage (BLOCK_SIZE);
  filter->comp =
      cvCreateSeq (0, sizeof (CvSeq), sizeof (CvPoint), filter->storage);
  filter->silent = FALSE;
  filter->threshold1 = 50.0;
  filter->threshold2 = 60.0;
  filter->level = 4;

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      TRUE);
}

static void
gst_pyramid_segment_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPyramidSegment *filter = GST_PYRAMID_SEGMENT (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case PROP_THRESHOLD1:
      filter->threshold1 = g_value_get_double (value);
      break;
    case PROP_THRESHOLD2:
      filter->threshold2 = g_value_get_double (value);
      break;
    case PROP_LEVEL:
      filter->level = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pyramid_segment_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPyramidSegment *filter = GST_PYRAMID_SEGMENT (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_THRESHOLD1:
      g_value_set_double (value, filter->threshold1);
      break;
    case PROP_THRESHOLD2:
      g_value_set_double (value, filter->threshold2);
      break;
    case PROP_LEVEL:
      g_value_set_int (value, filter->level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* transform function
 * this function does the actual processing
 */
static GstFlowReturn
gst_pyramid_segment_transform_ip (GstOpencvVideoFilter * base, GstBuffer * buf,
    IplImage * img)
{
  GstPyramidSegment *filter = GST_PYRAMID_SEGMENT (base);

  cvPyrSegmentation (img, img, filter->storage,
      &(filter->comp), filter->level, filter->threshold1, filter->threshold2);

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_pyramid_segment_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_pyramid_segment_debug, "pyramidsegment",
      0, "Applies pyramid segmentation to a video or image");

  return gst_element_register (plugin, "pyramidsegment", GST_RANK_NONE,
      GST_TYPE_PYRAMID_SEGMENT);
}
