/* GStreamer
 *
 * unit test for y4menc
 *
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define VIDEO_CAPS_STRING "video/x-raw-yuv, " \
                           "width = (int) 384, " \
                           "height = (int) 288, " \
                           "framerate = (fraction) 25/1, " \
                           "pixel-aspect-ratio = (fraction) 1/1"

#define Y4M_CAPS_STRING "application/x-yuv4mpeg, " \
                        "y4mversion = (int) 2"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (Y4M_CAPS_STRING));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));


static GstElement *
setup_y4menc (void)
{
  GstElement *y4menc;

  GST_DEBUG ("setup_y4menc");
  y4menc = gst_check_setup_element ("y4menc");
  mysrcpad = gst_check_setup_src_pad (y4menc, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (y4menc, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return y4menc;
}

static void
cleanup_y4menc (GstElement * y4menc)
{
  GST_DEBUG ("cleanup_y4menc");
  gst_element_set_state (y4menc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (y4menc);
  gst_check_teardown_sink_pad (y4menc);
  gst_check_teardown_element (y4menc);
}

GST_START_TEST (test_y4m)
{
  GstElement *y4menc;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int i, num_buffers, size;
  const gchar *data0 = "YUV4MPEG2 W384 H288 Ip F25:1 A1:1\nFRAME\n";

  y4menc = setup_y4menc ();
  fail_unless (gst_element_set_state (y4menc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* corresponds to I420 buffer for the size mentioned in the caps */
  size = 384 * 288 * 3 / 2;
  inbuffer = gst_buffer_new_and_alloc (size);
  /* makes valgrind's memcheck happier */
  memset (GST_BUFFER_DATA (inbuffer), 0, GST_BUFFER_SIZE (inbuffer));
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers == 1);

  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    switch (i) {
      case 0:
        fail_unless (strlen (data0) == 40);
        fail_unless (GST_BUFFER_SIZE (outbuffer) == size + 40);
        fail_unless (memcmp (data0, GST_BUFFER_DATA (outbuffer),
                strlen (data0)) == 0);
        break;
      default:
        break;
    }
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_y4menc (y4menc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;

static Suite *
y4menc_suite (void)
{
  Suite *s = suite_create ("y4menc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_y4m);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = y4menc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
