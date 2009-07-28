/* GStreamer
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
 *
 * Test that the FFmpeg plugin is loadable, and not broken in some stupid
 * way.
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
#include <stdlib.h>

GST_START_TEST (test_ffmpeg_plugin)
{
  GstPlugin *plugin = gst_plugin_load_by_name ("ffmpeg");

  fail_if (plugin == NULL, "Could not load FFmpeg plugin");

  gst_object_unref (plugin);

}

GST_END_TEST;

GST_START_TEST (test_ffmpeg_update_reg)
{
  GstElement *encoder, *muxer, *decoder;

  /* Ask for elements the first time */
  encoder = gst_element_factory_make ("ffenc_mpeg2video", "sink");
  GST_DEBUG ("Creating element ffenc_mpeg2video %p", encoder);
  fail_unless (encoder != NULL);

  decoder = gst_element_factory_make ("ffdec_mpeg2video", "sink");
  GST_DEBUG ("Creating element ffdec_mpeg2video %p", decoder);
  fail_unless (decoder != NULL);

  muxer = gst_element_factory_make ("ffmux_dvd", "sink");
  GST_DEBUG ("Creating element ffmux_dvd %p", muxer);
  fail_unless (muxer != NULL);

  gst_object_unref (encoder);
  gst_object_unref (decoder);
  gst_object_unref (muxer);

  GST_DEBUG ("calls gst_update_registry");
  gst_update_registry ();

  /* Ask for elements the second time */

  encoder = gst_element_factory_make ("ffenc_mpeg2video", "sink");
  GST_DEBUG ("Creating element ffenc_mpeg2video %p", encoder);
  fail_unless (encoder != NULL);

  decoder = gst_element_factory_make ("ffdec_mpeg2video", "sink");
  GST_DEBUG ("Creating element ffdec_mpeg2video %p", decoder);
  fail_unless (decoder != NULL);

  muxer = gst_element_factory_make ("ffmux_dvd", "sink");
  GST_DEBUG ("Creating element ffmux_dvd %p", muxer);
  fail_unless (muxer != NULL);

  gst_object_unref (encoder);
  gst_object_unref (decoder);
  gst_object_unref (muxer);
}

GST_END_TEST;

static Suite *
plugin_test_suite (void)
{
  Suite *s = suite_create ("Plugin");
  TCase *tc_chain = tcase_create ("existence");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_ffmpeg_plugin);
  tcase_add_test (tc_chain, test_ffmpeg_update_reg);

  return s;
}

int
main (int argc, char **argv)
{
  SRunner *sr;
  Suite *s;
  int nf;

  gst_check_init (&argc, &argv);

  s = plugin_test_suite ();
  sr = srunner_create (s);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
