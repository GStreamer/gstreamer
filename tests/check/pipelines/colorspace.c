/* GStreamer
 *
 * unit comparison test for colorspace
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>

static GMainLoop *loop;

static void
message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_assert_not_reached ();
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);
      const gchar *name = gst_structure_get_name (s);

      fail_unless (strcmp (name, "delta") == 0);
      break;
    }
    default:
      break;
  }
}

/* compare output with ffmpegcolorspace */
static void
colorspace_compare (gint width, gint height, gboolean comp)
{
  GstBus *bus;
  GstElement *pipeline, *src, *filter1, *filter2, *csp, *fcsp, *fakesink;
  GstElement *queue1, *queue2, *tee, *compare;
  GstCaps *caps, *tcaps, *rcaps, *fcaps;
  GstCaps *ccaps;
  GstPad *pad;

  gint i, j;

  /* create elements */
  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("videotestsrc", "videotestsrc");
  fail_unless (src != NULL);
  filter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
  fail_unless (filter1 != NULL);
  csp = gst_element_factory_make ("colorspace", "colorspace");
  fail_unless (csp != NULL);
  filter2 = gst_element_factory_make ("capsfilter", "capsfilter2");
  fail_unless (filter2 != NULL);

  if (comp) {
    fcsp = gst_element_factory_make ("ffmpegcolorspace", "ffmpegcolorspace");
    fail_unless (fcsp != NULL);
    tee = gst_element_factory_make ("tee", "tee");
    fail_unless (tee != NULL);
    queue1 = gst_element_factory_make ("queue", "queue1");
    fail_unless (queue1 != NULL);
    queue2 = gst_element_factory_make ("queue", "queue2");
    fail_unless (queue2 != NULL);
    compare = gst_element_factory_make ("compare", "compare");
    fail_unless (compare != NULL);
  } else {
    fcsp = tee = queue1 = queue2 = compare = NULL;
  }

  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (fakesink != NULL);

  /* add and link */
  gst_bin_add_many (GST_BIN (pipeline), src, filter1, filter2, csp, fakesink,
      tee, queue1, queue2, fcsp, compare, NULL);

  fail_unless (gst_element_link (src, filter1));

  if (comp) {
    fail_unless (gst_element_link (filter1, tee));

    fail_unless (gst_element_link (tee, queue1));
    fail_unless (gst_element_link (queue1, fcsp));
    fail_unless (gst_element_link_pads (fcsp, NULL, compare, "sink"));

    fail_unless (gst_element_link (tee, queue2));
    fail_unless (gst_element_link (queue2, csp));
    fail_unless (gst_element_link_pads (csp, NULL, compare, "check"));

    fail_unless (gst_element_link (compare, filter2));
  } else {
    fail_unless (gst_element_link (filter1, csp));
    fail_unless (gst_element_link (csp, filter2));
  }
  fail_unless (gst_element_link (filter2, fakesink));

  /* obtain possible caps combinations */
  if (comp) {
    pad = gst_element_get_static_pad (fcsp, "sink");
    fail_unless (pad != NULL);
    ccaps = gst_pad_get_pad_template_caps (pad);
    fail_unless (ccaps != NULL);
    fcaps = ccaps;
    gst_object_unref (pad);
  } else {
    fcaps = gst_caps_new_any ();
  }

  pad = gst_element_get_static_pad (csp, "sink");
  fail_unless (pad != NULL);
  ccaps = gst_pad_get_pad_template_caps (pad);
  fail_unless (ccaps != NULL);
  gst_object_unref (pad);

  /* handle videotestsrc limitations */
  pad = gst_element_get_static_pad (src, "src");
  fail_unless (pad != NULL);
  caps = (GstCaps *) gst_pad_get_pad_template_caps (pad);
  fail_unless (caps != NULL);
  gst_object_unref (pad);

  rcaps = gst_caps_new_simple ("video/x-raw-yuv",
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 25, 1,
      "color-matrix", G_TYPE_STRING, "sdtv",
      "chroma-site", G_TYPE_STRING, "mpeg2", NULL);
  gst_caps_append (rcaps, gst_caps_new_simple ("video/x-raw-rgb",
          "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
          "framerate", GST_TYPE_FRACTION, 25, 1,
          "depth", G_TYPE_INT, 32, NULL));

  /* FIXME also allow x-raw-gray if/when colorspace actually handles those */

  /* limit to supported compare types */
  if (comp) {
    gst_caps_append (rcaps, gst_caps_new_simple ("video/x-raw-rgb",
            "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
            "framerate", GST_TYPE_FRACTION, 25, 1,
            "depth", G_TYPE_INT, 24, NULL));
  }

  tcaps = gst_caps_intersect (fcaps, ccaps);
  gst_caps_unref (fcaps);
  gst_caps_unref (ccaps);
  caps = gst_caps_intersect (tcaps, caps);
  gst_caps_unref (tcaps);
  tcaps = caps;
  caps = gst_caps_intersect (tcaps, rcaps);
  gst_caps_unref (tcaps);
  gst_caps_unref (rcaps);

  /* normalize to finally have a list of acceptable fixed formats */
  caps = gst_caps_simplify (caps);
  caps = gst_caps_normalize (caps);

  /* set up for running stuff */
  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", (GCallback) message_cb, NULL);
  gst_object_unref (bus);

  g_object_set (src, "num-buffers", 5, NULL);
  if (comp) {
    /* set lower bound for ssim comparison, and allow slightly different caps */
    g_object_set (compare, "method", 2, NULL);
    g_object_set (compare, "meta", 3, NULL);
    g_object_set (compare, "threshold", 0.90, NULL);
    g_object_set (compare, "upper", FALSE, NULL);
  }

  GST_INFO ("possible caps to check %d", gst_caps_get_size (caps));

  /* loop over all input and output combinations */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    for (j = 0; j < gst_caps_get_size (caps); j++) {
      GstCaps *in_caps, *out_caps;
      GstStructure *s;
      const gchar *fourcc;

      in_caps = gst_caps_copy_nth (caps, i);
      out_caps = gst_caps_copy_nth (caps, j);

      /* FIXME remove if videotestsrc and video format handle these properly */
      s = gst_caps_get_structure (in_caps, 0);
      if ((fourcc = gst_structure_get_string (s, "format"))) {
        if (!strcmp (fourcc, "YUV9") ||
            !strcmp (fourcc, "YVU9") || !strcmp (fourcc, "v216")) {
          gst_caps_unref (in_caps);
          gst_caps_unref (out_caps);
          continue;
        }
      }

      GST_INFO ("checking conversion from %" GST_PTR_FORMAT " (%d)"
          " to %" GST_PTR_FORMAT " (%d)", in_caps, i, out_caps, j);

      g_object_set (filter1, "caps", in_caps, NULL);
      g_object_set (filter2, "caps", out_caps, NULL);

      fail_unless (gst_element_set_state (pipeline, GST_STATE_PLAYING)
          != GST_STATE_CHANGE_FAILURE);

      g_main_loop_run (loop);

      fail_unless (gst_element_set_state (pipeline, GST_STATE_NULL)
          == GST_STATE_CHANGE_SUCCESS);

      gst_caps_unref (in_caps);
      gst_caps_unref (out_caps);
    }
  }

  gst_caps_unref (caps);
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
}

#define WIDTH  176
#define HEIGHT 120

GST_START_TEST (test_colorspace_compare)
{
  colorspace_compare (WIDTH, HEIGHT, TRUE);
}

GST_END_TEST;

/* enable if you like stuff (ffmpegcolorspace) crashing */
#ifdef TEST_ODD

GST_START_TEST (test_colorspace_compare_odd_height)
{
  colorspace_compare (WIDTH, HEIGHT + 1, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_colorspace_compare_odd_width)
{
  colorspace_compare (WIDTH + 1, HEIGHT, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_colorspace_compare_odd)
{
  colorspace_compare (WIDTH + 1, HEIGHT + 1, TRUE);
}

GST_END_TEST;

#endif

/* useful for crash and valgrind check */

GST_START_TEST (test_colorspace)
{
  colorspace_compare (WIDTH + 1, HEIGHT + 1, FALSE);
}

GST_END_TEST;

static Suite *
colorspace_suite (void)
{
  Suite *s = suite_create ("colorspace");
  TCase *tc_chain;

  tc_chain = tcase_create ("colorspace_compare");
  tcase_add_test (tc_chain, test_colorspace_compare);
#ifdef TEST_ODD
  tcase_add_test (tc_chain, test_colorspace_compare_odd_height);
  tcase_add_test (tc_chain, test_colorspace_compare_odd_width);
  tcase_add_test (tc_chain, test_colorspace_compare_odd);
#endif
  tcase_add_test (tc_chain, test_colorspace);
  suite_add_tcase (s, tc_chain);

  /* test may take some time */
  tcase_set_timeout (tc_chain, 600);

  return s;
}

GST_CHECK_MAIN (colorspace)
