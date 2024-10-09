/* GStreamer
 *
 * unit test for VA allocators
 *
 * Copyright (C) 2021 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

GST_START_TEST (raw_copy)
{
  GstHarness *h;
  GstBuffer *buf, *buf_copy;
  gboolean ret;

  h = gst_harness_new_parse ("videotestsrc num-buffers=1 ! "
      "video/x-raw, width=(int)1024, height=(int)768 ! vapostproc");
  ck_assert (h);

  gst_harness_set_sink_caps_str (h,
      "video/x-raw, format=(string)NV12, width=(int)3840, height=(int)2160");

  gst_harness_add_propose_allocation_meta (h, GST_VIDEO_META_API_TYPE, NULL);
  gst_harness_play (h);

  buf = gst_harness_pull (h);
  ck_assert (buf);

  buf_copy = gst_buffer_new ();
  ret = gst_buffer_copy_into (buf_copy, buf,
      GST_BUFFER_COPY_MEMORY | GST_BUFFER_COPY_DEEP, 0, -1);
  ck_assert (ret);

  gst_clear_buffer (&buf);
  gst_clear_buffer (&buf_copy);

  gst_harness_teardown (h);
}

GST_END_TEST;

static GstCaps *
get_drmdma_format (void)
{
  GstElement *vpp;
  GstCaps *templ, *allowed_caps, *drm_caps = NULL;
  GstPad *srcpad;
  guint i;

  vpp = gst_element_factory_make ("vapostproc", NULL);
  if (!vpp)
    return NULL;
  srcpad = gst_element_get_static_pad (vpp, "src");
  fail_unless (srcpad != NULL);
  templ = gst_pad_get_pad_template_caps (srcpad);
  fail_unless (templ != NULL);

  allowed_caps = gst_caps_normalize (templ);

  for (i = 0; i < gst_caps_get_size (allowed_caps); ++i) {
    GstStructure *new_structure;
    GstStructure *structure;

    /* non-dmabuf caps don't describe drm-format: skip them */
    structure = gst_caps_get_structure (allowed_caps, i);
    if (!gst_structure_has_field (structure, "drm-format"))
      continue;

    drm_caps = gst_caps_new_empty ();
    new_structure = gst_structure_copy (structure);
    gst_structure_set (new_structure, "framerate", GST_TYPE_FRACTION,
        1, 1, NULL);
    gst_structure_remove_field (new_structure, "width");
    gst_structure_remove_field (new_structure, "height");
    gst_caps_append_structure (drm_caps, new_structure);
    gst_caps_set_features_simple (drm_caps,
        gst_caps_features_new_single_static_str ("memory:DMABuf"));

    GST_DEBUG ("have caps %" GST_PTR_FORMAT, drm_caps);
    /* should be fixed without width/height */
    fail_unless (gst_caps_is_fixed (drm_caps));
    break;
  }

  gst_caps_unref (allowed_caps);
  gst_object_unref (srcpad);
  gst_object_unref (vpp);

  return drm_caps;
}

GST_START_TEST (dmabuf_copy)
{
  GstHarness *h;
  GstBuffer *buf, *buf_copy;
  gboolean ret;
  GstCaps *drm_caps;

  h = gst_harness_new_parse ("videotestsrc num-buffers=1 ! "
      "video/x-raw, width=(int)1024, height=(int)768 ! vapostproc");
  ck_assert (h);

  drm_caps = get_drmdma_format ();
  ck_assert (drm_caps);
  gst_caps_set_simple (drm_caps, "width", G_TYPE_INT, 1600, "height",
      G_TYPE_INT, 1200, NULL);

  gst_harness_set_sink_caps (h, drm_caps);

  gst_harness_add_propose_allocation_meta (h, GST_VIDEO_META_API_TYPE, NULL);
  gst_harness_play (h);

  buf = gst_harness_pull (h);
  ck_assert (buf);

  buf_copy = gst_buffer_new ();
  ret = gst_buffer_copy_into (buf_copy, buf,
      GST_BUFFER_COPY_MEMORY | GST_BUFFER_COPY_DEEP, 0, -1);

  if (gst_buffer_n_memory (buf_copy) == 1)
    ck_assert (ret == TRUE);
  /* else it will depend on the drm modifier */

  gst_clear_buffer (&buf);
  gst_clear_buffer (&buf_copy);

  gst_harness_teardown (h);
}

GST_END_TEST;

int
main (int argc, char **argv)
{
  GstElement *vpp;
  Suite *s;
  TCase *tc_chain;

  gst_check_init (&argc, &argv);

  vpp = gst_element_factory_make ("vapostproc", NULL);
  if (!vpp)
    return EXIT_SUCCESS;        /* not available vapostproc */
  gst_object_unref (vpp);

  s = suite_create ("va");
  tc_chain = tcase_create ("copy");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, raw_copy);
  tcase_add_test (tc_chain, dmabuf_copy);

  return gst_check_run_suite (s, "va", __FILE__);
}
