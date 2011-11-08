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
static GstPad *src, *sink;

static void
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
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  MAIN_START_THREADS (5, thread_link_unlink, NULL);
  for (i = 0; i < 1000; ++i) {
    gst_pad_is_linked (src);
    gst_pad_is_linked (sink);
    THREAD_SWITCH ();
  }
  MAIN_STOP_THREADS ();

  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);
  gst_caps_unref (caps);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  gst_object_unref (src);
  gst_object_unref (sink);
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

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");

  sink = gst_pad_new ("sink", GST_PAD_SINK);
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
  GstCaps *any = GST_CAPS_ANY;

  new = gst_pad_template_new (name, GST_PAD_SRC, presence, any);
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
  gulong id;

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

  buffer = gst_buffer_new ();
#if 0
  /* FIXME, new pad should be flushing */
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_WRONG_STATE);
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_chain (sink, buffer) == GST_FLOW_WRONG_STATE);
#endif

  /* activate pads */
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  /* test */
  /* pushing on a linked pad will drop the ref to the buffer */
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

GST_START_TEST (test_push_linked_flushing)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;
  GstBuffer *buffer;
  gulong id;

  /* setup */
  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);
  caps = gst_pad_get_allowed_caps (sink);
  fail_unless (caps == NULL);

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

  /* not activating the pads here, which keeps them flushing */

  /* pushing on a flushing pad will drop the buffer */
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_WRONG_STATE);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  gst_buffer_unref (buffer);

  /* adding a probe that returns FALSE will drop the buffer without trying
   * to chain */
  id = gst_pad_add_buffer_probe (src, (GCallback) _probe_handler,
      GINT_TO_POINTER (0));
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  gst_buffer_unref (buffer);
  gst_pad_remove_buffer_probe (src, id);

  /* adding a probe that returns TRUE will still chain the buffer,
   * and hence drop because pad is flushing */
  id = gst_pad_add_buffer_probe (src, (GCallback) _probe_handler,
      GINT_TO_POINTER (1));
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_WRONG_STATE);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  gst_buffer_unref (buffer);
  gst_pad_remove_buffer_probe (src, id);


  /* cleanup */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  gst_pad_link (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

static GstBuffer *
buffer_from_string (const gchar * str)
{
  guint size;
  GstBuffer *buf;

  size = strlen (str);
  buf = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buf), str, size);
  GST_BUFFER_SIZE (buf) = size;

  return buf;
}

GST_START_TEST (test_push_buffer_list_compat)
{
  GstPad *src, *sink;
  GstPadLinkReturn plr;
  GstCaps *caps;
  GstBufferList *list;
  GstBufferListIterator *it;
  GstBuffer *buffer;

  /* setup */
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);
  /* leave chainlistfunc unset */

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  list = gst_buffer_list_new ();

  /* activate pads */
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  /* test */
  /* adding to a buffer list will drop the ref to the buffer */
  it = gst_buffer_list_iterate (list);
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add (it, buffer_from_string ("List"));
  gst_buffer_list_iterator_add (it, buffer_from_string ("Group"));
  gst_buffer_list_iterator_add_group (it);
  gst_buffer_list_iterator_add (it, buffer_from_string ("Another"));
  gst_buffer_list_iterator_add (it, buffer_from_string ("List"));
  gst_buffer_list_iterator_add (it, buffer_from_string ("Group"));
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_pad_push_list (src, list) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 2);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), "ListGroup", 9) == 0);
  gst_buffer_unref (buffer);
  buffers = g_list_delete_link (buffers, buffers);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), "AnotherListGroup", 16) == 0);
  gst_buffer_unref (buffer);
  buffers = g_list_delete_link (buffers, buffers);
  fail_unless (buffers == NULL);

  /* teardown */
  gst_pad_unlink (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_flowreturn)
{
  GstFlowReturn ret;
  GQuark quark;

  /* test some of the macros */
  ret = GST_FLOW_UNEXPECTED;
  fail_if (strcmp (gst_flow_get_name (ret), "unexpected"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "unexpected"));

  ret = GST_FLOW_RESEND;
  fail_if (strcmp (gst_flow_get_name (ret), "resend"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "resend"));

  /* custom returns */
  ret = GST_FLOW_CUSTOM_SUCCESS;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-success"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-success"));

  ret = GST_FLOW_CUSTOM_ERROR;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-error"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-error"));

  /* custom returns clamping */
  ret = GST_FLOW_CUSTOM_SUCCESS + 2;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-success"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-success"));

  ret = GST_FLOW_CUSTOM_ERROR - 2;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-error"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-error"));

  /* unknown values */
  ret = GST_FLOW_CUSTOM_ERROR + 2;
  fail_if (strcmp (gst_flow_get_name (ret), "unknown"));
  quark = gst_flow_to_quark (ret);
  fail_unless (quark == 0);
}

GST_END_TEST;

GST_START_TEST (test_push_negotiation)
{
  GstPad *src, *sink;
  GstPadLinkReturn plr;
  GstCaps *srccaps =
      gst_caps_from_string ("audio/x-raw-int,width={16,32},depth={16,32}");
  GstCaps *sinkcaps =
      gst_caps_from_string ("audio/x-raw-int,width=32,depth={16,32}");
  GstPadTemplate *src_template;
  GstPadTemplate *sink_template;
  GstCaps *caps;
  GstBuffer *buffer;

  /* setup */
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, srccaps);
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);

  sink = gst_pad_new_from_template (sink_template, "sink");
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  src = gst_pad_new_from_template (src_template, "src");
  fail_if (src == NULL);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  buffer = gst_buffer_new ();

  /* activate pads */
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  caps = gst_caps_from_string ("audio/x-raw-int,width=16,depth=16");

  /* Should fail if src pad caps are incompatible with sink pad caps */
  gst_pad_set_caps (src, caps);
  gst_buffer_set_caps (buffer, caps);
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_NOT_NEGOTIATED);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);

  /* teardown */
  gst_pad_unlink (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
  gst_caps_unref (caps);
  gst_object_unref (sink_template);
  gst_object_unref (src_template);
}

GST_END_TEST;

/* see that an unref also unlinks the pads */
GST_START_TEST (test_src_unref_unlink)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  /* unref the srcpad */
  gst_object_unref (src);

  /* sink should be unlinked now */
  fail_if (gst_pad_is_linked (sink));

  /* cleanup */
  gst_object_unref (sink);
  gst_caps_unref (caps);
}

GST_END_TEST;

/* see that an unref also unlinks the pads */
GST_START_TEST (test_sink_unref_unlink)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_caps (src, caps);
  gst_pad_set_caps (sink, caps);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  /* unref the sinkpad */
  gst_object_unref (sink);

  /* src should be unlinked now */
  fail_if (gst_pad_is_linked (src));

  /* cleanup */
  gst_object_unref (src);
  gst_caps_unref (caps);
}

GST_END_TEST;

/* gst_pad_get_caps should return a copy of the caps */
GST_START_TEST (test_get_caps_must_be_copy)
{
  GstPad *pad;
  GstCaps *caps;
  GstPadTemplate *templ;

  caps = gst_caps_new_any ();
  templ =
      gst_pad_template_new ("test_templ", GST_PAD_SRC, GST_PAD_ALWAYS, caps);

  pad = gst_pad_new_from_template (templ, NULL);
  fail_unless (GST_PAD_CAPS (pad) == NULL, "caps present on pad");
  /* This is a writable copy ! */
  caps = gst_pad_get_caps (pad);

  /* we must own the caps */
  ASSERT_OBJECT_REFCOUNT (caps, "caps", 1);

  /* cleanup */
  gst_object_unref (templ);
  gst_caps_unref (caps);
  gst_object_unref (pad);
}

GST_END_TEST;

static void
unblock_async_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  /* here we should have blocked == 1 unblocked == 0 */
  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == FALSE);

  bool_user_data[1] = TRUE;
}

static void
block_async_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  /* here we should have blocked == 0 unblocked == 0 */
  fail_unless (bool_user_data[0] == FALSE);
  fail_unless (bool_user_data[1] == FALSE);

  bool_user_data[0] = blocked;

  gst_pad_set_blocked_async (pad, FALSE, unblock_async_cb, user_data);
}

GST_START_TEST (test_block_async)
{
  GstPad *pad;
  /* we set data[0] = TRUE when the pad is blocked, data[1] = TRUE when it's
   * unblocked */
  gboolean data[2] = { FALSE, FALSE };

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);

  gst_pad_set_active (pad, TRUE);
  gst_pad_set_blocked_async (pad, TRUE, block_async_cb, &data);

  fail_unless (data[0] == FALSE);
  fail_unless (data[1] == FALSE);
  gst_pad_push (pad, gst_buffer_new ());

  gst_object_unref (pad);
}

GST_END_TEST;

#if 0
static void
block_async_second (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gst_pad_set_blocked_async (pad, FALSE, unblock_async_cb, NULL);
}

static void
block_async_first (GstPad * pad, gboolean blocked, gpointer user_data)
{
  static int n_calls = 0;
  gboolean *bool_user_data = (gboolean *) user_data;

  if (++n_calls > 1)
    /* we expect this callback to be called only once */
    g_warn_if_reached ();

  *bool_user_data = blocked;

  /* replace block_async_first with block_async_second so next time the pad is
   * blocked the latter should be called */
  gst_pad_set_blocked_async (pad, TRUE, block_async_second, NULL);

  /* unblock temporarily, in the next push block_async_second should be called
   */
  gst_pad_push_event (pad, gst_event_new_flush_start ());
}

GST_START_TEST (test_block_async_replace_callback)
{
  GstPad *pad;
  gboolean blocked;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  gst_pad_set_blocked_async (pad, TRUE, block_async_first, &blocked);
  blocked = FALSE;

  gst_pad_push (pad, gst_buffer_new ());
  fail_unless (blocked == TRUE);
  /* block_async_first flushes to unblock */
  gst_pad_push_event (pad, gst_event_new_flush_stop ());

  /* push again, this time block_async_second should be called */
  gst_pad_push (pad, gst_buffer_new ());
  fail_unless (blocked == TRUE);

  gst_object_unref (pad);
}

GST_END_TEST;
#endif

static void
block_async_full_destroy (gpointer user_data)
{
  gint *state = (gint *) user_data;

  fail_unless (*state < 2);

  GST_DEBUG ("setting state to 2");
  *state = 2;
}

static void
block_async_full_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
  *(gint *) user_data = (gint) blocked;

  gst_pad_push_event (pad, gst_event_new_flush_start ());
  GST_DEBUG ("setting state to 1");
}

GST_START_TEST (test_block_async_full_destroy)
{
  GstPad *pad;
  /* 0 = unblocked, 1 = blocked, 2 = destroyed */
  gint state = 0;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  gst_pad_set_blocked_async_full (pad, TRUE, block_async_full_cb,
      &state, block_async_full_destroy);
  fail_unless (state == 0);

  gst_pad_push (pad, gst_buffer_new ());
  /* block_async_full_cb sets state to 1 and then flushes to unblock temporarily
   */
  fail_unless (state == 1);
  gst_pad_push_event (pad, gst_event_new_flush_stop ());

  /* pad was already blocked so nothing happens */
  gst_pad_set_blocked_async_full (pad, TRUE, block_async_full_cb,
      &state, block_async_full_destroy);
  fail_unless (state == 1);

  /* unblock with the same data, callback is called */
  gst_pad_set_blocked_async_full (pad, FALSE, block_async_full_cb,
      &state, block_async_full_destroy);
  fail_unless (state == 2);

  /* block with the same data, callback is called */
  state = 1;
  gst_pad_set_blocked_async_full (pad, TRUE, block_async_full_cb,
      &state, block_async_full_destroy);
  fail_unless (state == 2);

  /* now change user_data (to NULL in this case) so destroy_notify should be
   * called */
  state = 1;
  gst_pad_set_blocked_async_full (pad, FALSE, block_async_full_cb,
      NULL, block_async_full_destroy);
  fail_unless (state == 2);

  gst_object_unref (pad);
}

GST_END_TEST;

GST_START_TEST (test_block_async_full_destroy_dispose)
{
  GstPad *pad;
  /* 0 = unblocked, 1 = blocked, 2 = destroyed */
  gint state = 0;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  gst_pad_set_blocked_async_full (pad, TRUE, block_async_full_cb,
      &state, block_async_full_destroy);

  gst_pad_push (pad, gst_buffer_new ());
  /* block_async_full_cb sets state to 1 and then flushes to unblock temporarily
   */
  fail_unless_equals_int (state, 1);
  gst_pad_push_event (pad, gst_event_new_flush_stop ());

  /* gst_pad_dispose calls the destroy_notify function if necessary */
  gst_object_unref (pad);

  fail_unless_equals_int (state, 2);
}

GST_END_TEST;


static void
unblock_async_no_flush_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  /* here we should have blocked == 1 unblocked == 0 */

  fail_unless (blocked == FALSE);

  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == TRUE);
  fail_unless (bool_user_data[2] == FALSE);

  bool_user_data[2] = TRUE;
}


static void
unblock_async_not_called (GstPad * pad, gboolean blocked, gpointer user_data)
{
  g_warn_if_reached ();
}

static void
block_async_second_no_flush (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  fail_unless (blocked == TRUE);

  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == FALSE);
  fail_unless (bool_user_data[2] == FALSE);

  bool_user_data[1] = TRUE;

  fail_unless (gst_pad_set_blocked_async (pad, FALSE, unblock_async_no_flush_cb,
          user_data));
}

static void
block_async_first_no_flush (GstPad * pad, gboolean blocked, gpointer user_data)
{
  static int n_calls = 0;
  gboolean *bool_user_data = (gboolean *) user_data;

  fail_unless (blocked == TRUE);

  if (++n_calls > 1)
    /* we expect this callback to be called only once */
    g_warn_if_reached ();

  *bool_user_data = blocked;

  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == FALSE);
  fail_unless (bool_user_data[2] == FALSE);

  fail_unless (gst_pad_set_blocked_async (pad, FALSE, unblock_async_not_called,
          NULL));

  /* replace block_async_first with block_async_second so next time the pad is
   * blocked the latter should be called */
  fail_unless (gst_pad_set_blocked_async (pad, TRUE,
          block_async_second_no_flush, user_data));
}

GST_START_TEST (test_block_async_replace_callback_no_flush)
{
  GstPad *pad;
  gboolean bool_user_data[3] = { FALSE, FALSE, FALSE };

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  fail_unless (gst_pad_set_blocked_async (pad, TRUE, block_async_first_no_flush,
          bool_user_data));

  gst_pad_push (pad, gst_buffer_new ());
  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == TRUE);
  fail_unless (bool_user_data[2] == TRUE);

  gst_object_unref (pad);
}

GST_END_TEST;


static Suite *
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
  tcase_add_test (tc_chain, test_push_linked_flushing);
  tcase_add_test (tc_chain, test_push_buffer_list_compat);
  tcase_add_test (tc_chain, test_flowreturn);
  tcase_add_test (tc_chain, test_push_negotiation);
  tcase_add_test (tc_chain, test_src_unref_unlink);
  tcase_add_test (tc_chain, test_sink_unref_unlink);
  tcase_add_test (tc_chain, test_get_caps_must_be_copy);
  tcase_add_test (tc_chain, test_block_async);
#if 0
  tcase_add_test (tc_chain, test_block_async_replace_callback);
#endif
  tcase_add_test (tc_chain, test_block_async_full_destroy);
  tcase_add_test (tc_chain, test_block_async_full_destroy_dispose);
  tcase_add_test (tc_chain, test_block_async_replace_callback_no_flush);

  return s;
}

GST_CHECK_MAIN (gst_pad);
