/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstzebrastripe
 *
 * The zebrastripe element marks areas of images in a video stream
 * that are brighter than a threshold with a diagonal zebra stripe
 * pattern.  Typically, this is used to aid in adjusting the exposure
 * setting on the camera.  Setting the threshold to 95 or 100 will
 * show areas that are completely overexposed and clipping.  A
 * threshold setting of 70 is often used to properly adjust skin
 * tones.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! zebrastripe ! xvimagesink
 * ]|
 * Marks overexposed areas of the video with zebra stripes.
 *
 * The threshold property is expressed as percentage of full scale,
 * whereas common usage expresses thresholds in terms of IRE.  The
 * property setting can be calculated from IRE by using the formula
 * percent = (IRE * 1.075) - 7.5.  Note that 100 IRE corresponds to
 * 100 %, and 70 IRE corresponds to 68 %.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <math.h>
#include "gstzebrastripe.h"

GST_DEBUG_CATEGORY_STATIC (gst_zebra_stripe_debug_category);
#define GST_CAT_DEFAULT gst_zebra_stripe_debug_category

/* prototypes */


static void gst_zebra_stripe_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_zebra_stripe_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_zebra_stripe_dispose (GObject * object);
static void gst_zebra_stripe_finalize (GObject * object);

static gboolean
gst_zebra_stripe_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size);
static gboolean
gst_zebra_stripe_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_zebra_stripe_start (GstBaseTransform * trans);
static gboolean gst_zebra_stripe_stop (GstBaseTransform * trans);
static GstFlowReturn gst_zebra_stripe_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

enum
{
  PROP_0,
  PROP_THRESHOLD
};

#define DEFAULT_THRESHOLD 90

/* pad templates */

static GstStaticPadTemplate gst_zebra_stripe_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{I420,YV12,Y41B,Y42B,NV12,NV21,YUV9,YVU9,Y444,UYVY,YVYU,YUY2,AYUV}"))
    );

static GstStaticPadTemplate gst_zebra_stripe_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{I420,YV12,Y41B,Y42B,NV12,NV21,YUV9,YVU9,Y444,UYVY,YVYU,YUY2,AYUV}"))
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_zebra_stripe_debug_category, "zebrastripe", 0, \
      "debug category for zebrastripe element");

GST_BOILERPLATE_FULL (GstZebraStripe, gst_zebra_stripe, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_zebra_stripe_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_zebra_stripe_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_zebra_stripe_src_template));

  gst_element_class_set_details_simple (element_class, "Zebra stripe overlay",
      "Filter/Analysis",
      "Overlays zebra striping on overexposed areas of video",
      "David Schleef <ds@entropywave.com>");
}

static void
gst_zebra_stripe_class_init (GstZebraStripeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_zebra_stripe_set_property;
  gobject_class->get_property = gst_zebra_stripe_get_property;
  gobject_class->dispose = gst_zebra_stripe_dispose;
  gobject_class->finalize = gst_zebra_stripe_finalize;
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_zebra_stripe_get_unit_size);
  base_transform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_zebra_stripe_set_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_zebra_stripe_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_zebra_stripe_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_zebra_stripe_transform_ip);

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_int ("threshold", "Threshold",
          "Threshold above which the video is striped", 0, 100,
          DEFAULT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

}

static void
gst_zebra_stripe_init (GstZebraStripe * zebrastripe,
    GstZebraStripeClass * zebrastripe_class)
{

  zebrastripe->sinkpad =
      gst_pad_new_from_static_template (&gst_zebra_stripe_sink_template,
      "sink");

  zebrastripe->srcpad =
      gst_pad_new_from_static_template (&gst_zebra_stripe_src_template, "src");
}

void
gst_zebra_stripe_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstZebraStripe *zebrastripe;

  g_return_if_fail (GST_IS_ZEBRA_STRIPE (object));
  zebrastripe = GST_ZEBRA_STRIPE (object);

  switch (property_id) {
    case PROP_THRESHOLD:
      zebrastripe->threshold = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_zebra_stripe_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstZebraStripe *zebrastripe;

  g_return_if_fail (GST_IS_ZEBRA_STRIPE (object));
  zebrastripe = GST_ZEBRA_STRIPE (object);

  switch (property_id) {
    case PROP_THRESHOLD:
      g_value_set_int (value, zebrastripe->threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_zebra_stripe_dispose (GObject * object)
{
  GstZebraStripe *zebrastripe;

  g_return_if_fail (GST_IS_ZEBRA_STRIPE (object));
  zebrastripe = GST_ZEBRA_STRIPE (object);

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_zebra_stripe_finalize (GObject * object)
{
  GstZebraStripe *zebrastripe;

  g_return_if_fail (GST_IS_ZEBRA_STRIPE (object));
  zebrastripe = GST_ZEBRA_STRIPE (object);

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_zebra_stripe_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  int width, height;
  GstVideoFormat format;
  gboolean ret;

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  *size = gst_video_format_get_size (format, width, height);

  return ret;
}

static gboolean
gst_zebra_stripe_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstZebraStripe *zebrastripe = GST_ZEBRA_STRIPE (trans);
  int width, height;
  GstVideoFormat format;
  gboolean ret;

  ret = gst_video_format_parse_caps (incaps, &format, &width, &height);
  if (ret) {
    zebrastripe->format = format;
    zebrastripe->width = width;
    zebrastripe->height = height;
  }

  return ret;
}

static gboolean
gst_zebra_stripe_start (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_zebra_stripe_stop (GstBaseTransform * trans)
{

  return TRUE;
}

static GstFlowReturn
gst_zebra_stripe_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstZebraStripe *zebrastripe = GST_ZEBRA_STRIPE (trans);
  int i, j;
  int threshold;
  int t;
  guint8 *ydata;
  int ystride;

  threshold = 16 + floor (0.5 + 2.19 * zebrastripe->threshold);
  t = zebrastripe->t;

  ydata = GST_BUFFER_DATA (buf);
  ystride = gst_video_format_get_row_stride (zebrastripe->format,
      0, zebrastripe->width);
  switch (zebrastripe->format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
    case GST_VIDEO_FORMAT_Y444:
      for (j = 0; j < zebrastripe->height; j++) {
        guint8 *data = ydata + ystride * j;
        for (i = 0; i < zebrastripe->width; i++) {
          if (data[i] >= threshold) {
            if ((i + j + t) & 0x4)
              data[i] = 16;
          }
        }
      }
      break;
    case GST_VIDEO_FORMAT_UYVY:
      ydata++;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
      for (j = 0; j < zebrastripe->height; j++) {
        guint8 *data = ydata + ystride * j;
        for (i = 0; i < zebrastripe->width; i++) {
          if (data[2 * i] >= threshold) {
            if ((i + j + t) & 0x4)
              data[2 * i] = 16;
          }
        }
      }
      break;
    case GST_VIDEO_FORMAT_AYUV:
      ydata++;
      for (j = 0; j < zebrastripe->height; j++) {
        guint8 *data = ydata + ystride * j;
        for (i = 0; i < zebrastripe->width; i++) {
          if (data[4 * i] >= threshold) {
            if ((i + j + t) & 0x4)
              data[4 * i] = 16;
          }
        }
      }
      break;
    default:
      g_assert_not_reached ();
  }

  zebrastripe->t++;

  return GST_FLOW_OK;
}
