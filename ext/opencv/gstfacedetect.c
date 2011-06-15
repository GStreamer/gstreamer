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
 * SECTION:element-facedetect
 *
 * Performs face detection on videos and images.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-0.10 videotestsrc ! decodebin ! ffmpegcolorspace ! facedetect ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstfacedetect.h"

GST_DEBUG_CATEGORY_STATIC (gst_facedetect_debug);
#define GST_CAT_DEFAULT gst_facedetect_debug

#define DEFAULT_PROFILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_default.xml"
#define DEFAULT_SCALE_FACTOR 1.1
#define DEFAULT_FLAGS 0
#define DEFAULT_MIN_NEIGHBORS 3
#define DEFAULT_MIN_SIZE_WIDTH 0
#define DEFAULT_MIN_SIZE_HEIGHT 0

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_PROFILE,
  PROP_SCALE_FACTOR,
  PROP_MIN_NEIGHBORS,
  PROP_FLAGS,
  PROP_MIN_SIZE_WIDTH,
  PROP_MIN_SIZE_HEIGHT
};


/**
 * GstOpencvFaceDetectFlags:
 *
 * Flags parameter to OpenCV's cvHaarDetectObjects function.
 */
typedef enum
{
  GST_OPENCV_FACE_DETECT_HAAR_DO_CANNY_PRUNING = (1 << 0)
} GstOpencvFaceDetectFlags;

#define GST_TYPE_OPENCV_FACE_DETECT_FLAGS (gst_opencv_face_detect_flags_get_type())

static void
register_gst_opencv_face_detect_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {(guint) GST_OPENCV_FACE_DETECT_HAAR_DO_CANNY_PRUNING,
        "Do Canny edge detection to discard some regions", "do-canny-pruning"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstOpencvFaceDetectFlags", values);
}

static GType
gst_opencv_face_detect_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_opencv_face_detect_flags, &id);
  return id;
}

/* the capabilities of the inputs and outputs.
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

GST_BOILERPLATE (Gstfacedetect, gst_facedetect, GstOpencvVideoFilter,
    GST_TYPE_OPENCV_VIDEO_FILTER);

static void gst_facedetect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_facedetect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_facedetect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);
static GstFlowReturn gst_facedetect_transform_ip (GstOpencvVideoFilter * base,
    GstBuffer * buf, IplImage * img);

static void gst_facedetect_load_profile (Gstfacedetect * filter);

/* Clean up */
static void
gst_facedetect_finalize (GObject * obj)
{
  Gstfacedetect *filter = GST_FACEDETECT (obj);

  if (filter->cvGray) {
    cvReleaseImage (&filter->cvGray);
  }
  if (filter->cvStorage) {
    cvReleaseMemStorage (&filter->cvStorage);
  }

  g_free (filter->profile);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}


/* GObject vmethod implementations */
static void
gst_facedetect_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "facedetect",
      "Filter/Effect/Video",
      "Performs face detection on videos and images, providing detected positions via bus messages",
      "Michael Sheldon <mike@mikeasoft.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the facedetect's class */
static void
gst_facedetect_class_init (GstfacedetectClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_facedetect_finalize);
  gobject_class->set_property = gst_facedetect_set_property;
  gobject_class->get_property = gst_facedetect_get_property;

  gstopencvbasefilter_class->cv_trans_ip_func = gst_facedetect_transform_ip;
  gstopencvbasefilter_class->cv_set_caps = gst_facedetect_set_caps;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_boolean ("display", "Display",
          "Sets whether the detected faces should be highlighted in the output",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_string ("profile", "Profile",
          "Location of Haar cascade file to use for face detection",
          DEFAULT_PROFILE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FLAGS,
      g_param_spec_flags ("flags", "Flags", "Flags to cvHaarDetectObjects",
          GST_TYPE_OPENCV_FACE_DETECT_FLAGS, DEFAULT_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SCALE_FACTOR,
      g_param_spec_double ("scale-factor", "Scale factor",
          "Factor by which the windows is scaled after each scan",
          1.1, 10.0, DEFAULT_SCALE_FACTOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_NEIGHBORS,
      g_param_spec_int ("min-neighbors", "Mininum neighbors",
          "Minimum number (minus 1) of neighbor rectangles that makes up "
          "an object", 0, G_MAXINT, DEFAULT_MIN_NEIGHBORS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_SIZE_WIDTH,
      g_param_spec_int ("min-size-width", "Minimum size width",
          "Minimum window width size", 0, G_MAXINT, DEFAULT_MIN_SIZE_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_SIZE_HEIGHT,
      g_param_spec_int ("min-size-height", "Minimum size height",
          "Minimum window height size", 0, G_MAXINT, DEFAULT_MIN_SIZE_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_facedetect_init (Gstfacedetect * filter, GstfacedetectClass * gclass)
{
  filter->profile = g_strdup (DEFAULT_PROFILE);
  filter->display = TRUE;
  filter->scale_factor = DEFAULT_SCALE_FACTOR;
  filter->min_neighbors = DEFAULT_MIN_NEIGHBORS;
  filter->flags = DEFAULT_FLAGS;
  filter->min_size_width = DEFAULT_MIN_SIZE_WIDTH;
  filter->min_size_height = DEFAULT_MIN_SIZE_HEIGHT;
  gst_facedetect_load_profile (filter);

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      TRUE);
}

static void
gst_facedetect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstfacedetect *filter = GST_FACEDETECT (object);

  switch (prop_id) {
    case PROP_PROFILE:
      g_free (filter->profile);
      filter->profile = g_value_dup_string (value);
      gst_facedetect_load_profile (filter);
      break;
    case PROP_DISPLAY:
      filter->display = g_value_get_boolean (value);
      break;
    case PROP_SCALE_FACTOR:
      filter->scale_factor = g_value_get_double (value);
      break;
    case PROP_MIN_NEIGHBORS:
      filter->min_neighbors = g_value_get_int (value);
      break;
    case PROP_MIN_SIZE_WIDTH:
      filter->min_size_width = g_value_get_int (value);
      break;
    case PROP_MIN_SIZE_HEIGHT:
      filter->min_size_height = g_value_get_int (value);
      break;
    case PROP_FLAGS:
      filter->flags = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_facedetect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstfacedetect *filter = GST_FACEDETECT (object);

  switch (prop_id) {
    case PROP_PROFILE:
      g_value_set_string (value, filter->profile);
      break;
    case PROP_DISPLAY:
      g_value_set_boolean (value, filter->display);
      break;
    case PROP_SCALE_FACTOR:
      g_value_set_double (value, filter->scale_factor);
      break;
    case PROP_MIN_NEIGHBORS:
      g_value_set_int (value, filter->min_neighbors);
      break;
    case PROP_MIN_SIZE_WIDTH:
      g_value_set_int (value, filter->min_size_width);
      break;
    case PROP_MIN_SIZE_HEIGHT:
      g_value_set_int (value, filter->min_size_height);
      break;
    case PROP_FLAGS:
      g_value_set_flags (value, filter->flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_facedetect_set_caps (GstOpencvVideoFilter * transform, gint in_width,
    gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels)
{
  Gstfacedetect *filter;

  filter = GST_FACEDETECT (transform);

  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);

  filter->cvGray = cvCreateImage (cvSize (in_width, in_height), IPL_DEPTH_8U,
      1);

  if (!filter->cvStorage)
    filter->cvStorage = cvCreateMemStorage (0);
  else
    cvClearMemStorage (filter->cvStorage);

  return TRUE;
}

static GstMessage *
gst_facedetect_message_new (Gstfacedetect * filter, GstBuffer * buf)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (filter);
  GstStructure *s;
  GstClockTime running_time, stream_time;

  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buf));
  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buf));

  s = gst_structure_new ("facedetect",
      "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (buf),
      "stream-time", G_TYPE_UINT64, stream_time,
      "running-time", G_TYPE_UINT64, running_time,
      "duration", G_TYPE_UINT64, GST_BUFFER_DURATION (buf), NULL);

  return gst_message_new_element (GST_OBJECT (filter), s);
}


/* 
 * Performs the face detection
 */
static GstFlowReturn
gst_facedetect_transform_ip (GstOpencvVideoFilter * base, GstBuffer * buf,
    IplImage * img)
{
  Gstfacedetect *filter;
  CvSeq *faces;
  int i;

  filter = GST_FACEDETECT (base);

  cvCvtColor (img, filter->cvGray, CV_RGB2GRAY);
  cvClearMemStorage (filter->cvStorage);

  if (filter->cvCascade) {
    GstMessage *msg = NULL;
    GValue facelist = { 0 };

    faces =
        cvHaarDetectObjects (filter->cvGray, filter->cvCascade,
        filter->cvStorage, filter->scale_factor, filter->min_neighbors,
        filter->flags, cvSize (filter->min_size_width, filter->min_size_height)
#if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
        , cvSize (filter->min_size_width + 2, filter->min_size_height + 2)
#endif
        );

    if (faces && faces->total > 0) {
      msg = gst_facedetect_message_new (filter, buf);
      g_value_init (&facelist, GST_TYPE_LIST);
    }

    for (i = 0; i < (faces ? faces->total : 0); i++) {
      CvRect *r = (CvRect *) cvGetSeqElem (faces, i);
      GValue value = { 0 };

      GstStructure *s = gst_structure_new ("face",
          "x", G_TYPE_UINT, r->x,
          "y", G_TYPE_UINT, r->y,
          "width", G_TYPE_UINT, r->width,
          "height", G_TYPE_UINT, r->height, NULL);

      GstMessage *m = gst_message_new_element (GST_OBJECT (filter), s);

      g_value_init (&value, GST_TYPE_STRUCTURE);
      gst_value_set_structure (&value, s);
      gst_value_list_append_value (&facelist, &value);
      g_value_unset (&value);

      gst_element_post_message (GST_ELEMENT (filter), m);

      if (filter->display) {
        if (gst_buffer_is_writable (buf)) {
          CvPoint center;
          int radius;
          center.x = cvRound ((r->x + r->width * 0.5));
          center.y = cvRound ((r->y + r->height * 0.5));
          radius = cvRound ((r->width + r->height) * 0.25);
          cvCircle (img, center, radius, CV_RGB (255, 32, 32), 3, 8, 0);
        } else {
          GST_DEBUG_OBJECT (filter, "Buffer is not writable, not drawing "
              "circles for faces");
        }
      }

    }

    if (msg) {
      gst_structure_set_value (msg->structure, "faces", &facelist);
      g_value_unset (&facelist);
      gst_element_post_message (GST_ELEMENT (filter), msg);
    }
  }

  return GST_FLOW_OK;
}


static void
gst_facedetect_load_profile (Gstfacedetect * filter)
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
gst_facedetect_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_facedetect_debug, "facedetect",
      0,
      "Performs face detection on videos and images, providing detected positions via bus messages");

  return gst_element_register (plugin, "facedetect", GST_RANK_NONE,
      GST_TYPE_FACEDETECT);
}
