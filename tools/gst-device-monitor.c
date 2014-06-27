/* GStreamer command line device monitor testing utility
 * Copyright (C) 2014 Tim-Philipp Müller <tim@centricular.com>
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

#include <locale.h>

#include <gst/gst.h>
#include <gst/gst-i18n-app.h>
#include <gst/math-compat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

GST_DEBUG_CATEGORY (devmon_debug);
#define GST_CAT_DEFAULT devmon_debug

typedef struct
{
  GMainLoop *loop;
  GstDeviceMonitor *monitor;
  guint bus_watch_id;
} DevMonApp;

static gboolean bus_msg_handler (GstBus * bus, GstMessage * msg, gpointer data);

static void
device_added (GstDevice * device)
{
  gchar *device_class, *caps_str, *name;
  GstCaps *caps;
  guint i, size = 0;

  caps = gst_device_get_caps (device);
  if (caps != NULL)
    size = gst_caps_get_size (caps);

  name = gst_device_get_display_name (device);
  device_class = gst_device_get_device_class (device);

  g_print ("\nDevice found:\n\n");
  g_print ("\tname  : %s\n", name);
  g_print ("\tclass : %s\n", device_class);
  for (i = 0; i < size; ++i) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    caps_str = gst_structure_to_string (s);
    g_print ("\t%s %s\n", (i == 0) ? "caps  :" : "       ", caps_str);
    g_free (caps_str);
  }

  g_print ("\n");

  g_free (name);
  g_free (device_class);

  if (caps != NULL)
    gst_caps_unref (caps);
}

static void
device_removed (GstDevice * device)
{
  gchar *name;

  name = gst_device_get_display_name (device);

  g_print ("Device removed:\n");
  g_print ("\tname  : %s\n", name);

  g_free (name);
}

static gboolean
bus_msg_handler (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstDevice *device;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_DEVICE_ADDED:
      gst_message_parse_device_added (msg, &device);
      device_added (device);
      break;
    case GST_MESSAGE_DEVICE_REMOVED:
      gst_message_parse_device_removed (msg, &device);
      device_removed (device);
      break;
    default:
      g_print ("%s message\n", GST_MESSAGE_TYPE_NAME (msg));
      break;
  }

  return TRUE;
}

int
main (int argc, char **argv)
{
  gboolean print_version = FALSE;
  GError *err = NULL;
  gchar **arg, **args = NULL;
  gboolean follow = FALSE;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    {"version", 0, 0, G_OPTION_ARG_NONE, &print_version,
        N_("Print version information and exit"), NULL},
    {"follow", 'f', 0, G_OPTION_ARG_NONE, &follow,
        N_("Don't exit after showing the initial device list, but wait "
              "for devices to added/removed."), NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args, NULL},
    {NULL}
  };
  GTimer *timer;
  DevMonApp app;
  GstBus *bus;
  GList *devices;

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-device-monitor-" GST_API_VERSION);

  ctx = g_option_context_new ("[DEVICE_CLASSES[:FILTER_CAPS]] "
      "[DEVICE_CLASSES[:FILTER_CAPS]] …");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    return 1;
  }
  g_option_context_free (ctx);

  GST_DEBUG_CATEGORY_INIT (devmon_debug, "device-monitor", 0,
      "gst-device-monitor");

  if (print_version) {
    gchar *version_str;

    version_str = gst_version_string ();
    g_print ("%s version %s\n", g_get_prgname (), PACKAGE_VERSION);
    g_print ("%s\n", version_str);
    g_print ("%s\n", GST_PACKAGE_ORIGIN);
    g_free (version_str);

    return 0;
  }

  app.loop = g_main_loop_new (NULL, FALSE);
  app.monitor = gst_device_monitor_new ();

  bus = gst_device_monitor_get_bus (app.monitor);
  app.bus_watch_id = gst_bus_add_watch (bus, bus_msg_handler, &app);
  gst_object_unref (bus);

  /* process optional remaining arguments in the form
   * DEVICE_CLASSES or DEVICE_CLASSES:FILTER_CAPS */
  for (arg = args; arg != NULL && *arg != NULL; ++arg) {
    gchar **filters = g_strsplit (*arg, ":", -1);
    if (filters != NULL && filters[0] != NULL) {
      GstCaps *caps = NULL;

      if (filters[1] != NULL) {
        caps = gst_caps_from_string (filters[1]);
        if (caps == NULL)
          g_warning ("Couldn't parse device filter caps '%s'", filters[1]);
      }
      gst_device_monitor_add_filter (app.monitor, filters[0], caps);
      if (caps)
        gst_caps_unref (caps);
      g_strfreev (filters);
    }
  }

  g_print ("Probing devices...\n\n");

  timer = g_timer_new ();

  if (!gst_device_monitor_start (app.monitor))
    g_error ("Failed to start device monitor!");

  GST_INFO ("Took %.2f seconds", g_timer_elapsed (timer, NULL));

  devices = gst_device_monitor_get_devices (app.monitor);
  if (devices != NULL) {
    while (devices != NULL) {
      GstDevice *device = devices->data;

      device_added (device);
      gst_object_unref (device);
      devices = g_list_remove_link (devices, devices);
    }
  } else {
    g_print ("No devices found!\n");
  }

  if (follow) {
    g_print ("Monitoring devices, waiting for devices to be removed or "
        "new devices to be added...\n");
    g_main_loop_run (app.loop);
  }

  gst_device_monitor_stop (app.monitor);
  gst_object_unref (app.monitor);

  g_source_remove (app.bus_watch_id);
  g_main_loop_unref (app.loop);
  g_timer_destroy (timer);

  return 0;
}
