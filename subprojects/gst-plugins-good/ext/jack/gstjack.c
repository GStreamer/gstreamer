/* GStreamer Jack plugins
 * Copyright (C) 2006 Wim Taymans <wim@fluendo.com>
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

#include "gstjack.h"
#include "gstjackloader.h"

GType
gst_jack_connect_get_type (void)
{
  static gsize jack_connect_type = 0;

  if (g_once_init_enter (&jack_connect_type)) {
    static const GEnumValue jack_connect_enums[] = {
      {GST_JACK_CONNECT_NONE,
          "Don't automatically connect ports to physical ports", "none"},
      {GST_JACK_CONNECT_AUTO,
          "Automatically connect ports to physical ports", "auto"},
      {GST_JACK_CONNECT_AUTO_FORCED,
            "Automatically connect ports to as many physical ports as possible",
          "auto-forced"},
      {GST_JACK_CONNECT_EXPLICIT,
            "Connect ports to explicitly requested physical ports",
          "explicit"},
      {0, NULL, NULL},
    };
    GType tmp = g_enum_register_static ("GstJackConnect", jack_connect_enums);
    g_once_init_leave (&jack_connect_type, tmp);
  }
  return (GType) jack_connect_type;
}

GType
gst_jack_transport_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    static const GFlagsValue flag_values[] = {
      {GST_JACK_TRANSPORT_MASTER,
          "Start and stop transport with state changes", "master"},
      {GST_JACK_TRANSPORT_SLAVE,
          "Follow transport state changes", "slave"},
      {0, NULL, NULL},
    };
    GType tmp = g_flags_register_static ("GstJackTransport", flag_values);
    g_once_init_leave (&type, tmp);
  }
  return (GType) type;
}


static gpointer
gst_jack_client_copy (gpointer jclient)
{
  return jclient;
}


static void
gst_jack_client_free (gpointer jclient)
{
  return;
}


GType
gst_jack_client_get_type (void)
{
  static gsize jack_client_type = 0;

  if (g_once_init_enter (&jack_client_type)) {
    /* hackish, but makes it show up nicely in gst-inspect */
    GType tmp = g_boxed_type_register_static ("JackClient",
        (GBoxedCopyFunc) gst_jack_client_copy,
        (GBoxedFreeFunc) gst_jack_client_free);
    g_once_init_leave (&jack_client_type, tmp);
  }

  return (GType) jack_client_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  if (!gst_jack_load_library ()) {
    GST_WARNING ("Failed to load jack library");
    return TRUE;
  }

  ret |= GST_ELEMENT_REGISTER (jackaudiosrc, plugin);
  ret |= GST_ELEMENT_REGISTER (jackaudiosink, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    jack,
    "JACK audio elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
