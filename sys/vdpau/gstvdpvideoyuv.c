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

#include "gstvdpvideobuffer.h"
#include "gstvdputils.h"
#include "gstvdpvideoyuv.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_video_yuv_debug);
#define GST_CAT_DEFAULT gst_vdp_video_yuv_debug

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
    GST_STATIC_CAPS (GST_VDP_VIDEO_CAPS));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE (GST_BASE_TRANSFORM_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("NV12")));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_video_yuv_debug, "vdpauvideoyuv", 0, "VDPAU VdpSurface to YUV");

GST_BOILERPLATE_FULL (GstVdpVideoYUV, gst_vdp_video_yuv, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

GstFlowReturn
gst_vdp_video_yuv_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);
  GstVdpDevice *device;
  VdpVideoSurface surface;

  device = GST_VDP_VIDEO_BUFFER (inbuf)->device;
  surface = GST_VDP_VIDEO_BUFFER (inbuf)->surface;

  switch (video_yuv->format) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
    {
      VdpStatus status;
      guint8 *data[3];
      guint32 stride[3];

      data[0] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          0, video_yuv->width, video_yuv->height);
      data[1] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          2, video_yuv->width, video_yuv->height);
      data[2] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          1, video_yuv->width, video_yuv->height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          0, video_yuv->width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          2, video_yuv->width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          1, video_yuv->width);

      GST_LOG_OBJECT (video_yuv, "Entering vdp_video_surface_get_bits_ycbcr");
      status =
          device->vdp_video_surface_get_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YV12, (void *) data, stride);
      GST_LOG_OBJECT (video_yuv,
          "Got status %d from vdp_video_surface_get_bits_ycbcr", status);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (video_yuv, RESOURCE, READ,
            ("Couldn't get data from vdpau"),
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

      data[0] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          0, video_yuv->width, video_yuv->height);
      data[1] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          2, video_yuv->width, video_yuv->height);
      data[2] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          1, video_yuv->width, video_yuv->height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          0, video_yuv->width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          2, video_yuv->width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          1, video_yuv->width);

      GST_LOG_OBJECT (video_yuv, "Entering vdp_video_surface_get_bits_ycbcr");
      status =
          device->vdp_video_surface_get_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YV12, (void *) data, stride);
      GST_LOG_OBJECT (video_yuv,
          "Got status %d from vdp_video_surface_get_bits_ycbcr", status);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (video_yuv, RESOURCE, READ,
            ("Couldn't get data from vdpau"),
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

      data[0] = GST_BUFFER_DATA (outbuf);
      data[1] = GST_BUFFER_DATA (outbuf) + video_yuv->width * video_yuv->height;

      stride[0] = video_yuv->width;
      stride[1] = video_yuv->width;

      GST_LOG_OBJECT (video_yuv, "Entering vdp_video_surface_get_bits_ycbcr");
      status =
          device->vdp_video_surface_get_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_NV12, (void *) data, stride);
      GST_LOG_OBJECT (video_yuv,
          "Got status %d from vdp_video_surface_get_bits_ycbcr", status);
      if (G_UNLIKELY (status != VDP_STATUS_OK)) {
        GST_ELEMENT_ERROR (video_yuv, RESOURCE, READ,
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

  gst_buffer_copy_metadata (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);
  GST_LOG_OBJECT (video_yuv, "Pushing buffer with ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

  return GST_FLOW_OK;
}

gboolean
gst_vdp_video_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, guint size,
    GstCaps * othercaps, guint * othersize)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);

  if (direction == GST_PAD_SINK) {
    switch (video_yuv->format) {
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
      {
        *othersize =
            gst_video_format_get_size (GST_VIDEO_FORMAT_YV12, video_yuv->width,
            video_yuv->height);
        break;
      }
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      {
        *othersize =
            gst_video_format_get_size (GST_VIDEO_FORMAT_YV12, video_yuv->width,
            video_yuv->height);
        break;
      }
      case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
      {
        *othersize =
            video_yuv->width * video_yuv->height +
            video_yuv->width * video_yuv->height / 2;
        break;
      }
      default:
        return FALSE;
    }
  } else
    *othersize = size;

  return TRUE;
}

gboolean
gst_vdp_video_yuv_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);
  GstStructure *structure;

  structure = gst_caps_get_structure (incaps, 0);

  if (!gst_structure_get_int (structure, "width", &video_yuv->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &video_yuv->height))
    return FALSE;

  structure = gst_caps_get_structure (outcaps, 0);
  if (!gst_structure_get_fourcc (structure, "format", &video_yuv->format))
    return FALSE;

  return TRUE;
}

GstCaps *
gst_vdp_video_yuv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *result;

  if (direction == GST_PAD_SINK)
    result = gst_vdp_video_to_yuv_caps (caps);

  else if (direction == GST_PAD_SRC)
    result = gst_vdp_yuv_to_video_caps (caps, NULL);

  GST_LOG ("transformed %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, caps, result);

  return result;
}

/* GObject vmethod implementations */

static void
gst_vdp_video_yuv_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VdpauVideoYUV",
      "Covideo_yuv/Decoder/Video",
      "VDPAU video surface to YUV",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_vdp_video_yuv_class_init (GstVdpVideoYUVClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *transform_class;

  gobject_class = (GObjectClass *) klass;
  transform_class = (GstBaseTransformClass *) klass;

  transform_class->transform_caps = gst_vdp_video_yuv_transform_caps;
  transform_class->transform_size = gst_vdp_video_transform_size;
  transform_class->transform = gst_vdp_video_yuv_transform;
  transform_class->set_caps = gst_vdp_video_yuv_set_caps;
}

static void
gst_vdp_video_yuv_init (GstVdpVideoYUV * video_yuv, GstVdpVideoYUVClass * klass)
{
}
