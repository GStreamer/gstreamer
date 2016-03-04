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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-templatematch
 *
 * Performs template matching on videos and images, providing detected positions via bus messages.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! decodebin ! videoconvert ! templatematch template=/path/to/file.jpg ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "../../gst-libs/gst/gst-i18n-plugin.h"
#include "gsttemplatematch.h"
#include <opencv2/imgproc/imgproc_c.h>

GST_DEBUG_CATEGORY_STATIC (gst_template_match_debug);
#define GST_CAT_DEFAULT gst_template_match_debug

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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("BGR"))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("BGR"))
    );

G_DEFINE_TYPE (GstTemplateMatch, gst_template_match,
    GST_TYPE_OPENCV_VIDEO_FILTER);

static void gst_template_match_finalize (GObject * object);
static void gst_template_match_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_template_match_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_template_match_transform_ip (GstOpencvVideoFilter *
    filter, GstBuffer * buf, IplImage * img);

/* initialize the templatematch's class */
static void
gst_template_match_class_init (GstTemplateMatchClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gobject_class->finalize = gst_template_match_finalize;
  gobject_class->set_property = gst_template_match_set_property;
  gobject_class->get_property = gst_template_match_get_property;

  gstopencvbasefilter_class->cv_trans_ip_func = gst_template_match_transform_ip;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_int ("method", "Method",
          "Specifies the way the template must be compared with image regions. 0=SQDIFF, 1=SQDIFF_NORMED, 2=CCOR, 3=CCOR_NORMED, 4=CCOEFF, 5=CCOEFF_NORMED.",
          0, 5, DEFAULT_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TEMPLATE,
      g_param_spec_string ("template", "Template", "Filename of template image",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_boolean ("display", "Display",
          "Sets whether the detected template should be highlighted in the output",
          TRUE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "templatematch",
      "Filter/Effect/Video",
      "Performs template matching on videos and images, providing detected positions via bus messages.",
      "Noam Lewis <jones.noamle@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_template_match_init (GstTemplateMatch * filter)
{
  filter->templ = NULL;
  filter->display = TRUE;
  filter->cvTemplateImage = NULL;
  filter->cvDistImage = NULL;
  filter->method = DEFAULT_METHOD;

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      TRUE);
}

/* We take ownership of template here */
static void
gst_template_match_load_template (GstTemplateMatch * filter, gchar * templ)
{
  gchar *oldTemplateFilename = NULL;
  IplImage *oldTemplateImage = NULL, *newTemplateImage = NULL, *oldDistImage =
      NULL;

  if (templ) {
    newTemplateImage = cvLoadImage (templ, CV_LOAD_IMAGE_COLOR);
    if (!newTemplateImage) {
      /* Unfortunately OpenCV doesn't seem to provide any way of finding out
         why the image load failed, so we can't be more specific than FAILED: */
      GST_ELEMENT_WARNING (filter, RESOURCE, FAILED,
          (_("OpenCV failed to load template image")),
          ("While attempting to load template '%s'", templ));
      g_free (templ);
      templ = NULL;
    }
  }

  GST_OBJECT_LOCK (filter);
  oldTemplateFilename = filter->templ;
  filter->templ = templ;
  oldTemplateImage = filter->cvTemplateImage;
  filter->cvTemplateImage = newTemplateImage;
  oldDistImage = filter->cvDistImage;
  /* This will be recreated in the chain function as required: */
  filter->cvDistImage = NULL;
  GST_OBJECT_UNLOCK (filter);

  cvReleaseImage (&oldDistImage);
  cvReleaseImage (&oldTemplateImage);
  g_free (oldTemplateFilename);
}

static void
gst_template_match_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTemplateMatch *filter = GST_TEMPLATE_MATCH (object);

  switch (prop_id) {
    case PROP_METHOD:
      GST_OBJECT_LOCK (filter);
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
      GST_OBJECT_UNLOCK (filter);
      break;
    case PROP_TEMPLATE:
      gst_template_match_load_template (filter, g_value_dup_string (value));
      break;
    case PROP_DISPLAY:
      GST_OBJECT_LOCK (filter);
      filter->display = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_template_match_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTemplateMatch *filter = GST_TEMPLATE_MATCH (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_int (value, filter->method);
      break;
    case PROP_TEMPLATE:
      g_value_set_string (value, filter->templ);
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

static void
gst_template_match_finalize (GObject * object)
{
  GstTemplateMatch *filter;
  filter = GST_TEMPLATE_MATCH (object);

  g_free (filter->templ);

  if (filter->cvDistImage) {
    cvReleaseImage (&filter->cvDistImage);
  }
  if (filter->cvTemplateImage) {
    cvReleaseImage (&filter->cvTemplateImage);
  }

  G_OBJECT_CLASS (gst_template_match_parent_class)->finalize (object);
}

static void
gst_template_match_match (IplImage * input, IplImage * templ,
    IplImage * dist_image, double *best_res, CvPoint * best_pos, int method)
{
  double dist_min = 0, dist_max = 0;
  CvPoint min_pos, max_pos;
  cvMatchTemplate (input, templ, dist_image, method);
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

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_template_match_transform_ip (GstOpencvVideoFilter * base, GstBuffer * buf,
    IplImage * img)
{
  GstTemplateMatch *filter;
  CvPoint best_pos;
  double best_res;
  GstMessage *m = NULL;

  filter = GST_TEMPLATE_MATCH (base);

  GST_LOG_OBJECT (filter, "Buffer size %u", (guint) gst_buffer_get_size (buf));

  GST_OBJECT_LOCK (filter);
  if (filter->cvTemplateImage && !filter->cvDistImage) {
    if (filter->cvTemplateImage->width > img->width) {
      GST_WARNING ("Template Image is wider than input image");
    } else if (filter->cvTemplateImage->height > img->height) {
      GST_WARNING ("Template Image is taller than input image");
    } else {

      GST_DEBUG_OBJECT (filter, "cvCreateImage (Size(%d-%d+1,%d) %d, %d)",
          img->width, filter->cvTemplateImage->width,
          img->height - filter->cvTemplateImage->height + 1, IPL_DEPTH_32F, 1);
      filter->cvDistImage =
          cvCreateImage (cvSize (img->width -
              filter->cvTemplateImage->width + 1,
              img->height - filter->cvTemplateImage->height + 1),
          IPL_DEPTH_32F, 1);
      if (!filter->cvDistImage) {
        GST_WARNING ("Couldn't create dist image.");
      }
    }
  }
  if (filter->cvTemplateImage && filter->cvDistImage) {
    GstStructure *s;

    gst_template_match_match (img, filter->cvTemplateImage,
        filter->cvDistImage, &best_res, &best_pos, filter->method);

    s = gst_structure_new ("template_match",
        "x", G_TYPE_UINT, best_pos.x,
        "y", G_TYPE_UINT, best_pos.y,
        "width", G_TYPE_UINT, filter->cvTemplateImage->width,
        "height", G_TYPE_UINT, filter->cvTemplateImage->height,
        "result", G_TYPE_DOUBLE, best_res, NULL);

    m = gst_message_new_element (GST_OBJECT (filter), s);

    if (filter->display) {
      CvPoint corner = best_pos;
      CvScalar color;
      if (filter->method == CV_TM_SQDIFF_NORMED
          || filter->method == CV_TM_CCORR_NORMED
          || filter->method == CV_TM_CCOEFF_NORMED) {
        /* Yellow growing redder as match certainty approaches 1.0.  This can
           only be applied with method == *_NORMED as the other match methods
           aren't normalized to be in range 0.0 - 1.0 */
        color = CV_RGB (255, 255 - pow (255, best_res), 32);
      } else {
        color = CV_RGB (255, 32, 32);
      }

      buf = gst_buffer_make_writable (buf);

      corner.x += filter->cvTemplateImage->width;
      corner.y += filter->cvTemplateImage->height;
      cvRectangle (img, best_pos, corner, color, 3, 8, 0);
    }

  }
  GST_OBJECT_UNLOCK (filter);

  if (m) {
    gst_element_post_message (GST_ELEMENT (filter), m);
  }
  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_template_match_plugin_init (GstPlugin * templatematch)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_template_match_debug, "templatematch",
      0,
      "Performs template matching on videos and images, providing detected positions via bus messages");

  return gst_element_register (templatematch, "templatematch", GST_RANK_NONE,
      GST_TYPE_TEMPLATE_MATCH);
}
