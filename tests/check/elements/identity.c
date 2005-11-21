/* GStreamer
 *
 * unit test for identity
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

GList *buffers = NULL;
gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

gboolean
event_func (GstPad * pad, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    have_eos = TRUE;
    gst_event_unref (event);
    return TRUE;
  }

  gst_event_unref (event);
  return FALSE;
}

GstElement *
setup_identity ()
{
  GstElement *identity;

  GST_DEBUG ("setup_identity");

  identity = gst_check_setup_element ("identity");
  mysrcpad = gst_check_setup_src_pad (identity, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (identity, &sinktemplate, NULL);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return identity;
}

void
cleanup_identity (GstElement * identity)
{
  GST_DEBUG ("cleanup_identity");

  gst_check_teardown_src_pad (identity);
  gst_check_teardown_sink_pad (identity);
  gst_check_teardown_element (identity);
}

GST_START_TEST (test_one_buffer)
{
  GstElement *identity;
  GstBuffer *buffer;

  identity = setup_identity ();
  fail_unless (gst_element_set_state (identity,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  buffer = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  memcpy (GST_BUFFER_DATA (buffer), "data", 4);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);
  /* ... but it should end up being collected on the global buffer list */
  fail_unless (g_list_length (buffers) == 1);
  fail_unless ((GstBuffer *) (g_list_first (buffers)->data) == buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);

  /* cleanup */
  cleanup_identity (identity);
}

GST_END_TEST;

Suite *
identity_suite (void)
{
  Suite *s = suite_create ("identity");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_one_buffer);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = identity_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
