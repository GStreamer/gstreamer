/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#include "unistd.h"

#include <gst/gst.h>

static GMainLoop *loop;

static gboolean
message_received (GstBus * bus, GstMessage * message, gpointer ignored)
{
  g_print ("message %p\n", message);

  if (message->type == GST_MESSAGE_EOS) {
    g_print ("EOS!!\n");
    if (g_main_loop_is_running (loop))
      g_main_loop_quit (loop);
  }
  gst_message_unref (message);

  return TRUE;
}

static gboolean
set_state (GstElement * element, GstElementState state,
    GstElementStateReturn expected)
{
  GstElementStateReturn ret;
  gboolean res;

  g_print ("setting state to %s, expecting %d...",
      gst_element_state_get_name (state), expected);
  ret = gst_element_set_state (element, state);
  res = (ret == expected);
  if (res) {
    g_print ("OK\n");
  } else {
    g_print ("failed, got %d\n", ret);
  }

  return res;
}

static gboolean
get_state (GstElement * element, GstElementState exp_state,
    GstElementState exp_pending, GTimeVal * timeval,
    GstElementStateReturn expected)
{
  GstElementStateReturn ret;
  gboolean res;
  GstElementState state, pending;

  g_print ("getting state: expecting %s, %s, %d...",
      gst_element_state_get_name (exp_state),
      gst_element_state_get_name (exp_pending), expected);

  ret = gst_element_get_state (element, &state, &pending, timeval);

  res = (ret == expected);
  res &= (state == exp_state);
  res &= (pending == exp_pending);

  if (res) {
    g_print ("OK\n");
  } else {
    g_print ("failed, got %s, %s, %d\n",
        gst_element_state_get_name (state),
        gst_element_state_get_name (pending), ret);
  }

  return res;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *fakesink;
  GstBus *bus;
  GTimeVal timeval;
  GstClock *clock;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_bus_new ();
  gst_bus_add_watch (bus, (GstBusHandler) message_received, NULL);

  clock = gst_system_clock_obtain ();
  g_assert (clock != NULL);

  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (fakesink);

  gst_element_set_bus (fakesink, bus);

  g_assert (set_state (fakesink, GST_STATE_READY, GST_STATE_SUCCESS));
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));

  g_assert (set_state (fakesink, GST_STATE_PAUSED, GST_STATE_ASYNC));

  g_assert (set_state (fakesink, GST_STATE_PLAYING, GST_STATE_ASYNC));

  g_get_current_time (&timeval);
  g_time_val_add (&timeval, G_USEC_PER_SEC * 1);

  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_PAUSED, &timeval,
          GST_STATE_ASYNC));

  g_print ("passed..\n");
  gst_object_unref (GST_OBJECT (fakesink));

  return 0;
}
