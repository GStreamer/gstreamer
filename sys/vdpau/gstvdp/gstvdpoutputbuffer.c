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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvdpoutputbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_output_buffer_debug);
#define GST_CAT_DEFAULT gst_vdp_output_buffer_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_output_buffer_debug, "vdpoutputbuffer", 0, "VDPAU output buffer");

GstVdpOutputBuffer *
gst_vdp_output_buffer_new (GstVdpDevice * device, VdpRGBAFormat rgba_format,
    gint width, gint height, GError ** error)
{
  GstVdpOutputBuffer *buffer;
  VdpStatus status;
  VdpOutputSurface surface;

  status =
      device->vdp_output_surface_create (device->device, rgba_format, width,
      height, &surface);
  if (status != VDP_STATUS_OK)
    goto create_error;

  buffer =
      (GstVdpOutputBuffer *) gst_mini_object_new (GST_TYPE_VDP_OUTPUT_BUFFER);

  buffer->device = g_object_ref (device);
  buffer->rgba_format = rgba_format;
  buffer->width = width;
  buffer->height = height;

  buffer->surface = surface;

  return buffer;

create_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ,
      "Couldn't create a VdpOutputSurface, error returned from vdpau was: %s",
      device->vdp_get_error_string (status));
  return NULL;
}

static GObjectClass *gst_vdp_output_buffer_parent_class;

static void
gst_vdp_output_buffer_finalize (GstVdpOutputBuffer * buffer)
{
  GstVdpDevice *device;
  VdpStatus status;

  if (gst_vdp_buffer_revive (GST_VDP_BUFFER_CAST (buffer)))
    return;

  device = buffer->device;

  status = device->vdp_output_surface_destroy (buffer->surface);
  if (status != VDP_STATUS_OK)
    GST_ERROR
        ("Couldn't destroy the buffers VdpOutputSurface, error returned was: %s",
        device->vdp_get_error_string (status));

  g_object_unref (buffer->device);

  GST_MINI_OBJECT_CLASS (gst_vdp_output_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (buffer));
}

static void
gst_vdp_output_buffer_init (GstVdpOutputBuffer * buffer, gpointer g_class)
{
  buffer->device = NULL;
  buffer->surface = VDP_INVALID_HANDLE;
}

static void
gst_vdp_output_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_vdp_output_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_vdp_output_buffer_finalize;
}


GType
gst_vdp_output_buffer_get_type (void)
{
  static GType _gst_vdp_output_buffer_type;

  if (G_UNLIKELY (_gst_vdp_output_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_vdp_output_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstVdpOutputBuffer),
      0,
      (GInstanceInitFunc) gst_vdp_output_buffer_init,
      NULL
    };
    _gst_vdp_output_buffer_type = g_type_register_static (GST_TYPE_VDP_BUFFER,
        "GstVdpOutputBuffer", &info, 0);

    DEBUG_INIT ();
  }
  return _gst_vdp_output_buffer_type;
}

typedef struct
{
  VdpRGBAFormat format;
  GstStaticCaps caps;
} GstVdpOutputBufferFormats;

GstVdpOutputBufferFormats rgba_formats[] = {
  {VDP_RGBA_FORMAT_A8,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)8, "
            "depth = (int)8, "
            "endianness = (int)4321, "
            "red_mask = (int)0x00, "
            "green_mask = (int)0x00, "
            "blue_mask = (int)0x00, " "alpha_mask = (int)0xff")},
  {VDP_RGBA_FORMAT_B10G10R10A2,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)30, "
            "endianness = (int)4321, "
            "red_mask = (int)0x000003fc, "
            "green_mask = (int)0x003ff000, "
            "blue_mask = (int)0xffc00000, " "alpha_mask = (int)0x00000003")},
  {VDP_RGBA_FORMAT_B8G8R8A8,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)24, "
            "endianness = (int)4321, "
            "red_mask = (int)0x0000ff00, "
            "green_mask = (int)0x00ff0000, "
            "blue_mask = (int)0xff000000, " "alpha_mask = (int)0x000000ff")},
  {VDP_RGBA_FORMAT_R10G10B10A2,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)30, "
            "endianness = (int)4321, "
            "red_mask = (int)0xffc00000, "
            "green_mask = (int)0x003ff000, "
            "blue_mask = (int)0x000003fc, " "alpha_mask = (int)0x00000003")},
  {VDP_RGBA_FORMAT_R8G8B8A8,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)24, "
            "endianness = (int)4321, "
            "red_mask = (int)0xff000000, "
            "green_mask = (int)0x00ff0000, "
            "blue_mask = (int)0x0000ff00, " "alpha_mask = (int)0x000000ff")},
};


GstCaps *
gst_vdp_output_buffer_get_template_caps (void)
{
  GstCaps *caps, *rgb_caps;
  gint i;

  caps = gst_caps_new_empty ();
  rgb_caps = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (rgba_formats); i++) {
    GstCaps *format_caps;

    format_caps = gst_caps_new_simple ("video/x-vdpau-output",
        "rgba-format", G_TYPE_INT, rgba_formats[i].format,
        "width", GST_TYPE_INT_RANGE, 1, 8192,
        "height", GST_TYPE_INT_RANGE, 1, 8192, NULL);
    gst_caps_append (caps, format_caps);

    format_caps = gst_static_caps_get (&rgba_formats[i].caps);
    format_caps = gst_caps_copy (format_caps);
    gst_caps_set_simple (format_caps,
        "width", GST_TYPE_INT_RANGE, 1, 8192,
        "height", GST_TYPE_INT_RANGE, 1, 8192, NULL);
    gst_caps_append (rgb_caps, format_caps);

  }

  gst_caps_append (caps, rgb_caps);

  return caps;
}

GstCaps *
gst_vdp_output_buffer_get_allowed_caps (GstVdpDevice * device)
{
  GstCaps *caps, *rgb_caps;
  gint i;

  g_return_val_if_fail (GST_IS_VDP_DEVICE (device), NULL);

  caps = gst_caps_new_empty ();
  rgb_caps = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (rgba_formats); i++) {
    VdpStatus status;
    VdpBool is_supported;
    guint max_w, max_h;

    status = device->vdp_output_surface_query_capabilities (device->device,
        rgba_formats[i].format, &is_supported, &max_w, &max_h);
    if (status != VDP_STATUS_OK && status != VDP_STATUS_INVALID_RGBA_FORMAT) {
      GST_ERROR_OBJECT (device,
          "Could not get query VDPAU output surface capabilites, "
          "Error returned from vdpau was: %s",
          device->vdp_get_error_string (status));

      goto error;
    }

    if (is_supported) {
      GstCaps *format_caps;

      format_caps = gst_caps_new_simple ("video/x-vdpau-output",
          "rgba-format", G_TYPE_INT, rgba_formats[i].format,
          "width", GST_TYPE_INT_RANGE, 1, max_w,
          "height", GST_TYPE_INT_RANGE, 1, max_h, NULL);
      gst_caps_append (caps, format_caps);

      format_caps = gst_static_caps_get (&rgba_formats[i].caps);
      format_caps = gst_caps_copy (format_caps);
      gst_caps_set_simple (format_caps,
          "width", GST_TYPE_INT_RANGE, 1, 8192,
          "height", GST_TYPE_INT_RANGE, 1, 8192, NULL);
      gst_caps_append (rgb_caps, format_caps);
    }
  }

  gst_caps_append (caps, rgb_caps);

error:

  return caps;
}

gboolean
gst_vdp_caps_to_rgba_format (GstCaps * caps, VdpRGBAFormat * rgba_format)
{
  GstStructure *structure;
  gint c_bpp, c_depth, c_endianness, c_red_mask, c_green_mask, c_blue_mask,
      c_alpha_mask;

  gint i;

  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_has_name (structure, "video/x-raw-rgb"))
    return FALSE;

  if (!gst_structure_get_int (structure, "bpp", &c_bpp) ||
      !gst_structure_get_int (structure, "depth", &c_depth) ||
      !gst_structure_get_int (structure, "endianness", &c_endianness) ||
      !gst_structure_get_int (structure, "red_mask", &c_red_mask) ||
      !gst_structure_get_int (structure, "green_mask", &c_green_mask) ||
      !gst_structure_get_int (structure, "blue_mask", &c_blue_mask) ||
      !gst_structure_get_int (structure, "alpha_mask", &c_alpha_mask))
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (rgba_formats); i++) {
    gint bpp, depth, endianness, red_mask, green_mask, blue_mask, alpha_mask;

    GstCaps *rgb_caps = gst_static_caps_get (&rgba_formats[i].caps);
    structure = gst_caps_get_structure (rgb_caps, 0);

    gst_structure_get_int (structure, "bpp", &bpp);
    gst_structure_get_int (structure, "depth", &depth);
    gst_structure_get_int (structure, "endianness", &endianness);
    gst_structure_get_int (structure, "red_mask", &red_mask);
    gst_structure_get_int (structure, "green_mask", &green_mask);
    gst_structure_get_int (structure, "blue_mask", &blue_mask);
    gst_structure_get_int (structure, "alpha_mask", &alpha_mask);

    if (c_bpp == bpp && c_depth == depth && c_endianness == endianness &&
        c_red_mask == red_mask && c_green_mask == green_mask &&
        c_blue_mask == blue_mask && c_alpha_mask == alpha_mask) {
      gst_caps_unref (rgb_caps);
      *rgba_format = rgba_formats[i].format;
      return TRUE;
    }

    gst_caps_unref (rgb_caps);
  }

  return FALSE;
}

gboolean
gst_vdp_output_buffer_calculate_size (GstVdpOutputBuffer * output_buf,
    guint * size)
{
  g_return_val_if_fail (GST_IS_VDP_OUTPUT_BUFFER (output_buf), FALSE);

  switch (output_buf->rgba_format) {
    case VDP_RGBA_FORMAT_A8:
    {
      *size = output_buf->width * output_buf->height;
      break;
    }

    case VDP_RGBA_FORMAT_B10G10R10A2:
    case VDP_RGBA_FORMAT_B8G8R8A8:
    case VDP_RGBA_FORMAT_R10G10B10A2:
    case VDP_RGBA_FORMAT_R8G8B8A8:
    {
      *size = output_buf->width * output_buf->height * 4;
      break;
    }

    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_vdp_output_buffer_download (GstVdpOutputBuffer * output_buf,
    GstBuffer * outbuf, GError ** error)
{
  guint8 *data[1];
  guint32 stride[1];
  GstVdpDevice *device;
  VdpOutputSurface surface;
  VdpStatus status;

  g_return_val_if_fail (GST_IS_VDP_OUTPUT_BUFFER (output_buf), FALSE);

  switch (output_buf->rgba_format) {
    case VDP_RGBA_FORMAT_A8:
    {
      stride[0] = output_buf->width;
      break;
    }

    case VDP_RGBA_FORMAT_B10G10R10A2:
    case VDP_RGBA_FORMAT_B8G8R8A8:
    case VDP_RGBA_FORMAT_R10G10B10A2:
    case VDP_RGBA_FORMAT_R8G8B8A8:
    {
      stride[0] = output_buf->width * 4;
      break;
    }

    default:
      return FALSE;
  }

  device = output_buf->device;
  surface = output_buf->surface;
  data[0] = GST_BUFFER_DATA (outbuf);

  GST_LOG_OBJECT (output_buf, "Entering vdp_output_surface_get_bits_native");
  status =
      device->vdp_output_surface_get_bits_native (surface, NULL, (void *) data,
      stride);
  GST_LOG_OBJECT (output_buf,
      "Got status %d from vdp_output_get_bits_native", status);

  if (G_UNLIKELY (status != VDP_STATUS_OK)) {
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ,
        "Couldn't get data from vdpau, error returned from vdpau was: %s",
        device->vdp_get_error_string (status));
    return FALSE;
  }

  return TRUE;
}
