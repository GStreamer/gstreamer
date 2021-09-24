/* GStreamer
 * Copyright (C) 2009 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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
#  include "config.h"
#endif
#include <string.h>
#include <kate/kate.h>
#include <gst/gst.h>
#include <gst/gstpad.h>
#include "gstkatespu.h"

#define MAX_SPU_SIZE 53220

GST_DEBUG_CATEGORY_EXTERN (gst_kateenc_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_katedec_debug);

/* taken off the dvdsubdec element */
const guint32 gst_kate_spu_default_clut[16] = {
  0xb48080, 0x248080, 0x628080, 0xd78080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080
};

#define GST_CAT_DEFAULT gst_kateenc_debug

static void
gst_kate_spu_decode_colormap (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_colormap[3] = ptr[0] >> 4;
  ke->spu_colormap[2] = ptr[0] & 0x0f;
  ke->spu_colormap[1] = ptr[1] >> 4;
  ke->spu_colormap[0] = ptr[1] & 0x0f;
}

static void
gst_kate_spu_decode_alpha (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_alpha[3] = ptr[0] >> 4;
  ke->spu_alpha[2] = ptr[0] & 0x0f;
  ke->spu_alpha[1] = ptr[1] >> 4;
  ke->spu_alpha[0] = ptr[1] & 0x0f;
}

static void
gst_kate_spu_decode_area (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_left = ((((guint16) ptr[0]) & 0xff) << 4) | (ptr[1] >> 4);
  ke->spu_top = ((((guint16) ptr[3]) & 0xff) << 4) | (ptr[4] >> 4);
  ke->spu_right = ((((guint16) ptr[1]) & 0x0f) << 8) | ptr[2];
  ke->spu_bottom = ((((guint16) ptr[4]) & 0x0f) << 8) | ptr[5];
  GST_DEBUG_OBJECT (ke, "SPU area %u %u -> %u %d", ke->spu_left, ke->spu_top,
      ke->spu_right, ke->spu_bottom);
}

static void
gst_kate_spu_decode_pixaddr (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_pix_data[0] = GST_KATE_UINT16_BE (ptr + 0);
  ke->spu_pix_data[1] = GST_KATE_UINT16_BE (ptr + 2);
}

/* heavily inspired from dvdspudec */
static guint16
gst_kate_spu_decode_colcon (GstKateEnc * ke, const guint8 * ptr, guint16 sz)
{
  guint16 nbytes = GST_KATE_UINT16_BE (ptr + 0);
  guint16 nbytes_left = nbytes;

  GST_LOG_OBJECT (ke, "Number of bytes in color/contrast change command is %u",
      nbytes);
  if (G_UNLIKELY (nbytes < 2)) {
    GST_WARNING_OBJECT (ke,
        "Number of bytes in color/contrast change command is %u, should be at least 2",
        nbytes);
    return 0;
  }
  if (G_UNLIKELY (nbytes > sz)) {
    GST_WARNING_OBJECT (ke,
        "Number of bytes in color/contrast change command is %u, but the buffer "
        "only contains %u byte(s)", nbytes, sz);
    return 0;
  }

  ptr += 2;
  nbytes_left -= 2;

  /* we will just skip that data for now */
  while (nbytes_left > 0) {
    guint32 entry, nchanges, sz;
    GST_LOG_OBJECT (ke, "Reading a color/contrast change entry, %u bytes left",
        nbytes_left);
    if (G_UNLIKELY (nbytes_left < 4)) {
      GST_WARNING_OBJECT (ke,
          "Not enough bytes to read a full color/contrast entry header");
      break;
    }
    entry = GST_READ_UINT32_BE (ptr);
    GST_LOG_OBJECT (ke, "Color/contrast change entry header is %08x", entry);
    nchanges = CLAMP ((ptr[2] >> 4), 1, 8);
    ptr += 4;
    nbytes_left -= 4;
    if (entry == 0x0fffffff) {
      GST_LOG_OBJECT (ke,
          "Encountered color/contrast change termination code, breaking, %u bytes left",
          nbytes_left);
      break;
    }
    GST_LOG_OBJECT (ke, "Color/contrast change entry has %u changes", nchanges);
    sz = 6 * nchanges;
    if (G_UNLIKELY (sz > nbytes_left)) {
      GST_WARNING_OBJECT (ke,
          "Not enough bytes to read a full color/contrast entry");
      break;
    }
    ptr += sz;
    nbytes_left -= sz;
  }
  return nbytes - nbytes_left;
}

static inline guint8
gst_kate_spu_get_nybble (const guint8 * nybbles, size_t * nybble_offset)
{
  guint8 ret;

  ret = nybbles[(*nybble_offset) / 2];

  /* If the offset is even, we shift the answer down 4 bits, otherwise not */
  if ((*nybble_offset) & 0x01)
    ret &= 0x0f;
  else
    ret = ret >> 4;

  (*nybble_offset)++;

  return ret;
}

static guint16
gst_kate_spu_get_rle_code (const guint8 * nybbles, size_t * nybble_offset)
{
  guint16 code;

  code = gst_kate_spu_get_nybble (nybbles, nybble_offset);
  if (code < 0x4) {             /* 4 .. f */
    code = (code << 4) | gst_kate_spu_get_nybble (nybbles, nybble_offset);
    if (code < 0x10) {          /* 1x .. 3x */
      code = (code << 4) | gst_kate_spu_get_nybble (nybbles, nybble_offset);
      if (code < 0x40) {        /* 04x .. 0fx */
        code = (code << 4) | gst_kate_spu_get_nybble (nybbles, nybble_offset);
      }
    }
  }
  return code;
}

static void
gst_kate_spu_crop_bitmap (GstKateEnc * ke, kate_bitmap * kb, guint16 * dx,
    guint16 * dy)
{
  int top, bottom, left, right;
  guint8 zero = 0;
  size_t n, x, y, w, h;

#if 0
  /* find the zero */
  zero = kb->pixels[0];
  for (x = 0; x < kb->width; ++x) {
    if (kb->pixels[x] != zero) {
      GST_LOG_OBJECT (ke, "top line at %u is not zero: %u", x, kb->pixels[x]);
      return;
    }
  }
#endif

  /* top */
  for (top = 0; top < kb->height; ++top) {
    int empty = 1;
    for (x = 0; x < kb->width; ++x) {
      if (G_UNLIKELY (kb->pixels[x + top * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }

  /* bottom */
  for (bottom = kb->height - 1; bottom >= top; --bottom) {
    int empty = 1;
    for (x = 0; x < kb->width; ++x) {
      if (G_UNLIKELY (kb->pixels[x + bottom * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }

  /* left */
  for (left = 0; left < kb->width; ++left) {
    int empty = 1;
    for (y = top; y <= bottom; ++y) {
      if (G_UNLIKELY (kb->pixels[left + y * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }

  /* right */
  for (right = kb->width - 1; right >= left; --right) {
    int empty = 1;
    for (y = top; y <= bottom; ++y) {
      if (G_UNLIKELY (kb->pixels[right + y * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }


  w = right - left + 1;
  h = bottom - top + 1;
  GST_LOG_OBJECT (ke, "cropped from %" G_GSIZE_FORMAT " %" G_GSIZE_FORMAT
      " to %" G_GSIZE_FORMAT " %" G_GSIZE_FORMAT, kb->width, kb->height, w, h);
  *dx += left;
  *dy += top;
  n = 0;
  for (y = 0; y < h; ++y) {
    memmove (kb->pixels + n, kb->pixels + kb->width * (y + top) + left, w);
    n += w;
  }
  kb->width = w;
  kb->height = h;
}

#define CHECK(x) G_STMT_START { \
      guint16 _ = (x); \
      if (G_UNLIKELY((_) > sz)) { \
        GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL), ("Read outside buffer")); \
        return GST_FLOW_ERROR; \
      } \
    } G_STMT_END
#define ADVANCE(x) G_STMT_START { \
      guint16 _ = (x); ptr += (_); sz -= (_); \
    } G_STMT_END
#define IGNORE(x) G_STMT_START { \
      guint16 __ = (x); \
      CHECK (__); \
      ADVANCE (__); \
    } G_STMT_END

static GstFlowReturn
gst_kate_spu_decode_command_sequence (GstKateEnc * ke, GstBuffer * buf,
    guint16 command_sequence_offset)
{
  guint16 date;
  guint16 next_command_sequence;
  const guint8 *ptr;
  GstMapInfo info;
  guint16 sz;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (ke, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  if (command_sequence_offset >= info.size)
    goto out_of_range;

  ptr = info.data + command_sequence_offset;
  sz = info.size - command_sequence_offset;

  GST_DEBUG_OBJECT (ke, "Decoding command sequence at %u (%u bytes)",
      command_sequence_offset, sz);

  CHECK (2);
  date = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  GST_DEBUG_OBJECT (ke, "date %u", date);

  CHECK (2);
  next_command_sequence = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  GST_DEBUG_OBJECT (ke, "next command sequence at %u", next_command_sequence);

  while (sz) {
    guint8 cmd = *ptr++;
    switch (cmd) {
      case SPU_CMD_FSTA_DSP:   /* 0x00 */
        GST_DEBUG_OBJECT (ke, "[0] DISPLAY");
        break;
      case SPU_CMD_DSP:        /* 0x01 */
        GST_DEBUG_OBJECT (ke, "[1] SHOW");
        ke->show_time = date;
        break;
      case SPU_CMD_STP_DSP:    /* 0x02 */
        GST_DEBUG_OBJECT (ke, "[2] HIDE");
        ke->hide_time = date;
        break;
      case SPU_CMD_SET_COLOR:  /* 0x03 */
        GST_DEBUG_OBJECT (ke, "[3] SET COLOR");
        CHECK (2);
        gst_kate_spu_decode_colormap (ke, ptr);
        ADVANCE (2);
        break;
      case SPU_CMD_SET_ALPHA:  /* 0x04 */
        GST_DEBUG_OBJECT (ke, "[4] SET ALPHA");
        CHECK (2);
        gst_kate_spu_decode_alpha (ke, ptr);
        ADVANCE (2);
        break;
      case SPU_CMD_SET_DAREA:  /* 0x05 */
        GST_DEBUG_OBJECT (ke, "[5] SET DISPLAY AREA");
        CHECK (6);
        gst_kate_spu_decode_area (ke, ptr);
        ADVANCE (6);
        break;
      case SPU_CMD_DSPXA:      /* 0x06 */
        GST_DEBUG_OBJECT (ke, "[6] SET PIXEL ADDRESSES");
        CHECK (4);
        gst_kate_spu_decode_pixaddr (ke, ptr);
        GST_DEBUG_OBJECT (ke, "  -> first pixel address %u",
            ke->spu_pix_data[0]);
        GST_DEBUG_OBJECT (ke, "  -> second pixel address %u",
            ke->spu_pix_data[1]);
        ADVANCE (4);
        break;
      case SPU_CMD_CHG_COLCON: /* 0x07 */
        GST_DEBUG_OBJECT (ke, "[7] CHANGE COLOR/CONTRAST");
        CHECK (2);
        ADVANCE (gst_kate_spu_decode_colcon (ke, ptr, sz));
        break;
      case SPU_CMD_END:        /* 0xff */
        GST_DEBUG_OBJECT (ke, "[0xff] END");
        if (next_command_sequence != command_sequence_offset) {
          GST_DEBUG_OBJECT (ke, "Jumping to next sequence at offset %u",
              next_command_sequence);
          gst_buffer_unmap (buf, &info);
          return gst_kate_spu_decode_command_sequence (ke, buf,
              next_command_sequence);
        } else {
          gst_buffer_unmap (buf, &info);
          GST_DEBUG_OBJECT (ke, "No more sequences to decode");
          return GST_FLOW_OK;
        }
        break;
      default:
        gst_buffer_unmap (buf, &info);
        GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
            ("Invalid SPU command: %u", cmd));
        return GST_FLOW_ERROR;
    }
  }
  gst_buffer_unmap (buf, &info);
  GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL), ("Error parsing SPU"));
  return GST_FLOW_ERROR;

  /* ERRORS */
out_of_range:
  {
    gst_buffer_unmap (buf, &info);
    GST_ELEMENT_ERROR (ke, STREAM, DECODE, (NULL),
        ("Command sequence offset %u is out of range %" G_GSIZE_FORMAT,
            command_sequence_offset, info.size));
    return GST_FLOW_ERROR;
  }
}

static inline int
gst_kate_spu_clamp (int value)
{
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return value;
}

static void
gst_kate_spu_yuv2rgb (int y, int u, int v, int *r, int *g, int *b)
{
#if 0
  *r = gst_kate_spu_clamp (y + 1.371 * v);
  *g = gst_kate_spu_clamp (y - 0.698 * v - 0.336 * u);
  *b = gst_kate_spu_clamp (y + 1.732 * u);
#elif 0
  *r = gst_kate_spu_clamp (y + u);
  *g = gst_kate_spu_clamp (y - (76 * u - 26 * v) / 256);
  *b = gst_kate_spu_clamp (y + v);
#else
  y = (y - 16) * 255 / 219;
  u = (u - 128) * 255 / 224;
  v = (v - 128) * 255 / 224;

  *r = gst_kate_spu_clamp (y + 1.402 * v);
  *g = gst_kate_spu_clamp (y - 0.34414 * u - 0.71414 * v);
  *b = gst_kate_spu_clamp (y + 1.772 * u);
#endif
}

static GstFlowReturn
gst_kate_spu_create_spu_palette (GstKateEnc * ke, kate_palette * kp)
{
  size_t n;

  kate_palette_init (kp);
  kp->ncolors = 4;
  kp->colors = (kate_color *) g_malloc (kp->ncolors * sizeof (kate_color));
  if (G_UNLIKELY (!kp->colors)) {
    GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL), ("Out of memory"));
    return GST_FLOW_ERROR;
  }
#if 1
  for (n = 0; n < kp->ncolors; ++n) {
    int idx = ke->spu_colormap[n];
    guint32 color = ke->spu_clut[idx];
    int y = (color >> 16) & 0xff;
    int v = (color >> 8) & 0xff;
    int u = color & 0xff;
    int r, g, b;
    gst_kate_spu_yuv2rgb (y, u, v, &r, &g, &b);
    kp->colors[n].r = r;
    kp->colors[n].g = g;
    kp->colors[n].b = b;
    kp->colors[n].a = ke->spu_alpha[n] * 17;
  }
#else
  /* just make a ramp from 0 to 255 for those non transparent colors */
  for (n = 0; n < kp->ncolors; ++n)
    if (ke->spu_alpha[n] == 0)
      ++ntrans;

  for (n = 0; n < kp->ncolors; ++n) {
    kp->colors[n].r = luma;
    kp->colors[n].g = luma;
    kp->colors[n].b = luma;
    kp->colors[n].a = ke->spu_alpha[n] * 17;
    if (ke->spu_alpha[n])
      luma /= 2;
  }
#endif

  return GST_FLOW_OK;
}

GstFlowReturn
gst_kate_spu_decode_spu (GstKateEnc * ke, GstBuffer * buf, kate_region * kr,
    kate_bitmap * kb, kate_palette * kp)
{
  GstMapInfo info;
  const guint8 *ptr;
  size_t sz;
  guint16 packet_size;
  guint16 x, y;
  size_t n;
  guint8 *pixptr[2];
  size_t nybble_offset[2];
  size_t max_nybbles[2];
  GstFlowReturn rflow;
  guint16 next_command_sequence;
  guint16 code;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (ke, "Failed to map buffer");
  }

  ptr = info.data;
  sz = info.size;

  /* before decoding anything, initialize to sensible defaults */
  memset (ke->spu_colormap, 0, sizeof (ke->spu_colormap));
  memset (ke->spu_alpha, 0, sizeof (ke->spu_alpha));
  ke->spu_top = ke->spu_left = 1;
  ke->spu_bottom = ke->spu_right = 0;
  ke->spu_pix_data[0] = ke->spu_pix_data[1] = 0;
  ke->show_time = ke->hide_time = 0;

  /* read sizes and get to the start of the data */
  CHECK (2);
  packet_size = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  GST_DEBUG_OBJECT (ke, "packet size %d (GstBuffer size %" G_GSIZE_FORMAT ")",
      packet_size, info.size);

  CHECK (2);
  next_command_sequence = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  ptr = info.data + next_command_sequence;
  sz = info.size - next_command_sequence;
  GST_DEBUG_OBJECT (ke, "next command sequence at %u for %u",
      next_command_sequence, (guint) sz);

  rflow = gst_kate_spu_decode_command_sequence (ke, buf, next_command_sequence);
  if (G_UNLIKELY (rflow != GST_FLOW_OK)) {
    gst_buffer_unmap (buf, &info);
    return rflow;
  }

  /* if no addresses or sizes were given, or if they define an empty SPU, nothing more to do */
  if (G_UNLIKELY (ke->spu_right - ke->spu_left < 0
          || ke->spu_bottom - ke->spu_top < 0 || ke->spu_pix_data[0] == 0
          || ke->spu_pix_data[1] == 0)) {
    GST_DEBUG_OBJECT (ke,
        "left %d, right %d, top %d, bottom %d, pix data %d %d", ke->spu_left,
        ke->spu_right, ke->spu_top, ke->spu_bottom, ke->spu_pix_data[0],
        ke->spu_pix_data[1]);
    GST_WARNING_OBJECT (ke, "SPU area is empty, nothing to encode");
    kate_bitmap_init (kb);
    kb->width = kb->height = 0;
    gst_buffer_unmap (buf, &info);
    return GST_FLOW_OK;
  }

  /* create the palette */
  rflow = gst_kate_spu_create_spu_palette (ke, kp);
  if (G_UNLIKELY (rflow != GST_FLOW_OK)) {
    gst_buffer_unmap (buf, &info);
    return rflow;
  }

  /* create the bitmap */
  kate_bitmap_init (kb);
  kb->width = ke->spu_right - ke->spu_left + 1;
  kb->height = ke->spu_bottom - ke->spu_top + 1;
  kb->bpp = 2;
  kb->type = kate_bitmap_type_paletted;
  kb->pixels = (unsigned char *) g_malloc (kb->width * kb->height);
  if (G_UNLIKELY (!kb->pixels)) {
    gst_buffer_unmap (buf, &info);
    GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
        ("Failed to allocate memory for pixel data"));
    return GST_FLOW_ERROR;
  }

  n = 0;
  pixptr[0] = info.data + ke->spu_pix_data[0];
  pixptr[1] = info.data + ke->spu_pix_data[1];
  nybble_offset[0] = 0;
  nybble_offset[1] = 0;
  max_nybbles[0] = 2 * (packet_size - ke->spu_pix_data[0]);
  max_nybbles[1] = 2 * (packet_size - ke->spu_pix_data[1]);
  for (y = 0; y < kb->height; ++y) {
    nybble_offset[y & 1] = GST_ROUND_UP_2 (nybble_offset[y & 1]);
    for (x = 0; x < kb->width;) {
      if (G_UNLIKELY (nybble_offset[y & 1] >= max_nybbles[y & 1])) {
        GST_DEBUG_OBJECT (ke, "RLE overflow, clearing the remainder");
        memset (kb->pixels + n, 0, kb->width - x);
        n += kb->width - x;
        break;
      }
      code = gst_kate_spu_get_rle_code (pixptr[y & 1], &nybble_offset[y & 1]);
      if (code == 0) {
        memset (kb->pixels + n, 0, kb->width - x);
        n += kb->width - x;
        break;
      } else {
        guint16 npixels = code >> 2;
        guint16 pixel = code & 3;
        if (npixels > kb->width - x) {
          npixels = kb->width - x;
        }
        memset (kb->pixels + n, pixel, npixels);
        n += npixels;
        x += npixels;
      }
    }
  }

  GST_LOG_OBJECT (ke, "%u/%u bytes left in the data packet",
      (guint) (max_nybbles[0] - nybble_offset[0]),
      (guint) (max_nybbles[1] - nybble_offset[1]));

  /* some streams seem to have huge uncropped SPUs, fix those up */
  x = ke->spu_left;
  y = ke->spu_top;
  gst_kate_spu_crop_bitmap (ke, kb, &x, &y);

  /* create the region */
  kate_region_init (kr);
  if (ke->original_canvas_width > 0 && ke->original_canvas_height > 0) {
    /* prefer relative sizes in case we're encoding for a different resolution
       that what the SPU was created for */
    kr->metric = kate_millionths;
    kr->x = 1000000 * (size_t) x / ke->original_canvas_width;
    kr->y = 1000000 * (size_t) y / ke->original_canvas_height;
    kr->w = 1000000 * kb->width / ke->original_canvas_width;
    kr->h = 1000000 * kb->height / ke->original_canvas_height;
  } else {
    kr->metric = kate_pixel;
    kr->x = x;
    kr->y = y;
    kr->w = kb->width;
    kr->h = kb->height;
  }

  /* some SPUs have no hide time */
  if (ke->hide_time == 0) {
    GST_INFO_OBJECT (ke, "SPU has no hide time");
    /* now, we don't know when the next SPU is scheduled to go, since we probably
       haven't received it yet, so we'll just make it a 1 second delay, which is
       probably going to end before the next one while being readable */
    //ke->hide_time = ke->show_time + (1000 * 90 / 1024);
  }
  gst_buffer_unmap (buf, &info);

  return GST_FLOW_OK;
}

#undef IGNORE
#undef ADVANCE
#undef CHECK

#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_katedec_debug

static void
gst_kate_spu_add_nybble (unsigned char *bytes, size_t nbytes, int nybble_offset,
    unsigned char nybble)
{
  unsigned char *ptr = bytes + nbytes + nybble_offset / 2;
  if (!(nybble_offset & 1)) {
    *ptr = nybble << 4;
  } else {
    *ptr |= nybble;
  }
}

static void
gst_kate_spu_rgb2yuv (int r, int g, int b, int *y, int *u, int *v)
{
  *y = gst_kate_spu_clamp (r * 0.299 * 219 / 255 + g * 0.587 * 219 / 255 +
      b * 0.114 * 219 / 255 + 16);
  *u = gst_kate_spu_clamp (-r * 0.16874 * 224 / 255 - g * 0.33126 * 224 / 255 +
      b * 0.5 * 224 / 255 + 128);
  *v = gst_kate_spu_clamp (r * 0.5 * 224 / 255 - g * 0.41869 * 224 / 255 -
      b * 0.08131 * 224 / 255 + 128);
}

static void
gst_kate_spu_make_palette (GstKateDec * kd, int palette[4],
    const kate_palette * kp)
{
  int n;
  GstStructure *structure;
  GstEvent *event;
  char name[16];
  int y, u, v;

  palette[0] = 0;
  palette[1] = 1;
  palette[2] = 2;
  palette[3] = 3;

  structure = gst_structure_new ("application/x-gst-dvd",
      "event", G_TYPE_STRING, "dvd-spu-clut-change", NULL);

  /* Create a separate field for each value in the table. */
  for (n = 0; n < 16; n++) {
    guint32 color = 0;
    if (n < 4) {
      gst_kate_spu_rgb2yuv (kp->colors[n].r, kp->colors[n].g, kp->colors[n].b,
          &y, &u, &v);
      color = (y << 16) | (v << 8) | u;
    }
    g_snprintf (name, sizeof (name), "clut%02d", n);
    gst_structure_set (structure, name, G_TYPE_INT, (int) color, NULL);
  }

  /* Create the DVD event and put the structure into it. */
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure);

  GST_LOG_OBJECT (kd, "preparing clut change event %" GST_PTR_FORMAT, event);
  gst_pad_push_event (kd->srcpad, event);
}

GstBuffer *
gst_kate_spu_encode_spu (GstKateDec * kd, const kate_event * ev)
{
  kate_tracker kin;
  unsigned char *bytes = NULL;
  size_t nbytes = 0;
  GstBuffer *buffer = NULL;
  int ret;
  int ocw, och;
  int top, left, right, bottom;
  int pass, line, row;
  int lines_offset[2];
  int first_commands_offset, second_commands_offset;
  int nybble_count;
  const kate_bitmap *kb;
  const kate_palette *kp;
  int palette[4];
  int delay;

  /* we need a region, a bitmap, and a palette */
  if (!ev || !ev->region || !ev->bitmap || !ev->palette)
    return NULL;

  kb = ev->bitmap;
  kp = ev->palette;

  /* these need particular properties */
  if (kb->type != kate_bitmap_type_paletted || kb->bpp != 2)
    return NULL;
  if (kp->ncolors != 4)
    return NULL;

  ret = kate_tracker_init (&kin, ev->ki, ev);
  if (ret < 0) {
    GST_WARNING_OBJECT (kd, "Failed to initialize kate tracker");
    return NULL;
  }

  ocw = ev->ki->original_canvas_width;
  och = ev->ki->original_canvas_height;
  ret = kate_tracker_update (&kin, (kate_float) 0, ocw, och, 0, 0, ocw, och);
  if (ret < 0)
    goto error;

  if (kin.has.region) {
    top = (int) (kin.region_y + (kate_float) 0.5);
    left = (int) (kin.region_x + (kate_float) 0.5);
  } else {
    GST_WARNING_OBJECT (kd,
        "No region information to place SPU, placing at 0 0");
    top = left = 0;
  }
  right = left + kb->width - 1;
  bottom = top + kb->height - 1;

  /* Allocate space to build the SPU */
  bytes = g_malloc (MAX_SPU_SIZE);
  if (G_UNLIKELY (!bytes)) {
    GST_WARNING_OBJECT (kd,
        "Failed to allocate %" G_GSIZE_FORMAT " byte buffer", nbytes);
    goto error;
  }
  nbytes = 4;
  nybble_count = 0;

#define CHKBUFSPC(nybbles) \
  do { \
    if ((nbytes + (nybbles + nybble_count + 1) / 2) > MAX_SPU_SIZE) { \
      GST_WARNING_OBJECT (kd, "Not enough space in SPU buffer"); \
      goto error; \
    } \
  } while(0)

  /* encode lines */
  for (pass = 0; pass <= 1; ++pass) {
    lines_offset[pass] = nbytes;
    for (line = pass; line < bottom - top + 1; line += 2) {
      const unsigned char *ptr = kb->pixels + line * kb->width;
      for (row = 0; row < kb->width;) {
        int run = 1;
        while (row + run < kb->width && run < 255 && ptr[row + run] == ptr[row])
          ++run;
        if (run >= 63 && row + run == kb->width) {
          /* special end of line marker */
          CHKBUFSPC (4);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, 0);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, 0);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, 0);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, ptr[row]);
        } else if (run >= 1 && run <= 3) {
          CHKBUFSPC (1);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++,
              (run << 2) | ptr[row]);
        } else if (run <= 15) {
          CHKBUFSPC (2);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, run >> 2);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++,
              ((run & 3) << 2) | ptr[row]);
        } else if (run <= 63) {
          CHKBUFSPC (3);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, 0);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, run >> 2);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++,
              ((run & 3) << 2) | ptr[row]);
        } else {
          CHKBUFSPC (4);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, 0);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, (run >> 6));
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++,
              (run >> 2) & 0xf);
          gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++,
              ((run & 3) << 2) | ptr[row]);
        }
        row += run;
      }
      if (nybble_count & 1) {
        CHKBUFSPC (1);
        gst_kate_spu_add_nybble (bytes, nbytes, nybble_count++, 0);
      }
      nbytes += nybble_count / 2;
      nybble_count = 0;
    }
  }
  first_commands_offset = nbytes;

  gst_kate_spu_make_palette (kd, palette, kp);

  /* Commands header */
  CHKBUFSPC (4 * 2);
  bytes[nbytes++] = 0;
  bytes[nbytes++] = 0;
  /* link to next command chunk will be filled later, when we know where it is */
  bytes[nbytes++] = 0;
  bytes[nbytes++] = 0;

  CHKBUFSPC (3 * 2);
  bytes[nbytes++] = SPU_CMD_SET_COLOR;
  bytes[nbytes++] = (palette[3] << 4) | palette[2];
  bytes[nbytes++] = (palette[1] << 4) | palette[0];

  CHKBUFSPC (3 * 2);
  bytes[nbytes++] = SPU_CMD_SET_ALPHA;
  bytes[nbytes++] =
      ((kp->colors[palette[3]].a / 17) << 4) | (kp->colors[palette[2]].a / 17);
  bytes[nbytes++] =
      ((kp->colors[palette[1]].a / 17) << 4) | (kp->colors[palette[0]].a / 17);

  CHKBUFSPC (7 * 2);
  bytes[nbytes++] = SPU_CMD_SET_DAREA;
  bytes[nbytes++] = left >> 4;
  bytes[nbytes++] = ((left & 0xf) << 4) | (right >> 8);
  bytes[nbytes++] = right & 0xff;
  bytes[nbytes++] = top >> 4;
  bytes[nbytes++] = ((top & 0xf) << 4) | (bottom >> 8);
  bytes[nbytes++] = bottom & 0xff;

  CHKBUFSPC (5 * 2);
  bytes[nbytes++] = SPU_CMD_DSPXA;
  bytes[nbytes++] = (lines_offset[0] >> 8) & 0xff;
  bytes[nbytes++] = lines_offset[0] & 0xff;
  bytes[nbytes++] = (lines_offset[1] >> 8) & 0xff;
  bytes[nbytes++] = lines_offset[1] & 0xff;

  CHKBUFSPC (1 * 2);
  bytes[nbytes++] = SPU_CMD_DSP;

  CHKBUFSPC (1 * 2);
  bytes[nbytes++] = SPU_CMD_END;

  /* stop display chunk */
  CHKBUFSPC (4 * 2);
  second_commands_offset = nbytes;
  bytes[first_commands_offset + 2] = (second_commands_offset >> 8) & 0xff;
  bytes[first_commands_offset + 3] = second_commands_offset & 0xff;
  delay = GST_KATE_GST_TO_STM (ev->end_time - ev->start_time);
  bytes[nbytes++] = (delay >> 8) & 0xff;
  bytes[nbytes++] = delay & 0xff;
  /* close the loop by linking back to self */
  bytes[nbytes++] = (second_commands_offset >> 8) & 0xff;
  bytes[nbytes++] = second_commands_offset & 0xff;

  CHKBUFSPC (1 * 2);
  bytes[nbytes++] = SPU_CMD_STP_DSP;

  CHKBUFSPC (1 * 2);
  bytes[nbytes++] = SPU_CMD_END;

  /* Now that we know the size of the SPU, update the size and pointers */
  bytes[0] = (nbytes >> 8) & 0xff;
  bytes[1] = nbytes & 0xff;
  bytes[2] = (first_commands_offset >> 8) & 0xff;
  bytes[3] = first_commands_offset & 0xff;

  /* Create a buffer with those values */
  buffer = gst_buffer_new_wrapped (bytes, nbytes);
  if (G_UNLIKELY (!buffer)) {
    GST_WARNING_OBJECT (kd,
        "Failed to allocate %" G_GSIZE_FORMAT " byte buffer", nbytes);
    goto error;
  }
  GST_BUFFER_OFFSET_END (buffer) = GST_SECOND * (ev->end_time);
  GST_BUFFER_OFFSET (buffer) = GST_SECOND * (ev->start_time);
  GST_BUFFER_TIMESTAMP (buffer) = GST_SECOND * (ev->start_time);
  GST_BUFFER_DURATION (buffer) = GST_SECOND * (ev->end_time - ev->start_time);

  GST_DEBUG_OBJECT (kd, "SPU uses %" G_GSIZE_FORMAT " bytes", nbytes);

  kate_tracker_clear (&kin);
  return buffer;

error:
  kate_tracker_clear (&kin);
  g_free (bytes);
  return NULL;
}
