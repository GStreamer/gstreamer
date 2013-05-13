/* GStreamer
 *
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * xingmux.c: Unit test for the xingmux element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>

#include <math.h>

#include "xingmux_testdata.h"

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1," "layer = (int) 3")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1," "layer = (int) 3")
    );

GstElement *
setup_xingmux ()
{
  GstElement *xingmux;
  GstCaps *caps;

  GST_DEBUG ("setup_xingmux");
  xingmux = gst_check_setup_element ("xingmux");
  mysrcpad = gst_check_setup_src_pad (xingmux, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (xingmux, &sinktemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  caps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
  gst_check_setup_events (mysrcpad, xingmux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  return xingmux;
}

void
cleanup_xingmux (GstElement * xingmux)
{
  GST_DEBUG ("cleanup_xingmux");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (xingmux);
  gst_check_teardown_sink_pad (xingmux);
  gst_check_teardown_element (xingmux);
}

GST_START_TEST (test_xing_remux)
{
  GstElement *xingmux;
  GstBuffer *inbuffer;
  GList *it;
  const guint8 *verify_data;

  xingmux = setup_xingmux ();

  fail_unless (gst_element_set_state (xingmux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_xing));
  gst_buffer_fill (inbuffer, 0, test_xing, sizeof (test_xing));

  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 93);

  verify_data = test_xing;
  for (it = buffers; it != NULL; it = it->next) {
    GstBuffer *outbuffer = (GstBuffer *) it->data;
    GstMapInfo map;

    gst_buffer_map (outbuffer, &map, GST_MAP_READ);

    if (it == buffers) {
      gint j;

      /* Empty Xing header, should be the same as input data until the "Xing" marker
       * and zeroes afterwards. */
      fail_unless (memcmp (map.data, test_xing, 25) == 0);
      for (j = 26; j < map.size; j++)
        fail_unless (map.data[j] == 0);
      verify_data += map.size;
    } else if (it->next != NULL) {
      /* Should contain the raw MP3 data without changes */
      fail_unless (memcmp (map.data, verify_data, map.size) == 0);
      verify_data += map.size;
    } else {
      /* Last buffer is the rewrite of the first buffer and should be exactly the same
       * as the old Xing header we had */
      fail_unless (memcmp (test_xing, map.data, map.size) == 0);
    }
    gst_buffer_unmap (outbuffer, &map);
  }

  /* cleanup */
  cleanup_xingmux (xingmux);
}

GST_END_TEST;

Suite *
xingmux_suite (void)
{
  Suite *s = suite_create ("xingmux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_xing_remux);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = xingmux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
