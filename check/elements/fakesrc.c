/* GStreamer
 *
 * unit test for fakesrc
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

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstFlowReturn
chain_func (GstPad * pad, GstBuffer * buffer)
{
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
setup_fakesrc ()
{
  GstElement *src;
  GstPad *srcpad, *sinkpad;

  src = gst_element_factory_make ("fakesrc", "src");
  fail_if (src == NULL, "Could not create a fakesrc");

  sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  fail_if (sinkpad == NULL, "Could not create a sinkpad");

  srcpad = gst_element_get_pad (src, "src");
  fail_if (srcpad == NULL, "Could not get source pad from fakesrc");
  gst_pad_set_caps (sinkpad, NULL);
  gst_pad_set_chain_function (sinkpad, chain_func);
  gst_pad_set_event_function (sinkpad, event_func);

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and sink pads");
  return src;
}

void
cleanup_fakesrc (GstElement * src)
{
  GstPad *srcpad, *sinkpad;

  fail_unless (gst_element_set_state (src, GST_STATE_NULL) == GST_STATE_SUCCESS,
      "could not set to null");

  srcpad = gst_element_get_pad (src, "src");
  sinkpad = gst_pad_get_peer (srcpad);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  gst_object_unref (src);

  gst_pad_unlink (srcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  gst_object_unref (srcpad);

  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  gst_object_unref (sinkpad);
}

GST_START_TEST (test_num_buffers)
{
  GstElement *src;

  src = setup_fakesrc ();
  g_object_set (G_OBJECT (src), "num-buffers", 3, NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 3);
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_empty)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 1, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_unless (GST_BUFFER_SIZE (buf) == 0);
    l = l->next;
  }
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_fixed)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 2, NULL);
  g_object_set (G_OBJECT (src), "sizemax", 8192, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_unless (GST_BUFFER_SIZE (buf) == 8192);
    l = l->next;
  }
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_random)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 3, NULL);
  g_object_set (G_OBJECT (src), "sizemin", 4096, NULL);
  g_object_set (G_OBJECT (src), "sizemax", 8192, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_if (GST_BUFFER_SIZE (buf) > 8192);
    fail_if (GST_BUFFER_SIZE (buf) < 4096);
    l = l->next;
  }
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;


Suite *
fakesrc_suite (void)
{
  Suite *s = suite_create ("fakesrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_sizetype_empty);
  tcase_add_test (tc_chain, test_sizetype_fixed);
  tcase_add_test (tc_chain, test_sizetype_random);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = fakesrc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
