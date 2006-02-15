/* GStreamer
 * Copyright (C) <2002> Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) <2006> Jürg Billeter <j@bitron.ch>
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


/* external functions */

/**
 * gst_hal_get_string:
 * @key: a #gchar corresponding to the key you want to get.
 *
 * Get Hal UDI @udi's string value.
 *
 * Returns: a newly allocated #gchar string containing the appropriate pipeline
 * for UDI @udi, or NULL in the case of an error..
 */
gchar *
gst_hal_get_string (const gchar * udi)
{
  DBusConnection *connection;
  DBusError error;
  LibHalContext *ctx;
  char *type, *string;
  char *element;
  int card, device;

  dbus_error_init (&error);

  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
  g_return_val_if_fail (connection != NULL, NULL);

  ctx = libhal_ctx_new ();
  g_return_val_if_fail (ctx != NULL, NULL);

  libhal_ctx_set_dbus_connection (ctx, connection);
  libhal_ctx_init (ctx, &error);

  string = NULL;

  if (libhal_device_query_capability (ctx, udi, "alsa", &error)) {
    type = libhal_device_get_property_string (ctx, udi, "alsa.type", &error);
    if (strcmp (type, "playback") == 0) {
      element = "alsasink";
    } else if (strcmp (type, "capture") == 0) {
      element = "alsasrc";
    } else {
      element = NULL;
    }
    card = libhal_device_get_property_int (ctx, udi, "alsa.card", &error);
    device = libhal_device_get_property_int (ctx, udi, "alsa.device", &error);
    if (device == 0) {
      /* handle default device specially to use
       * dmix, dsnoop, and softvol if appropriate */
      string = g_strdup_printf ("%s device=default:%d", element, card);
    } else {
      string =
          g_strdup_printf ("%s device=plughw:%d,%d", element, card, device);
    }
  }

  libhal_ctx_shutdown (ctx, &error);
  libhal_ctx_free (ctx);

  dbus_error_free (&error);

  return string;
}

/**
 * gst_hal_render_bin_from_udi:
 * @key: a #gchar string corresponding to a Hal UDI.
 *
 * Render bin from Hal UDI @udi.
 *
 * Returns: a #GstElement containing the rendered bin.
 */
GstElement *
gst_hal_render_bin_from_udi (const gchar * udi)
{
  GstElement *bin = NULL;
  gchar *value;

  value = gst_hal_get_string (udi);
  if (value)
    bin = gst_parse_bin_from_description (value, TRUE, NULL);
  g_free (value);
  return bin;
}

/**
 * gst_hal_get_audio_sink:
 *
 * Render audio output bin from GStreamer Hal UDI.
 * If no device with the specified UDI exists, the default audio sink for the
 * platform is used (typically osssink or sunaudiosink).
 *
 * Returns: a #GstElement containing the audio output bin, or NULL if
 * everything failed.
 */
GstElement *
gst_hal_get_audio_sink (const gchar * udi)
{
  GstElement *ret = gst_hal_render_bin_from_udi (udi);

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_AUDIOSINK, NULL);

    if (!ret)
      g_warning ("No Hal default audio sink key and %s doesn't work",
          DEFAULT_AUDIOSINK);
  }

  return ret;
}

/**
 * gst_hal_get_audio_src:
 *
 * Render audio acquisition bin from GStreamer Hal UDI.
 * If no device with the specified UDI exists, the default audio source for the
 * plaform is used (typically osssrc or sunaudiosrc).
 *
 * Returns: a #GstElement containing the audio source bin, or NULL if
 * everything failed.
 */
GstElement *
gst_hal_get_audio_src (const gchar * udi)
{
  GstElement *ret = gst_hal_render_bin_from_udi (udi);

  if (!ret) {
    ret = gst_element_factory_make (DEFAULT_AUDIOSRC, NULL);

    if (!ret)
      g_warning ("No Hal default audio src key and %s doesn't work",
          DEFAULT_AUDIOSRC);
  }

  return ret;
}
