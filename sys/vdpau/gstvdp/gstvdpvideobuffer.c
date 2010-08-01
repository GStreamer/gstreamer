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

#include "gstvdpvideobuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_video_buffer_debug);
#define GST_CAT_DEFAULT gst_vdp_video_buffer_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_video_buffer_debug, "vdpvideobuffer", 0, "VDPAU video buffer");

GstVdpVideoBuffer *
gst_vdp_video_buffer_new (GstVdpDevice * device, VdpChromaType chroma_type,
    gint width, gint height, GError ** error)
{
  GstVdpVideoBuffer *buffer;
  VdpStatus status;
  VdpVideoSurface surface;

  g_return_val_if_fail (GST_IS_VDP_DEVICE (device), NULL);


  status = device->vdp_video_surface_create (device->device, chroma_type, width,
      height, &surface);
  if (status != VDP_STATUS_OK)
    goto create_error;

  buffer =
      (GstVdpVideoBuffer *) gst_mini_object_new (GST_TYPE_VDP_VIDEO_BUFFER);

  buffer->device = g_object_ref (device);
  buffer->surface = surface;

  return buffer;

create_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ,
      "Couldn't create a VdpVideoSurface, error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  return NULL;
}

static GObjectClass *gst_vdp_video_buffer_parent_class;

static void
gst_vdp_video_buffer_finalize (GstVdpVideoBuffer * buffer)
{
  GstVdpDevice *device;
  VdpStatus status;

  if (gst_vdp_buffer_revive (GST_VDP_BUFFER_CAST (buffer)))
    return;

  device = buffer->device;

  status = device->vdp_video_surface_destroy (buffer->surface);
  if (status != VDP_STATUS_OK)
    GST_ERROR
        ("Couldn't destroy the buffers VdpVideoSurface, error returned was: %s",
        device->vdp_get_error_string (status));

  g_object_unref (buffer->device);

  GST_MINI_OBJECT_CLASS (gst_vdp_video_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (buffer));
}

static void
gst_vdp_video_buffer_init (GstVdpVideoBuffer * buffer, gpointer g_class)
{
  buffer->device = NULL;
  buffer->surface = VDP_INVALID_HANDLE;
}

static void
gst_vdp_video_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_vdp_video_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_vdp_video_buffer_finalize;
}


GType
gst_vdp_video_buffer_get_type (void)
{
  static GType _gst_vdp_video_buffer_type;

  if (G_UNLIKELY (_gst_vdp_video_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_vdp_video_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstVdpVideoBuffer),
      0,
      (GInstanceInitFunc) gst_vdp_video_buffer_init,
      NULL
    };
    _gst_vdp_video_buffer_type = g_type_register_static (GST_TYPE_VDP_BUFFER,
        "GstVdpVideoBuffer", &info, 0);

    DEBUG_INIT ();
  }
  return _gst_vdp_video_buffer_type;
}

GstCaps *
gst_vdp_video_buffer_get_caps (gboolean filter, VdpChromaType chroma_type)
{
  GstCaps *video_caps, *yuv_caps;
  gint i;

  video_caps = gst_caps_new_empty ();
  for (i = 0; i < G_N_ELEMENTS (chroma_types); i++) {
    GstStructure *structure;

    if (filter) {
      if (chroma_types[i] != chroma_type)
        continue;
    }

    structure = gst_structure_new ("video/x-vdpau-video",
        "chroma-type", G_TYPE_INT, chroma_types[i],
        "width", GST_TYPE_INT_RANGE, 1, 4096,
        "height", GST_TYPE_INT_RANGE, 1, 4096, NULL);
    gst_caps_append_structure (video_caps, structure);
  }

  yuv_caps = gst_caps_new_empty ();
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    GstStructure *structure;

    if (filter) {
      if (formats[i].chroma_type != chroma_type)
        continue;
    }

    structure = gst_structure_new ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, formats[i].fourcc,
        "width", GST_TYPE_INT_RANGE, 1, 4096,
        "height", GST_TYPE_INT_RANGE, 1, 4096, NULL);
    gst_caps_append_structure (yuv_caps, structure);
  }

  gst_caps_append (video_caps, yuv_caps);
  return video_caps;
}

GstCaps *
gst_vdp_video_buffer_get_allowed_caps (GstVdpDevice * device)
{
  GstCaps *video_caps, *yuv_caps;
  gint i;
  VdpStatus status;

  video_caps = gst_caps_new_empty ();
  yuv_caps = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (chroma_types); i++) {
    VdpBool is_supported;
    guint32 max_w, max_h;

    status =
        device->vdp_video_surface_query_capabilities (device->device,
        chroma_types[i], &is_supported, &max_w, &max_h);

    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_CHROMA_TYPE)
      goto surface_query_caps_error;

    if (is_supported) {
      GstCaps *format_caps;
      gint j;

      format_caps = gst_caps_new_simple ("video/x-vdpau-video",
          "chroma-type", G_TYPE_INT, chroma_types[i],
          "width", GST_TYPE_INT_RANGE, 1, max_w,
          "height", GST_TYPE_INT_RANGE, 1, max_h, NULL);
      gst_caps_append (video_caps, format_caps);

      for (j = 0; j < G_N_ELEMENTS (formats); j++) {
        if (formats[j].chroma_type != chroma_types[i])
          continue;

        status =
            device->vdp_video_surface_query_ycbcr_capabilities (device->device,
            formats[j].chroma_type, formats[j].format, &is_supported);
        if (status != VDP_STATUS_OK
            && status != VDP_STATUS_INVALID_Y_CB_CR_FORMAT)
          goto surface_query_ycbcr_error;

        if (is_supported) {
          format_caps = gst_caps_new_simple ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, formats[j].fourcc,
              "width", GST_TYPE_INT_RANGE, 1, max_w,
              "height", GST_TYPE_INT_RANGE, 1, max_h, NULL);
          gst_caps_append (yuv_caps, format_caps);
        }
      }
    }
  }

done:
  gst_caps_append (video_caps, yuv_caps);
  return video_caps;

surface_query_caps_error:
  GST_ERROR_OBJECT (device,
      "Could not get query VDPAU video surface capabilites, "
      "Error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  goto done;

surface_query_ycbcr_error:
  GST_ERROR_OBJECT (device, "Could not query VDPAU YCbCr capabilites, "
      "Error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  goto done;
}

gboolean
gst_vdp_video_buffer_calculate_size (guint32 fourcc, gint width, gint height,
    guint * size)
{
  switch (fourcc) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
    {
      *size = gst_video_format_get_size (GST_VIDEO_FORMAT_YV12, width, height);
      break;
    }
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    {
      *size = gst_video_format_get_size (GST_VIDEO_FORMAT_YV12, width, height);
      break;
    }
    case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
    {
      *size = width * height + width * height / 2;
      break;
    }
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    {
      *size = gst_video_format_get_size (GST_VIDEO_FORMAT_UYVY, width, height);
      break;
    }
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    {
      *size = gst_video_format_get_size (GST_VIDEO_FORMAT_YUY2, width, height);
      break;
    }
    default:
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_vdp_video_buffer_download (GstVdpVideoBuffer * video_buf,
    GstBuffer * outbuf, guint32 fourcc, gint width, gint height)
{

  guint8 *data[3];
  guint32 stride[3];
  VdpYCbCrFormat format;
  GstVdpDevice *device;
  VdpVideoSurface surface;
  VdpStatus status;

  g_return_val_if_fail (GST_IS_VDP_VIDEO_BUFFER (video_buf), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (outbuf), FALSE);

  switch (fourcc) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
    {
      data[0] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          0, width, height);
      data[1] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          2, width, height);
      data[2] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          1, width, height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          0, width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          2, width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          1, width);

      format = VDP_YCBCR_FORMAT_YV12;
      break;
    }
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    {
      data[0] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          0, width, height);
      data[1] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          2, width, height);
      data[2] = GST_BUFFER_DATA (outbuf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          1, width, height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          0, width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          2, width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          1, width);

      format = VDP_YCBCR_FORMAT_YV12;
      break;
    }
    case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
    {
      data[0] = GST_BUFFER_DATA (outbuf);
      data[1] = GST_BUFFER_DATA (outbuf) + width * height;

      stride[0] = width;
      stride[1] = width;

      format = VDP_YCBCR_FORMAT_NV12;
      break;
    }
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    {
      data[0] = GST_BUFFER_DATA (outbuf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_UYVY,
          0, width);

      format = VDP_YCBCR_FORMAT_UYVY;
      break;
    }
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    {
      data[0] = GST_BUFFER_DATA (outbuf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YUY2,
          0, width);

      format = VDP_YCBCR_FORMAT_YUYV;
      break;
    }
    default:
      return FALSE;
  }

  device = video_buf->device;
  surface = video_buf->surface;

  GST_LOG_OBJECT (video_buf, "Entering vdp_video_surface_get_bits_ycbcr");
  status =
      device->vdp_video_surface_get_bits_ycbcr (surface,
      format, (void *) data, stride);
  GST_LOG_OBJECT (video_buf,
      "Got status %d from vdp_video_surface_get_bits_ycbcr", status);
  if (G_UNLIKELY (status != VDP_STATUS_OK)) {
    GST_ERROR_OBJECT (video_buf,
        "Couldn't get data from vdpau, Error returned from vdpau was: %s",
        device->vdp_get_error_string (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_vdp_video_buffer_upload (GstVdpVideoBuffer * video_buf, GstBuffer * src_buf,
    guint fourcc, gint width, gint height)
{
  guint8 *data[3];
  guint32 stride[3];
  VdpYCbCrFormat format;
  GstVdpDevice *device;
  VdpStatus status;

  g_return_val_if_fail (GST_IS_VDP_VIDEO_BUFFER (video_buf), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src_buf), FALSE);

  switch (fourcc) {
    case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
    {
      data[0] = GST_BUFFER_DATA (src_buf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          0, width, height);
      data[1] = GST_BUFFER_DATA (src_buf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          2, width, height);
      data[2] = GST_BUFFER_DATA (src_buf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_YV12,
          1, width, height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          0, width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          2, width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YV12,
          1, width);

      format = VDP_YCBCR_FORMAT_YV12;
      break;
    }
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    {
      data[0] = GST_BUFFER_DATA (src_buf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          0, width, height);
      data[1] = GST_BUFFER_DATA (src_buf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          2, width, height);
      data[2] = GST_BUFFER_DATA (src_buf) +
          gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420,
          1, width, height);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          0, width);
      stride[1] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          2, width);
      stride[2] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420,
          1, width);

      format = VDP_YCBCR_FORMAT_YV12;
      break;
    }
    case GST_MAKE_FOURCC ('N', 'V', '1', '2'):
    {
      data[0] = GST_BUFFER_DATA (src_buf);
      data[1] = GST_BUFFER_DATA (src_buf) + width * height;

      stride[0] = width;
      stride[1] = width;

      format = VDP_YCBCR_FORMAT_NV12;
      break;
    }
    case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
    {
      data[0] = GST_BUFFER_DATA (src_buf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_UYVY,
          0, width);

      format = VDP_YCBCR_FORMAT_UYVY;
      break;
    }
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
    {
      data[0] = GST_BUFFER_DATA (src_buf);

      stride[0] = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_YUY2,
          0, width);

      format = VDP_YCBCR_FORMAT_YUYV;
      break;
    }
    default:
      return FALSE;
  }

  device = video_buf->device;
  status = device->vdp_video_surface_put_bits_ycbcr (video_buf->surface, format,
      (void *) data, stride);
  if (G_UNLIKELY (status != VDP_STATUS_OK)) {
    GST_ERROR_OBJECT (video_buf, "Couldn't push YUV data to VDPAU, "
        "Error returned from vdpau was: %s",
        device->vdp_get_error_string (status));
    return FALSE;
  }

  return TRUE;
}
