/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/* non-GST-specific stuff */

#include "gstvideotestsrc.h"
#include "videotestsrc.h"
#include <liboil/liboil.h>


#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

static unsigned char
random_char (void)
{
  static unsigned int state;

  state *= 1103515245;
  state += 12345;
  return (state >> 16) & 0xff;
}

#if 0
static void
random_chars (unsigned char *dest, int nbytes)
{
  int i;
  static unsigned int state;

  for (i = 0; i < nbytes; i++) {
    state *= 1103515245;
    state += 12345;
    dest[i] = (state >> 16);
  }
}
#endif

#if 0
static void
paint_rect_random (unsigned char *dest, int stride, int x, int y, int w, int h)
{
  unsigned char *d = dest + stride * y + x;
  int i;

  for (i = 0; i < h; i++) {
    random_chars (d, w);
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect (unsigned char *dest, int stride, int x, int y, int w, int h,
    unsigned char color)
{
  unsigned char *d = dest + stride * y + x;
  int i;

  for (i = 0; i < h; i++) {
    oil_splat_u8_ns (d, &color, w);
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect_s2 (unsigned char *dest, int stride, int x, int y, int w, int h,
    unsigned char col)
{
  unsigned char *d = dest + stride * y + x * 2;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp = col;
      dp += 2;
    }
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect2 (unsigned char *dest, int stride, int x, int y, int w, int h,
    unsigned char *col)
{
  unsigned char *d = dest + stride * y + x * 2;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp++ = col[0];
      *dp++ = col[1];
    }
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect3 (unsigned char *dest, int stride, int x, int y, int w, int h,
    unsigned char *col)
{
  unsigned char *d = dest + stride * y + x * 3;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp++ = col[0];
      *dp++ = col[1];
      *dp++ = col[2];
    }
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect4 (unsigned char *dest, int stride, int x, int y, int w, int h,
    unsigned char *col)
{
  unsigned char *d = dest + stride * y + x * 4;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp++ = col[0];
      *dp++ = col[1];
      *dp++ = col[2];
      *dp++ = col[3];
    }
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect_s4 (unsigned char *dest, int stride, int x, int y, int w, int h,
    unsigned char col)
{
  unsigned char *d = dest + stride * y + x * 4;
  unsigned char *dp;
  int i, j;

  for (i = 0; i < h; i++) {
    dp = d;
    for (j = 0; j < w; j++) {
      *dp = col;
      dp += 4;
    }
    d += stride;
  }
}
#endif

enum
{
  COLOR_WHITE = 0,
  COLOR_YELLOW,
  COLOR_CYAN,
  COLOR_GREEN,
  COLOR_MAGENTA,
  COLOR_RED,
  COLOR_BLUE,
  COLOR_BLACK,
  COLOR_NEG_I,
  COLOR_POS_Q,
  COLOR_SUPER_BLACK,
  COLOR_DARK_GREY
};

static const struct vts_color_struct vts_colors[] = {
  /* 100% white */
  {255, 128, 128, 255, 255, 255, 255},
  /* yellow */
  {226, 0, 155, 255, 255, 0, 255},
  /* cyan */
  {179, 170, 0, 0, 255, 255, 255},
  /* green */
  {150, 46, 21, 0, 255, 0, 255},
  /* magenta */
  {105, 212, 235, 255, 0, 255, 255},
  /* red */
  {76, 85, 255, 255, 0, 0, 255},
  /* blue */
  {29, 255, 107, 0, 0, 255, 255},
  /* black */
  {16, 128, 128, 0, 0, 0, 255},
  /* -I */
  {16, 198, 21, 0, 0, 128, 255},
  /* +Q */
  {16, 235, 198, 0, 128, 255, 255},
  /* superblack */
  {0, 128, 128, 0, 0, 0, 255},
  /* 5% grey */
  {32, 128, 128, 32, 32, 32, 255},
};


#if 0

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q, superblack, dark grey */
static int y_colors[] = { 255, 226, 179, 150, 105, 76, 29, 16, 16, 16, 0, 32 };
static int u_colors[] =
    { 128, 0, 170, 46, 212, 85, 255, 128, 198, 235, 128, 128 };
static int v_colors[] =
    { 128, 155, 0, 21, 235, 255, 107, 128, 21, 198, 128, 128 };

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q  superblack, dark grey */
static int r_colors[] = { 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 32 };
static int g_colors[] = { 255, 255, 255, 255, 0, 0, 0, 0, 0, 128, 0, 32 };
static int b_colors[] = { 255, 0, 255, 0, 255, 0, 255, 0, 128, 255, 0, 32 };
#endif


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
static void paint_setup_NV12 (paintinfo * p, unsigned char *dest);
static void paint_setup_NV21 (paintinfo * p, unsigned char *dest);

#if 0
static void paint_setup_IMC1 (paintinfo * p, unsigned char *dest);
static void paint_setup_IMC2 (paintinfo * p, unsigned char *dest);
static void paint_setup_IMC3 (paintinfo * p, unsigned char *dest);
static void paint_setup_IMC4 (paintinfo * p, unsigned char *dest);
#endif
static void paint_setup_YUV9 (paintinfo * p, unsigned char *dest);
static void paint_setup_YVU9 (paintinfo * p, unsigned char *dest);
static void paint_setup_ARGB8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_ABGR8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_RGBA8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_BGRA8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_xRGB8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_xBGR8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_RGBx8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_BGRx8888 (paintinfo * p, unsigned char *dest);
static void paint_setup_RGB888 (paintinfo * p, unsigned char *dest);
static void paint_setup_BGR888 (paintinfo * p, unsigned char *dest);
static void paint_setup_RGB565 (paintinfo * p, unsigned char *dest);
static void paint_setup_xRGB1555 (paintinfo * p, unsigned char *dest);

static void paint_setup_bayer (paintinfo * p, unsigned char *dest);

static void paint_hline_I420 (paintinfo * p, int x, int y, int w);
static void paint_hline_NV12_NV21 (paintinfo * p, int x, int y, int w);
static void paint_hline_YUY2 (paintinfo * p, int x, int y, int w);
static void paint_hline_IYU2 (paintinfo * p, int x, int y, int w);
static void paint_hline_Y41B (paintinfo * p, int x, int y, int w);
static void paint_hline_Y42B (paintinfo * p, int x, int y, int w);
static void paint_hline_Y800 (paintinfo * p, int x, int y, int w);
static void paint_hline_AYUV (paintinfo * p, int x, int y, int w);

#if 0
static void paint_hline_IMC1 (paintinfo * p, int x, int y, int w);
#endif
static void paint_hline_YUV9 (paintinfo * p, int x, int y, int w);
static void paint_hline_str4 (paintinfo * p, int x, int y, int w);
static void paint_hline_str3 (paintinfo * p, int x, int y, int w);
static void paint_hline_RGB565 (paintinfo * p, int x, int y, int w);
static void paint_hline_xRGB1555 (paintinfo * p, int x, int y, int w);

static void paint_hline_bayer (paintinfo * p, int x, int y, int w);

struct fourcc_list_struct fourcc_list[] = {
/* packed */
  {VTS_YUV, "YUY2", "YUY2", 16, paint_setup_YUY2, paint_hline_YUY2},
  {VTS_YUV, "UYVY", "UYVY", 16, paint_setup_UYVY, paint_hline_YUY2},
  {VTS_YUV, "Y422", "Y422", 16, paint_setup_UYVY, paint_hline_YUY2},
  {VTS_YUV, "UYNV", "UYNV", 16, paint_setup_UYVY, paint_hline_YUY2},    /* FIXME: UYNV? */
  {VTS_YUV, "YVYU", "YVYU", 16, paint_setup_YVYU, paint_hline_YUY2},
  {VTS_YUV, "AYUV", "AYUV", 32, paint_setup_AYUV, paint_hline_AYUV},

  /* interlaced */
  /*{ VTS_YUV,  "IUYV", "IUY2", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* inverted */
  /*{ VTS_YUV,  "cyuv", "cyuv", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ VTS_YUV,  "Y41P", "Y41P", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* interlaced */
  /*{ VTS_YUV,  "IY41", "IY41", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ VTS_YUV,  "Y211", "Y211", 8, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ VTS_YUV,  "Y41T", "Y41T", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ VTS_YUV,  "Y42P", "Y42P", 16, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ VTS_YUV,  "CLJR", "CLJR", 8, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ VTS_YUV,  "IYU1", "IYU1", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  {VTS_YUV, "IYU2", "IYU2", 24, paint_setup_IYU2, paint_hline_IYU2},

/* planar */
  /* YVU9 */
  {VTS_YUV, "YVU9", "YVU9", 9, paint_setup_YVU9, paint_hline_YUV9},
  /* YUV9 */
  {VTS_YUV, "YUV9", "YUV9", 9, paint_setup_YUV9, paint_hline_YUV9},
  /* IF09 */
  /* YV12 */
  {VTS_YUV, "YV12", "YV12", 12, paint_setup_YV12, paint_hline_I420},
  /* I420 */
  {VTS_YUV, "I420", "I420", 12, paint_setup_I420, paint_hline_I420},
  /* NV12 */
  {VTS_YUV, "NV12", "NV12", 12, paint_setup_NV12, paint_hline_NV12_NV21},
  /* NV21 */
  {VTS_YUV, "NV21", "NV21", 12, paint_setup_NV21, paint_hline_NV12_NV21},
#if 0
  /* IMC1 */
  {VTS_YUV, "IMC1", "IMC1", 16, paint_setup_IMC1, paint_hline_IMC1},
  /* IMC2 */
  {VTS_YUV, "IMC2", "IMC2", 12, paint_setup_IMC2, paint_hline_IMC1},
  /* IMC3 */
  {VTS_YUV, "IMC3", "IMC3", 16, paint_setup_IMC3, paint_hline_IMC1},
  /* IMC4 */
  {VTS_YUV, "IMC4", "IMC4", 12, paint_setup_IMC4, paint_hline_IMC1},
#endif
  /* CLPL */
  /* Y41B */
  {VTS_YUV, "Y41B", "Y41B", 12, paint_setup_Y41B, paint_hline_Y41B},
  /* Y42B */
  {VTS_YUV, "Y42B", "Y42B", 16, paint_setup_Y42B, paint_hline_Y42B},
  /* Y800 grayscale */
  {VTS_YUV, "Y800", "Y800", 8, paint_setup_Y800, paint_hline_Y800},

  {VTS_RGB, "RGB ", "xRGB8888", 32, paint_setup_xRGB8888, paint_hline_str4, 24,
      0x00ff0000, 0x0000ff00, 0x000000ff},
  {VTS_RGB, "RGB ", "xBGR8888", 32, paint_setup_xBGR8888, paint_hline_str4, 24,
      0x000000ff, 0x0000ff00, 0x00ff0000},
  {VTS_RGB, "RGB ", "RGBx8888", 32, paint_setup_RGBx8888, paint_hline_str4, 24,
      0xff000000, 0x00ff0000, 0x0000ff00},
  {VTS_RGB, "RGB ", "BGRx8888", 32, paint_setup_BGRx8888, paint_hline_str4, 24,
      0x0000ff00, 0x00ff0000, 0xff000000},
  {VTS_RGB, "RGB ", "ARGB8888", 32, paint_setup_ARGB8888, paint_hline_str4, 32,
      0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000},
  {VTS_RGB, "RGB ", "ABGR8888", 32, paint_setup_ABGR8888, paint_hline_str4, 32,
      0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000},
  {VTS_RGB, "RGB ", "RGBA8888", 32, paint_setup_RGBA8888, paint_hline_str4, 32,
      0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff},
  {VTS_RGB, "RGB ", "BGRA8888", 32, paint_setup_BGRA8888, paint_hline_str4, 32,
      0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff},
  {VTS_RGB, "RGB ", "RGB888", 24, paint_setup_RGB888, paint_hline_str3, 24,
      0x00ff0000, 0x0000ff00, 0x000000ff},
  {VTS_RGB, "RGB ", "BGR888", 24, paint_setup_BGR888, paint_hline_str3, 24,
      0x000000ff, 0x0000ff00, 0x00ff0000},
  {VTS_RGB, "RGB ", "RGB565", 16, paint_setup_RGB565, paint_hline_RGB565, 16,
      0x0000f800, 0x000007e0, 0x0000001f},
  {VTS_RGB, "RGB ", "xRGB1555", 16, paint_setup_xRGB1555, paint_hline_xRGB1555,
        15,
      0x00007c00, 0x000003e0, 0x0000001f},

  {VTS_BAYER, "BAY8", "Bayer", 8, paint_setup_bayer, paint_hline_bayer}
};
int n_fourccs = G_N_ELEMENTS (fourcc_list);

struct fourcc_list_struct *
paintinfo_find_by_structure (const GstStructure * structure)
{
  int i;
  const char *media_type = gst_structure_get_name (structure);
  int ret;

  g_return_val_if_fail (structure, NULL);

  if (strcmp (media_type, "video/x-raw-yuv") == 0) {
    char *s;
    int fourcc;
    guint32 format;

    ret = gst_structure_get_fourcc (structure, "format", &format);
    if (!ret)
      return NULL;
    for (i = 0; i < n_fourccs; i++) {
      s = fourcc_list[i].fourcc;
      /* g_print("testing %" GST_FOURCC_FORMAT " and %s\n", GST_FOURCC_ARGS(format), s); */
      fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
      if (fourcc_list[i].type == VTS_YUV && fourcc == format) {
        return fourcc_list + i;
      }
    }
  } else if (strcmp (media_type, "video/x-raw-rgb") == 0) {
    int red_mask;
    int green_mask;
    int blue_mask;
    int alpha_mask;
    int depth;
    int bpp;

    ret = gst_structure_get_int (structure, "red_mask", &red_mask);
    ret &= gst_structure_get_int (structure, "green_mask", &green_mask);
    ret &= gst_structure_get_int (structure, "blue_mask", &blue_mask);
    ret &= gst_structure_get_int (structure, "depth", &depth);
    ret &= gst_structure_get_int (structure, "bpp", &bpp);

    if (depth == 32) {
      ret &= gst_structure_get_int (structure, "alpha_mask", &alpha_mask);
      ret &= (alpha_mask != 0);
    } else {
      alpha_mask = 0;
    }

    if (!ret) {
      GST_WARNING ("incomplete caps structure: %" GST_PTR_FORMAT, structure);
      return NULL;
    }

    for (i = 0; i < n_fourccs; i++) {
      if (fourcc_list[i].type == VTS_RGB &&
          fourcc_list[i].red_mask == red_mask &&
          fourcc_list[i].green_mask == green_mask &&
          fourcc_list[i].blue_mask == blue_mask &&
          (alpha_mask == 0 || fourcc_list[i].alpha_mask == alpha_mask) &&
          fourcc_list[i].depth == depth && fourcc_list[i].bitspp == bpp) {
        return fourcc_list + i;
      }
    }
    return NULL;
  } else if (strcmp (media_type, "video/x-raw-bayer") == 0) {
    for (i = 0; i < n_fourccs; i++) {
      if (fourcc_list[i].type == VTS_BAYER) {
        return fourcc_list + i;
      }
    }
    return NULL;
  }

  g_critical ("format not found for media type %s", media_type);

  return NULL;
}

struct fourcc_list_struct *
paintrect_find_fourcc (int find_fourcc)
{
  int i;

  for (i = 0; i < n_fourccs; i++) {
    char *s;
    int fourcc;

    s = fourcc_list[i].fourcc;
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    if (find_fourcc == fourcc) {
      /* If YUV format, it's good */
      if (!fourcc_list[i].type == VTS_YUV) {
        return fourcc_list + i;
      }

      return fourcc_list + i;
    }
  }
  return NULL;
}

struct fourcc_list_struct *
paintrect_find_name (const char *name)
{
  int i;

  for (i = 0; i < n_fourccs; i++) {
    if (strcmp (name, fourcc_list[i].name) == 0) {
      return fourcc_list + i;
    }
  }
  return NULL;
}


GstStructure *
paint_get_structure (struct fourcc_list_struct * format)
{
  GstStructure *structure = NULL;
  unsigned int fourcc;
  int endianness;

  g_return_val_if_fail (format, NULL);

  fourcc =
      GST_MAKE_FOURCC (format->fourcc[0], format->fourcc[1], format->fourcc[2],
      format->fourcc[3]);

  switch (format->type) {
    case VTS_RGB:
      if (format->bitspp == 16) {
        endianness = G_BYTE_ORDER;
      } else {
        endianness = G_BIG_ENDIAN;
      }
      structure = gst_structure_new ("video/x-raw-rgb",
          "bpp", G_TYPE_INT, format->bitspp,
          "endianness", G_TYPE_INT, endianness,
          "depth", G_TYPE_INT, format->depth,
          "red_mask", G_TYPE_INT, format->red_mask,
          "green_mask", G_TYPE_INT, format->green_mask,
          "blue_mask", G_TYPE_INT, format->blue_mask, NULL);
      if (format->depth == 32 && format->alpha_mask > 0) {
        gst_structure_set (structure, "alpha_mask", G_TYPE_INT,
            format->alpha_mask, NULL);
      }
      break;
    case VTS_YUV:
      structure = gst_structure_new ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, fourcc, NULL);
      break;
    case VTS_BAYER:
      structure = gst_structure_new ("video/x-raw-bayer", NULL);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  return structure;
}

/* returns the size in bytes for one video frame of the given dimensions
 * given the fourcc in GstVideoTestSrc */
int
gst_video_test_src_get_size (GstVideoTestSrc * v, int w, int h)
{
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return 0;

  fourcc->paint_setup (p, NULL);

  return (unsigned long) p->endptr;
}

void
gst_video_test_src_smpte (GstVideoTestSrc * v, unsigned char *dest, int w,
    int h)
{
  int i;
  int y1, y2;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  y1 = 2 * h / 3;
  y2 = h * 0.75;

  /* color bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;

    p->color = vts_colors + i;
    for (j = 0; j < y1; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  /* inverse blue bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    int k;

    if (i & 1) {
      k = 7;
    } else {
      k = 6 - i;
    }
    p->color = vts_colors + k;
    for (j = y1; j < y2; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  /* -I, white, Q regions */
  for (i = 0; i < 3; i++) {
    int x1 = i * w / 6;
    int x2 = (i + 1) * w / 6;
    int k;

    if (i == 0) {
      k = 8;
    } else if (i == 1) {
      k = 0;
    } else
      k = 9;

    p->color = vts_colors + k;
    for (j = y2; j < h; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  /* superblack, black, dark grey */
  for (i = 0; i < 3; i++) {
    int x1 = w / 2 + i * w / 12;
    int x2 = w / 2 + (i + 1) * w / 12;
    int k;

    if (i == 0) {
      k = COLOR_SUPER_BLACK;
    } else if (i == 1) {
      k = COLOR_BLACK;
    } else
      k = COLOR_DARK_GREY;

    p->color = vts_colors + k;
    for (j = y2; j < h; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  {
    int x1 = w * 3 / 4;
    struct vts_color_struct color;

    color = vts_colors[COLOR_BLACK];
    p->color = &color;

    for (i = x1; i < w; i++) {
      for (j = y2; j < h; j++) {
        /* FIXME not strictly correct */
        color.Y = random_char ();
        color.R = color.Y;
        color.G = color.Y;
        color.B = color.Y;
        p->paint_hline (p, i, j, 1);
      }
    }

  }
}

void
gst_video_test_src_snow (GstVideoTestSrc * v, unsigned char *dest, int w, int h)
{
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct color;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  color = vts_colors[COLOR_BLACK];
  p->color = &color;

  for (i = 0; i < w; i++) {
    for (j = 0; j < h; j++) {
      /* FIXME not strictly correct */
      color.Y = random_char ();
      color.R = color.Y;
      color.G = color.Y;
      color.B = color.Y;
      p->paint_hline (p, i, j, 1);
    }
  }
}

static void
gst_video_test_src_unicolor (GstVideoTestSrc * v, unsigned char *dest, int w,
    int h, const struct vts_color_struct *color)
{
  int i;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = color;

  for (i = 0; i < h; i++) {
    p->paint_hline (p, 0, i, w);
  }
}

void
gst_video_test_src_black (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, vts_colors + COLOR_BLACK);
}

void
gst_video_test_src_white (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, vts_colors + COLOR_WHITE);
}

void
gst_video_test_src_red (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, vts_colors + COLOR_RED);
}

void
gst_video_test_src_green (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, vts_colors + COLOR_GREEN);
}

void
gst_video_test_src_blue (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, vts_colors + COLOR_BLUE);
}

void
gst_video_test_src_checkers1 (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  for (y = 0; y < h; y++) {
    p->color = vts_colors + COLOR_GREEN;
    p->paint_hline (p, 0, y, w);
    for (x = (y % 2); x < w; x += 2) {
      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y, 1);
    }
  }
}

void
gst_video_test_src_checkers2 (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 2) {
    for (x = ((y % 4) == 0) ? 0 : 2; x < w; x += 4) {
      guint len = (x < (w - 1)) ? 2 : (w - x);

      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y + 0, len);
      if (G_LIKELY ((y + 1) < h)) {
        p->paint_hline (p, x, y + 1, len);
      }
    }
  }
}

void
gst_video_test_src_checkers4 (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 4) {
    for (x = ((y % 8) == 0) ? 0 : 4; x < w; x += 8) {
      guint len = (x < (w - 3)) ? 4 : (w - x);

      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y + 0, len);
      if (G_LIKELY ((y + 1) < h)) {
        p->paint_hline (p, x, y + 1, len);
        if (G_LIKELY ((y + 2) < h)) {
          p->paint_hline (p, x, y + 2, len);
          if (G_LIKELY ((y + 3) < h)) {
            p->paint_hline (p, x, y + 3, len);
          }
        }
      }
    }
  }
}

void
gst_video_test_src_checkers8 (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 8) {
    for (x = ((GST_ROUND_UP_8 (y) % 16) == 0) ? 0 : 8; x < w; x += 16) {
      guint len = (x < (w - 7)) ? 8 : (w - x);

      p->color = vts_colors + COLOR_RED;
      p->paint_hline (p, x, y + 0, len);
      if (G_LIKELY ((y + 1) < h)) {
        p->paint_hline (p, x, y + 1, len);
        if (G_LIKELY ((y + 2) < h)) {
          p->paint_hline (p, x, y + 2, len);
          if (G_LIKELY ((y + 3) < h)) {
            p->paint_hline (p, x, y + 3, len);
            if (G_LIKELY ((y + 4) < h)) {
              p->paint_hline (p, x, y + 4, len);
              if (G_LIKELY ((y + 5) < h)) {
                p->paint_hline (p, x, y + 5, len);
                if (G_LIKELY ((y + 6) < h)) {
                  p->paint_hline (p, x, y + 6, len);
                  if (G_LIKELY ((y + 7) < h)) {
                    p->paint_hline (p, x, y + 7, len);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

#undef SCALE_AMPLITUDE
void
gst_video_test_src_circular (GstVideoTestSrc * v, unsigned char *dest,
    int w, int h)
{
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct color;
  static uint8_t sine_array[256];
  static int sine_array_inited = FALSE;
  double freq[8];

#ifdef SCALE_AMPLITUDE
  double ampl[8];
#endif
  int d;

  if (!sine_array_inited) {
    for (i = 0; i < 256; i++) {
      sine_array[i] =
          floor (255 * (0.5 + 0.5 * sin (i * 2 * M_PI / 256)) + 0.5);
    }
    sine_array_inited = TRUE;
  }

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  color = vts_colors[COLOR_BLACK];
  p->color = &color;

  for (i = 1; i < 8; i++) {
    freq[i] = 200 * pow (2.0, -(i - 1) / 4.0);
#ifdef SCALE_AMPLITUDE
    {
      double x;

      x = 2 * M_PI * freq[i] / w;
      ampl[i] = sin (x) / x;
    }
#endif
  }

  for (i = 0; i < w; i++) {
    for (j = 0; j < h; j++) {
      double dist;
      int seg;

      dist =
          sqrt ((2 * i - w) * (2 * i - w) + (2 * j - h) * (2 * j -
              h)) / (2 * w);
      seg = floor (dist * 16);
      if (seg == 0 || seg >= 8) {
        color.Y = 255;
      } else {
#ifdef SCALE_AMPLITUDE
        double a;
#endif
        d = floor (256 * dist * freq[seg] + 0.5);
#ifdef SCALE_AMPLITUDE
        a = ampl[seg];
        if (a < 0)
          a = 0;
        color.Y = 128 + a * (sine_array[d & 0xff] - 128);
#else
        color.Y = sine_array[d & 0xff];
#endif
      }
      color.R = color.Y;
      color.G = color.Y;
      color.B = color.Y;
      p->paint_hline (p, i, j, 1);
    }
  }
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
paint_setup_NV12 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * GST_ROUND_UP_2 (p->height);
  p->vp = p->up + 1;
  p->ustride = p->ystride;
  p->vstride = p->ystride;
  p->endptr = p->up + (p->ystride * p->height) / 2;
}

static void
paint_setup_NV21 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * GST_ROUND_UP_2 (p->height);
  p->up = p->vp + 1;
  p->ustride = p->ystride;
  p->vstride = p->ystride;
  p->endptr = p->vp + (p->ustride * p->height) / 2;
}

static void
paint_hline_I420 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->ystride;
  int offset1 = (y / 2) * p->ustride;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
  oil_splat_u8_ns (p->up + offset1 + x1, &p->color->U, x2 - x1);
  oil_splat_u8_ns (p->vp + offset1 + x1, &p->color->V, x2 - x1);
}

static void
paint_hline_NV12_NV21 (paintinfo * p, int x, int y, int w)
{
  int x1 = GST_ROUND_UP_2 (x) / 2;
  int x2 = GST_ROUND_UP_2 (x + w) / 2;
  int offset = y * p->ystride;
  int offsetuv = GST_ROUND_UP_2 ((y / 2) * p->ustride + x);
  int uvlength = x2 - x1;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
  if (uvlength) {
    oil_splat_u8 (p->up + offsetuv, 2, &p->color->U, uvlength);
    oil_splat_u8 (p->vp + offsetuv, 2, &p->color->V, uvlength);
  }
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
paint_hline_AYUV (paintinfo * p, int x, int y, int w)
{
  int offset;

  offset = (y * p->ystride) + (x * 4);
  oil_splat_u8 (p->yp + offset, 4, &p->color->Y, w);
  oil_splat_u8 (p->up + offset, 4, &p->color->U, w);
  oil_splat_u8 (p->vp + offset, 4, &p->color->V, w);
  oil_splat_u8 (p->ap + offset, 4, &p->color->A, w);
}

static void
paint_hline_YUY2 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset;

  offset = y * p->ystride;
  oil_splat_u8 (p->yp + offset + x * 2, 2, &p->color->Y, w);
  oil_splat_u8 (p->up + offset + x1 * 4, 4, &p->color->U, x2 - x1);
  oil_splat_u8 (p->vp + offset + x1 * 4, 4, &p->color->V, x2 - x1);
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
paint_hline_IYU2 (paintinfo * p, int x, int y, int w)
{
  int offset;

  offset = y * p->ystride;
  oil_splat_u8 (p->yp + offset + x * 3, 3, &p->color->Y, w);
  oil_splat_u8 (p->up + offset + x * 3, 3, &p->color->U, w);
  oil_splat_u8 (p->vp + offset + x * 3, 3, &p->color->V, w);
}

static void
paint_setup_Y41B (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->ustride = GST_ROUND_UP_8 (p->width) / 4;
  p->vp = p->up + p->ustride * p->height;
  p->vstride = GST_ROUND_UP_8 (p->width) / 4;
  p->endptr = p->vp + p->vstride * p->height;
}

static void
paint_hline_Y41B (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 4;
  int x2 = (x + w) / 4;
  int offset = y * p->ystride;
  int offset1 = y * p->ustride;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
  oil_splat_u8_ns (p->up + offset1 + x1, &p->color->U, x2 - x1);
  oil_splat_u8_ns (p->vp + offset1 + x1, &p->color->V, x2 - x1);
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
paint_hline_Y42B (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->ystride;
  int offset1 = y * p->ustride;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
  oil_splat_u8_ns (p->up + offset1 + x1, &p->color->U, x2 - x1);
  oil_splat_u8_ns (p->vp + offset1 + x1, &p->color->V, x2 - x1);
}

static void
paint_setup_Y800 (paintinfo * p, unsigned char *dest)
{
  /* untested */
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_hline_Y800 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
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

static void
paint_hline_IMC1 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->width;
  int offset1 = (y / 2) * p->width;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
  oil_splat_u8_ns (p->up + offset1 + x1, &p->color->U, x2 - x1);
  oil_splat_u8_ns (p->vp + offset1 + x1, &p->color->V, x2 - x1);
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

static void
paint_hline_YUV9 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 4;
  int x2 = (x + w) / 4;
  int offset = y * p->ystride;
  int offset1 = (y / 4) * p->ustride;

  oil_splat_u8_ns (p->yp + offset + x, &p->color->Y, w);
  oil_splat_u8_ns (p->up + offset1 + x1, &p->color->U, x2 - x1);
  oil_splat_u8_ns (p->vp + offset1 + x1, &p->color->V, x2 - x1);
}

static void
paint_setup_ARGB8888 (paintinfo * p, unsigned char *dest)
{
  paint_setup_xRGB8888 (p, dest);
  p->ap = dest;
}

static void
paint_setup_ABGR8888 (paintinfo * p, unsigned char *dest)
{
  paint_setup_xBGR8888 (p, dest);
  p->ap = dest;
}

static void
paint_setup_RGBA8888 (paintinfo * p, unsigned char *dest)
{
  paint_setup_RGBx8888 (p, dest);
  p->ap = dest + 3;
}

static void
paint_setup_BGRA8888 (paintinfo * p, unsigned char *dest)
{
  paint_setup_BGRx8888 (p, dest);
  p->ap = dest + 3;
}

static void
paint_setup_xRGB8888 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 1;
  p->up = dest + 2;
  p->vp = dest + 3;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_xBGR8888 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 3;
  p->up = dest + 2;
  p->vp = dest + 1;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_RGBx8888 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 1;
  p->vp = dest + 2;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_BGRx8888 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 2;
  p->up = dest + 1;
  p->vp = dest + 0;
  p->ystride = p->width * 4;
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_RGB888 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 1;
  p->vp = dest + 2;
  p->ystride = GST_ROUND_UP_4 (p->width * 3);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_setup_BGR888 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 2;
  p->up = dest + 1;
  p->vp = dest + 0;
  p->ystride = GST_ROUND_UP_4 (p->width * 3);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_str4 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;

  oil_splat_u8 (p->yp + offset + x * 4, 4, &p->color->R, w);
  oil_splat_u8 (p->up + offset + x * 4, 4, &p->color->G, w);
  oil_splat_u8 (p->vp + offset + x * 4, 4, &p->color->B, w);

  if (p->ap != NULL) {
    oil_splat_u8 (p->ap + offset + (x * 4), 4, &p->color->A, w);
  }
}

static void
paint_hline_str3 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;

  oil_splat_u8 (p->yp + offset + x * 3, 3, &p->color->R, w);
  oil_splat_u8 (p->up + offset + x * 3, 3, &p->color->G, w);
  oil_splat_u8 (p->vp + offset + x * 3, 3, &p->color->B, w);
}

static void
paint_setup_RGB565 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width * 2);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_RGB565 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;
  uint8_t a, b;

  a = (p->color->R & 0xf8) | (p->color->G >> 5);
  b = ((p->color->G << 3) & 0xe0) | (p->color->B >> 3);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, &b, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, &a, w);
#else
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, &a, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, &b, w);
#endif
}

static void
paint_setup_xRGB1555 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width * 2);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_xRGB1555 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;
  uint8_t a, b;

  a = ((p->color->R >> 1) & 0x7c) | (p->color->G >> 6);
  b = ((p->color->G << 2) & 0xe0) | (p->color->B >> 3);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, &b, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, &a, w);
#else
  oil_splat_u8 (p->yp + offset + x * 2 + 0, 2, &a, w);
  oil_splat_u8 (p->yp + offset + x * 2 + 1, 2, &b, w);
#endif
}


static void
paint_setup_bayer (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->endptr = p->dest + p->ystride * p->height;
}

static void
paint_hline_bayer (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;
  uint8_t *dest = p->yp + offset;
  int i;

  if (y & 1) {
    for (i = x; i < x + w; i++) {
      if (i & 1) {
        dest[i] = p->color->G;
      } else {
        dest[i] = p->color->B;
      }
    }
  } else {
    for (i = x; i < x + w; i++) {
      if (i & 1) {
        dest[i] = p->color->R;
      } else {
        dest[i] = p->color->G;
      }
    }
  }
}
