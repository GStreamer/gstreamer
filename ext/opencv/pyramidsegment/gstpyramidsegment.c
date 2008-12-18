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
 * SECTION:element-pyramidsegment
 *
 * FIXME:Describe pyramidsegment here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! pyramidsegment ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstpyramidsegment.h"

#define BLOCK_SIZE 1000

GST_DEBUG_CATEGORY_STATIC (gst_pyramidsegment_debug);
#define GST_CAT_DEFAULT gst_pyramidsegment_debug

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
    GST_STATIC_CAPS ("video/x-raw-rgb")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb")
    );

GST_BOILERPLATE (Gstpyramidsegment, gst_pyramidsegment, GstElement,
    GST_TYPE_ELEMENT);

static void gst_pyramidsegment_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pyramidsegment_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_pyramidsegment_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_pyramidsegment_chain (GstPad * pad, GstBuffer * buf);

/* GObject vmethod implementations */

static void
gst_pyramidsegment_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "pyramidsegment",
    "Filter/Effect/Video",
    "Applies pyramid segmentation to a video or image.",
    "Michael Sheldon <mike@mikeasoft.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the pyramidsegment's class */
static void
gst_pyramidsegment_class_init (GstpyramidsegmentClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_pyramidsegment_set_property;
  gobject_class->get_property = gst_pyramidsegment_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD1,
      g_param_spec_double ("threshold1", "Threshold1", "Error threshold for establishing links",
          0, 1000, 50, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD2,
      g_param_spec_double ("threshold2", "Threshold2", "Error threshold for segment clustering",
          0, 1000, 60, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LEVEL,
      g_param_spec_int ("level", "Level", "Maximum level of the pyramid segmentation",
          0, 4, 4, G_PARAM_READWRITE));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_pyramidsegment_init (Gstpyramidsegment * filter,
    GstpyramidsegmentClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pyramidsegment_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_pyramidsegment_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->storage = cvCreateMemStorage ( BLOCK_SIZE );
  filter->comp = cvCreateSeq(0, sizeof(CvSeq), sizeof(CvPoint), filter->storage);
  filter->silent = FALSE;
  filter->threshold1 = 50.0;
  filter->threshold2 = 60.0;
  filter->level = 4;
}

static void
gst_pyramidsegment_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstpyramidsegment *filter = GST_PYRAMIDSEGMENT (object);

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
gst_pyramidsegment_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstpyramidsegment *filter = GST_PYRAMIDSEGMENT (object);

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

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_pyramidsegment_set_caps (GstPad * pad, GstCaps * caps)
{
  Gstpyramidsegment *filter;
  GstPad *otherpad;
  GstStructure *structure;
  gint width, height;

  filter = GST_PYRAMIDSEGMENT (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  
  filter->cvImage = cvCreateImage(cvSize(width, height), IPL_DEPTH_8U, 3);

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_pyramidsegment_chain (GstPad * pad, GstBuffer * buf)
{
  Gstpyramidsegment *filter;

  filter = GST_PYRAMIDSEGMENT (GST_OBJECT_PARENT (pad));

  filter->cvImage->imageData = (char *) GST_BUFFER_DATA (buf);
  filter->cvSegmentedImage = cvCloneImage(filter->cvImage);

  cvPyrSegmentation(filter->cvImage, filter->cvSegmentedImage, filter->storage, &(filter->comp), filter->level, filter->threshold1, filter->threshold2);

  gst_buffer_set_data(buf, filter->cvSegmentedImage->imageData, filter->cvSegmentedImage->imageSize);

  /* just push out the incoming buffer without touching it */
  return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
pyramidsegment_init (GstPlugin * pyramidsegment)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template pyramidsegment' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_pyramidsegment_debug, "pyramidsegment",
      0, "Applies pyramid segmentation to a video or image");

  return gst_element_register (pyramidsegment, "pyramidsegment", GST_RANK_NONE,
      GST_TYPE_PYRAMIDSEGMENT);
}

/* gstreamer looks for this structure to register pyramidsegments
 *
 * exchange the string 'Template pyramidsegment' with your pyramidsegment description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "pyramidsegment",
    "Applies pyramid segmentation to a video or image",
    pyramidsegment_init,
    VERSION,
    "LGPL",
    "GStreamer OpenCV Plugins",
    "http://www.mikeasoft.com"
)
