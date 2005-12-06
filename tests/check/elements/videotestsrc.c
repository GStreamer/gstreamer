/* GStreamer
 *
 * unit test for videotestsrc
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

GList *buffers = NULL;
gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysinkpad;


#define CAPS_TEMPLATE_STRING            \
    "video/x-raw-yuv, "                 \
    "format = (fourcc) Y422, "          \
    "width = (int) [ 1,  MAX ], "       \
    "height = (int) [ 1,  MAX ], "      \
    "framerate = (fraction) [ 0/1, MAX ]"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_TEMPLATE_STRING)
    );

GstElement *
setup_videotestsrc ()
{
  GstElement *videotestsrc;

  GST_DEBUG ("setup_videotestsrc");
  videotestsrc = gst_check_setup_element ("videotestsrc");
  mysinkpad = gst_check_setup_sink_pad (videotestsrc, &sinktemplate, NULL);
  gst_pad_set_active (mysinkpad, TRUE);

  return videotestsrc;
}

void
cleanup_videotestsrc (GstElement * videotestsrc)
{
  GST_DEBUG ("cleanup_videotestsrc");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_check_teardown_sink_pad (videotestsrc);
  gst_check_teardown_element (videotestsrc);
}

GST_START_TEST (test_all_patterns)
{
  GstElement *videotestsrc;
  GObjectClass *oclass;
  GParamSpec *property;
  GEnumValue *values;
  guint j = 0;

  videotestsrc = setup_videotestsrc ();
  oclass = G_OBJECT_GET_CLASS (videotestsrc);
  property = g_object_class_find_property (oclass, "pattern");
  fail_unless (G_IS_PARAM_SPEC_ENUM (property));
  values = G_ENUM_CLASS (g_type_class_ref (property->value_type))->values;


  while (values[j].value_name) {
    GST_DEBUG_OBJECT (videotestsrc, "testing pattern %s", values[j].value_name);

    fail_unless (gst_element_set_state (videotestsrc,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
        "could not set to playing");

    while (g_list_length (buffers) < 10);

    gst_element_set_state (videotestsrc, GST_STATE_READY);

    g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
    g_list_free (buffers);
    buffers = NULL;
    ++j;
  }

  /* cleanup */
  cleanup_videotestsrc (videotestsrc);
}

GST_END_TEST;

/* FIXME:
 * add tests for every colorspace */

Suite *
videotestsrc_suite (void)
{
  Suite *s = suite_create ("videotestsrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_all_patterns);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = videotestsrc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
