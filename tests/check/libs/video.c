/* GStreamer unit test for video
 *
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2006> Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) <2008,2011> Tim-Philipp Müller <tim centricular net>
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
#include <gst/video/video-overlay-composition.h>
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
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * p->height;
  p->vstride = GST_ROUND_UP_4 (p->ystride / 4);
  p->up = p->vp + p->vstride * (GST_ROUND_UP_4 (p->height) / 4);
  p->ustride = GST_ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->up + p->ustride * (GST_ROUND_UP_4 (p->height) / 4);
}

static void
paint_setup_YUV9 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->ustride = GST_ROUND_UP_4 (p->ystride / 4);
  p->vp = p->up + p->ustride * (GST_ROUND_UP_4 (p->height) / 4);
  p->vstride = GST_ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->vp + p->vstride * (GST_ROUND_UP_4 (p->height) / 4);
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
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
      return FALSE;
    case GST_VIDEO_FORMAT_IYU1:
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
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
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
        off0 = gst_video_format_get_component_offset (fmt, 0, w, h);
        off1 = gst_video_format_get_component_offset (fmt, 1, w, h);
        off2 = gst_video_format_get_component_offset (fmt, 2, w, h);

        fail_unless_equals_int (size, (unsigned long) paintinfo.endptr);
        fail_unless_equals_int (off0, (unsigned long) paintinfo.yp);
        fail_unless_equals_int (off1, (unsigned long) paintinfo.up);
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

GST_START_TEST (test_video_formats_rgb)
{
  gint width, height, framerate_n, framerate_d, par_n, par_d;
  GstCaps *caps =
      gst_video_format_new_caps (GST_VIDEO_FORMAT_RGB, 800, 600, 0, 1, 1, 1);
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  fail_unless (gst_structure_get_int (structure, "width", &width));
  fail_unless (gst_structure_get_int (structure, "height", &height));
  fail_unless (gst_structure_get_fraction (structure, "framerate", &framerate_n,
          &framerate_d));
  fail_unless (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &par_n, &par_d));

  fail_unless (width == 800);
  fail_unless (height == 600);
  fail_unless (framerate_n == 0);
  fail_unless (framerate_d == 1);
  fail_unless (par_n == 1);
  fail_unless (par_d == 1);

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_video_template_caps)
{
  GstCaps *caps = gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB);
  gst_caps_unref (caps);
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

GST_START_TEST (test_convert_frame)
{
  GstCaps *from_caps, *to_caps;
  GstBuffer *from_buffer, *to_buffer;
  GError *error = NULL;
  gint i;
  guint8 *data;

  gst_debug_set_threshold_for_name ("default", GST_LEVEL_NONE);

  from_buffer = gst_buffer_new_and_alloc (640 * 480 * 4);
  data = GST_BUFFER_DATA (from_buffer);

  for (i = 0; i < 640 * 480; i++) {
    data[4 * i + 0] = 0;        /* x */
    data[4 * i + 1] = 255;      /* R */
    data[4 * i + 2] = 0;        /* G */
    data[4 * i + 3] = 0;        /* B */
  }
  from_caps = gst_video_format_new_caps (GST_VIDEO_FORMAT_xRGB,
      640, 480, 25, 1, 1, 1);
  gst_buffer_set_caps (from_buffer, from_caps);

  to_caps =
      gst_caps_from_string
      ("something/that, does=(string)not, exist=(boolean)FALSE");

  to_buffer =
      gst_video_convert_frame (from_buffer, to_caps, GST_CLOCK_TIME_NONE,
      &error);
  fail_if (to_buffer != NULL);
  fail_unless (error != NULL);
  g_error_free (error);
  error = NULL;

  gst_caps_unref (to_caps);
  to_caps =
      gst_video_format_new_caps (GST_VIDEO_FORMAT_I420, 240, 320, 25, 1, 1, 2);
  to_buffer =
      gst_video_convert_frame (from_buffer, to_caps, GST_CLOCK_TIME_NONE,
      &error);
  fail_unless (to_buffer != NULL);
  fail_unless (gst_caps_can_intersect (to_caps, GST_BUFFER_CAPS (to_buffer)));
  fail_unless (error == NULL);

  gst_buffer_unref (from_buffer);
  gst_caps_unref (from_caps);
  gst_buffer_unref (to_buffer);
  gst_caps_unref (to_caps);
}

GST_END_TEST;

typedef struct
{
  GMainLoop *loop;
  GstBuffer *buffer;
  GError *error;
} ConvertFrameContext;

static void
convert_frame_async_callback (GstBuffer * buf, GError * err,
    ConvertFrameContext * cf_data)
{
  cf_data->buffer = buf;
  cf_data->error = err;

  g_main_loop_quit (cf_data->loop);
}

GST_START_TEST (test_convert_frame_async)
{
  GstCaps *from_caps, *to_caps;
  GstBuffer *from_buffer;
  gint i;
  guint8 *data;
  GMainLoop *loop;
  ConvertFrameContext cf_data = { NULL, NULL, NULL };

  gst_debug_set_threshold_for_name ("default", GST_LEVEL_NONE);

  from_buffer = gst_buffer_new_and_alloc (640 * 480 * 4);
  data = GST_BUFFER_DATA (from_buffer);

  for (i = 0; i < 640 * 480; i++) {
    data[4 * i + 0] = 0;        /* x */
    data[4 * i + 1] = 255;      /* R */
    data[4 * i + 2] = 0;        /* G */
    data[4 * i + 3] = 0;        /* B */
  }
  from_caps = gst_video_format_new_caps (GST_VIDEO_FORMAT_xRGB,
      640, 480, 25, 1, 1, 1);
  gst_buffer_set_caps (from_buffer, from_caps);

  to_caps =
      gst_caps_from_string
      ("something/that, does=(string)not, exist=(boolean)FALSE");

  loop = cf_data.loop = g_main_loop_new (NULL, FALSE);

  gst_video_convert_frame_async (from_buffer, to_caps, GST_CLOCK_TIME_NONE,
      (GstVideoConvertFrameCallback) convert_frame_async_callback, &cf_data,
      NULL);

  g_main_loop_run (loop);

  fail_if (cf_data.buffer != NULL);
  fail_unless (cf_data.error != NULL);
  g_error_free (cf_data.error);
  cf_data.error = NULL;

  gst_caps_unref (to_caps);
  to_caps =
      gst_video_format_new_caps (GST_VIDEO_FORMAT_I420, 240, 320, 25, 1, 1, 2);
  gst_video_convert_frame_async (from_buffer, to_caps, GST_CLOCK_TIME_NONE,
      (GstVideoConvertFrameCallback) convert_frame_async_callback, &cf_data,
      NULL);
  g_main_loop_run (loop);
  fail_unless (cf_data.buffer != NULL);
  fail_unless (gst_caps_can_intersect (to_caps,
          GST_BUFFER_CAPS (cf_data.buffer)));
  fail_unless (cf_data.error == NULL);

  gst_buffer_unref (from_buffer);
  gst_caps_unref (from_caps);
  gst_buffer_unref (cf_data.buffer);
  gst_caps_unref (to_caps);

  g_main_loop_unref (loop);
}

GST_END_TEST;

GST_START_TEST (test_video_size_from_caps)
{
  gint size;
  guint32 fourcc = GST_MAKE_FOURCC ('Y', 'V', '1', '2');
  GstCaps *caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, fourcc,
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480,
      "framerate", GST_TYPE_FRACTION, 25, 1,
      NULL);

  fail_unless (gst_video_get_size_from_caps (caps, &size));
  fail_unless (size ==
      gst_video_format_get_size (gst_video_format_from_fourcc (fourcc), 640,
          480));
  fail_unless (size == (640 * 480 * 12 / 8));

  gst_caps_unref (caps);
}

GST_END_TEST;

#undef ASSERT_CRITICAL
#define ASSERT_CRITICAL(code) while(0){}        /* nothing */

GST_START_TEST (test_overlay_composition)
{
  GstVideoOverlayComposition *comp1, *comp2;
  GstVideoOverlayRectangle *rect1, *rect2;
  GstBuffer *pix1, *pix2, *buf;
  guint seq1, seq2;
  guint w, h, stride;
  gint x, y;

  pix1 = gst_buffer_new_and_alloc (200 * sizeof (guint32) * 50);
  memset (GST_BUFFER_DATA (pix1), 0, GST_BUFFER_SIZE (pix1));

  rect1 = gst_video_overlay_rectangle_new_argb (pix1, 200, 50, 200 * 4,
      600, 50, 300, 50, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

  gst_buffer_unref (pix1);
  pix1 = NULL;

  comp1 = gst_video_overlay_composition_new (rect1);
  fail_unless (gst_video_overlay_composition_n_rectangles (comp1) == 1);
  fail_unless (gst_video_overlay_composition_get_rectangle (comp1, 0) == rect1);
  fail_unless (gst_video_overlay_composition_get_rectangle (comp1, 1) == NULL);

  /* rectangle was created first, sequence number should be smaller */
  seq1 = gst_video_overlay_rectangle_get_seqnum (rect1);
  seq2 = gst_video_overlay_composition_get_seqnum (comp1);
  fail_unless (seq1 < seq2);

  /* composition took own ref, so refcount is 2 now, so this should fail */
  ASSERT_CRITICAL (gst_video_overlay_rectangle_set_render_rectangle (rect1, 50,
          600, 300, 50));

  /* drop our ref, so refcount is 1 (we know it will continue to be valid) */
  gst_video_overlay_rectangle_unref (rect1);
  gst_video_overlay_rectangle_set_render_rectangle (rect1, 50, 600, 300, 50);

  comp2 = gst_video_overlay_composition_new (rect1);
  fail_unless (gst_video_overlay_composition_n_rectangles (comp2) == 1);
  fail_unless (gst_video_overlay_composition_get_rectangle (comp2, 0) == rect1);
  fail_unless (gst_video_overlay_composition_get_rectangle (comp2, 1) == NULL);

  fail_unless (seq1 < gst_video_overlay_composition_get_seqnum (comp2));
  fail_unless (seq2 < gst_video_overlay_composition_get_seqnum (comp2));

  /* now refcount is 2 again because comp2 has also taken a ref, so must fail */
  ASSERT_CRITICAL (gst_video_overlay_rectangle_set_render_rectangle (rect1, 0,
          0, 1, 1));

  /* this should make a copy of the rectangles so drop the original
   * second ref on rect1 */
  comp2 = gst_video_overlay_composition_make_writable (comp2);
  gst_video_overlay_rectangle_set_render_rectangle (rect1, 51, 601, 301, 51);

  rect2 = gst_video_overlay_composition_get_rectangle (comp2, 0);
  fail_unless (gst_video_overlay_composition_n_rectangles (comp2) == 1);
  fail_unless (gst_video_overlay_composition_get_rectangle (comp2, 0) == rect2);
  fail_unless (gst_video_overlay_composition_get_rectangle (comp2, 1) == NULL);
  fail_unless (rect1 != rect2);

  gst_video_overlay_composition_add_rectangle (comp1, rect2);
  gst_video_overlay_composition_ref (comp1);
  ASSERT_CRITICAL (gst_video_overlay_composition_add_rectangle (comp1, rect2));
  gst_video_overlay_composition_unref (comp1);

  /* make sure the copy really worked */
  gst_video_overlay_rectangle_get_render_rectangle (rect1, &x, &y, &w, &h);
  fail_unless_equals_int (x, 51);
  fail_unless_equals_int (y, 601);
  fail_unless_equals_int (w, 301);
  fail_unless_equals_int (h, 51);

  /* get scaled pixbuf and touch last byte */
  pix1 = gst_video_overlay_rectangle_get_pixels_argb (rect1, &stride,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (GST_BUFFER_SIZE (pix1) > ((h - 1) * stride + (w * 4) - 1),
      "size %u vs. last pixel offset %u", GST_BUFFER_SIZE (pix1),
      ((h - 1) * stride + (w * 4) - 1));
  fail_unless_equals_int (*(GST_BUFFER_DATA (pix1) + ((h - 1) * stride +
              (w * 4) - 1)), 0);

  gst_video_overlay_rectangle_get_render_rectangle (rect2, &x, &y, &w, &h);
  fail_unless_equals_int (x, 50);
  fail_unless_equals_int (y, 600);
  fail_unless_equals_int (w, 300);
  fail_unless_equals_int (h, 50);

  /* get scaled pixbuf and touch last byte */
  pix2 = gst_video_overlay_rectangle_get_pixels_argb (rect2, &stride,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (GST_BUFFER_SIZE (pix2) > ((h - 1) * stride + (w * 4) - 1),
      "size %u vs. last pixel offset %u", GST_BUFFER_SIZE (pix1),
      ((h - 1) * stride + (w * 4) - 1));
  fail_unless_equals_int (*(GST_BUFFER_DATA (pix2) + ((h - 1) * stride +
              (w * 4) - 1)), 0);

  /* get scaled pixbuf again, should be the same buffer as before (caching) */
  pix1 = gst_video_overlay_rectangle_get_pixels_argb (rect2, &stride,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 == pix2);

  /* now compare the original unscaled ones */
  pix1 = gst_video_overlay_rectangle_get_pixels_unscaled_argb (rect1, &w, &h,
      &stride, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_argb (rect2, &w, &h,
      &stride, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

  /* the original pixel buffers should be identical */
  fail_unless (pix1 == pix2);
  fail_unless_equals_int (w, 200);
  fail_unless_equals_int (h, 50);

  /* touch last byte */
  fail_unless (GST_BUFFER_SIZE (pix1) > ((h - 1) * stride + (w * 4) - 1),
      "size %u vs. last pixel offset %u", GST_BUFFER_SIZE (pix1),
      ((h - 1) * stride + (w * 4) - 1));
  fail_unless_equals_int (*(GST_BUFFER_DATA (pix1) + ((h - 1) * stride +
              (w * 4) - 1)), 0);

  /* test attaching and retrieving of compositions to/from buffers */
  buf = gst_buffer_new ();
  fail_unless (gst_video_buffer_get_overlay_composition (buf) == NULL);

  gst_buffer_ref (buf);
  ASSERT_CRITICAL (gst_video_buffer_set_overlay_composition (buf, comp1));
  gst_buffer_unref (buf);
  gst_video_buffer_set_overlay_composition (buf, comp1);
  fail_unless (gst_video_buffer_get_overlay_composition (buf) == comp1);
  gst_video_buffer_set_overlay_composition (buf, comp2);
  fail_unless (gst_video_buffer_get_overlay_composition (buf) == comp2);
  gst_video_buffer_set_overlay_composition (buf, NULL);
  fail_unless (gst_video_buffer_get_overlay_composition (buf) == NULL);

  /* make sure the buffer cleans up its composition ref when unreffed */
  gst_video_buffer_set_overlay_composition (buf, comp2);
  gst_buffer_unref (buf);

  gst_video_overlay_composition_unref (comp2);
  gst_video_overlay_composition_unref (comp1);
}

GST_END_TEST;

static Suite *
video_suite (void)
{
  Suite *s = suite_create ("video support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_video_formats);
  tcase_add_test (tc_chain, test_video_formats_rgb);
  tcase_add_test (tc_chain, test_video_template_caps);
  tcase_add_test (tc_chain, test_dar_calc);
  tcase_add_test (tc_chain, test_parse_caps_rgb);
  tcase_add_test (tc_chain, test_events);
  tcase_add_test (tc_chain, test_convert_frame);
  tcase_add_test (tc_chain, test_convert_frame_async);
  tcase_add_test (tc_chain, test_video_size_from_caps);
  tcase_add_test (tc_chain, test_overlay_composition);

  return s;
}

GST_CHECK_MAIN (video);
