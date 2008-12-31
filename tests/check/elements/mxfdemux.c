/* GStreamer
 *
 * unit test for mxfdemux
 *
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <gst/check/gstcheck.h>
#include <string.h>
#include "mxfdemux.h"

static GstPad *mysrcpad, *mysinkpad;
static GMainLoop *loop = NULL;
static gboolean have_eos = FALSE;
static gboolean have_data = FALSE;

static GstStaticPadTemplate mysrctemplate =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/mxf"));

static GstStaticPadTemplate mysinktemplate =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
_pad_added (GstElement * element, GstPad * pad, gpointer user_data)
{
  gchar *name = gst_pad_get_name (pad);

  fail_unless_equals_string (name, "track_2");
  fail_unless (gst_pad_link (pad, mysinkpad) == GST_PAD_LINK_OK);

  g_free (name);
}

static GstFlowReturn
_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstCaps *caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, 11025,
      "channels", G_TYPE_INT, 1,
      "signed", G_TYPE_BOOLEAN, FALSE,
      "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
      "depth", G_TYPE_INT, 8,
      "width", G_TYPE_INT, 8,
      NULL);

  fail_unless (GST_BUFFER_CAPS (buffer) != NULL);
  fail_unless (gst_caps_is_always_compatible (GST_BUFFER_CAPS (buffer), caps));

  fail_unless_equals_int (GST_BUFFER_SIZE (buffer), sizeof (mxf_essence));
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), mxf_essence,
          sizeof (mxf_essence)) == 0);

  fail_unless (GST_BUFFER_TIMESTAMP (buffer) == 0);
  fail_unless (GST_BUFFER_DURATION (buffer) == 200 * GST_MSECOND);

  gst_buffer_unref (buffer);
  gst_caps_unref (caps);

  have_data = TRUE;
  return GST_FLOW_OK;
}

static gboolean
_sink_event (GstPad * pad, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    if (loop) {
      while (!g_main_loop_is_running (loop));
    }

    have_eos = TRUE;
    if (loop)
      g_main_loop_quit (loop);
  }

  gst_event_unref (event);

  return TRUE;
}

static GstPad *
_create_sink_pad (void)
{
  mysinkpad = gst_pad_new_from_static_template (&mysinktemplate, "sink");

  gst_pad_set_chain_function (mysinkpad, _sink_chain);
  gst_pad_set_event_function (mysinkpad, _sink_event);

  return mysinkpad;
}

static GstPad *
_create_src_pad_push (void)
{
  mysrcpad = gst_pad_new_from_static_template (&mysrctemplate, "src");

  return mysrcpad;
}

static GstFlowReturn
_src_getrange (GstPad * pad, guint64 offset, guint length, GstBuffer ** buffer)
{
  GstCaps *caps;

  if (offset + length > sizeof (mxf_file))
    return GST_FLOW_UNEXPECTED;

  caps = gst_caps_new_simple ("application/mxf", NULL);

  *buffer = gst_buffer_new ();
  GST_BUFFER_DATA (*buffer) = (guint8 *) (mxf_file + offset);
  GST_BUFFER_SIZE (*buffer) = length;
  gst_buffer_set_caps (*buffer, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}

static gboolean
_src_query (GstPad * pad, GstQuery * query)
{
  GstFormat fmt;

  if (GST_QUERY_TYPE (query) != GST_QUERY_DURATION)
    return FALSE;

  gst_query_parse_duration (query, &fmt, NULL);

  if (fmt != GST_FORMAT_BYTES)
    return FALSE;

  gst_query_set_duration (query, fmt, sizeof (mxf_file));

  return TRUE;
}

static GstPad *
_create_src_pad_pull (void)
{
  mysrcpad = gst_pad_new_from_static_template (&mysrctemplate, "src");
  gst_pad_set_getrange_function (mysrcpad, _src_getrange);
  gst_pad_set_query_function (mysrcpad, _src_query);

  return mysrcpad;
}

GST_START_TEST (test_pull)
{
  GstElement *mxfdemux;
  GstPad *sinkpad;

  have_eos = FALSE;
  have_data = FALSE;
  loop = g_main_loop_new (NULL, FALSE);

  mxfdemux = gst_element_factory_make ("mxfdemux", NULL);
  fail_unless (mxfdemux != NULL);
  g_signal_connect (mxfdemux, "pad-added", G_CALLBACK (_pad_added), NULL);
  sinkpad = gst_element_get_static_pad (mxfdemux, "sink");
  fail_unless (sinkpad != NULL);

  mysinkpad = _create_sink_pad ();
  fail_unless (mysinkpad != NULL);
  mysrcpad = _create_src_pad_pull ();
  fail_unless (mysrcpad != NULL);

  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);

  gst_pad_set_active (mysinkpad, TRUE);
  gst_pad_set_active (mysrcpad, TRUE);

  gst_element_set_state (mxfdemux, GST_STATE_PLAYING);

  g_main_loop_run (loop);
  fail_unless (have_eos == TRUE);
  fail_unless (have_data == TRUE);

  gst_element_set_state (mxfdemux, GST_STATE_NULL);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_pad_set_active (mysrcpad, FALSE);

  gst_object_unref (mxfdemux);
  gst_object_unref (mysinkpad);
  gst_object_unref (mysrcpad);
  g_main_loop_unref (loop);
  loop = NULL;
}

GST_END_TEST;

GST_START_TEST (test_push)
{
  GstElement *mxfdemux;
  GstBuffer *buffer;
  GstPad *sinkpad;

  have_data = FALSE;
  have_eos = FALSE;

  mxfdemux = gst_element_factory_make ("mxfdemux", NULL);
  fail_unless (mxfdemux != NULL);
  g_signal_connect (mxfdemux, "pad-added", G_CALLBACK (_pad_added), NULL);
  sinkpad = gst_element_get_static_pad (mxfdemux, "sink");
  fail_unless (sinkpad != NULL);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guint8 *) mxf_file;
  GST_BUFFER_SIZE (buffer) = sizeof (mxf_file);
  GST_BUFFER_OFFSET (buffer) = 0;

  mysinkpad = _create_sink_pad ();
  fail_unless (mysinkpad != NULL);
  mysrcpad = _create_src_pad_push ();
  fail_unless (mysrcpad != NULL);

  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);

  gst_pad_set_active (mysinkpad, TRUE);
  gst_pad_set_active (mysrcpad, TRUE);

  gst_element_set_state (mxfdemux, GST_STATE_PLAYING);

  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless (have_eos == TRUE);
  fail_unless (have_data == TRUE);

  gst_element_set_state (mxfdemux, GST_STATE_NULL);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_pad_set_active (mysrcpad, FALSE);

  gst_object_unref (mxfdemux);
  gst_object_unref (mysinkpad);
  gst_object_unref (mysrcpad);
}

GST_END_TEST;

static Suite *
mxfdemux_suite (void)
{
  Suite *s = suite_create ("mxfdemux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 180);
  tcase_add_test (tc_chain, test_pull);
  tcase_add_test (tc_chain, test_push);

  return s;
}

GST_CHECK_MAIN (mxfdemux);
