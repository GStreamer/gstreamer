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

#include "gstvdpoutputbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_output_buffer_debug);
#define GST_CAT_DEFAULT gst_vdp_output_buffer_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_output_buffer_debug, "vdpauoutputbuffer", 0, "VDPAU output buffer");

GstVdpOutputBuffer *
gst_vdp_output_buffer_new (GstVdpDevice * device, VdpRGBAFormat rgba_format,
    gint width, gint height)
{
  GstVdpOutputBuffer *buffer;
  VdpStatus status;
  VdpOutputSurface surface;

  status =
      device->vdp_output_surface_create (device->device, rgba_format, width,
      height, &surface);
  if (status != VDP_STATUS_OK) {
    GST_ERROR ("Couldn't create a VdpOutputSurface, error returned was: %s",
        device->vdp_get_error_string (status));
    return NULL;
  }

  buffer =
      (GstVdpOutputBuffer *) gst_mini_object_new (GST_TYPE_VDP_OUTPUT_BUFFER);

  buffer->device = g_object_ref (device);
  buffer->surface = surface;

  return buffer;
}

static GObjectClass *gst_vdp_output_buffer_parent_class;

static void
gst_vdp_output_buffer_finalize (GstVdpOutputBuffer * buffer)
{
  GstVdpDevice *device;
  VdpStatus status;

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
    _gst_vdp_output_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
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
            "depth = (int)0, "
            "endianness = G_BIG_ENDIAN, "
            "red_mask = (int)0x00, "
            "green_mask = (int)0x00, "
            "blue_mask = (int)0x00, " "alpha_mask = (int)0xff")},
  {VDP_RGBA_FORMAT_B10G10R10A2,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)30, "
            "endianness = G_BIG_ENDIAN, "
            "red_mask = (int)0x000003fc, "
            "green_mask = (int)0x003ff000, "
            "blue_mask = (int)0xffc00000, " "alpha_mask = (int)0x00000003")},
  {VDP_RGBA_FORMAT_B8G8R8A8,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)24, "
            "endianness = G_BIG_ENDIAN, "
            "red_mask = (int)0x0000ff00, "
            "green_mask = (int)0x00ff0000, "
            "blue_mask = (int)0xff000000, " "alpha_mask = (int)0x000000ff")},
  {VDP_RGBA_FORMAT_R10G10B10A2,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)30, "
            "endianness = G_BIG_ENDIAN, "
            "red_mask = (int)0xffc00000, "
            "green_mask = (int)0x003ff000, "
            "blue_mask = (int)0x000003fc, " "alpha_mask = (int)0x00000003")},
  {VDP_RGBA_FORMAT_R8G8B8A8,
      GST_STATIC_CAPS ("video/x-raw-rgb, "
            "bpp = (int)32, "
            "depth = (int)24, "
            "endianness = G_BIG_ENDIAN"
            "red_mask = (int)0xff000000, "
            "green_mask = (int)0x00ff0000, "
            "blue_mask = (int)0x0000ff00, " "alpha_mask = (int)0x000000ff")},
};

int n_rgba_formats = G_N_ELEMENTS (rgba_formats);

GstCaps *
gst_vdp_output_buffer_get_allowed_caps (GstVdpDevice * device)
{
  GstCaps *caps;
  gint i;

  g_return_val_if_fail (GST_IS_VDP_DEVICE (device), NULL);

  caps = gst_caps_new_empty ();

  for (i = 0; i < n_rgba_formats; i++) {
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
    }
  }

error:

  return caps;
}
