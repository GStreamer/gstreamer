/* GStreamer unit test for video
 *
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2006> Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) <2008,2011> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2012> Collabora Ltd. <tim.muller@collabora.co.uk>
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

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
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
static void paint_setup_GRAY8 (paintinfo * p, unsigned char *dest);
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
  /* GRAY8 grayscale */
  {"GRAY8", "GRAY8", 8, paint_setup_GRAY8}
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
paint_setup_GRAY8 (paintinfo * p, unsigned char *dest)
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
    case GST_VIDEO_FORMAT_GRAY8:
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
    case GST_VIDEO_FORMAT_RGB8P:
      return TRUE;
    default:
      g_return_val_if_reached (FALSE);
  }
  return FALSE;
}

GST_START_TEST (test_video_formats_all)
{
  GstStructure *s;
  const GValue *val, *list_val;
  GstCaps *caps;
  guint num, n, num_formats;

  num_formats = 100;
  fail_unless (gst_video_format_to_string (num_formats) == NULL);
  while (gst_video_format_to_string (num_formats) == NULL)
    --num_formats;
  GST_INFO ("number of known video formats: %d", num_formats);

  caps = gst_caps_from_string ("video/x-raw, format=" GST_VIDEO_FORMATS_ALL);
  s = gst_caps_get_structure (caps, 0);
  val = gst_structure_get_value (s, "format");
  fail_unless (val != NULL);
  fail_unless (GST_VALUE_HOLDS_LIST (val));
  num = gst_value_list_get_size (val);
  fail_unless (num > 0);
  for (n = 0; n < num; ++n) {
    const gchar *fmt_str;

    list_val = gst_value_list_get_value (val, n);
    fail_unless (G_VALUE_HOLDS_STRING (list_val));
    fmt_str = g_value_get_string (list_val);
    GST_INFO ("format: %s", fmt_str);
    fail_if (gst_video_format_from_string (fmt_str) ==
        GST_VIDEO_FORMAT_UNKNOWN);
  }
  /* Take into account GST_VIDEO_FORMAT_ENCODED */
  fail_unless_equals_int (num, num_formats - 1);

  gst_caps_unref (caps);
}

GST_END_TEST;

#define WIDTH 77
#define HEIGHT 20

GST_START_TEST (test_video_formats_pack_unpack)
{
  guint n, num_formats;

  num_formats = 100;
  fail_unless (gst_video_format_to_string (num_formats) == NULL);
  while (gst_video_format_to_string (num_formats) == NULL)
    --num_formats;
  GST_INFO ("number of known video formats: %d", num_formats);

  for (n = GST_VIDEO_FORMAT_ENCODED + 1; n < num_formats; ++n) {
    const GstVideoFormatInfo *vfinfo, *unpackinfo;
    GstVideoFormat fmt = n;
    GstVideoInfo vinfo;
    gpointer data[GST_VIDEO_MAX_PLANES];
    gint stride[GST_VIDEO_MAX_PLANES];
    guint8 *vdata, *unpack_data;
    gsize vsize, unpack_size;
    guint p;

    GST_INFO ("testing %s", gst_video_format_to_string (fmt));

    vfinfo = gst_video_format_get_info (fmt);
    fail_unless (vfinfo != NULL);

    unpackinfo = gst_video_format_get_info (vfinfo->unpack_format);
    fail_unless (unpackinfo != NULL);

    gst_video_info_init (&vinfo);
    gst_video_info_set_format (&vinfo, fmt, WIDTH, HEIGHT);
    vsize = GST_VIDEO_INFO_SIZE (&vinfo);
    vdata = g_malloc (vsize);
    memset (vdata, 0x99, vsize);

    g_assert (vfinfo->pack_lines == 1);

    unpack_size =
        GST_VIDEO_FORMAT_INFO_BITS (unpackinfo) *
        GST_VIDEO_FORMAT_INFO_N_COMPONENTS (unpackinfo) *
        GST_ROUND_UP_16 (WIDTH);
    unpack_data = g_malloc (unpack_size);

    for (p = 0; p < GST_VIDEO_INFO_N_PLANES (&vinfo); ++p) {
      data[p] = vdata + GST_VIDEO_INFO_PLANE_OFFSET (&vinfo, p);
      stride[p] = GST_VIDEO_INFO_PLANE_STRIDE (&vinfo, p);
    }

    /* now unpack */
    vfinfo->unpack_func (vfinfo, GST_VIDEO_PACK_FLAG_NONE, unpack_data, data,
        stride, 0, 0, WIDTH);

    /* and pack */
    vfinfo->pack_func (vfinfo, GST_VIDEO_PACK_FLAG_NONE, unpack_data,
        unpack_size, data, stride, GST_VIDEO_CHROMA_SITE_UNKNOWN, 0, WIDTH);

    /* now unpack */
    vfinfo->unpack_func (vfinfo, GST_VIDEO_PACK_FLAG_NONE, unpack_data, data,
        stride, 0, HEIGHT - 1, WIDTH);

    /* and pack */
    vfinfo->pack_func (vfinfo, GST_VIDEO_PACK_FLAG_NONE, unpack_data,
        unpack_size, data, stride, GST_VIDEO_CHROMA_SITE_UNKNOWN, HEIGHT - 1,
        WIDTH);

    g_free (unpack_data);
    g_free (vdata);
  }
}

GST_END_TEST;

GST_START_TEST (test_video_formats)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (fourcc_list); ++i) {
    const GstVideoFormatInfo *vf_info;
    GstVideoFormat fmt;
    const gchar *s;
    guint32 fourcc;
    guint w, h;

    s = fourcc_list[i].fourcc;
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    fmt = gst_video_format_from_fourcc (fourcc);

    if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_DEBUG ("Unknown format %s, skipping tests", fourcc_list[i].fourcc);
      continue;
    }

    vf_info = gst_video_format_get_info (fmt);
    fail_unless (vf_info != NULL);

    fail_unless_equals_int (GST_VIDEO_FORMAT_INFO_FORMAT (vf_info), fmt);

    GST_INFO ("Fourcc %s, packed=%d", fourcc_list[i].fourcc,
        gst_video_format_is_packed (fmt));

    fail_unless (GST_VIDEO_FORMAT_INFO_IS_YUV (vf_info));

    /* use any non-NULL pointer so we can compare against NULL */
    {
      paintinfo paintinfo = { 0, };
      fourcc_list[i].paint_setup (&paintinfo, (unsigned char *) s);
      if (paintinfo.ap != NULL) {
        fail_unless (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (vf_info));
      } else {
        fail_if (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (vf_info));
      }
    }

    for (w = 1; w <= 65; ++w) {
      for (h = 1; h <= 65; ++h) {
        GstVideoInfo vinfo;
        paintinfo paintinfo = { 0, };
        guint off0, off1, off2, off3;
        guint cs0, cs1, cs2, cs3;
        guint size;

        GST_LOG ("%s, %dx%d", fourcc_list[i].fourcc, w, h);

        gst_video_info_init (&vinfo);
        gst_video_info_set_format (&vinfo, fmt, w, h);

        paintinfo.width = w;
        paintinfo.height = h;
        fourcc_list[i].paint_setup (&paintinfo, NULL);
        fail_unless_equals_int (GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 0),
            paintinfo.ystride);
        if (!gst_video_format_is_packed (fmt)
            && GST_VIDEO_INFO_N_PLANES (&vinfo) <= 2) {
          /* planar */
          fail_unless_equals_int (GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 1),
              paintinfo.ustride);
          fail_unless_equals_int (GST_VIDEO_INFO_COMP_STRIDE (&vinfo, 2),
              paintinfo.vstride);
          /* check component_width * height against offsets/size somehow? */
        }

        size = GST_VIDEO_INFO_SIZE (&vinfo);
        off0 = GST_VIDEO_INFO_COMP_OFFSET (&vinfo, 0);
        off1 = GST_VIDEO_INFO_COMP_OFFSET (&vinfo, 1);
        off2 = GST_VIDEO_INFO_COMP_OFFSET (&vinfo, 2);

        GST_INFO ("size %d <> %d", size, (int) ((guintptr) paintinfo.endptr));
        GST_INFO ("off0 %d <> %d", off0, (int) ((guintptr) paintinfo.yp));
        GST_INFO ("off1 %d <> %d", off1, (int) ((guintptr) paintinfo.up));
        GST_INFO ("off2 %d <> %d", off2, (int) ((guintptr) paintinfo.vp));

        fail_unless_equals_int (size, (unsigned long) paintinfo.endptr);
        fail_unless_equals_int (off0, (unsigned long) paintinfo.yp);
        fail_unless_equals_int (off1, (unsigned long) paintinfo.up);
        fail_unless_equals_int (off2, (unsigned long) paintinfo.vp);

        /* should be 0 if there's no alpha component */
        off3 = GST_VIDEO_INFO_COMP_OFFSET (&vinfo, 3);
        fail_unless_equals_int (off3, (unsigned long) paintinfo.ap);

        cs0 = GST_VIDEO_INFO_COMP_WIDTH (&vinfo, 0) *
            GST_VIDEO_INFO_COMP_HEIGHT (&vinfo, 0);
        cs1 = GST_VIDEO_INFO_COMP_WIDTH (&vinfo, 1) *
            GST_VIDEO_INFO_COMP_HEIGHT (&vinfo, 1);
        cs2 = GST_VIDEO_INFO_COMP_WIDTH (&vinfo, 2) *
            GST_VIDEO_INFO_COMP_HEIGHT (&vinfo, 2);

        /* GST_LOG ("cs0=%d,cs1=%d,cs2=%d,off0=%d,off1=%d,off2=%d,size=%d",
           cs0, cs1, cs2, off0, off1, off2, size); */

        if (!gst_video_format_is_packed (fmt))
          fail_unless (cs0 <= off1);

        if (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (vinfo.finfo)) {
          cs3 = GST_VIDEO_INFO_COMP_WIDTH (&vinfo, 3) *
              GST_VIDEO_INFO_COMP_HEIGHT (&vinfo, 2);
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

GST_END_TEST;

GST_START_TEST (test_video_formats_rgb)
{
  GstVideoInfo vinfo;
  gint width, height, framerate_n, framerate_d, par_n, par_d;
  GstCaps *caps;
  GstStructure *structure;

  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_RGB, 800, 600);
  vinfo.par_n = 1;
  vinfo.par_d = 1;
  vinfo.fps_n = 0;
  vinfo.fps_d = 1;
  caps = gst_video_info_to_caps (&vinfo);
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


GST_START_TEST (test_video_formats_rgba_large_dimension)
{
  GstVideoInfo vinfo;
  gint width, height, framerate_n, framerate_d, par_n, par_d;
  GstCaps *caps;
  GstStructure *structure;

  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_RGBA, 29700, 21000);
  vinfo.par_n = 1;
  vinfo.par_d = 1;
  vinfo.fps_n = 0;
  vinfo.fps_d = 1;
  caps = gst_video_info_to_caps (&vinfo);
  structure = gst_caps_get_structure (caps, 0);

  fail_unless (gst_structure_get_int (structure, "width", &width));
  fail_unless (gst_structure_get_int (structure, "height", &height));
  fail_unless (gst_structure_get_fraction (structure, "framerate", &framerate_n,
          &framerate_d));
  fail_unless (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &par_n, &par_d));

  fail_unless (width == 29700);
  fail_unless (height == 21000);
  fail_unless (framerate_n == 0);
  fail_unless (framerate_d == 1);
  fail_unless (par_n == 1);
  fail_unless (par_d == 1);
  fail_unless (vinfo.size == (gsize) 29700 * 21000 * 4);

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
    GST_VIDEO_CAPS_MAKE ("RGB"), GST_VIDEO_FORMAT_RGB}, {
    GST_VIDEO_CAPS_MAKE ("BGR"), GST_VIDEO_FORMAT_BGR},
        /* 32 bit (no alpha) */
    {
    GST_VIDEO_CAPS_MAKE ("RGBx"), GST_VIDEO_FORMAT_RGBx}, {
    GST_VIDEO_CAPS_MAKE ("xRGB"), GST_VIDEO_FORMAT_xRGB}, {
    GST_VIDEO_CAPS_MAKE ("BGRx"), GST_VIDEO_FORMAT_BGRx}, {
    GST_VIDEO_CAPS_MAKE ("xBGR"), GST_VIDEO_FORMAT_xBGR},
        /* 32 bit (with alpha) */
    {
    GST_VIDEO_CAPS_MAKE ("RGBA"), GST_VIDEO_FORMAT_RGBA}, {
    GST_VIDEO_CAPS_MAKE ("ARGB"), GST_VIDEO_FORMAT_ARGB}, {
    GST_VIDEO_CAPS_MAKE ("BGRA"), GST_VIDEO_FORMAT_BGRA}, {
    GST_VIDEO_CAPS_MAKE ("ABGR"), GST_VIDEO_FORMAT_ABGR},
        /* 16 bit */
    {
    GST_VIDEO_CAPS_MAKE ("RGB16"), GST_VIDEO_FORMAT_RGB16}, {
    GST_VIDEO_CAPS_MAKE ("BGR16"), GST_VIDEO_FORMAT_BGR16}, {
    GST_VIDEO_CAPS_MAKE ("RGB15"), GST_VIDEO_FORMAT_RGB15}, {
    GST_VIDEO_CAPS_MAKE ("BGR15"), GST_VIDEO_FORMAT_BGR15}
  };
  gint i;

  for (i = 0; i < G_N_ELEMENTS (formats); ++i) {
    GstVideoInfo vinfo;
    GstCaps *caps, *caps2;

    caps = gst_caps_from_string (formats[i].tmpl_caps_string);
    gst_caps_set_simple (caps, "width", G_TYPE_INT, 2 * (i + 1), "height",
        G_TYPE_INT, i + 1, "framerate", GST_TYPE_FRACTION, 15, 1,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
        "interlace-mode", G_TYPE_STRING, "progressive",
        "colorimetry", G_TYPE_STRING, "1:1:0:0", NULL);
    g_assert (gst_caps_is_fixed (caps));

    GST_DEBUG ("testing caps: %" GST_PTR_FORMAT, caps);

    gst_video_info_init (&vinfo);
    fail_unless (gst_video_info_from_caps (&vinfo, caps));
    fail_unless_equals_int (GST_VIDEO_INFO_FORMAT (&vinfo), formats[i].fmt);
    fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (&vinfo), 2 * (i + 1));
    fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (&vinfo), i + 1);

    /* make sure they're serialised back correctly */
    caps2 = gst_video_info_to_caps (&vinfo);
    fail_unless (caps != NULL);
    fail_unless (gst_caps_is_equal (caps, caps2),
        "caps [%" GST_PTR_FORMAT "] not equal to caps2 [%" GST_PTR_FORMAT "]",
        caps, caps2);

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
  GstVideoInfo vinfo;
  GstCaps *from_caps, *to_caps;
  GstBuffer *from_buffer;
  GstSample *from_sample, *to_sample;
  GError *error = NULL;
  gint i;
  GstMapInfo map;

  gst_debug_set_threshold_for_name ("default", GST_LEVEL_NONE);

  from_buffer = gst_buffer_new_and_alloc (640 * 480 * 4);

  gst_buffer_map (from_buffer, &map, GST_MAP_WRITE);
  for (i = 0; i < 640 * 480; i++) {
    map.data[4 * i + 0] = 0;    /* x */
    map.data[4 * i + 1] = 255;  /* R */
    map.data[4 * i + 2] = 0;    /* G */
    map.data[4 * i + 3] = 0;    /* B */
  }
  gst_buffer_unmap (from_buffer, &map);

  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_xRGB, 640, 480);
  vinfo.fps_n = 25;
  vinfo.fps_d = 1;
  vinfo.par_n = 1;
  vinfo.par_d = 1;
  from_caps = gst_video_info_to_caps (&vinfo);

  from_sample = gst_sample_new (from_buffer, from_caps, NULL, NULL);

  to_caps =
      gst_caps_from_string
      ("something/that, does=(string)not, exist=(boolean)FALSE");

  to_sample =
      gst_video_convert_sample (from_sample, to_caps,
      GST_CLOCK_TIME_NONE, &error);
  fail_if (to_sample != NULL);
  fail_unless (error != NULL);
  g_error_free (error);
  error = NULL;

  gst_caps_unref (to_caps);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_I420, 240, 320);
  vinfo.fps_n = 25;
  vinfo.fps_d = 1;
  vinfo.par_n = 1;
  vinfo.par_d = 2;
  to_caps = gst_video_info_to_caps (&vinfo);

  to_sample =
      gst_video_convert_sample (from_sample, to_caps,
      GST_CLOCK_TIME_NONE, &error);
  fail_unless (to_sample != NULL);
  fail_unless (error == NULL);

  gst_buffer_unref (from_buffer);
  gst_caps_unref (from_caps);
  gst_sample_unref (from_sample);
  gst_sample_unref (to_sample);
  gst_caps_unref (to_caps);
}

GST_END_TEST;

typedef struct
{
  GMainLoop *loop;
  GstSample *sample;
  GError *error;
} ConvertFrameContext;

static void
convert_sample_async_callback (GstSample * sample, GError * err,
    ConvertFrameContext * cf_data)
{
  cf_data->sample = sample;
  cf_data->error = err;

  g_main_loop_quit (cf_data->loop);
}

GST_START_TEST (test_convert_frame_async)
{
  GstVideoInfo vinfo;
  GstCaps *from_caps, *to_caps;
  GstBuffer *from_buffer;
  GstSample *from_sample;
  gint i;
  GstMapInfo map;
  GMainLoop *loop;
  ConvertFrameContext cf_data = { NULL, NULL, NULL };

  gst_debug_set_threshold_for_name ("default", GST_LEVEL_NONE);

  from_buffer = gst_buffer_new_and_alloc (640 * 480 * 4);

  gst_buffer_map (from_buffer, &map, GST_MAP_WRITE);
  for (i = 0; i < 640 * 480; i++) {
    map.data[4 * i + 0] = 0;    /* x */
    map.data[4 * i + 1] = 255;  /* R */
    map.data[4 * i + 2] = 0;    /* G */
    map.data[4 * i + 3] = 0;    /* B */
  }
  gst_buffer_unmap (from_buffer, &map);

  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_xRGB, 640, 470);
  vinfo.par_n = 1;
  vinfo.par_d = 1;
  vinfo.fps_n = 25;
  vinfo.fps_d = 1;
  from_caps = gst_video_info_to_caps (&vinfo);

  to_caps =
      gst_caps_from_string
      ("something/that, does=(string)not, exist=(boolean)FALSE");

  loop = cf_data.loop = g_main_loop_new (NULL, FALSE);

  from_sample = gst_sample_new (from_buffer, from_caps, NULL, NULL);
  gst_buffer_unref (from_buffer);
  gst_caps_unref (from_caps);

  gst_video_convert_sample_async (from_sample, to_caps,
      GST_CLOCK_TIME_NONE,
      (GstVideoConvertSampleCallback) convert_sample_async_callback, &cf_data,
      NULL);

  g_main_loop_run (loop);

  fail_if (cf_data.sample != NULL);
  fail_unless (cf_data.error != NULL);
  g_error_free (cf_data.error);
  cf_data.error = NULL;

  gst_caps_unref (to_caps);
  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_I420, 240, 320);
  vinfo.par_n = 1;
  vinfo.par_d = 2;
  vinfo.fps_n = 25;
  vinfo.fps_d = 1;
  to_caps = gst_video_info_to_caps (&vinfo);
  gst_video_convert_sample_async (from_sample, to_caps,
      GST_CLOCK_TIME_NONE,
      (GstVideoConvertSampleCallback) convert_sample_async_callback, &cf_data,
      NULL);
  g_main_loop_run (loop);
  fail_unless (cf_data.sample != NULL);
  fail_unless (cf_data.error == NULL);

  gst_sample_unref (cf_data.sample);
  gst_caps_unref (to_caps);
  gst_sample_unref (from_sample);

  g_main_loop_unref (loop);
}

GST_END_TEST;

GST_START_TEST (test_video_size_from_caps)
{
  GstVideoInfo vinfo;
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "YV12",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 480, "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

  gst_video_info_init (&vinfo);
  fail_unless (gst_video_info_from_caps (&vinfo, caps));
  fail_unless (GST_VIDEO_INFO_SIZE (&vinfo) == (640 * 480 * 12 / 8));

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_overlay_composition)
{
  GstVideoOverlayComposition *comp1, *comp2;
  GstVideoOverlayRectangle *rect1, *rect2;
  GstVideoOverlayCompositionMeta *ometa;
  GstBuffer *pix1, *pix2, *buf;
  GstVideoMeta *vmeta;
  guint seq1, seq2;
  guint w, h, stride;
  gint x, y;
  guint8 val;

  pix1 = gst_buffer_new_and_alloc (200 * sizeof (guint32) * 50);
  gst_buffer_memset (pix1, 0, 0, gst_buffer_get_size (pix1));

  gst_buffer_add_video_meta (pix1, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, 200, 50);
  rect1 = gst_video_overlay_rectangle_new_raw (pix1,
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
  pix1 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  stride = 4 * w;
  fail_unless (gst_buffer_get_size (pix1) > ((h - 1) * stride + (w * 4) - 1),
      "size %u vs. last pixel offset %u", gst_buffer_get_size (pix1),
      ((h - 1) * stride + (w * 4) - 1));
  gst_buffer_extract (pix1, ((h - 1) * stride + (w * 4) - 1), &val, 1);
  fail_unless_equals_int (val, 0);

  gst_video_overlay_rectangle_get_render_rectangle (rect2, &x, &y, &w, &h);
  fail_unless_equals_int (x, 50);
  fail_unless_equals_int (y, 600);
  fail_unless_equals_int (w, 300);
  fail_unless_equals_int (h, 50);

  /* get scaled pixbuf and touch last byte */
  pix2 = gst_video_overlay_rectangle_get_pixels_raw (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  stride = 4 * w;
  fail_unless (gst_buffer_get_size (pix2) > ((h - 1) * stride + (w * 4) - 1),
      "size %u vs. last pixel offset %u", gst_buffer_get_size (pix1),
      ((h - 1) * stride + (w * 4) - 1));
  gst_buffer_extract (pix2, ((h - 1) * stride + (w * 4) - 1), &val, 1);
  fail_unless_equals_int (val, 0);

  /* get scaled pixbuf again, should be the same buffer as before (caching) */
  pix1 = gst_video_overlay_rectangle_get_pixels_raw (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 == pix2);

  /* get in different format */
  pix1 = gst_video_overlay_rectangle_get_pixels_ayuv (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 != pix2);
  /* get it again, should be same (caching) */
  pix2 = gst_video_overlay_rectangle_get_pixels_ayuv (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 == pix2);
  /* get unscaled, should be different */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_ayuv (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 != pix2);
  /* but should be cached */
  pix1 = gst_video_overlay_rectangle_get_pixels_unscaled_ayuv (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 == pix2);

  vmeta = gst_buffer_get_video_meta (pix1);
  fail_unless (vmeta != NULL);
  w = vmeta->width;
  h = vmeta->height;
  fail_unless_equals_int (w, 200);
  fail_unless_equals_int (h, 50);
  fail_unless_equals_int (vmeta->format,
      GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_YUV);
  fail_unless (gst_buffer_get_size (pix1) == w * h * 4);
  gst_buffer_extract (pix1, 0, &seq1, 4);
  fail_unless (seq1 != 0);

  /* now compare the original unscaled ones */
  pix1 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect2,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

  vmeta = gst_buffer_get_video_meta (pix2);
  fail_unless (vmeta != NULL);
  w = vmeta->width;
  h = vmeta->height;

  /* the original pixel buffers should be identical */
  fail_unless (pix1 == pix2);
  fail_unless_equals_int (w, 200);
  fail_unless_equals_int (h, 50);
  stride = 4 * w;

  /* touch last byte */
  fail_unless (gst_buffer_get_size (pix1) > ((h - 1) * stride + (w * 4) - 1),
      "size %u vs. last pixel offset %u", gst_buffer_get_size (pix1),
      ((h - 1) * stride + (w * 4) - 1));
  gst_buffer_extract (pix1, ((h - 1) * stride + (w * 4) - 1), &val, 1);
  fail_unless_equals_int (val, 0);

  /* test attaching and retrieving of compositions to/from buffers */
  buf = gst_buffer_new ();
  fail_unless (gst_buffer_get_video_overlay_composition_meta (buf) == NULL);

  gst_buffer_ref (buf);
  /* buffer now has refcount of 2, so its metadata is not writable.
   * only check this if we are not running in valgrind, as it leaks */
#ifdef HAVE_VALGRIND
  if (!RUNNING_ON_VALGRIND) {
    ASSERT_CRITICAL (gst_buffer_add_video_overlay_composition_meta (buf,
            comp1));
  }
#endif
  gst_buffer_unref (buf);
  gst_buffer_add_video_overlay_composition_meta (buf, comp1);
  ometa = gst_buffer_get_video_overlay_composition_meta (buf);
  fail_unless (ometa != NULL);
  fail_unless (ometa->overlay == comp1);
  fail_unless (gst_buffer_remove_video_overlay_composition_meta (buf, ometa));
  gst_buffer_add_video_overlay_composition_meta (buf, comp2);
  ometa = gst_buffer_get_video_overlay_composition_meta (buf);
  fail_unless (ometa->overlay == comp2);
  fail_unless (gst_buffer_remove_video_overlay_composition_meta (buf, ometa));
  fail_unless (gst_buffer_get_video_overlay_composition_meta (buf) == NULL);

  /* make sure the buffer cleans up its composition ref when unreffed */
  gst_buffer_add_video_overlay_composition_meta (buf, comp2);
  gst_buffer_unref (buf);

  gst_video_overlay_composition_unref (comp2);
  gst_video_overlay_composition_unref (comp1);
}

GST_END_TEST;

GST_START_TEST (test_overlay_composition_premultiplied_alpha)
{
  GstVideoOverlayRectangle *rect1;
  GstVideoMeta *vmeta;
  GstBuffer *pix1, *pix2, *pix3, *pix4, *pix5;
  GstBuffer *pix6, *pix7, *pix8, *pix9, *pix10;
  guint8 *data5, *data7;
  guint w, h, w2, h2;
  GstMapInfo map;

  pix1 = gst_buffer_new_and_alloc (200 * sizeof (guint32) * 50);
  gst_buffer_memset (pix1, 0, 0x80, gst_buffer_get_size (pix1));

  gst_buffer_add_video_meta (pix1, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, 200, 50);
  rect1 = gst_video_overlay_rectangle_new_raw (pix1,
      600, 50, 300, 50, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  gst_buffer_unref (pix1);

  /* same flags, unscaled, should be the same buffer */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 == pix2);

  /* same flags, but scaled */
  pix3 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_if (pix3 == pix1 || pix3 == pix2);

  /* same again, should hopefully get the same (cached) buffer as before */
  pix4 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix4 == pix3);

  /* just to update the vars */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

  vmeta = gst_buffer_get_video_meta (pix2);
  fail_unless (vmeta != NULL);
  w = vmeta->width;
  h = vmeta->height;

  /* now, let's try to get premultiplied alpha from the unpremultiplied input */
  pix5 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_if (pix5 == pix1 || pix5 == pix2 || pix5 == pix3);
  vmeta = gst_buffer_get_video_meta (pix5);
  fail_unless (vmeta != NULL);
  w2 = vmeta->width;
  h2 = vmeta->height;
  fail_unless_equals_int (w, w2);
  fail_unless_equals_int (h, h2);
  fail_unless_equals_int (gst_buffer_get_size (pix2),
      gst_buffer_get_size (pix5));
  gst_buffer_map (pix5, &map, GST_MAP_READ);
  fail_if (gst_buffer_memcmp (pix2, 0, map.data, map.size) == 0);
  /* make sure it actually did what we expected it to do (input=0x80808080) */
  data5 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data5[0], 0x40);
  fail_unless_equals_int (data5[1], 0x40);
  fail_unless_equals_int (data5[2], 0x40);
  fail_unless_equals_int (data5[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data5[0], 0x80);
  fail_unless_equals_int (data5[1], 0x40);
  fail_unless_equals_int (data5[2], 0x40);
  fail_unless_equals_int (data5[3], 0x40);
#endif
  gst_buffer_unmap (pix5, &map);

  /* same again, now we should be getting back the same buffer as before,
   * as it should have been cached */
  pix6 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_unless (pix6 == pix5);

  /* just to update the stride var */
  pix3 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix3 == pix4);

  /* now try to get scaled premultiplied alpha from unpremultiplied input */
  pix7 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_if (pix7 == pix1 || pix7 == pix2 || pix7 == pix3 || pix7 == pix5);

  gst_buffer_map (pix7, &map, GST_MAP_READ);
  data7 = map.data;
  /* make sure it actually did what we expected it to do (input=0x80808080)
   * hoping that the scaling didn't mess up our values */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data7[0], 0x40);
  fail_unless_equals_int (data7[1], 0x40);
  fail_unless_equals_int (data7[2], 0x40);
  fail_unless_equals_int (data7[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data7[0], 0x80);
  fail_unless_equals_int (data7[1], 0x40);
  fail_unless_equals_int (data7[2], 0x40);
  fail_unless_equals_int (data7[3], 0x40);
#endif
  gst_buffer_unmap (pix7, &map);

  /* and the same again, it should be cached now */
  pix8 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_unless (pix8 == pix7);

  /* make sure other cached stuff is still there */
  pix9 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix9 == pix3);
  pix10 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_unless (pix10 == pix5);

  gst_video_overlay_rectangle_unref (rect1);
}

GST_END_TEST;

GST_START_TEST (test_overlay_composition_global_alpha)
{
  GstVideoOverlayRectangle *rect1;
  GstBuffer *pix1, *pix2, *pix3, *pix4, *pix5;
  GstVideoMeta *vmeta;
  guint8 *data2, *data4, *data5;
  guint w, h, w4, h4;
  guint seq1, seq2;
  gfloat ga1, ga2;
  GstVideoOverlayFormatFlags flags1;
  GstMapInfo map;

  pix1 = gst_buffer_new_and_alloc (200 * sizeof (guint32) * 50);
  gst_buffer_memset (pix1, 0, 0x80, gst_buffer_get_size (pix1));

  gst_buffer_add_video_meta (pix1, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, 200, 50);
  rect1 = gst_video_overlay_rectangle_new_raw (pix1,
      600, 50, 300, 50, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  gst_buffer_unref (pix1);

  /* same flags, unscaled, should be the same buffer */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix1 == pix2);

  vmeta = gst_buffer_get_video_meta (pix2);
  fail_unless (vmeta != NULL);
  w = vmeta->width;
  h = vmeta->height;

  /* same flags, but scaled */
  pix3 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_if (pix3 == pix1 || pix3 == pix2);

  /* get unscaled premultiplied data, new cached rectangle should be created */
  pix4 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_if (pix4 == pix2 || pix4 == pix3);
  vmeta = gst_buffer_get_video_meta (pix4);
  fail_unless (vmeta != NULL);
  w4 = vmeta->width;
  h4 = vmeta->height;
  fail_unless_equals_int (w, w4);
  fail_unless_equals_int (h, h4);
  fail_unless_equals_int (gst_buffer_get_size (pix2),
      gst_buffer_get_size (pix4));
  gst_buffer_map (pix4, &map, GST_MAP_READ);
  fail_if (gst_buffer_memcmp (pix1, 0, map.data, map.size) == 0);
  /* make sure it actually did what we expected it to do (input=0x80808080) */
  data4 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data4[0], 0x40);
  fail_unless_equals_int (data4[1], 0x40);
  fail_unless_equals_int (data4[2], 0x40);
  fail_unless_equals_int (data4[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data4[0], 0x80);
  fail_unless_equals_int (data4[1], 0x40);
  fail_unless_equals_int (data4[2], 0x40);
  fail_unless_equals_int (data4[3], 0x40);
#endif
  gst_buffer_unmap (pix4, &map);

  /* now premultiplied and scaled, again a new cached rectangle should be cached */
  pix5 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  fail_if (pix5 == pix2 || pix5 == pix3 || pix5 == pix4);
  /* stride and size should be equal to the first scaled rect */
  fail_unless_equals_int (gst_buffer_get_size (pix5),
      gst_buffer_get_size (pix3));
  /* data should be different (premutliplied) though */
  gst_buffer_map (pix5, &map, GST_MAP_READ);
  fail_if (gst_buffer_memcmp (pix3, 0, map.data, map.size) == 0);
  /* make sure it actually did what we expected it to do (input=0x80808080) */
  data5 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data5[0], 0x40);
  fail_unless_equals_int (data5[1], 0x40);
  fail_unless_equals_int (data5[2], 0x40);
  fail_unless_equals_int (data5[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data5[0], 0x80);
  fail_unless_equals_int (data5[1], 0x40);
  fail_unless_equals_int (data5[2], 0x40);
  fail_unless_equals_int (data5[3], 0x40);
#endif
  gst_buffer_unmap (pix5, &map);

  /* global_alpha should initially be 1.0 */
  ga1 = gst_video_overlay_rectangle_get_global_alpha (rect1);
  fail_unless_equals_float (ga1, 1.0);

  /* now set global_alpha */
  seq1 = gst_video_overlay_rectangle_get_seqnum (rect1);
  gst_video_overlay_rectangle_set_global_alpha (rect1, 0.5);
  ga2 = gst_video_overlay_rectangle_get_global_alpha (rect1);
  fail_unless_equals_float (ga2, 0.5);

  /* seqnum should have changed */
  seq2 = gst_video_overlay_rectangle_get_seqnum (rect1);
  fail_unless (seq1 < seq2);

  /* internal flags should have been set */
  flags1 = gst_video_overlay_rectangle_get_flags (rect1);
  fail_unless_equals_int (flags1, GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);

  /* request unscaled pixel-data, global-alpha not applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
  /* this should just return the same buffer */
  fail_unless (pix2 == pix1);
  /* make sure we got the initial data (input=0x80808080) */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#endif
  gst_buffer_unmap (pix2, &map);

  /* unscaled pixel-data, global-alpha applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  /* this should be the same buffer with on-the-fly modified alpha-channel */
  fail_unless (pix2 == pix1);
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
  /* make sure we got the initial data with adjusted alpha-channel */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x40);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x40);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#endif
  gst_buffer_unmap (pix2, &map);

  /* adjust global_alpha once more */
  gst_video_overlay_rectangle_set_global_alpha (rect1, 0.25);
  ga2 = gst_video_overlay_rectangle_get_global_alpha (rect1);
  fail_unless_equals_float (ga2, 0.25);
  /* and again request unscaled pixel-data, global-alpha applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  fail_unless (pix2 == pix1);
  /* make sure we got the initial data with adjusted alpha-channel */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x20);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x20);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#endif
  gst_buffer_unmap (pix2, &map);

  /* again: unscaled pixel-data, global-alpha not applied,
   * this should revert alpha-channel to initial values */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
  fail_unless (pix2 == pix1);
  /* make sure we got the initial data (input=0x80808080) */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#endif
  gst_buffer_unmap (pix2, &map);

  /* now scaled, global-alpha not applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
  /* this should just return the rect/buffer, that was cached for these
   * scaling dimensions */
  fail_unless (pix2 == pix3);
  /* make sure we got the initial data (input=0x80808080) */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#endif
  gst_buffer_unmap (pix2, &map);

  /* scaled, global-alpha (0.25) applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
  /* this should just return the rect/buffer, that was cached for these
   * scaling dimensions with modified alpha channel */
  fail_unless (pix2 == pix3);
  /* make sure we got the data we expect for global-alpha=0.25 */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x20);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x20);
  fail_unless_equals_int (data2[1], 0x80);
  fail_unless_equals_int (data2[2], 0x80);
  fail_unless_equals_int (data2[3], 0x80);
#endif
  gst_buffer_unmap (pix2, &map);

  /* now unscaled premultiplied data, global-alpha not applied,
   * is this really a valid use case?*/
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA |
      GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
  /* this should just return the rect/buffer, that was cached for the
   * premultiplied data */
  fail_unless (pix2 == pix4);
  /* make sure we got what we expected */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x40);
  fail_unless_equals_int (data2[1], 0x40);
  fail_unless_equals_int (data2[2], 0x40);
  fail_unless_equals_int (data2[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x40);
  fail_unless_equals_int (data2[2], 0x40);
  fail_unless_equals_int (data2[3], 0x40);
#endif
  gst_buffer_unmap (pix2, &map);

  /* unscaled premultiplied data, global-alpha (0.25) applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  /* this should just return the rect/buffer, that was cached for the
   * premultiplied data */
  fail_unless (pix2 == pix4);
  /* make sure we got what we expected:
   * (0x40 / (0x80/0xFF) * (0x20/0xFF) = 0x10
   * NOTE: unless we are using round() for the premultiplied case
   * in gst_video_overlay_rectangle_apply_global_alpha() we get rounding
   * error, i.e. 0x0F here */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x0F);
  fail_unless_equals_int (data2[1], 0x0F);
  fail_unless_equals_int (data2[2], 0x0F);
  fail_unless_equals_int (data2[3], 0x20);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x20);
  fail_unless_equals_int (data2[1], 0x0F);
  fail_unless_equals_int (data2[2], 0x0F);
  fail_unless_equals_int (data2[3], 0x0F);
#endif
  gst_buffer_unmap (pix2, &map);

  /* set global_alpha once more */
  gst_video_overlay_rectangle_set_global_alpha (rect1, 0.75);
  /* and verify that also premultiplied data is adjusted
   * correspondingly (though with increasing rounding errors) */
  pix2 = gst_video_overlay_rectangle_get_pixels_unscaled_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  /* this should just return the rect/buffer, that was cached for the
   * premultiplied data */
  fail_unless (pix2 == pix4);
  /* make sure we got what we expected:
   * (0x0F / (0x20/0xFF) * (0x60/0xFF) = 0x2D
   * NOTE: using floats everywhere we would get 0x30
   * here we will actually end up with 0x2C */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x2C);
  fail_unless_equals_int (data2[1], 0x2C);
  fail_unless_equals_int (data2[2], 0x2C);
  fail_unless_equals_int (data2[3], 0x60);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x60);
  fail_unless_equals_int (data2[1], 0x2C);
  fail_unless_equals_int (data2[2], 0x2C);
  fail_unless_equals_int (data2[3], 0x2C);
#endif
  gst_buffer_unmap (pix2, &map);

  /* now scaled and premultiplied data, global-alpha not applied,
   * is this really a valid use case?*/
  pix2 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA |
      GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
  /* this should just return the rect/buffer, that was cached for the
   * first premultiplied+scaled rect*/
  fail_unless (pix2 == pix5);
  /* make sure we got what we expected */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x40);
  fail_unless_equals_int (data2[1], 0x40);
  fail_unless_equals_int (data2[2], 0x40);
  fail_unless_equals_int (data2[3], 0x80);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x80);
  fail_unless_equals_int (data2[1], 0x40);
  fail_unless_equals_int (data2[2], 0x40);
  fail_unless_equals_int (data2[3], 0x40);
#endif
  gst_buffer_unmap (pix2, &map);

  /* scaled and premultiplied data, global-alpha applied */
  pix2 = gst_video_overlay_rectangle_get_pixels_raw (rect1,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
  /* this should just return the rect/buffer, that was cached for the
   * first premultiplied+scaled rect*/
  fail_unless (pix2 == pix5);
  /* make sure we got what we expected; see above note about rounding errors! */
  gst_buffer_map (pix2, &map, GST_MAP_READ);
  data2 = map.data;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  /* B - G - R - A */
  fail_unless_equals_int (data2[0], 0x2F);
  fail_unless_equals_int (data2[1], 0x2F);
  fail_unless_equals_int (data2[2], 0x2F);
  fail_unless_equals_int (data2[3], 0x60);
#else
  /* A - R - G - B */
  fail_unless_equals_int (data2[0], 0x60);
  fail_unless_equals_int (data2[1], 0x2F);
  fail_unless_equals_int (data2[2], 0x2F);
  fail_unless_equals_int (data2[3], 0x2F);
#endif
  gst_buffer_unmap (pix2, &map);

  gst_video_overlay_rectangle_unref (rect1);
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
  tcase_add_test (tc_chain, test_video_formats_rgba_large_dimension);
  tcase_add_test (tc_chain, test_video_formats_all);
  tcase_add_test (tc_chain, test_video_formats_pack_unpack);
  tcase_add_test (tc_chain, test_dar_calc);
  tcase_add_test (tc_chain, test_parse_caps_rgb);
  tcase_add_test (tc_chain, test_events);
  tcase_add_test (tc_chain, test_convert_frame);
  tcase_add_test (tc_chain, test_convert_frame_async);
  tcase_add_test (tc_chain, test_video_size_from_caps);
  tcase_add_test (tc_chain, test_overlay_composition);
  tcase_add_test (tc_chain, test_overlay_composition_premultiplied_alpha);
  tcase_add_test (tc_chain, test_overlay_composition_global_alpha);

  return s;
}

GST_CHECK_MAIN (video);
