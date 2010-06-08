/* GStreamer unit test for video
 *
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2006> Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) <2008> Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>

#include <gst/video/video.h>
#include <string.h>

/* These are from the current/old videotestsrc; we check our new public API
 * in libgstvideo against the old one to make sure the sizes and offsets
 * end up the same */

typedef struct paintinfo_struct paintinfo;
struct paintinfo_struct
{
  unsigned char *dest;          /* pointer to first byte of video data */
  unsigned char *yp, *up, *vp;  /* pointers to first byte of each component
                                 * for both packed/planar YUV and RGB */
  unsigned char *ap;            /* pointer to first byte of alpha component */
  unsigned char *endptr;        /* pointer to byte beyond last video data */
  int ystride;
  int ustride;
  int vstride;
  int width;
  int height;
};

struct fourcc_list_struct
{
  const char *fourcc;
  const char *name;
  int bitspp;
  void (*paint_setup) (paintinfo * p, unsigned char *dest);
};

static void paint_setup_I420 (paintinfo * p, unsigned char *dest);
static void paint_setup_YV12 (paintinfo * p, unsigned char *dest);
static void paint_setup_YUY2 (paintinfo * p, unsigned char *dest);
static void paint_setup_UYVY (paintinfo * p, unsigned char *dest);
static void paint_setup_YVYU (paintinfo * p, unsigned char *dest);
static void paint_setup_IYU2 (paintinfo * p, unsigned char *dest);
static void paint_setup_Y41B (paintinfo * p, unsigned char *dest);
static void paint_setup_Y42B (paintinfo * p, unsigned char *dest);
static void paint_setup_Y800 (paintinfo * p, unsigned char *dest);
static void paint_setup_AYUV (paintinfo * p, unsigned char *dest);

#if 0
static void paint_setup_IMC1 (paintinfo * p, unsigned char *dest);
static void paint_setup_IMC2 (paintinfo * p, unsigned char *dest);
static void paint_setup_IMC3 (paintinfo * p, unsigned char *dest);
static void paint_setup_IMC4 (paintinfo * p, unsigned char *dest);
#endif
static void paint_setup_YUV9 (paintinfo * p, unsigned char *dest);
static void paint_setup_YVU9 (paintinfo * p, unsigned char *dest);

int fourcc_get_size (struct fourcc_list_struct *fourcc, int w, int h);

struct fourcc_list_struct fourcc_list[] = {
/* packed */
  {"YUY2", "YUY2", 16, paint_setup_YUY2},
  {"UYVY", "UYVY", 16, paint_setup_UYVY},
  {"Y422", "Y422", 16, paint_setup_UYVY},
  {"UYNV", "UYNV", 16, paint_setup_UYVY},       /* FIXME: UYNV? */
  {"YVYU", "YVYU", 16, paint_setup_YVYU},
  {"AYUV", "AYUV", 32, paint_setup_AYUV},

  /* interlaced */
  /*{   "IUYV", "IUY2", 16, paint_setup_YVYU }, */

  /* inverted */
  /*{   "cyuv", "cyuv", 16, paint_setup_YVYU }, */

  /*{   "Y41P", "Y41P", 12, paint_setup_YVYU }, */

  /* interlaced */
  /*{   "IY41", "IY41", 12, paint_setup_YVYU }, */

  /*{   "Y211", "Y211", 8, paint_setup_YVYU }, */

  /*{   "Y41T", "Y41T", 12, paint_setup_YVYU }, */
  /*{   "Y42P", "Y42P", 16, paint_setup_YVYU }, */
  /*{   "CLJR", "CLJR", 8, paint_setup_YVYU }, */
  /*{   "IYU1", "IYU1", 12, paint_setup_YVYU }, */
  {"IYU2", "IYU2", 24, paint_setup_IYU2},

/* planar */
  /* YVU9 */
  {"YVU9", "YVU9", 9, paint_setup_YVU9},
  /* YUV9 */
  {"YUV9", "YUV9", 9, paint_setup_YUV9},
  /* IF09 */
  /* YV12 */
  {"YV12", "YV12", 12, paint_setup_YV12},
  /* I420 */
  {"I420", "I420", 12, paint_setup_I420},
  /* NV12 */
  /* NV21 */
#if 0
  /* IMC1 */
  {"IMC1", "IMC1", 16, paint_setup_IMC1},
  /* IMC2 */
  {"IMC2", "IMC2", 12, paint_setup_IMC2},
  /* IMC3 */
  {"IMC3", "IMC3", 16, paint_setup_IMC3},
  /* IMC4 */
  {"IMC4", "IMC4", 12, paint_setup_IMC4},
#endif
  /* CLPL */
  /* Y41B */
  {"Y41B", "Y41B", 12, paint_setup_Y41B},
  /* Y42B */
  {"Y42B", "Y42B", 16, paint_setup_Y42B},
  /* Y800 grayscale */
  {"Y800", "Y800", 8, paint_setup_Y800}
};

/* returns the size in bytes for one video frame of the given dimensions
 * given the fourcc */
int
fourcc_get_size (struct fourcc_list_struct *fourcc, int w, int h)
{
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;

  p->width = w;
  p->height = h;

  fourcc->paint_setup (p, NULL);

  return (unsigned long) p->endptr;
}

static void
paint_setup_I420 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * GST_ROUND_UP_2 (p->height);
  p->ustride = GST_ROUND_UP_8 (p->width) / 2;
  p->vp = p->up + p->ustride * GST_ROUND_UP_2 (p->height) / 2;
  p->vstride = GST_ROUND_UP_8 (p->ystride) / 2;
  p->endptr = p->vp + p->vstride * GST_ROUND_UP_2 (p->height) / 2;
}

static void
paint_setup_YV12 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * GST_ROUND_UP_2 (p->height);
  p->vstride = GST_ROUND_UP_8 (p->ystride) / 2;
  p->up = p->vp + p->vstride * GST_ROUND_UP_2 (p->height) / 2;
  p->ustride = GST_ROUND_UP_8 (p->ystride) / 2;
  p->endptr = p->up + p->ustride * GST_ROUND_UP_2 (p->height) / 2;
}

static void
paint_setup_AYUV (paintinfo * p, unsigned char *dest)
{
  p->ap = dest;
  p->yp = dest + 1;
  p->up = dest + 2;
  p->vp = dest + 3;
  p->ystride = p->width * 4;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_YUY2 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->up = dest + 1;
  p->vp = dest + 3;
  p->ystride = GST_ROUND_UP_2 (p->width) * 2;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_UYVY (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 1;
  p->up = dest;
  p->vp = dest + 2;
  p->ystride = GST_ROUND_UP_2 (p->width) * 2;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_YVYU (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->up = dest + 3;
  p->vp = dest + 1;
  p->ystride = GST_ROUND_UP_2 (p->width) * 2;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_IYU2 (paintinfo * p, unsigned char *dest)
{
  /* untested */
  p->yp = dest + 1;
  p->up = dest + 0;
  p->vp = dest + 2;
  p->ystride = GST_ROUND_UP_4 (p->width * 3);
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_Y41B (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->ustride = GST_ROUND_UP_16 (p->width) / 4;
  p->vp = p->up + p->ustride * p->height;
  p->vstride = GST_ROUND_UP_16 (p->width) / 4;
  p->endptr = p->vp + p->vstride * p->height;
}

static void
paint_setup_Y42B (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->ustride = GST_ROUND_UP_8 (p->width) / 2;
  p->vp = p->up + p->ustride * p->height;
  p->vstride = GST_ROUND_UP_8 (p->width) / 2;
  p->endptr = p->vp + p->vstride * p->height;
}

static void
paint_setup_Y800 (paintinfo * p, unsigned char *dest)
{
  /* untested */
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->endptr = dest + p->ystride * p->height;
}

#if 0
static void
paint_setup_IMC1 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height;
  p->vp = dest + p->width * p->height + p->width * p->height / 2;
}

static void
paint_setup_IMC2 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->vp = dest + p->width * p->height;
  p->up = dest + p->width * p->height + p->width / 2;
}

static void
paint_setup_IMC3 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height + p->width * p->height / 2;
  p->vp = dest + p->width * p->height;
}

static void
paint_setup_IMC4 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->vp = dest + p->width * p->height + p->width / 2;
  p->up = dest + p->width * p->height;
}
#endif

static void
paint_setup_YVU9 (paintinfo * p, unsigned char *dest)
{
  int h = GST_ROUND_UP_4 (p->height);

  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * GST_ROUND_UP_4 (p->height);
  p->vstride = GST_ROUND_UP_4 (p->ystride / 4);
  p->up = p->vp + p->vstride * GST_ROUND_UP_4 (h / 4);
  p->ustride = GST_ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->up + p->ustride * GST_ROUND_UP_4 (h / 4);
}

static void
paint_setup_YUV9 (paintinfo * p, unsigned char *dest)
{
  /* untested */
  int h = GST_ROUND_UP_4 (p->height);

  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * h;
  p->ustride = GST_ROUND_UP_4 (p->ystride / 4);
  p->vp = p->up + p->ustride * GST_ROUND_UP_4 (h / 4);
  p->vstride = GST_ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->vp + p->vstride * GST_ROUND_UP_4 (h / 4);
}

#define gst_video_format_is_packed video_format_is_packed
static gboolean
video_format_is_packed (GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y800:
      return FALSE;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_YVYU:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      return TRUE;
    default:
      g_return_val_if_reached (FALSE);
  }
  return FALSE;
}

GST_START_TEST (test_video_formats)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (fourcc_list); ++i) {
    GstVideoFormat fmt;
    const gchar *s;
    guint32 fourcc;
    guint w, h;

    s = fourcc_list[i].fourcc;
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    fmt = gst_video_format_from_fourcc (fourcc);

    if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    GST_INFO ("Fourcc %s, packed=%d", fourcc_list[i].fourcc,
        gst_video_format_is_packed (fmt));

    fail_unless (gst_video_format_is_yuv (fmt));

    /* use any non-NULL pointer so we can compare against NULL */
    {
      paintinfo paintinfo = { 0, };
      fourcc_list[i].paint_setup (&paintinfo, (unsigned char *) s);
      if (paintinfo.ap != NULL) {
        fail_unless (gst_video_format_has_alpha (fmt));
      } else {
        fail_if (gst_video_format_has_alpha (fmt));
      }
    }

    for (w = 1; w <= 65; ++w) {
      for (h = 1; h <= 65; ++h) {
        paintinfo paintinfo = { 0, };
        guint off0, off1, off2, off3;
        guint size;

        GST_LOG ("%s, %dx%d", fourcc_list[i].fourcc, w, h);

        paintinfo.width = w;
        paintinfo.height = h;
        fourcc_list[i].paint_setup (&paintinfo, NULL);
        fail_unless_equals_int (gst_video_format_get_row_stride (fmt, 0, w),
            paintinfo.ystride);
        if (!gst_video_format_is_packed (fmt)
            && !gst_video_format_is_gray (fmt)) {
          /* planar */
          fail_unless_equals_int (gst_video_format_get_row_stride (fmt, 1, w),
              paintinfo.ustride);
          fail_unless_equals_int (gst_video_format_get_row_stride (fmt, 2, w),
              paintinfo.vstride);
          /* check component_width * height against offsets/size somehow? */
        }

        size = gst_video_format_get_size (fmt, w, h);
        fail_unless_equals_int (size, (unsigned long) paintinfo.endptr);

        off0 = gst_video_format_get_component_offset (fmt, 0, w, h);
        fail_unless_equals_int (off0, (unsigned long) paintinfo.yp);
        off1 = gst_video_format_get_component_offset (fmt, 1, w, h);
        fail_unless_equals_int (off1, (unsigned long) paintinfo.up);
        off2 = gst_video_format_get_component_offset (fmt, 2, w, h);
        fail_unless_equals_int (off2, (unsigned long) paintinfo.vp);

        /* should be 0 if there's no alpha component */
        off3 = gst_video_format_get_component_offset (fmt, 3, w, h);
        fail_unless_equals_int (off3, (unsigned long) paintinfo.ap);

        /* some gstvideo checks ... (FIXME: fails for Y41B and Y42B; not sure
         * if the check or the _get_component_size implementation is wrong) */
        if (fmt != GST_VIDEO_FORMAT_Y41B && fmt != GST_VIDEO_FORMAT_Y42B
            && fmt != GST_VIDEO_FORMAT_Y800) {
          guint cs0, cs1, cs2, cs3;

          cs0 = gst_video_format_get_component_width (fmt, 0, w) *
              gst_video_format_get_component_height (fmt, 0, h);
          cs1 = gst_video_format_get_component_width (fmt, 1, w) *
              gst_video_format_get_component_height (fmt, 1, h);
          cs2 = gst_video_format_get_component_width (fmt, 2, w) *
              gst_video_format_get_component_height (fmt, 2, h);

          /* GST_LOG ("cs0=%d,cs1=%d,cs2=%d,off0=%d,off1=%d,off2=%d,size=%d",
             cs0, cs1, cs2, off0, off1, off2, size); */

          if (!gst_video_format_is_packed (fmt))
            fail_unless (cs0 <= off1);

          if (gst_video_format_has_alpha (fmt)) {
            cs3 = gst_video_format_get_component_width (fmt, 3, w) *
                gst_video_format_get_component_height (fmt, 3, h);
            fail_unless (cs3 < size);
            /* U/V/alpha shouldn't take up more space than the Y component */
            fail_if (cs1 > cs0, "cs1 (%d) should be <= cs0 (%d)", cs1, cs0);
            fail_if (cs2 > cs0, "cs2 (%d) should be <= cs0 (%d)", cs2, cs0);
            fail_if (cs3 > cs0, "cs3 (%d) should be <= cs0 (%d)", cs3, cs0);

            /* all components together shouldn't take up more space than size */
            fail_unless (cs0 + cs1 + cs2 + cs3 <= size);
          } else {
            /* U/V shouldn't take up more space than the Y component */
            fail_if (cs1 > cs0, "cs1 (%d) should be <= cs0 (%d)", cs1, cs0);
            fail_if (cs2 > cs0, "cs2 (%d) should be <= cs0 (%d)", cs2, cs0);

            /* all components together shouldn't take up more space than size */
            fail_unless (cs0 + cs1 + cs2 <= size,
                "cs0 (%d) + cs1 (%d) + cs2 (%d) should be <= size (%d)",
                cs0, cs1, cs2, size);
          }
        }
      }
    }
  }
}

GST_END_TEST;

GST_START_TEST (test_dar_calc)
{
  guint display_ratio_n, display_ratio_d;

  /* Ensure that various Display Ratio calculations are correctly done */
  /* video 768x576, par 16/15, display par 16/15 = 4/3 */
  fail_unless (gst_video_calculate_display_ratio (&display_ratio_n,
          &display_ratio_d, 768, 576, 16, 15, 16, 15));
  fail_unless (display_ratio_n == 4 && display_ratio_d == 3);

  /* video 720x480, par 32/27, display par 1/1 = 16/9 */
  fail_unless (gst_video_calculate_display_ratio (&display_ratio_n,
          &display_ratio_d, 720, 480, 32, 27, 1, 1));
  fail_unless (display_ratio_n == 16 && display_ratio_d == 9);

  /* video 360x288, par 533333/500000, display par 16/15 = 
   * dar 1599999/1600000 */
  fail_unless (gst_video_calculate_display_ratio (&display_ratio_n,
          &display_ratio_d, 360, 288, 533333, 500000, 16, 15));
  fail_unless (display_ratio_n == 1599999 && display_ratio_d == 1280000);
}

GST_END_TEST;

GST_START_TEST (test_parse_caps_rgb)
{
  struct
  {
    const gchar *tmpl_caps_string;
    GstVideoFormat fmt;
  } formats[] = {
    /* 24 bit */
    {
    GST_VIDEO_CAPS_RGB, GST_VIDEO_FORMAT_RGB}, {
    GST_VIDEO_CAPS_BGR, GST_VIDEO_FORMAT_BGR},
        /* 32 bit (no alpha) */
    {
    GST_VIDEO_CAPS_RGBx, GST_VIDEO_FORMAT_RGBx}, {
    GST_VIDEO_CAPS_xRGB, GST_VIDEO_FORMAT_xRGB}, {
    GST_VIDEO_CAPS_BGRx, GST_VIDEO_FORMAT_BGRx}, {
    GST_VIDEO_CAPS_xBGR, GST_VIDEO_FORMAT_xBGR},
        /* 32 bit (with alpha) */
    {
    GST_VIDEO_CAPS_RGBA, GST_VIDEO_FORMAT_RGBA}, {
    GST_VIDEO_CAPS_ARGB, GST_VIDEO_FORMAT_ARGB}, {
    GST_VIDEO_CAPS_BGRA, GST_VIDEO_FORMAT_BGRA}, {
    GST_VIDEO_CAPS_ABGR, GST_VIDEO_FORMAT_ABGR}
  };
  gint i;

  for (i = 0; i < G_N_ELEMENTS (formats); ++i) {
    GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;
    GstCaps *caps, *caps2;
    int w = -1, h = -1;

    caps = gst_caps_from_string (formats[i].tmpl_caps_string);
    gst_caps_set_simple (caps, "width", G_TYPE_INT, 2 * (i + 1), "height",
        G_TYPE_INT, i + 1, "framerate", GST_TYPE_FRACTION, 15, 1,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
    g_assert (gst_caps_is_fixed (caps));

    GST_DEBUG ("testing caps: %" GST_PTR_FORMAT, caps);

    fail_unless (gst_video_format_parse_caps (caps, &fmt, &w, &h));
    fail_unless_equals_int (fmt, formats[i].fmt);
    fail_unless_equals_int (w, 2 * (i + 1));
    fail_unless_equals_int (h, i + 1);

    /* make sure they're serialised back correctly */
    caps2 = gst_video_format_new_caps (fmt, w, h, 15, 1, 1, 1);
    fail_unless (caps != NULL);
    fail_unless (gst_caps_is_equal (caps, caps2));

    gst_caps_unref (caps);
    gst_caps_unref (caps2);
  }
}

GST_END_TEST;

GST_START_TEST (test_events)
{
  GstEvent *e;
  gboolean in_still;

  e = gst_video_event_new_still_frame (TRUE);
  fail_if (e == NULL, "Failed to create still frame event");
  fail_unless (gst_video_event_parse_still_frame (e, &in_still),
      "Failed to parse still frame event");
  fail_unless (gst_video_event_parse_still_frame (e, NULL),
      "Failed to parse still frame event w/ in_still == NULL");
  fail_unless (in_still == TRUE);
  gst_event_unref (e);

  e = gst_video_event_new_still_frame (FALSE);
  fail_if (e == NULL, "Failed to create still frame event");
  fail_unless (gst_video_event_parse_still_frame (e, &in_still),
      "Failed to parse still frame event");
  fail_unless (gst_video_event_parse_still_frame (e, NULL),
      "Failed to parse still frame event w/ in_still == NULL");
  fail_unless (in_still == FALSE);
  gst_event_unref (e);
}

GST_END_TEST;

static Suite *
video_suite (void)
{
  Suite *s = suite_create ("video support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_video_formats);
  tcase_add_test (tc_chain, test_dar_calc);
  tcase_add_test (tc_chain, test_parse_caps_rgb);
  tcase_add_test (tc_chain, test_events);

  return s;
}

GST_CHECK_MAIN (video);
