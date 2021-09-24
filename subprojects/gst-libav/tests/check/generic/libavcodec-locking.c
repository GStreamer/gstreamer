/* GStreamer
 * Copyright (C) 2005 Luca Ognibene <luogni@tin.it>
 * Based (copied) on simple_launch_lines.c
 *
 * libavcodec-locking.c: Unit test for libavcodec's locks
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


#include <gst/check/gstcheck.h>
#include <stdlib.h>

#define NUM_SINKS 10

static GstElement *
setup_pipeline (const gchar * pipe_descr)
{
  GstElement *pipeline;

  pipeline = gst_parse_launch (pipe_descr, NULL);
  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), NULL);
  return pipeline;
}

/*
 * run_pipeline:
 * @pipe: the pipeline to run
 * @desc: the description for use in messages
 * @events: is a mask of expected events
 * @tevent: is the expected terminal event.
 *
 * the poll call will time out after half a second.
 */
static void
run_pipeline (GstElement * pipe, const gchar * descr,
    GstMessageType events, GstMessageType tevent)
{
  GstBus *bus;
  GstMessage *message;
  GstMessageType revent;
  GstStateChangeReturn ret;

  g_assert (pipe);
  bus = gst_element_get_bus (pipe);
  g_assert (bus);

  ret = gst_element_set_state (pipe, GST_STATE_PLAYING);
  ret = gst_element_get_state (pipe, NULL, NULL, GST_CLOCK_TIME_NONE);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_critical ("Couldn't set pipeline to PLAYING");
    goto done;
  }

  while (1) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);

    /* always have to pop the message before getting back into poll */
    if (message) {
      revent = GST_MESSAGE_TYPE (message);
      gst_message_unref (message);
    } else {
      revent = GST_MESSAGE_UNKNOWN;
    }

    if (revent == tevent) {
      break;
    } else if (revent == GST_MESSAGE_UNKNOWN) {
      g_critical ("Unexpected timeout in gst_bus_poll, looking for %d: %s",
          tevent, descr);
      break;
    } else if (revent & events) {
      continue;
    }
    g_critical
        ("Unexpected message received of type %d, '%s', looking for %d: %s",
        revent, gst_message_type_get_name (revent), tevent, descr);
  }

done:
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_START_TEST (test_libavcodec_locks)
{
  gchar *sink[NUM_SINKS + 1], *s, *sinks;
  gint i;

  for (i = 0; i < NUM_SINKS; i++)
    sink[i] =
        g_strdup_printf
        (" t.src_%u ! queue ! avenc_mpeg4 ! avdec_mpeg4 ! fakesink sync=true",
        i);

  sink[NUM_SINKS] = NULL;

  sinks = g_strjoinv (" ", sink);

  s = g_strdup_printf
      ("videotestsrc ! video/x-raw,format=(string)I420,width=320,height=240,framerate=(fraction)10/1 ! tee name=t %s",
      sinks);

  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);
  g_free (s);

  for (i = 0; i < NUM_SINKS; i++)
    g_free (sink[i]);
  g_free (sinks);
}

GST_END_TEST;

static Suite *
simple_launch_lines_suite (void)
{
  gint timeout = 0;

  Suite *s = suite_create ("Pipelines");
  TCase *tc_chain = tcase_create ("linear");

  if (g_getenv ("CK_DEFAULT_TIMEOUT"))
    timeout = atoi (g_getenv ("CK_DEFAULT_TIMEOUT"));

  if (timeout == 0)
    timeout = 3;

  /* set multiple of default timeout (random magic value) */
  tcase_set_timeout (tc_chain, timeout * 12);

  suite_add_tcase (s, tc_chain);

#ifndef GST_DISABLE_PARSE
  /* only run this if we haven't been configured with --disable-encoders */
  if (gst_registry_check_feature_version (gst_registry_get (), "avenc_mpeg4",
          GST_VERSION_MAJOR, GST_VERSION_MINOR, 0)) {
    tcase_add_test (tc_chain, test_libavcodec_locks);
  } else {
    g_print ("******* Skipping libavcodec_locks test, no encoder available\n");
  }
#endif

  return s;
}

GST_CHECK_MAIN (simple_launch_lines);
