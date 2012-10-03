/* GStreamer
 *
 * unit test for videotestsrc
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
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
# include <config.h>
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysinkpad;


#define CAPS_TEMPLATE_STRING            \
    "video/x-raw, "                 \
    "format = (string) UYVY, "          \
    "width = (int) [ 1,  MAX ], "       \
    "height = (int) [ 1,  MAX ], "      \
    "framerate = (fraction) [ 0/1, MAX ]"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_TEMPLATE_STRING)
    );

static GstElement *
setup_videotestsrc (void)
{
  GstElement *videotestsrc;

  GST_DEBUG ("setup_videotestsrc");
  videotestsrc = gst_check_setup_element ("videotestsrc");
  mysinkpad = gst_check_setup_sink_pad (videotestsrc, &sinktemplate);
  gst_pad_set_active (mysinkpad, TRUE);

  return videotestsrc;
}

static void
cleanup_videotestsrc (GstElement * videotestsrc)
{
  GST_DEBUG ("cleanup_videotestsrc");

  gst_check_drop_buffers ();

  gst_pad_set_active (mysinkpad, FALSE);
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

    g_object_set (videotestsrc, "pattern", j, NULL);

    fail_unless (gst_element_set_state (videotestsrc,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
        "could not set to playing");

    g_mutex_lock (&check_mutex);
    while (g_list_length (buffers) < 10) {
      GST_DEBUG_OBJECT (videotestsrc, "Waiting for more buffers");
      g_cond_wait (&check_cond, &check_mutex);
    }
    g_mutex_unlock (&check_mutex);

    gst_element_set_state (videotestsrc, GST_STATE_READY);

    gst_check_drop_buffers ();
    ++j;
  }

  /* cleanup */
  cleanup_videotestsrc (videotestsrc);
}

GST_END_TEST;

static guint32
right_shift_colour (guint32 mask, guint32 pixel)
{
  if (mask == 0)
    return 0;

  pixel = pixel & mask;
  while ((mask & 0x01) == 0) {
    mask = mask >> 1;
    pixel = pixel >> 1;
  }

  return pixel;
}

static guint8
fix_expected_colour (guint32 col_mask, guint8 col_expected)
{
  guint32 mask;
  gint last = g_bit_nth_msf (col_mask, -1);
  gint first = g_bit_nth_lsf (col_mask, -1);

  mask = 1 << (last - first + 1);
  mask -= 1;

  g_assert (col_expected == 0x00 || col_expected == 0xff);

  /* this only works because we only check for all-bits-set or no-bits-set */
  return col_expected & mask;
}

static void
check_rgb_buf (const guint8 * pixels, guint32 r_mask, guint32 g_mask,
    guint32 b_mask, guint32 a_mask, guint8 r_expected, guint8 g_expected,
    guint8 b_expected, guint bpp, guint depth)
{
  guint32 pixel, red, green, blue, alpha;

  switch (bpp) {
    case 32:
      pixel = GST_READ_UINT32_BE (pixels);
      break;
    case 24:
      pixel = (GST_READ_UINT8 (pixels) << 16) |
          (GST_READ_UINT8 (pixels + 1) << 8) |
          (GST_READ_UINT8 (pixels + 2) << 0);
      break;
    case 16:
      if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
        pixel = GST_READ_UINT16_LE (pixels);
      else
        pixel = GST_READ_UINT16_BE (pixels);
      break;
    default:
      g_return_if_reached ();
  }

  red = right_shift_colour (r_mask, pixel);
  green = right_shift_colour (g_mask, pixel);
  blue = right_shift_colour (b_mask, pixel);
  alpha = right_shift_colour (a_mask, pixel);

  /* can't enable this by default, valgrind will complain about accessing
   * uninitialised memory for the depth=24,bpp=32 formats ... */
  /* GST_LOG ("pixels: 0x%02x 0x%02x 0x%02x 0x%02x => pixel = 0x%08x",
     pixels[0], (guint) pixels[1], pixels[2], pixels[3], pixel); */

  /* fix up the mask (for rgb15/16) */
  if (bpp == 16) {
    r_expected = fix_expected_colour (r_mask, r_expected);
    g_expected = fix_expected_colour (g_mask, g_expected);
    b_expected = fix_expected_colour (b_mask, b_expected);
  }

  fail_unless (red == r_expected, "RED: expected 0x%02x, found 0x%02x",
      r_expected, red);
  fail_unless (green == g_expected, "GREEN: expected 0x%02x, found 0x%02x",
      g_expected, green);
  fail_unless (blue == b_expected, "BLUE: expected 0x%02x, found 0x%02x",
      b_expected, blue);

  fail_unless (a_mask == 0 || alpha != 0);      /* better than nothing */
}

static void
got_buf_cb (GstElement * sink, GstBuffer * new_buf, GstPad * pad,
    GstSample ** p_old_sample)
{
  GstCaps *caps;

  caps = gst_pad_get_current_caps (pad);

  if (*p_old_sample)
    gst_sample_unref (*p_old_sample);
  *p_old_sample = gst_sample_new (new_buf, caps, NULL, NULL);

  gst_caps_unref (caps);
}

/* tests the positioning of pixels within the various RGB pixel layouts */
GST_START_TEST (test_rgb_formats)
{
  const struct
  {
    const gchar *pattern_name;
    gint pattern_enum;
    guint8 r_expected;
    guint8 g_expected;
    guint8 b_expected;
  } test_patterns[] = {
    {
    "white", 3, 0xff, 0xff, 0xff}, {
    "red", 4, 0xff, 0x00, 0x00}, {
    "green", 5, 0x00, 0xff, 0x00}, {
    "blue", 6, 0x00, 0x00, 0xff}, {
    "black", 2, 0x00, 0x00, 0x00}
  };
  const struct
  {
    const gchar *nick;
    guint bpp, depth;
    guint32 red_mask, green_mask, blue_mask, alpha_mask;
  } rgb_formats[] = {
    {
    "RGBA", 32, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff}, {
    "ARGB", 32, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000}, {
    "BGRA", 32, 32, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff}, {
    "ABGR", 32, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000}, {
    "RGBx", 32, 24, 0xff000000, 0x00ff0000, 0x0000ff00, 0x00000000}, {
    "xRGB", 32, 24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000}, {
    "BGRx", 32, 24, 0x0000ff00, 0x00ff0000, 0xff000000, 0x00000000}, {
    "xBGR", 32, 24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000}, {
    "RGB", 24, 24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000}, {
    "BGR", 24, 24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000}, {
    "RGB16", 16, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000}, {
    "RGB15", 16, 15, 0x00007c00, 0x000003e0, 0x0000001f, 0x0000000}
  };
  GstElement *pipeline, *src, *filter, *sink;
  GstCaps *template_caps;
  GstSample *sample = NULL;
  GstPad *srcpad;
  gint p, i, e;

  /* test check function */
  fail_unless (right_shift_colour (0x00ff0000, 0x11223344) == 0x22);

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("videotestsrc");
  filter = gst_check_setup_element ("capsfilter");
  sink = gst_check_setup_element ("fakesink");

  gst_bin_add_many (GST_BIN (pipeline), src, filter, sink, NULL);

  fail_unless (gst_element_link (src, filter));
  fail_unless (gst_element_link (filter, sink));

  srcpad = gst_element_get_static_pad (src, "src");
  template_caps = gst_pad_get_pad_template_caps (srcpad);

  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "preroll-handoff", G_CALLBACK (got_buf_cb), &sample);

  GST_LOG ("videotestsrc src template caps: %" GST_PTR_FORMAT, template_caps);

  for (i = 0; i < G_N_ELEMENTS (rgb_formats); ++i) {
    for (e = 0; e < 2; ++e) {
      GstCaps *caps;

      caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, rgb_formats[i].nick,
          "width", G_TYPE_INT, 16, "height", G_TYPE_INT, 16,
          "framerate", GST_TYPE_FRACTION, 1, 1, NULL);

      if (gst_caps_is_subset (caps, template_caps)) {
        /* caps are supported, let's run some tests then ... */
        for (p = 0; p < G_N_ELEMENTS (test_patterns); ++p) {
          GstStateChangeReturn state_ret;
          GstMapInfo map;

          g_object_set (src, "pattern", test_patterns[p].pattern_enum, NULL);

          GST_INFO ("%5s %u/%u %08x %08x %08x %08x, pattern=%s",
              rgb_formats[i].nick, rgb_formats[i].bpp, rgb_formats[i].depth,
              rgb_formats[i].red_mask, rgb_formats[i].green_mask,
              rgb_formats[i].blue_mask, rgb_formats[i].alpha_mask,
              test_patterns[p].pattern_name);

          /* now get videotestsrc to produce a buffer with the given caps */
          g_object_set (filter, "caps", caps, NULL);

          state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
          fail_unless (state_ret != GST_STATE_CHANGE_FAILURE,
              "pipeline _set_state() to PAUSED failed");
          state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
          fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS,
              "pipeline failed going to PAUSED state");

          state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
          fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

          fail_unless (sample != NULL);

          /* check buffer caps */
          {
            GstBuffer *buf;
            GstStructure *s;
            GstCaps *caps;
            const gchar *format;

            buf = gst_sample_get_buffer (sample);
            fail_unless (buf != NULL);
            caps = gst_sample_get_caps (sample);
            fail_unless (caps != NULL);

            s = gst_caps_get_structure (caps, 0);
            format = gst_structure_get_string (s, "format");
            fail_unless (g_str_equal (format, rgb_formats[i].nick));

            /* now check the first pixel */
            gst_buffer_map (buf, &map, GST_MAP_READ);
            check_rgb_buf (map.data, rgb_formats[i].red_mask,
                rgb_formats[i].green_mask, rgb_formats[i].blue_mask,
                rgb_formats[i].alpha_mask, test_patterns[p].r_expected,
                test_patterns[p].g_expected, test_patterns[p].b_expected,
                rgb_formats[i].bpp, rgb_formats[i].depth);
            gst_buffer_unmap (buf, &map);

            gst_sample_unref (sample);
            sample = NULL;
          }
        }

      } else {
        GST_INFO ("videotestsrc doesn't support format %" GST_PTR_FORMAT, caps);
      }

      gst_caps_unref (caps);
    }
  }
  gst_caps_unref (template_caps);
  gst_object_unref (srcpad);

  gst_object_unref (pipeline);
}

GST_END_TEST;


/* FIXME: add tests for YUV formats */

static Suite *
videotestsrc_suite (void)
{
  Suite *s = suite_create ("videotestsrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    /* test_rgb_formats takes a bit longer, so increase timeout */
    tcase_set_timeout (tc_chain, 5 * 60);
  }
#endif

  tcase_add_test (tc_chain, test_all_patterns);
  tcase_add_test (tc_chain, test_rgb_formats);

  return s;
}

GST_CHECK_MAIN (videotestsrc);
