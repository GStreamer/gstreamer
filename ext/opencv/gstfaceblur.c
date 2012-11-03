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

#define HAAR_CASCADES_DIR OPENCV_PREFIX "/share/opencv/haarcascades/"
#define DEFAULT_PROFILE HAAR_CASCADES_DIR "haarcascade_frontalface_default.xml"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

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

G_DEFINE_TYPE (GstFaceBlur, gst_face_blur, GST_TYPE_ELEMENT);

static void gst_face_blur_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_face_blur_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_face_blur_handle_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_face_blur_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static void gst_face_blur_load_profile (GstFaceBlur * filter);

/* Clean up */
static void
gst_face_blur_finalize (GObject * obj)
{
  GstFaceBlur *filter = GST_FACE_BLUR (obj);

  if (filter->cvImage) {
    cvReleaseImage (&filter->cvImage);
    cvReleaseImage (&filter->cvGray);
  }

  g_free (filter->profile);

  G_OBJECT_CLASS (gst_face_blur_parent_class)->finalize (obj);
}


/* initialize the faceblur's class */
static void
gst_face_blur_class_init (GstFaceBlurClass * klass)
{
  GObjectClass *gobject_class;

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gobject_class = (GObjectClass *) klass;

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
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_face_blur_handle_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_face_blur_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->profile = g_strdup (DEFAULT_PROFILE);
  gst_face_blur_load_profile (filter);
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

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_face_blur_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstFaceBlur *filter;
  gint width, height;
  GstStructure *structure;
  gboolean res = TRUE;

  filter = GST_FACE_BLUR (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);

      structure = gst_caps_get_structure (caps, 0);
      gst_structure_get_int (structure, "width", &width);
      gst_structure_get_int (structure, "height", &height);

      filter->cvImage = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 3);
      filter->cvGray = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 1);
      filter->cvStorage = cvCreateMemStorage (0);
      break;
    }
    default:
      break;
  }

  res = gst_pad_event_default (pad, parent, event);

  return res;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_face_blur_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFaceBlur *filter;
  CvSeq *faces;
  GstMapInfo info;
  int i;

  filter = GST_FACE_BLUR (GST_OBJECT_PARENT (pad));

  buf = gst_buffer_make_writable (buf);
  gst_buffer_map (buf, &info, GST_MAP_READWRITE);
  filter->cvImage->imageData = (char *) info.data;

  cvCvtColor (filter->cvImage, filter->cvGray, CV_RGB2GRAY);
  cvClearMemStorage (filter->cvStorage);

  if (filter->cvCascade) {
    faces =
        cvHaarDetectObjects (filter->cvGray, filter->cvCascade,
        filter->cvStorage, 1.1, 2, 0, cvSize (30, 30)
#if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
        , cvSize (32, 32)
#endif
        );

    if (faces && faces->total > 0) {
      buf = gst_buffer_make_writable (buf);
    }
    for (i = 0; i < (faces ? faces->total : 0); i++) {
      CvRect *r = (CvRect *) cvGetSeqElem (faces, i);
      cvSetImageROI (filter->cvImage, *r);
      cvSmooth (filter->cvImage, filter->cvImage, CV_BLUR, 11, 11, 0, 0);
      cvSmooth (filter->cvImage, filter->cvImage, CV_GAUSSIAN, 11, 11, 0, 0);
      cvResetImageROI (filter->cvImage);
    }
  }

  /* these filters operate in place, so we push the same buffer */

  return gst_pad_push (filter->srcpad, buf);
}


static void
gst_face_blur_load_profile (GstFaceBlur * filter)
{
  filter->cvCascade =
      (CvHaarClassifierCascade *) cvLoad (filter->profile, 0, 0, 0);
  if (!filter->cvCascade) {
    GST_WARNING ("Couldn't load Haar classifier cascade: %s.", filter->profile);
  }
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
