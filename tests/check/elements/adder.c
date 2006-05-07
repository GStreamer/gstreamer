/* GStreamer
 *
 * unit test for volume
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

static GstFormat format = GST_FORMAT_UNDEFINED;
static gint64 position = -1;

static void
event_loop (GstElement * bin)
{
  GstBus *bus;
  GstMessage *message = NULL;
  GstObject *object;
  GstState oldstate, newstate, pending;
  gboolean loop = TRUE;

  bus = gst_element_get_bus (GST_ELEMENT (bin));

  while (loop) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    g_assert (message != NULL);
    object = GST_MESSAGE_SRC (message);

    GST_INFO ("bus message from \"%s\" (%s): ",
        GST_STR_NULL (GST_ELEMENT_NAME (object)),
        gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

    switch (message->type) {
      case GST_MESSAGE_EOS:
        loop = FALSE;
        break;
      case GST_MESSAGE_SEGMENT_DONE:
        gst_message_parse_segment_done (message, &format, &position);
        GST_INFO ("received segment_done : %" G_GINT64_FORMAT, position);
        gst_element_set_state (bin, GST_STATE_NULL);
        loop = FALSE;
        break;
      case GST_MESSAGE_WARNING:
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        return;
      }
      default:
        break;
    }
    gst_message_unref (message);
  }
}


GST_START_TEST (test_event)
{
  GstElement *bin, *src1, *src2, *adder, *sink;
  GstEvent *seek_event;
  gboolean res;

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  adder = gst_element_factory_make ("adder", "adder");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, src2, adder, sink, NULL);

  res = gst_element_link (src1, adder);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (src2, adder);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (adder, sink);
  fail_unless (res == TRUE, NULL);

  seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_SEGMENT,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) GST_SECOND);

  /* prepare playing */
  res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  res = gst_element_send_event (bin, seek_event);
  fail_unless (res == TRUE, NULL);

  /* run pipeline */
  res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (res != GST_STATE_CHANGE_FAILURE, NULL);

  event_loop (bin);

  fail_unless (position == GST_SECOND, NULL);

  /* cleanup */
  gst_object_unref (G_OBJECT (bin));
}

GST_END_TEST;

Suite *
adder_suite (void)
{
  Suite *s = suite_create ("adder");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_event);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = adder_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
