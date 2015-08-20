/* GStreamer
 * Copyright (C) 2003 Thomas Vander Stichele <thomas@apestaart.org>
 *               2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *               2005 Andy Wingo <wingo@pobox.com>
 *
 * gst-typefind.c: Use GStreamer to find the type of a file
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
#  include "config.h"
#endif

#include <string.h>
#include <locale.h>

#include "tools.h"

static void
have_type_handler (GstElement * typefind, guint probability,
    const GstCaps * caps, GstCaps ** p_caps)
{
  if (p_caps) {
    *p_caps = gst_caps_copy (caps);
  }
}

static void
typefind_file (const gchar * filename)
{
  GstStateChangeReturn sret;
  GstElement *pipeline;
  GstElement *source;
  GstElement *typefind;
  GstElement *fakesink;
  GstState state;
  GstCaps *caps = NULL;
  GDir *dir;

  if ((dir = g_dir_open (filename, 0, NULL))) {
    const gchar *entry;

    while ((entry = g_dir_read_name (dir))) {
      gchar *path;

      path = g_strconcat (filename, G_DIR_SEPARATOR_S, entry, NULL);
      typefind_file (path);
      g_free (path);
    }

    g_dir_close (dir);
    return;
  }

  pipeline = gst_pipeline_new ("pipeline");

  source = gst_element_factory_make ("filesrc", "source");
  g_assert (GST_IS_ELEMENT (source));
  typefind = gst_element_factory_make ("typefind", "typefind");
  g_assert (GST_IS_ELEMENT (typefind));
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (GST_IS_ELEMENT (typefind));

  gst_bin_add_many (GST_BIN (pipeline), source, typefind, fakesink, NULL);
  gst_element_link_many (source, typefind, fakesink, NULL);

  g_signal_connect (G_OBJECT (typefind), "have-type",
      G_CALLBACK (have_type_handler), &caps);

  g_object_set (source, "location", filename, NULL);

  GST_DEBUG ("Starting typefinding for %s", filename);

  /* typefind will only commit to PAUSED if it actually finds a type;
   * otherwise the state change fails */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

  /* wait until state change either completes or fails */
  sret = gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL, -1);

  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:{
      GstMessage *msg;
      GstBus *bus;
      GError *err = NULL;

      bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
      msg = gst_bus_poll (bus, GST_MESSAGE_ERROR, 0);
      gst_object_unref (bus);

      if (msg) {
        gst_message_parse_error (msg, &err, NULL);
        g_printerr ("%s - FAILED: %s\n", filename, err->message);
        g_clear_error (&err);
        gst_message_unref (msg);
      } else {
        g_printerr ("%s - FAILED: unknown error\n", filename);
      }
      break;
    }
    case GST_STATE_CHANGE_SUCCESS:{
      if (caps) {
        gchar *caps_str;

        caps_str = gst_caps_to_string (caps);
        g_print ("%s - %s\n", filename, caps_str);
        g_free (caps_str);
        gst_caps_unref (caps);
      } else {
        g_print ("%s - %s\n", filename, "No type found");
      }
      break;
    }
    default:
      g_assert_not_reached ();
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

int
main (int argc, char *argv[])
{
  gchar **filenames = NULL;
  guint num, i;
  GError *err = NULL;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    GST_TOOLS_GOPTION_VERSION,
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL},
    {NULL}
  };

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-typefind-" GST_API_VERSION);

  ctx = g_option_context_new ("FILES");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_clear_error (&err);
    g_option_context_free (ctx);
    exit (1);
  }
  g_option_context_free (ctx);

  gst_tools_print_version ();

  if (filenames == NULL || *filenames == NULL) {
    g_print ("Please give one or more filenames to %s\n\n", g_get_prgname ());
    return 1;
  }

  num = g_strv_length (filenames);

  for (i = 0; i < num; ++i) {
    typefind_file (filenames[i]);
  }

  g_strfreev (filenames);

  return 0;
}
