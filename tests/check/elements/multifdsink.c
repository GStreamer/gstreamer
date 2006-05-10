/* GStreamer
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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
#include <sys/ioctl.h>
#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include <gst/check/gstcheck.h>

GstPad *mysrcpad;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-gdp")
    );

GstElement *
setup_multifdsink ()
{
  GstElement *multifdsink;

  GST_DEBUG ("setup_multifdsink");
  multifdsink = gst_check_setup_element ("multifdsink");
  mysrcpad = gst_check_setup_src_pad (multifdsink, &srctemplate, NULL);

  return multifdsink;
}

void
cleanup_multifdsink (GstElement * multifdsink)
{
  GST_DEBUG ("cleanup_multifdsink");

  gst_check_teardown_src_pad (multifdsink);
  gst_check_teardown_element (multifdsink);
}

static void
wait_bytes_served (GstElement * sink, guint64 bytes)
{
  guint64 bytes_served = 0;

  while (bytes_served != bytes) {
    g_object_get (sink, "bytes-served", &bytes_served, NULL);
  }
}

GST_START_TEST (test_no_clients)
{
  GstElement *sink;
  GstBuffer *buffer;

  sink = setup_multifdsink ();

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  GST_DEBUG ("cleaning up multifdsink");
  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_multifdsink (sink);
}

GST_END_TEST;

GST_START_TEST (test_add_client)
{
  GstElement *sink;
  GstBuffer *buffer;
  int pfd[2];
  gchar data[4];
  guint64 bytes_served;

  sink = setup_multifdsink ();

  fail_if (pipe (pfd) == -1);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  /* add the client */
  g_signal_emit_by_name (sink, "add", pfd[1]);

  buffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (buffer), "dead", 4);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  fail_if (read (pfd[0], data, 4) < 4);
  fail_unless (strncmp (data, "dead", 4) == 0);
  wait_bytes_served (sink, 4);

  GST_DEBUG ("cleaning up multifdsink");
  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_multifdsink (sink);
}

GST_END_TEST;

static void
fail_unless_read (const char *msg, int fd, int size, const char *ref)
{
  char data[size];
  int nbytes;

  GST_DEBUG ("%s: reading %d bytes", msg, size);
  nbytes = read (fd, data, size);
  GST_DEBUG ("%s: read %d bytes", msg, nbytes);
  fail_if (nbytes < size);
  fail_unless (memcmp (data, ref, size) == 0, "data read differs from '%s'",
      ref);
}

/* this test:
 * - adds a first client
 * - sets streamheader caps on the pad
 * - pushes the IN_CAPS buffers
 * - pushes a buffer
 * - verifies that the client received all the data correctly
 * - adds a second client
 * - verifies that this second client receives the streamheader caps too
 */
GST_START_TEST (test_streamheader)
{
  GstElement *sink;
  GstBuffer *hbuf1, *hbuf2, *buf;
  GstCaps *caps;
  GstStructure *structure;
  int pfd1[2], pfd2[2];
  guint8 data[12];
  GValue array = { 0 };
  GValue value = { 0 };
  guint64 bytes_served;
  int avail;

  sink = setup_multifdsink ();

  fail_if (pipe (pfd1) == -1);
  fail_if (pipe (pfd2) == -1);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  /* add the first client */
  g_signal_emit_by_name (sink, "add", pfd1[1]);

  /* create caps with streamheader, set the caps, and push the IN_CAPS
   * buffers */
  hbuf1 = gst_buffer_new_and_alloc (4);
  GST_BUFFER_FLAG_SET (hbuf1, GST_BUFFER_FLAG_IN_CAPS);
  memcpy (GST_BUFFER_DATA (hbuf1), "babe", 4);
  hbuf2 = gst_buffer_new_and_alloc (8);
  GST_BUFFER_FLAG_SET (hbuf2, GST_BUFFER_FLAG_IN_CAPS);
  memcpy (GST_BUFFER_DATA (hbuf2), "deadbeef", 8);
  /* we want to keep them around for the tests */
  gst_buffer_ref (hbuf1);
  gst_buffer_ref (hbuf2);

  g_value_init (&array, GST_TYPE_ARRAY);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, hbuf1);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, hbuf2);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  caps = gst_caps_from_string ("application/x-gst-check");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

  fail_unless (gst_pad_push (mysrcpad, hbuf1) == GST_FLOW_OK);
  fail_unless (gst_pad_push (mysrcpad, hbuf2) == GST_FLOW_OK);

  /* verify this hasn't triggered a write yet */
  /* FIXME: possibly racy, since if it would write, we may not get it
   * immediately ? */
  fail_if (ioctl (pfd1[0], FIONREAD, &avail) < 0);
  fail_if (avail > 0);

  /* push a non-IN_CAPS buffer, this should trigger the client receiving the
   * first three buffers */
  buf = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (buf), "f00d", 4);
  gst_pad_push (mysrcpad, buf);

  fail_unless_read ("first client", pfd1[0], 4, "babe");
  fail_unless_read ("first client", pfd1[0], 8, "deadbeef");
  fail_unless_read ("first client", pfd1[0], 4, "f00d");
  wait_bytes_served (sink, 16);

  /* now add the second client */
  g_signal_emit_by_name (sink, "add", pfd2[1]);
  fail_if (ioctl (pfd2[0], FIONREAD, &avail) < 0);
  fail_if (avail > 0);

  /* now push another buffer, which will trigger streamheader for second
   * client */
  buf = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (buf), "deaf", 4);
  gst_pad_push (mysrcpad, buf);

  fail_unless_read ("first client", pfd1[0], 4, "deaf");

  fail_unless_read ("second client", pfd2[0], 4, "babe");
  fail_unless_read ("second client", pfd2[0], 8, "deadbeef");
  /* we missed the f00d buffer */
  fail_unless_read ("second client", pfd2[0], 4, "deaf");
  wait_bytes_served (sink, 36);

  GST_DEBUG ("cleaning up multifdsink");
  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_multifdsink (sink);
}

GST_END_TEST;


Suite *
multifdsink_suite (void)
{
  Suite *s = suite_create ("multifdsink");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_no_clients);
  tcase_add_test (tc_chain, test_add_client);
  tcase_add_test (tc_chain, test_streamheader);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = multifdsink_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
