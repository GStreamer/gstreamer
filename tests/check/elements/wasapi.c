/* GStreamer unit test for wasapi plugin
 *
 * Copyright (C) 2021 Jakub Janků <janku.jakub.jj@gmail.com>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>

typedef struct
{
  GMainLoop *loop;
  GstElement *pipe;
  guint rem_st_changes;
} SinkPlayReadyTData;

static gboolean
bus_watch_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  fail_unless (message->type != GST_MESSAGE_ERROR);
  return G_SOURCE_CONTINUE;
}

static gboolean
state_timer_cb (gpointer user_data)
{
  SinkPlayReadyTData *tdata = user_data;
  GstState nxt_st = tdata->rem_st_changes % 2 == 1 ?
      GST_STATE_READY : GST_STATE_PLAYING;

  ASSERT_SET_STATE (tdata->pipe, nxt_st, GST_STATE_CHANGE_SUCCESS);
  tdata->rem_st_changes--;

  if (tdata->rem_st_changes == 0) {
    g_main_loop_quit (tdata->loop);
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

/* Test that the wasapisink can survive the state change
 * from PLAYING to READY and then back to PLAYING */
GST_START_TEST (test_sink_play_ready)
{
  SinkPlayReadyTData tdata;
  GstBus *bus;

  tdata.pipe = gst_parse_launch ("audiotestsrc ! wasapisink async=false", NULL);
  fail_unless (tdata.pipe != NULL);
  bus = gst_element_get_bus (tdata.pipe);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_watch_cb, NULL);

  ASSERT_SET_STATE (tdata.pipe, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  tdata.rem_st_changes = 3;     /* -> READY -> PLAYING -> QUIT */
  g_timeout_add_seconds (1, state_timer_cb, &tdata);

  tdata.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (tdata.loop);

  g_main_loop_unref (tdata.loop);
  gst_bus_remove_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (tdata.pipe);
}

GST_END_TEST;

static gboolean
device_is_available (const gchar * factory_name)
{
  GstElement *elem;
  gboolean avail;

  elem = gst_element_factory_make (factory_name, NULL);
  if (elem == NULL) {
    GST_INFO ("%s: not available", factory_name);
    return FALSE;
  }

  avail = gst_element_set_state (elem, GST_STATE_READY)
      == GST_STATE_CHANGE_SUCCESS;
  if (!avail) {
    GST_INFO ("%s: cannot change state to ready", factory_name);
  }

  gst_element_set_state (elem, GST_STATE_NULL);
  gst_object_unref (elem);

  return avail;
}

static Suite *
wasapi_suite (void)
{
  Suite *suite = suite_create ("wasapi");
  TCase *tc_sink = tcase_create ("sink");

  suite_add_tcase (suite, tc_sink);

  if (device_is_available ("wasapisink")) {
    tcase_add_test (tc_sink, test_sink_play_ready);
  } else {
    GST_INFO ("Sink not available, skipping sink tests");
  }

  return suite;
}

GST_CHECK_MAIN (wasapi)
