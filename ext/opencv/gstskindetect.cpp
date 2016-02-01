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
 * SECTION:element-skindetect
 *
 * Human skin detection on videos and images
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! decodebin ! videoconvert ! skindetect ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstskindetect.h"
#include <opencv2/imgproc/imgproc_c.h>

GST_DEBUG_CATEGORY_STATIC (gst_skin_detect_debug);
#define GST_CAT_DEFAULT gst_skin_detect_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_POSTPROCESS,
  PROP_METHOD,
  PROP_MASK
};
typedef enum
{
  HSV,
  RGB
} GstSkindetectMethod;

#define GST_TYPE_SKIN_DETECT_METHOD (gst_skin_detect_method_get_type ())
static GType
gst_skin_detect_method_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {HSV, "Classic HSV thresholding", "hsv"},
      {RGB, "Normalised-RGB colorspace thresholding", "rgb"},
      {0, NULL, NULL},
    };
    etype = g_enum_register_static ("GstSkindetectMethod", values);
  }
  return etype;
}

G_DEFINE_TYPE (GstSkinDetect, gst_skin_detect, GST_TYPE_OPENCV_VIDEO_FILTER);
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB")));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB")));


static void gst_skin_detect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_skin_detect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_skin_detect_transform (GstOpencvVideoFilter * filter,
    GstBuffer * buf, IplImage * img, GstBuffer * outbuf, IplImage * outimg);

static gboolean gst_skin_detect_stop (GstBaseTransform * basesrc);
static gboolean
gst_skin_detect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels);
static void gst_skin_detect_release_all_images (GstSkinDetect * filter);

/* initialize the skindetect's class */
static void
gst_skin_detect_class_init (GstSkinDetectClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *basesrc_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  gobject_class = (GObjectClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gobject_class->set_property = gst_skin_detect_set_property;
  gobject_class->get_property = gst_skin_detect_get_property;

  gstopencvbasefilter_class->cv_trans_func = gst_skin_detect_transform;

  g_object_class_install_property (gobject_class, PROP_POSTPROCESS,
      g_param_spec_boolean ("postprocess", "Postprocess",
          "Apply opening-closing to skin detection to extract large, significant blobs ",
          TRUE, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Method to use",
          "Method to use",
          GST_TYPE_SKIN_DETECT_METHOD, HSV,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "skindetect",
      "Filter/Effect/Video",
      "Performs non-parametric skin detection on input",
      "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  basesrc_class->stop = gst_skin_detect_stop;
  gstopencvbasefilter_class->cv_set_caps = gst_skin_detect_set_caps;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_skin_detect_init (GstSkinDetect * filter)
{
  filter->postprocess = TRUE;
  filter->method = HSV;

  gst_opencv_video_filter_set_in_place (GST_OPENCV_VIDEO_FILTER_CAST (filter),
      FALSE);
}


static void
gst_skin_detect_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSkinDetect *filter = GST_SKIN_DETECT (object);

  switch (prop_id) {
    case PROP_POSTPROCESS:
      filter->postprocess = g_value_get_boolean (value);
      break;
    case PROP_METHOD:
      filter->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_skin_detect_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSkinDetect *filter = GST_SKIN_DETECT (object);

  switch (prop_id) {
    case PROP_POSTPROCESS:
      g_value_set_boolean (value, filter->postprocess);
      break;
    case PROP_METHOD:
      g_value_set_enum (value, filter->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_skin_detect_set_caps (GstOpencvVideoFilter * transform,
    gint in_width, gint in_height, gint in_depth, gint in_channels,
    gint out_width, gint out_height, gint out_depth, gint out_channels)
{
  GstSkinDetect *filter = GST_SKIN_DETECT (transform);
  CvSize size = cvSize (in_width, in_height);

  // If cvRGB is already allocated, it means there's a cap modification,
  // so release first all the images.
  if (NULL != filter->cvRGB)
    gst_skin_detect_release_all_images (filter);


  filter->cvRGB = cvCreateImageHeader (size, IPL_DEPTH_8U, 3);
  filter->cvSkin = cvCreateImageHeader (size, IPL_DEPTH_8U, 3);
  filter->cvChA = cvCreateImage (size, IPL_DEPTH_8U, 1);
  filter->width = in_width;
  filter->height = in_height;

  filter->cvHSV = cvCreateImage (size, IPL_DEPTH_8U, 3);
  filter->cvH = cvCreateImage (size, 8, 1);     /*  Hue component. */
  filter->cvH2 = cvCreateImage (size, 8, 1);    /*  Hue component, 2nd threshold */
  filter->cvS = cvCreateImage (size, 8, 1);     /*  Saturation component. */
  filter->cvV = cvCreateImage (size, 8, 1);     /*  Brightness component. */
  filter->cvSkinPixels1 = cvCreateImage (size, 8, 1);   /*  Greyscale output image */

  filter->cvR = cvCreateImage (size, 8, 1);     /*  R component. */
  filter->cvG = cvCreateImage (size, 8, 1);     /*  G component. */
  filter->cvB = cvCreateImage (size, 8, 1);     /*  B component. */
  filter->cvAll = cvCreateImage (size, IPL_DEPTH_32F, 1);       /*  (R+G+B) component. */
  filter->cvR2 = cvCreateImage (size, IPL_DEPTH_32F, 1);        /*  R component, 32bits */
  filter->cvRp = cvCreateImage (size, IPL_DEPTH_32F, 1);        /*  R' and >0.4 */
  filter->cvGp = cvCreateImage (size, IPL_DEPTH_32F, 1);        /*  G' and > 0.28 */
  filter->cvRp2 = cvCreateImage (size, IPL_DEPTH_32F, 1);       /*  R' <0.6 */
  filter->cvGp2 = cvCreateImage (size, IPL_DEPTH_32F, 1);       /*  G' <0.4 */
  filter->cvSkinPixels2 = cvCreateImage (size, IPL_DEPTH_32F, 1);       /*  Greyscale output image. */
  filter->cvdraft = cvCreateImage (size, IPL_DEPTH_8U, 1);      /*  Greyscale output image. */

  return TRUE;
}

/* Clean up */
static gboolean
gst_skin_detect_stop (GstBaseTransform * basesrc)
{
  GstSkinDetect *filter = GST_SKIN_DETECT (basesrc);

  if (filter->cvRGB != NULL)
    gst_skin_detect_release_all_images (filter);
  return TRUE;
}

static void
gst_skin_detect_release_all_images (GstSkinDetect * filter)
{
  cvReleaseImage (&filter->cvRGB);
  cvReleaseImage (&filter->cvSkin);
  cvReleaseImage (&filter->cvChA);

  cvReleaseImage (&filter->cvHSV);
  cvReleaseImage (&filter->cvH);
  cvReleaseImage (&filter->cvH2);
  cvReleaseImage (&filter->cvS);
  cvReleaseImage (&filter->cvV);
  cvReleaseImage (&filter->cvSkinPixels1);

  cvReleaseImage (&filter->cvR);
  cvReleaseImage (&filter->cvG);
  cvReleaseImage (&filter->cvB);
  cvReleaseImage (&filter->cvAll);
  cvReleaseImage (&filter->cvR2);
  cvReleaseImage (&filter->cvRp);
  cvReleaseImage (&filter->cvGp);
  cvReleaseImage (&filter->cvRp2);
  cvReleaseImage (&filter->cvGp2);
  cvReleaseImage (&filter->cvSkinPixels2);
  cvReleaseImage (&filter->cvdraft);
}

static GstFlowReturn
gst_skin_detect_transform (GstOpencvVideoFilter * base, GstBuffer * buf,
    IplImage * img, GstBuffer * outbuf, IplImage * outimg)
{
  GstSkinDetect *filter = GST_SKIN_DETECT (base);

  filter->cvRGB->imageData = (char *) img->imageData;
  filter->cvSkin->imageData = (char *) outimg->imageData;

  /* SKIN COLOUR BLOB DETECTION */
  if (HSV == filter->method) {
    cvCvtColor (filter->cvRGB, filter->cvHSV, CV_RGB2HSV);
    cvSplit (filter->cvHSV, filter->cvH, filter->cvS, filter->cvV, 0);  /*  Extract the 3 color components. */

    /*  Detect which pixels in each of the H, S and V channels are probably skin pixels.
       Assume that skin has a Hue between 0 to 18 (out of 180), and Saturation above 50, and Brightness above 80. */
    cvThreshold (filter->cvH, filter->cvH2, 10, UCHAR_MAX, CV_THRESH_BINARY);   /* (hue > 10) */
    cvThreshold (filter->cvH, filter->cvH, 20, UCHAR_MAX, CV_THRESH_BINARY_INV);        /* (hue < 20) */
    cvThreshold (filter->cvS, filter->cvS, 48, UCHAR_MAX, CV_THRESH_BINARY);    /* (sat > 48) */
    cvThreshold (filter->cvV, filter->cvV, 80, UCHAR_MAX, CV_THRESH_BINARY);    /* (val > 80) */

    /*  erode the HUE to get rid of noise. */
    cvErode (filter->cvH, filter->cvH, NULL, 1);

    /*  Combine all 3 thresholded color components, so that an output pixel will only
       be white (255) if the H, S and V pixels were also white.
       imageSkin = (hue > 10) ^ (hue < 20) ^ (sat > 48) ^ (val > 80), where   ^ mean pixels-wise AND */
    cvAnd (filter->cvH, filter->cvS, filter->cvSkinPixels1, NULL);
    cvAnd (filter->cvSkinPixels1, filter->cvH2, filter->cvSkinPixels1, NULL);
    cvAnd (filter->cvSkinPixels1, filter->cvV, filter->cvSkinPixels1, NULL);

    cvCvtColor (filter->cvSkinPixels1, filter->cvRGB, CV_GRAY2RGB);
  } else if (RGB == filter->method) {
    cvSplit (filter->cvRGB, filter->cvR, filter->cvG, filter->cvB, 0);  /*  Extract the 3 color components. */
    cvAdd (filter->cvR, filter->cvG, filter->cvAll, NULL);
    cvAdd (filter->cvB, filter->cvAll, filter->cvAll, NULL);    /*  All = R + G + B */
    cvDiv (filter->cvR, filter->cvAll, filter->cvRp, 1.0);      /*  R' = R / ( R + G + B) */
    cvDiv (filter->cvG, filter->cvAll, filter->cvGp, 1.0);      /*  G' = G / ( R + G + B) */

    cvConvertScale (filter->cvR, filter->cvR2, 1.0, 0.0);
    cvCopy (filter->cvGp, filter->cvGp2, NULL);
    cvCopy (filter->cvRp, filter->cvRp2, NULL);

    cvThreshold (filter->cvR2, filter->cvR2, 60, UCHAR_MAX, CV_THRESH_BINARY);  /* (R > 60) */
    cvThreshold (filter->cvRp, filter->cvRp, 0.42, UCHAR_MAX, CV_THRESH_BINARY);        /* (R'> 0.4) */
    cvThreshold (filter->cvRp2, filter->cvRp2, 0.6, UCHAR_MAX, CV_THRESH_BINARY_INV);   /* (R'< 0.6) */
    cvThreshold (filter->cvGp, filter->cvGp, 0.28, UCHAR_MAX, CV_THRESH_BINARY);        /* (G'> 0.28) */
    cvThreshold (filter->cvGp2, filter->cvGp2, 0.4, UCHAR_MAX, CV_THRESH_BINARY_INV);   /* (G'< 0.4) */

    /*  Combine all 3 thresholded color components, so that an output pixel will only
       be white (255) if the H, S and V pixels were also white. */

    cvAnd (filter->cvR2, filter->cvRp, filter->cvSkinPixels2, NULL);
    cvAnd (filter->cvRp, filter->cvSkinPixels2, filter->cvSkinPixels2, NULL);
    cvAnd (filter->cvRp2, filter->cvSkinPixels2, filter->cvSkinPixels2, NULL);
    cvAnd (filter->cvGp, filter->cvSkinPixels2, filter->cvSkinPixels2, NULL);
    cvAnd (filter->cvGp2, filter->cvSkinPixels2, filter->cvSkinPixels2, NULL);

    cvConvertScale (filter->cvSkinPixels2, filter->cvdraft, 1.0, 0.0);
    cvCvtColor (filter->cvdraft, filter->cvRGB, CV_GRAY2RGB);
  }

  /* After this we have a RGB Black and white image with the skin, in
     filter->cvRGB. We can postprocess by applying 1 erode-dilate and 1
     dilate-erode, or alternatively 1 opening-closing all together, with
     the goal of removing small (spurious) skin spots and creating large
     connected areas */
  if (filter->postprocess) {
    cvSplit (filter->cvRGB, filter->cvChA, NULL, NULL, NULL);

    cvErode (filter->cvChA, filter->cvChA,
        cvCreateStructuringElementEx (3, 3, 1, 1, CV_SHAPE_RECT, NULL), 1);
    cvDilate (filter->cvChA, filter->cvChA,
        cvCreateStructuringElementEx (3, 3, 1, 1, CV_SHAPE_RECT, NULL), 2);
    cvErode (filter->cvChA, filter->cvChA,
        cvCreateStructuringElementEx (3, 3, 1, 1, CV_SHAPE_RECT, NULL), 1);

    cvCvtColor (filter->cvChA, filter->cvRGB, CV_GRAY2RGB);
  }

  cvCopy (filter->cvRGB, filter->cvSkin, NULL);

  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_skin_detect_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_skin_detect_debug, "skindetect",
      0, "Performs skin detection on videos and images");

  return gst_element_register (plugin, "skindetect", GST_RANK_NONE,
      GST_TYPE_SKIN_DETECT);
}
