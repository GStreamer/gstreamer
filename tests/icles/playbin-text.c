/* GStreamer
 *
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.com>
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

#include <gst/gst.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct _App App;

struct _App
{
  GstElement *playbin;
  GstElement *textsink;

  GMainLoop *loop;
};

static App s_app;

static gboolean
bus_message (GstBus * bus, GstMessage * message, App * app)
{
  GST_DEBUG ("got message %s",
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);

      g_main_loop_quit (app->loop);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_warning (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);

      g_main_loop_quit (app->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_message ("received EOS");
      g_main_loop_quit (app->loop);
      break;
    default:
      break;
  }
  return TRUE;
}

static void
have_subtitle (GstElement * appsink, App * app)
{
  GstBuffer *buffer;

  /* get the buffer, we can also wakeup the mainloop to get the subtitle from
   * appsink in the mainloop */
  g_signal_emit_by_name (appsink, "pull-buffer", &buffer);

  if (buffer) {
    guint8 *data;
    guint size;
    GstFormat format;
    gint64 position;
    GstClock *clock;
    GstClockTime base_time, running_time;

    format = GST_FORMAT_TIME;
    gst_element_query_position (appsink, &format, &position);

    clock = gst_element_get_clock (appsink);
    base_time = gst_element_get_base_time (appsink);

    running_time = gst_clock_get_time (clock) - base_time;

    gst_object_unref (clock);

    g_message ("received a subtitle at position %" GST_TIME_FORMAT
        ", running_time %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
        GST_TIME_ARGS (running_time));

    data = GST_BUFFER_DATA (buffer);
    size = GST_BUFFER_SIZE (buffer);

    gst_util_dump_mem (data, size);
  }
}

int
main (int argc, char *argv[])
{
  App *app = &s_app;
  GstBus *bus;
  GstCaps *subcaps;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    return -1;
  }

  /* create a mainloop to get messages */
  app->loop = g_main_loop_new (NULL, TRUE);

  app->playbin = gst_element_factory_make ("playbin2", NULL);
  g_assert (app->playbin);

  /* set appsink to get the subtitles */
  app->textsink = gst_element_factory_make ("appsink", "subtitle_sink");
  g_object_set (G_OBJECT (app->textsink), "emit-signals", TRUE, NULL);
  g_object_set (G_OBJECT (app->textsink), "ts-offset", 0 * GST_SECOND, NULL);
  g_signal_connect (app->textsink, "new-buffer", G_CALLBACK (have_subtitle),
      app);
  subcaps = gst_caps_from_string ("text/x-pango-markup; text/plain");
  g_object_set (G_OBJECT (app->textsink), "caps", subcaps, NULL);
  gst_caps_unref (subcaps);

  g_object_set (G_OBJECT (app->playbin), "text-sink", app->textsink, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (app->playbin));

  /* add watch for messages */
  gst_bus_add_watch (bus, (GstBusFunc) bus_message, app);

  /* set to read from appsrc */
  g_object_set (app->playbin, "uri", argv[1], NULL);

  /* go to playing and wait in a mainloop. */
  gst_element_set_state (app->playbin, GST_STATE_PLAYING);

  /* this mainloop is stopped when we receive an error or EOS */
  g_main_loop_run (app->loop);

  g_message ("stopping");

  gst_element_set_state (app->playbin, GST_STATE_NULL);

  gst_object_unref (bus);
  g_main_loop_unref (app->loop);

  return 0;
}
