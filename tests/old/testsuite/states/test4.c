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

  g_print ("setting %s state to %s, expecting %d...",
      gst_element_get_name (element),
      gst_element_state_get_name (state), expected);
  ret = gst_element_set_state (element, state);
  res = (ret == expected);
  g_print ("%s\n", res ? "OK" : "failed");

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

  g_print ("getting state %s: expecting %s, %s, %d...",
      gst_element_get_name (element),
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

static gboolean
commit_callback (GstClock * clock, GstClockTime time,
    GstClockID id, GstElement * element)
{
  g_print ("commiting state change..");
  gst_element_commit_state (element);

  return FALSE;
}

static gboolean
abort_callback (GstClock * clock, GstClockTime time,
    GstClockID id, GstElement * element)
{
  g_print ("aborting state change..");
  gst_element_abort_state (element);

  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *fakesink, *bin;
  GstBus *bus;
  GTimeVal timeval;
  GstClock *clock;
  GstClockID id;
  GstClockTime base;
  GstClockReturn result;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_bus_new ();
  gst_bus_add_watch (bus, (GstBusHandler) message_received, NULL);

  clock = gst_system_clock_obtain ();
  g_assert (clock != NULL);

  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (fakesink);
  bin = gst_element_factory_make ("bin", "bin");
  g_assert (bin);

  gst_bin_add (GST_BIN (bin), fakesink);

  gst_element_set_bus (bin, bus);

  g_assert (set_state (bin, GST_STATE_READY, GST_STATE_SUCCESS));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));

  g_assert (set_state (fakesink, GST_STATE_NULL, GST_STATE_SUCCESS));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));
  g_assert (get_state (fakesink, GST_STATE_NULL, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));

  g_assert (set_state (bin, GST_STATE_READY, GST_STATE_SUCCESS));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));

  g_get_current_time (&timeval);
  g_time_val_add (&timeval, G_USEC_PER_SEC / 2);

  g_assert (set_state (fakesink, GST_STATE_PAUSED, GST_STATE_ASYNC));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, &timeval,
          GST_STATE_ASYNC));

  g_time_val_add (&timeval, G_USEC_PER_SEC / 2);
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_PAUSED, &timeval,
          GST_STATE_ASYNC));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, &timeval,
          GST_STATE_ASYNC));

  g_assert (set_state (fakesink, GST_STATE_READY, GST_STATE_SUCCESS));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));

  g_assert (set_state (bin, GST_STATE_PAUSED, GST_STATE_ASYNC));
  g_time_val_add (&timeval, G_USEC_PER_SEC / 2);
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_PAUSED, &timeval,
          GST_STATE_ASYNC));
  g_time_val_add (&timeval, G_USEC_PER_SEC / 2);
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_PAUSED, &timeval,
          GST_STATE_ASYNC));

  g_assert (set_state (bin, GST_STATE_READY, GST_STATE_SUCCESS));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, &timeval,
          GST_STATE_SUCCESS));
  g_assert (get_state (fakesink, GST_STATE_READY, GST_STATE_VOID_PENDING,
          &timeval, GST_STATE_SUCCESS));

  g_assert (set_state (bin, GST_STATE_PAUSED, GST_STATE_ASYNC));
  g_assert (set_state (fakesink, GST_STATE_READY, GST_STATE_SUCCESS));
  g_time_val_add (&timeval, G_USEC_PER_SEC / 2);
  g_assert (get_state (bin, GST_STATE_PAUSED, GST_STATE_VOID_PENDING, &timeval,
          GST_STATE_SUCCESS));

  g_assert (set_state (bin, GST_STATE_READY, GST_STATE_SUCCESS));
  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_VOID_PENDING, &timeval,
          GST_STATE_SUCCESS));

  g_assert (set_state (bin, GST_STATE_PAUSED, GST_STATE_ASYNC));

  base = gst_clock_get_time (clock);
  id = gst_clock_new_single_shot_id (clock, base + 1 * GST_SECOND);
  g_print ("waiting one second async id %p to abort state\n", id);
  result =
      gst_clock_id_wait_async (id, (GstClockCallback) abort_callback, fakesink);
  gst_clock_id_unref (id);
  g_assert (result == GST_CLOCK_OK);

  g_assert (get_state (bin, GST_STATE_READY, GST_STATE_PAUSED, NULL,
          GST_STATE_FAILURE));

  g_assert (set_state (bin, GST_STATE_PAUSED, GST_STATE_ASYNC));

  base = gst_clock_get_time (clock);
  id = gst_clock_new_single_shot_id (clock, base + 1 * GST_SECOND);
  g_print ("waiting one second async id %p to commit state\n", id);
  result =
      gst_clock_id_wait_async (id, (GstClockCallback) commit_callback,
      fakesink);
  gst_clock_id_unref (id);
  g_assert (result == GST_CLOCK_OK);

  g_assert (get_state (bin, GST_STATE_PAUSED, GST_STATE_VOID_PENDING, NULL,
          GST_STATE_SUCCESS));

  g_print ("passed..\n");
  gst_object_unref (GST_OBJECT (fakesink));

  return 0;
}
