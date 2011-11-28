/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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
 * SECTION:element-gstvideofilter2
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "gstvideofilter2.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_filter2_debug_category);
#define GST_CAT_DEFAULT gst_video_filter2_debug_category

/* prototypes */


static void gst_video_filter2_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_video_filter2_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_video_filter2_dispose (GObject * object);
static void gst_video_filter2_finalize (GObject * object);

static GstCaps *gst_video_filter2_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean
gst_video_filter2_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size);
static gboolean
gst_video_filter2_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_video_filter2_start (GstBaseTransform * trans);
static gboolean gst_video_filter2_stop (GstBaseTransform * trans);
static GstFlowReturn gst_video_filter2_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_video_filter2_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

enum
{
  PROP_0
};


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_video_filter2_debug_category, "videofilter2", 0, \
      "debug category for videofilter2 element");

GST_BOILERPLATE_FULL (GstVideoFilter2, gst_video_filter2, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_video_filter2_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  int i;
  GstCaps *caps = NULL;
  GstPadTemplate *pad_template;

  caps = gst_caps_new_empty ();
  for (i = GST_VIDEO_FORMAT_I420; i <= GST_VIDEO_FORMAT_I420; i++) {
    gst_caps_append (caps, gst_video_format_new_template_caps (i));
  }

  pad_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_ref (caps));
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
  pad_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, pad_template);
  gst_object_unref (pad_template);
}

static void
gst_video_filter2_class_init (GstVideoFilter2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_video_filter2_set_property;
  gobject_class->get_property = gst_video_filter2_get_property;
  gobject_class->dispose = gst_video_filter2_dispose;
  gobject_class->finalize = gst_video_filter2_finalize;
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_filter2_transform_caps);
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_filter2_get_unit_size);
  base_transform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_video_filter2_set_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_video_filter2_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_video_filter2_stop);
  base_transform_class->transform =
      GST_DEBUG_FUNCPTR (gst_video_filter2_transform);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_video_filter2_transform_ip);

}

static void
gst_video_filter2_init (GstVideoFilter2 * videofilter2,
    GstVideoFilter2Class * videofilter2_class)
{

  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (videofilter2), TRUE);
}

void
gst_video_filter2_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_VIDEO_FILTER2 (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_video_filter2_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_VIDEO_FILTER2 (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_video_filter2_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_VIDEO_FILTER2 (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_video_filter2_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_VIDEO_FILTER2 (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_video_filter2_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  return gst_caps_ref (caps);
}

static gboolean
gst_video_filter2_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstVideoFormat format;
  gint width, height;
  gboolean ret;

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  *size = gst_video_format_get_size (format, width, height);

  return ret;
}

static gboolean
gst_video_filter2_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoFilter2 *videofilter2;
  gboolean ret;
  int width;
  int height;
  GstVideoFormat format;

  g_return_val_if_fail (GST_IS_VIDEO_FILTER2 (trans), FALSE);
  videofilter2 = GST_VIDEO_FILTER2 (trans);

  ret = gst_video_format_parse_caps (incaps, &format, &width, &height);

  if (ret) {
    videofilter2->format = format;
    videofilter2->width = width;
    videofilter2->height = height;
  }

  return ret;
}

static gboolean
gst_video_filter2_start (GstBaseTransform * trans)
{

  return FALSE;
}

static gboolean
gst_video_filter2_stop (GstBaseTransform * trans)
{

  return FALSE;
}

static GstFlowReturn
gst_video_filter2_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_video_filter2_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoFilter2 *video_filter2 = GST_VIDEO_FILTER2 (trans);
  GstVideoFilter2Class *klass =
      GST_VIDEO_FILTER2_CLASS (G_OBJECT_GET_CLASS (trans));
  int i;
  GstFlowReturn ret;

  for (i = 0; klass->functions[i].format != GST_VIDEO_FORMAT_UNKNOWN; i++) {
    if (klass->functions[i].format == video_filter2->format) {
      ret = klass->functions[i].filter_ip (video_filter2, buf, 0,
          video_filter2->height);
      return ret;
    }
  }

  return GST_FLOW_ERROR;
}

/* API */

void
gst_video_filter2_class_add_functions (GstVideoFilter2Class * klass,
    const GstVideoFilter2Functions * functions)
{
  klass->functions = functions;
}
