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
#include "gstvideotestsrcorc.h"


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

static void
oil_splat_u8 (guint8 * dest, int stride, const guint8 * value, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    *dest = *value;
    dest += stride;
  }
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
    gst_orc_splat_u8 (d, &color, w);
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

static const struct vts_color_struct_rgb vts_colors_rgb[] = {
  {255, 255, 255},
  {255, 255, 0},
  {0, 255, 255},
  {0, 255, 0},
  {255, 0, 255},
  {255, 0, 0},
  {0, 0, 255},
  {0, 0, 0},
  {0, 0, 128},                  /* -I ? */
  {0, 128, 255},                /* +Q ? */
  {0, 0, 0},
  {19, 19, 19},
};

static const struct vts_color_struct_rgb vts_colors_rgb_75[] = {
  {191, 191, 191},
  {191, 191, 0},
  {0, 191, 191},
  {0, 191, 0},
  {191, 0, 191},
  {191, 0, 0},
  {0, 0, 191},
  {0, 0, 0},
  {0, 0, 128},                  /* -I ? */
  {0, 128, 255},                /* +Q ? */
  {0, 0, 0},
  {19, 19, 19},
};

static const struct vts_color_struct_yuv vts_colors_bt709_ycbcr_100[] = {
  {235, 128, 128},
  {219, 16, 138},
  {188, 154, 16},
  {173, 42, 26},
  {78, 214, 230},
  {63, 102, 240},
  {32, 240, 118},
  {16, 128, 128},
  {16, 198, 21},                /* -I ? */
  {16, 235, 198},               /* +Q ? */
  {0, 128, 128},
  {32, 128, 128},
};

static const struct vts_color_struct_yuv vts_colors_bt709_ycbcr_75[] = {
  {180, 128, 128},
  {168, 44, 136},
  {145, 147, 44},
  {133, 63, 52},
  {63, 193, 204},
  {51, 109, 212},
  {28, 212, 120},
  {16, 128, 128},
  {16, 198, 21},                /* -I ? */
  {16, 235, 198},               /* +Q ? */
  {0, 128, 128},
  {32, 128, 128},
};

static const struct vts_color_struct_yuv vts_colors_bt601_ycbcr_100[] = {
  {235, 128, 128},
  {210, 16, 146},
  {170, 166, 16},
  {145, 54, 34},
  {106, 202, 222},
  {81, 90, 240},
  {41, 240, 110},
  {16, 128, 128},
  {16, 198, 21},                /* -I ? */
  {16, 235, 198},               /* +Q ? */
  {-0, 128, 128},
  {32, 128, 128},
};

static const struct vts_color_struct_yuv vts_colors_bt601_ycbcr_75[] = {
  {180, 128, 128},
  {162, 44, 142},
  {131, 156, 44},
  {112, 72, 58},
  {84, 184, 198},
  {65, 100, 212},
  {35, 212, 114},
  {16, 128, 128},
  {16, 198, 21},                /* -I ? */
  {16, 235, 198},               /* +Q ? */
  {-0, 128, 128},
  {32, 128, 128},
};

static const struct vts_color_struct_gray vts_colors_gray_100[] = {
  {235 << 8},
  {210 << 8},
  {170 << 8},
  {145 << 8},
  {106 << 8},
  {81 << 8},
  {41 << 8},
  {16 << 8},
  {16 << 8},
  {16 << 8},
  {-0 << 8},
  {32 << 8},
};

static const struct vts_color_struct_gray vts_colors_gray_75[] = {
  {180 << 8},
  {162 << 8},
  {131 << 8},
  {112 << 8},
  {84 << 8},
  {65 << 8},
  {35 << 8},
  {16 << 8},
  {16 << 8},
  {16 << 8},
  {-0 << 8},
  {32 << 8},
};

static void paint_setup_I420 (paintinfo * p, unsigned char *dest);
static void paint_setup_YV12 (paintinfo * p, unsigned char *dest);
static void paint_setup_YUY2 (paintinfo * p, unsigned char *dest);
static void paint_setup_UYVY (paintinfo * p, unsigned char *dest);
static void paint_setup_YVYU (paintinfo * p, unsigned char *dest);
static void paint_setup_IYU2 (paintinfo * p, unsigned char *dest);
static void paint_setup_Y41B (paintinfo * p, unsigned char *dest);
static void paint_setup_Y42B (paintinfo * p, unsigned char *dest);
static void paint_setup_Y444 (paintinfo * p, unsigned char *dest);
static void paint_setup_Y800 (paintinfo * p, unsigned char *dest);
static void paint_setup_AYUV (paintinfo * p, unsigned char *dest);
static void paint_setup_v308 (paintinfo * p, unsigned char *dest);
static void paint_setup_NV12 (paintinfo * p, unsigned char *dest);
static void paint_setup_NV21 (paintinfo * p, unsigned char *dest);
static void paint_setup_v410 (paintinfo * p, unsigned char *dest);
static void paint_setup_v216 (paintinfo * p, unsigned char *dest);
static void paint_setup_v210 (paintinfo * p, unsigned char *dest);

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
static void paint_hline_Y444 (paintinfo * p, int x, int y, int w);
static void paint_hline_Y800 (paintinfo * p, int x, int y, int w);
static void paint_hline_v308 (paintinfo * p, int x, int y, int w);
static void paint_hline_AYUV (paintinfo * p, int x, int y, int w);
static void paint_hline_v410 (paintinfo * p, int x, int y, int w);
static void paint_hline_v216 (paintinfo * p, int x, int y, int w);
static void paint_hline_v210 (paintinfo * p, int x, int y, int w);

#if 0
static void paint_hline_IMC1 (paintinfo * p, int x, int y, int w);
#endif
static void paint_hline_YUV9 (paintinfo * p, int x, int y, int w);
static void paint_hline_str4 (paintinfo * p, int x, int y, int w);
static void paint_hline_str3 (paintinfo * p, int x, int y, int w);
static void paint_hline_RGB565 (paintinfo * p, int x, int y, int w);
static void paint_hline_xRGB1555 (paintinfo * p, int x, int y, int w);

static void paint_hline_bayer (paintinfo * p, int x, int y, int w);

static void paint_setup_GRAY8 (paintinfo * p, unsigned char *dest);
static void paint_setup_GRAY16 (paintinfo * p, unsigned char *dest);
static void paint_hline_GRAY8 (paintinfo * p, int x, int y, int w);
static void paint_hline_GRAY16 (paintinfo * p, int x, int y, int w);

struct fourcc_list_struct fourcc_list[] = {
/* packed */
  {VTS_YUV, "YUY2", "YUY2", 16, paint_setup_YUY2, paint_hline_YUY2},
  {VTS_YUV, "UYVY", "UYVY", 16, paint_setup_UYVY, paint_hline_YUY2},
  {VTS_YUV, "Y422", "Y422", 16, paint_setup_UYVY, paint_hline_YUY2},
  {VTS_YUV, "UYNV", "UYNV", 16, paint_setup_UYVY, paint_hline_YUY2},    /* FIXME: UYNV? */
  {VTS_YUV, "YVYU", "YVYU", 16, paint_setup_YVYU, paint_hline_YUY2},
  {VTS_YUV, "v308", "v308", 24, paint_setup_v308, paint_hline_v308},
  {VTS_YUV, "AYUV", "AYUV", 32, paint_setup_AYUV, paint_hline_AYUV},
  {VTS_YUV, "v410", "v410", 32, paint_setup_v410, paint_hline_v410},
  {VTS_YUV, "v210", "v210", 21, paint_setup_v210, paint_hline_v210},
  {VTS_YUV, "v216", "v216", 32, paint_setup_v216, paint_hline_v216},

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
  /* Y444 */
  {VTS_YUV, "Y444", "Y444", 24, paint_setup_Y444, paint_hline_Y444},
  /* Y800 grayscale */
  {VTS_YUV, "Y800", "Y800", 8, paint_setup_Y800, paint_hline_Y800},

  /* Not exactly YUV but it's the same as above */
  {VTS_GRAY, "GRAY8", "GRAY8", 8, paint_setup_GRAY8, paint_hline_GRAY8},
  {VTS_GRAY, "GRAY16", "GRAY16", 16, paint_setup_GRAY16, paint_hline_GRAY16},

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

  if (strcmp (media_type, "video/x-raw-gray") == 0) {
    gint bpp, depth, endianness = 0;

    ret = gst_structure_get_int (structure, "bpp", &bpp) &&
        gst_structure_get_int (structure, "depth", &depth);
    if (!ret || bpp != depth || (depth != 8 && depth != 16))
      return NULL;

    ret = gst_structure_get_int (structure, "endianness", &endianness);
    if ((!ret || endianness != G_BYTE_ORDER) && bpp == 16)
      return NULL;

    for (i = 0; i < n_fourccs; i++) {
      if (fourcc_list[i].type == VTS_GRAY && fourcc_list[i].bitspp == bpp) {
        return fourcc_list + i;
      }
    }
  } else if (strcmp (media_type, "video/x-raw-yuv") == 0) {
    const char *s;
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
    const char *s;
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
    case VTS_GRAY:
      structure = gst_structure_new ("video/x-raw-gray",
          "bpp", G_TYPE_INT, format->bitspp, "depth", G_TYPE_INT,
          format->bitspp, NULL);
      if (format->bitspp == 16)
        gst_structure_set (structure, "endianness", G_TYPE_INT, G_BYTE_ORDER,
            NULL);
      break;
    case VTS_YUV:
    {
      GValue value_list = { 0 };
      GValue value = { 0 };

      structure = gst_structure_new ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, fourcc, NULL);

      if (fourcc != GST_STR_FOURCC ("Y800")) {
        g_value_init (&value_list, GST_TYPE_LIST);

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_static_string (&value, "sdtv");
        gst_value_list_append_value (&value_list, &value);

        g_value_set_static_string (&value, "hdtv");
        gst_value_list_append_value (&value_list, &value);

        gst_structure_set_value (structure, "color-matrix", &value_list);
        g_value_reset (&value_list);

        if (fourcc != GST_STR_FOURCC ("AYUV") &&
            fourcc != GST_STR_FOURCC ("v308") &&
            fourcc != GST_STR_FOURCC ("v410") &&
            fourcc != GST_STR_FOURCC ("Y444")) {
          g_value_set_static_string (&value, "mpeg2");
          gst_value_list_append_value (&value_list, &value);

          g_value_set_static_string (&value, "jpeg");
          gst_value_list_append_value (&value_list, &value);

          gst_structure_set_value (structure, "chroma-site", &value_list);
        }
        g_value_unset (&value_list);
      }
    }
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

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
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

    p->yuv_color = p->yuv_colors + i;
    p->rgb_color = p->rgb_colors + i;
    p->gray_color = p->gray_colors + i;
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
    p->yuv_color = p->yuv_colors + k;
    p->rgb_color = p->rgb_colors + k;
    p->gray_color = p->gray_colors + k;
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

    p->yuv_color = p->yuv_colors + k;
    p->rgb_color = p->rgb_colors + k;
    p->gray_color = p->gray_colors + k;
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

    p->yuv_color = p->yuv_colors + k;
    p->rgb_color = p->rgb_colors + k;
    p->gray_color = p->gray_colors + k;
    for (j = y2; j < h; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
    }
  }

  {
    int x1 = w * 3 / 4;
    struct vts_color_struct_rgb rgb_color;
    struct vts_color_struct_yuv yuv_color;
    struct vts_color_struct_gray gray_color;

    rgb_color = p->rgb_colors[COLOR_BLACK];
    yuv_color = p->yuv_colors[COLOR_BLACK];
    gray_color = p->gray_colors[COLOR_BLACK];
    p->rgb_color = &rgb_color;
    p->yuv_color = &yuv_color;
    p->gray_color = &gray_color;

    for (i = x1; i < w; i++) {
      for (j = y2; j < h; j++) {
        /* FIXME not strictly correct */
        int y = random_char ();
        yuv_color.Y = y;
        rgb_color.R = y;
        rgb_color.G = y;
        rgb_color.B = y;
        gray_color.G = (y << 8) | random_char ();
        p->paint_hline (p, i, j, 1);
      }
    }

  }
}

void
gst_video_test_src_smpte75 (GstVideoTestSrc * v, unsigned char *dest, int w,
    int h)
{
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->rgb_colors = vts_colors_rgb_75;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_75;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_75;
  }
  p->gray_colors = vts_colors_gray_75;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  /* color bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;

    p->yuv_color = p->yuv_colors + i;
    p->rgb_color = p->rgb_colors + i;
    p->gray_color = p->gray_colors + i;
    for (j = 0; j < h; j++) {
      p->paint_hline (p, x1, j, (x2 - x1));
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
  struct vts_color_struct_rgb rgb_color;
  struct vts_color_struct_yuv yuv_color;
  struct vts_color_struct_gray gray_color;

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  rgb_color = p->rgb_colors[COLOR_BLACK];
  yuv_color = p->yuv_colors[COLOR_BLACK];
  gray_color = p->gray_colors[COLOR_BLACK];
  p->rgb_color = &rgb_color;
  p->yuv_color = &yuv_color;
  p->gray_color = &gray_color;

  for (i = 0; i < w; i++) {
    for (j = 0; j < h; j++) {
      /* FIXME not strictly correct */
      int y = random_char ();
      yuv_color.Y = y;
      rgb_color.R = y;
      rgb_color.G = y;
      rgb_color.B = y;
      gray_color.G = (y << 8) | random_char ();
      p->paint_hline (p, i, j, 1);
    }
  }
}

static void
gst_video_test_src_unicolor (GstVideoTestSrc * v, unsigned char *dest, int w,
    int h, int color_index)
{
  int i;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->rgb_color = p->rgb_colors + color_index;
  p->yuv_color = p->yuv_colors + color_index;
  p->gray_color = p->gray_colors + color_index;

  for (i = 0; i < h; i++) {
    p->paint_hline (p, 0, i, w);
  }
}

void
gst_video_test_src_black (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, COLOR_BLACK);
}

void
gst_video_test_src_white (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, COLOR_WHITE);
}

void
gst_video_test_src_red (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, COLOR_RED);
}

void
gst_video_test_src_green (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, COLOR_GREEN);
}

void
gst_video_test_src_blue (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  gst_video_test_src_unicolor (v, dest, w, h, COLOR_BLUE);
}

void
gst_video_test_src_checkers1 (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  for (y = 0; y < h; y++) {
    p->rgb_color = p->rgb_colors + COLOR_GREEN;
    p->yuv_color = p->yuv_colors + COLOR_GREEN;
    p->gray_color = p->gray_colors + COLOR_GREEN;
    p->paint_hline (p, 0, y, w);
    for (x = (y % 2); x < w; x += 2) {
      p->rgb_color = p->rgb_colors + COLOR_RED;
      p->yuv_color = p->yuv_colors + COLOR_RED;
      p->gray_color = p->gray_colors + COLOR_RED;
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

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->rgb_color = p->rgb_colors + COLOR_GREEN;
  p->yuv_color = p->yuv_colors + COLOR_GREEN;
  p->gray_color = p->gray_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 2) {
    for (x = ((y % 4) == 0) ? 0 : 2; x < w; x += 4) {
      guint len = (x < (w - 1)) ? 2 : (w - x);

      p->rgb_color = p->rgb_colors + COLOR_RED;
      p->yuv_color = p->yuv_colors + COLOR_RED;
      p->gray_color = p->gray_colors + COLOR_RED;
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

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->rgb_color = p->rgb_colors + COLOR_GREEN;
  p->yuv_color = p->yuv_colors + COLOR_GREEN;
  p->gray_color = p->gray_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    p->paint_hline (p, 0, y, w);
  }

  for (y = 0; y < h; y += 4) {
    for (x = ((y % 8) == 0) ? 0 : 4; x < w; x += 8) {
      guint len = (x < (w - 3)) ? 4 : (w - x);

      p->rgb_color = p->rgb_colors + COLOR_RED;
      p->yuv_color = p->yuv_colors + COLOR_RED;
      p->gray_color = p->gray_colors + COLOR_RED;
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

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->rgb_color = p->rgb_colors + COLOR_GREEN;
  p->yuv_color = p->yuv_colors + COLOR_GREEN;
  p->gray_color = p->gray_colors + COLOR_GREEN;
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x += 8) {
      int len = MIN (8, w - x);

      if ((x ^ y) & (1 << 3)) {
        p->rgb_color = p->rgb_colors + COLOR_GREEN;
        p->yuv_color = p->yuv_colors + COLOR_GREEN;
        p->gray_color = p->gray_colors + COLOR_GREEN;
      } else {
        p->rgb_color = p->rgb_colors + COLOR_RED;
        p->yuv_color = p->yuv_colors + COLOR_RED;
        p->gray_color = p->gray_colors + COLOR_RED;
      }
      p->paint_hline (p, x, y, len);
    }
  }
}

void
gst_video_test_src_zoneplate (GstVideoTestSrc * v, unsigned char *dest,
    int w, int h)
{
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct_rgb rgb_color;
  struct vts_color_struct_yuv yuv_color;
  struct vts_color_struct_gray gray_color;
  static guint8 sine_array[256];
  static int sine_array_inited = FALSE;

  static int t = 0;             /* time - increment phase vs time by 1 for each generated frame */
  /* this may not fit with the correct gstreamer notion of time, so maybe FIXME? */

  int xreset = -(w / 2) - v->xoffset;   /* starting values for x^2 and y^2, centering the ellipse */
  int yreset = -(h / 2) - v->yoffset;

  int x, y;
  int accum_kx;
  int accum_kxt;
  int accum_ky;
  int accum_kyt;
  int accum_kxy;
  int kt;
  int kt2;
  int ky2;
  int delta_kxt = v->kxt * t;
  int delta_kxy;
  int scale_kxy = 0xffff / (w / 2);
  int scale_kx2 = 0xffff / w;

  if (!sine_array_inited) {
    int black = 16;
    int white = 235;
    int range = white - black;
    for (i = 0; i < 256; i++) {
      sine_array[i] =
          floor (range * (0.5 + 0.5 * sin (i * 2 * M_PI / 256)) + 0.5 + black);
    }
    sine_array_inited = TRUE;
  }

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  rgb_color = p->rgb_colors[COLOR_BLACK];
  yuv_color = p->yuv_colors[COLOR_BLACK];
  gray_color = p->gray_colors[COLOR_BLACK];
  p->rgb_color = &rgb_color;
  p->yuv_color = &yuv_color;
  p->gray_color = &gray_color;

  /* Zoneplate equation:
   *
   * phase = k0 + kx*x + ky*y + kt*t
   *       + kxt*x*t + kyt*y*t + kxy*x*y
   *       + kx2*x*x + ky2*y*y + Kt2*t*t
   */

#if 0
  for (j = 0, y = yreset; j < h; j++, y++) {
    for (i = 0, x = xreset; i < w; i++, x++) {

      /* zero order */
      int phase = v->k0;

      /* first order */
      phase = phase + (v->kx * i) + (v->ky * j) + (v->kt * t);

      /* cross term */
      /* phase = phase + (v->kxt * i * t) + (v->kyt * j * t); */
      /* phase = phase + (v->kxy * x * y) / (w/2); */

      /*second order */
      /*normalise x/y terms to rate of change of phase at the picture edge */
      phase =
          phase + ((v->kx2 * x * x) / w) + ((v->ky2 * y * y) / h) +
          ((v->kt2 * t * t) >> 1);

      color.Y = sine_array[phase & 0xff];

      color.R = color.Y;
      color.G = color.Y;
      color.B = color.Y;
      p->paint_hline (p, i, j, 1);
    }
  }
#endif

  /* optimised version, with original code shown in comments */
  accum_ky = 0;
  accum_kyt = 0;
  kt = v->kt * t;
  kt2 = v->kt2 * t * t;
  for (j = 0, y = yreset; j < h; j++, y++) {
    accum_kx = 0;
    accum_kxt = 0;
    accum_ky += v->ky;
    accum_kyt += v->kyt * t;
    delta_kxy = v->kxy * y * scale_kxy;
    accum_kxy = delta_kxy * xreset;
    ky2 = (v->ky2 * y * y) / h;
    for (i = 0, x = xreset; i < w; i++, x++) {

      /* zero order */
      int phase = v->k0;

      /* first order */
      accum_kx += v->kx;
      /* phase = phase + (v->kx * i) + (v->ky * j) + (v->kt * t); */
      phase = phase + accum_kx + accum_ky + kt;

      /* cross term */
      accum_kxt += delta_kxt;
      accum_kxy += delta_kxy;
      /* phase = phase + (v->kxt * i * t) + (v->kyt * j * t); */
      phase = phase + accum_kxt + accum_kyt;

      /* phase = phase + (v->kxy * x * y) / (w/2); */
      /* phase = phase + accum_kxy / (w/2); */
      phase = phase + (accum_kxy >> 16);

      /*second order */
      /*normalise x/y terms to rate of change of phase at the picture edge */
      /*phase = phase + ((v->kx2 * x * x)/w) + ((v->ky2 * y * y)/h) + ((v->kt2 * t * t)>>1); */
      phase = phase + ((v->kx2 * x * x * scale_kx2) >> 16) + ky2 + (kt2 >> 1);

      yuv_color.Y = sine_array[phase & 0xff];

      rgb_color.R = yuv_color.Y;
      rgb_color.G = yuv_color.Y;
      rgb_color.B = yuv_color.Y;

      gray_color.G = yuv_color.Y << 8;
      p->paint_hline (p, i, j, 1);
    }
  }

  t++;
}

void
gst_video_test_src_chromazoneplate (GstVideoTestSrc * v, unsigned char *dest,
    int w, int h)
{
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct_rgb rgb_color;
  struct vts_color_struct_yuv yuv_color;
  struct vts_color_struct_gray gray_color;
  static guint8 sine_array[256];
  static int sine_array_inited = FALSE;

  static int t = 0;             /* time - increment phase vs time by 1 for each generated frame */
  /* this may not fit with the correct gstreamer notion of time, so maybe FIXME? */

  int xreset = -(w / 2) - v->xoffset;   /* starting values for x^2 and y^2, centering the ellipse */
  int yreset = -(h / 2) - v->yoffset;

  int x, y;
  int accum_kx;
  int accum_kxt;
  int accum_ky;
  int accum_kyt;
  int accum_kxy;
  int kt;
  int kt2;
  int ky2;
  int delta_kxt = v->kxt * t;
  int delta_kxy;
  int scale_kxy = 0xffff / (w / 2);
  int scale_kx2 = 0xffff / w;

  if (!sine_array_inited) {
    int black = 16;
    int white = 235;
    int range = white - black;
    for (i = 0; i < 256; i++) {
      sine_array[i] =
          floor (range * (0.5 + 0.5 * sin (i * 2 * M_PI / 256)) + 0.5 + black);
    }
    sine_array_inited = TRUE;
  }

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  rgb_color = p->rgb_colors[COLOR_BLACK];
  yuv_color = p->yuv_colors[COLOR_BLACK];
  gray_color = p->gray_colors[COLOR_BLACK];
  p->rgb_color = &rgb_color;
  p->yuv_color = &yuv_color;
  p->gray_color = &gray_color;

  /* Zoneplate equation:
   *
   * phase = k0 + kx*x + ky*y + kt*t
   *       + kxt*x*t + kyt*y*t + kxy*x*y
   *       + kx2*x*x + ky2*y*y + Kt2*t*t
   */

  /* optimised version, with original code shown in comments */
  accum_ky = 0;
  accum_kyt = 0;
  kt = v->kt * t;
  kt2 = v->kt2 * t * t;
  for (j = 0, y = yreset; j < h; j++, y++) {
    accum_kx = 0;
    accum_kxt = 0;
    accum_ky += v->ky;
    accum_kyt += v->kyt * t;
    delta_kxy = v->kxy * y * scale_kxy;
    accum_kxy = delta_kxy * xreset;
    ky2 = (v->ky2 * y * y) / h;
    for (i = 0, x = xreset; i < w; i++, x++) {

      /* zero order */
      int phase = v->k0;

      /* first order */
      accum_kx += v->kx;
      /* phase = phase + (v->kx * i) + (v->ky * j) + (v->kt * t); */
      phase = phase + accum_kx + accum_ky + kt;

      /* cross term */
      accum_kxt += delta_kxt;
      accum_kxy += delta_kxy;
      /* phase = phase + (v->kxt * i * t) + (v->kyt * j * t); */
      phase = phase + accum_kxt + accum_kyt;

      /* phase = phase + (v->kxy * x * y) / (w/2); */
      /* phase = phase + accum_kxy / (w/2); */
      phase = phase + (accum_kxy >> 16);

      /*second order */
      /*normalise x/y terms to rate of change of phase at the picture edge */
      /*phase = phase + ((v->kx2 * x * x)/w) + ((v->ky2 * y * y)/h) + ((v->kt2 * t * t)>>1); */
      phase = phase + ((v->kx2 * x * x * scale_kx2) >> 16) + ky2 + (kt2 >> 1);

      yuv_color.Y = 128;
      yuv_color.U = sine_array[phase & 0xff];
      yuv_color.V = sine_array[phase & 0xff];

      rgb_color.R = 128;
      rgb_color.G = 128;
      rgb_color.B = yuv_color.V;

      gray_color.G = yuv_color.Y << 8;
      p->paint_hline (p, i, j, 1);
    }
  }

  t++;
}

#undef SCALE_AMPLITUDE
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
  struct vts_color_struct_rgb rgb_color;
  struct vts_color_struct_yuv yuv_color;
  struct vts_color_struct_gray gray_color;
  static guint8 sine_array[256];
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

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  rgb_color = p->rgb_colors[COLOR_BLACK];
  yuv_color = p->yuv_colors[COLOR_BLACK];
  gray_color = p->gray_colors[COLOR_BLACK];
  p->rgb_color = &rgb_color;
  p->yuv_color = &yuv_color;
  p->gray_color = &gray_color;

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
        yuv_color.Y = 255;
        gray_color.G = 65535;
      } else {
#ifdef SCALE_AMPLITUDE
        double a;
#endif
        d = floor (256 * dist * freq[seg] + 0.5);
#ifdef SCALE_AMPLITUDE
        a = ampl[seg];
        if (a < 0)
          a = 0;
        yuv_color.Y = 128 + a * (sine_array[d & 0xff] - 128);
        gray_color.G = 128 + a * (sine_array[d & 0xff] - 128);
#else
        yuv_color.Y = sine_array[d & 0xff];
        gray_color.G = sine_array[d & 0xff];
#endif
      }
      rgb_color.R = yuv_color.Y;
      rgb_color.G = yuv_color.Y;
      rgb_color.B = yuv_color.Y;
      p->paint_hline (p, i, j, 1);
    }
  }
}

void
gst_video_test_src_gamut (GstVideoTestSrc * v, guchar * dest, int w, int h)
{
  int x, y;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct_yuv yuv_primary;
  struct vts_color_struct_yuv yuv_secondary;
  struct vts_color_struct_rgb rgb_primary = { 0 };
  struct vts_color_struct_rgb rgb_secondary = { 0 };
  struct vts_color_struct_gray gray_primary = { 0 };
  struct vts_color_struct_gray gray_secondary = { 0 };

  p->rgb_colors = vts_colors_rgb;
  if (v->color_spec == GST_VIDEO_TEST_SRC_BT601) {
    p->yuv_colors = vts_colors_bt601_ycbcr_100;
  } else {
    p->yuv_colors = vts_colors_bt709_ycbcr_100;
  }
  p->gray_colors = vts_colors_gray_100;
  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  for (y = 0; y < h; y++) {
    int region = (y * 4) / h;

    switch (region) {
      case 0:                  /* black */
        yuv_primary = p->yuv_colors[COLOR_BLACK];
        yuv_secondary = p->yuv_colors[COLOR_BLACK];
        yuv_secondary.Y = 0;
        rgb_primary = p->rgb_colors[COLOR_BLACK];
        rgb_secondary = p->rgb_colors[COLOR_BLACK];
        gray_primary = p->gray_colors[COLOR_BLACK];
        gray_secondary = p->gray_colors[COLOR_BLACK];
        break;
      case 1:
        yuv_primary = p->yuv_colors[COLOR_WHITE];
        yuv_secondary = p->yuv_colors[COLOR_WHITE];
        yuv_secondary.Y = 255;
        rgb_primary = p->rgb_colors[COLOR_WHITE];
        rgb_secondary = p->rgb_colors[COLOR_WHITE];
        gray_primary = p->gray_colors[COLOR_WHITE];
        gray_secondary = p->gray_colors[COLOR_WHITE];
        break;
      case 2:
        yuv_primary = p->yuv_colors[COLOR_RED];
        yuv_secondary = p->yuv_colors[COLOR_RED];
        yuv_secondary.V = 255;
        rgb_primary = p->rgb_colors[COLOR_RED];
        rgb_secondary = p->rgb_colors[COLOR_RED];
        gray_primary = p->gray_colors[COLOR_RED];
        gray_secondary = p->gray_colors[COLOR_RED];
        break;
      case 3:
        yuv_primary = p->yuv_colors[COLOR_BLUE];
        yuv_secondary = p->yuv_colors[COLOR_BLUE];
        yuv_secondary.U = 255;
        rgb_primary = p->rgb_colors[COLOR_BLUE];
        rgb_secondary = p->rgb_colors[COLOR_BLUE];
        gray_primary = p->gray_colors[COLOR_BLUE];
        gray_secondary = p->gray_colors[COLOR_BLUE];
        break;
    }

    for (x = 0; x < w; x += 8) {
      int len = MIN (8, w - x);

      if ((x ^ y) & (1 << 4)) {
        p->rgb_color = &rgb_primary;
        p->yuv_color = &yuv_primary;
        p->gray_color = &gray_primary;
      } else {
        p->rgb_color = &rgb_secondary;
        p->yuv_color = &yuv_secondary;
        p->gray_color = &gray_secondary;
      }
      p->paint_hline (p, x, y, len);
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
  p->endptr = p->up + (p->ystride * GST_ROUND_UP_2 (p->height)) / 2;
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
  p->endptr = p->vp + (p->ystride * GST_ROUND_UP_2 (p->height)) / 2;
}

static void
paint_hline_I420 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int w1 = (x + w) / 2 - x1;
  int offset = y * p->ystride;
  int offset1 = (y / 2) * p->ustride;

  if (x + w == p->width && p->width % 2 != 0)
    w1++;
  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
  gst_orc_splat_u8 (p->up + offset1 + x1, p->yuv_color->U, w1);
  gst_orc_splat_u8 (p->vp + offset1 + x1, p->yuv_color->V, w1);
}

static void
paint_hline_NV12_NV21 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->ystride;
  int offsetuv = (y / 2) * p->ustride + (x & ~0x01);
  int uvlength = x2 - x1 + 1;
  guint16 value;

  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  value = (p->yuv_color->U << 0) | (p->yuv_color->V << 8);
#else
  value = (p->yuv_color->U << 8) | (p->yuv_color->V << 0);
#endif

  if (uvlength) {
    gst_orc_splat_u16 (p->up + offsetuv, value, uvlength);
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
paint_setup_v308 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->up = dest + 1;
  p->vp = dest + 2;
  p->ystride = GST_ROUND_UP_4 (p->width * 3);
  p->endptr = dest + p->ystride * p->height;
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
paint_setup_v410 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 0;
  p->vp = dest + 0;
  p->ystride = p->width * 4;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_v216 (paintinfo * p, unsigned char *dest)
{
  p->ap = dest;
  p->yp = dest + 2;
  p->up = dest + 0;
  p->vp = dest + 4;
  p->ystride = p->width * 4;
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_setup_v210 (paintinfo * p, unsigned char *dest)
{
  p->ap = dest;
  p->yp = dest + 0;
  p->up = dest + 0;
  p->vp = dest + 0;
  p->ystride = ((p->width + 47) / 48) * 128;    /* no, really. */
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
paint_hline_v308 (paintinfo * p, int x, int y, int w)
{
  int offset;
  int i;

  offset = (y * p->ystride) + (x * 3);
  for (i = 0; i < w; i++) {
    p->yp[offset + 3 * i] = p->yuv_color->Y;
    p->up[offset + 3 * i] = p->yuv_color->U;
    p->vp[offset + 3 * i] = p->yuv_color->V;
  }
}

static void
paint_hline_AYUV (paintinfo * p, int x, int y, int w)
{
  int offset;
  guint8 alpha = 255;
  guint32 value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  value = (alpha << 0) | (p->yuv_color->Y << 8) |
      (p->yuv_color->U << 16) | (p->yuv_color->V << 24);
#else
  value = (alpha << 24) | (p->yuv_color->Y << 16) |
      (p->yuv_color->U << 8) | (p->yuv_color->V << 0);
#endif

  offset = (y * p->ystride) + (x * 4);
  gst_orc_splat_u32 (p->ap + offset, value, w);
}

#define TO_16(x) (((x)<<8) | (x))
#define TO_10(x) (((x)<<2) | ((x)>>6))

static void
paint_hline_v216 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  guint16 Y, U, V;
  int i;
  int offset;

  offset = y * p->ystride;
  Y = TO_16 (p->yuv_color->Y);
  U = TO_16 (p->yuv_color->U);
  V = TO_16 (p->yuv_color->V);
  for (i = x; i < x + w; i++) {
    GST_WRITE_UINT16_LE (p->yp + offset + i * 4, Y);
  }
  for (i = x1; i < x2; i++) {
    GST_WRITE_UINT16_LE (p->up + offset + i * 8, U);
    GST_WRITE_UINT16_LE (p->vp + offset + i * 8, V);
  }
}

static void
paint_hline_v410 (paintinfo * p, int x, int y, int w)
{
  guint32 a;
  guint8 *data;
  int i;

  a = (TO_10 (p->yuv_color->U) << 22) |
      (TO_10 (p->yuv_color->Y) << 12) | (TO_10 (p->yuv_color->V) << 2);

  data = p->yp + y * p->ystride + x * 4;
  for (i = 0; i < w; i++) {
    GST_WRITE_UINT32_LE (data, a);
  }
}

static void
paint_hline_v210 (paintinfo * p, int x, int y, int w)
{
  guint32 a0, a1, a2, a3;
  guint8 *data;
  int i;

  /* FIXME this is kinda gross.  it only handles x values in
     multiples of 6 */

  a0 = TO_10 (p->yuv_color->U) | (TO_10 (p->yuv_color->Y) << 10)
      | (TO_10 (p->yuv_color->V) << 20);
  a1 = TO_10 (p->yuv_color->Y) | (TO_10 (p->yuv_color->U) << 10)
      | (TO_10 (p->yuv_color->Y) << 20);
  a2 = TO_10 (p->yuv_color->V) | (TO_10 (p->yuv_color->Y) << 10)
      | (TO_10 (p->yuv_color->U) << 20);
  a3 = TO_10 (p->yuv_color->Y) | (TO_10 (p->yuv_color->V) << 10)
      | (TO_10 (p->yuv_color->Y) << 20);

  data = p->yp + y * p->ystride;
  for (i = x / 6; i < (x + w) / 6; i++) {
    GST_WRITE_UINT32_LE (data + i * 16 + 0, a0);
    GST_WRITE_UINT32_LE (data + i * 16 + 4, a1);
    GST_WRITE_UINT32_LE (data + i * 16 + 8, a2);
    GST_WRITE_UINT32_LE (data + i * 16 + 12, a3);
  }
}

static void
paint_hline_YUY2 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int w1 = (x + w) / 2 - x1;
  int offset = y * p->ystride;

  if (x + w == p->width && p->width % 2 != 0)
    w1++;
  oil_splat_u8 (p->yp + offset + x * 2, 2, &p->yuv_color->Y, w);
  oil_splat_u8 (p->up + offset + x1 * 4, 4, &p->yuv_color->U, w1);
  oil_splat_u8 (p->vp + offset + x1 * 4, 4, &p->yuv_color->V, w1);
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
  oil_splat_u8 (p->yp + offset + x * 3, 3, &p->yuv_color->Y, w);
  oil_splat_u8 (p->up + offset + x * 3, 3, &p->yuv_color->U, w);
  oil_splat_u8 (p->vp + offset + x * 3, 3, &p->yuv_color->V, w);
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
paint_hline_Y41B (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 4;
  int w1 = (x + w) / 4 - x1;
  int offset = y * p->ystride;
  int offset1 = y * p->ustride;

  if (x + w == p->width && p->width % 4 != 0)
    w1++;
  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
  gst_orc_splat_u8 (p->up + offset1 + x1, p->yuv_color->U, w1);
  gst_orc_splat_u8 (p->vp + offset1 + x1, p->yuv_color->V, w1);
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
  int w1 = (x + w) / 2 - x1;
  int offset = y * p->ystride;
  int offset1 = y * p->ustride;

  if (x + w == p->width && p->width % 2 != 0)
    w1++;
  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
  gst_orc_splat_u8 (p->up + offset1 + x1, p->yuv_color->U, w1);
  gst_orc_splat_u8 (p->vp + offset1 + x1, p->yuv_color->V, w1);
}

static void
paint_setup_Y444 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->up = p->yp + p->ystride * p->height;
  p->vp = p->up + p->ystride * p->height;
  p->endptr = p->vp + p->ystride * p->height;
}

static void
paint_hline_Y444 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;

  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
  gst_orc_splat_u8 (p->up + offset + x, p->yuv_color->U, w);
  gst_orc_splat_u8 (p->vp + offset + x, p->yuv_color->V, w);
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

  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
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

  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
  gst_orc_splat_u8 (p->up + offset1 + x1, p->yuv_color->U, x2 - x1);
  gst_orc_splat_u8 (p->vp + offset1 + x1, p->yuv_color->V, x2 - x1);
}
#endif

static void
paint_setup_YVU9 (paintinfo * p, unsigned char *dest)
{
  int h = GST_ROUND_UP_4 (p->height);

  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->vp = p->yp + p->ystride * h;
  p->vstride = GST_ROUND_UP_4 (p->ystride / 4);
  p->up = p->vp + p->vstride * h / 4;
  p->ustride = GST_ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->up + p->ustride * h / 4;
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
  p->vp = p->up + p->ustride * h / 4;
  p->vstride = GST_ROUND_UP_4 (p->ystride / 4);
  p->endptr = p->vp + p->vstride * h / 4;
}

static void
paint_hline_YUV9 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 4;
  int w1 = (x + w) / 4 - x1;
  int offset = y * p->ystride;
  int offset1 = (y / 4) * p->ustride;

  if (x + w == p->width && p->width % 4 != 0)
    w1++;
  gst_orc_splat_u8 (p->yp + offset + x, p->yuv_color->Y, w);
  gst_orc_splat_u8 (p->up + offset1 + x1, p->yuv_color->U, w1);
  gst_orc_splat_u8 (p->vp + offset1 + x1, p->yuv_color->V, w1);
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
  guint8 alpha = 255;

  oil_splat_u8 (p->yp + offset + x * 4, 4, &p->rgb_color->R, w);
  oil_splat_u8 (p->up + offset + x * 4, 4, &p->rgb_color->G, w);
  oil_splat_u8 (p->vp + offset + x * 4, 4, &p->rgb_color->B, w);

  if (p->ap != NULL) {
    oil_splat_u8 (p->ap + offset + (x * 4), 4, &alpha, w);
  }
}

static void
paint_hline_str3 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;

  oil_splat_u8 (p->yp + offset + x * 3, 3, &p->rgb_color->R, w);
  oil_splat_u8 (p->up + offset + x * 3, 3, &p->rgb_color->G, w);
  oil_splat_u8 (p->vp + offset + x * 3, 3, &p->rgb_color->B, w);
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
  guint16 value;

  value = ((p->rgb_color->R & 0xf8) << 8) |
      ((p->rgb_color->G & 0xfc) << 3) | ((p->rgb_color->B & 0xf8) >> 3);

  gst_orc_splat_u16 (p->yp + offset + x * 2 + 0, value, w);
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
  guint8 a, b;

  a = ((p->rgb_color->R >> 1) & 0x7c) | (p->rgb_color->G >> 6);
  b = ((p->rgb_color->G << 2) & 0xe0) | (p->rgb_color->B >> 3);

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
  guint8 *dest = p->yp + offset;
  int i;

  if (y & 1) {
    for (i = x; i < x + w; i++) {
      if (i & 1) {
        dest[i] = p->rgb_color->G;
      } else {
        dest[i] = p->rgb_color->B;
      }
    }
  } else {
    for (i = x; i < x + w; i++) {
      if (i & 1) {
        dest[i] = p->rgb_color->R;
      } else {
        dest[i] = p->rgb_color->G;
      }
    }
  }
}

static void
paint_setup_GRAY8 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width);
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_hline_GRAY8 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;
  guint8 color = p->gray_color->G >> 8;

  gst_orc_splat_u8 (p->yp + offset + x, color, w);
}

static void
paint_setup_GRAY16 (paintinfo * p, unsigned char *dest)
{
  p->yp = dest;
  p->ystride = GST_ROUND_UP_4 (p->width * 2);
  p->endptr = dest + p->ystride * p->height;
}

static void
paint_hline_GRAY16 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->ystride;

  gst_orc_splat_u16 (p->yp + offset + 2 * x, p->gray_color->G, w);
}
