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
 * SECTION:element-faceblur
 *
 * Blurs faces in images and videos.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-0.10 videotestsrc ! decodebin ! videoconvert ! faceblur ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstopencvutils.h"
#include "gstfaceblur.h"

GST_DEBUG_CATEGORY_STATIC (gst_face_blur_debug);
#define GST_CAT_DEFAULT gst_face_blur_debug

#define DEFAULT_PROFILE OPENCV_PREFIX G_DIR_SEPARATOR_S "share" \
    G_DIR_SEPARATOR_S "opencv" G_DIR_SEPARATOR_S "haarcascades" \
    G_DIR_SEPARATOR_S "haarcascade_frontalface_default.xml"

enum
{
  PROP_0,
  PROP_PROFILE
};

/* the capabilities of the inputs and outputs.
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

G_DEFINE_TYPE (GstFaceBlur, gst_face_blur, GST_TYPE_OPENCV_VIDEO_FILTER);

static void gst_face_blur_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_face_blur_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static gboolean gst_face_blur_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);
static GstFlowReturn gst_face_blur_transform_ip (GstOpencvVideoFilter *
    transform, GstBuffer * buffer, IplImage * img);

static void gst_face_blur_load_profile (GstFaceBlur * filter);

/* Clean up */
static void
gst_face_blur_finalize (GObject * obj)
{
  GstFaceBlur *filter = GST_FACE_BLUR (obj);

  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);

  if (filter->cvStorage)
    cvReleaseMemStorage (&filter->cvStorage);

  if (filter->cvCascade)
    cvReleaseHaarClassifierCascade (&filter->cvCascade);

  g_free (filter->profile);

  G_OBJECT_CLASS (gst_face_blur_parent_class)->finalize (obj);
}


/* initialize the faceblur's class */
static void
gst_face_blur_class_init (GstFaceBlurClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gstopencvbasefilter_class->cv_trans_ip_func = gst_face_blur_transform_ip;
  gstopencvbasefilter_class->cv_set_caps = gst_face_blur_set_caps;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_face_blur_finalize);
  gobject_class->set_property = gst_face_blur_set_property;
  gobject_class->get_property = gst_face_blur_get_property;

  g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_string ("profile", "Profile",
          "Location of Haar cascade file to use for face blurion",
          DEFAULT_PROFILE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "faceblur",
      "Filter/Effect/Video",
      "Blurs faces in images and videos",
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
gst_face_blur_init (GstFaceBlur * filter)
{
  filter->profile = g_strdup (DEFAULT_PROFILE);
  gst_face_blur_load_profile (filter);

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      TRUE);
}

static void
gst_face_blur_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFaceBlur *filter = GST_FACE_BLUR (object);

  switch (prop_id) {
    case PROP_PROFILE:
      g_free (filter->profile);
      filter->profile = g_value_dup_string (value);
      gst_face_blur_load_profile (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_face_blur_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFaceBlur *filter = GST_FACE_BLUR (object);

  switch (prop_id) {
    case PROP_PROFILE:
      g_value_set_string (value, filter->profile);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_face_blur_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels)
{
  GstFaceBlur *filter = GST_FACE_BLUR (transform);

  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);
  filter->cvGray =
      cvCreateImage (cvSize (in_width, in_height), IPL_DEPTH_8U, 1);

  if (filter->cvStorage)
    cvClearMemStorage (filter->cvStorage);
  else
    filter->cvStorage = cvCreateMemStorage (0);

  return TRUE;
}

static GstFlowReturn
gst_face_blur_transform_ip (GstOpencvVideoFilter * transform,
    GstBuffer * buffer, IplImage * img)
{
  GstFaceBlur *filter = GST_FACE_BLUR (transform);
  CvSeq *faces;
  int i;

  if (!filter->cvCascade)
    return GST_FLOW_OK;

  cvCvtColor (img, filter->cvGray, CV_RGB2GRAY);
  cvClearMemStorage (filter->cvStorage);

  faces =
      cvHaarDetectObjects (filter->cvGray, filter->cvCascade,
      filter->cvStorage, 1.1, 2, 0, cvSize (30, 30)
#if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
      , cvSize (0, 0)
#endif
      );

  for (i = 0; i < (faces ? faces->total : 0); i++) {
    CvRect *r = (CvRect *) cvGetSeqElem (faces, i);
    cvSetImageROI (img, *r);
    cvSmooth (img, img, CV_BLUR, 11, 11, 0, 0);
    cvSmooth (img, img, CV_GAUSSIAN, 11, 11, 0, 0);
    cvResetImageROI (img);
  }

  return GST_FLOW_OK;
}


static void
gst_face_blur_load_profile (GstFaceBlur * filter)
{
  if (filter->cvCascade)
    cvReleaseHaarClassifierCascade (&filter->cvCascade);
  filter->cvCascade =
      (CvHaarClassifierCascade *) cvLoad (filter->profile, 0, 0, 0);
  if (!filter->cvCascade)
    GST_WARNING ("Couldn't load Haar classifier cascade: %s.", filter->profile);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_face_blur_plugin_init (GstPlugin * plugin)
{
  /* debug category for filtering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_face_blur_debug, "faceblur",
      0, "Blurs faces in images and videos");

  return gst_element_register (plugin, "faceblur", GST_RANK_NONE,
      GST_TYPE_FACE_BLUR);
}
