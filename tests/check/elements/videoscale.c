/* GStreamer
 *
 * unit test for videoscale
 *
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

static GstCaps **
videoscale_get_allowed_caps (void)
{
  GstElement *scale = gst_element_factory_make ("videoscale", "scale");
  GstPadTemplate *templ;
  GstCaps *caps, **ret;
  GstStructure *s;
  gint i, n;

  templ =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (scale),
      "sink");
  fail_unless (templ != NULL);

  caps = gst_pad_template_get_caps (templ);

  n = gst_caps_get_size (caps);
  ret = g_new0 (GstCaps *, n + 1);

  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (caps, i);
    ret[i] = gst_caps_new_empty ();
    gst_caps_append_structure (ret[i], gst_structure_copy (s));
  }

  gst_object_unref (scale);

  return ret;
}

typedef struct
{
  GMainLoop *loop;
  gboolean eos;
} OnMessageUserData;

static void
on_message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  OnMessageUserData *d = user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_assert_not_reached ();
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (d->loop);
      d->eos = TRUE;
      break;
    default:
      break;
  }
}

static void
on_sink_handoff (GstElement * element, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  guint *n_buffers = user_data;

  *n_buffers = *n_buffers + 1;
}

static void
run_test (const GstCaps * caps, gint src_width, gint src_height,
    gint dest_width, gint dest_height, gint method,
    GCallback src_handoff, gpointer src_handoff_user_data,
    GCallback sink_handoff, gpointer sink_handoff_user_data)
{
  GstElement *pipeline;
  GstElement *src, *capsfilter1, *identity, *scale, *capsfilter2, *sink;
  GstBus *bus;
  GMainLoop *loop;
  GstCaps *copy;
  guint n_buffers = 0;
  OnMessageUserData omud = { NULL, };

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fail_unless (pipeline != NULL);

  src = gst_element_factory_make ("videotestsrc", "src");
  fail_unless (src != NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 5, NULL);

  capsfilter1 = gst_element_factory_make ("capsfilter", "filter1");
  fail_unless (capsfilter1 != NULL);
  copy = gst_caps_copy (caps);
  gst_caps_set_simple (copy, "width", G_TYPE_INT, src_width, "height",
      G_TYPE_INT, src_height, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  g_object_set (G_OBJECT (capsfilter1), "caps", copy, NULL);
  gst_caps_unref (copy);

  identity = gst_element_factory_make ("identity", "identity");
  fail_unless (identity != NULL);
  if (src_handoff) {
    g_object_set (G_OBJECT (identity), "signal-handoffs", TRUE, NULL);
    g_signal_connect (identity, "handoff", G_CALLBACK (src_handoff),
        src_handoff_user_data);
  }

  scale = gst_element_factory_make ("videoscale", "scale");
  fail_unless (scale != NULL);
  g_object_set (G_OBJECT (scale), "method", method, NULL);

  capsfilter2 = gst_element_factory_make ("capsfilter", "filter2");
  fail_unless (capsfilter2 != NULL);
  copy = gst_caps_copy (caps);
  gst_caps_set_simple (copy, "width", G_TYPE_INT, dest_width, "height",
      G_TYPE_INT, dest_height, NULL);
  g_object_set (G_OBJECT (capsfilter2), "caps", copy, NULL);
  gst_caps_unref (copy);

  sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (sink != NULL);
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, "async", FALSE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (on_sink_handoff), &n_buffers);
  if (sink_handoff) {
    g_signal_connect (sink, "handoff", G_CALLBACK (sink_handoff),
        sink_handoff_user_data);
  }

  gst_bin_add_many (GST_BIN (pipeline), src, capsfilter1, identity, scale,
      capsfilter2, sink, NULL);
  fail_unless (gst_element_link_many (src, capsfilter1, identity, scale,
          capsfilter2, sink, NULL));

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_signal_watch (bus);

  omud.loop = loop;
  omud.eos = FALSE;

  g_signal_connect (bus, "message", (GCallback) on_message_cb, &omud);

  gst_object_unref (bus);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  g_main_loop_run (loop);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (omud.eos == TRUE);
  fail_unless (n_buffers == 5);

  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
}

static void
on_sink_handoff_passthrough (GstElement * element, GstBuffer * buffer,
    GstPad * pad, gpointer user_data)
{
  GList **list = user_data;

  *list = g_list_prepend (*list, gst_buffer_ref (buffer));
}

static void
on_src_handoff_passthrough (GstElement * element, GstBuffer * buffer,
    gpointer user_data)
{
  GList **list = user_data;

  *list = g_list_prepend (*list, gst_buffer_ref (buffer));
}

GST_START_TEST (test_passthrough)
{
  GList *l1, *l2, *src_buffers = NULL, *sink_buffers = NULL;
  GstCaps **allowed_caps = NULL, **p;
  gint method;
  static const gint src_width = 640, src_height = 480;
  static const gint dest_width = 640, dest_height = 480;

  p = allowed_caps = videoscale_get_allowed_caps ();

  while (*p) {
    GstCaps *caps = *p;

    for (method = 0; method < 3; method++) {
      GST_DEBUG ("Running test for caps '%" GST_PTR_FORMAT "'"
          " from %dx%u to %dx%d with method %d", caps, src_width, src_height,
          dest_width, dest_height, method);
      run_test (caps, src_width, src_height,
          dest_width, dest_height, method,
          G_CALLBACK (on_src_handoff_passthrough), &src_buffers,
          G_CALLBACK (on_sink_handoff_passthrough), &sink_buffers);

      fail_unless (src_buffers && sink_buffers);
      fail_unless_equals_int (g_list_length (src_buffers),
          g_list_length (sink_buffers));

      for (l1 = src_buffers, l2 = sink_buffers; l1 && l2;
          l1 = l1->next, l2 = l2->next) {
        GstBuffer *a = l1->data;
        GstBuffer *b = l2->data;

        fail_unless_equals_int (GST_BUFFER_SIZE (a), GST_BUFFER_SIZE (b));
        fail_unless (GST_BUFFER_DATA (a) == GST_BUFFER_DATA (b));

        gst_buffer_unref (a);
        gst_buffer_unref (b);
      }
      g_list_free (src_buffers);
      src_buffers = NULL;
      g_list_free (sink_buffers);
      sink_buffers = NULL;
    }

    gst_caps_unref (caps);
    p++;
  }
  g_free (allowed_caps);
}

GST_END_TEST;

#define CREATE_TEST(name,method,src_width,src_height,dest_width,dest_height) \
GST_START_TEST (name) \
{ \
  GstCaps **allowed_caps = NULL, **p; \
  \
  p = allowed_caps = videoscale_get_allowed_caps (); \
  \
  while (*p) { \
    GstCaps *caps = *p; \
    \
    GST_DEBUG ("Running test for caps '%" GST_PTR_FORMAT "'" \
        " from %dx%u to %dx%d with method %d", caps, src_width, src_height, \
        dest_width, dest_height, method); \
    run_test (caps, src_width, src_height, \
        dest_width, dest_height, method, \
        NULL, NULL, NULL, NULL); \
    \
    gst_caps_unref (caps); \
    p++; \
  } \
  g_free (allowed_caps); \
} \
\
GST_END_TEST;

CREATE_TEST (test_downscale_640x480_320x240_method_0, 0, 640, 480, 320, 240);
CREATE_TEST (test_downscale_640x480_320x240_method_1, 1, 640, 480, 320, 240);
CREATE_TEST (test_downscale_640x480_320x240_method_2, 2, 640, 480, 320, 240);
CREATE_TEST (test_upscale_320x240_640x480_method_0, 0, 320, 240, 640, 480);
CREATE_TEST (test_upscale_320x240_640x480_method_1, 1, 320, 240, 640, 480);
CREATE_TEST (test_upscale_320x240_640x480_method_2, 2, 320, 240, 640, 480);
CREATE_TEST (test_downscale_640x480_1x1_method_0, 0, 640, 480, 1, 1);
CREATE_TEST (test_downscale_640x480_1x1_method_1, 1, 640, 480, 1, 1);
CREATE_TEST (test_downscale_640x480_1x1_method_2, 2, 640, 480, 1, 1);
CREATE_TEST (test_upscale_1x1_640x480_method_0, 0, 1, 1, 640, 480);
CREATE_TEST (test_upscale_1x1_640x480_method_1, 1, 1, 1, 640, 480);
CREATE_TEST (test_upscale_1x1_640x480_method_2, 2, 1, 1, 640, 480);
CREATE_TEST (test_downscale_641x481_111x30_method_0, 0, 641, 481, 111, 30);
CREATE_TEST (test_downscale_641x481_111x30_method_1, 1, 641, 481, 111, 30);
CREATE_TEST (test_downscale_641x481_111x30_method_2, 2, 641, 481, 111, 30);
CREATE_TEST (test_upscale_111x30_641x481_method_0, 0, 111, 30, 641, 481);
CREATE_TEST (test_upscale_111x30_641x481_method_1, 1, 111, 30, 641, 481);
CREATE_TEST (test_upscale_111x30_641x481_method_2, 2, 111, 30, 641, 481);
CREATE_TEST (test_downscale_641x481_30x111_method_0, 0, 641, 481, 30, 111);
CREATE_TEST (test_downscale_641x481_30x111_method_1, 1, 641, 481, 30, 111);
CREATE_TEST (test_downscale_641x481_30x111_method_2, 2, 641, 481, 30, 111);
CREATE_TEST (test_upscale_30x111_641x481_method_0, 0, 30, 111, 641, 481);
CREATE_TEST (test_upscale_30x111_641x481_method_1, 1, 30, 111, 641, 481);
CREATE_TEST (test_upscale_30x111_641x481_method_2, 2, 30, 111, 641, 481);
CREATE_TEST (test_downscale_640x480_320x1_method_0, 0, 640, 480, 320, 1);
CREATE_TEST (test_downscale_640x480_320x1_method_1, 1, 640, 480, 320, 1);
CREATE_TEST (test_downscale_640x480_320x1_method_2, 2, 640, 480, 320, 1);
CREATE_TEST (test_upscale_320x1_640x480_method_0, 0, 320, 1, 640, 480);
CREATE_TEST (test_upscale_320x1_640x480_method_1, 1, 320, 1, 640, 480);
CREATE_TEST (test_upscale_320x1_640x480_method_2, 2, 320, 1, 640, 480);
CREATE_TEST (test_downscale_640x480_1x240_method_0, 0, 640, 480, 1, 240);
CREATE_TEST (test_downscale_640x480_1x240_method_1, 1, 640, 480, 1, 240);
CREATE_TEST (test_downscale_640x480_1x240_method_2, 2, 640, 480, 1, 240);
CREATE_TEST (test_upscale_1x240_640x480_method_0, 0, 1, 240, 640, 480);
CREATE_TEST (test_upscale_1x240_640x480_method_1, 1, 1, 240, 640, 480);
CREATE_TEST (test_upscale_1x240_640x480_method_2, 2, 1, 240, 640, 480);

static Suite *
videoscale_suite (void)
{
  Suite *s = suite_create ("videoscale");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 180);
  tcase_add_test (tc_chain, test_passthrough);
  tcase_add_test (tc_chain, test_downscale_640x480_320x240_method_0);
  tcase_add_test (tc_chain, test_downscale_640x480_320x240_method_1);
  tcase_add_test (tc_chain, test_downscale_640x480_320x240_method_2);
  tcase_add_test (tc_chain, test_upscale_320x240_640x480_method_0);
  tcase_add_test (tc_chain, test_upscale_320x240_640x480_method_1);
  tcase_add_test (tc_chain, test_upscale_320x240_640x480_method_2);
  tcase_add_test (tc_chain, test_downscale_640x480_1x1_method_0);
  tcase_add_test (tc_chain, test_downscale_640x480_1x1_method_1);
  tcase_add_test (tc_chain, test_downscale_640x480_1x1_method_2);
  tcase_add_test (tc_chain, test_upscale_1x1_640x480_method_0);
  tcase_add_test (tc_chain, test_upscale_1x1_640x480_method_1);
  tcase_add_test (tc_chain, test_upscale_1x1_640x480_method_2);
  tcase_add_test (tc_chain, test_downscale_641x481_111x30_method_0);
  tcase_add_test (tc_chain, test_downscale_641x481_111x30_method_1);
  tcase_add_test (tc_chain, test_downscale_641x481_111x30_method_2);
  tcase_add_test (tc_chain, test_upscale_111x30_641x481_method_0);
  tcase_add_test (tc_chain, test_upscale_111x30_641x481_method_1);
  tcase_add_test (tc_chain, test_upscale_111x30_641x481_method_2);
  tcase_add_test (tc_chain, test_downscale_641x481_30x111_method_0);
  tcase_add_test (tc_chain, test_downscale_641x481_30x111_method_1);
  tcase_add_test (tc_chain, test_downscale_641x481_30x111_method_2);
  tcase_add_test (tc_chain, test_upscale_30x111_641x481_method_0);
  tcase_add_test (tc_chain, test_upscale_30x111_641x481_method_1);
  tcase_add_test (tc_chain, test_upscale_30x111_641x481_method_2);
  tcase_add_test (tc_chain, test_downscale_640x480_320x1_method_0);
  tcase_add_test (tc_chain, test_downscale_640x480_320x1_method_1);
  tcase_add_test (tc_chain, test_downscale_640x480_320x1_method_2);
  tcase_add_test (tc_chain, test_upscale_320x1_640x480_method_0);
  tcase_add_test (tc_chain, test_upscale_320x1_640x480_method_1);
  tcase_add_test (tc_chain, test_upscale_320x1_640x480_method_2);
  tcase_add_test (tc_chain, test_downscale_640x480_1x240_method_0);
  tcase_add_test (tc_chain, test_downscale_640x480_1x240_method_1);
  tcase_add_test (tc_chain, test_downscale_640x480_1x240_method_2);
  tcase_add_test (tc_chain, test_upscale_1x240_640x480_method_0);
  tcase_add_test (tc_chain, test_upscale_1x240_640x480_method_1);
  tcase_add_test (tc_chain, test_upscale_1x240_640x480_method_2);

  return s;
}

GST_CHECK_MAIN (videoscale);
