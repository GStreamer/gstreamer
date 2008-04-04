/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
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

/* FIXME: why are the fingerprints different depending on endianness? */
static const gchar *fingerprint_le =
    "AQATABQAFwAbACIALQBHAI1//QDQAGYAOAAoAB8AGAAVABIAEAAOAA0ADAAHAAUABAADAAMAAgACAAIAAgABAAEAAQABAAEAAQABAAEAAQAB03vWCti124bd2N/y4grjsQBN5VUZAxeoFocVfhRwE5ISxBIGEUIQnxAHDLYKggjyB8kG5AYsBZQFGAS3BF8EEwPPA5EDZQM7Aw4C7QLOAq9BVS7bHvkRDwdwAAD5uvWT/8j0zREWExcUehWIFmIW6xdFF4QXpBeyF5wWLRQjEjAQfw8IDcUMpguyCvgKPwmcCQkIWwgjB8kHUQcKBswGhMA/7l0J1RkNHjEe1RyOGUv/qRcH7/X0f/h6++r/iQJ4BSUHggneC00NYBTNF30YBRerFtcVyxTEE68S2BHmEQYQRw7IDtYOWw18DRQMtgxJN13vJdld2vLlgvDf/FQENgA0Bj7vle1865rry+wf7O3sLe998TvyUvR1AEkInw3iESUS/BQ6FJsUlBRPE+4TiBOIEKMSGxHLETgQhRBXD/bU2zFHJ30MJPYi6PnmCucvAD/pMiATBx0KR/vg9ovyVe7Q7Hzp4uk05AHoSe9t9z39qAK5B2oJjwuGDYwPHA+jEgoKFhFbEUAQUBD/EJ8QuAwz7AH1zQK7ChEQ2hCgEVz/dRBAdaH0Te5395P5YfrN/HD9J/4d/qX/SwHsAkMByQE0AKP//ACMAAn/ZP8w/yn+Mv+jAEf+3QEb/rj7x/4ZMAAAAA==";

static const gchar *fingerprint_be =
    "AQhyFyId/RqhCbIHoQmfBzEXWwlBEYIKbQ3ZDaQfQwg5FuEG5xFACroabgmSGd8RkBC6GiocphhtFJQgFRLsFuQUBBJMFkYYEQy5HvkOAxxX8X4LNO0B9/3tae7SByT4HPH789cMMQEg7QTGZxdv90HuXRcvIi02Yvy3AZoeGt+x84EjSfJ0/ZsKq/e7+BMIvQ0P6s/7PwjT8p4RR/zS/L4TLAps8YMjORL29kEKpR2/DxoSXOwQAMsFniqhC/DvMMPz9ejyFAx/6J0AsQhe65oPnwfi/3T2PSyx9ckL3Acd6mPo/fnu/xTohQSN7U8ErNvB63kIVA7o1HHqC+FAIt7/VRGS7En6PtSdEawbXA5d/kz1XgCBBOf1iwmL/qf2U+7WAJUUCBpmEQYZtgGx7SwHVwo7AnwCDhHT6hgRsdkRAc3bPQydCp3/pex7EMUJf/jg19oqthu677UM/+qZ5AwAHvdc6PkQKAQSDwcRtwZLB0Hv4wVJ9+UBh+/zDU77JPLM66YLP/vA/WAVvkH48pn8Th7mBhIadervG074ye5P/q8B9/rLDLkJqAlXB37oYtnM0YE6NfyWF7Lm1OvnCsP/bwXh7mkIkOzd7un5FPdbBFb8IgSf6dgILPt1JuD4tCUXAD/3lge/BOj95SJL740G5gwu81caze6l4TH7Oez57jEqRQdC9RLyuee4+an94hGg3I4EXwJrITcMbPGj/foRmxjcHLIKRBt/3RQS8u0/PjlFOw==";

static gboolean found_fingerprint = FALSE;
static gboolean big_endian = FALSE;

static gboolean
bus_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING:
    case GST_MESSAGE_ERROR:{
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      gst_message_unref (message);
      g_error_free (gerror);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_TAG:
    {
      GstTagList *tag_list;
      gchar *fpr;

      gst_message_parse_tag (message, &tag_list);

      fail_unless (gst_tag_list_get_string (tag_list, "ofa-fingerprint", &fpr));

      if (big_endian) {
        fail_unless_equals_string (fpr, fingerprint_be);
      } else {
        fail_unless_equals_string (fpr, fingerprint_le);
      }

      found_fingerprint = TRUE;

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

GST_START_TEST (test_ofa_le_1ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;

  GstBus *bus;
  GMainLoop *loop;

  GstCaps *caps;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, 44100,
      "channels", G_TYPE_INT, 1,
      "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  big_endian = FALSE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
}

GST_END_TEST;


GST_START_TEST (test_ofa_be_1ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;

  GstBus *bus;
  GMainLoop *loop;

  GstCaps *caps;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, 44100,
      "channels", G_TYPE_INT, 1,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  big_endian = TRUE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
}

GST_END_TEST;

GST_START_TEST (test_ofa_le_2ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;

  GstBus *bus;
  GMainLoop *loop;

  GstCaps *caps;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, 44100,
      "channels", G_TYPE_INT, 2,
      "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  big_endian = FALSE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
}

GST_END_TEST;


GST_START_TEST (test_ofa_be_2ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;

  GstBus *bus;
  GMainLoop *loop;

  GstCaps *caps;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, 44100,
      "channels", G_TYPE_INT, 2,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  big_endian = TRUE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
}

GST_END_TEST;

Suite *
ofa_suite (void)
{
  Suite *s = suite_create ("OFA");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 120s, not the default 3 */
  tcase_set_timeout (tc_chain, 120);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ofa_le_1ch);
  tcase_add_test (tc_chain, test_ofa_be_1ch);
  tcase_add_test (tc_chain, test_ofa_le_2ch);
  tcase_add_test (tc_chain, test_ofa_be_2ch);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ofa_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
