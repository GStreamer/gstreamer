/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2008 Michael Sheldon <mike@mikeasoft.com>
 * Copyright (C) 2009 Noam Lewis <jones.noamle@gmail.com>
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
 * SECTION:element-templatematch
 *
 * FIXME:Describe templatematch here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-0.10 videotestsrc ! decodebin ! ffmpegcolorspace ! templatematch template=/path/to/file.jpg ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstopencvutils.h"
#include "gsttemplatematch.h"

GST_DEBUG_CATEGORY_STATIC (gst_templatematch_debug);
#define GST_CAT_DEFAULT gst_templatematch_debug

#define DEFAULT_METHOD (3)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_METHOD,
  PROP_TEMPLATE,
  PROP_DISPLAY,
};

/* the capabilities of the inputs and outputs.
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

GST_BOILERPLATE (GstTemplateMatch, gst_templatematch, GstElement,
    GST_TYPE_ELEMENT);

static void gst_templatematch_finalize (GObject * object);
static void gst_templatematch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_templatematch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_templatematch_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_templatematch_chain (GstPad * pad, GstBuffer * buf);

static void gst_templatematch_load_template (GstTemplateMatch * filter);
static void gst_templatematch_match (IplImage * input, IplImage * template,
    IplImage * dist_image, double *best_res, CvPoint * best_pos, int method);

/* GObject vmethod implementations */

static void
gst_templatematch_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "templatematch",
      "Filter/Effect/Video",
      "Performs template matching on videos and images, providing detected positions via bus messages",
      "Noam Lewis <jones.noamle@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the templatematch's class */
static void
gst_templatematch_class_init (GstTemplateMatchClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_templatematch_finalize;
  gobject_class->set_property = gst_templatematch_set_property;
  gobject_class->get_property = gst_templatematch_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_int ("method", "Method",
          "Specifies the way the template must be compared with image regions. 0=SQDIFF, 1=SQDIFF_NORMED, 2=CCOR, 3=CCOR_NORMED, 4=CCOEFF, 5=CCOEFF_NORMED.",
          0, 5, DEFAULT_METHOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TEMPLATE,
      g_param_spec_string ("template", "Template", "Filename of template image",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_boolean ("display", "Display",
          "Sets whether the detected template should be highlighted in the output",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_templatematch_init (GstTemplateMatch * filter,
    GstTemplateMatchClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_templatematch_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_templatematch_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->template = NULL;
  filter->display = TRUE;
  filter->cvTemplateImage = NULL;
  filter->cvDistImage = NULL;
  filter->cvImage = NULL;
  filter->method = DEFAULT_METHOD;
  gst_templatematch_load_template (filter);
}

static void
gst_templatematch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTemplateMatch *filter = GST_TEMPLATEMATCH (object);

  switch (prop_id) {
    case PROP_METHOD:
      switch (g_value_get_int (value)) {
        case 0:
          filter->method = CV_TM_SQDIFF;
          break;
        case 1:
          filter->method = CV_TM_SQDIFF_NORMED;
          break;
        case 2:
          filter->method = CV_TM_CCORR;
          break;
        case 3:
          filter->method = CV_TM_CCORR_NORMED;
          break;
        case 4:
          filter->method = CV_TM_CCOEFF;
          break;
        case 5:
          filter->method = CV_TM_CCOEFF_NORMED;
          break;
      }
      break;
    case PROP_TEMPLATE:
      filter->template = (char *) g_value_get_string (value);
      gst_templatematch_load_template (filter);
      break;
    case PROP_DISPLAY:
      filter->display = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_templatematch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTemplateMatch *filter = GST_TEMPLATEMATCH (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_int (value, filter->method);
      break;
    case PROP_TEMPLATE:
      g_value_set_string (value, filter->template);
      break;
    case PROP_DISPLAY:
      g_value_set_boolean (value, filter->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_templatematch_set_caps (GstPad * pad, GstCaps * caps)
{
  GstTemplateMatch *filter;
  GstPad *otherpad;
  gint width, height;
  GstStructure *structure;

  filter = GST_TEMPLATEMATCH (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  filter->cvImage =
      cvCreateImageHeader (cvSize (width, height), IPL_DEPTH_8U, 3);

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

static void
gst_templatematch_finalize (GObject * object)
{
  GstTemplateMatch *filter;
  filter = GST_TEMPLATEMATCH (object);

  if (filter->cvImage) {
    cvReleaseImageHeader (&filter->cvImage);
  }
  if (filter->cvDistImage) {
    cvReleaseImage (&filter->cvDistImage);
  }
  if (filter->cvTemplateImage) {
    cvReleaseImage (&filter->cvTemplateImage);
  }
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_templatematch_chain (GstPad * pad, GstBuffer * buf)
{
  GstTemplateMatch *filter;
  CvPoint best_pos;
  double best_res;

  filter = GST_TEMPLATEMATCH (GST_OBJECT_PARENT (pad));

  /* FIXME Why template == NULL returns OK?
   * shouldn't it be a passthrough instead? */
  if ((!filter) || (!buf) || filter->template == NULL) {
    return GST_FLOW_OK;
  }
  filter->cvImage->imageData = (char *) GST_BUFFER_DATA (buf);

  if (!filter->cvDistImage) {
    filter->cvDistImage =
        cvCreateImage (cvSize (filter->cvImage->width -
            filter->cvTemplateImage->width + 1,
            filter->cvImage->height - filter->cvTemplateImage->height + 1),
        IPL_DEPTH_32F, 1);
    if (!filter->cvDistImage) {
      GST_WARNING ("Couldn't create dist image.");
    }
  }
  if (filter->cvTemplateImage) {
    GstStructure *s;
    GstMessage *m;

    gst_templatematch_match (filter->cvImage, filter->cvTemplateImage,
        filter->cvDistImage, &best_res, &best_pos, filter->method);

    s = gst_structure_new ("template_match",
        "x", G_TYPE_UINT, best_pos.x,
        "y", G_TYPE_UINT, best_pos.y,
        "width", G_TYPE_UINT, filter->cvTemplateImage->width,
        "height", G_TYPE_UINT, filter->cvTemplateImage->height,
        "result", G_TYPE_DOUBLE, best_res, NULL);

    m = gst_message_new_element (GST_OBJECT (filter), s);
    gst_element_post_message (GST_ELEMENT (filter), m);

    if (filter->display) {
      CvPoint corner = best_pos;

      buf = gst_buffer_make_writable (buf);

      corner.x += filter->cvTemplateImage->width;
      corner.y += filter->cvTemplateImage->height;
      cvRectangle (filter->cvImage, best_pos, corner, CV_RGB (255, 32, 32), 3,
          8, 0);
    }

  }

  return gst_pad_push (filter->srcpad, buf);
}



static void
gst_templatematch_match (IplImage * input, IplImage * template,
    IplImage * dist_image, double *best_res, CvPoint * best_pos, int method)
{
  double dist_min = 0, dist_max = 0;
  CvPoint min_pos, max_pos;
  cvMatchTemplate (input, template, dist_image, method);
  cvMinMaxLoc (dist_image, &dist_min, &dist_max, &min_pos, &max_pos, NULL);
  if ((CV_TM_SQDIFF_NORMED == method) || (CV_TM_SQDIFF == method)) {
    *best_res = dist_min;
    *best_pos = min_pos;
    if (CV_TM_SQDIFF_NORMED == method) {
      *best_res = 1 - *best_res;
    }
  } else {
    *best_res = dist_max;
    *best_pos = max_pos;
  }
}


static void
gst_templatematch_load_template (GstTemplateMatch * filter)
{
  if (filter->template) {
    filter->cvTemplateImage =
        cvLoadImage (filter->template, CV_LOAD_IMAGE_COLOR);
    if (!filter->cvTemplateImage) {
      GST_WARNING ("Couldn't load template image: %s.", filter->template);
    }
  }
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_templatematch_plugin_init (GstPlugin * templatematch)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_templatematch_debug, "templatematch",
      0,
      "Performs template matching on videos and images, providing detected positions via bus messages");

  return gst_element_register (templatematch, "templatematch", GST_RANK_NONE,
      GST_TYPE_TEMPLATEMATCH);
}
