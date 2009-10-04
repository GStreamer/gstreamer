/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/vdpau/gstvdpvideobuffer.h>

#include "gstvdputils.h"
#include "gstvdpyuvvideo.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_yuv_video_debug);
#define GST_CAT_DEFAULT gst_vdp_yuv_video_debug

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

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE (GST_BASE_TRANSFORM_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("NV12") ";"
        GST_VIDEO_CAPS_YUV ("UYVY") ";" GST_VIDEO_CAPS_YUV ("YUY2")));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_TRANSFORM_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VDP_VIDEO_CAPS));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_yuv_video_debug, "vdpauyuvvideo", 0, "YUV to VDPAU video surface");

GST_BOILERPLATE_FULL (GstVdpYUVVideo, gst_vdp_yuv_video, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static gboolean
gst_vdp_yuv_video_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, guint size,
    GstCaps * othercaps, guint * othersize)
{
  *othersize = size;

  return TRUE;
}

static GstFlowReturn
gst_vdp_yuv_video_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);
  GstVdpDevice *device;
  VdpVideoSurface surface;

  device = GST_VDP_VIDEO_BUFFER (outbuf)->device;
  if (!yuv_video->device)
    yuv_video->device = g_object_ref (device);

  surface = GST_VDP_VIDEO_BUFFER (outbuf)->surface;

  switch (yuv_video->format) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
    {
      VdpStatus status;
      guint8 *data[3];
      guint32 stride[3];

      data[0] = GST_BUFFER_DATA (inbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          0, yuv_video->width, yuv_video->height);
      data[1] = GST_BUFFER_DATA (inbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          2, yuv_video->width, yuv_video->height);
      data[2] = GST_BUFFER_DATA (inbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          1, yuv_video->width, yuv_video->height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          0, yuv_video->width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          2, yuv_video->width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          1, yuv_video->width);

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YV12, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't push YV12 data to VDPAU"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        return GST_FLOW_ERROR;
      }
      break;
    }
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    {
      VdpStatus status;
      guint8 *data[3];
      guint32 stride[3];

      data[0] = GST_BUFFER_DATA (inbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          0, yuv_video->width, yuv_video->height);
      data[1] = GST_BUFFER_DATA (inbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          2, yuv_video->width, yuv_video->height);
      data[2] = GST_BUFFER_DATA (inbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          1, yuv_video->width, yuv_video->height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          0, yuv_video->width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          2, yuv_video->width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          1, yuv_video->width);

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YV12, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't push YV12 data to VDPAU"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        return GST_FLOW_ERROR;
      }
      break;
    }
    case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
    {
      VdpStatus status;
      guint8 *data[2];
      guint32 stride[2];

      data[0] = GST_BUFFER_DATA (inbuf);
      data[1] = GST_BUFFER_DATA (inbuf) + yuv_video->width * yuv_video->height;

      stride[0] = yuv_video->width;
      stride[1] = yuv_video->width;

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_NV12, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't get data from vdpau"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        return GST_FLOW_ERROR;
      }
      break;
    }
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    {
      VdpStatus status;
      guint8 *data[1];
      guint32 stride[1];

      data[0] = GST_BUFFER_DATA (inbuf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_UYVY,
          0, yuv_video->width);

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_UYVY, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't get data from vdpau"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        return GST_FLOW_ERROR;
      }
      break;
    }
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    {
      VdpStatus status;
      guint8 *data[1];
      guint32 stride[1];

      data[0] = GST_BUFFER_DATA (inbuf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YUY2,
          0, yuv_video->width);

      status =
          device->vdp_video_surface_put_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YUYV, (void *) data, stride);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
            ("Couldn't get data from vdpau"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));
        return GST_FLOW_ERROR;
      }
      break;
    }
    default:
      break;
  }

  gst_buffer_copy_metadata (outbuf, inbuf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

  return GST_FLOW_OK;
}

static gboolean
gst_vdp_yuv_video_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);
  GstStructure *structure;

  structure = gst_caps_get_structure (incaps, 0);

  if (!gst_structure_get_int (structure, "width", &yuv_video->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &yuv_video->height))
    return FALSE;
  if (!gst_structure_get_fourcc (structure, "format", &yuv_video->format))
    return FALSE;

  return TRUE;
}

static GstCaps *
gst_vdp_yuv_video_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);
  GstCaps *result = NULL;

  if (direction == GST_PAD_SINK) {
    result = gst_vdp_yuv_to_video_caps (caps, yuv_video->device);
  } else if (direction == GST_PAD_SRC) {
    result = gst_vdp_video_to_yuv_caps (caps, yuv_video->device);
  }

  GST_LOG ("transformed %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, caps, result);

  return result;
}

static gboolean
gst_vdp_yuv_video_start (GstBaseTransform * trans)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);

  yuv_video->device = NULL;

  return TRUE;
}

static gboolean
gst_vdp_yuv_video_stop (GstBaseTransform * trans)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);

  if (yuv_video->device)
    g_object_unref (yuv_video->device);

  return TRUE;
}

/* GObject vmethod implementations */

static void
gst_vdp_yuv_video_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VdpauYUVVideo",
      "Filter/Converter/Decoder/Video",
      "VDPAU video surface to YUV",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_vdp_yuv_video_class_init (GstVdpYUVVideoClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  trans_class->start = gst_vdp_yuv_video_start;
  trans_class->stop = gst_vdp_yuv_video_stop;
  trans_class->transform_caps = gst_vdp_yuv_video_transform_caps;
  trans_class->transform_size = gst_vdp_yuv_video_transform_size;
  trans_class->set_caps = gst_vdp_yuv_video_set_caps;
  trans_class->transform = gst_vdp_yuv_video_transform;
}

static void
gst_vdp_yuv_video_init (GstVdpYUVVideo * yuv_video, GstVdpYUVVideoClass * klass)
{
}
