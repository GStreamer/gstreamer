/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) <2006> Jürg Billeter <j@bitron.ch>
 * Copyright (C) <2007> Sebastian Dröge <slomo@circular-chaos.org>
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

/*
 * this library handles interaction with Hal
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <glib.h>
#include "hal.h"

GST_DEBUG_CATEGORY_EXTERN (hal_debug);

#define GST_CAT_DEFAULT hal_debug

/* compat for older libhal */
#ifndef LIBHAL_FREE_DBUS_ERROR
#define LIBHAL_FREE_DBUS_ERROR(e) dbus_error_free (e)
#endif

/*
 * gst_hal_get_alsa_element:
 * @ctx: a #LibHalContext which should be used for querying HAL.
 * @udi: a #gchar corresponding to the UDI you want to get.
 * @device_type: a #GstHalDeviceType specifying the wanted device type.
 *
 * Get Hal UDI @udi's string value.
 *
 * Returns: a newly allocated #gchar string containing the appropriate pipeline
 * for UDI @udi, or NULL in the case of an error..
 */
static gchar *
gst_hal_get_alsa_element (LibHalContext * ctx, const gchar * udi,
    GstHalDeviceType device_type)
{
  char *type, *string = NULL;
  const char *element = NULL;
  DBusError error;

  dbus_error_init (&error);

  if (!libhal_device_query_capability (ctx, udi, "alsa", &error)) {
    if (dbus_error_is_set (&error)) {
      GST_DEBUG ("Failed querying %s for alsa capability: %s: %s",
          udi, error.name, error.message);
      LIBHAL_FREE_DBUS_ERROR (&error);
    } else {
      GST_DEBUG ("UDI %s has no alsa capability", udi);
    }
    return NULL;
  }

  type = libhal_device_get_property_string (ctx, udi, "alsa.type", &error);

  if (dbus_error_is_set (&error)) {
    GST_DEBUG ("UDI %s has alsa capabilities but no alsa.type property: %s, %s",
        udi, error.name, error.message);
    LIBHAL_FREE_DBUS_ERROR (&error);
    return NULL;
  } else if (!type) {
    GST_DEBUG ("UDI %s has empty alsa.type property", udi);
    return NULL;
  }

  if (strcmp (type, "playback") == 0 && device_type == GST_HAL_AUDIOSINK)
    element = "alsasink";
  else if (strcmp (type, "capture") == 0 && device_type == GST_HAL_AUDIOSRC)
    element = "alsasrc";

  libhal_free_string (type);

  if (element) {
    int card, device;

    card = libhal_device_get_property_int (ctx, udi, "alsa.card", &error);
    if (dbus_error_is_set (&error)) {
      GST_DEBUG ("UDI %s has no alsa.card property: %s: %s", udi, error.name,
          error.message);
      LIBHAL_FREE_DBUS_ERROR (&error);
      return NULL;
    } else if (card == -1) {
      GST_DEBUG ("UDI %s has no alsa.card property", udi);
      return NULL;
    }

    device = libhal_device_get_property_int (ctx, udi, "alsa.device", &error);
    if (dbus_error_is_set (&error)) {
      GST_DEBUG ("UDI %s has no alsa.device property: %s: %s", udi, error.name,
          error.message);
      LIBHAL_FREE_DBUS_ERROR (&error);
      return NULL;
    } else if (device == -1) {
      GST_DEBUG ("UDI %s has no alsa.device property", udi);
      return NULL;
    }

    /* This is a bit dodgy, since it makes lots of assumptions about the way
     * alsa is set up. In any case, only munge the device string for playback */
    if (strcmp (element, "alsasink") == 0 && device == 0) {
      /* handle default device specially to use
       * dmix, dsnoop, and softvol if appropriate */
      string = g_strdup_printf ("%s device=default:%d", element, card);
    } else {
      string =
          g_strdup_printf ("%s device=plughw:%d,%d", element, card, device);
    }
  }

  return string;
}

/*
 * gst_hal_get_oss_element:
 * @ctx: a #LibHalContext which should be used for querying HAL.
 * @udi: a #gchar corresponding to the UDI you want to get.
 * @device_type: a #GstHalDeviceType specifying the wanted device type.
 *
 * Get Hal UDI @udi's string value.
 *
 * Returns: a newly allocated #gchar string containing the appropriate pipeline
 * for UDI @udi, or NULL in the case of an error..
 */
static gchar *
gst_hal_get_oss_element (LibHalContext * ctx, const gchar * udi,
    GstHalDeviceType device_type)
{
  char *type, *string = NULL;
  const char *element = NULL;
  DBusError error;

  dbus_error_init (&error);

  if (!libhal_device_query_capability (ctx, udi, "oss", &error)) {
    if (dbus_error_is_set (&error)) {
      GST_DEBUG ("Failed querying %s for oss capability: %s: %s", udi,
          error.name, error.message);
      LIBHAL_FREE_DBUS_ERROR (&error);
    } else {
      GST_DEBUG ("UDI %s has no oss capability", udi);
    }
    return NULL;
  }

  type = libhal_device_get_property_string (ctx, udi, "oss.type", &error);
  if (dbus_error_is_set (&error)) {
    GST_DEBUG ("UDI %s has oss capabilities but no oss.type property: %s, %s",
        udi, error.name, error.message);
    LIBHAL_FREE_DBUS_ERROR (&error);
    return NULL;
  } else if (!type) {
    GST_DEBUG ("UDI %s has empty oss.type property", udi);
    return NULL;
  }

  if (strcmp (type, "pcm") == 0) {
    if (device_type == GST_HAL_AUDIOSINK)
      element = "osssink";
    else if (device_type == GST_HAL_AUDIOSRC)
      element = "osssrc";
  }
  libhal_free_string (type);

  if (element) {
    char *device = NULL;

    device =
        libhal_device_get_property_string (ctx, udi, "oss.device_file", &error);
    if (dbus_error_is_set (&error)) {
      GST_DEBUG
          ("UDI %s has oss capabilities but no oss.device_file property: %s, %s",
          udi, error.name, error.message);
      LIBHAL_FREE_DBUS_ERROR (&error);
      return NULL;
    } else if (!device) {
      GST_DEBUG ("UDI %s has empty oss.device_file property", udi);
      return NULL;
    }

    string = g_strdup_printf ("%s device=%s", element, device);
    libhal_free_string (device);
  }

  return string;
}

/*
 * gst_hal_get_string:
 * @udi: a #gchar corresponding to the UDI you want to get.
 * @device_type: a #GstHalDeviceType specifying the wanted device type.
 *
 * Get Hal UDI @udi's string value.
 *
 * Returns: a newly allocated #gchar string containing the appropriate pipeline
 * for UDI @udi, or NULL in the case of an error..
 */
static gchar *
gst_hal_get_string (const gchar * udi, GstHalDeviceType device_type)
{
  DBusError error;
  LibHalContext *ctx;
  char *string = NULL;

  /* Don't query HAL for NULL UDIs. Passing NULL as UDI to HAL gives
   * an assertion failure in D-Bus when running with
   * DBUS_FATAL_WARNINGS=1. */
  if (!udi)
    return NULL;

  dbus_error_init (&error);

  ctx = libhal_ctx_new ();
  /* Should only happen on OOM */
  g_return_val_if_fail (ctx != NULL, NULL);

  if (!libhal_ctx_set_dbus_connection (ctx, dbus_bus_get (DBUS_BUS_SYSTEM,
              &error))) {
    GST_DEBUG ("Unable to set DBus connection: %s: %s", error.name,
        error.message);
    LIBHAL_FREE_DBUS_ERROR (&error);
    goto ctx_free;
  }

  if (!libhal_ctx_init (ctx, &error)) {
    GST_DEBUG ("Unable to set init HAL context: %s: %s", error.name,
        error.message);
    LIBHAL_FREE_DBUS_ERROR (&error);
    goto ctx_free;
  }

  /* Now first check if UDI is an alsa device, then oss and then
   * check the childs of the given device. If there are alsa and oss
   * children the first alsa one is used. */

  string = gst_hal_get_alsa_element (ctx, udi, device_type);

  if (!string)
    string = gst_hal_get_oss_element (ctx, udi, device_type);

  if (!string) {
    int num_childs;
    char **childs = NULL;

    /* now try if one of the direct subdevices supports ALSA or OSS */
    childs =
        libhal_manager_find_device_string_match (ctx, "info.parent", udi,
        &num_childs, &error);
    if (dbus_error_is_set (&error)) {
      GST_DEBUG ("Unable to retrieve childs of %s: %s: %s", udi, error.name,
          error.message);
      LIBHAL_FREE_DBUS_ERROR (&error);
      goto ctx_shutdown;
    }

    if (childs && num_childs > 0) {
      int i;
      char *alsa_string = NULL, *oss_string = NULL;

      for (i = 0; i < num_childs && !alsa_string; i++) {
        alsa_string = gst_hal_get_alsa_element (ctx, childs[i], device_type);

        if (!oss_string)
          oss_string = gst_hal_get_oss_element (ctx, childs[i], device_type);
      }

      if (alsa_string) {
        string = alsa_string;
        g_free (oss_string);
      } else if (oss_string) {
        string = oss_string;
      }
    }
    libhal_free_string_array (childs);
  }

ctx_shutdown:
  if (!libhal_ctx_shutdown (ctx, &error)) {
    GST_DEBUG ("Closing connection to HAL failed: %s: %s", error.name,
        error.message);
    LIBHAL_FREE_DBUS_ERROR (&error);
  }

ctx_free:
  libhal_ctx_free (ctx);

  if (string == NULL) {
    GST_WARNING ("Problem finding a HAL audio device for udi %s", udi);
  } else {
    GST_INFO ("Using %s", string);
  }

  return string;
}

/* external functions */

/**
 * gst_hal_render_bin_from_udi:
 * @udi: a #gchar string corresponding to a Hal UDI.
 *
 * Render bin from Hal UDI @udi.
 *
 * Returns: a #GstElement containing the rendered bin.
 */
GstElement *
gst_hal_render_bin_from_udi (const gchar * udi, GstHalDeviceType type)
{
  GstElement *bin = NULL;
  gchar *value;

  value = gst_hal_get_string (udi, type);
  if (value)
    bin = gst_parse_bin_from_description (value, TRUE, NULL);
  g_free (value);
  return bin;
}

/**
 * gst_hal_get_audio_sink:
 * @udi: a #gchar string corresponding to a Hal UDI.
 *
 * Render audio output bin from GStreamer Hal UDI.
 * If no device with the specified UDI exists or @udi is NULL,
 * the default audio sink for the  platform is used
 * (typically alsasink, osssink or sunaudiosink).
 *
 * Returns: a #GstElement containing the audio output bin, or NULL if
 * everything failed.
 */
GstElement *
gst_hal_get_audio_sink (const gchar * udi)
{
  GstElement *ret = NULL;

  if (udi)
    ret = gst_hal_render_bin_from_udi (udi, GST_HAL_AUDIOSINK);

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_AUDIOSINK, NULL);

    if (!ret)
      GST_ERROR ("Hal audio sink and %s don't work", DEFAULT_AUDIOSINK);
  }

  return ret;
}

/**
 * gst_hal_get_audio_src:
 * @udi: a #gchar string corresponding to a Hal UDI.
 *
 * Render audio acquisition bin from GStreamer Hal UDI.
 * If no device with the specified UDI exists or @udi is NULL,
 * the default audio source for the  plaform is used
 * (typically alsasrc, osssrc or sunaudiosrc).
 *
 * Returns: a #GstElement containing the audio source bin, or NULL if
 * everything failed.
 */
GstElement *
gst_hal_get_audio_src (const gchar * udi)
{
  GstElement *ret = NULL;

  if (udi)
    ret = gst_hal_render_bin_from_udi (udi, GST_HAL_AUDIOSRC);

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_AUDIOSRC, NULL);

    if (!ret)
      GST_ERROR ("Hal audio src and %s don't work", DEFAULT_AUDIOSRC);
  }

  return ret;
}
