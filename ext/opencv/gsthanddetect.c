/*
 * GStreamer hand gesture detection plugins
 * Copyright (C) 2012 Andol Li <<andol@andol.info>>
 * Copyright (C) 2013 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
 * SECTION:video-filter-handdetect
 *
 * FIXME:operates hand gesture detection in video streams and images,
 * and enable media operation e.g. play/stop/fast forward/back rewind.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 autovideosrc ! videoconvert ! "video/x-raw, formt=RGB, width=320, height=240" ! \
 * videoscale ! handdetect ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* element header */
#include "gsthanddetect.h"
#include "gstopencvutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_handdetect_debug);
#define GST_CAT_DEFAULT gst_handdetect_debug

/* define HAAR files */
#define HAAR_FILE_FIST GST_HAAR_CASCADES_DIR G_DIR_SEPARATOR_S "fist.xml"
#define HAAR_FILE_PALM GST_HAAR_CASCADES_DIR G_DIR_SEPARATOR_S "palm.xml"

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
  PROP_PROFILE_FIST,
  PROP_PROFILE_PALM,
  PROP_ROI_X,
  PROP_ROI_Y,
  PROP_ROI_WIDTH,
  PROP_ROI_HEIGHT
};

/* the capabilities of the inputs and outputs */
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

static void gst_handdetect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_handdetect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_handdetect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);
static GstFlowReturn gst_handdetect_transform_ip (GstOpencvVideoFilter *
    transform, GstBuffer * buffer, IplImage * img);

static void gst_handdetect_load_profile (GstHanddetect * filter);

static void gst_handdetect_navigation_interface_init (GstNavigationInterface *
    iface);
static void gst_handdetect_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure);

G_DEFINE_TYPE_WITH_CODE (GstHanddetect, gst_handdetect,
    GST_TYPE_OPENCV_VIDEO_FILTER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_handdetect_navigation_interface_init););

static void
gst_handdetect_navigation_interface_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_handdetect_navigation_send_event;
}

/* FIXME: this function used to parse the region of interests coordinates
 * sending from applications when the hand gestures reach the defined regions of interests,
 * at this moment this function is not doing anything significantly
 * but will be CHANGED when the gstreamer is patched with new hand gesture events
 */
static void
gst_handdetect_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstHanddetect *filter = GST_HANDDETECT (navigation);
  GstPad *peer;

  if ((peer = gst_pad_get_peer (GST_BASE_TRANSFORM_CAST (filter)->sinkpad))) {
    GstEvent *event;
    event = gst_event_new_navigation (structure);
    gst_pad_send_event (peer, event);
    gst_object_unref (peer);
  }
}

/* clean opencv images and parameters */
static void
gst_handdetect_finalize (GObject * obj)
{
  GstHanddetect *filter = GST_HANDDETECT (obj);

  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);
  if (filter->cvStorage)
    cvReleaseMemStorage (&filter->cvStorage);
  if (filter->cvStorage_palm)
    cvReleaseMemStorage (&filter->cvStorage_palm);
  g_free (filter->profile_fist);
  g_free (filter->profile_palm);

  G_OBJECT_CLASS (gst_handdetect_parent_class)->finalize (obj);
}

/* initialise the HANDDETECT class */
static void
gst_handdetect_class_init (GstHanddetectClass * klass)
{
  GObjectClass *gobject_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gstopencvbasefilter_class->cv_trans_ip_func = gst_handdetect_transform_ip;
  gstopencvbasefilter_class->cv_set_caps = gst_handdetect_set_caps;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_handdetect_finalize);
  gobject_class->set_property = gst_handdetect_set_property;
  gobject_class->get_property = gst_handdetect_get_property;

  g_object_class_install_property (gobject_class,
      PROP_DISPLAY,
      g_param_spec_boolean ("display",
          "Display",
          "Whether the detected hands are highlighted in output frame",
          TRUE, G_PARAM_READWRITE)
      );
  g_object_class_install_property (gobject_class,
      PROP_PROFILE_FIST,
      g_param_spec_string ("profile_fist",
          "Profile_fist",
          "Location of HAAR cascade file (fist gesture)",
          HAAR_FILE_FIST, G_PARAM_READWRITE)
      );
  g_object_class_install_property (gobject_class,
      PROP_PROFILE_PALM,
      g_param_spec_string ("profile_palm",
          "Profile_palm",
          "Location of HAAR cascade file (palm gesture)",
          HAAR_FILE_PALM, G_PARAM_READWRITE)
      );
  /* FIXME: property name needs fixing */
  g_object_class_install_property (gobject_class,
      PROP_ROI_X,
      g_param_spec_uint ("ROI_X",
          "ROI_X",
          "X of left-top pointer in region of interest \nGestures in the defined region of interest will emit messages",
          0, UINT_MAX, 0, G_PARAM_READWRITE)
      );
  /* FIXME: property name needs fixing */
  g_object_class_install_property (gobject_class,
      PROP_ROI_Y,
      g_param_spec_uint ("ROI_Y",
          "ROI_Y",
          "Y of left-top pointer in region of interest \nGestures in the defined region of interest will emit messages",
          0, UINT_MAX, 0, G_PARAM_READWRITE)
      );
  /* FIXME: property name needs fixing */
  g_object_class_install_property (gobject_class,
      PROP_ROI_WIDTH,
      g_param_spec_uint ("ROI_WIDTH",
          "ROI_WIDTH",
          "WIDTH of left-top pointer in region of interest \nGestures in the defined region of interest will emit messages",
          0, UINT_MAX, 0, G_PARAM_READWRITE)
      );
  /* FIXME: property name needs fixing */
  g_object_class_install_property (gobject_class,
      PROP_ROI_HEIGHT,
      g_param_spec_uint ("ROI_HEIGHT",
          "ROI_HEIGHT",
          "HEIGHT of left-top pointer in region of interest \nGestures in the defined region of interest will emit messages",
          0, UINT_MAX, 0, G_PARAM_READWRITE)
      );

  gst_element_class_set_static_metadata (element_class,
      "handdetect",
      "Filter/Effect/Video",
      "Performs hand gesture detection on videos, providing detected hand positions via bus message and navigation event, and deals with hand gesture events",
      "Andol Li <andol@andol.info>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

}

/* initialise the new element
 * instantiate pads and add them to element
 * set pad call-back functions
 * initialise instance structure
 */
static void
gst_handdetect_init (GstHanddetect * filter)
{
  filter->profile_fist = g_strdup (HAAR_FILE_FIST);
  filter->profile_palm = g_strdup (HAAR_FILE_PALM);
  filter->roi_x = 0;
  filter->roi_y = 0;
  filter->roi_width = 0;
  filter->roi_height = 0;
  filter->display = TRUE;

  gst_handdetect_load_profile (filter);

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      TRUE);
}

static void
gst_handdetect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHanddetect *filter = GST_HANDDETECT (object);

  switch (prop_id) {
    case PROP_PROFILE_FIST:
      g_free (filter->profile_fist);
      filter->profile_fist = g_value_dup_string (value);
      gst_handdetect_load_profile (filter);
      break;
    case PROP_PROFILE_PALM:
      g_free (filter->profile_palm);
      filter->profile_palm = g_value_dup_string (value);
      gst_handdetect_load_profile (filter);
      break;
    case PROP_DISPLAY:
      filter->display = g_value_get_boolean (value);
      break;
    case PROP_ROI_X:
      filter->roi_x = g_value_get_uint (value);
      break;
    case PROP_ROI_Y:
      filter->roi_y = g_value_get_uint (value);
      break;
    case PROP_ROI_WIDTH:
      filter->roi_width = g_value_get_uint (value);
      break;
    case PROP_ROI_HEIGHT:
      filter->roi_height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_handdetect_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstHanddetect *filter = GST_HANDDETECT (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_boolean (value, filter->display);
      break;
    case PROP_PROFILE_FIST:
      g_value_set_string (value, filter->profile_fist);
      break;
    case PROP_PROFILE_PALM:
      g_value_set_string (value, filter->profile_palm);
      break;
    case PROP_ROI_X:
      g_value_set_uint (value, filter->roi_x);
      break;
    case PROP_ROI_Y:
      g_value_set_uint (value, filter->roi_y);
      break;
    case PROP_ROI_WIDTH:
      g_value_set_uint (value, filter->roi_width);
      break;
    case PROP_ROI_HEIGHT:
      g_value_set_uint (value, filter->roi_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_handdetect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels)
{
  GstHanddetect *filter;
  filter = GST_HANDDETECT (transform);

  /* 320 x 240 is with the best detect accuracy, if not, give info */
  if (in_width != 320 || in_height != 240)
    GST_WARNING_OBJECT (filter,
        "resize to 320 x 240 to have best detect accuracy.\n");

  if (filter->cvGray)
    cvReleaseImage (&filter->cvGray);
  filter->cvGray =
      cvCreateImage (cvSize (in_width, in_height), IPL_DEPTH_8U, 1);

  if (!filter->cvStorage)
    filter->cvStorage = cvCreateMemStorage (0);
  else
    cvClearMemStorage (filter->cvStorage);
  if (!filter->cvStorage_palm)
    filter->cvStorage_palm = cvCreateMemStorage (0);
  else
    cvClearMemStorage (filter->cvStorage_palm);
  return TRUE;
}

/* Hand detection function
 * This function does the actual processing 'of hand detect and display'
 */
static GstFlowReturn
gst_handdetect_transform_ip (GstOpencvVideoFilter * transform,
    GstBuffer * buffer, IplImage * img)
{
  GstHanddetect *filter = GST_HANDDETECT (transform);
  CvSeq *hands;
  CvRect *r;
  GstStructure *s;
  GstMessage *m;
  int i;

  /* check detection cascades */
  if (!filter->cvCascade_fist || !filter->cvCascade_palm)
    return GST_FLOW_OK;

  /* cvt to gray colour space for hand detect */
  cvCvtColor (img, filter->cvGray, CV_RGB2GRAY);
  cvClearMemStorage (filter->cvStorage);

  /* detect FIST gesture fist */
  hands =
      cvHaarDetectObjects (filter->cvGray, filter->cvCascade_fist,
      filter->cvStorage, 1.1, 2, CV_HAAR_DO_CANNY_PRUNING, cvSize (24, 24)
#if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
      , cvSize (0, 0)
#endif
      );

  /* if FIST gesture detected */
  if (hands && hands->total > 0) {
    int min_distance, distance;
    CvRect temp_r;
    CvPoint c;

    GST_DEBUG_OBJECT (filter, "%d FIST gestures detected\n",
        (int) hands->total);

    /* Go through all detected FIST gestures to get the best one
     * prev_r => previous hand
     * best_r => best hand in this frame
     */
    /* set min_distance for init comparison */
    min_distance = img->width + img->height;
    /* Init filter->prev_r */
    temp_r = cvRect (0, 0, 0, 0);
    if (filter->prev_r == NULL)
      filter->prev_r = &temp_r;
    /* Get the best FIST gesture */
    for (i = 0; i < (hands ? hands->total : 0); i++) {
      r = (CvRect *) cvGetSeqElem (hands, i);
      distance = (int) sqrt (pow ((r->x - filter->prev_r->x),
              2) + pow ((r->y - filter->prev_r->y), 2));
      if (distance <= min_distance) {
        min_distance = distance;
        filter->best_r = r;
      }
    }
    /* Save best_r as prev_r for next frame comparison */
    filter->prev_r = (CvRect *) filter->best_r;
    /* send msg to app/bus if the detected gesture falls in the region of interest */
    /* get center point of gesture */
    c = cvPoint (filter->best_r->x + filter->best_r->width / 2,
        filter->best_r->y + filter->best_r->height / 2);
    /* send message:
     * if the center point is in the region of interest, OR,
     * if the region of interest remains default as (0,0,0,0)*/
    if ((c.x >= filter->roi_x && c.x <= (filter->roi_x + filter->roi_width)
            && c.y >= filter->roi_y
            && c.y <= (filter->roi_y + filter->roi_height))
        || (filter->roi_x == 0
            && filter->roi_y == 0
            && filter->roi_width == 0 && filter->roi_height == 0)) {
      /* Define structure for message post */
      s = gst_structure_new ("hand-gesture",
          "gesture", G_TYPE_STRING, "fist",
          "x", G_TYPE_UINT,
          (guint) (filter->best_r->x + filter->best_r->width * 0.5), "y",
          G_TYPE_UINT,
          (guint) (filter->best_r->y + filter->best_r->height * 0.5), "width",
          G_TYPE_UINT, (guint) filter->best_r->width, "height", G_TYPE_UINT,
          (guint) filter->best_r->height, NULL);
      /* Init message element */
      m = gst_message_new_element (GST_OBJECT (filter), s);
      /* Send message */
      gst_element_post_message (GST_ELEMENT (filter), m);

#if 0
      /* send event
       * here we use mouse-move event instead of fist-move or palm-move event
       * !!! this will CHANGE in the future !!!
       * !!! by adding gst_navigation_send_hand_detect_event() in navigation.c !!!
       */
      gst_navigation_send_mouse_event (GST_NAVIGATION (filter),
          "mouse-move",
          0,
          (double) (filter->best_r->x + filter->best_r->width * 0.5),
          (double) (filter->best_r->y + filter->best_r->height * 0.5));
#endif
    }
    /* Check filter->display,
     * If TRUE, displaying red circle marker in the out frame */
    if (filter->display) {
      CvPoint center;
      int radius;
      center.x = cvRound ((filter->best_r->x + filter->best_r->width * 0.5));
      center.y = cvRound ((filter->best_r->y + filter->best_r->height * 0.5));
      radius =
          cvRound ((filter->best_r->width + filter->best_r->height) * 0.25);
      cvCircle (img, center, radius, CV_RGB (0, 0, 200), 1, 8, 0);
    }
  } else {
    /* if NO FIST gesture, detecting PALM gesture */
    hands =
        cvHaarDetectObjects (filter->cvGray, filter->cvCascade_palm,
        filter->cvStorage, 1.1, 3, CV_HAAR_DO_CANNY_PRUNING, cvSize (24, 24)
#if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
        , cvSize (0, 0)
#endif
        );
    /* if PALM detected */
    if (hands && hands->total > 0) {
      int min_distance, distance;
      CvRect temp_r;
      CvPoint c;
      /* set frame buffer writable */
      if (filter->display) {
        buffer = gst_buffer_make_writable (buffer);
        GST_DEBUG_OBJECT (filter, "%d PALM gestures detected\n",
            (int) hands->total);
      }
      /* Go through all detected PALM gestures to get the best one
       * prev_r => previous hand
       * best_r => best hand in this frame
       */
      /* suppose a min_distance for init comparison */
      min_distance = img->width + img->height;
      /* Init filter->prev_r */
      temp_r = cvRect (0, 0, 0, 0);
      if (filter->prev_r == NULL)
        filter->prev_r = &temp_r;
      /* Get the best PALM gesture */
      for (i = 0; i < (hands ? hands->total : 0); i++) {
        r = (CvRect *) cvGetSeqElem (hands, i);
        distance = (int) sqrt (pow ((r->x - filter->prev_r->x),
                2) + pow ((r->y - filter->prev_r->y), 2));
        if (distance <= min_distance) {
          min_distance = distance;
          filter->best_r = r;
        }
      }
      /* Save best_r as prev_r for next frame comparison */
      filter->prev_r = (CvRect *) filter->best_r;

      /* send msg to app/bus if the detected gesture falls in the region of interest */
      /* get center point of gesture */
      c = cvPoint (filter->best_r->x + filter->best_r->width / 2,
          filter->best_r->y + filter->best_r->height / 2);
      /* send message:
       * if the center point is in the region of interest, OR,
       * if the region of interest remains default as (0,0,0,0)*/
      if ((c.x >= filter->roi_x && c.x <= (filter->roi_x + filter->roi_width)
              && c.y >= filter->roi_y
              && c.y <= (filter->roi_y + filter->roi_height))
          || (filter->roi_x == 0
              && filter->roi_y == 0
              && filter->roi_width == 0 && filter->roi_height == 0)) {
        /* Define structure for message post */
        s = gst_structure_new ("hand-gesture",
            "gesture", G_TYPE_STRING, "palm",
            "x", G_TYPE_UINT,
            (guint) (filter->best_r->x + filter->best_r->width * 0.5), "y",
            G_TYPE_UINT,
            (guint) (filter->best_r->y + filter->best_r->height * 0.5), "width",
            G_TYPE_UINT, (guint) filter->best_r->width, "height", G_TYPE_UINT,
            (guint) filter->best_r->height, NULL);
        /* Init message element */
        m = gst_message_new_element (GST_OBJECT (filter), s);
        /* Send message */
        gst_element_post_message (GST_ELEMENT (filter), m);

#if 0
        /* send event
         * here we use mouse-move event instead of fist-move or palm-move event
         * !!! this will CHANGE in the future !!!
         * !!! by adding gst_navigation_send_hand_detect_event() in navigation.c !!!
         */
        gst_navigation_send_mouse_event (GST_NAVIGATION (filter),
            "mouse-move",
            0,
            (double) (filter->best_r->x + filter->best_r->width * 0.5),
            (double) (filter->best_r->y + filter->best_r->height * 0.5));

        /* or use another way to send upstream navigation event for debug
         *
         * GstEvent *event =
         * gst_event_new_navigation (gst_structure_new
         * ("application/x-gst-navigation", "event", G_TYPE_STRING,
         * "mouse-move",
         * "button", G_TYPE_INT, 0,
         * "pointer_x", G_TYPE_DOUBLE,
         * (double) (filter->best_r->x + filter->best_r->width * 0.5),
         * "pointer_y", G_TYPE_DOUBLE,
         * (double) (filter->best_r->y + filter->best_r->height * 0.5),
         * NULL));
         * gst_pad_send_event (GST_BASE_TRANSFORM_CAST (filter)->srcpad, event);
         */
#endif
      }
      /* Check filter->display,
       * If TRUE, displaying red circle marker in the out frame */
      if (filter->display) {
        CvPoint center;
        int radius;
        center.x = cvRound ((filter->best_r->x + filter->best_r->width * 0.5));
        center.y = cvRound ((filter->best_r->y + filter->best_r->height * 0.5));
        radius =
            cvRound ((filter->best_r->width + filter->best_r->height) * 0.25);
        cvCircle (img, center, radius, CV_RGB (0, 0, 200), 1, 8, 0);
      }
    }
  }

  /* Push out the incoming buffer */
  return GST_FLOW_OK;
}

static void
gst_handdetect_load_profile (GstHanddetect * filter)
{
  GST_DEBUG_OBJECT (filter, "Loading profiles...\n");
  filter->cvCascade_fist =
      (CvHaarClassifierCascade *) cvLoad (filter->profile_fist, 0, 0, 0);
  filter->cvCascade_palm =
      (CvHaarClassifierCascade *) cvLoad (filter->profile_palm, 0, 0, 0);
  if (!filter->cvCascade_fist || !filter->cvCascade_palm)
    GST_WARNING_OBJECT (filter,
        "WARNING: Could not load HAAR classifier cascade: %s.\n",
        filter->profile_fist);
  else
    GST_DEBUG_OBJECT (filter, "Loaded profile %s\n", filter->profile_fist);
  if (!filter->cvCascade_palm)
    GST_WARNING_OBJECT (filter,
        "WARNING: Could not load HAAR classifier cascade: %s.\n",
        filter->profile_palm);
  else
    GST_DEBUG_OBJECT (filter, "Loaded profile %s\n", filter->profile_palm);
}

/* Entry point to initialize the plug-in
 * Initialize the plug-in itself
 * Register the element factories and other features
 */
gboolean
gst_handdetect_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_handdetect_debug,
      "handdetect", 0, "opencv hand gesture detection");
  return gst_element_register (plugin, "handdetect", GST_RANK_NONE,
      GST_TYPE_HANDDETECT);
}
