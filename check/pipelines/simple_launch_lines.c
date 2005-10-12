/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * simple_launch_lines.c: Unit test for simple pipelines
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


static GstElement *
setup_pipeline (const gchar * pipe_descr)
{
  GstElement *pipeline;

  pipeline = gst_parse_launch (pipe_descr, NULL);
  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), NULL);
  return pipeline;
}

/* 
 * run_pipeline:
 * @pipe: the pipeline to run
 * @desc: the description for use in messages
 * @events: is a mask of expected events
 * @tevent: is the expected terminal event.
 *
 * the poll call will time out after half a second.
 */
static void
run_pipeline (GstElement * pipe, const gchar * descr,
    GstMessageType events, GstMessageType tevent)
{
  GstBus *bus;
  GstMessage *message;
  GstMessageType revent;
  GstStateChangeReturn ret;

  g_assert (pipe);
  bus = gst_element_get_bus (pipe);
  g_assert (bus);

  ret = gst_element_set_state (pipe, GST_STATE_PLAYING);
  ret = gst_element_get_state (pipe, NULL, NULL, GST_CLOCK_TIME_NONE);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_critical ("Couldn't set pipeline to PLAYING");
    goto done;
  }

  while (1) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);


    /* always have to pop the message before getting back into poll */
    if (message) {
      revent = GST_MESSAGE_TYPE (message);
      gst_message_unref (message);
    } else {
      revent = GST_MESSAGE_UNKNOWN;
    }

    if (revent == tevent) {
      break;
    } else if (revent == GST_MESSAGE_UNKNOWN) {
      g_critical ("Unexpected timeout in gst_bus_poll, looking for %d: %s",
          tevent, descr);
      break;
    } else if (revent & events) {
      continue;
    }
    g_critical ("Unexpected message received of type %d, looking for %d: %s",
        revent, tevent, descr);
  }

done:
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_START_TEST (test_element_negotiation)
{
  gchar *s;

  /* see http://bugzilla.gnome.org/show_bug.cgi?id=315126 */
  s = "fakesrc ! audio/x-raw-int,width=16,depth=16,rate=22050,channels=1 ! audioconvert ! audio/x-raw-int,width=16,depth=16,rate=22050,channels=1 ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);

#ifdef HAVE_LIBVISUAL
  s = "sinesrc ! tee name=t ! alsasink t. ! audioconvert ! libvisual_lv_scope ! ffmpegcolorspace ! xvimagesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);
#endif
}

GST_END_TEST
GST_START_TEST (test_basetransform_based)
{
  /* Each of these tests is to check whether various basetransform based elements can
   * select output caps when not allowed to do passthrough and going to a generic sink
   * such as fakesink or filesink */
  const gchar *s;

  /* Check that videoscale can pick a height given only a width */
  s = "videotestsrc ! video/x-raw-yuv,format=(fourcc)I420,width=320,height=240 ! " "videoscale ! video/x-raw-yuv,width=640 ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);

  /* Test that ffmpegcolorspace can pick an output format that isn't passthrough without 
   * completely specified output caps */
  s = "videotestsrc ! video/x-raw-yuv,format=(fourcc)I420,width=320,height=240 ! " "ffmpegcolorspace ! video/x-raw-rgb ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);

  /* Check that audioresample can pick a samplerate to use from a 
   * range that doesn't include the input */
  s = "sinesrc ! audio/x-raw-int,width=16,depth=16,rate=8000 ! audioresample ! "
      "audio/x-raw-int,rate=[16000,48000] ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);

  /* Check that audioconvert can pick a depth to use, given a width */
  s = "sinesrc ! audio/x-raw-int,width=16,depth=16 ! audioconvert ! "
      "audio/x-raw-int,width=32 ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_ANY & ~(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING),
      GST_MESSAGE_UNKNOWN);
}
GST_END_TEST Suite *
simple_launch_lines_suite (void)
{
  Suite *s = suite_create ("Pipelines");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
//  tcase_add_test (tc_chain, test_element_negotiation);
  tcase_add_test (tc_chain, test_basetransform_based);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = simple_launch_lines_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
