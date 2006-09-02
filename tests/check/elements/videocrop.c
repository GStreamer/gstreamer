/* GStreamer unit test for the videocrop element
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/base/gstbasetransform.h>

/* return a list of caps where we only need to set
 * width and height to get fixed caps */
static GList *
video_crop_get_test_caps (GstElement * videocrop)
{
  const GstCaps *allowed_caps;
  GstPad *srcpad;
  GList *list = NULL;
  guint i;

  srcpad = gst_element_get_pad (videocrop, "src");
  fail_unless (srcpad != NULL);
  allowed_caps = gst_pad_get_pad_template_caps (srcpad);
  fail_unless (allowed_caps != NULL);

  for (i = 0; i < gst_caps_get_size (allowed_caps); ++i) {
    GstStructure *new_structure;
    GstCaps *single_caps;

    single_caps = gst_caps_new_empty ();
    new_structure =
        gst_structure_copy (gst_caps_get_structure (allowed_caps, i));
    gst_structure_set (new_structure, "framerate", GST_TYPE_FRACTION,
        1, 1, NULL);
    gst_structure_remove_field (new_structure, "width");
    gst_structure_remove_field (new_structure, "height");
    gst_caps_append_structure (single_caps, new_structure);

    /* should be fixed without width/height */
    fail_unless (gst_caps_is_fixed (single_caps));

    list = g_list_prepend (list, single_caps);
  }

  gst_object_unref (srcpad);

  return list;
}

GST_START_TEST (test_unit_sizes)
{
  GstBaseTransformClass *csp_klass, *vcrop_klass;
  GstElement *videocrop, *csp;
  GList *caps_list, *l;
  gint i;

  videocrop = gst_element_factory_make ("videocrop", "videocrop");
  fail_unless (videocrop != NULL);
  vcrop_klass = GST_BASE_TRANSFORM_GET_CLASS (videocrop);

  csp = gst_element_factory_make ("ffmpegcolorspace", "csp");
  fail_unless (csp != NULL);
  csp_klass = GST_BASE_TRANSFORM_GET_CLASS (csp);

  caps_list = video_crop_get_test_caps (videocrop);

  for (l = caps_list; l != NULL; l = l->next) {
    const struct
    {
      gint width, height;
    } sizes_to_try[] = {
      {
      160, 120}, {
      161, 120}, {
      160, 121}, {
      161, 121}, {
      159, 120}, {
      160, 119}, {
      159, 119}, {
      159, 121}
    };
    GstStructure *s;
    GstCaps *caps;
    gint i;

    caps = gst_caps_copy (GST_CAPS (l->data));
    s = gst_caps_get_structure (caps, 0);
    fail_unless (s != NULL);

    for (i = 0; i < G_N_ELEMENTS (sizes_to_try); ++i) {
      gchar *caps_str;
      guint32 format = 0;
      guint csp_size = 0;
      guint vc_size = 0;

      gst_structure_set (s, "width", G_TYPE_INT, sizes_to_try[i].width,
          "height", G_TYPE_INT, sizes_to_try[i].height, NULL);

      caps_str = gst_caps_to_string (caps);
      GST_INFO ("Testing unit size for %s", caps_str);

      /* skip if ffmpegcolorspace doesn't support these caps
       * (only works with gst-plugins-base 0.10.9.1 or later) */
      if (!csp_klass->get_unit_size ((GstBaseTransform *) csp, caps, &csp_size)) {
        GST_INFO ("ffmpegcolorspace does not support format %s", caps_str);
        g_free (caps_str);
        continue;
      }

      fail_unless (vcrop_klass->get_unit_size ((GstBaseTransform *) videocrop,
              caps, &vc_size));

      fail_unless (vc_size == csp_size,
          "videocrop and ffmpegcolorspace return different unit sizes for "
          "caps %s: vc_size=%d, csp_size=%d", caps_str, vc_size, csp_size);

      g_free (caps_str);
    }

    gst_caps_unref (caps);
  }

  g_list_foreach (caps_list, (GFunc) gst_caps_unref, NULL);
  g_list_free (caps_list);

  gst_object_unref (csp);
  gst_object_unref (videocrop);
}

GST_END_TEST;

typedef struct
{
  GstElement *pipeline;
  GstElement *src;
  GstElement *filter;
  GstElement *crop;
  GstElement *sink;
} GstVideoCropTestContext;

static void
videocrop_test_cropping_init_context (GstVideoCropTestContext * ctx)
{
  fail_unless (ctx != NULL);

  ctx->pipeline = gst_pipeline_new ("pipeline");
  fail_unless (ctx->pipeline != NULL);
  ctx->src = gst_element_factory_make ("videotestsrc", "src");
  fail_unless (ctx->src != NULL);
  ctx->filter = gst_element_factory_make ("capsfilter", "filter");
  fail_unless (ctx->filter != NULL);
  ctx->crop = gst_element_factory_make ("videocrop", "crop");
  fail_unless (ctx->crop != NULL);
  ctx->sink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (ctx->sink != NULL);

  gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->src, ctx->filter,
      ctx->crop, ctx->sink, NULL);
  gst_element_link_many (ctx->src, ctx->filter, ctx->crop, ctx->sink, NULL);

  GST_LOG ("context inited");
}

static void
videocrop_test_cropping_deinit_context (GstVideoCropTestContext * ctx)
{
  GST_LOG ("deiniting context");

  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
  gst_object_unref (ctx->pipeline);
  memset (ctx, 0x00, sizeof (GstVideoCropTestContext));
}

static void
videocrop_test_cropping (GstVideoCropTestContext * ctx, GstCaps * in_caps,
    gint left, gint right, gint top, gint bottom)
{
  GST_LOG ("lrtb = %03u %03u %03u %03u, caps = %" GST_PTR_FORMAT, left, right,
      top, bottom, in_caps);

  g_object_set (ctx->filter, "caps", in_caps, NULL);

  g_object_set (ctx->crop, "left", left, "right", right, "top", top,
      "bottom", bottom, NULL);

  /* this will fail if videotestsrc doesn't support our format; we need
   * videotestsrc from -base CVS 0.10.9.1 with RGBA and AYUV support */
  fail_unless (gst_element_set_state (ctx->pipeline,
          GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE);
  fail_unless (gst_element_get_state (ctx->pipeline, NULL, NULL,
          -1) == GST_STATE_CHANGE_SUCCESS);

  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
}

GST_START_TEST (test_cropping)
{
  GstVideoCropTestContext ctx;
  struct
  {
    gint width, height;
  } sizes_to_try[] = {
    {
    160, 160}, {
    161, 160}, {
    160, 161}, {
    161, 161}, {
    159, 160}, {
    160, 159}, {
    159, 159}, {
    159, 161}
  };
  GList *caps_list, *node;
  gint i;

  videocrop_test_cropping_init_context (&ctx);

  caps_list = video_crop_get_test_caps (ctx.crop);

  for (node = caps_list; node != NULL; node = node->next) {
    GstStructure *s;
    GstCaps *caps;

    caps = gst_caps_copy (GST_CAPS (node->data));
    s = gst_caps_get_structure (caps, 0);
    fail_unless (s != NULL);

    GST_INFO ("testing format: %" GST_PTR_FORMAT, caps);

    for (i = 0; i < G_N_ELEMENTS (sizes_to_try); ++i) {
      gst_structure_set (s, "width", G_TYPE_INT, sizes_to_try[i].width,
          "height", G_TYPE_INT, sizes_to_try[i].height, NULL);

      GST_INFO (" - %d x %d", sizes_to_try[i].width, sizes_to_try[i].height);

      videocrop_test_cropping (&ctx, caps, 0, 0, 0, 0);
      videocrop_test_cropping (&ctx, caps, 1, 0, 0, 0);
      videocrop_test_cropping (&ctx, caps, 0, 1, 0, 0);
      videocrop_test_cropping (&ctx, caps, 0, 0, 1, 0);
      videocrop_test_cropping (&ctx, caps, 0, 0, 0, 1);
      videocrop_test_cropping (&ctx, caps, 63, 0, 0, 0);
      videocrop_test_cropping (&ctx, caps, 0, 63, 0, 0);
      videocrop_test_cropping (&ctx, caps, 0, 0, 63, 0);
      videocrop_test_cropping (&ctx, caps, 0, 0, 0, 63);
      videocrop_test_cropping (&ctx, caps, 63, 0, 0, 1);
      videocrop_test_cropping (&ctx, caps, 0, 63, 1, 0);
      videocrop_test_cropping (&ctx, caps, 0, 1, 63, 0);
      videocrop_test_cropping (&ctx, caps, 1, 0, 0, 63);
      videocrop_test_cropping (&ctx, caps, 0, 0, 0, 0);
      videocrop_test_cropping (&ctx, caps, 32, 0, 0, 128);
      videocrop_test_cropping (&ctx, caps, 0, 32, 128, 0);
      videocrop_test_cropping (&ctx, caps, 0, 128, 32, 0);
      videocrop_test_cropping (&ctx, caps, 128, 0, 0, 32);
      videocrop_test_cropping (&ctx, caps, 1, 1, 1, 1);
      videocrop_test_cropping (&ctx, caps, 63, 63, 63, 63);
      videocrop_test_cropping (&ctx, caps, 64, 64, 64, 64);
    }
  }

  videocrop_test_cropping_deinit_context (&ctx);
}

GST_END_TEST;

static Suite *
videocrop_suite (void)
{
  Suite *s = suite_create ("videocrop");
  TCase *tc_chain = tcase_create ("general");

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    /* otherwise valgrind errors out when liboil probes CPU extensions
     * during which it causes SIGILLs etc. to be fired */
    g_setenv ("OIL_CPU_FLAGS", "0", 0);
    /* our tests take quite a long time, so increase
     * timeout (~10 minutes on my 1.6GHz AMD K7) */
    tcase_set_timeout (tc_chain, 20 * 60);
  } else
#endif
  {
    /* increase timeout, these tests take a long time (60 secs here) */
    tcase_set_timeout (tc_chain, 2 * 60);
  }

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_unit_sizes);
  tcase_add_test (tc_chain, test_cropping);

  return s;
}

GST_CHECK_MAIN (videocrop);
