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

/*#define DEBUG_ENABLED */
#include <gstvideotestsrc.h>
#include <videotestsrc.h>

#include <string.h>
#include <stdlib.h>


#if 0
static void
gst_videotestsrc_setup (GstVideotestsrc * v)
{

}
#endif

static unsigned char
random_char (void)
{
  static unsigned int state;

  state *= 1103515245;
  state += 12345;
  return (state >> 16);
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

static void
memset_str2 (unsigned char *dest, unsigned char val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += 2;
  }
}

static void
memset_str3 (unsigned char *dest, unsigned char val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += 3;
  }
}

static void
memset_str4 (unsigned char *dest, unsigned char val, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = val;
    dest += 4;
  }
}

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
paint_rect (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char color)
{
  unsigned char *d = dest + stride * y + x;
  int i;

  for (i = 0; i < h; i++) {
    memset (d, color, w);
    d += stride;
  }
}
#endif

#if 0
static void
paint_rect_s2 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char col)
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
paint_rect2 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
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
paint_rect3 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
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
paint_rect4 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
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
paint_rect_s4 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char col)
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

enum {
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
	COLOR_DARK_GREY,
};

static struct vts_color_struct vts_colors[] = {
	/* 100% white */
	{ 255, 128, 128, 255, 255, 255 },
	/* yellow */
	{ 226,   0, 155, 255, 255,   0 },
	/* cyan */
	{ 179, 170,   0,   0, 255, 255 },
	/* green */
	{ 150,  46,  21,   0, 255,   0 },
	/* magenta */
	{ 105, 212, 235, 255,   0, 255 },
	/* red */
	{  76,  85, 255, 255,   0,   0 },
	/* blue */
	{  29, 255, 107,   0,   0, 255 },
	/* black */
	{  16, 128, 128,   0,   0,   0 },
	/* -I */
	{  16, 198,  21,   0,   0, 128 },
	/* +Q */
	{  16, 235, 198,   0, 128, 255 },
	/* superblack */
	{   0, 128, 128,   0,   0,   0 },
	/* 5% grey */
	{  32, 128, 128,  32,  32,  32 },
};


#if 0

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q, superblack, dark grey */
static int y_colors[] = { 255, 226, 179, 150, 105,  76,  29,  16,  16,  16,   0, 32 };
static int u_colors[] = { 128, 0,   170, 46,  212,  85, 255, 128, 198, 235, 128, 128 };
static int v_colors[] = { 128, 155, 0,   21,  235, 255, 107, 128,  21, 198, 128, 128 };

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q  superblack, dark grey */
static int r_colors[] = { 255, 255,   0,   0, 255, 255,   0,   0,   0,   0,   0, 32 };
static int g_colors[] = { 255, 255, 255, 255,   0,   0,   0,   0,   0, 128,   0, 32 };
static int b_colors[] = { 255,   0, 255,   0, 255,   0, 255,   0, 128, 255,   0, 32 };
#endif


static void paint_setup_I420 (paintinfo * p, char *dest);
static void paint_setup_YV12 (paintinfo * p, char *dest);
static void paint_setup_YUY2 (paintinfo * p, char *dest);
static void paint_setup_UYVY (paintinfo * p, char *dest);
static void paint_setup_YVYU (paintinfo * p, char *dest);
static void paint_setup_IYU2 (paintinfo * p, char *dest);
static void paint_setup_Y800 (paintinfo * p, char *dest);
static void paint_setup_IMC1 (paintinfo * p, char *dest);
static void paint_setup_IMC2 (paintinfo * p, char *dest);
static void paint_setup_IMC3 (paintinfo * p, char *dest);
static void paint_setup_IMC4 (paintinfo * p, char *dest);
static void paint_setup_YUV9 (paintinfo * p, char *dest);
static void paint_setup_YVU9 (paintinfo * p, char *dest);
static void paint_setup_xRGB8888 (paintinfo * p, char *dest);
static void paint_setup_xBGR8888 (paintinfo * p, char *dest);
static void paint_setup_RGBx8888 (paintinfo * p, char *dest);
static void paint_setup_BGRx8888 (paintinfo * p, char *dest);
static void paint_setup_RGB888 (paintinfo * p, char *dest);
static void paint_setup_BGR888 (paintinfo * p, char *dest);
static void paint_setup_RGB565 (paintinfo * p, char *dest);
static void paint_setup_xRGB1555 (paintinfo * p, char *dest);

static void paint_hline_I420 (paintinfo * p, int x, int y, int w);
static void paint_hline_YUY2 (paintinfo * p, int x, int y, int w);
static void paint_hline_IYU2 (paintinfo * p, int x, int y, int w);
static void paint_hline_Y800 (paintinfo * p, int x, int y, int w);
static void paint_hline_IMC1 (paintinfo * p, int x, int y, int w);
static void paint_hline_YUV9 (paintinfo * p, int x, int y, int w);
static void paint_hline_str4 (paintinfo * p, int x, int y, int w);
static void paint_hline_str3 (paintinfo * p, int x, int y, int w);
static void paint_hline_RGB565 (paintinfo * p, int x, int y, int w);
static void paint_hline_xRGB1555 (paintinfo * p, int x, int y, int w);

struct fourcc_list_struct fourcc_list[] = {
/* packed */
  {"YUY2", "YUY2", 16, paint_setup_YUY2, paint_hline_YUY2},
  {"UYVY", "UYVY", 16, paint_setup_UYVY, paint_hline_YUY2},
  {"Y422", "Y422", 16, paint_setup_UYVY, paint_hline_YUY2},
  {"UYNV", "UYNV", 16, paint_setup_UYVY, paint_hline_YUY2}, /* FIXME: UYNV? */
  {"YVYU", "YVYU", 16, paint_setup_YVYU, paint_hline_YUY2},

  /* interlaced */
  /*{ "IUYV", "IUY2", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* inverted */
  /*{ "cyuv", "cyuv", 16, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y41P", "Y41P", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /* interlaced */
  /*{ "IY41", "IY41", 12, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y211", "Y211", 8, paint_setup_YVYU, paint_hline_YUY2 }, */

  /*{ "Y41T", "Y41T", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "Y42P", "Y42P", 16, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "CLJR", "CLJR", 8, paint_setup_YVYU, paint_hline_YUY2 }, */
  /*{ "IYU1", "IYU1", 12, paint_setup_YVYU, paint_hline_YUY2 }, */
  { "IYU2", "IYU2", 24, paint_setup_IYU2, paint_hline_IYU2 },

/* planar */
  /* YVU9 */
  {"YVU9", "YVU9", 9, paint_setup_YVU9, paint_hline_YUV9},
  /* YUV9 */
  {"YUV9", "YUV9", 9, paint_setup_YUV9, paint_hline_YUV9},
  /* IF09 */
  /* YV12 */
  {"YV12", "YV12", 12, paint_setup_YV12, paint_hline_I420},
  /* I420 */
  {"I420", "I420", 12, paint_setup_I420, paint_hline_I420},
  /* NV12 */
  /* NV21 */
  /* IMC1 */
  {"IMC1", "IMC1", 16, paint_setup_IMC1, paint_hline_IMC1},
  /* IMC2 */
  {"IMC2", "IMC2", 12, paint_setup_IMC2, paint_hline_IMC1},
  /* IMC3 */
  {"IMC3", "IMC3", 16, paint_setup_IMC3, paint_hline_IMC1},
  /* IMC4 */
  {"IMC4", "IMC4", 12, paint_setup_IMC4, paint_hline_IMC1},
  /* CLPL */
  /* Y41B */
  /* Y42B */
  /* Y800 grayscale */
  {"Y800", "Y800", 8, paint_setup_Y800, paint_hline_Y800},

  {"RGB ", "xRGB8888", 32, paint_setup_xRGB8888, paint_hline_str4, 1, 24,
  		0x00ff0000, 0x0000ff00, 0x000000ff },
  {"RGB ", "xBGR8888", 32, paint_setup_xBGR8888, paint_hline_str4, 1, 24,
		0x000000ff, 0x0000ff00, 0x00ff0000 },
  {"RGB ", "RGBx8888", 32, paint_setup_RGBx8888, paint_hline_str4, 1, 24,
  		0xff000000, 0x00ff0000, 0x0000ff00 },
  {"RGB ", "BGRx8888", 32, paint_setup_BGRx8888, paint_hline_str4, 1, 24,
		0x0000ff00, 0x00ff0000, 0xff000000 },
  {"RGB ", "RGB888", 24, paint_setup_RGB888, paint_hline_str3, 1, 24,
  		0x00ff0000, 0x0000ff00, 0x000000ff },
  {"RGB ", "BGR888", 24, paint_setup_BGR888, paint_hline_str3, 1, 24,
  		0x000000ff, 0x0000ff00, 0x00ff0000 },
  {"RGB ", "RGB565", 16, paint_setup_RGB565, paint_hline_RGB565, 1, 16,
  		0x0000f800, 0x000007e0, 0x0000001f },
  {"RGB ", "xRGB1555", 16, paint_setup_xRGB1555, paint_hline_xRGB1555, 1, 15,
  		0x00007c00, 0x000003e0, 0x0000001f },
};
int n_fourccs = sizeof (fourcc_list) / sizeof (fourcc_list[0]);

struct fourcc_list_struct *paintinfo_find_by_structure(const GstStructure *structure)
{
  int i;
  const char *media_type = gst_structure_get_name(structure);
  int ret;

  g_return_val_if_fail (structure, NULL);

  if(strcmp(media_type, "video/x-raw-yuv")==0){
    char *s;
    int fourcc;
    guint32 format;

    ret = gst_structure_get_fourcc (structure, "format", &format);
    if (!ret) return NULL;
    for (i = 0; i < n_fourccs; i++) {
      s = fourcc_list[i].fourcc;
      //g_print("testing " GST_FOURCC_FORMAT " and %s\n", GST_FOURCC_ARGS(format), s);
      fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
      if(fourcc == format){
        return fourcc_list + i;
      }
    }
  }else if(strcmp(media_type, "video/x-raw-rgb")==0){
    int red_mask;
    int green_mask;
    int blue_mask;
    int depth;
    int bpp;

    ret = gst_structure_get_int (structure, "red_mask", &red_mask);
    ret &= gst_structure_get_int (structure, "green_mask", &green_mask);
    ret &= gst_structure_get_int (structure, "blue_mask", &blue_mask);
    ret &= gst_structure_get_int (structure, "depth", &depth);
    ret &= gst_structure_get_int (structure, "bpp", &bpp);

    for (i = 0; i < n_fourccs; i++) {
      if (strcmp(fourcc_list[i].fourcc, "RGB ") == 0 &&
	  fourcc_list[i].red_mask == red_mask &&
	  fourcc_list[i].green_mask == green_mask &&
	  fourcc_list[i].blue_mask == blue_mask &&
	  fourcc_list[i].depth == depth &&
	  fourcc_list[i].bitspp == bpp){
	return fourcc_list + i;

      }
    }
    return NULL;
  }

  g_critical("format not found for media type %s", media_type);

  return NULL;
}

struct fourcc_list_struct * paintrect_find_fourcc (int find_fourcc)
{
  int i;

  for (i = 0; i < n_fourccs; i++) {
    char *s;
    int fourcc;

    s = fourcc_list[i].fourcc;
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    if (find_fourcc == fourcc) {
      /* If YUV format, it's good */
      if(!fourcc_list[i].ext_caps){
        return fourcc_list + i;
      }

      return fourcc_list + i;
    }
  }
  return NULL;
}

struct fourcc_list_struct * paintrect_find_name (const char *name)
{
  int i;

  for (i = 0; i < n_fourccs; i++) {
    if(strcmp(name,fourcc_list[i].name)==0){
      return fourcc_list + i;
    }
  }
  return NULL;
}


GstStructure *paint_get_structure(struct fourcc_list_struct *format)
{
  unsigned int fourcc;

  g_return_val_if_fail(format, NULL);

  fourcc = GST_MAKE_FOURCC (format->fourcc[0], format->fourcc[1], format->fourcc[2], format->fourcc[3]);

  if(format->ext_caps){
    int endianness;

    if(format->bitspp==16){
      endianness = G_BYTE_ORDER;
    }else{
      endianness = G_BIG_ENDIAN;
    }
    return gst_structure_new ("video/x-raw-rgb",
	"bpp", G_TYPE_INT, format->bitspp,
	"endianness", G_TYPE_INT, endianness,
	"depth", G_TYPE_INT, format->depth,
	"red_mask", G_TYPE_INT, format->red_mask,
	"green_mask", G_TYPE_INT, format->green_mask,
	"blue_mask", G_TYPE_INT, format->blue_mask,
        NULL);
  }else{
    return gst_structure_new ("video/x-raw-yuv",
	"format", GST_TYPE_FOURCC, fourcc,
        NULL);
  }
}

void
gst_videotestsrc_smpte (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int i;
  int y1, y2;
  int j;
  paintinfo pi;
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
    int x1 = w/2 + i * w / 12;
    int x2 = w/2 + (i + 1) * w / 12;
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
    int x1 = w*3 / 4;
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
gst_videotestsrc_snow (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int i;
  int j;
  paintinfo pi;
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

void
gst_videotestsrc_black (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int i;
  paintinfo pi;
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  p->color = vts_colors + COLOR_BLACK;

  for (i = 0; i < h; i++) {
    p->paint_hline (p, 0, i, w);
  }
}

static void
paint_setup_I420 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height;
  p->vp = dest + p->width * p->height + p->width * p->height / 4;
}

static void
paint_hline_I420 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset = y * p->width;
  int offset1 = (y / 2) * (p->width / 2);

  memset (p->yp + offset + x, p->color->Y, w);
  memset (p->up + offset1 + x1, p->color->U, x2 - x1);
  memset (p->vp + offset1 + x1, p->color->V, x2 - x1);
}

static void
paint_setup_YV12 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height + p->width * p->height / 4;
  p->vp = dest + p->width * p->height;
}

static void
paint_setup_YUY2 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + 1;
  p->vp = dest + 3;
}

static void
paint_setup_UYVY (paintinfo * p, char *dest)
{
  p->yp = dest + 1;
  p->up = dest;
  p->vp = dest + 2;
}

static void
paint_setup_YVYU (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + 3;
  p->vp = dest + 1;
}

static void
paint_hline_YUY2 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 2;
  int x2 = (x + w) / 2;
  int offset;

  offset = y * p->width * 2;
  memset_str2 (p->yp + offset + x * 2, p->color->Y, w);
  memset_str4 (p->up + offset + x1 * 4, p->color->U, x2 - x1);
  memset_str4 (p->vp + offset + x1 * 4, p->color->V, x2 - x1);
}

static void
paint_setup_IYU2 (paintinfo * p, char *dest)
{
  p->yp = dest + 1;
  p->up = dest + 0;
  p->vp = dest + 2;
}

static void
paint_hline_IYU2 (paintinfo * p, int x, int y, int w)
{
  int offset;

  offset = y * p->width * 3;
  memset_str3 (p->yp + offset + x * 3, p->color->Y, w);
  memset_str3 (p->up + offset + x * 3, p->color->U, w);
  memset_str3 (p->vp + offset + x * 3, p->color->V, w);
}

static void
paint_setup_Y800 (paintinfo * p, char *dest)
{
  p->yp = dest;
}

static void
paint_hline_Y800 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->width;

  memset (p->yp + offset + x, p->color->Y, w);
}

static void
paint_setup_IMC1 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height;
  p->vp = dest + p->width * p->height + p->width * p->height / 2;
}

static void
paint_setup_IMC2 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->vp = dest + p->width * p->height;
  p->up = dest + p->width * p->height + p->width / 2;
}

static void
paint_setup_IMC3 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height + p->width * p->height / 2;
  p->vp = dest + p->width * p->height;
}

static void
paint_setup_IMC4 (paintinfo * p, char *dest)
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

  memset (p->yp + offset + x, p->color->Y, w);
  memset (p->up + offset1 + x1, p->color->U, x2 - x1);
  memset (p->vp + offset1 + x1, p->color->V, x2 - x1);
}

static void
paint_setup_YVU9 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->vp = dest + p->width * p->height;
  p->up = dest + p->width * p->height + (p->width/4) * (p->height/4);
}

static void
paint_setup_YUV9 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->up = dest + p->width * p->height;
  p->vp = dest + p->width * p->height + (p->width/4) * (p->height/4);
}

static void
paint_hline_YUV9 (paintinfo * p, int x, int y, int w)
{
  int x1 = x / 4;
  int x2 = (x + w) / 4;
  int offset = y * p->width;
  int offset1 = (y / 4) * (p->width / 4);

  memset (p->yp + offset + x, p->color->Y, w);
  memset (p->up + offset1 + x1, p->color->U, x2 - x1);
  memset (p->vp + offset1 + x1, p->color->V, x2 - x1);
}

static void
paint_setup_xRGB8888 (paintinfo * p, char *dest)
{
  p->yp = dest + 1;
  p->up = dest + 2;
  p->vp = dest + 3;
}

static void
paint_setup_xBGR8888 (paintinfo * p, char *dest)
{
  p->yp = dest + 3;
  p->up = dest + 2;
  p->vp = dest + 1;
}

static void
paint_setup_RGBx8888 (paintinfo * p, char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 1;
  p->vp = dest + 2;
}

static void
paint_setup_BGRx8888 (paintinfo * p, char *dest)
{
  p->yp = dest + 2;
  p->up = dest + 1;
  p->vp = dest + 0;
}

static void
paint_setup_RGB888 (paintinfo * p, char *dest)
{
  p->yp = dest + 0;
  p->up = dest + 1;
  p->vp = dest + 2;
  p->stride = (p->width*3 + 1)&(~0x3);
}

static void
paint_setup_BGR888 (paintinfo * p, char *dest)
{
  p->yp = dest + 2;
  p->up = dest + 1;
  p->vp = dest + 0;
  p->stride = (p->width*3 + 1)&(~0x3);
}

static void
paint_hline_str4 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->width * 4;

  memset_str4 (p->yp + offset + x * 4, p->color->R, w);
  memset_str4 (p->up + offset + x * 4, p->color->G, w);
  memset_str4 (p->vp + offset + x * 4, p->color->B, w);
}

static void
paint_hline_str3 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->stride;

  memset_str3 (p->yp + offset + x * 3, p->color->R, w);
  memset_str3 (p->up + offset + x * 3, p->color->G, w);
  memset_str3 (p->vp + offset + x * 3, p->color->B, w);
}

static void
paint_setup_RGB565 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->stride = (p->width*2 + 1)&(~0x3);
}

static void
paint_hline_RGB565 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->stride;
  unsigned int a,b;

  a = (p->color->R&0xf8) | (p->color->G>>5);
  b = ((p->color->G<<3)&0xe0) | (p->color->B>>3);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  memset_str2 (p->yp + offset + x * 2 + 0, b, w);
  memset_str2 (p->yp + offset + x * 2 + 1, a, w);
#else
  memset_str2 (p->yp + offset + x * 2 + 0, a, w);
  memset_str2 (p->yp + offset + x * 2 + 1, b, w);
#endif
}

static void
paint_setup_xRGB1555 (paintinfo * p, char *dest)
{
  p->yp = dest;
  p->stride = (p->width*2 + 1)&(~0x3);
}

static void
paint_hline_xRGB1555 (paintinfo * p, int x, int y, int w)
{
  int offset = y * p->stride;
  unsigned int a,b;

  a = ((p->color->R>>1)&0x7c) | (p->color->G>>6);
  b = ((p->color->G<<2)&0xe0) | (p->color->B>>3);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  memset_str2 (p->yp + offset + x * 2 + 0, b, w);
  memset_str2 (p->yp + offset + x * 2 + 1, a, w);
#else
  memset_str2 (p->yp + offset + x * 2 + 0, a, w);
  memset_str2 (p->yp + offset + x * 2 + 1, b, w);
#endif
}

#if 0
#ifdef unused
static void
gst_videotestsrc_smpte_RGB (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int i;
  int y1, y2;

  y1 = h * 2 / 3;
  y2 = h * 0.75;

  /* color bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    unsigned char col[2];

    col[0] = (g_colors[i] & 0xe0) | (b_colors[i] >> 3);
    col[1] = (r_colors[i] & 0xf8) | (g_colors[i] >> 5);
    paint_rect2 (dest, w * 2, x1, 0, x2 - x1, y1, col);
  }

  /* inverse blue bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    unsigned char col[2];
    int k;

    if (i & 1) {
      k = 7;
    } else {
      k = 6 - i;
    }
    col[0] = (g_colors[k] & 0xe0) | (b_colors[k] >> 3);
    col[1] = (r_colors[k] & 0xf8) | (g_colors[k] >> 5);
    paint_rect2 (dest, w * 2, x1, y1, x2 - x1, y2 - y1, col);
  }

  /* -I, white, Q regions */
  for (i = 0; i < 3; i++) {
    int x1 = i * w / 6;
    int x2 = (i + 1) * w / 6;
    unsigned char col[2];
    int k;

    if (i == 0) {
      k = 8;
    } else if (i == 1) {
      k = 0;
    } else {
      k = 9;
    }

    col[0] = (g_colors[k] & 0xe0) | (b_colors[k] >> 3);
    col[1] = (r_colors[k] & 0xf8) | (g_colors[k] >> 5);
    paint_rect2 (dest, w * 2, x1, y2, x2 - x1, h - y2, col);
  }

  {
    int x1 = w / 2;
    int x2 = w - 1;

    paint_rect_random (dest, w * 2, x1 * 2, y2, (x2 - x1) * 2, h - y2);
  }
}
#endif

static void
gst_videotestsrc_smpte_RGB (GstVideotestsrc * v, unsigned char *dest, int w, int h)
{
  int i;
  int y1, y2;

  y1 = h * 2 / 3;
  y2 = h * 0.75;

  /* color bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    unsigned char col[2];

    col[0] = 0;
    col[1] = r_colors[i];
    col[2] = g_colors[i];
    col[3] = b_colors[i];
    paint_rect4 (dest, w * 4, x1, 0, x2 - x1, y1, col);
  }

  /* inverse blue bars */
  for (i = 0; i < 7; i++) {
    int x1 = i * w / 7;
    int x2 = (i + 1) * w / 7;
    unsigned char col[2];
    int k;

    if (i & 1) {
      k = 7;
    } else {
      k = 6 - i;
    }
    col[0] = 0;
    col[1] = r_colors[k];
    col[2] = g_colors[k];
    col[3] = b_colors[k];
    paint_rect4 (dest, w * 4, x1, y1, x2 - x1, y2 - y1, col);
  }

  /* -I, white, Q regions */
  for (i = 0; i < 3; i++) {
    int x1 = i * w / 6;
    int x2 = (i + 1) * w / 6;
    unsigned char col[2];
    int k;

    if (i == 0) {
      k = 8;
    } else if (i == 1) {
      k = 0;
    } else {
      k = 9;
    }

    col[0] = 0;
    col[1] = r_colors[k];
    col[2] = g_colors[k];
    col[3] = b_colors[k];
    paint_rect4 (dest, w * 4, x1, y2, x2 - x1, h - y2, col);
  }

  {
    int x1 = w / 2;
    int x2 = w - 1;

    paint_rect_random (dest, w * 4, x1 * 4, y2, (x2 - x1) * 4, h - y2);
  }
}
#endif

