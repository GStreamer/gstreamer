/* GStreamer
 *
 * unit test for streamheader handling
 *
 * Copyright (C) 2007 Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstbufferstraw.h>

#ifndef GST_DISABLE_PARSE

/* this tests a gdp-serialized tag from audiotestsrc being sent only once
 * to clients of multifdsink */

static int n_tags = 0;

static gboolean
tag_event_probe_cb (GstPad * pad, GstEvent * event, GMainLoop * loop)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
    {
      ++n_tags;
      fail_if (n_tags > 1, "More than 1 tag received");
      break;
    }
    case GST_EVENT_EOS:
    {
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

GST_START_TEST (test_multifdsink_gdp_tag)
{
  GstElement *p1, *p2;
  GstElement *src, *sink, *depay;
  GstPad *pad;
  GMainLoop *loop;
  int pfd[2];

  loop = g_main_loop_new (NULL, FALSE);

  p1 = gst_parse_launch ("audiotestsrc num-buffers=10 ! gdppay"
      " ! multifdsink name=p1sink", NULL);
  fail_if (p1 == NULL);
  p2 = gst_parse_launch ("fdsrc name=p2src ! gdpdepay name=depay"
      " ! fakesink name=p2sink signal-handoffs=True", NULL);
  fail_if (p2 == NULL);

  fail_if (pipe (pfd) == -1);


  gst_element_set_state (p1, GST_STATE_READY);

  sink = gst_bin_get_by_name (GST_BIN (p1), "p1sink");
  g_signal_emit_by_name (sink, "add", pfd[1], NULL);
  gst_object_unref (sink);

  src = gst_bin_get_by_name (GST_BIN (p2), "p2src");
  g_object_set (G_OBJECT (src), "fd", pfd[0], NULL);
  gst_object_unref (src);

  depay = gst_bin_get_by_name (GST_BIN (p2), "depay");
  fail_if (depay == NULL);

  pad = gst_element_get_static_pad (depay, "src");
  fail_unless (pad != NULL, "Could not get pad out of depay");
  gst_object_unref (depay);

  gst_pad_add_event_probe (pad, G_CALLBACK (tag_event_probe_cb), loop);

  gst_element_set_state (p1, GST_STATE_PLAYING);
  gst_element_set_state (p2, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  assert_equals_int (n_tags, 1);

  gst_element_set_state (p1, GST_STATE_NULL);
  gst_object_unref (p1);
  gst_element_set_state (p2, GST_STATE_NULL);
  gst_object_unref (p2);
}

GST_END_TEST;

#ifdef HAVE_VORBIS
/* this tests gdp-serialized Vorbis header pages being sent only once
 * to clients of multifdsink; the gdp depayloader should deserialize
 * exactly three in_caps buffers for the three header packets */

static int n_in_caps = 0;

static gboolean
buffer_probe_cb (GstPad * pad, GstBuffer * buffer)
{
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_IN_CAPS)) {
    GstCaps *caps;
    GstStructure *s;
    const GValue *sh;
    GArray *buffers;
    GstBuffer *buf;
    int i;
    gboolean found = FALSE;

    n_in_caps++;

    caps = gst_buffer_get_caps (buffer);
    s = gst_caps_get_structure (caps, 0);
    fail_unless (gst_structure_has_field (s, "streamheader"));
    sh = gst_structure_get_value (s, "streamheader");
    buffers = g_value_peek_pointer (sh);
    assert_equals_int (buffers->len, 3);


    for (i = 0; i < 3; ++i) {
      GValue *val;

      val = &g_array_index (buffers, GValue, i);
      buf = g_value_peek_pointer (val);
      fail_unless (GST_IS_BUFFER (buf));
      if (GST_BUFFER_SIZE (buf) == GST_BUFFER_SIZE (buffer)) {
        if (memcmp (GST_BUFFER_DATA (buf), GST_BUFFER_DATA (buffer),
                GST_BUFFER_SIZE (buffer)) == 0) {
          found = TRUE;
        }
      }
    }
    fail_unless (found, "Did not find incoming IN_CAPS buffer %p on caps",
        buffer);

    gst_caps_unref (caps);
  }

  return TRUE;
}

GST_START_TEST (test_multifdsink_gdp_vorbisenc)
{
  GstElement *p1, *p2;
  GstElement *src, *sink, *depay;
  GstPad *pad;
  GMainLoop *loop;
  int pfd[2];

  loop = g_main_loop_new (NULL, FALSE);

  p1 = gst_parse_launch ("audiotestsrc num-buffers=10 ! audioconvert "
      " ! vorbisenc ! gdppay ! multifdsink name=p1sink", NULL);
  fail_if (p1 == NULL);
  p2 = gst_parse_launch ("fdsrc name=p2src ! gdpdepay name=depay"
      " ! fakesink name=p2sink signal-handoffs=True", NULL);
  fail_if (p2 == NULL);

  fail_if (pipe (pfd) == -1);


  gst_element_set_state (p1, GST_STATE_READY);

  sink = gst_bin_get_by_name (GST_BIN (p1), "p1sink");
  g_signal_emit_by_name (sink, "add", pfd[1], NULL);
  gst_object_unref (sink);

  src = gst_bin_get_by_name (GST_BIN (p2), "p2src");
  g_object_set (G_OBJECT (src), "fd", pfd[0], NULL);
  gst_object_unref (src);

  depay = gst_bin_get_by_name (GST_BIN (p2), "depay");
  fail_if (depay == NULL);

  pad = gst_element_get_static_pad (depay, "src");
  fail_unless (pad != NULL, "Could not get pad out of depay");
  gst_object_unref (depay);

  gst_pad_add_event_probe (pad, G_CALLBACK (tag_event_probe_cb), loop);
  gst_pad_add_buffer_probe (pad, G_CALLBACK (buffer_probe_cb), NULL);

  gst_element_set_state (p1, GST_STATE_PLAYING);
  gst_element_set_state (p2, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  assert_equals_int (n_in_caps, 3);

  gst_element_set_state (p1, GST_STATE_NULL);
  gst_object_unref (p1);
  gst_element_set_state (p2, GST_STATE_NULL);
  gst_object_unref (p2);
}

GST_END_TEST;
#endif /* HAVE_VORBIS */

#endif /* #ifndef GST_DISABLE_PARSE */

static Suite *
streamheader_suite (void)
{
  Suite *s = suite_create ("streamheader");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
#ifndef GST_DISABLE_PARSE
  tcase_add_test (tc_chain, test_multifdsink_gdp_tag);
#ifdef HAVE_VORBIS
  tcase_add_test (tc_chain, test_multifdsink_gdp_vorbisenc);
#endif
#endif

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = streamheader_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
