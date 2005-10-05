/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstpad.c: Unit test for GstPad
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

GST_START_TEST (test_link)
{
  GstPad *src, *sink;
  GstPadTemplate *srct;

  GstPadLinkReturn ret;
  gchar *name;

  src = gst_pad_new ("source", GST_PAD_SRC);
  fail_if (src == NULL);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);

  name = gst_pad_get_name (src);
  fail_unless (strcmp (name, "source") == 0);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);
  g_free (name);

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  /* linking without templates or caps should fail */
  ret = gst_pad_link (src, sink);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink pad", 1);
  fail_unless (ret == GST_PAD_LINK_NOFORMAT);

  ASSERT_CRITICAL (gst_pad_get_pad_template (NULL));

  srct = gst_pad_get_pad_template (src);
  fail_unless (srct == NULL);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);

  /* clean up */
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

/* threaded link/unlink */
/* use globals */
GstPad *src, *sink;

void
thread_link_unlink (gpointer data)
{
  THREAD_START ();

  while (THREAD_TEST_RUNNING ()) {
    gst_pad_link (src, sink);
    gst_pad_unlink (src, sink);
    THREAD_SWITCH ();
  }
}

GST_START_TEST (test_link_unlink_threaded)
{
  GstCaps *caps;
  int i;

  src = gst_pad_new ("source", GST_PAD_SRC);
  fail_if (src == NULL);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  caps = gst_caps_from_string ("foo/bar");
  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);

  MAIN_START_THREADS (5, thread_link_unlink, NULL);
  for (i = 0; i < 1000; ++i) {
    gst_pad_is_linked (src);
    gst_pad_is_linked (sink);
    THREAD_SWITCH ();
  }
  MAIN_STOP_THREADS ();
}

GST_END_TEST;

GST_START_TEST (test_refcount)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");
  /* one for me */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);
  /* one for me and one for each set_caps */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  gst_pad_unlink (src, sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  /* cleanup */
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_get_allowed_caps)
{
  GstPad *src, *sink;
  GstCaps *caps, *gotcaps;
  GstBuffer *buffer;
  GstPadLinkReturn plr;

  ASSERT_CRITICAL (gst_pad_get_allowed_caps (NULL));

  buffer = gst_buffer_new ();
  ASSERT_CRITICAL (gst_pad_get_allowed_caps ((GstPad *) buffer));
  gst_buffer_unref (buffer);

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  ASSERT_CRITICAL (gst_pad_get_allowed_caps (sink));

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  gotcaps = gst_pad_get_allowed_caps (src);
  fail_if (gotcaps == NULL);
  fail_unless (gst_caps_is_equal (gotcaps, caps));

  ASSERT_CAPS_REFCOUNT (gotcaps, "gotcaps", 1);
  gst_caps_unref (gotcaps);

  gst_pad_unlink (src, sink);

  /* cleanup */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);

  gst_object_unref (src);
  gst_object_unref (sink);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

static gboolean
name_is_valid (const gchar * name, GstPadPresence presence)
{
  GstPadTemplate *new;

  new = gst_pad_template_new (name, GST_PAD_SRC, presence, GST_CAPS_ANY);
  if (new) {
    gst_object_unref (GST_OBJECT (new));
    return TRUE;
  }
  return FALSE;
}

GST_START_TEST (test_name_is_valid)
{
  gboolean result = FALSE;

  fail_unless (name_is_valid ("src", GST_PAD_ALWAYS));
  ASSERT_WARNING (name_is_valid ("src%", GST_PAD_ALWAYS));
  ASSERT_WARNING (result = name_is_valid ("src%d", GST_PAD_ALWAYS));
  fail_if (result);

  fail_unless (name_is_valid ("src", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%s%s", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%c", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%dsrc", GST_PAD_REQUEST));

  fail_unless (name_is_valid ("src", GST_PAD_SOMETIMES));
  fail_unless (name_is_valid ("src%c", GST_PAD_SOMETIMES));
}

GST_END_TEST;

static gboolean
_probe_handler (GstPad * pad, GstBuffer * buffer, gpointer userdata)
{
  gint ret = GPOINTER_TO_INT (userdata);

  if (ret == 1)
    return TRUE;
  return FALSE;
}

GST_START_TEST (test_push_unlinked)
{
  GstPad *src;
  GstCaps *caps;
  GstBuffer *buffer;
  gulong id;

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_caps (src, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  /* pushing on an unlinked pad will drop the buffer */
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_NOT_LINKED);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);

  /* adding a probe that returns FALSE will drop the buffer without trying
   * to chain */
  id = gst_pad_add_buffer_probe (src, (GCallback) _probe_handler,
      GINT_TO_POINTER (0));
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_buffer_probe (src, id);

  /* adding a probe that returns TRUE will still chain the buffer,
   * and hence drop because pad is unlinked */
  id = gst_pad_add_buffer_probe (src, (GCallback) _probe_handler,
      GINT_TO_POINTER (1));
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_NOT_LINKED);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_buffer_probe (src, id);


  /* cleanup */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  gst_object_unref (src);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_push_linked)
{
  GstPad *src, *sink;
  GstPadLinkReturn plr;
  GstCaps *caps;
  GstBuffer *buffer;
  ulong id;

  /* setup */
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");
  /* one for me */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);
  /* one for me and one for each set_caps */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  /* test */
  /* pushing on a linked pad will drop the ref to the buffer */
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 2);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  g_list_free (buffers);
  buffers = NULL;

  /* adding a probe that returns FALSE will drop the buffer without trying
   * to chain */
  id = gst_pad_add_buffer_probe (src, (GCallback) _probe_handler,
      GINT_TO_POINTER (0));
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_buffer_probe (src, id);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* adding a probe that returns TRUE will still chain the buffer */
  id = gst_pad_add_buffer_probe (src, (GCallback) _probe_handler,
      GINT_TO_POINTER (1));
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  gst_pad_remove_buffer_probe (src, id);

  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 2);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  g_list_free (buffers);
  buffers = NULL;

  /* teardown */
  gst_pad_unlink (src, sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_caps_unref (caps);
}

GST_END_TEST;


Suite *
gst_pad_suite (void)
{
  Suite *s = suite_create ("GstPad");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_refcount);
  tcase_add_test (tc_chain, test_get_allowed_caps);
  tcase_add_test (tc_chain, test_link_unlink_threaded);
  tcase_add_test (tc_chain, test_name_is_valid);
  tcase_add_test (tc_chain, test_push_unlinked);
  tcase_add_test (tc_chain, test_push_linked);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_pad_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
