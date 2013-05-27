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
 * SECTION:element-retinex
 *
 * Basic and multiscale retinex for colour image enhancement, see article:
 *
 * Rahman, Zia-ur, Daniel J. Jobson, and Glenn A. Woodell. "Multi-scale retinex for 
 * color image enhancement." Image Processing, 1996. Proceedings., International 
 * Conference on. Vol. 3. IEEE, 1996.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! decodebin ! videoconvert ! retinex ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstretinex.h"

GST_DEBUG_CATEGORY_STATIC (gst_retinex_debug);
#define GST_CAT_DEFAULT gst_retinex_debug

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
  PROP_SCALES
};
typedef enum
{
  METHOD_BASIC,
  METHOD_MULTISCALE
} GstRetinexMethod;

#define DEFAULT_METHOD METHOD_BASIC
#define DEFAULT_SCALES 3

#define GST_TYPE_RETINEX_METHOD (gst_retinex_method_get_type ())
static GType
gst_retinex_method_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {METHOD_BASIC, "Basic retinex restoration", "basic"},
      {METHOD_MULTISCALE, "Mutiscale retinex restoration", "multiscale"},
      {0, NULL, NULL},
    };
    etype = g_enum_register_static ("GstRetinexMethod", values);
  }
  return etype;
}

G_DEFINE_TYPE (GstRetinex, gst_retinex, GST_TYPE_VIDEO_FILTER);
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB")));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB")));


static void gst_retinex_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_retinex_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_retinex_transform_ip (GstBaseTransform * btrans,
    GstBuffer * buf);
static gboolean gst_retinex_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);

static void gst_retinex_release_all_images (GstRetinex * filter);

static gboolean gst_retinex_stop (GstBaseTransform * basesrc);

/* initialize the retinex's class */
static void
gst_retinex_class_init (GstRetinexClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_retinex_set_property;
  gobject_class->get_property = gst_retinex_get_property;

  btrans_class->transform_ip = gst_retinex_transform_ip;
  btrans_class->set_caps = gst_retinex_set_caps;
  btrans_class->stop = gst_retinex_stop;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Retinex method to use",
          "Retinex method to use",
          GST_TYPE_RETINEX_METHOD, DEFAULT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SCALES,
      g_param_spec_int ("scales", "scales",
          "Amount of gaussian filters (scales) used in multiscale retinex", 1,
          4, DEFAULT_SCALES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Retinex image colour enhacement", "Filter/Effect/Video",
      "Multiscale retinex for colour image enhancement",
      "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

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
gst_retinex_init (GstRetinex * filter)
{
  filter->method = DEFAULT_METHOD;
  filter->scales = DEFAULT_SCALES;
  filter->current_scales = 0;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
}


static void
gst_retinex_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRetinex *retinex = GST_RETINEX (object);

  switch (prop_id) {
    case PROP_METHOD:
      retinex->method = g_value_get_enum (value);
      break;
    case PROP_SCALES:
      retinex->scales = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_retinex_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRetinex *filter = GST_RETINEX (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, filter->method);
      break;
    case PROP_SCALES:
      g_value_set_int (value, filter->scales);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_retinex_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRetinex *retinex = GST_RETINEX (btrans);
  CvSize size;
  GstVideoInfo info;
  gst_video_info_from_caps (&info, incaps);

  size = cvSize (info.width, info.height);

  /* If cvRGB is already allocated, it means there's a cap modification,
     so release first all the images.                                      */
  if (NULL != retinex->cvRGBin)
    gst_retinex_release_all_images (retinex);

  retinex->cvRGBin = cvCreateImageHeader (size, IPL_DEPTH_8U, 3);
  retinex->cvRGBout = cvCreateImageHeader (size, IPL_DEPTH_8U, 3);

  retinex->cvA = cvCreateImage (size, IPL_DEPTH_32F, 3);
  retinex->cvB = cvCreateImage (size, IPL_DEPTH_32F, 3);
  retinex->cvC = cvCreateImage (size, IPL_DEPTH_32F, 3);
  retinex->cvD = cvCreateImage (size, IPL_DEPTH_32F, 3);

  return TRUE;
}

/* Clean up */
static gboolean
gst_retinex_stop (GstBaseTransform * basesrc)
{
  GstRetinex *filter = GST_RETINEX (basesrc);

  if (filter->cvRGBin != NULL)
    gst_retinex_release_all_images (filter);

  g_free (filter->weights);
  filter->weights = NULL;
  g_free (filter->sigmas);
  filter->sigmas = NULL;

  return TRUE;
}

static void
gst_retinex_release_all_images (GstRetinex * filter)
{
  cvReleaseImage (&filter->cvRGBin);
  cvReleaseImage (&filter->cvRGBout);

  cvReleaseImage (&filter->cvA);
  cvReleaseImage (&filter->cvB);
  cvReleaseImage (&filter->cvC);
  cvReleaseImage (&filter->cvD);
}

static GstFlowReturn
gst_retinex_transform_ip (GstBaseTransform * btrans, GstBuffer * buf)
{
  GstRetinex *retinex = GST_RETINEX (btrans);
  GstMapInfo info;
  double sigma = 14.0;
  int gain = 128;
  int offset = 128;
  int filter_size;

  if (!gst_buffer_map (buf, &info, GST_MAP_READWRITE)) {
    return GST_FLOW_ERROR;
  }
  retinex->cvRGBin->imageData = (char *) info.data;

  /* Basic retinex restoration.  The image and a filtered image are converted
     to the log domain and subtracted.
     O = Log(I) - Log(H(I))   
     where O is the output, H is a gaussian 2d filter and I is the input image. */
  if (METHOD_BASIC == retinex->method) {
    /*  Compute log image */
    cvConvert (retinex->cvRGBin, retinex->cvA);
    cvLog (retinex->cvA, retinex->cvB);

    /*  Compute log of blured image */
    filter_size = (int) floor (sigma * 6) / 2;
    filter_size = filter_size * 2 + 1;

    cvConvert (retinex->cvRGBin, retinex->cvD);
    cvSmooth (retinex->cvD, retinex->cvD, CV_GAUSSIAN, filter_size, filter_size,
        0.0, 0.0);
    cvLog (retinex->cvD, retinex->cvC);

    /*  Compute difference */
    cvSub (retinex->cvB, retinex->cvC, retinex->cvA, NULL);

    /*  Restore */
    cvConvertScale (retinex->cvA, retinex->cvRGBin, (float) gain,
        (float) offset);
  }
  /* Multiscale retinex restoration.  The image and a set of filtered images are
     converted to the log domain and subtracted from the original with some set
     of weights. Typicaly called with three equally weighted scales of fine,
     medium and wide standard deviations.
     O = Log(I) - sum_i [ wi * Log(H(I)) ]
     where O is the output, H is a gaussian 2d filter and I is the input image
     sum_i means summatory on var i with i in [0..scales) and wi are the weights */
  else if (METHOD_MULTISCALE == retinex->method) {
    /* allocate or reallocate the weights and sigmas according to scales */
    if (retinex->current_scales != retinex->scales || !retinex->sigmas) {
      retinex->weights =
          (double *) g_realloc (retinex->weights,
          sizeof (double) * retinex->scales);
      retinex->sigmas =
          (double *) g_realloc (retinex->sigmas,
          sizeof (double) * retinex->scales);
      for (int i = 0; i < retinex->scales; i++) {
        retinex->weights[i] = 1.0 / (double) retinex->scales;
        retinex->sigmas[i] = 10.0 + 4.0 * (double) retinex->scales;
      }
      retinex->current_scales = retinex->scales;
    }

    /*  Compute log image */
    cvConvert (retinex->cvRGBin, retinex->cvA);
    cvLog (retinex->cvA, retinex->cvB);

    /*  Filter at each scale */
    for (int i = 0; i < retinex->scales; i++) {
      filter_size = (int) floor (retinex->sigmas[i] * 6) / 2;
      filter_size = filter_size * 2 + 1;

      cvConvert (retinex->cvRGBin, retinex->cvD);
      cvSmooth (retinex->cvD, retinex->cvD, CV_GAUSSIAN, filter_size,
          filter_size, 0.0, 0.0);
      cvLog (retinex->cvD, retinex->cvC);

      /*  Compute weighted difference */
      cvScale (retinex->cvC, retinex->cvC, retinex->weights[i], 0.0);
      cvSub (retinex->cvB, retinex->cvC, retinex->cvB, NULL);
    }

    /*  Restore */
    cvConvertScale (retinex->cvB, retinex->cvRGBin, (float) gain,
        (float) offset);
  }

  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_retinex_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_retinex_debug, "retinex",
      0, "Multiscale retinex for colour image enhancement");

  return gst_element_register (plugin, "retinex", GST_RANK_NONE,
      GST_TYPE_RETINEX);
}
