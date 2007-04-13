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

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstbufferstraw.h>

#ifndef GST_DISABLE_PARSE

/* this tests a gdp-serialized tag from audiotestsrc being sent only once
 * to clients of multifdsink */

static int n_tags = 0;

gboolean
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

  pad = gst_element_get_pad (depay, "src");
  fail_unless (pad != NULL, "Could not get pad out of depay");
  gst_object_unref (depay);

  gst_pad_add_event_probe (pad, G_CALLBACK (tag_event_probe_cb), loop);

  gst_element_set_state (p1, GST_STATE_PLAYING);
  gst_element_set_state (p2, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  assert_equals_int (n_tags, 1);
}

GST_END_TEST;

#endif /* #ifndef GST_DISABLE_PARSE */

Suite *
streamheader_suite (void)
{
  Suite *s = suite_create ("streamheader");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
#ifndef GST_DISABLE_PARSE
  tcase_add_test (tc_chain, test_multifdsink_gdp_tag);
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
