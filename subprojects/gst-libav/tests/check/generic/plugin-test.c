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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#include <gst/check/gstcheck.h>
#include <stdlib.h>

GST_START_TEST (test_libav_plugin)
{
  GstPlugin *plugin = gst_plugin_load_by_name ("libav");

  fail_if (plugin == NULL, "Could not load FFmpeg plugin");

  gst_object_unref (plugin);

}

GST_END_TEST;

GST_START_TEST (test_libav_update_reg)
{
  GstElement *encoder, *muxer, *decoder;

  /* Ask for elements the first time */
  encoder = gst_element_factory_make ("avenc_mpeg2video", "sink");
  GST_DEBUG ("Creating element avenc_mpeg2video %p", encoder);
  fail_unless (encoder != NULL);

  decoder = gst_element_factory_make ("avdec_mpeg2video", "sink");
  GST_DEBUG ("Creating element avdec_mpeg2video %p", decoder);
  fail_unless (decoder != NULL);

  muxer = gst_element_factory_make ("avmux_dvd", "sink");
  GST_DEBUG ("Creating element avmux_dvd %p", muxer);
  fail_unless (muxer != NULL);

  gst_object_unref (encoder);
  gst_object_unref (decoder);
  gst_object_unref (muxer);

  GST_DEBUG ("calls gst_update_registry");
  gst_update_registry ();

  /* Ask for elements the second time */

  encoder = gst_element_factory_make ("avenc_mpeg2video", "sink");
  GST_DEBUG ("Creating element avenc_mpeg2video %p", encoder);
  fail_unless (encoder != NULL);

  decoder = gst_element_factory_make ("avdec_mpeg2video", "sink");
  GST_DEBUG ("Creating element avdec_mpeg2video %p", decoder);
  fail_unless (decoder != NULL);

  muxer = gst_element_factory_make ("avmux_dvd", "sink");
  GST_DEBUG ("Creating element avmux_dvd %p", muxer);
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

  tcase_add_test (tc_chain, test_libav_plugin);
  tcase_add_test (tc_chain, test_libav_update_reg);

  return s;
}

GST_CHECK_MAIN (plugin_test);
