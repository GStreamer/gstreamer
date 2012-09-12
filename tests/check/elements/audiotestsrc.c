/* GStreamer
 *
 * unit test for audiotestsrc
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
#include <gst/audio/audio.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysinkpad;


#define CAPS_TEMPLATE_STRING            \
    "audio/x-raw, "                     \
    "format = (string) "GST_AUDIO_NE(S16)", "   \
    "channels = (int) 1, "              \
    "rate = (int) [ 1,  MAX ]"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_TEMPLATE_STRING)
    );

static GstElement *
setup_audiotestsrc (void)
{
  GstElement *audiotestsrc;

  GST_DEBUG ("setup_audiotestsrc");
  audiotestsrc = gst_check_setup_element ("audiotestsrc");
  mysinkpad = gst_check_setup_sink_pad (audiotestsrc, &sinktemplate);
  gst_pad_set_active (mysinkpad, TRUE);

  return audiotestsrc;
}

static void
cleanup_audiotestsrc (GstElement * audiotestsrc)
{
  GST_DEBUG ("cleanup_audiotestsrc");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (audiotestsrc);
  gst_check_teardown_element (audiotestsrc);
}

GST_START_TEST (test_all_waves)
{
  GstElement *audiotestsrc;
  GObjectClass *oclass;
  GParamSpec *property;
  GEnumValue *values;
  guint j = 0;

  audiotestsrc = setup_audiotestsrc ();
  oclass = G_OBJECT_GET_CLASS (audiotestsrc);
  property = g_object_class_find_property (oclass, "wave");
  fail_unless (G_IS_PARAM_SPEC_ENUM (property));
  values = G_ENUM_CLASS (g_type_class_ref (property->value_type))->values;


  while (values[j].value_name) {
    GST_DEBUG_OBJECT (audiotestsrc, "testing wave %s", values[j].value_name);
    g_object_set (audiotestsrc, "wave", values[j].value, NULL);

    fail_unless (gst_element_set_state (audiotestsrc,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
        "could not set to playing");

    g_mutex_lock (&check_mutex);
    while (g_list_length (buffers) < 10)
      g_cond_wait (&check_cond, &check_mutex);
    g_mutex_unlock (&check_mutex);

    gst_element_set_state (audiotestsrc, GST_STATE_READY);

    g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
    g_list_free (buffers);
    buffers = NULL;
    ++j;
  }

  /* cleanup */
  cleanup_audiotestsrc (audiotestsrc);
}

GST_END_TEST;

static Suite *
audiotestsrc_suite (void)
{
  Suite *s = suite_create ("audiotestsrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_all_waves);

  return s;
}

GST_CHECK_MAIN (audiotestsrc);
