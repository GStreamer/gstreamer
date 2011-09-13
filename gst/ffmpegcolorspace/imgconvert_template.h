/*
 * Templates for image conversion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef RGB_OUT
#define RGB_OUT(d, r, g, b) RGBA_OUT(d, r, g, b, 0xffU)
#endif

static void glue (uyvy422_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
    uint8_t *s, *d, *d1, *s1;
    int w, y, cb, cr, r_add, g_add, b_add;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    s = src->data[0];
    for(;height > 0; height --) {
        d1 = d;
        s1 = s;
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1_CCIR(s1[0], s1[2]);

            YUV_TO_RGB2_CCIR(r, g, b, s1[1]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            YUV_TO_RGB2_CCIR(r, g, b, s1[3]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            s1 += 4;
        }

        if (w) {
            YUV_TO_RGB1_CCIR(s1[0], s1[2]);

            YUV_TO_RGB2_CCIR(r, g, b, s1[1]);
            RGB_OUT(d1, r, g, b);
        }

        d += dst->linesize[0];
        s += src->linesize[0];
    }
}

static void glue (yuv422_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
    uint8_t *s, *d, *d1, *s1;
    int w, y, cb, cr, r_add, g_add, b_add;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    s = src->data[0];
    for(;height > 0; height --) {
        d1 = d;
        s1 = s;
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1_CCIR(s1[1], s1[3]);

            YUV_TO_RGB2_CCIR(r, g, b, s1[0]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            YUV_TO_RGB2_CCIR(r, g, b, s1[2]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            s1 += 4;
        }

        if (w) {
            YUV_TO_RGB1_CCIR(s1[1], s1[3]);

            YUV_TO_RGB2_CCIR(r, g, b, s1[0]);
            RGB_OUT(d1, r, g, b);
        }

        d += dst->linesize[0];
        s += src->linesize[0];
    }
}

static void glue (yvyu422_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
    uint8_t *s, *d, *d1, *s1;
    int w, y, cb, cr, r_add, g_add, b_add;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;
    unsigned int r, g, b;

    d = dst->data[0];
    s = src->data[0];
    for(;height > 0; height --) {
        d1 = d;
        s1 = s;
        for(w = width; w >= 2; w -= 2) {
            YUV_TO_RGB1_CCIR(s1[3], s1[1]);

            YUV_TO_RGB2_CCIR(r, g, b, s1[0]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            YUV_TO_RGB2_CCIR(r, g, b, s1[2]);
            RGB_OUT(d1, r, g, b);
            d1 += BPP;

            s1 += 4;
        }

        if (w) {
            YUV_TO_RGB1_CCIR(s1[3], s1[1]);

            YUV_TO_RGB2_CCIR(r, g, b, s1[0]);
            RGB_OUT(d1, r, g, b);
        }

        d += dst->linesize[0];
        s += src->linesize[0];
    }
}

static void glue (yuv420p_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
  const uint8_t *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr;
  uint8_t *d, *d1, *d2;
  int w, y, cb, cr, r_add, g_add, b_add, width2;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;

  d = dst->data[0];
  y1_ptr = src->data[0];
  cb_ptr = src->data[1];
  cr_ptr = src->data[2];
  width2 = (width + 1) >> 1;
  for (; height >= 2; height -= 2) {
    d1 = d;
    d2 = d + dst->linesize[0];
    y2_ptr = y1_ptr + src->linesize[0];
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      /* output 4 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[1]);
      RGB_OUT (d2 + BPP, r, g, b);

      d1 += 2 * BPP;
      d2 += 2 * BPP;

      y1_ptr += 2;
      y2_ptr += 2;
      cb_ptr++;
      cr_ptr++;
    }
    /* handle odd width */
    if (w) {
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);
      d1 += BPP;
      d2 += BPP;
      y1_ptr++;
      y2_ptr++;
      cb_ptr++;
      cr_ptr++;
    }
    d += 2 * dst->linesize[0];
    y1_ptr += 2 * src->linesize[0] - width;
    cb_ptr += src->linesize[1] - width2;
    cr_ptr += src->linesize[2] - width2;
  }
  /* handle odd height */
  if (height) {
    d1 = d;
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      /* output 2 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      d1 += 2 * BPP;

      y1_ptr += 2;
      cb_ptr++;
      cr_ptr++;
    }
    /* handle width */
    if (w) {
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      /* output 2 pixel */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;

      y1_ptr++;
      cb_ptr++;
      cr_ptr++;
    }
  }
}

#ifndef RGBA_OUT
#define RGBA_OUT_(d, r, g, b, a) RGB_OUT(d, r, g, b)
#define YUVA_TO_A(d, a)
#else
#define RGBA_OUT_(d, r, g, b, a) RGBA_OUT(d, r, g, b, a)
#define YUVA_TO_A(d, a) do { d = a; } while (0);
#endif

static void glue (yuva420p_to_, RGB_NAME)(AVPicture *dst, const AVPicture *src,
                                        int width, int height)
{
  const uint8_t *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr, *a1_ptr, *a2_ptr;
  uint8_t *d, *d1, *d2;
  int w, y, cb, cr, r_add, g_add, b_add, width2;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;
#ifdef RGBA_OUT
  unsigned int a = 0;
#endif

  d = dst->data[0];
  y1_ptr = src->data[0];
  cb_ptr = src->data[1];
  cr_ptr = src->data[2];
  a1_ptr = src->data[3];
  width2 = (width + 1) >> 1;
  for (; height >= 2; height -= 2) {
    d1 = d;
    d2 = d + dst->linesize[0];
    y2_ptr = y1_ptr + src->linesize[0];
    a2_ptr = a1_ptr + src->linesize[3];
    for (w = width; w >= 2; w -= 2) {
      YUVA_TO_A (a, a1_ptr[0]);
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      /* output 4 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGBA_OUT_ (d1, r, g, b, a);

      YUVA_TO_A (a, a1_ptr[1]);
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGBA_OUT_ (d1 + BPP, r, g, b, a);

      YUVA_TO_A (a, a2_ptr[0]);
      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGBA_OUT_ (d2, r, g, b, a);

      YUVA_TO_A (a, a2_ptr[1]);
      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[1]);
      RGBA_OUT_ (d2 + BPP, r, g, b, a);

      d1 += 2 * BPP;
      d2 += 2 * BPP;

      y1_ptr += 2;
      y2_ptr += 2;
      cb_ptr++;
      cr_ptr++;
      a1_ptr += 2;
      a2_ptr += 2;
    }
    /* handle odd width */
    if (w) {
      YUVA_TO_A (a, a1_ptr[0]);
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGBA_OUT_ (d1, r, g, b, a);

      YUVA_TO_A (a, a2_ptr[0]);
      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGBA_OUT_ (d2, r, g, b, a);
      d1 += BPP;
      d2 += BPP;
      y1_ptr++;
      y2_ptr++;
      cb_ptr++;
      cr_ptr++;
      a1_ptr++;
      a2_ptr++;
    }
    d += 2 * dst->linesize[0];
    y1_ptr += 2 * src->linesize[0] - width;
    cb_ptr += src->linesize[1] - width2;
    cr_ptr += src->linesize[2] - width2;
    a1_ptr += 2 * src->linesize[3] - width;
  }
  /* handle odd height */
  if (height) {
    d1 = d;
    for (w = width; w >= 2; w -= 2) {
      YUVA_TO_A (a, a1_ptr[0]);
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      /* output 2 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGBA_OUT_ (d1, r, g, b, a);

      YUVA_TO_A (a, a1_ptr[1]);
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGBA_OUT_ (d1 + BPP, r, g, b, a);

      d1 += 2 * BPP;

      y1_ptr += 2;
      cb_ptr++;
      cr_ptr++;
      a1_ptr += 2;
    }
    /* handle width */
    if (w) {
      YUVA_TO_A (a, a1_ptr[0]);
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);
      /* output 2 pixel */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGBA_OUT_ (d1, r, g, b, a);
      d1 += BPP;

      y1_ptr++;
      cb_ptr++;
      cr_ptr++;
      a1_ptr++;
    }
  }
}

static void glue (nv12_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *y1_ptr, *y2_ptr, *c_ptr;
  uint8_t *d, *d1, *d2;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;
  int c_wrap = src->linesize[1] - ((width + 1) & ~0x01);

  d = dst->data[0];
  y1_ptr = src->data[0];
  c_ptr = src->data[1];
  for (; height >= 2; height -= 2) {
    d1 = d;
    d2 = d + dst->linesize[0];
    y2_ptr = y1_ptr + src->linesize[0];
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1_CCIR (c_ptr[0], c_ptr[1]);
      /* output 4 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[1]);
      RGB_OUT (d2 + BPP, r, g, b);

      d1 += 2 * BPP;
      d2 += 2 * BPP;

      y1_ptr += 2;
      y2_ptr += 2;
      c_ptr += 2;
    }
    /* handle odd width */
    if (w) {
      YUV_TO_RGB1_CCIR (c_ptr[0], c_ptr[1]);
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);
      d1 += BPP;
      d2 += BPP;
      y1_ptr++;
      y2_ptr++;
      c_ptr += 2;
    }
    d += 2 * dst->linesize[0];
    y1_ptr += 2 * src->linesize[0] - width;
    c_ptr += c_wrap;
  }
  /* handle odd height */
  if (height) {
    d1 = d;
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1_CCIR (c_ptr[0], c_ptr[1]);
      /* output 2 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      d1 += 2 * BPP;

      y1_ptr += 2;
      c_ptr += 2;
    }
    /* handle odd width */
    if (w) {
      YUV_TO_RGB1_CCIR (c_ptr[0], c_ptr[1]);
      /* output 1 pixel */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;

      y1_ptr++;
      c_ptr += 2;
    }
  }
}

static void glue (nv21_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *y1_ptr, *y2_ptr, *c_ptr;
  uint8_t *d, *d1, *d2;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;
  int c_wrap = src->linesize[1] - ((width + 1) & ~0x01);

  d = dst->data[0];
  y1_ptr = src->data[0];
  c_ptr = src->data[1];
  for (; height >= 2; height -= 2) {
    d1 = d;
    d2 = d + dst->linesize[0];
    y2_ptr = y1_ptr + src->linesize[0];
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1_CCIR (c_ptr[1], c_ptr[0]);
      /* output 4 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[1]);
      RGB_OUT (d2 + BPP, r, g, b);

      d1 += 2 * BPP;
      d2 += 2 * BPP;

      y1_ptr += 2;
      y2_ptr += 2;
      c_ptr += 2;
    }
    /* handle odd width */
    if (w) {
      YUV_TO_RGB1_CCIR (c_ptr[1], c_ptr[0]);
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);
      d1 += BPP;
      d2 += BPP;
      y1_ptr++;
      y2_ptr++;
      c_ptr += 2;
    }
    d += 2 * dst->linesize[0];
    y1_ptr += 2 * src->linesize[0] - width;
    c_ptr += c_wrap;
  }
  /* handle odd height */
  if (height) {
    d1 = d;
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1_CCIR (c_ptr[1], c_ptr[0]);
      /* output 2 pixels */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      d1 += 2 * BPP;

      y1_ptr += 2;
      c_ptr += 2;
    }
    /* handle odd width */
    if (w) {
      YUV_TO_RGB1_CCIR (c_ptr[1], c_ptr[0]);
      /* output 1 pixel */
      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;

      y1_ptr++;
      c_ptr += 2;
    }
  }
}

static void glue (yuvj420p_to_, RGB_NAME) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const uint8_t *y1_ptr, *y2_ptr, *cb_ptr, *cr_ptr;
  uint8_t *d, *d1, *d2;
  int w, y, cb, cr, r_add, g_add, b_add, width2;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;

  d = dst->data[0];
  y1_ptr = src->data[0];
  cb_ptr = src->data[1];
  cr_ptr = src->data[2];
  width2 = (width + 1) >> 1;
  for (; height >= 2; height -= 2) {
    d1 = d;
    d2 = d + dst->linesize[0];
    y2_ptr = y1_ptr + src->linesize[0];
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1 (cb_ptr[0], cr_ptr[0]);
      /* output 4 pixels */
      YUV_TO_RGB2 (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2 (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      YUV_TO_RGB2 (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);

      YUV_TO_RGB2 (r, g, b, y2_ptr[1]);
      RGB_OUT (d2 + BPP, r, g, b);

      d1 += 2 * BPP;
      d2 += 2 * BPP;

      y1_ptr += 2;
      y2_ptr += 2;
      cb_ptr++;
      cr_ptr++;
    }
    /* handle odd width */
    if (w) {
      YUV_TO_RGB1 (cb_ptr[0], cr_ptr[0]);
      YUV_TO_RGB2 (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2 (r, g, b, y2_ptr[0]);
      RGB_OUT (d2, r, g, b);
      d1 += BPP;
      d2 += BPP;
      y1_ptr++;
      y2_ptr++;
      cb_ptr++;
      cr_ptr++;
    }
    d += 2 * dst->linesize[0];
    y1_ptr += 2 * src->linesize[0] - width;
    cb_ptr += src->linesize[1] - width2;
    cr_ptr += src->linesize[2] - width2;
  }
  /* handle odd height */
  if (height) {
    d1 = d;
    for (w = width; w >= 2; w -= 2) {
      YUV_TO_RGB1 (cb_ptr[0], cr_ptr[0]);
      /* output 2 pixels */
      YUV_TO_RGB2 (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);

      YUV_TO_RGB2 (r, g, b, y1_ptr[1]);
      RGB_OUT (d1 + BPP, r, g, b);

      d1 += 2 * BPP;

      y1_ptr += 2;
      cb_ptr++;
      cr_ptr++;
    }
    /* handle width */
    if (w) {
      YUV_TO_RGB1 (cb_ptr[0], cr_ptr[0]);
      /* output 2 pixels */
      YUV_TO_RGB2 (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;

      y1_ptr++;
      cb_ptr++;
      cr_ptr++;
    }
  }
}

static void glue (y800_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  int r, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - BPP * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      r = Y_CCIR_TO_JPEG (p[0]);
      RGB_OUT (q, r, r, r);
      q += BPP;
      p++;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (y16_to_, RGB_NAME) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  int r, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - BPP * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      r = Y_CCIR_TO_JPEG (GST_READ_UINT16_LE (p) >> 8);
      RGB_OUT (q, r, r, r);
      q += BPP;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_yuv420p) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  int wrap, wrap3, width2;
  int r, g, b, r1, g1, b1, w;
  uint8_t *lum, *cb, *cr;
  const uint8_t *p;

  lum = dst->data[0];
  cb = dst->data[1];
  cr = dst->data[2];

  width2 = (width + 1) >> 1;
  wrap = dst->linesize[0];
  wrap3 = src->linesize[0];
  p = src->data[0];
  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      p += wrap3;
      lum += wrap;

      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);

      cb[0] = RGB_TO_U_CCIR (r1, g1, b1, 2);
      cr[0] = RGB_TO_V_CCIR (r1, g1, b1, 2);


      cb++;
      cr++;
      p += -wrap3 + 2 * BPP;
      lum += -wrap + 2;
    }
    if (w) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      p += wrap3;
      lum += wrap;
      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      cb[0] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      cr[0] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      cb++;
      cr++;
      p += -wrap3 + BPP;
      lum += -wrap + 1;
    }
    p += wrap3 + (wrap3 - width * BPP);
    lum += wrap + (wrap - width);
    cb += dst->linesize[1] - width2;
    cr += dst->linesize[2] - width2;
  }
  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      cb[0] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      cr[0] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      cb++;
      cr++;
      p += 2 * BPP;
      lum += 2;
    }
    if (w) {
      RGB_IN (r, g, b, p);
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      cb[0] = RGB_TO_U_CCIR (r, g, b, 0);
      cr[0] = RGB_TO_V_CCIR (r, g, b, 0);
    }
  }
}

#ifndef RGBA_IN
#define RGBA_IN_(r, g, b, a, p) RGB_IN(r, g, b, p)
#else
#define RGBA_IN_(r, g, b, a, p) RGBA_IN(r, g, b, a, p)
#endif

static void glue (RGB_NAME, _to_yuva420p) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  int wrap, wrap3, width2;
  int r, g, b, r1, g1, b1, w, ra = 255;
  uint8_t *lum, *cb, *cr, *a;
  const uint8_t *p;

  lum = dst->data[0];
  cb = dst->data[1];
  cr = dst->data[2];
  a = dst->data[3];

  width2 = (width + 1) >> 1;
  wrap = dst->linesize[0];
  wrap3 = src->linesize[0];
  p = src->data[0];
  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      RGBA_IN_ (r, g, b, ra, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      a[0] = ra;

      RGBA_IN_ (r, g, b, ra, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      a[1] = ra;
      p += wrap3;
      lum += wrap;
      a += wrap;

      RGBA_IN_ (r, g, b, ra, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      a[0] = ra;

      RGBA_IN_ (r, g, b, ra, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      a[1] = ra;

      cb[0] = RGB_TO_U_CCIR (r1, g1, b1, 2);
      cr[0] = RGB_TO_V_CCIR (r1, g1, b1, 2);

      cb++;
      cr++;
      p += -wrap3 + 2 * BPP;
      lum += -wrap + 2;
      a += -wrap + 2;
    }
    if (w) {
      RGBA_IN_ (r, g, b, ra, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      a[0] = ra;
      p += wrap3;
      lum += wrap;
      a += wrap;
      RGBA_IN_ (r, g, b, ra, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      a[0] = ra;
      cb[0] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      cr[0] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      cb++;
      cr++;
      p += -wrap3 + BPP;
      lum += -wrap + 1;
      a += -wrap + 1;
    }
    p += wrap3 + (wrap3 - width * BPP);
    lum += wrap + (wrap - width);
    a += wrap + (wrap - width);
    cb += dst->linesize[1] - width2;
    cr += dst->linesize[2] - width2;
  }
  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      RGBA_IN_ (r, g, b, ra, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      a[0] = ra;

      RGBA_IN_ (r, g, b, ra, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      a[1] = ra;
      cb[0] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      cr[0] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      cb++;
      cr++;
      p += 2 * BPP;
      lum += 2;
      a += 2;
    }
    if (w) {
      RGBA_IN_ (r, g, b, ra, p);
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      a[0] = ra;
      cb[0] = RGB_TO_U_CCIR (r, g, b, 0);
      cr[0] = RGB_TO_V_CCIR (r, g, b, 0);
    }
  }
}

static void glue (RGB_NAME, _to_nv12) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int wrap, wrap3;
  int r, g, b, r1, g1, b1, w;
  uint8_t *lum, *c;
  const uint8_t *p;

  lum = dst->data[0];
  c = dst->data[1];

  wrap = dst->linesize[0];
  wrap3 = src->linesize[0];
  p = src->data[0];
  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      p += wrap3;
      lum += wrap;

      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);

      c[0] = RGB_TO_U_CCIR (r1, g1, b1, 2);
      c[1] = RGB_TO_V_CCIR (r1, g1, b1, 2);


      c += 2;
      p += -wrap3 + 2 * BPP;
      lum += -wrap + 2;
    }
    /* handle odd width */
    if (w) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      p += wrap3;
      lum += wrap;
      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      c[0] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      c[1] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      p += -wrap3 + BPP;
      lum += -wrap + 1;
    }
    p += wrap3 + (wrap3 - width * BPP);
    lum += wrap + (wrap - width);
    c += dst->linesize[1] - (width & ~1);
  }
  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      c[0] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      c[1] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      c += 2;
      p += 2 * BPP;
      lum += 2;
    }
    /* handle odd width */
    if (w) {
      RGB_IN (r, g, b, p);
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      c[0] = RGB_TO_U_CCIR (r, g, b, 0);
      c[1] = RGB_TO_V_CCIR (r, g, b, 0);
    }
  }
}

static void glue (RGB_NAME, _to_nv21) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int wrap, wrap3;
  int r, g, b, r1, g1, b1, w;
  uint8_t *lum, *c;
  const uint8_t *p;

  lum = dst->data[0];
  c = dst->data[1];

  wrap = dst->linesize[0];
  wrap3 = src->linesize[0];
  p = src->data[0];
  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      p += wrap3;
      lum += wrap;

      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);

      c[1] = RGB_TO_U_CCIR (r1, g1, b1, 2);
      c[0] = RGB_TO_V_CCIR (r1, g1, b1, 2);


      c += 2;
      p += -wrap3 + 2 * BPP;
      lum += -wrap + 2;
    }
    /* handle odd width */
    if (w) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      p += wrap3;
      lum += wrap;
      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      c[1] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      c[0] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      p += -wrap3 + BPP;
      lum += -wrap + 1;
    }
    p += wrap3 + (wrap3 - width * BPP);
    lum += wrap + (wrap - width);
    c += dst->linesize[1] - (width & ~1);
  }
  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y_CCIR (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y_CCIR (r, g, b);
      c[1] = RGB_TO_U_CCIR (r1, g1, b1, 1);
      c[0] = RGB_TO_V_CCIR (r1, g1, b1, 1);
      c += 2;
      p += 2 * BPP;
      lum += 2;
    }
    /* handle odd width */
    if (w) {
      RGB_IN (r, g, b, p);
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      c[1] = RGB_TO_U_CCIR (r, g, b, 0);
      c[0] = RGB_TO_V_CCIR (r, g, b, 0);
    }
  }
}

static void glue (RGB_NAME, _to_gray) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, g, b, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      q[0] = RGB_TO_Y (r, g, b);
      q++;
      p += BPP;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_y800) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, g, b, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      q[0] = RGB_TO_Y_CCIR (r, g, b);
      q++;
      p += BPP;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_y16) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, g, b, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      GST_WRITE_UINT16_LE (q, RGB_TO_Y_CCIR (r, g, b) << 8);
      q += 2;
      p += BPP;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (gray_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - BPP * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      r = p[0];
      RGB_OUT (q, r, r, r);
      q += BPP;
      p++;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_gray16_l) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, g, b, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      GST_WRITE_UINT16_LE (q, RGB_TO_Y (r, g, b) << 8);
      q += 2;
      p += BPP;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (gray16_l_to_, RGB_NAME) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - BPP * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      r = GST_READ_UINT16_LE (p) >> 8;
      RGB_OUT (q, r, r, r);
      q += BPP;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_gray16_b) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, g, b, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - 2 * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      GST_WRITE_UINT16_BE (q, RGB_TO_Y (r, g, b) << 8);
      q += 2;
      p += BPP;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (gray16_b_to_, RGB_NAME) (AVPicture * dst,
    const AVPicture * src, int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, dst_wrap, src_wrap;
  int x, y;

  p = src->data[0];
  src_wrap = src->linesize[0] - 2 * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - BPP * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      r = GST_READ_UINT16_BE (p) >> 8;
      RGB_OUT (q, r, r, r);
      q += BPP;
      p += 2;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

static void glue (pal8_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int r, g, b, dst_wrap, src_wrap;
  int x, y;
  uint32_t v;
  const uint32_t *palette;

  p = src->data[0];
  src_wrap = src->linesize[0] - width;
  palette = (uint32_t *) src->data[1];

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - BPP * width;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      v = palette[p[0]];
      r = (v >> 16) & 0xff;
      g = (v >> 8) & 0xff;
      b = (v) & 0xff;
#ifdef RGBA_OUT
      {
        int a;
        a = (v >> 24) & 0xff;
        RGBA_OUT (q, r, g, b, a);
      }
#else
      RGB_OUT (q, r, g, b);
#endif
      q += BPP;
      p++;
    }
    p += src_wrap;
    q += dst_wrap;
  }
}

#if !defined(FMT_RGBA32) && defined(RGBA_OUT)
/* alpha support */

static void glue (rgba32_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *s;
  uint8_t *d;
  int src_wrap, dst_wrap, j, y;
  unsigned int v, r, g, b, a;

  s = src->data[0];
  src_wrap = src->linesize[0] - width * 4;

  d = dst->data[0];
  dst_wrap = dst->linesize[0] - width * BPP;

  for (y = 0; y < height; y++) {
    for (j = 0; j < width; j++) {
      v = ((const uint32_t *) (s))[0];
      a = (v >> 24) & 0xff;
      r = (v >> 16) & 0xff;
      g = (v >> 8) & 0xff;
      b = v & 0xff;
      RGBA_OUT (d, r, g, b, a);
      s += 4;
      d += BPP;
    }
    s += src_wrap;
    d += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_rgba32) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *s;
  uint8_t *d;
  int src_wrap, dst_wrap, j, y;
  unsigned int r, g, b, a;

  s = src->data[0];
  src_wrap = src->linesize[0] - width * BPP;

  d = dst->data[0];
  dst_wrap = dst->linesize[0] - width * 4;

  for (y = 0; y < height; y++) {
    for (j = 0; j < width; j++) {
      RGBA_IN (r, g, b, a, s);
      ((uint32_t *) (d))[0] = (a << 24) | (r << 16) | (g << 8) | b;
      d += 4;
      s += BPP;
    }
    s += src_wrap;
    d += dst_wrap;
  }
}
#endif /* !defined(FMT_RGBA32) && defined(RGBA_OUT) */

#if defined(FMT_RGBA32)

#if !defined(rgba32_fcts_done)
#define rgba32_fcts_done

static void
ayuv4444_to_rgba32 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *s, *d, *d1, *s1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b, a;

  d = dst->data[0];
  s = src->data[0];
  for (; height > 0; height--) {
    d1 = d;
    s1 = s;
    for (w = width; w > 0; w--) {
      a = s1[0];
      YUV_TO_RGB1_CCIR (s1[2], s1[3]);

      YUV_TO_RGB2_CCIR (r, g, b, s1[1]);
      RGBA_OUT (d1, r, g, b, a);
      d1 += BPP;
      s1 += 4;
    }
    d += dst->linesize[0];
    s += src->linesize[0];
  }
}

static void
rgba32_to_ayuv4444 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int src_wrap, dst_wrap, x, y;
  int r, g, b, a;
  uint8_t *d;
  const uint8_t *p;

  src_wrap = src->linesize[0] - width * BPP;
  dst_wrap = dst->linesize[0] - width * 4;
  d = dst->data[0];
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGBA_IN (r, g, b, a, p);
      d[0] = a;
      d[1] = RGB_TO_Y_CCIR (r, g, b);
      d[2] = RGB_TO_U_CCIR (r, g, b, 0);
      d[3] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      d += 4;
    }
    p += src_wrap;
    d += dst_wrap;
  }
}

#endif /* !defined(rgba32_fcts_done) */

#endif /* defined(FMT_RGBA32) */

#if defined(FMT_BGRA32)
#if !defined(bgra32_fcts_done)
#define bgra32_fcts_done

static void
bgra32_to_ayuv4444 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int src_wrap, dst_wrap, x, y;
  int r, g, b, a;
  uint8_t *d;
  const uint8_t *p;

  src_wrap = src->linesize[0] - width * BPP;
  dst_wrap = dst->linesize[0] - width * 4;
  d = dst->data[0];
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGBA_IN (r, g, b, a, p);
      d[0] = a;
      d[1] = RGB_TO_Y_CCIR (r, g, b);
      d[2] = RGB_TO_U_CCIR (r, g, b, 0);
      d[3] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      d += 4;
    }
    p += src_wrap;
    d += dst_wrap;
  }
}

static void
ayuv4444_to_bgra32 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *s, *d, *d1, *s1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b, a;

  d = dst->data[0];
  s = src->data[0];
  for (; height > 0; height--) {
    d1 = d;
    s1 = s;
    for (w = width; w > 0; w--) {
      a = s1[0];
      YUV_TO_RGB1_CCIR (s1[2], s1[3]);

      YUV_TO_RGB2_CCIR (r, g, b, s1[1]);
      RGBA_OUT (d1, r, g, b, a);
      d1 += BPP;
      s1 += 4;
    }
    d += dst->linesize[0];
    s += src->linesize[0];
  }
}

#endif /* !defined(bgra32_fcts_done) */

#endif /* defined(FMT_BGRA32) */

#if defined(FMT_ARGB32)

#if !defined(argb32_fcts_done)
#define argb32_fcts_done

static void
ayuv4444_to_argb32 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *s, *d, *d1, *s1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b, a;

  d = dst->data[0];
  s = src->data[0];
  for (; height > 0; height--) {
    d1 = d;
    s1 = s;
    for (w = width; w > 0; w--) {
      a = s1[0];
      YUV_TO_RGB1_CCIR (s1[2], s1[3]);

      YUV_TO_RGB2_CCIR (r, g, b, s1[1]);
      RGBA_OUT (d1, r, g, b, a);
      d1 += BPP;
      s1 += 4;
    }
    d += dst->linesize[0];
    s += src->linesize[0];
  }
}

static void
argb32_to_ayuv4444 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int src_wrap, dst_wrap, x, y;
  int r, g, b, a;
  uint8_t *d;
  const uint8_t *p;

  src_wrap = src->linesize[0] - width * BPP;
  dst_wrap = dst->linesize[0] - width * 4;
  d = dst->data[0];
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGBA_IN (r, g, b, a, p);
      d[0] = a;
      d[1] = RGB_TO_Y_CCIR (r, g, b);
      d[2] = RGB_TO_U_CCIR (r, g, b, 0);
      d[3] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      d += 4;
    }
    p += src_wrap;
    d += dst_wrap;
  }
}

#endif /* !defined(argb32_fcts_done) */

#endif /* defined(FMT_ARGB32) */

#if defined(FMT_ABGR32)
#if !defined(abgr32_fcts_done)
#define abgr32_fcts_done

static void
abgr32_to_ayuv4444 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int src_wrap, dst_wrap, x, y;
  int r, g, b, a;
  uint8_t *d;
  const uint8_t *p;

  src_wrap = src->linesize[0] - width * BPP;
  dst_wrap = dst->linesize[0] - width * 4;
  d = dst->data[0];
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGBA_IN (r, g, b, a, p);
      d[0] = a;
      d[1] = RGB_TO_Y_CCIR (r, g, b);
      d[2] = RGB_TO_U_CCIR (r, g, b, 0);
      d[3] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      d += 4;
    }
    p += src_wrap;
    d += dst_wrap;
  }
}

static void
ayuv4444_to_abgr32 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *s, *d, *d1, *s1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b, a;

  d = dst->data[0];
  s = src->data[0];
  for (; height > 0; height--) {
    d1 = d;
    s1 = s;
    for (w = width; w > 0; w--) {
      a = s1[0];
      YUV_TO_RGB1_CCIR (s1[2], s1[3]);

      YUV_TO_RGB2_CCIR (r, g, b, s1[1]);
      RGBA_OUT (d1, r, g, b, a);
      d1 += BPP;
      s1 += 4;
    }
    d += dst->linesize[0];
    s += src->linesize[0];
  }
}

#endif /* !defined(abgr32_fcts_done) */

#endif /* defined(FMT_ABGR32) */

#ifndef FMT_RGB24

static void glue (rgb24_to_, RGB_NAME) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *s;
  uint8_t *d;
  int src_wrap, dst_wrap, j, y;
  unsigned int r, g, b;

  s = src->data[0];
  src_wrap = src->linesize[0] - width * 3;

  d = dst->data[0];
  dst_wrap = dst->linesize[0] - width * BPP;

  for (y = 0; y < height; y++) {
    for (j = 0; j < width; j++) {
      r = s[0];
      g = s[1];
      b = s[2];
      RGB_OUT (d, r, g, b);
      s += 3;
      d += BPP;
    }
    s += src_wrap;
    d += dst_wrap;
  }
}

static void glue (RGB_NAME, _to_rgb24) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *s;
  uint8_t *d;
  int src_wrap, dst_wrap, j, y;
  unsigned int r, g, b;

  s = src->data[0];
  src_wrap = src->linesize[0] - width * BPP;

  d = dst->data[0];
  dst_wrap = dst->linesize[0] - width * 3;

  for (y = 0; y < height; y++) {
    for (j = 0; j < width; j++) {
      RGB_IN (r, g, b, s)
          d[0] = r;
      d[1] = g;
      d[2] = b;
      d += 3;
      s += BPP;
    }
    s += src_wrap;
    d += dst_wrap;
  }
}

#endif /* !FMT_RGB24 */

#ifdef FMT_RGB24

static void
yuv444p_to_rgb24 (AVPicture * dst, const AVPicture * src, int width, int height)
{
  const uint8_t *y1_ptr, *cb_ptr, *cr_ptr;
  uint8_t *d, *d1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;

  d = dst->data[0];
  y1_ptr = src->data[0];
  cb_ptr = src->data[1];
  cr_ptr = src->data[2];
  for (; height > 0; height--) {
    d1 = d;
    for (w = width; w > 0; w--) {
      YUV_TO_RGB1_CCIR (cb_ptr[0], cr_ptr[0]);

      YUV_TO_RGB2_CCIR (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;

      y1_ptr++;
      cb_ptr++;
      cr_ptr++;
    }
    d += dst->linesize[0];
    y1_ptr += src->linesize[0] - width;
    cb_ptr += src->linesize[1] - width;
    cr_ptr += src->linesize[2] - width;
  }
}

static void
yuvj444p_to_rgb24 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const uint8_t *y1_ptr, *cb_ptr, *cr_ptr;
  uint8_t *d, *d1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;

  d = dst->data[0];
  y1_ptr = src->data[0];
  cb_ptr = src->data[1];
  cr_ptr = src->data[2];
  for (; height > 0; height--) {
    d1 = d;
    for (w = width; w > 0; w--) {
      YUV_TO_RGB1 (cb_ptr[0], cr_ptr[0]);

      YUV_TO_RGB2 (r, g, b, y1_ptr[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;

      y1_ptr++;
      cb_ptr++;
      cr_ptr++;
    }
    d += dst->linesize[0];
    y1_ptr += src->linesize[0] - width;
    cb_ptr += src->linesize[1] - width;
    cr_ptr += src->linesize[2] - width;
  }
}

static void
rgb24_to_yuv444p (AVPicture * dst, const AVPicture * src, int width, int height)
{
  int src_wrap, x, y;
  int r, g, b;
  uint8_t *lum, *cb, *cr;
  const uint8_t *p;

  lum = dst->data[0];
  cb = dst->data[1];
  cr = dst->data[2];

  src_wrap = src->linesize[0] - width * BPP;
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      lum[0] = RGB_TO_Y_CCIR (r, g, b);
      cb[0] = RGB_TO_U_CCIR (r, g, b, 0);
      cr[0] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      cb++;
      cr++;
      lum++;
    }
    p += src_wrap;
    lum += dst->linesize[0] - width;
    cb += dst->linesize[1] - width;
    cr += dst->linesize[2] - width;
  }
}

static void
rgb24_to_yuvj420p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int wrap, wrap3, width2;
  int r, g, b, r1, g1, b1, w;
  uint8_t *lum, *cb, *cr;
  const uint8_t *p;

  lum = dst->data[0];
  cb = dst->data[1];
  cr = dst->data[2];

  width2 = (width + 1) >> 1;
  wrap = dst->linesize[0];
  wrap3 = src->linesize[0];
  p = src->data[0];
  for (; height >= 2; height -= 2) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y (r, g, b);
      p += wrap3;
      lum += wrap;

      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y (r, g, b);

      cb[0] = RGB_TO_U (r1, g1, b1, 2);
      cr[0] = RGB_TO_V (r1, g1, b1, 2);

      cb++;
      cr++;
      p += -wrap3 + 2 * BPP;
      lum += -wrap + 2;
    }
    if (w) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y (r, g, b);
      p += wrap3;
      lum += wrap;
      RGB_IN (r, g, b, p);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[0] = RGB_TO_Y (r, g, b);
      cb[0] = RGB_TO_U (r1, g1, b1, 1);
      cr[0] = RGB_TO_V (r1, g1, b1, 1);
      cb++;
      cr++;
      p += -wrap3 + BPP;
      lum += -wrap + 1;
    }
    p += wrap3 + (wrap3 - width * BPP);
    lum += wrap + (wrap - width);
    cb += dst->linesize[1] - width2;
    cr += dst->linesize[2] - width2;
  }
  /* handle odd height */
  if (height) {
    for (w = width; w >= 2; w -= 2) {
      RGB_IN (r, g, b, p);
      r1 = r;
      g1 = g;
      b1 = b;
      lum[0] = RGB_TO_Y (r, g, b);

      RGB_IN (r, g, b, p + BPP);
      r1 += r;
      g1 += g;
      b1 += b;
      lum[1] = RGB_TO_Y (r, g, b);
      cb[0] = RGB_TO_U (r1, g1, b1, 1);
      cr[0] = RGB_TO_V (r1, g1, b1, 1);
      cb++;
      cr++;
      p += 2 * BPP;
      lum += 2;
    }
    if (w) {
      RGB_IN (r, g, b, p);
      lum[0] = RGB_TO_Y (r, g, b);
      cb[0] = RGB_TO_U (r, g, b, 0);
      cr[0] = RGB_TO_V (r, g, b, 0);
    }
  }
}

static void
rgb24_to_yuvj444p (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int src_wrap, x, y;
  int r, g, b;
  uint8_t *lum, *cb, *cr;
  const uint8_t *p;

  lum = dst->data[0];
  cb = dst->data[1];
  cr = dst->data[2];

  src_wrap = src->linesize[0] - width * BPP;
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      lum[0] = RGB_TO_Y (r, g, b);
      cb[0] = RGB_TO_U (r, g, b, 0);
      cr[0] = RGB_TO_V (r, g, b, 0);
      p += BPP;
      cb++;
      cr++;
      lum++;
    }
    p += src_wrap;
    lum += dst->linesize[0] - width;
    cb += dst->linesize[1] - width;
    cr += dst->linesize[2] - width;
  }
}

static void
ayuv4444_to_rgb24 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  uint8_t *s, *d, *d1, *s1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;

  d = dst->data[0];
  s = src->data[0];
  for (; height > 0; height--) {
    d1 = d;
    s1 = s;
    for (w = width; w > 0; w--) {
      YUV_TO_RGB1_CCIR (s1[2], s1[3]);

      YUV_TO_RGB2_CCIR (r, g, b, s1[1]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;
      s1 += 4;
    }
    d += dst->linesize[0];
    s += src->linesize[0];
  }
}

static void
rgb24_to_ayuv4444 (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  int src_wrap, dst_wrap, x, y;
  int r, g, b;
  uint8_t *d;
  const uint8_t *p;

  src_wrap = src->linesize[0] - width * BPP;
  dst_wrap = dst->linesize[0] - width * 4;
  d = dst->data[0];
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      d[0] = 0xff;
      d[1] = RGB_TO_Y_CCIR (r, g, b);
      d[2] = RGB_TO_U_CCIR (r, g, b, 0);
      d[3] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      d += 4;
    }
    p += src_wrap;
    d += dst_wrap;
  }
}

static void
v308_to_rgb24 (AVPicture * dst, const AVPicture * src, int width, int height)
{
  uint8_t *s, *d, *d1, *s1;
  int w, y, cb, cr, r_add, g_add, b_add;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  unsigned int r, g, b;

  d = dst->data[0];
  s = src->data[0];
  for (; height > 0; height--) {
    d1 = d;
    s1 = s;
    for (w = width; w > 0; w--) {
      YUV_TO_RGB1_CCIR (s1[1], s1[2]);

      YUV_TO_RGB2_CCIR (r, g, b, s1[0]);
      RGB_OUT (d1, r, g, b);
      d1 += BPP;
      s1 += 3;
    }
    d += dst->linesize[0];
    s += src->linesize[0];
  }
}

static void
rgb24_to_v308 (AVPicture * dst, const AVPicture * src, int width, int height)
{
  int src_wrap, dst_wrap, x, y;
  int r, g, b;
  uint8_t *d;
  const uint8_t *p;

  src_wrap = src->linesize[0] - width * BPP;
  dst_wrap = dst->linesize[0] - width * 3;
  d = dst->data[0];
  p = src->data[0];
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGB_IN (r, g, b, p);
      d[0] = RGB_TO_Y_CCIR (r, g, b);
      d[1] = RGB_TO_U_CCIR (r, g, b, 0);
      d[2] = RGB_TO_V_CCIR (r, g, b, 0);
      p += BPP;
      d += 3;
    }
    p += src_wrap;
    d += dst_wrap;
  }
}
#endif /* FMT_RGB24 */

#if defined(FMT_RGB24) || defined(FMT_RGBA32)

static void glue (RGB_NAME, _to_pal8) (AVPicture * dst, const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  unsigned char *q;
  int dst_wrap, src_wrap;
  int x, y, has_alpha;
  unsigned int r, g, b;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;

  q = dst->data[0];
  dst_wrap = dst->linesize[0] - width;
  has_alpha = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
#ifdef RGBA_IN
      {
        unsigned int a;
        RGBA_IN (r, g, b, a, p);
        /* crude approximation for alpha ! */
        if (a < 0x80) {
          has_alpha = 1;
          q[0] = TRANSP_INDEX;
        } else {
          q[0] = gif_clut_index (r, g, b);
        }
      }
#else
      RGB_IN (r, g, b, p);
      q[0] = gif_clut_index (r, g, b);
#endif
      q++;
      p += BPP;
    }
    p += src_wrap;
    q += dst_wrap;
  }

  build_rgb_palette (dst->data[1], has_alpha);
}

#endif /* defined(FMT_RGB24) || defined(FMT_RGBA32) */

#ifdef RGBA_IN

static int glue (get_alpha_info_, RGB_NAME) (const AVPicture * src,
    int width, int height)
{
  const unsigned char *p;
  int src_wrap, ret, x, y;
  unsigned int G_GNUC_UNUSED r, G_GNUC_UNUSED g, G_GNUC_UNUSED b, a;

  p = src->data[0];
  src_wrap = src->linesize[0] - BPP * width;
  ret = 0;
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      RGBA_IN (r, g, b, a, p);
      if (a == 0x00) {
        ret |= FF_ALPHA_TRANSP;
      } else if (a != 0xff) {
        ret |= FF_ALPHA_SEMI_TRANSP;
      }
      p += BPP;
    }
    p += src_wrap;
  }
  return ret;
}

#endif /* RGBA_IN */

#undef RGB_IN
#undef RGBA_IN
#undef RGB_OUT
#undef RGBA_OUT
#undef BPP
#undef RGB_NAME
#undef FMT_RGB24
#undef FMT_RGBA32
#undef YUVA_TO_A
#undef RGBA_OUT_
#undef RGBA_IN_
