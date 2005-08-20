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

GstFlowReturn
chain_func (GstPad * pad, GstBuffer * buffer)
{
  GST_DEBUG ("chain_func: received buffer %p", buffer);
  buffers = g_list_append (buffers, buffer);

  return GST_FLOW_OK;
}

gboolean
event_func (GstPad * pad, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    /* we take the lock here because it's good practice to so, even though
     * no buffers will be pushed anymore anyway */
    GST_STREAM_LOCK (pad);
    have_eos = TRUE;
    GST_STREAM_UNLOCK (pad);
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
  GstPad *srcpad, *sinkpad;

  GST_DEBUG ("setup_identity");

  identity = gst_element_factory_make ("identity", "identity");
  fail_if (identity == NULL, "Could not create a identity");

  /* sending pad */
  mysrcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  fail_if (mysrcpad == NULL, "Could not create a mysrcpad");
  ASSERT_OBJECT_REFCOUNT (mysrcpad, "mysrcpad", 1);

  sinkpad = gst_element_get_pad (identity, "sink");
  fail_if (sinkpad == NULL, "Could not get source pad from identity");
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_pad_set_caps (mysrcpad, NULL);
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and identity sink pads");
  gst_object_unref (sinkpad);   /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  /* receiving pad */
  mysinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  fail_if (mysinkpad == NULL, "Could not create a mysinkpad");

  srcpad = gst_element_get_pad (identity, "src");
  fail_if (srcpad == NULL, "Could not get source pad from identity");
  gst_pad_set_caps (mysinkpad, NULL);
  gst_pad_set_chain_function (mysinkpad, chain_func);
  gst_pad_set_event_function (mysinkpad, event_func);

  fail_unless (gst_pad_link (srcpad, mysinkpad) == GST_PAD_LINK_OK,
      "Could not link identity source and mysink pads");
  gst_object_unref (srcpad);    /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  return identity;
}

void
cleanup_identity (GstElement * identity)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG ("cleanup_identity");

  fail_unless (gst_element_set_state (identity, GST_STATE_NULL) ==
      GST_STATE_SUCCESS, "could not set to null");
  ASSERT_OBJECT_REFCOUNT (identity, "identity", 1);

  /* clean up floating src pad */
  sinkpad = gst_element_get_pad (identity, "sink");
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);

  gst_pad_unlink (mysrcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (mysrcpad, "srcpad", 1);
  gst_object_unref (mysrcpad);
  mysrcpad = NULL;

  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  /* one more ref is held by identity itself */

  /* clean up floating sink pad */
  srcpad = gst_element_get_pad (identity, "src");
  gst_pad_unlink (srcpad, mysinkpad);

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  /* one more ref is held by identity itself */

  ASSERT_OBJECT_REFCOUNT (mysinkpad, "mysinkpad", 1);
  gst_object_unref (mysinkpad);
  mysinkpad = NULL;

  ASSERT_OBJECT_REFCOUNT (identity, "identity", 1);
  gst_object_unref (identity);
}

GST_START_TEST (test_one_buffer)
{
  GstElement *identity;
  GstBuffer *buffer;

  identity = setup_identity ();
  fail_unless (gst_element_set_state (identity,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  buffer = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  memcpy (GST_BUFFER_DATA (buffer), "data", 4);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);
  /* ... but it ends up being collected on the global buffer list */
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
