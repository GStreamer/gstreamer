/* GStreamer
 * unit test for videoconvert
 *
 * Copyright (C) 2006-2012 Tim-Philipp Müller <tim centricular net>
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
# include <config.h>
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

static void
check_pad_template (GstPadTemplate * tmpl)
{
  const GValue *list_val, *fmt_val;
  GstStructure *s;
  gboolean *formats_supported;
  GstCaps *caps;
  guint i;

  formats_supported = g_new0 (gboolean, GST_VIDEO_FORMAT_LAST);

  caps = gst_pad_template_get_caps (tmpl);

  /* If this fails, we need to update this unit test */
  fail_unless_equals_int (gst_caps_get_size (caps), 2);
  /* Remove the ANY caps features structure */
  caps = gst_caps_truncate (caps);
  s = gst_caps_get_structure (caps, 0);

  fail_unless (gst_structure_has_name (s, "video/x-raw"));

  list_val = gst_structure_get_value (s, "format");
  fail_unless (list_val != NULL);
  /* If this fails, we need to update this unit test */
  fail_unless (GST_VALUE_HOLDS_LIST (list_val));

  for (i = 0; i < gst_value_list_get_size (list_val); ++i) {
    GstVideoFormat fmt;
    const gchar *fmt_str;

    fmt_val = gst_value_list_get_value (list_val, i);
    fail_unless (G_VALUE_HOLDS_STRING (fmt_val));
    fmt_str = g_value_get_string (fmt_val);
    GST_LOG ("format string: '%s'", fmt_str);
    fmt = gst_video_format_from_string (fmt_str);
    fail_unless (fmt != GST_VIDEO_FORMAT_UNKNOWN);
    formats_supported[(guint) fmt] = TRUE;
  }

  gst_caps_unref (caps);

  for (i = 2; i < GST_VIDEO_FORMAT_LAST; ++i) {
    if (i == GST_VIDEO_FORMAT_DMA_DRM)
      continue;

    if (!formats_supported[i]) {
      g_error ("videoconvert doesn't support format '%s'",
          gst_video_format_to_string ((GstVideoFormat) i));
    }
  }

  g_free (formats_supported);
}

GST_START_TEST (test_template_formats)
{
  GstElementFactory *f;
  GstPadTemplate *t;
  const GList *pad_templates;

  f = gst_element_factory_find ("videoconvert");
  fail_unless (f != NULL);

  pad_templates = gst_element_factory_get_static_pad_templates (f);
  fail_unless_equals_int (g_list_length ((GList *) pad_templates), 2);

  t = gst_static_pad_template_get (pad_templates->data);
  check_pad_template (GST_PAD_TEMPLATE (t));
  gst_object_unref (t);
  t = gst_static_pad_template_get (pad_templates->next->data);
  check_pad_template (GST_PAD_TEMPLATE (t));
  gst_object_unref (t);

  gst_object_unref (f);
}

GST_END_TEST;

GST_START_TEST (test_negotiate_alternate)
{
  GstHarness *h;
  GstBuffer *buffer;
  GstMapInfo map;

  h = gst_harness_new ("videoconvert");

  buffer = gst_buffer_new_and_alloc (4);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  map.data[0] = 0x0;
  map.data[1] = 0x0;
  map.data[2] = 0x0;
  map.data[3] = 0x0;
  gst_buffer_unmap (buffer, &map);

  gst_harness_set_sink_caps_str (h,
      "video/x-raw,interlace-mode=alternate,width=1,height=1,format=AYUV");
  gst_harness_set_src_caps_str (h,
      "video/x-raw(format:Interlaced),interlace-mode=alternate,width=1,height=1,format=ARGB");
  fail_unless_equals_int (gst_harness_push (h, gst_buffer_ref (buffer)),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_set_sink_caps_str (h,
      "video/x-raw(format:Interlaced),interlace-mode=alternate,width=1,height=1,format=AYUV");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=alternate,width=1,height=1,format=ARGB");
  fail_unless_equals_int (gst_harness_push (h, gst_buffer_ref (buffer)),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_set_sink_caps_str (h,
      "video/x-raw(format:Interlaced),interlace-mode=alternate,width=1,height=1,format=AYUV");
  gst_harness_set_src_caps_str (h,
      "video/x-raw(format:Interlaced),interlace-mode=alternate,width=1,height=1,format=ARGB");
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

static GstBuffer *
create_gray8_buffer_with_layout (gsize offset, gint stride)
{
  GstBuffer *buffer;
  GstMapInfo map;
  gsize offsets[GST_VIDEO_MAX_PLANES] = { offset, 0, 0, 0 };
  gint strides[GST_VIDEO_MAX_PLANES] = { stride, 0, 0, 0 };

  buffer = gst_buffer_new_allocate (NULL, offset + 2 * stride, NULL);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_WRITE));
  memset (map.data, 0, map.size);
  map.data[offset] = 16;
  map.data[offset + 1] = 32;
  map.data[offset + stride] = 64;
  map.data[offset + stride + 1] = 128;
  gst_buffer_unmap (buffer, &map);

  gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_GRAY8, 2, 2, 1, offsets, strides);

  return buffer;
}

static GstBuffer *
create_gray8_4x4_buffer (void)
{
  GstBuffer *buffer;
  GstMapInfo map;
  gsize offsets[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gint strides[GST_VIDEO_MAX_PLANES] = { 4, 0, 0, 0 };
  const guint8 pixels[16] = {
    0, 0, 255, 255,
    0, 0, 255, 255,
    255, 255, 255, 255,
    255, 255, 255, 255
  };

  buffer = gst_buffer_new_allocate (NULL, sizeof (pixels), NULL);
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_WRITE));
  memcpy (map.data, pixels, sizeof (pixels));
  gst_buffer_unmap (buffer, &map);

  gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_GRAY8, 4, 4, 1, offsets, strides);

  return buffer;
}

static gboolean
rgb_buffer_pixels_equal (GstBuffer * first, GstBuffer * second)
{
  GstVideoInfo info;
  GstVideoFrame first_frame;
  GstVideoFrame second_frame;
  gboolean equal = TRUE;
  guint i;

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_RGB, 2, 2);
  fail_unless (gst_video_frame_map (&first_frame, &info, first, GST_MAP_READ));
  fail_unless (gst_video_frame_map (&second_frame, &info, second,
          GST_MAP_READ));

  for (i = 0; i < 2; i++) {
    if (memcmp ((guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&first_frame, 0) +
            i * GST_VIDEO_FRAME_PLANE_STRIDE (&first_frame, 0),
            (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&second_frame, 0) +
            i * GST_VIDEO_FRAME_PLANE_STRIDE (&second_frame, 0), 6) != 0) {
      equal = FALSE;
      break;
    }
  }

  gst_video_frame_unmap (&second_frame);
  gst_video_frame_unmap (&first_frame);

  return equal;
}

/* Test that changing GstVideoMeta layouts does not change converted pixels */
GST_START_TEST (test_videometa_layout_changes)
{
  GstHarness *h;
  GstBuffer *first;
  GstBuffer *second;

  h = gst_harness_new ("videoconvertscale");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=GRAY8,width=2,height=2,framerate=30/1");
  gst_harness_set_sink_caps_str (h,
      "video/x-raw,format=RGB,width=2,height=2,framerate=30/1");

  first = gst_harness_push_and_pull (h, create_gray8_buffer_with_layout (0, 2));
  fail_unless (first != NULL);
  second = gst_harness_push_and_pull (h,
      create_gray8_buffer_with_layout (3, 4));
  fail_unless (second != NULL);

  fail_unless (rgb_buffer_pixels_equal (first, second));

  gst_buffer_unref (second);
  gst_buffer_unref (first);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that runtime converter configuration updates rebuild the converter */
GST_START_TEST (test_converter_config_update)
{
  GstHarness *h;
  GstBuffer *full_frame;
  GstBuffer *configured;
  GstBuffer *restored;
  GstStructure *config;

  h = gst_harness_new ("videoconvertscale");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=GRAY8,width=2,height=2,framerate=30/1");
  gst_harness_set_sink_caps_str (h,
      "video/x-raw,format=RGB,width=2,height=2,framerate=30/1");

  full_frame = gst_harness_push_and_pull (h, create_gray8_4x4_buffer ());
  fail_unless (full_frame != NULL);

  config = gst_structure_new ("GstVideoConverter",
      GST_VIDEO_CONVERTER_OPT_SRC_X, G_TYPE_INT, 0,
      GST_VIDEO_CONVERTER_OPT_SRC_Y, G_TYPE_INT, 0,
      GST_VIDEO_CONVERTER_OPT_SRC_WIDTH, G_TYPE_INT, 2,
      GST_VIDEO_CONVERTER_OPT_SRC_HEIGHT, G_TYPE_INT, 2, NULL);
  g_object_set (h->element, "converter-config", config, NULL);
  gst_structure_free (config);

  configured = gst_harness_push_and_pull (h, create_gray8_4x4_buffer ());
  fail_unless (configured != NULL);
  fail_if (rgb_buffer_pixels_equal (full_frame, configured));

  g_object_set (h->element, "converter-config", NULL, NULL);
  restored = gst_harness_push_and_pull (h, create_gray8_4x4_buffer ());
  fail_unless (restored != NULL);
  fail_unless (rgb_buffer_pixels_equal (full_frame, restored));

  gst_buffer_unref (restored);
  gst_buffer_unref (configured);
  gst_buffer_unref (full_frame);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
videoconvert_suite (void)
{
  Suite *s = suite_create ("videoconvert");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_template_formats);
  tcase_add_test (tc_chain, test_negotiate_alternate);
  tcase_add_test (tc_chain, test_videometa_layout_changes);
  tcase_add_test (tc_chain, test_converter_config_update);

  return s;
}

GST_CHECK_MAIN (videoconvert);
