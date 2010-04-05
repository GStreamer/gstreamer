/* GStreamer
 *
 * unit test for jpegenc
 *
 * Copyright (C) <2010> Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
 * sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysinkpad;

#define JPEG_CAPS_STRING "image/jpeg"

#define JPEG_CAPS_RESTRICTIVE "image/jpeg, " \
    "width = (int) [100, 200], " \
    "framerate = (fraction) 25/1, " \
    "extraparameter = (string) { abc, def }"

static GstStaticPadTemplate jpeg_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (JPEG_CAPS_STRING));

static GstStaticPadTemplate any_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate jpeg_restrictive_sinktemplate =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (JPEG_CAPS_RESTRICTIVE));

static GstElement *
setup_jpegenc (GstStaticPadTemplate * sinktemplate)
{
  GstElement *jpegenc;

  GST_DEBUG ("setup_jpegenc");
  jpegenc = gst_check_setup_element ("jpegenc");
  mysinkpad = gst_check_setup_sink_pad (jpegenc, sinktemplate, NULL);
  gst_pad_set_active (mysinkpad, TRUE);

  return jpegenc;
}

static void
cleanup_jpegenc (GstElement * jpegenc)
{
  GST_DEBUG ("cleanup_jpegenc");
  gst_element_set_state (jpegenc, GST_STATE_NULL);

  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (jpegenc);
  gst_check_teardown_element (jpegenc);
}

GST_START_TEST (test_jpegenc_getcaps)
{
  GstElement *jpegenc;
  GstPad *sinkpad;
  GstCaps *caps;
  GstStructure *structure;
  gint fps_n;
  gint fps_d;
  const GValue *value;

  /* we are going to do some get caps to confirm it doesn't return non-subset
   * caps */

  jpegenc = setup_jpegenc (&any_sinktemplate);
  sinkpad = gst_element_get_static_pad (jpegenc, "sink");
  /* this should assert if non-subset */
  caps = gst_pad_get_caps (sinkpad);
  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  cleanup_jpegenc (jpegenc);

  jpegenc = setup_jpegenc (&jpeg_sinktemplate);
  sinkpad = gst_element_get_static_pad (jpegenc, "sink");
  /* this should assert if non-subset */
  caps = gst_pad_get_caps (sinkpad);
  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  cleanup_jpegenc (jpegenc);

  /* now use a more restricted one and check the resulting caps */
  jpegenc = setup_jpegenc (&jpeg_restrictive_sinktemplate);
  sinkpad = gst_element_get_static_pad (jpegenc, "sink");
  /* this should assert if non-subset */
  caps = gst_pad_get_caps (sinkpad);
  structure = gst_caps_get_structure (caps, 0);

  /* check the width */
  value = gst_structure_get_value (structure, "width");
  fail_unless (gst_value_get_int_range_min (value) == 100);
  fail_unless (gst_value_get_int_range_max (value) == 200);

  fail_unless (gst_structure_get_fraction (structure, "framerate", &fps_n,
          &fps_d));
  fail_unless (fps_n == 25);
  fail_unless (fps_d == 1);

  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  cleanup_jpegenc (jpegenc);
}

GST_END_TEST;

static Suite *
jpegenc_suite (void)
{
  Suite *s = suite_create ("jpegenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_jpegenc_getcaps);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = jpegenc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
