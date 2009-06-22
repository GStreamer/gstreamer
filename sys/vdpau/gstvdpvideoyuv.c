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
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]"));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_video_yuv_debug, "vdpauvideoyuv", 0, "VDPAU VdpSurface to YUV");

GST_BOILERPLATE_FULL (GstVdpVideoYUV, gst_vdp_video_yuv, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static GstCaps *
gst_vdp_video_yuv_get_src_caps (GstVdpVideoYUV * video_yuv, GstCaps * caps)
{
  GstStructure *structure;
  const GValue *value;
  GstVdpDevice *device;
  gint chroma_type;
  gint width, height;
  gboolean got_fps;
  gint fps_n, fps_d;
  gboolean got_par;
  gint par_n, par_d;
  GstCaps *src_caps;
  gint i;

  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "device");
  device = g_value_get_object (value);

  gst_structure_get_int (structure, "chroma-type", &chroma_type);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  got_fps = gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);

  got_par = gst_structure_get_fraction (structure, "pixel-aspect-ratio",
      &par_n, &par_d);

  src_caps = gst_caps_new_empty ();

  for (i = 0; i < N_FORMATS; i++) {
    VdpStatus status;
    VdpBool is_supported;

    if (formats[i].chroma_type != chroma_type)
      continue;

    status =
        device->vdp_video_surface_query_ycbcr_capabilities (device->device,
        chroma_type, formats[i].format, &is_supported);
    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_Y_CB_CR_FORMAT) {
      GST_ELEMENT_ERROR (video_yuv, RESOURCE, READ,
          ("Could not query VDPAU YCbCr capabilites"),
          ("Error returned from vdpau was: %s",
              device->vdp_get_error_string (status)));

      goto done;
    }
    if (is_supported) {
      GstCaps *format_caps;

      format_caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, formats[i].fourcc,
          "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

      if (got_fps)
        gst_caps_set_simple (format_caps,
            "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);

      if (got_par)
        gst_caps_set_simple (format_caps,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);

      gst_caps_append (src_caps, format_caps);
    }
  }

done:
  if (gst_caps_is_empty (src_caps)) {
    gst_caps_unref (src_caps);
    return NULL;
  }

  return src_caps;
}

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

      GST_LOG_OBJECT (video_yuv, "Entering buffer_alloc");

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
      GST_WARNING ("WTF!!!");
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
  GstVdpVideoYUV *video_yuv = GST_VDP_VIDEO_YUV (trans);
  GstCaps *new_caps = NULL;

  if (direction == GST_PAD_SINK) {
    new_caps = gst_vdp_video_yuv_get_src_caps (video_yuv, caps);

  } else if (direction == GST_PAD_SRC) {
    /* FIXME: upstream negotiation */
    new_caps = gst_static_pad_template_get_caps (&sink_template);
  }

  return new_caps;
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
