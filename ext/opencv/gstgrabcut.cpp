/*
 * GStreamer
 * Copyright (C) 2013 Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>
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
 * SECTION:element-grabcut
 *
 *
 * This element is a wrapper around OpenCV grabcut implementation. GrabCut is an
 * image segmentation method based on graph cuts technique. It can be seen as a
 * way of fine-grain segmenting the image from some FG and BG "seed" areas. The
 * OpenCV implementation follows the article [1].
 * The "seed" areas are taken in this element from either an input bounding box
 * coming from a face detection, or from alpha channel values. The input box is
 * taken from a "face" event such as the one generated from the 'facedetect'
 * element. The Alpha channel values should be one of the following (cv.hpp):
 * enum{
 *  GC_BGD    = 0,  //!< background
 *  GC_FGD    = 1,  //!< foreground
 *  GC_PR_BGD = 2,  //!< most probably background
 *  GC_PR_FGD = 3   //!< most probably foreground
 * };
 * with values over GC_PR_FGD interpreted as GC_PR_FGD. IN CASE OF no alpha mask
 * input (all 0's or all 1's), the 'GstOpenCvFaceDetect-face' downstream event
 * is used to create a bbox of PR_FG elements. If both foreground alpha
 * is not specified and there is no face detection, nothing is done.
 *
 * [1] C. Rother, V. Kolmogorov, and A. Blake, "GrabCut: Interactive foreground
 * extraction using iterated graph cuts, ACM Trans. Graph., vol. 23, pp. 309–314,
 * 2004.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 --gst-debug=grabcut=4  v4l2src device=/dev/video0 ! videoconvert ! grabcut ! videoconvert ! video/x-raw,width=320,height=240 ! ximagesink
 * ]|
 * Another example launch line
 * |[
 * gst-launch-1.0 --gst-debug=grabcut=4  v4l2src device=/dev/video0 ! videoconvert ! facedetect display=0 ! videoconvert ! grabcut test-mode=true ! videoconvert ! video/x-raw,width=320,height=240 ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgrabcut.h"
extern "C"
{
#include <opencv2/imgproc/imgproc_c.h>
}
#include <opencv2/imgproc/imgproc.hpp>
GST_DEBUG_CATEGORY_STATIC (gst_grabcut_debug);
#define GST_CAT_DEFAULT gst_grabcut_debug

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
  PROP_TEST_MODE,
  PROP_SCALE
};

#define DEFAULT_TEST_MODE FALSE
#define DEFAULT_SCALE 1.6

G_DEFINE_TYPE (GstGrabcut, gst_grabcut, GST_TYPE_OPENCV_VIDEO_FILTER);
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGBA")));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGBA")));


static void gst_grabcut_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_grabcut_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_grabcut_transform_ip (GstOpencvVideoFilter * filter,
    GstBuffer * buf, IplImage * img);
static gboolean gst_grabcut_set_caps (GstOpencvVideoFilter * filter,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);

static void gst_grabcut_release_all_pointers (GstGrabcut * filter);

static gboolean gst_grabcut_stop (GstBaseTransform * basesrc);
static void compose_matrix_from_image (CvMat * output, IplImage * input);

static int initialise_grabcut (struct grabcut_params *GC, IplImage * image_c,
    CvMat * mask_c);
static int run_grabcut_iteration (struct grabcut_params *GC,
    IplImage * image_c, CvMat * mask_c, CvRect * bbox);
static int run_grabcut_iteration2 (struct grabcut_params *GC,
    IplImage * image_c, CvMat * mask_c, CvRect * bbox);
static int finalise_grabcut (struct grabcut_params *GC);

/* initialize the grabcut's class */
static void
gst_grabcut_class_init (GstGrabcutClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOpencvVideoFilterClass *cvbasefilter_class =
      (GstOpencvVideoFilterClass *) klass;
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_grabcut_set_property;
  gobject_class->get_property = gst_grabcut_get_property;

  btrans_class->stop = gst_grabcut_stop;
  btrans_class->passthrough_on_same_caps = TRUE;

  cvbasefilter_class->cv_trans_ip_func = gst_grabcut_transform_ip;
  cvbasefilter_class->cv_set_caps = gst_grabcut_set_caps;

  g_object_class_install_property (gobject_class, PROP_TEST_MODE,
      g_param_spec_boolean ("test-mode", "test-mode",
          "If true, the output RGB is overwritten with the segmented foreground. Alpha channel same as normal case ",
          DEFAULT_TEST_MODE, (GParamFlags)
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SCALE,
      g_param_spec_float ("scale", "scale",
          "Grow factor for the face bounding box, if present", 1.0,
          4.0, DEFAULT_SCALE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "Grabcut-based image FG/BG segmentation", "Filter/Effect/Video",
      "Runs Grabcut algorithm on input alpha. Values: BG=0, FG=1, PR_BG=2, PR_FGD=3; \
NOTE: larger values of alpha (notably 255) are interpreted as PR_FGD too. \n\
IN CASE OF no alpha mask input (all 0's or all 1's), the 'face' \
downstream event is used to create a bbox of PR_FG elements.\n\
IF nothing is present, then nothing is done.", "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_grabcut_init (GstGrabcut * filter)
{
  filter->test_mode = DEFAULT_TEST_MODE;
  filter->scale = DEFAULT_SCALE;
  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER (filter),
      TRUE);
}


static void
gst_grabcut_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGrabcut *grabcut = GST_GRABCUT (object);

  switch (prop_id) {
    case PROP_TEST_MODE:
      grabcut->test_mode = g_value_get_boolean (value);
      break;
    case PROP_SCALE:
      grabcut->scale = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_grabcut_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGrabcut *filter = GST_GRABCUT (object);

  switch (prop_id) {
    case PROP_TEST_MODE:
      g_value_set_boolean (value, filter->test_mode);
      break;
    case PROP_SCALE:
      g_value_set_float (value, filter->scale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_grabcut_set_caps (GstOpencvVideoFilter * filter, gint in_width,
    gint in_height, gint in_depth, gint in_channels, gint out_width,
    gint out_height, gint out_depth, gint out_channels)
{
  GstGrabcut *grabcut = GST_GRABCUT (filter);
  CvSize size;

  size = cvSize (in_width, in_height);

  /* If cvRGB is already allocated, it means there's a cap modification,
   * so release first all the images. */
  if (!grabcut->cvRGBin)
    gst_grabcut_release_all_pointers (grabcut);

  grabcut->cvRGBin = cvCreateImage (size, IPL_DEPTH_8U, 3);

  grabcut->cvA = cvCreateImage (size, IPL_DEPTH_8U, 1);
  grabcut->cvB = cvCreateImage (size, IPL_DEPTH_8U, 1);
  grabcut->cvC = cvCreateImage (size, IPL_DEPTH_8U, 1);
  grabcut->cvD = cvCreateImage (size, IPL_DEPTH_8U, 1);

  grabcut->grabcut_mask = cvCreateMat (size.height, size.width, CV_8UC1);
  cvZero (grabcut->grabcut_mask);
  initialise_grabcut (&(grabcut->GC), grabcut->cvRGBin, grabcut->grabcut_mask);

  return TRUE;
}

/* Clean up */
static gboolean
gst_grabcut_stop (GstBaseTransform * basesrc)
{
  GstGrabcut *filter = GST_GRABCUT (basesrc);

  if (filter->cvRGBin != NULL)
    gst_grabcut_release_all_pointers (filter);

  return TRUE;
}

static void
gst_grabcut_release_all_pointers (GstGrabcut * filter)
{
  cvReleaseImage (&filter->cvRGBin);

  cvReleaseImage (&filter->cvA);
  cvReleaseImage (&filter->cvB);
  cvReleaseImage (&filter->cvC);
  cvReleaseImage (&filter->cvD);

  finalise_grabcut (&(filter->GC));
}

static GstFlowReturn
gst_grabcut_transform_ip (GstOpencvVideoFilter * filter, GstBuffer * buffer,
        IplImage * img)
{
  GstGrabcut *gc = GST_GRABCUT (filter);
  gint alphapixels;

  GstVideoRegionOfInterestMeta *meta;
  meta = gst_buffer_get_video_region_of_interest_meta (buffer);
  if (meta) {
    gc->facepos.x = (meta->x) - ((gc->scale - 1) * meta->w / 2);
    gc->facepos.y = (meta->y) - ((gc->scale - 1) * meta->h / 2);
    gc->facepos.width = meta->w * gc->scale * 0.9;
    gc->facepos.height = meta->h * gc->scale * 1.1;
  } else {
    memset (&(gc->facepos), 0, sizeof (gc->facepos));
  }

  /*  normally input should be RGBA */
  cvSplit (img, gc->cvA, gc->cvB, gc->cvC, gc->cvD);
  cvCvtColor (img, gc->cvRGBin, CV_BGRA2BGR);
  compose_matrix_from_image (gc->grabcut_mask, gc->cvD);

  /*  Pass cvD to grabcut_mask for the graphcut stuff but that only if
     really there is something in the mask! otherwise -->input bbox is
     what we use */
  alphapixels = cvCountNonZero (gc->cvD);
  if ((0 < alphapixels) && (alphapixels < (gc->width * gc->height))) {
    GST_INFO ("running on mask");
    run_grabcut_iteration (&(gc->GC), gc->cvRGBin, gc->grabcut_mask, NULL);
  } else {

    if ((abs (gc->facepos.width) > 2) && (abs (gc->facepos.height) > 2)) {
      GST_INFO ("running on bbox (%d,%d),(%d,%d)", gc->facepos.x, gc->facepos.y,
          gc->facepos.width, gc->facepos.height);
      run_grabcut_iteration2 (&(gc->GC), gc->cvRGBin, gc->grabcut_mask,
          &(gc->facepos));
    } else {
      GST_WARNING ("No face info present, skipping frame.");
      return GST_FLOW_OK;
    }
  }

  /*  if we want to display, just overwrite the output */
  if (gc->test_mode) {
    /*  get only FG, PR_FG */
    cvAndS (gc->grabcut_mask, cvRealScalar (1), gc->grabcut_mask, NULL);
    /*  (saturated) FG, PR_FG --> 255 */
    cvConvertScale (gc->grabcut_mask, gc->grabcut_mask, 255.0, 0.0);

    cvAnd (gc->grabcut_mask, gc->cvA, gc->cvA, NULL);
    cvAnd (gc->grabcut_mask, gc->cvB, gc->cvB, NULL);
    cvAnd (gc->grabcut_mask, gc->cvC, gc->cvC, NULL);
  }

  cvMerge (gc->cvA, gc->cvB, gc->cvC, gc->cvD, img);

  if (gc->test_mode) {
    cvRectangle (img,
        cvPoint (gc->facepos.x, gc->facepos.y),
        cvPoint (gc->facepos.x + gc->facepos.width,
            gc->facepos.y + gc->facepos.height), CV_RGB (255, 0, 255), 1, 8, 0);
  }

  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_grabcut_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_grabcut_debug, "grabcut",
      0,
      "Grabcut image segmentation on either input alpha or input bounding box");

  return gst_element_register (plugin, "grabcut", GST_RANK_NONE,
      GST_TYPE_GRABCUT);
}

void
compose_matrix_from_image (CvMat * output, IplImage * input)
{

  int x, y;
  for (x = 0; x < output->cols; x++) {
    for (y = 0; y < output->rows; y++) {
      CV_MAT_ELEM (*output, uchar, y, x) =
          (cvGetReal2D (input, y, x) <= GC_PR_FGD) ? cvGetReal2D (input, y,
          x) : GC_PR_FGD;
    }
  }
}


int
initialise_grabcut (struct grabcut_params *GC, IplImage * image_c,
    CvMat * mask_c)
{
  GC->image = (void *) new Mat (cvarrToMat (image_c, false));    /*  "true" refers to copydata */
  GC->mask = (void *) new Mat (cvarrToMat (mask_c, false));
  GC->bgdModel = (void *) new Mat ();       /*  "true" refers to copydata */
  GC->fgdModel = (void *) new Mat ();

  return (0);
}

int
run_grabcut_iteration (struct grabcut_params *GC, IplImage * image_c,
    CvMat * mask_c, CvRect * bbox)
{
  ((Mat *) GC->image)->data = (uchar *) image_c->imageData;
  ((Mat *) GC->mask)->data = mask_c->data.ptr;

  if (cvCountNonZero (mask_c))
    grabCut (*((Mat *) GC->image), *((Mat *) GC->mask), Rect (),
        *((Mat *) GC->bgdModel), *((Mat *) GC->fgdModel), 1,
        GC_INIT_WITH_MASK);

  return (0);
}

int
run_grabcut_iteration2 (struct grabcut_params *GC, IplImage * image_c,
    CvMat * mask_c, CvRect * bbox)
{
  ((Mat *) GC->image)->data = (uchar *) image_c->imageData;
  ((Mat *) GC->mask)->data = mask_c->data.ptr;
  grabCut (*((Mat *) GC->image), *((Mat *) GC->mask), *(bbox),
      *((Mat *) GC->bgdModel), *((Mat *) GC->fgdModel), 1,
      GC_INIT_WITH_RECT);

  return (0);
}

int
finalise_grabcut (struct grabcut_params *GC)
{
  delete ((Mat *) GC->image);
  delete ((Mat *) GC->mask);
  delete ((Mat *) GC->bgdModel);
  delete ((Mat *) GC->fgdModel);

  return (0);
}
