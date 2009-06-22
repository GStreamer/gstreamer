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
  PROP_0,
  PROP_DISPLAY
};

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE (GST_BASE_TRANSFORM_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("NV12")));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_TRANSFORM_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VDP_VIDEO_CAPS));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_yuv_video_debug, "vdpauyuvvideo", 0, "YUV to VDPAU video surface");

GST_BOILERPLATE_FULL (GstVdpYUVVideo, gst_vdp_yuv_video, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static GstCaps *
gst_vdp_yuv_video_get_sink_caps (GstVdpYUVVideo * yuv_video)
{
  GstVdpDevice *device;
  GstCaps *caps;
  gint i;

  device = yuv_video->device;

  caps = gst_caps_new_empty ();
  for (i = 0; i < N_CHROMA_TYPES; i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint32 max_w, max_h;

    status =
        device->vdp_video_surface_query_capabilities (device->device,
        chroma_types[i], &is_supported, &max_w, &max_h);

    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_CHROMA_TYPE) {
      GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
          ("Could not get query VDPAU video surface capabilites"),
          ("Error returned from vdpau was: %s",
              device->vdp_get_error_string (status)));

      goto error;
    }
    if (is_supported) {
      gint j;

      for (j = 0; j < N_FORMATS; j++) {
        if (formats[j].chroma_type != chroma_types[i])
          continue;

        status =
            device->vdp_video_surface_query_ycbcr_capabilities (device->device,
            formats[j].chroma_type, formats[j].format, &is_supported);
        if (status != VDP_STATUS_OK
            && status != VDP_STATUS_INVALID_Y_CB_CR_FORMAT) {
          GST_ELEMENT_ERROR (yuv_video, RESOURCE, READ,
              ("Could not query VDPAU YCbCr capabilites"),
              ("Error returned from vdpau was: %s",
                  device->vdp_get_error_string (status)));

          goto error;
        }

        if (is_supported) {
          GstCaps *format_caps;

          format_caps = gst_caps_new_simple ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, formats[j].fourcc,
              "width", GST_TYPE_INT_RANGE, 1, max_w,
              "height", GST_TYPE_INT_RANGE, 1, max_h,
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
          gst_caps_append (caps, format_caps);
        }
      }
    }
  }

error:
  if (gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    return NULL;
  }

  return caps;
}

static GstFlowReturn
gst_vdp_yuv_video_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);

  *buf = GST_BUFFER (gst_vdp_video_buffer_new (yuv_video->device,
          yuv_video->chroma_type, yuv_video->width, yuv_video->height));

  if (*buf == NULL)
    return GST_FLOW_ERROR;

  gst_buffer_set_caps (*buf, caps);

  return GST_FLOW_OK;
}

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

  device = yuv_video->device;
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
    default:
      break;
  }

  gst_buffer_copy_metadata (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);

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

  structure = gst_caps_get_structure (outcaps, 0);
  if (!gst_structure_get_int (structure, "chroma-type",
          &yuv_video->chroma_type))
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
    gint i;

    /* Intersect with the allowed caps */
    if (yuv_video->sink_caps)
      result = gst_caps_intersect (caps, yuv_video->sink_caps);
    else
      result = gst_caps_copy (caps);

    for (i = 0; i < gst_caps_get_size (result); i++) {
      GstStructure *structure = gst_caps_get_structure (result, i);
      guint32 fourcc;
      gint chroma_type;

      gst_structure_get_fourcc (structure, "format", &fourcc);
      /* calculate chroma type from fourcc */
      for (i = 0; i < N_FORMATS; i++) {
        if (formats[i].fourcc == fourcc) {
          chroma_type = formats[i].chroma_type;
          break;
        }
      }

      gst_structure_set_name (structure, "video/x-vdpau-video");
      gst_structure_remove_field (structure, "format");
      gst_structure_set (structure, "chroma-type", G_TYPE_INT, chroma_type,
          "device", G_TYPE_OBJECT, yuv_video->device, NULL);
    }
    gst_caps_do_simplify (result);
    GST_LOG ("transformed %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, caps,
        result);
  } else if (direction == GST_PAD_SRC) {
    /* FIXME: upstream negotiation */
    result = gst_static_pad_template_get_caps (&sink_template);
  }

  return result;
}

static gboolean
gst_vdp_yuv_video_start (GstBaseTransform * trans)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);

  yuv_video->device = gst_vdp_get_device (yuv_video->display);
  if (!yuv_video->device)
    return FALSE;
  yuv_video->sink_caps = gst_vdp_yuv_video_get_sink_caps (yuv_video);

  return TRUE;
}

static gboolean
gst_vdp_yuv_video_stop (GstBaseTransform * trans)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (trans);

  g_object_unref (yuv_video->device);
  gst_caps_unref (yuv_video->sink_caps);
  yuv_video->sink_caps = NULL;

  return TRUE;
}

/* GObject vmethod implementations */

static void
gst_vdp_yuv_video_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "VdpauYUVVideo",
      "Coyuv_video/Decoder/Video",
      "VDPAU video surface to YUV",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_vdp_yuv_video_finalize (GObject * object)
{
  GstVdpYUVVideo *yuv_video = (GstVdpYUVVideo *) object;

  g_free (yuv_video->display);
}

static void
gst_vdp_yuv_video_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_free (yuv_video->display);
      yuv_video->display = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_yuv_video_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpYUVVideo *yuv_video = GST_VDP_YUV_VIDEO (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, yuv_video->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_yuv_video_class_init (GstVdpYUVVideoClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_vdp_yuv_video_finalize;
  gobject_class->set_property = gst_vdp_yuv_video_set_property;
  gobject_class->get_property = gst_vdp_yuv_video_get_property;

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  trans_class->start = gst_vdp_yuv_video_start;
  trans_class->stop = gst_vdp_yuv_video_stop;
  trans_class->transform_caps = gst_vdp_yuv_video_transform_caps;
  trans_class->transform_size = gst_vdp_yuv_video_transform_size;
  trans_class->set_caps = gst_vdp_yuv_video_set_caps;
  trans_class->transform = gst_vdp_yuv_video_transform;
  trans_class->prepare_output_buffer = gst_vdp_yuv_video_prepare_output_buffer;
}

static void
gst_vdp_yuv_video_init (GstVdpYUVVideo * yuv_video, GstVdpYUVVideoClass * klass)
{
  yuv_video->sink_caps = NULL;

  yuv_video->display = NULL;
}
