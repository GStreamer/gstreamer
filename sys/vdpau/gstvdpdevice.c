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

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstvdpdevice.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_device_debug);
#define GST_CAT_DEFAULT gst_vdp_device_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_device_debug, "vdpdevice", 0, "VDPAU device object");

enum
{
  PROP_0,
  PROP_DISPLAY
};

G_DEFINE_TYPE_WITH_CODE (GstVdpDevice, gst_vdp_device, G_TYPE_OBJECT,
    DEBUG_INIT ());

static gboolean
gst_vdp_device_open (GstVdpDevice * device, GError ** error)
{
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
    {VDP_FUNC_ID_VIDEO_MIXER_CREATE, &device->vdp_video_mixer_create},
    {VDP_FUNC_ID_VIDEO_MIXER_DESTROY, &device->vdp_video_mixer_destroy},
    {VDP_FUNC_ID_VIDEO_MIXER_RENDER, &device->vdp_video_mixer_render},
    {VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES,
        &device->vdp_video_mixer_set_feature_enables},
    {VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES,
        &device->vdp_video_mixer_set_attribute_values},
    {VDP_FUNC_ID_OUTPUT_SURFACE_CREATE, &device->vdp_output_surface_create},
    {VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY, &device->vdp_output_surface_destroy},
    {VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES,
        &device->vdp_output_surface_query_capabilities},
    {VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE,
        &device->vdp_output_surface_get_bits_native},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11,
        &device->vdp_presentation_queue_target_create_x11},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY,
        &device->vdp_presentation_queue_target_destroy},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE,
        &device->vdp_presentation_queue_create},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY,
        &device->vdp_presentation_queue_destroy},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY,
        &device->vdp_presentation_queue_display},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE,
        &device->vdp_presentation_queue_block_until_surface_idle},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR,
        &device->vdp_presentation_queue_set_background_color},
    {VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS,
        &device->vdp_presentation_queue_query_surface_status}
  };

  GST_DEBUG_OBJECT (device, "Opening the device for display '%s'",
      device->display_name);

  device->display = XOpenDisplay (device->display_name);
  if (!device->display)
    goto create_display_error;

  screen = DefaultScreen (device->display);
  status =
      vdp_device_create_x11 (device->display, screen, &device->device,
      &device->vdp_get_proc_address);
  if (status != VDP_STATUS_OK)
    goto create_device_error;

  status = device->vdp_get_proc_address (device->device,
      VDP_FUNC_ID_GET_ERROR_STRING, (void **) &device->vdp_get_error_string);
  if (status != VDP_STATUS_OK)
    goto get_error_string_error;

  for (i = 0; i < G_N_ELEMENTS (vdp_function); i++) {
    status = device->vdp_get_proc_address (device->device,
        vdp_function[i].id, vdp_function[i].func);

    if (status != VDP_STATUS_OK)
      goto function_error;
  }

  GST_DEBUG_OBJECT (device, "Succesfully opened the device");

  return TRUE;

create_display_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ,
      "Could not open X display with name: %s", device->display_name);
  return FALSE;

create_device_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ,
      "Could not create VDPAU device for display: %s", device->display_name);
  return FALSE;

get_error_string_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ,
      "Could not get vdp_get_error_string function pointer from VDPAU");
  return FALSE;

function_error:
  g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ,
      "Could not get function pointer from VDPAU, error returned was: %s",
      device->vdp_get_error_string (status));
  return FALSE;
}

static GstVdpDevice *
gst_vdp_device_new (const gchar * display_name, GError ** error)
{
  GstVdpDevice *device;

  device = g_object_new (GST_TYPE_VDP_DEVICE, "display", display_name, NULL);

  if (!gst_vdp_device_open (device, error)) {
    g_object_unref (device);
    return NULL;
  }

  return device;
}

static void
gst_vdp_device_init (GstVdpDevice * device)
{
  device->display_name = NULL;
  device->display = NULL;
  device->device = VDP_INVALID_HANDLE;
  device->vdp_decoder_destroy = NULL;
}

static void
gst_vdp_device_finalize (GObject * object)
{
  GstVdpDevice *device = (GstVdpDevice *) object;

  if (device->device != VDP_INVALID_HANDLE && device->vdp_decoder_destroy) {
    device->vdp_device_destroy (device->device);
    device->device = VDP_INVALID_HANDLE;
  }

  if (device->display) {
    XCloseDisplay (device->display);
    device->display = NULL;
  }

  g_free (device->display_name);
  device->display_name = NULL;

  G_OBJECT_CLASS (gst_vdp_device_parent_class)->finalize (object);
}

static void
gst_vdp_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpDevice *device;

  g_return_if_fail (GST_IS_VDP_DEVICE (object));

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

  g_return_if_fail (GST_IS_VDP_DEVICE (object));

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

  object_class->finalize = gst_vdp_device_finalize;
  object_class->get_property = gst_vdp_device_get_property;
  object_class->set_property = gst_vdp_device_set_property;

  g_object_class_install_property (object_class,
      PROP_DISPLAY,
      g_param_spec_string ("display",
          "Display",
          "X Display Name",
          "", G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

typedef struct
{
  GHashTable *hash_table;
  GMutex mutex;
} GstVdpDeviceCache;

static void
device_destroyed_cb (gpointer data, GObject * object)
{
  GstVdpDeviceCache *device_cache = data;
  GHashTableIter iter;
  gpointer device;

  GST_DEBUG ("Removing object from hash table");

  g_mutex_lock (&device_cache->mutex);

  g_hash_table_iter_init (&iter, device_cache->hash_table);
  while (g_hash_table_iter_next (&iter, NULL, &device)) {
    if (device == object) {
      g_hash_table_iter_remove (&iter);
      break;
    }
  }

  g_mutex_unlock (&device_cache->mutex);
}

GstVdpDevice *
gst_vdp_get_device (const gchar * display_name, GError ** error)
{
  static gsize once = 0;
  static GstVdpDeviceCache device_cache;
  GstVdpDevice *device;

  GST_DEBUG ("display_name '%s'", display_name);

  if (g_once_init_enter (&once)) {
    device_cache.hash_table =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_mutex_init (&device_cache.mutex);

    g_once_init_leave (&once, 1);
  }

  g_mutex_lock (&device_cache.mutex);

  if (display_name)
    device = g_hash_table_lookup (device_cache.hash_table, display_name);
  else
    device = g_hash_table_lookup (device_cache.hash_table, "");

  if (!device) {
    GST_DEBUG ("No cached device, creating a new one");
    device = gst_vdp_device_new (display_name, error);
    if (device) {
      g_object_weak_ref (G_OBJECT (device), device_destroyed_cb, &device_cache);
      if (display_name)
        g_hash_table_insert (device_cache.hash_table, g_strdup (display_name),
            device);
      else
        g_hash_table_insert (device_cache.hash_table, g_strdup (""), device);
    } else
      GST_ERROR ("Could not create GstVdpDevice !");
  } else
    g_object_ref (device);

  g_mutex_unlock (&device_cache.mutex);

  return device;
}
