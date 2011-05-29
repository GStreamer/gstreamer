/* GStreamer
 *
 * unit test for ffmpegcolorspace
 *
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

typedef struct _RGBFormat
{
  const gchar *nick;
  guint bpp, depth;
  guint32 red_mask, green_mask, blue_mask, alpha_mask;
  guint endianness;
} RGBFormat;

typedef struct _RGBConversion
{
  RGBFormat from_fmt;
  RGBFormat to_fmt;
  GstCaps *from_caps;
  GstCaps *to_caps;
} RGBConversion;

static GstCaps *
rgb_format_to_caps (RGBFormat * fmt)
{
  GstCaps *caps;

  g_assert (fmt != NULL);
  g_assert (fmt->endianness != 0);

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "bpp", G_TYPE_INT, fmt->bpp,
      "depth", G_TYPE_INT, fmt->depth,
      "red_mask", G_TYPE_INT, fmt->red_mask,
      "green_mask", G_TYPE_INT, fmt->green_mask,
      "blue_mask", G_TYPE_INT, fmt->blue_mask,
      "width", G_TYPE_INT, 16, "height", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, fmt->endianness,
      "framerate", GST_TYPE_FRACTION, 1, 1, NULL);

  fail_unless (fmt->alpha_mask == 0 || fmt->bpp == 32);

  if (fmt->alpha_mask != 0) {
    gst_structure_set (gst_caps_get_structure (caps, 0),
        "alpha_mask", G_TYPE_INT, fmt->alpha_mask, NULL);
  }

  return caps;
}

static GList *
create_rgb_conversions (void)
{
  const RGBFormat rgb_formats[] = {
    {
        "RGBA", 32, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff, 0}, {
        "ARGB", 32, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, 0}, {
        "BGRA", 32, 32, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff, 0}, {
        "ABGR", 32, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000, 0}, {
        "RGBx", 32, 24, 0xff000000, 0x00ff0000, 0x0000ff00, 0x00000000, 0}, {
        "xRGB", 32, 24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000, 0}, {
        "BGRx", 32, 24, 0x0000ff00, 0x00ff0000, 0xff000000, 0x00000000, 0}, {
        "xBGR", 32, 24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000, 0}, {
        "RGB ", 24, 24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000, 0}, {
        "BGR ", 24, 24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000, 0}, {
        "RGB565", 16, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000, 0}, {
        "xRGB1555", 16, 15, 0x00007c00, 0x000003e0, 0x0000001f, 0x0000000, 0}
  };
  const struct
  {
    guint from_endianness, to_endianness;
  } end_arr[4] = {
    {
    G_LITTLE_ENDIAN, G_LITTLE_ENDIAN}, {
    G_BIG_ENDIAN, G_LITTLE_ENDIAN}, {
    G_LITTLE_ENDIAN, G_BIG_ENDIAN}, {
    G_BIG_ENDIAN, G_BIG_ENDIAN}
  };
  GList *conversions = NULL;
  guint from_fmt, to_fmt;

  for (from_fmt = 0; from_fmt < G_N_ELEMENTS (rgb_formats); ++from_fmt) {
    for (to_fmt = 0; to_fmt < G_N_ELEMENTS (rgb_formats); ++to_fmt) {
      guint i;

      for (i = 0; i < 4; ++i) {
        RGBConversion *conversion;

        conversion = g_new0 (RGBConversion, 1);
        conversion->from_fmt = rgb_formats[from_fmt];
        conversion->to_fmt = rgb_formats[to_fmt];
        conversion->from_fmt.endianness = end_arr[i].from_endianness;
        conversion->to_fmt.endianness = end_arr[i].to_endianness;
        conversion->from_caps = rgb_format_to_caps (&conversion->from_fmt);
        conversion->to_caps = rgb_format_to_caps (&conversion->to_fmt);
        conversions = g_list_prepend (conversions, conversion);
      }
    }
  }

  return g_list_reverse (conversions);
}

static void
rgb_conversion_free (RGBConversion * conv)
{
  gst_caps_unref (conv->from_caps);
  gst_caps_unref (conv->to_caps);
  memset (conv, 0x99, sizeof (RGBConversion));
  g_free (conv);
}

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
    guint8 b_expected, guint endianness, guint bpp, guint depth)
{
  guint32 pixel, red, green, blue;

  switch (bpp) {
    case 32:{
      if (endianness == G_LITTLE_ENDIAN)
        pixel = GST_READ_UINT32_LE (pixels);
      else
        pixel = GST_READ_UINT32_BE (pixels);
      break;
    }
    case 24:{
      if (endianness == G_BIG_ENDIAN) {
        pixel = (GST_READ_UINT8 (pixels) << 16) |
            (GST_READ_UINT8 (pixels + 1) << 8) |
            (GST_READ_UINT8 (pixels + 2) << 0);
      } else {
        pixel = (GST_READ_UINT8 (pixels + 2) << 16) |
            (GST_READ_UINT8 (pixels + 1) << 8) |
            (GST_READ_UINT8 (pixels + 0) << 0);
      }
      break;
    }
    case 16:{
      if (endianness == G_LITTLE_ENDIAN)
        pixel = GST_READ_UINT16_LE (pixels);
      else
        pixel = GST_READ_UINT16_BE (pixels);
      break;
    }
    default:
      g_return_if_reached ();
  }

  red = right_shift_colour (r_mask, pixel);
  green = right_shift_colour (g_mask, pixel);
  blue = right_shift_colour (b_mask, pixel);
  /* alpha = right_shift_colour (a_mask, pixel); */

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

  fail_unless (red == r_expected, "RED: expected 0x%02x, found 0x%02x    "
      "Bytes: 0x%02x 0x%02x 0x%02x 0x%02x    Pixel: 0x%08x", r_expected, red,
      pixels[0], pixels[1], pixels[2], pixels[3], pixel);
  fail_unless (green == g_expected, "GREEN: expected 0x%02x, found 0x%02x    "
      "Bytes: 0x%02x 0x%02x 0x%02x 0x%02x    Pixel: 0x%08x", g_expected, green,
      pixels[0], pixels[1], pixels[2], pixels[3], pixel);
  fail_unless (blue == b_expected, "BLUE: expected 0x%02x, found 0x%02x    "
      "Bytes: 0x%02x 0x%02x 0x%02x 0x%02x    Pixel: 0x%08x", b_expected, blue,
      pixels[0], pixels[1], pixels[2], pixels[3], pixel);

//  FIXME: fix alpha check
//  fail_unless (a_mask == 0 || alpha != 0);      /* better than nothing */
}

static void
got_buf_cb (GstElement * sink, GstBuffer * new_buf, GstPad * pad,
    GstBuffer ** p_old_buf)
{
  gst_buffer_replace (p_old_buf, new_buf);
}

/* Note: lots of this code here is also in the videotestsrc.c unit test */
GST_START_TEST (test_rgb_to_rgb)
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
  GstElement *pipeline, *src, *filter1, *csp, *filter2, *sink;
  const GstCaps *template_caps;
  GstBuffer *buf = NULL;
  GstPad *srcpad;
  GList *conversions, *l;
  gint p;

  /* test check function */
  fail_unless (right_shift_colour (0x00ff0000, 0x11223344) == 0x22);

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("videotestsrc");
  filter1 = gst_check_setup_element ("capsfilter");
  csp = gst_check_setup_element ("ffmpegcolorspace");
  filter2 = gst_element_factory_make ("capsfilter", "to_filter");
  sink = gst_check_setup_element ("fakesink");

  gst_bin_add_many (GST_BIN (pipeline), src, filter1, csp, filter2, sink, NULL);

  fail_unless (gst_element_link (src, filter1));
  fail_unless (gst_element_link (filter1, csp));
  fail_unless (gst_element_link (csp, filter2));
  fail_unless (gst_element_link (filter2, sink));

  srcpad = gst_element_get_static_pad (src, "src");
  template_caps = gst_pad_get_pad_template_caps (srcpad);
  gst_object_unref (srcpad);

  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "preroll-handoff", G_CALLBACK (got_buf_cb), &buf);

  GST_LOG ("videotestsrc src template caps: %" GST_PTR_FORMAT, template_caps);

  conversions = create_rgb_conversions ();

  for (l = conversions; l != NULL; l = l->next) {
    RGBConversion *conv = (RGBConversion *) l->data;

    /* does videotestsrc support the from_caps? */
    if (!gst_caps_is_subset (conv->from_caps, template_caps)) {
      GST_DEBUG ("videotestsrc doesn't support from_caps %" GST_PTR_FORMAT,
          conv->from_caps);
      continue;
    }

    /* caps are supported, let's run some tests then ... */
    for (p = 0; p < G_N_ELEMENTS (test_patterns); ++p) {
      GstStateChangeReturn state_ret;
      RGBFormat *from = &conv->from_fmt;
      RGBFormat *to = &conv->to_fmt;

      /* trick compiler into thinking from is used, might throw warning
       * otherwise if the debugging system is disabled */
      fail_unless (from != NULL);

      gst_element_set_state (pipeline, GST_STATE_NULL);

      g_object_set (src, "pattern", test_patterns[p].pattern_enum, NULL);

      GST_INFO ("%5s %u/%u %08x %08x %08x %08x %u => "
          "%5s %u/%u %08x %08x %08x %08x %u, pattern=%s",
          from->nick, from->bpp, from->depth, from->red_mask,
          from->green_mask, from->blue_mask, from->alpha_mask,
          from->endianness, to->nick, to->bpp, to->depth, to->red_mask,
          to->green_mask, to->blue_mask, to->alpha_mask, to->endianness,
          test_patterns[p].pattern_name);

      /* now get videotestsrc to produce a buffer with the given caps */
      g_object_set (filter1, "caps", conv->from_caps, NULL);

      /* ... and force ffmpegcolorspace to convert to our target caps */
      g_object_set (filter2, "caps", conv->to_caps, NULL);

      state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
      if (state_ret == GST_STATE_CHANGE_FAILURE) {
        GstMessage *msg;
        GError *err = NULL;

        msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline), GST_MESSAGE_ERROR, 0);
        fail_if (msg == NULL, "expected ERROR message on the bus");
        fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        gst_message_parse_error (msg, &err, NULL);
        fail_unless (err != NULL);
        if (msg->src == GST_OBJECT_CAST (src) &&
            err->code == GST_STREAM_ERROR_FORMAT) {
          GST_DEBUG ("ffmpegcolorspace does not support this conversion");
          gst_message_unref (msg);
          g_error_free (err);
          continue;
        }
        fail_unless (state_ret != GST_STATE_CHANGE_FAILURE,
            "pipeline _set_state() to PAUSED failed: %s", err->message);
      }

      state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
      fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS,
          "pipeline failed going to PAUSED state");

      state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
      fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

      fail_unless (buf != NULL);

      /* check buffer caps */
      {
        GstStructure *s;
        gint v;

        fail_unless (GST_BUFFER_CAPS (buf) != NULL);
        s = gst_caps_get_structure (GST_BUFFER_CAPS (buf), 0);
        fail_unless (gst_structure_get_int (s, "bpp", &v));
        fail_unless_equals_int (v, to->bpp);
        fail_unless (gst_structure_get_int (s, "depth", &v));
        fail_unless_equals_int (v, to->depth);
        fail_unless (gst_structure_get_int (s, "red_mask", &v));
        fail_unless_equals_int (v, to->red_mask);
        fail_unless (gst_structure_get_int (s, "green_mask", &v));
        fail_unless_equals_int (v, to->green_mask);
        fail_unless (gst_structure_get_int (s, "blue_mask", &v));
        fail_unless_equals_int (v, to->blue_mask);
        /* there mustn't be an alpha_mask if there's no alpha component */
        if (to->depth == 32) {
          fail_unless (gst_structure_get_int (s, "alpha_mask", &v));
          fail_unless_equals_int (v, to->alpha_mask);
        } else {
          fail_unless (gst_structure_get_value (s, "alpha_mask") == NULL);
        }
      }

      /* now check the top-left pixel */
      check_rgb_buf (GST_BUFFER_DATA (buf), to->red_mask,
          to->green_mask, to->blue_mask, to->alpha_mask,
          test_patterns[p].r_expected, test_patterns[p].g_expected,
          test_patterns[p].b_expected, to->endianness, to->bpp, to->depth);

      gst_buffer_unref (buf);
      buf = NULL;
    }
  }

  g_list_foreach (conversions, (GFunc) rgb_conversion_free, NULL);
  g_list_free (conversions);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
ffmpegcolorspace_suite (void)
{
  Suite *s = suite_create ("ffmpegcolorspace");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    /* otherwise valgrind errors out when liboil probes CPU extensions
     * during which it causes SIGILLs etc. to be fired */
    g_setenv ("OIL_CPU_FLAGS", "0", 0);
    /* test_rgb_formats takes a bit longer, so increase timeout */
    tcase_set_timeout (tc_chain, 10 * 60);
  }
#endif

  /* FIXME: add tests for YUV <=> YUV and YUV <=> RGB */
  tcase_add_test (tc_chain, test_rgb_to_rgb);

  return s;
}

GST_CHECK_MAIN (ffmpegcolorspace);
