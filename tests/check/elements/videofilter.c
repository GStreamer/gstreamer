/* GStreamer
 *
 * unit test for videofilter elements
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
#include <stdarg.h>

#include <gst/video/video.h>
#include <gst/check/gstcheck.h>

gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define VIDEO_CAPS_TEMPLATE_STRING \
  GST_VIDEO_CAPS_YUV ("I420") ";" \
  GST_VIDEO_CAPS_YUV ("AYUV") ";" \
  GST_VIDEO_CAPS_YUV ("YUY2") ";" \
  GST_VIDEO_CAPS_YUV ("UYVY") ";" \
  GST_VIDEO_CAPS_YUV ("YVYU") ";" \
  GST_VIDEO_CAPS_xRGB

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_TEMPLATE_STRING)
    );

/* takes over reference for outcaps */
static GstElement *
setup_filter (const gchar * name, const gchar * prop, va_list var_args)
{
  GstElement *element;

  GST_DEBUG ("setup_element");
  element = gst_check_setup_element (name);
  g_object_set_valist (G_OBJECT (element), prop, var_args);
  mysrcpad = gst_check_setup_src_pad (element, &srctemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  mysinkpad = gst_check_setup_sink_pad (element, &sinktemplate, NULL);
  gst_pad_set_active (mysinkpad, TRUE);

  return element;
}

static void
cleanup_filter (GstElement * filter)
{
  GST_DEBUG ("cleanup_element");

  gst_check_teardown_src_pad (filter);
  gst_check_teardown_sink_pad (filter);
  gst_check_teardown_element (filter);
}

static void
check_filter_caps (const gchar * name, GstCaps * caps, gint size,
    gint num_buffers, const gchar * prop, va_list varargs)
{
  GstElement *filter;
  GstBuffer *inbuffer, *outbuffer;
  gint i;

  filter = setup_filter (name, prop, varargs);
  fail_unless (gst_element_set_state (filter,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  for (i = 0; i < num_buffers; ++i) {
    inbuffer = gst_buffer_new_and_alloc (size);
    /* makes valgrind's memcheck happier */
    memset (GST_BUFFER_DATA (inbuffer), 0, GST_BUFFER_SIZE (inbuffer));
    gst_buffer_set_caps (inbuffer, caps);
    GST_BUFFER_TIMESTAMP (inbuffer) = 0;
    ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
    fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  }

  fail_unless (g_list_length (buffers) == num_buffers);

  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    switch (i) {
      case 0:
        fail_unless (GST_BUFFER_SIZE (outbuffer) == size);
        /* no check on filter operation itself */
        break;
      default:
        break;
    }
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_filter (filter);
  g_list_free (buffers);
  buffers = NULL;
}

static void
check_filter (const gchar * name, gint num_buffers, const gchar * prop, ...)
{
  static const struct
  {
    const int width, height;
  } resolutions[] = { {
  384, 288}, {
  385, 289}, {
  385, 385}};
  gint i, n, r;
  GstVideoFormat format;
  gint size;
  GstCaps *templ = gst_caps_from_string (VIDEO_CAPS_TEMPLATE_STRING);
  va_list varargs;

  n = gst_caps_get_size (templ);

  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (templ, i);
    GstCaps *caps = gst_caps_new_empty ();

    gst_caps_append_structure (caps, gst_structure_copy (s));

    /* try various resolutions */
    for (r = 0; r < G_N_ELEMENTS (resolutions); ++r) {
      caps = gst_caps_make_writable (caps);
      gst_caps_set_simple (caps, "width", G_TYPE_INT, resolutions[r].width,
          "height", G_TYPE_INT, resolutions[r].height,
          "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

      GST_DEBUG ("Testing with caps: %" GST_PTR_FORMAT, caps);
      gst_video_format_parse_caps (caps, &format, NULL, NULL);
      size = gst_video_format_get_size (format, resolutions[r].width,
          resolutions[r].height);

      va_start (varargs, prop);
      check_filter_caps (name, caps, size, num_buffers, prop, varargs);
      va_end (varargs);
    }

    gst_caps_unref (caps);
  }

  gst_caps_unref (templ);
}

GST_START_TEST (test_videobalance)
{
  check_filter ("videobalance", 2, NULL);
  check_filter ("videobalance", 2, "saturation", 0.5, "hue", 0.8, NULL);
}

GST_END_TEST;


GST_START_TEST (test_videoflip)
{
  /* these we can handle with the caps */
  check_filter ("videoflip", 2, "method", 0, NULL);
  check_filter ("videoflip", 2, "method", 2, NULL);
  check_filter ("videoflip", 2, "method", 4, NULL);
  check_filter ("videoflip", 2, "method", 5, NULL);
}

GST_END_TEST;

GST_START_TEST (test_gamma)
{
  check_filter ("gamma", 2, NULL);
  check_filter ("gamma", 2, "gamma", 2.0, NULL);
}

GST_END_TEST;


static Suite *
videofilter_suite (void)
{
  Suite *s = suite_create ("videofilter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_videobalance);
  tcase_add_test (tc_chain, test_videoflip);
  tcase_add_test (tc_chain, test_gamma);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = videofilter_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
