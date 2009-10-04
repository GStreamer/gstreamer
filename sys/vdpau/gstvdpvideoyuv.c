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
  PROP_0,
  PROP_DISPLAY
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
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("NV12") ";"
        GST_VIDEO_CAPS_YUV ("UYVY") ";" GST_VIDEO_CAPS_YUV ("YUY2")));

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
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    {
      VdpStatus status;
      guint8 *data[1];
      guint32 stride[1];

      data[0] = GST_BUFFER_DATA (outbuf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_UYVY,
          0, video_yuv->width);

      GST_LOG_OBJECT (video_yuv, "Entering vdp_video_surface_get_bits_ycbcr");
      status =
          device->vdp_video_surface_get_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_UYVY, (void *) data, stride);
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
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    {
      VdpStatus status;
      guint8 *data[1];
      guint32 stride[1];

      data[0] = GST_BUFFER_DATA (outbuf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YUY2,
          0, video_yuv->width);

      GST_LOG_OBJECT (video_yuv, "Entering vdp_video_surface_get_bits_ycbcr");
      status =
          device->vdp_video_surface_get_bits_ycbcr (surface,
          VDP_YCBCR_FORMAT_YUYV, (void *) data, stride);
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

  gst_buffer_copy_metadata (outbuf, inbuf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

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
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
      {
        *othersize =
            gst_video_format_get_size (GST_VIDEO_FORMAT_UYVY, video_yuv->width,
            video_yuv->height);
        break;
      }
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      {
        *othersize =
            gst_video_format_get_size (GST_VIDEO_FORMAT_YUY2, video_yuv->width,
            video_yuv->height);
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
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);
  GstCaps *result = NULL;

  if (direction == GST_PAD_SINK)
    result = gst_vdp_video_to_yuv_caps (caps, video_yuv->device);

  else if (direction == GST_PAD_SRC)
    result = gst_vdp_yuv_to_video_caps (caps, video_yuv->device);

  GST_LOG ("transformed %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, caps, result);

  return result;
}

static gboolean
gst_vdp_video_yuv_start (GstBaseTransform * trans)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);

  video_yuv->device = gst_vdp_get_device (video_yuv->display);
  if (!video_yuv->device)
    return FALSE;

  return TRUE;
}

static gboolean
gst_vdp_video_yuv_stop (GstBaseTransform * trans)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);

  g_object_unref (video_yuv->device);

  return TRUE;
}

static GstFlowReturn
gst_vdp_video_yuv_buffer_alloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstStructure *structure;
  gint width, height;
  gint chroma_type;

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    goto error;

  if (!gst_structure_get_int (structure, "width", &width))
    goto error;
  if (!gst_structure_get_int (structure, "height", &height))
    goto error;

  if (!gst_structure_get_int (structure, "chroma-type", &chroma_type))
    goto error;

  *buf = GST_BUFFER (gst_vdp_video_buffer_new (video_yuv->device,
          chroma_type, width, height));

  if (*buf == NULL)
    goto error;

  GST_BUFFER_SIZE (*buf) = size;
  GST_BUFFER_OFFSET (*buf) = offset;

  gst_buffer_set_caps (*buf, caps);

  ret = GST_FLOW_OK;

error:
  gst_object_unref (video_yuv);
  return ret;
}

/* GObject vmethod implementations */

static void
gst_vdp_video_yuv_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VdpauVideoYUV",
      "Filter/Converter/Decoder/Video",
      "VDPAU video surface to YUV",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_vdp_video_yuv_finalize (GObject * object)
{
  GstVdpVideoYUV *video_yuv = (GstVdpVideoYUV *) object;

  g_free (video_yuv->display);
}

static void
gst_vdp_video_yuv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (video_yuv->display);
      video_yuv->display = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_video_yuv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, video_yuv->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_video_yuv_class_init (GstVdpVideoYUVClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_vdp_video_yuv_finalize;
  gobject_class->set_property = gst_vdp_video_yuv_set_property;
  gobject_class->get_property = gst_vdp_video_yuv_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  trans_class->start = gst_vdp_video_yuv_start;
  trans_class->stop = gst_vdp_video_yuv_stop;
  trans_class->transform_caps = gst_vdp_video_yuv_transform_caps;
  trans_class->transform_size = gst_vdp_video_transform_size;
  trans_class->transform = gst_vdp_video_yuv_transform;
  trans_class->set_caps = gst_vdp_video_yuv_set_caps;
}

static void
gst_vdp_video_yuv_init (GstVdpVideoYUV * video_yuv, GstVdpVideoYUVClass * klass)
{
  video_yuv->display = NULL;

  gst_pad_set_bufferalloc_function (GST_BASE_TRANSFORM_SINK_PAD (video_yuv),
      gst_vdp_video_yuv_buffer_alloc);
}
