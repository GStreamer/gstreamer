/* GStreamer unit tests for decodebin
 *
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <unistd.h>

const gchar dummytext[] = "Quick Brown Fox Jumps over a Lazy Frog Quick Brown "
    "Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick "
    "Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog "
    "Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy "
    "Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a "
    "Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps "
    "over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox "
    "jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown "
    "Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick "
    "Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog "
    "Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a Lazy "
    "Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps over a "
    "Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox Jumps "
    "over a Lazy Frog Quick Brown Fox Jumps over a Lazy Frog Quick Brown Fox ";

static void
src_handoff_cb (GstElement * src, GstBuffer * buf, GstPad * pad, gpointer data)
{
  GST_BUFFER_DATA (buf) = (guint8 *) dummytext;
  GST_BUFFER_SIZE (buf) = sizeof (dummytext);
  GST_BUFFER_OFFSET (buf) = 0;
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);
}

static void
decodebin_new_decoded_pad_cb (GstElement * decodebin, GstPad * pad,
    gboolean last, gboolean * p_flag)
{
  /* we should not be reached */
  fail_unless (decodebin == NULL, "new-decoded-pad should not be emitted");
}

/* make sure that decodebin errors out instead of creating a new decoded pad
 * if the entire stream is a plain text file */
GST_START_TEST (test_text_plain_streams)
{
  GstElement *pipe, *src, *decodebin;
  GstMessage *msg;

  pipe = gst_pipeline_new (NULL);
  fail_unless (pipe != NULL, "failed to create pipeline");

  src = gst_element_factory_make ("fakesrc", "src");
  fail_unless (src != NULL, "Failed to create fakesrc element");

  g_object_set (src, "signal-handoffs", TRUE, NULL);
  g_object_set (src, "num-buffers", 1, NULL);
  g_signal_connect (src, "handoff", G_CALLBACK (src_handoff_cb), NULL);

  decodebin = gst_element_factory_make ("decodebin", "decodebin");
  fail_unless (decodebin != NULL, "Failed to create decodebin element");

  g_signal_connect (decodebin, "new-decoded-pad",
      G_CALLBACK (decodebin_new_decoded_pad_cb), NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src));
  fail_unless (gst_bin_add (GST_BIN (pipe), decodebin));
  fail_unless (gst_element_link (src, decodebin), "can't link src<->decodebin");

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  /* it's push-based, so should be async */
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);

  /* it should error out at some point */
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
  gst_message_unref (msg);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static Suite *
decodebin_suite (void)
{
  Suite *s = suite_create ("decodebin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_text_plain_streams);

  return s;
}

GST_CHECK_MAIN (decodebin);
