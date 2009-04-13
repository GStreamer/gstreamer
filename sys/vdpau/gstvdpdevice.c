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

#include <vdpau/vdpau_x11.h>
#include <gst/gst.h>

#include "gstvdpdevice.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_device_debug);
#define GST_CAT_DEFAULT gst_vdp_device_debug

enum
{
  PROP_0,
  PROP_DISPLAY
};



G_DEFINE_TYPE (GstVdpDevice, gst_vdp_device, G_TYPE_OBJECT);

static void
gst_vdp_device_init (GstVdpDevice * device)
{
  device->display_name = NULL;
  device->display = NULL;
  device->device = VDP_INVALID_HANDLE;
}

static void
gst_vdp_device_finalize (GObject * object)
{
  GstVdpDevice *device = (GstVdpDevice *) object;

  device->vdp_device_destroy (device->device);
  g_free (device->display_name);

  G_OBJECT_CLASS (gst_vdp_device_parent_class)->finalize (object);

}

static void
gst_vdp_device_constructed (GObject * object)
{
  GstVdpDevice *device = (GstVdpDevice *) object;
  gint screen;
  VdpStatus status;
  gint i;

  typedef struct
  {
    gint id;
    void *func;
  } VdpFunction;

  VdpFunction vdp_function[] = {
    {VDP_FUNC_ID_DEVICE_DESTROY, &device->vdp_device_destroy},
    {VDP_FUNC_ID_VIDEO_SURFACE_CREATE,
        &device->vdp_video_surface_create},
    {VDP_FUNC_ID_VIDEO_SURFACE_DESTROY,
        &device->vdp_video_surface_destroy},
    {VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES,
        &device->vdp_video_surface_query_capabilities},
    {VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES,
        &device->vdp_video_surface_query_ycbcr_capabilities},
    {VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR,
        &device->vdp_video_surface_get_bits_ycbcr},
    {VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR,
        &device->vdp_video_surface_put_bits_ycbcr},
    {VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS,
        &device->vdp_video_surface_get_parameters},
    {VDP_FUNC_ID_DECODER_CREATE, &device->vdp_decoder_create},
    {VDP_FUNC_ID_DECODER_RENDER, &device->vdp_decoder_render},
    {VDP_FUNC_ID_DECODER_DESTROY, &device->vdp_decoder_destroy},
    {VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES,
        &device->vdp_decoder_query_capabilities},
    {VDP_FUNC_ID_DECODER_GET_PARAMETERS,
        &device->vdp_decoder_get_parameters},
    {0, NULL}
  };

  device->display = XOpenDisplay (device->display_name);
  if (!device->display) {
    GST_ERROR_OBJECT (device, "Could not open X display with name: %s",
        device->display_name);
    return;
  }

  screen = DefaultScreen (device->display);
  status =
      vdp_device_create_x11 (device->display, screen, &device->device,
      &device->vdp_get_proc_address);
  if (status != VDP_STATUS_OK) {
    GST_ERROR_OBJECT (device, "Could not create VDPAU device");
    XCloseDisplay (device->display);
    device->display = NULL;

    return;
  }

  status = device->vdp_get_proc_address (device->device,
      VDP_FUNC_ID_GET_ERROR_STRING, (void **) &device->vdp_get_error_string);
  if (status != VDP_STATUS_OK) {
    GST_ERROR_OBJECT (device,
        "Could not get vdp_get_error_string function pointer from VDPAU");
    goto error;
  }

  for (i = 0; vdp_function[i].func != NULL; i++) {
    status = device->vdp_get_proc_address (device->device,
        vdp_function[i].id, vdp_function[i].func);

    if (status != VDP_STATUS_OK) {
      GST_ERROR_OBJECT (device, "Could not get function pointer from VDPAU,"
          " error returned was: %s", device->vdp_get_error_string (status));
      goto error;
    }
  }

  return;

error:
  XCloseDisplay (device->display);
  device->display = NULL;

  if (device->device != VDP_INVALID_HANDLE) {
    device->vdp_device_destroy (device->device);
    device->device = VDP_INVALID_HANDLE;
  }
}

static void
gst_vdp_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpDevice *device;

  g_return_if_fail (GST_IS_VDPAU_DEVICE (object));

  device = (GstVdpDevice *) object;

  switch (prop_id) {
    case PROP_DISPLAY:
      device->display_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_device_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVdpDevice *device;

  g_return_if_fail (GST_IS_VDPAU_DEVICE (object));

  device = (GstVdpDevice *) object;

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, device->display_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_device_class_init (GstVdpDeviceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = gst_vdp_device_constructed;
  object_class->finalize = gst_vdp_device_finalize;
  object_class->get_property = gst_vdp_device_get_property;
  object_class->set_property = gst_vdp_device_set_property;


  g_object_class_install_property (object_class,
      PROP_DISPLAY,
      g_param_spec_string ("display",
          "Display",
          "X Display Name",
          "", G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

  GST_DEBUG_CATEGORY_INIT (gst_vdp_device_debug, "vdpaudevice",
      0, "vdpaudevice");
}

GstVdpDevice *
gst_vdp_device_new (const gchar * display_name)
{
  GstVdpDevice *device;

  device = g_object_new (GST_TYPE_VDPAU_DEVICE, "display", display_name);

  return device;
}

static void
device_destroyed_cb (gpointer data, GObject * object)
{
  GHashTable *devices_hash = data;
  GHashTableIter iter;
  gpointer device;

  g_hash_table_iter_init (&iter, devices_hash);
  while (g_hash_table_iter_next (&iter, NULL, &device)) {
    if (device == object) {
      g_hash_table_iter_remove (&iter);
      break;
    }
  }
}

static gpointer
create_devices_hash (gpointer data)
{
  return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

GstVdpDevice *
gst_vdp_get_device (const gchar * display_name)
{
  static GOnce my_once = G_ONCE_INIT;
  GHashTable *devices_hash;
  GstVdpDevice *device;

  g_once (&my_once, create_devices_hash, NULL);
  devices_hash = my_once.retval;

  if (display_name)
    device = g_hash_table_lookup (devices_hash, display_name);
  else
    device = g_hash_table_lookup (devices_hash, "");

  if (!device) {
    device = gst_vdp_device_new (display_name);
    g_object_weak_ref (G_OBJECT (device), device_destroyed_cb, devices_hash);
    if (display_name)
      g_hash_table_insert (devices_hash, g_strdup (display_name), device);
    else
      g_hash_table_insert (devices_hash, g_strdup (""), device);
  } else
    g_object_ref (device);

  return device;
}
