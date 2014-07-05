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

/* TODO opencv can do scaling for some cases */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstopencvvideofilter.h"
#include "gstopencvutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_opencv_video_filter_debug);
#define GST_CAT_DEFAULT gst_opencv_video_filter_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

static GstElementClass *parent_class = NULL;

static void gst_opencv_video_filter_class_init (GstOpencvVideoFilterClass *
    klass);
static void gst_opencv_video_filter_init (GstOpencvVideoFilter * trans,
    GstOpencvVideoFilterClass * klass);

static gboolean gst_opencv_video_filter_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_opencv_video_filter_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static GstFlowReturn gst_opencv_video_filter_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);

static void gst_opencv_video_filter_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_opencv_video_filter_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GType
gst_opencv_video_filter_get_type (void)
{
  static volatile gsize opencv_base_transform_type = 0;

  if (g_once_init_enter (&opencv_base_transform_type)) {
    GType _type;
    static const GTypeInfo opencv_base_transform_info = {
      sizeof (GstOpencvVideoFilterClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_opencv_video_filter_class_init,
      NULL,
      NULL,
      sizeof (GstOpencvVideoFilter),
      0,
      (GInstanceInitFunc) gst_opencv_video_filter_init,
    };

    _type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstOpencvVideoFilter", &opencv_base_transform_info,
        G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&opencv_base_transform_type, _type);
  }
  return opencv_base_transform_type;
}

/* Clean up */
static void
gst_opencv_video_filter_finalize (GObject * obj)
{
  GstOpencvVideoFilter *transform = GST_OPENCV_VIDEO_FILTER (obj);

  if (transform->cvImage)
    cvReleaseImage (&transform->cvImage);
  if (transform->out_cvImage)
    cvReleaseImage (&transform->out_cvImage);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_opencv_video_filter_class_init (GstOpencvVideoFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *basetrans_class;

  gobject_class = (GObjectClass *) klass;
  basetrans_class = (GstBaseTransformClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (gst_opencv_video_filter_debug,
      "opencvbasetransform", 0, "opencvbasetransform element");

  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_opencv_video_filter_finalize);
  gobject_class->set_property = gst_opencv_video_filter_set_property;
  gobject_class->get_property = gst_opencv_video_filter_get_property;

  basetrans_class->transform = gst_opencv_video_filter_transform;
  basetrans_class->transform_ip = gst_opencv_video_filter_transform_ip;
  basetrans_class->set_caps = gst_opencv_video_filter_set_caps;
}

static void
gst_opencv_video_filter_init (GstOpencvVideoFilter * transform,
    GstOpencvVideoFilterClass * klass)
{
}

static GstFlowReturn
gst_opencv_video_filter_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstOpencvVideoFilter *transform;
  GstOpencvVideoFilterClass *fclass;
  GstMapInfo in_info;
  GstMapInfo out_info;
  GstFlowReturn ret;

  transform = GST_OPENCV_VIDEO_FILTER (trans);
  fclass = GST_OPENCV_VIDEO_FILTER_GET_CLASS (transform);

  g_return_val_if_fail (fclass->cv_trans_func != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (transform->cvImage != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (transform->out_cvImage != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_map (inbuf, &in_info, GST_MAP_READ))
    goto inbuf_map_failed;

  if (!gst_buffer_map (outbuf, &out_info, GST_MAP_WRITE))
    goto outbuf_map_failed;

  transform->cvImage->imageData = (char *) in_info.data;
  transform->out_cvImage->imageData = (char *) out_info.data;

  ret = fclass->cv_trans_func (transform, inbuf, transform->cvImage, outbuf,
      transform->out_cvImage);

  gst_buffer_unmap (inbuf, &in_info);
  gst_buffer_unmap (outbuf, &out_info);

  return ret;

inbuf_map_failed:
  GST_ELEMENT_ERROR (transform, RESOURCE, READ,
      ("Failed to map buffer for reading"), (NULL));
  return GST_FLOW_ERROR;

outbuf_map_failed:
  GST_ELEMENT_ERROR (transform, RESOURCE, WRITE,
      ("Failed to map buffer for writing"), (NULL));
  gst_buffer_unmap (inbuf, &in_info);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_opencv_video_filter_transform_ip (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  GstOpencvVideoFilter *transform;
  GstOpencvVideoFilterClass *fclass;
  GstMapInfo info;
  GstFlowReturn ret;

  transform = GST_OPENCV_VIDEO_FILTER (trans);
  fclass = GST_OPENCV_VIDEO_FILTER_GET_CLASS (transform);

  g_return_val_if_fail (fclass->cv_trans_ip_func != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (transform->cvImage != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_map (buffer, &info, GST_MAP_READWRITE))
    goto map_failed;

  transform->cvImage->imageData = (char *) info.data;

  ret = fclass->cv_trans_ip_func (transform, buffer, transform->cvImage);

  gst_buffer_unmap (buffer, &info);

  return ret;

map_failed:
  GST_ELEMENT_ERROR (transform, RESOURCE, WRITE,
      ("Failed to map buffer for reading and writing"), (NULL));
  return GST_FLOW_ERROR;
}

static gboolean
gst_opencv_video_filter_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstOpencvVideoFilter *transform = GST_OPENCV_VIDEO_FILTER (trans);
  GstOpencvVideoFilterClass *klass =
      GST_OPENCV_VIDEO_FILTER_GET_CLASS (transform);
  gint in_width, in_height;
  gint in_depth, in_channels;
  gint out_width, out_height;
  gint out_depth, out_channels;
  GError *in_err = NULL;
  GError *out_err = NULL;

  if (!gst_opencv_parse_iplimage_params_from_caps (incaps, &in_width,
          &in_height, &in_depth, &in_channels, &in_err)) {
    GST_WARNING_OBJECT (transform, "Failed to parse input caps: %s",
        in_err->message);
    g_error_free (in_err);
    return FALSE;
  }

  if (!gst_opencv_parse_iplimage_params_from_caps (outcaps, &out_width,
          &out_height, &out_depth, &out_channels, &out_err)) {
    GST_WARNING_OBJECT (transform, "Failed to parse output caps: %s",
        out_err->message);
    g_error_free (out_err);
    return FALSE;
  }

  if (klass->cv_set_caps) {
    if (!klass->cv_set_caps (transform, in_width, in_height, in_depth,
            in_channels, out_width, out_height, out_depth, out_channels))
      return FALSE;
  }

  if (transform->cvImage) {
    cvReleaseImage (&transform->cvImage);
  }
  if (transform->out_cvImage) {
    cvReleaseImage (&transform->out_cvImage);
  }

  transform->cvImage =
      cvCreateImageHeader (cvSize (in_width, in_height), in_depth, in_channels);
  transform->out_cvImage =
      cvCreateImageHeader (cvSize (out_width, out_height), out_depth,
      out_channels);

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (transform),
      transform->in_place);
  return TRUE;
}

static void
gst_opencv_video_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opencv_video_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_opencv_video_filter_set_in_place (GstOpencvVideoFilter * transform,
    gboolean ip)
{
  transform->in_place = ip;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (transform), ip);
}
