/* GStreamer unit tests for the neonhttpsrc element
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

static void
handoff_cb (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    GstBuffer ** p_outbuf)
{
  GST_LOG ("handoff, buf = %p", buf);
  if (*p_outbuf == NULL)
    *p_outbuf = gst_buffer_ref (buf);
}

GST_START_TEST (test_first_buffer_has_offset)
{
  GstStateChangeReturn ret;
  GstElement *pipe, *src, *sink;
  GstBuffer *buf = NULL;

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("neonhttpsrc", NULL);
  fail_unless (src != NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);

  gst_bin_add (GST_BIN (pipe), src);
  gst_bin_add (GST_BIN (pipe), sink);
  fail_unless (gst_element_link (src, sink));

  g_object_set (src, "location", "http://gstreamer.freedesktop.org/", NULL);
  g_object_set (src, "automatic-redirect", TRUE, NULL);

  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "preroll-handoff", G_CALLBACK (handoff_cb), &buf);

  ret = gst_element_set_state (pipe, GST_STATE_PAUSED);
  if (ret != GST_STATE_CHANGE_ASYNC) {
    GST_DEBUG ("failed to start up neon http src, ret = %d", ret);
    goto done;
  }

  /* don't wait for more than 10 seconds */
  ret = gst_element_get_state (pipe, NULL, NULL, 10 * GST_SECOND);
  GST_LOG ("ret = %u", ret);

  if (buf == NULL) {
    /* we want to test the buffer offset, nothing else; if there's a failure
     * it might be for lots of reasons (no network connection, whatever), we're
     * not interested in those */
    GST_DEBUG ("didn't manage to get data within 10 seconds, skipping test");
    goto done;
  }

  GST_DEBUG ("buffer offset = %" G_GUINT64_FORMAT, GST_BUFFER_OFFSET (buf));

  /* first buffer should have a 0 offset */
  fail_unless (GST_BUFFER_OFFSET (buf) == 0);
  gst_buffer_unref (buf);

done:

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static Suite *
neonhttpsrc_suite (void)
{
  Suite *s = suite_create ("neonhttpsrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_first_buffer_has_offset);

  return s;
}

GST_CHECK_MAIN (neonhttpsrc);
