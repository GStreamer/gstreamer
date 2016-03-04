/*
 * GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
 * SECTION:element-cvlaplace
 *
 * Applies cvLaplace OpenCV function to the image.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! cvlaplace ! videoconvert ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstcvlaplace.h"
#include <opencv2/imgproc/imgproc_c.h>

GST_DEBUG_CATEGORY_STATIC (gst_cv_laplace_debug);
#define GST_CAT_DEFAULT gst_cv_laplace_debug

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

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};
enum
{
  PROP_0,
  PROP_APERTURE_SIZE,
  PROP_SCALE,
  PROP_SHIFT,
  PROP_MASK
};

#define DEFAULT_APERTURE_SIZE 3
#define DEFAULT_SCALE_FACTOR 1.0
#define DEFAULT_SHIFT 0.0
#define DEFAULT_MASK TRUE

G_DEFINE_TYPE (GstCvLaplace, gst_cv_laplace, GST_TYPE_OPENCV_VIDEO_FILTER);

static void gst_cv_laplace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cv_laplace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_cv_laplace_handle_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

static GstFlowReturn gst_cv_laplace_transform (GstOpencvVideoFilter * filter,
    GstBuffer * buf, IplImage * img, GstBuffer * outbuf, IplImage * outimg);

static gboolean gst_cv_laplace_cv_set_caps (GstOpencvVideoFilter * trans,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);

/* Clean up */
static void
gst_cv_laplace_finalize (GObject * obj)
{
  GstCvLaplace *filter = GST_CV_LAPLACE (obj);

  if (filter->intermediary_img) {
    cvReleaseImage (&filter->intermediary_img);
    cvReleaseImage (&filter->cvGray);
    cvReleaseImage (&filter->Laplace);
    cvReleaseImage (&filter->CLaplace);
  }

  G_OBJECT_CLASS (gst_cv_laplace_parent_class)->finalize (obj);
}

/* initialize the cvlaplace's class */
static void
gst_cv_laplace_class_init (GstCvLaplaceClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_cv_laplace_finalize);
  gobject_class->set_property = gst_cv_laplace_set_property;
  gobject_class->get_property = gst_cv_laplace_get_property;

  gstopencvbasefilter_class->cv_trans_func = gst_cv_laplace_transform;
  gstopencvbasefilter_class->cv_set_caps = gst_cv_laplace_cv_set_caps;

  g_object_class_install_property (gobject_class, PROP_APERTURE_SIZE,
      g_param_spec_int ("aperture-size", "aperture size",
          "Size of the extended Laplace Kernel (1, 3, 5 or 7)", 1, 7,
          DEFAULT_APERTURE_SIZE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_SCALE,
      g_param_spec_double ("scale", "scale factor",
          "Scale factor", 0.0, G_MAXDOUBLE,
          DEFAULT_SCALE_FACTOR, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_SHIFT,
      g_param_spec_double ("shift", "Shift",
          "Value added to the scaled source array elements", 0.0, G_MAXDOUBLE,
          DEFAULT_SHIFT, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_MASK,
      g_param_spec_boolean ("mask", "Mask",
          "Sets whether the detected edges should be used as a mask on the original input or not",
          DEFAULT_MASK, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_static_metadata (element_class,
      "cvlaplace",
      "Transform/Effect/Video",
      "Applies cvLaplace OpenCV function to the image",
      "Thiago Santos<thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_cv_laplace_init (GstCvLaplace * filter)
{
  gst_pad_set_event_function (GST_BASE_TRANSFORM_SINK_PAD (filter),
      GST_DEBUG_FUNCPTR (gst_cv_laplace_handle_sink_event));

  filter->aperture_size = DEFAULT_APERTURE_SIZE;
  filter->scale = DEFAULT_SCALE_FACTOR;
  filter->shift = DEFAULT_SHIFT;
  filter->mask = DEFAULT_MASK;

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      FALSE);
}

static gboolean
gst_cv_laplace_cv_set_caps (GstOpencvVideoFilter * trans, gint in_width,
    gint in_height, gint in_depth, gint in_channels, gint out_width,
    gint out_height, gint out_depth, gint out_channels)
{
  GstCvLaplace *filter = GST_CV_LAPLACE (trans);

  if (filter->intermediary_img) {
    cvReleaseImage (&filter->intermediary_img);
  }

  filter->intermediary_img =
      cvCreateImage (cvSize (out_width, out_height), IPL_DEPTH_16S, 1);

  return TRUE;
}

static void
gst_cv_laplace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCvLaplace *filter = GST_CV_LAPLACE (object);

  switch (prop_id) {
    case PROP_APERTURE_SIZE:{
      gint as = g_value_get_int (value);

      if (as % 2 != 1) {
        GST_WARNING_OBJECT (filter, "Invalid value %d for aperture size", as);
      } else
        filter->aperture_size = g_value_get_int (value);
    }
      break;
    case PROP_SCALE:
      filter->scale = g_value_get_double (value);
      break;
    case PROP_SHIFT:
      filter->shift = g_value_get_double (value);
      break;
    case PROP_MASK:
      filter->mask = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cv_laplace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCvLaplace *filter = GST_CV_LAPLACE (object);

  switch (prop_id) {
    case PROP_APERTURE_SIZE:
      g_value_set_int (value, filter->aperture_size);
      break;
    case PROP_SCALE:
      g_value_set_double (value, filter->scale);
      break;
    case PROP_SHIFT:
      g_value_set_double (value, filter->shift);
      break;
    case PROP_MASK:
      g_value_set_boolean (value, filter->mask);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_cv_laplace_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstCvLaplace *filter;
  gint width, height;
  GstStructure *structure;
  gboolean res = TRUE;

  filter = GST_CV_LAPLACE (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);

      structure = gst_caps_get_structure (caps, 0);
      gst_structure_get_int (structure, "width", &width);
      gst_structure_get_int (structure, "height", &height);

      if (filter->intermediary_img != NULL) {
        cvReleaseImage (&filter->intermediary_img);
        cvReleaseImage (&filter->CLaplace);
        cvReleaseImage (&filter->cvGray);
        cvReleaseImage (&filter->Laplace);
      }

      filter->CLaplace =
          cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 3);
      filter->intermediary_img =
          cvCreateImage (cvSize (width, height), IPL_DEPTH_16S, 1);
      filter->cvGray = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 1);
      filter->Laplace = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 1);
      break;
    }
    default:
      break;
  }

  res = gst_pad_event_default (pad, parent, event);

  return res;
}

static GstFlowReturn
gst_cv_laplace_transform (GstOpencvVideoFilter * base, GstBuffer * buf,
    IplImage * img, GstBuffer * outbuf, IplImage * outimg)
{
  GstCvLaplace *filter = GST_CV_LAPLACE (base);
  GstMapInfo out_info;

  g_assert (filter->intermediary_img);

  cvCvtColor (img, filter->cvGray, CV_RGB2GRAY);
  cvLaplace (filter->cvGray, filter->intermediary_img, filter->aperture_size);
  cvConvertScale (filter->intermediary_img, filter->Laplace, filter->scale,
      filter->shift);

  cvZero (filter->CLaplace);
  if (filter->mask) {
    cvCopy (img, filter->CLaplace, filter->Laplace);
  } else {
    cvCvtColor (filter->Laplace, filter->CLaplace, CV_GRAY2RGB);
  }

  gst_buffer_map (outbuf, &out_info, GST_MAP_WRITE);
  memcpy (out_info.data, filter->CLaplace->imageData,
      gst_buffer_get_size (outbuf));

  return GST_FLOW_OK;
}

gboolean
gst_cv_laplace_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_cv_laplace_debug, "cvlaplace", 0, "cvlaplace");

  return gst_element_register (plugin, "cvlaplace", GST_RANK_NONE,
      GST_TYPE_CV_LAPLACE);
}
