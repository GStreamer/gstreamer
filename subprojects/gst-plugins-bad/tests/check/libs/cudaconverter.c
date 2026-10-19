/*
 * GStreamer
 * Copyright (C) 2026 David Maseda Neira <david.masedan@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/cuda/gstcuda.h>
#include <gst/video/video.h>

static GstCudaContext *context = NULL;

static void
setup_func (void)
{
  context = gst_cuda_context_new (0);
  fail_unless (GST_IS_CUDA_CONTEXT (context));
}

static void
teardown_func (void)
{
  gst_object_unref (context);
}

static GstCudaConverter *
create_converter (void)
{
  GstVideoInfo info;
  GstCudaConverter *conv;

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_RGBA, 64, 64);
  conv = gst_cuda_converter_new (context, &info, &info, NULL);
  fail_unless (conv != NULL);

  return conv;
}

GST_START_TEST (test_alpha_property)
{
  GstCudaConverter *conv = create_converter ();
  gdouble alpha;

  g_object_set (conv, "alpha", 0.5, NULL);
  g_object_get (conv, "alpha", &alpha, NULL);
  fail_unless_equals_float (alpha, 0.5);

  /* Going back to the default value must be applied too */
  g_object_set (conv, "alpha", 1.0, NULL);
  g_object_get (conv, "alpha", &alpha, NULL);
  fail_unless_equals_float (alpha, 1.0);

  g_object_set (conv, "alpha", 0.3, NULL);
  g_object_get (conv, "alpha", &alpha, NULL);
  fail_unless_equals_float (alpha, 0.3);

  gst_object_unref (conv);
}

GST_END_TEST;

GST_START_TEST (test_blend_property)
{
  GstCudaConverter *conv = create_converter ();
  gboolean blend;

  g_object_set (conv, "blend", TRUE, NULL);
  g_object_get (conv, "blend", &blend, NULL);
  fail_unless (blend);

  g_object_set (conv, "blend", FALSE, NULL);
  g_object_get (conv, "blend", &blend, NULL);
  fail_unless (!blend);

  gst_object_unref (conv);
}

GST_END_TEST;

static gboolean
check_cuda_device (void)
{
  GstCudaContext *context;

  if (!gst_cuda_load_library ())
    return FALSE;

  context = gst_cuda_context_new (0);
  if (context) {
    gst_object_unref (context);
    return TRUE;
  }

  return FALSE;
}

static Suite *
cudaconverter_suite (void)
{
  Suite *s;
  TCase *tc_chain;

  s = suite_create ("cudaconverter");
  tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup_func, teardown_func);

  if (!check_cuda_device ())
    return s;

  tcase_add_test (tc_chain, test_alpha_property);
  tcase_add_test (tc_chain, test_blend_property);

  return s;
}

GST_CHECK_MAIN_NOFORK (cudaconverter);
