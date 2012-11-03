/*
 * Interplay MVE video encoder (16 bit)
 * Copyright (C) 2006 Jens Granseuer <jensgr@gmx.net>
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
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include "gstmvemux.h"

typedef struct _GstMveEncoderData GstMveEncoderData;
typedef struct _GstMveEncoding GstMveEncoding;
typedef struct _GstMveApprox GstMveApprox;
typedef struct _GstMveQuant GstMveQuant;

#define MVE_RMASK   0x7c00
#define MVE_GMASK   0x03e0
#define MVE_BMASK   0x001f
#define MVE_RSHIFT  10
#define MVE_GSHIFT  5
#define MVE_BSHIFT  0

#define MVE_RVAL(p)     (((p) & MVE_RMASK) >> MVE_RSHIFT)
#define MVE_GVAL(p)     (((p) & MVE_GMASK) >> MVE_GSHIFT)
#define MVE_BVAL(p)     (((p) & MVE_BMASK) >> MVE_BSHIFT)
#define MVE_COL(r,g,b)  (((r) << MVE_RSHIFT) | ((g) << MVE_GSHIFT) | ((b) << MVE_BSHIFT))

struct _GstMveEncoderData
{
  GstMveMux *mve;
  /* current position in frame */
  guint16 x, y;

  /* commonly used quantization results
     (2 and 4 colors) for the current block */
  guint16 q2block[64];
  guint16 q2colors[2];
  guint32 q2error;
  gboolean q2available;

  guint16 q4block[64];
  guint16 q4colors[4];
  guint32 q4error;
  gboolean q4available;
};

struct _GstMveEncoding
{
  guint8 opcode;
  guint8 size;
    guint32 (*approx) (GstMveEncoderData * enc, const guint16 * src,
      GstMveApprox * res);
};

#define MVE_APPROX_MAX_ERROR  G_MAXUINT32

struct _GstMveApprox
{
  guint32 error;
  guint8 type;
  guint8 data[128];             /* max 128 bytes encoded per block */
  guint16 block[64];            /* block in final image */
};

struct _GstMveQuant
{
  guint16 col;
  guint16 r_total, g_total, b_total;
  guint8 r, g, b;
  guint8 hits, hits_last;
  guint32 max_error;
  guint16 max_miss;
};

#define mve_median(mve, src) mve_median_sub ((mve), (src), 8, 8, 0)
#define mve_color_dist(c1, c2) \
        mve_color_dist_rgb (MVE_RVAL (c1), MVE_GVAL (c1), MVE_BVAL (c1), \
                            MVE_RVAL (c2), MVE_GVAL (c2), MVE_BVAL (c2));

/* comparison function for qsort() */
static int
mve_comp_solution (const void *a, const void *b)
{
  const GArray *aa = *((GArray **) a);
  const GArray *bb = *((GArray **) b);

  if (aa->len <= 1)
    return G_MAXINT;
  else if (bb->len <= 1)
    return G_MININT;
  else
    return g_array_index (aa, GstMveApprox, aa->len - 2).error -
        g_array_index (bb, GstMveApprox, bb->len - 2).error;
}

static inline guint32
mve_color_dist_rgb (guint8 r1, guint8 g1, guint8 b1,
    guint8 r2, guint8 g2, guint8 b2)
{
  /* euclidean distance (minus sqrt) */
  gint dr = r1 - r2;
  gint dg = g1 - g2;
  gint db = b1 - b2;

  return dr * dr + dg * dg + db * db;
}

/* compute average color in a sub-block */
static guint16
mve_median_sub (const GstMveMux * mve, const guint16 * src, guint w, guint h,
    guint n)
{
  guint x, y;
  const guint max = w * h, max2 = max >> 1;
  guint32 r_total = max2, g_total = max2, b_total = max2;

  src += ((n * w) % 8) + (((n * (8 - h)) / (12 - w)) * h * mve->width);

  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      r_total += MVE_RVAL (src[x]);
      g_total += MVE_GVAL (src[x]);
      b_total += MVE_BVAL (src[x]);
    }
    src += mve->width;
  }

  return MVE_COL (r_total / max, g_total / max, b_total / max);
}

static void
mve_quant_init (const GstMveMux * mve, GstMveQuant * q, guint n_clusters,
    const guint16 * data, guint w, guint h)
{
  guint i;
  guint x, y;
  guint16 cols[4];
  guint16 val[2];

  /* init first cluster with lowest (darkest), second with highest (lightest)
     color. if we need 4 clusters, fill in first and last color in the block
     and hope they make for a good distribution */
  cols[0] = cols[1] = cols[2] = data[0];
  cols[3] = data[(h - 1) * mve->width + w - 1];

  /* favour red over green and blue */
  val[0] = val[1] =
      (MVE_RVAL (data[0]) << 1) + MVE_GVAL (data[0]) + MVE_BVAL (data[0]);

  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      guint16 c = data[x];

      if ((c != cols[0]) && (c != cols[1])) {
        guint v = (MVE_RVAL (c) << 1) + MVE_GVAL (c) + MVE_BVAL (c);

        if (v < val[0]) {
          val[0] = v;
          cols[0] = c;
        } else if (v > val[1]) {
          val[1] = v;
          cols[1] = c;
        }
      }
    }
    data += mve->width;
  }

  for (i = 0; i < n_clusters; ++i) {
    q[i].col = cols[i];
    q[i].r = MVE_RVAL (cols[i]);
    q[i].g = MVE_GVAL (cols[i]);
    q[i].b = MVE_BVAL (cols[i]);
    q[i].r_total = q[i].g_total = q[i].b_total = 0;
    q[i].hits = q[i].hits_last = 0;
    q[i].max_error = 0;
    q[i].max_miss = 0;
  }
}

static gboolean
mve_quant_update_clusters (GstMveQuant * q, guint n_clusters)
{
  gboolean changed = FALSE;
  guint i;

  for (i = 0; i < n_clusters; ++i) {
    if (q[i].hits > 0) {
      guint16 means = MVE_COL ((q[i].r_total + q[i].hits / 2) / q[i].hits,
          (q[i].g_total + q[i].hits / 2) / q[i].hits,
          (q[i].b_total + q[i].hits / 2) / q[i].hits);

      if ((means != q[i].col) || (q[i].hits != q[i].hits_last))
        changed = TRUE;

      q[i].col = means;
      q[i].r_total = q[i].g_total = q[i].b_total = 0;
    } else {
      guint j;
      guint32 max_err = 0;
      GstMveQuant *worst = NULL;

      /* try to replace unused cluster with a better representative */
      for (j = 0; j < n_clusters; ++j) {
        if (q[j].max_error > max_err) {
          worst = &q[j];
          max_err = worst->max_error;
        }
      }
      if (worst) {
        q[i].col = worst->max_miss;
        worst->max_error = 0;
        changed = TRUE;
      }
    }

    q[i].r = MVE_RVAL (q[i].col);
    q[i].g = MVE_GVAL (q[i].col);
    q[i].b = MVE_BVAL (q[i].col);
    q[i].hits_last = q[i].hits;
    q[i].hits = 0;
  }
  for (i = 0; i < n_clusters; ++i) {
    q[i].max_error = 0;
  }

  return changed;
}

/* quantize a sub-block using a k-means algorithm */
static guint32
mve_quantize (const GstMveMux * mve, const guint16 * src,
    guint w, guint h, guint n, guint ncols, guint16 * scratch, guint16 * cols)
{
  guint x, y, i;
  GstMveQuant q[4];
  const guint16 *data;
  guint16 *dest;
  guint32 error;

  g_assert (n <= 4 && ncols <= 4);

  src += ((n * w) % 8) + (((n * (8 - h)) / (12 - w)) * h * mve->width);
  scratch += ((n * w) % 8) + (((n * (8 - h)) / (12 - w)) * h * 8);

  mve_quant_init (mve, q, ncols, src, w, h);

  do {
    data = src;
    dest = scratch;
    error = 0;

    /* for each pixel find the closest cluster */
    for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
        guint16 c = data[x];
        guint8 r = MVE_RVAL (c), g = MVE_GVAL (c), b = MVE_BVAL (c);
        guint32 minerr = MVE_APPROX_MAX_ERROR, err;
        GstMveQuant *best = NULL;

        for (i = 0; i < ncols; ++i) {
          err = mve_color_dist_rgb (r, g, b, q[i].r, q[i].g, q[i].b);

          if (err < minerr) {
            minerr = err;
            best = &q[i];
          }
        }

        if (G_UNLIKELY (!best))
          continue;

        ++best->hits;
        best->r_total += r;
        best->g_total += g;
        best->b_total += b;

        if (minerr > best->max_error) {
          best->max_error = minerr;
          best->max_miss = c;
        }

        error += minerr;
        dest[x] = best->col;
      }
      data += mve->width;
      dest += 8;
    }
  } while (mve_quant_update_clusters (q, ncols));

  /* fill cols array with result colors */
  for (i = 0; i < ncols; ++i)
    cols[i] = q[i].col;

  return error;
}

static guint32
mve_block_error (const GstMveMux * mve, const guint16 * b1, const guint16 * b2,
    guint32 threshold)
{
  /* compute error between two blocks in a frame */
  guint32 e = 0;
  guint x, y;

  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 8; ++x) {
      e += mve_color_dist (*b1, *b2);

      /* using a threshold to return early gives a huge performance bonus */
      if (e >= threshold)
        return MVE_APPROX_MAX_ERROR;
      ++b1;
      ++b2;
    }

    b1 += mve->width - 8;
    b2 += mve->width - 8;
  }

  return e;
}

static guint32
mve_block_error_packed (const GstMveMux * mve, const guint16 * block,
    const guint16 * scratch)
{
  /* compute error between a block in a frame and a (continuous) scratch pad */
  guint32 e = 0;
  guint x, y;

  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 8; ++x) {
      e += mve_color_dist (block[x], scratch[x]);
    }
    block += mve->width;
    scratch += 8;
  }

  return e;
}

static void
mve_store_block (const GstMveMux * mve, const guint16 * block,
    guint16 * scratch)
{
  /* copy block from frame to a (continuous) scratch pad */
  guint y;

  for (y = 0; y < 8; ++y) {
    memcpy (scratch, block, 16);
    block += mve->width;
    scratch += 8;
  }
}

static void
mve_restore_block (const GstMveMux * mve, guint16 * block,
    const guint16 * scratch)
{
  /* copy block from scratch pad to frame */
  guint y;

  for (y = 0; y < 8; ++y) {
    memcpy (block, scratch, 16);
    block += mve->width;
    scratch += 8;
  }
}


static guint32
mve_try_vector (GstMveEncoderData * enc, const guint16 * src,
    const guint16 * frame, gint pn, GstMveApprox * apx)
{
  /* try to locate a similar 8x8 block in the given frame using a motion vector */
  guint i;
  gint dx, dy;
  gint fx, fy;
  guint32 err;

  apx->error = MVE_APPROX_MAX_ERROR;

  for (i = 0; i < 256; ++i) {
    if (i < 56) {
      dx = 8 + (i % 7);
      dy = i / 7;
    } else {
      dx = -14 + ((i - 56) % 29);
      dy = 8 + ((i - 56) / 29);
    }

    fx = enc->x + dx * pn;
    fy = enc->y + dy * pn;

    if ((fx >= 0) && (fy >= 0) && (fx + 8 <= enc->mve->width)
        && (fy + 8 <= enc->mve->height)) {
      err =
          mve_block_error (enc->mve, src, frame + fy * enc->mve->width + fx,
          apx->error);
      if (err < apx->error) {
        apx->data[0] = i;
        mve_store_block (enc->mve, frame + fy * enc->mve->width + fx,
            apx->block);
        apx->error = err;
        if (err == 0)
          return 0;
      }
    }
  }

  return apx->error;
}

static guint32
mve_encode_0x0 (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* copy a block from the last frame (0 bytes) */
  if (enc->mve->last_frame == NULL)
    return MVE_APPROX_MAX_ERROR;

  mve_store_block (enc->mve,
      ((guint16 *) GST_BUFFER_DATA (enc->mve->last_frame)) +
      enc->y * enc->mve->width + enc->x, apx->block);
  apx->error = mve_block_error_packed (enc->mve, src, apx->block);
  return apx->error;
}

static guint32
mve_encode_0x1 (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* copy a block from the second to last frame (0 bytes) */
  if (enc->mve->second_last_frame == NULL)
    return MVE_APPROX_MAX_ERROR;

  mve_store_block (enc->mve,
      ((guint16 *) GST_BUFFER_DATA (enc->mve->second_last_frame)) +
      enc->y * enc->mve->width + enc->x, apx->block);
  apx->error = mve_block_error_packed (enc->mve, src, apx->block);
  return apx->error;
}

static guint32
mve_encode_0x2 (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* copy block from 2 frames ago using a motion vector (1 byte) */
  if (enc->mve->quick_encoding || enc->mve->second_last_frame == NULL)
    return MVE_APPROX_MAX_ERROR;

  apx->error = mve_try_vector (enc, src,
      (guint16 *) GST_BUFFER_DATA (enc->mve->second_last_frame), 1, apx);
  return apx->error;
}

static guint32
mve_encode_0x3 (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* copy 8x8 block from current frame from an up/left block (1 byte) */
  if (enc->mve->quick_encoding)
    return MVE_APPROX_MAX_ERROR;

  apx->error = mve_try_vector (enc, src,
      src - enc->mve->width * enc->y - enc->x, -1, apx);
  return apx->error;
}

static guint32
mve_encode_0x4 (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* copy a block from previous frame using a motion vector (-8/-8 to +7/+7) (1 byte) */
  const GstMveMux *mve = enc->mve;
  guint32 err;
  const guint16 *frame;
  gint x1, x2, xi, y1, y2, yi;

  if (mve->last_frame == NULL)
    return MVE_APPROX_MAX_ERROR;

  frame = (guint16 *) GST_BUFFER_DATA (mve->last_frame);

  x1 = enc->x - 8;
  x2 = enc->x + 7;
  if (x1 < 0)
    x1 = 0;
  else if (x2 + 8 > mve->width)
    x2 = mve->width - 8;

  y1 = enc->y - 8;
  y2 = enc->y + 7;
  if (y1 < 0)
    y1 = 0;
  else if (y2 + 8 > mve->height)
    y2 = mve->height - 8;

  apx->error = MVE_APPROX_MAX_ERROR;

  for (yi = y1; yi <= y2; ++yi) {
    guint yoff = yi * mve->width;

    for (xi = x1; xi <= x2; ++xi) {
      err = mve_block_error (mve, src, frame + yoff + xi, apx->error);
      if (err < apx->error) {
        apx->data[0] = ((xi - enc->x + 8) & 0xF) | ((yi - enc->y + 8) << 4);
        mve_store_block (mve, frame + yoff + xi, apx->block);
        apx->error = err;
        if (err == 0)
          return 0;
      }
    }
  }

  return apx->error;
}

static guint32
mve_encode_0x5 (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* copy a block from previous frame using a motion vector
     (-128/-128 to +127/+127) (2 bytes) */
  const GstMveMux *mve = enc->mve;
  guint32 err;
  const guint16 *frame;
  gint x1, x2, xi, y1, y2, yi;

  if (mve->quick_encoding || mve->last_frame == NULL)
    return MVE_APPROX_MAX_ERROR;

  frame = (guint16 *) GST_BUFFER_DATA (mve->last_frame);

  x1 = enc->x - 128;
  x2 = enc->x + 127;
  if (x1 < 0)
    x1 = 0;
  if (x2 + 8 > mve->width)
    x2 = mve->width - 8;

  y1 = enc->y - 128;
  y2 = enc->y + 127;
  if (y1 < 0)
    y1 = 0;
  if (y2 + 8 > mve->height)
    y2 = mve->height - 8;

  apx->error = MVE_APPROX_MAX_ERROR;

  for (yi = y1; yi <= y2; ++yi) {
    gint yoff = yi * mve->width;

    for (xi = x1; xi <= x2; ++xi) {
      err = mve_block_error (mve, src, frame + yoff + xi, apx->error);
      if (err < apx->error) {
        apx->data[0] = xi - enc->x;
        apx->data[1] = yi - enc->y;
        mve_store_block (mve, frame + yoff + xi, apx->block);
        apx->error = err;
        if (err == 0)
          return 0;
      }
    }
  }

  return apx->error;
}

static guint32
mve_encode_0x7a (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 2-color encoding for 2x2 solid blocks (6 bytes) */
  guint16 p[2];
  guint16 mean;
  guint32 e1, e2;
  guint x, y;
  guint8 r[2], g[2], b[2], rb, gb, bb;
  guint16 *block = apx->block;
  guint16 mask = 0x0001;
  guint16 flags = 0;

  /* calculate mean colors for the entire block */
  if (!enc->q2available) {
    enc->q2error =
        mve_quantize (enc->mve, src, 8, 8, 0, 2, enc->q2block, enc->q2colors);
    enc->q2available = TRUE;
  }

  /* p[0] & 0x8000 */
  GST_WRITE_UINT16_LE (&apx->data[0], enc->q2colors[0] | 0x8000);
  GST_WRITE_UINT16_LE (&apx->data[2], enc->q2colors[1]);

  for (x = 0; x < 2; ++x) {
    r[x] = MVE_RVAL (enc->q2colors[x]);
    g[x] = MVE_GVAL (enc->q2colors[x]);
    b[x] = MVE_BVAL (enc->q2colors[x]);
  }

  /* calculate mean colors for each 2x2 block and map to global colors */
  for (y = 0; y < 4; ++y) {
    for (x = 0; x < 4; ++x, mask <<= 1) {
      p[0] = src[enc->mve->width];
      p[1] = src[enc->mve->width + 1];

      rb = (MVE_RVAL (src[0]) + MVE_RVAL (src[1]) + MVE_RVAL (p[0]) +
          MVE_RVAL (p[1]) + 2) / 4;
      gb = (MVE_GVAL (src[0]) + MVE_GVAL (src[1]) + MVE_GVAL (p[0]) +
          MVE_GVAL (p[1]) + 2) / 4;
      bb = (MVE_BVAL (src[0]) + MVE_BVAL (src[1]) + MVE_BVAL (p[0]) +
          MVE_BVAL (p[1]) + 2) / 4;

      e1 = mve_color_dist_rgb (rb, gb, bb, r[0], g[0], b[0]);
      e2 = mve_color_dist_rgb (rb, gb, bb, r[1], g[1], b[1]);

      if (e1 > e2) {
        mean = enc->q2colors[1];
        flags |= mask;
      } else {
        mean = enc->q2colors[0];
      }

      block[0] = block[1] = block[8] = block[9] = mean;

      src += 2;
      block += 2;
    }
    src += (enc->mve->width * 2) - 8;
    block += 8;
  }

  apx->data[4] = flags & 0x00FF;
  apx->data[5] = (flags & 0xFF00) >> 8;

  apx->error =
      mve_block_error_packed (enc->mve, src - enc->mve->width * 8, apx->block);
  return apx->error;
}

static guint32
mve_encode_0x7b (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* generic 2-color encoding (12 bytes) */
  guint x, y;
  guint8 *data = apx->data;
  guint16 *block = apx->block;

  if (!enc->q2available) {
    enc->q2error =
        mve_quantize (enc->mve, src, 8, 8, 0, 2, enc->q2block, enc->q2colors);
    enc->q2available = TRUE;
  }

  memcpy (block, enc->q2block, 128);

  /* !(p[0] & 0x8000) */
  GST_WRITE_UINT16_LE (&data[0], enc->q2colors[0] & ~0x8000);
  GST_WRITE_UINT16_LE (&data[2], enc->q2colors[1]);
  data += 4;

  for (y = 0; y < 8; ++y) {
    guint8 flags = 0;

    for (x = 0x01; x <= 0x80; x <<= 1) {
      if (*block == enc->q2colors[1])
        flags |= x;
      ++block;
    }
    *data++ = flags;
  }

  apx->error = enc->q2error;
  return apx->error;
}

static guint32
mve_encode_0x8a (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 2-color encoding for top and bottom half (16 bytes) */
  guint16 cols[2];
  guint32 flags;
  guint i, x, y, shifter;
  guint16 *block = apx->block;
  guint8 *data = apx->data;

  apx->error = 0;

  for (i = 0; i < 2; ++i) {
    apx->error += mve_quantize (enc->mve, src, 8, 4, i, 2, apx->block, cols);

    flags = 0;
    shifter = 0;

    /* p0 & 0x8000 && p2 & 0x8000 */
    GST_WRITE_UINT16_LE (&data[0], cols[0] | 0x8000);
    GST_WRITE_UINT16_LE (&data[2], cols[1]);

    for (y = 0; y < 4; ++y) {
      for (x = 0; x < 8; ++x, ++shifter) {
        if (block[x] == cols[1])
          flags |= 1 << shifter;
      }
      block += 8;
    }
    data[4] = flags & 0x000000FF;
    data[5] = (flags & 0x0000FF00) >> 8;
    data[6] = (flags & 0x00FF0000) >> 16;
    data[7] = (flags & 0xFF000000) >> 24;
    data += 8;
  }

  return apx->error;
}

static guint32
mve_encode_0x8b (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 2-color encoding for left and right half (16 bytes) */
  guint16 cols[2];
  guint32 flags;
  guint i, x, y, shifter;
  guint16 *block = apx->block;
  guint8 *data = apx->data;

  apx->error = 0;

  for (i = 0; i < 2; ++i) {
    apx->error += mve_quantize (enc->mve, src, 4, 8, i, 2, apx->block, cols);

    flags = 0;
    shifter = 0;

    /* p0 & 0x8000 && !(p2 & 0x8000) */
    GST_WRITE_UINT16_LE (&data[0], (cols[0] & ~0x8000) | (0x8000 * (i ^ 1)));
    GST_WRITE_UINT16_LE (&data[2], cols[1]);

    for (y = 0; y < 8; ++y) {
      for (x = 0; x < 4; ++x, ++shifter) {
        if (block[x] == cols[1])
          flags |= 1 << shifter;
      }
      block += 8;
    }

    data[4] = flags & 0x000000FF;
    data[5] = (flags & 0x0000FF00) >> 8;
    data[6] = (flags & 0x00FF0000) >> 16;
    data[7] = (flags & 0xFF000000) >> 24;
    data += 8;
    block = apx->block + 4;
  }

  return apx->error;
}

static guint32
mve_encode_0x8c (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 2-color encoding for each 4x4 quadrant (24 bytes) */
  guint16 cols[2];
  guint16 flags;
  guint i, x, y, shifter;
  guint16 *block;
  guint8 *data = apx->data;

  apx->error = 0;

  for (i = 0; i < 4; ++i) {
    apx->error +=
        mve_quantize (enc->mve, src, 4, 4, ((i & 1) << 1) | ((i & 2) >> 1), 2,
        apx->block, cols);

    /* !(p0 & 0x8000) */
    GST_WRITE_UINT16_LE (&data[0], cols[0] & ~0x8000);
    GST_WRITE_UINT16_LE (&data[2], cols[1]);

    block = apx->block + ((i / 2) * 4) + ((i % 2) * 32);
    flags = 0;
    shifter = 0;

    for (y = 0; y < 4; ++y) {
      for (x = 0; x < 4; ++x, ++shifter) {
        if (block[x] == cols[1])
          flags |= 1 << shifter;
      }
      block += 8;
    }

    data[4] = flags & 0x00FF;
    data[5] = (flags & 0xFF00) >> 8;
    data += 6;
  }

  return apx->error;
}

static guint32
mve_encode_0x9a (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color encoding for 2x2 solid blocks (12 bytes) */
  guint16 p[2];
  guint32 e, emin;
  guint i, x, y, mean = 0;
  guint8 r[4], g[4], b[4], rb, gb, bb;
  guint16 *block = apx->block;
  guint shifter = 0;
  guint32 flags = 0;

  /* calculate mean colors for the entire block */
  if (!enc->q4available) {
    enc->q4error =
        mve_quantize (enc->mve, src, 8, 8, 0, 4, enc->q4block, enc->q4colors);
    enc->q4available = TRUE;
  }

  /* !(p[0] & 0x8000) && p[2] & 0x8000 */
  GST_WRITE_UINT16_LE (&apx->data[0], enc->q4colors[0] & ~0x8000);
  GST_WRITE_UINT16_LE (&apx->data[2], enc->q4colors[1]);
  GST_WRITE_UINT16_LE (&apx->data[4], enc->q4colors[2] | 0x8000);
  GST_WRITE_UINT16_LE (&apx->data[6], enc->q4colors[3]);

  for (i = 0; i < 4; ++i) {
    r[i] = MVE_RVAL (enc->q4colors[i]);
    g[i] = MVE_GVAL (enc->q4colors[i]);
    b[i] = MVE_BVAL (enc->q4colors[i]);
  }

  /* calculate mean colors for each 2x2 block and map to global colors */
  for (y = 0; y < 4; ++y) {
    for (x = 0; x < 4; ++x, shifter += 2) {
      p[0] = src[enc->mve->width];
      p[1] = src[enc->mve->width + 1];

      rb = (MVE_RVAL (src[0]) + MVE_RVAL (src[1]) + MVE_RVAL (p[0]) +
          MVE_RVAL (p[1]) + 2) / 4;
      gb = (MVE_GVAL (src[0]) + MVE_GVAL (src[1]) + MVE_GVAL (p[0]) +
          MVE_GVAL (p[1]) + 2) / 4;
      bb = (MVE_BVAL (src[0]) + MVE_BVAL (src[1]) + MVE_BVAL (p[0]) +
          MVE_BVAL (p[1]) + 2) / 4;

      emin = MVE_APPROX_MAX_ERROR;
      for (i = 0; i < 4; ++i) {
        e = mve_color_dist_rgb (rb, gb, bb, r[i], g[i], b[i]);
        if (e < emin) {
          emin = e;
          mean = i;
        }
      }

      flags |= mean << shifter;
      block[0] = block[1] = block[8] = block[9] = enc->q4colors[mean];

      src += 2;
      block += 2;
    }
    src += (enc->mve->width * 2) - 8;
    block += 8;
  }

  apx->data[8] = flags & 0x000000FF;
  apx->data[9] = (flags & 0x0000FF00) >> 8;
  apx->data[10] = (flags & 0x00FF0000) >> 16;
  apx->data[11] = (flags & 0xFF000000) >> 24;

  apx->error =
      mve_block_error_packed (enc->mve, src - 8 * enc->mve->width, apx->block);
  return apx->error;
}

static guint32
mve_encode_0x9b (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color encoding for 2x1 solid blocks (16 bytes) */
  guint32 e, emin;
  guint i, x, y, mean = 0;
  guint8 r[4], g[4], b[4], rb, gb, bb;
  guint8 *data = apx->data;
  guint16 *block = apx->block;
  guint shifter = 0;
  guint32 flags = 0;

  /* calculate mean colors for the entire block */
  if (!enc->q4available) {
    enc->q4error =
        mve_quantize (enc->mve, src, 8, 8, 0, 4, enc->q4block, enc->q4colors);
    enc->q4available = TRUE;
  }

  /* p[0] & 0x8000 && !(p[2] & 0x8000) */
  GST_WRITE_UINT16_LE (&data[0], enc->q4colors[0] | 0x8000);
  GST_WRITE_UINT16_LE (&data[2], enc->q4colors[1]);
  GST_WRITE_UINT16_LE (&data[4], enc->q4colors[2] & ~0x8000);
  GST_WRITE_UINT16_LE (&data[6], enc->q4colors[3]);
  data += 8;

  for (i = 0; i < 4; ++i) {
    r[i] = MVE_RVAL (enc->q4colors[i]);
    g[i] = MVE_GVAL (enc->q4colors[i]);
    b[i] = MVE_BVAL (enc->q4colors[i]);
  }

  /* calculate mean colors for each 2x1 block and map to global colors */
  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 4; ++x, shifter += 2) {
      rb = (MVE_RVAL (src[0]) + MVE_RVAL (src[1]) + 1) / 2;
      gb = (MVE_GVAL (src[0]) + MVE_GVAL (src[1]) + 1) / 2;
      bb = (MVE_BVAL (src[0]) + MVE_BVAL (src[1]) + 1) / 2;

      emin = MVE_APPROX_MAX_ERROR;
      for (i = 0; i < 4; ++i) {
        e = mve_color_dist_rgb (rb, gb, bb, r[i], g[i], b[i]);
        if (e < emin) {
          emin = e;
          mean = i;
        }
      }

      flags |= mean << shifter;
      block[0] = block[1] = enc->q4colors[mean];

      src += 2;
      block += 2;
    }

    if ((y == 3) || (y == 7)) {
      data[0] = flags & 0x000000FF;
      data[1] = (flags & 0x0000FF00) >> 8;
      data[2] = (flags & 0x00FF0000) >> 16;
      data[3] = (flags & 0xFF000000) >> 24;
      data += 4;

      flags = 0;
      shifter = 0;
    }

    src += enc->mve->width - 8;
  }

  apx->error =
      mve_block_error_packed (enc->mve, src - 8 * enc->mve->width, apx->block);
  return apx->error;
}

static guint32
mve_encode_0x9c (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color encoding for 1x2 solid blocks (16 bytes) */
  guint16 p2;
  guint32 e, emin;
  guint i, x, y, mean = 0;
  guint8 r[4], g[4], b[4], rb, gb, bb;
  guint8 *data = apx->data;
  guint16 *block = apx->block;
  guint shifter = 0;
  guint32 flags = 0;

  /* calculate mean colors for the entire block */
  if (!enc->q4available) {
    enc->q4error =
        mve_quantize (enc->mve, src, 8, 8, 0, 4, enc->q4block, enc->q4colors);
    enc->q4available = TRUE;
  }

  /* p[0] & 0x8000 && p[2] & 0x8000 */
  GST_WRITE_UINT16_LE (&data[0], enc->q4colors[0] | 0x8000);
  GST_WRITE_UINT16_LE (&data[2], enc->q4colors[1]);
  GST_WRITE_UINT16_LE (&data[4], enc->q4colors[2] | 0x8000);
  GST_WRITE_UINT16_LE (&data[6], enc->q4colors[3]);
  data += 8;

  for (i = 0; i < 4; ++i) {
    r[i] = MVE_RVAL (enc->q4colors[i]);
    g[i] = MVE_GVAL (enc->q4colors[i]);
    b[i] = MVE_BVAL (enc->q4colors[i]);
  }

  /* calculate mean colors for each 1x2 block and map to global colors */
  for (y = 0; y < 4; ++y) {
    for (x = 0; x < 8; ++x, shifter += 2) {
      p2 = src[enc->mve->width];
      rb = (MVE_RVAL (src[0]) + MVE_RVAL (p2) + 1) / 2;
      gb = (MVE_GVAL (src[0]) + MVE_GVAL (p2) + 1) / 2;
      bb = (MVE_BVAL (src[0]) + MVE_BVAL (p2) + 1) / 2;

      emin = MVE_APPROX_MAX_ERROR;
      for (i = 0; i < 4; ++i) {
        e = mve_color_dist_rgb (rb, gb, bb, r[i], g[i], b[i]);
        if (e < emin) {
          emin = e;
          mean = i;
        }
      }

      flags |= mean << shifter;
      block[0] = block[8] = enc->q4colors[mean];

      ++src;
      ++block;
    }

    if ((y == 1) || (y == 3)) {
      data[0] = flags & 0x000000FF;
      data[1] = (flags & 0x0000FF00) >> 8;
      data[2] = (flags & 0x00FF0000) >> 16;
      data[3] = (flags & 0xFF000000) >> 24;
      data += 4;

      flags = 0;
      shifter = 0;
    }

    src += (enc->mve->width * 2) - 8;
    block += 8;
  }

  apx->error =
      mve_block_error_packed (enc->mve, src - 8 * enc->mve->width, apx->block);
  return apx->error;
}

static guint32
mve_encode_0x9d (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* generic 4-color encoding (24 bytes) */
  guint32 flags = 0;
  guint shifter = 0;
  guint i, x, y;
  guint8 *data = apx->data;
  guint16 *block = apx->block;

  if (!enc->q4available) {
    enc->q4error =
        mve_quantize (enc->mve, src, 8, 8, 0, 4, enc->q4block, enc->q4colors);
    enc->q4available = TRUE;
  }

  memcpy (block, enc->q4block, 128);

  /* !(p[0] & 0x8000) && !(p[2] & 0x8000) */
  GST_WRITE_UINT16_LE (&data[0], enc->q4colors[0] & ~0x8000);
  GST_WRITE_UINT16_LE (&data[2], enc->q4colors[1]);
  GST_WRITE_UINT16_LE (&data[4], enc->q4colors[2] & ~0x8000);
  GST_WRITE_UINT16_LE (&data[6], enc->q4colors[3]);
  data += 8;

  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 8; ++x, shifter += 2) {

      for (i = 0; i < 3; ++i) {
        if (*block == enc->q4colors[i])
          break;
      }

      flags |= i << shifter;
      ++block;
    }

    data[0] = flags & 0x000000FF;
    data[1] = (flags & 0x0000FF00) >> 8;
    data += 2;
    shifter = 0;
    flags = 0;
  }

  apx->error = enc->q4error;
  return apx->error;
}

static guint32
mve_encode_0xaa (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color encoding for top and bottom half (32 bytes) */
  guint16 cols[4];
  guint32 flags;
  guint i, j, x, y, shifter;
  guint16 *block = apx->block;
  guint8 *data = apx->data;

  apx->error = 0;

  for (i = 0; i < 2; ++i) {
    apx->error += mve_quantize (enc->mve, src, 8, 4, i, 4, apx->block, cols);

    flags = 0;
    shifter = 0;

    /* p0 & 0x8000 && p4 & 0x8000 */
    GST_WRITE_UINT16_LE (&data[0], cols[0] | 0x8000);
    GST_WRITE_UINT16_LE (&data[2], cols[1]);
    GST_WRITE_UINT16_LE (&data[4], cols[2]);
    GST_WRITE_UINT16_LE (&data[6], cols[3]);
    data += 8;

    for (y = 0; y < 4; ++y) {
      for (x = 0; x < 8; ++x, shifter += 2) {
        for (j = 0; j < 3; ++j) {
          if (block[x] == cols[j])
            break;
        }
        flags |= j << shifter;
      }
      block += 8;

      if ((y == 1) || (y == 3)) {
        data[0] = flags & 0x000000FF;
        data[1] = (flags & 0x0000FF00) >> 8;
        data[2] = (flags & 0x00FF0000) >> 16;
        data[3] = (flags & 0xFF000000) >> 24;
        data += 4;
        flags = 0;
        shifter = 0;
      }
    }
  }

  return apx->error;
}

static guint32
mve_encode_0xab (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color encoding for left and right half (32 bytes) */
  guint16 cols[4];
  guint32 flags;
  guint i, j, x, y, shifter;
  guint16 *block = apx->block;
  guint8 *data = apx->data;

  apx->error = 0;

  for (i = 0; i < 2; ++i) {
    apx->error += mve_quantize (enc->mve, src, 4, 8, i, 4, apx->block, cols);

    flags = 0;
    shifter = 0;

    /* p0 & 0x8000 && !(p4 & 0x8000) */
    GST_WRITE_UINT16_LE (&data[0], (cols[0] & ~0x8000) | (0x8000 * (i ^ 1)));
    GST_WRITE_UINT16_LE (&data[2], cols[1]);
    GST_WRITE_UINT16_LE (&data[4], cols[2]);
    GST_WRITE_UINT16_LE (&data[6], cols[3]);
    data += 8;

    for (y = 0; y < 8; ++y) {
      for (x = 0; x < 4; ++x, shifter += 2) {
        for (j = 0; j < 3; ++j) {
          if (block[x] == cols[j])
            break;
        }
        flags |= j << shifter;
      }
      block += 8;

      if ((y == 3) || (y == 7)) {
        data[0] = flags & 0x000000FF;
        data[1] = (flags & 0x0000FF00) >> 8;
        data[2] = (flags & 0x00FF0000) >> 16;
        data[3] = (flags & 0xFF000000) >> 24;
        data += 4;
        flags = 0;
        shifter = 0;
      }
    }
    block = apx->block + 4;
  }

  return apx->error;
}

static guint32
mve_encode_0xac (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color encoding for each 4x4 quadrant (48 bytes) */
  guint16 cols[4];
  guint32 flags;
  guint i, j, x, y, shifter;
  guint16 *block;
  guint8 *data = apx->data;

  apx->error = 0;

  for (i = 0; i < 4; ++i) {
    apx->error +=
        mve_quantize (enc->mve, src, 4, 4, ((i & 1) << 1) | ((i & 2) >> 1), 4,
        apx->block, cols);

    /* !(p0 & 0x8000) */
    GST_WRITE_UINT16_LE (&data[0], cols[0] & ~0x8000);
    GST_WRITE_UINT16_LE (&data[2], cols[1]);
    GST_WRITE_UINT16_LE (&data[4], cols[2]);
    GST_WRITE_UINT16_LE (&data[6], cols[3]);

    block = apx->block + ((i / 2) * 4) + ((i % 2) * 32);
    flags = 0;
    shifter = 0;

    for (y = 0; y < 4; ++y) {
      for (x = 0; x < 4; ++x, shifter += 2) {
        for (j = 0; j < 3; ++j) {
          if (block[x] == cols[j])
            break;
        }
        flags |= j << shifter;
      }
      block += 8;
    }

    data[8] = flags & 0x000000FF;
    data[9] = (flags & 0x0000FF00) >> 8;
    data[10] = (flags & 0x00FF0000) >> 16;
    data[11] = (flags & 0xFF000000) >> 24;
    data += 12;
  }

  return apx->error;
}

static guint32
mve_encode_0xb (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 64-color encoding (each pixel in block is a different color) (128 bytes) */
  guint i;

  apx->error = 0;

  mve_store_block (enc->mve, src, apx->block);
  for (i = 0; i < 64; ++i)
    GST_WRITE_UINT16_LE (&apx->data[i << 1], apx->block[i]);

  return 0;
}

static guint32
mve_encode_0xc (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 16-color block encoding: each 2x2 block is a different color (32 bytes) */
  guint i = 0, x, y;
  const guint w = enc->mve->width;
  guint16 r, g, b;

  /* calculate median color for each 2x2 block */
  for (y = 0; y < 4; ++y) {
    for (x = 0; x < 4; ++x) {
      r = MVE_RVAL (src[0]) + MVE_RVAL (src[1]) +
          MVE_RVAL (src[w]) + MVE_RVAL (src[w + 1]) + 2;
      g = MVE_GVAL (src[0]) + MVE_GVAL (src[1]) +
          MVE_GVAL (src[w]) + MVE_GVAL (src[w + 1]) + 2;
      b = MVE_BVAL (src[0]) + MVE_BVAL (src[1]) +
          MVE_BVAL (src[w]) + MVE_BVAL (src[w + 1]) + 2;
      apx->block[i] = apx->block[i + 1] = apx->block[i + 2] =
          apx->block[i + 3] = MVE_COL (r >> 2, g >> 2, b >> 2);
      GST_WRITE_UINT16_LE (&apx->data[i >> 1], apx->block[i]);

      i += 4;
      src += 2;
    }
    src += (w * 2) - 8;
  }

  apx->error = mve_block_error_packed (enc->mve, src - (8 * w), apx->block);
  return apx->error;
}

static guint32
mve_encode_0xd (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 4-color block encoding: each 4x4 block is a different color (8 bytes) */
  guint i, x, y;
  guint16 *block;

  /* calculate median color for each 4x4 block */
  for (i = 0; i < 4; ++i) {
    guint16 median =
        mve_median_sub (enc->mve, src, 4, 4, ((i & 1) << 1) | ((i & 2) >> 1));

    block = apx->block + ((i / 2) * 4) + ((i % 2) * 32);
    for (y = 0; y < 4; ++y) {
      for (x = 0; x < 4; ++x) {
        block[x] = median;
      }
      block += 8;
    }

    GST_WRITE_UINT16_LE (&apx->data[i << 1], median);
  }

  apx->error = mve_block_error_packed (enc->mve, src, apx->block);
  return apx->error;
}

static guint32
mve_encode_0xe (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 1-color encoding: the whole block is 1 solid color (2 bytes) */
  guint i;
  guint16 median = mve_median (enc->mve, src);

  for (i = 0; i < 64; ++i)
    apx->block[i] = median;

  apx->error = mve_block_error_packed (enc->mve, src, apx->block);
  GST_WRITE_UINT16_LE (apx->data, median);

  return apx->error;
}

static guint32
mve_encode_0xf (GstMveEncoderData * enc, const guint16 * src,
    GstMveApprox * apx)
{
  /* 2 colors dithered encoding (4 bytes) */
  guint i, x, y;
  guint32 r[2] = { 0 }, g[2] = {
  0}, b[2] = {
  0};
  guint16 col[2];

  /* find medians for both colors */
  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 8; x += 2) {
      guint16 p = src[x];

      r[y & 1] += MVE_RVAL (p);
      g[y & 1] += MVE_GVAL (p);
      b[y & 1] += MVE_BVAL (p);

      p = src[x + 1];
      r[(y & 1) ^ 1] += MVE_RVAL (p);
      g[(y & 1) ^ 1] += MVE_GVAL (p);
      b[(y & 1) ^ 1] += MVE_BVAL (p);
    }
    src += enc->mve->width;
  }
  col[0] = MVE_COL ((r[0] + 16) / 32, (g[0] + 16) / 32, (b[0] + 16) / 32);
  col[1] = MVE_COL ((r[1] + 16) / 32, (g[1] + 16) / 32, (b[1] + 16) / 32);

  /* store block after encoding */
  for (i = 0, y = 0; y < 8; ++y) {
    for (x = 0; x < 4; ++x) {
      apx->block[i++] = col[y & 1];
      apx->block[i++] = col[(y & 1) ^ 1];
    }
  }

  GST_WRITE_UINT16_LE (&apx->data[0], col[0]);
  GST_WRITE_UINT16_LE (&apx->data[2], col[1]);
  apx->error = mve_block_error_packed (enc->mve,
      src - (8 * enc->mve->width), apx->block);
  return apx->error;
}

/* all available encodings in the preferred order,
   i.e. in ascending encoded size */
static const GstMveEncoding mve_encodings[] = {
  {0x1, 0, mve_encode_0x1},
  {0x0, 0, mve_encode_0x0},
  {0x3, 1, mve_encode_0x3},
  {0x4, 1, mve_encode_0x4},
  {0x2, 1, mve_encode_0x2},
  {0xe, 2, mve_encode_0xe},
  {0x5, 2, mve_encode_0x5},
  {0xf, 4, mve_encode_0xf},
  {0x7, 6, mve_encode_0x7a},
  {0xd, 8, mve_encode_0xd},
  {0x7, 12, mve_encode_0x7b},
  {0x9, 12, mve_encode_0x9a},
  {0x9, 16, mve_encode_0x9b},
  {0x9, 16, mve_encode_0x9c},
  {0x8, 16, mve_encode_0x8a},
  {0x8, 16, mve_encode_0x8b},
  {0x8, 24, mve_encode_0x8c},
  {0x9, 24, mve_encode_0x9d},
  {0xc, 32, mve_encode_0xc},
  {0xa, 32, mve_encode_0xaa},
  {0xa, 32, mve_encode_0xab},
  {0xa, 48, mve_encode_0xac},
  {0xb, 128, mve_encode_0xb}
};

static gboolean
mve_reorder_solution (GArray ** solution, guint16 n)
{
  /* do a binary search to find the position to reinsert the modified element */
  /* the block we need to reconsider is always at position 0 */
  /* return TRUE if this block only has 1 encoding left and can be dropped */
  if (mve_comp_solution (&solution[0], &solution[1]) <= 0)
    return FALSE;               /* already sorted */

  else if (solution[0]->len <= 1)
    /* drop this element from further calculations since we cannot improve here */
    return TRUE;

  else {
    /* we know the error value can only get worse, so we can actually start at 1 */
    guint lower = 1;
    guint upper = n - 1;
    gint cmp;
    guint idx = 0;

    while (upper > lower) {
      idx = lower + ((upper - lower) / 2);

      cmp = mve_comp_solution (&solution[0], &solution[idx]);

      if (cmp < 0) {
        upper = idx;
      } else if (cmp > 0) {
        lower = ++idx;
      } else {
        upper = lower = idx;
      }
    }

    if (idx > 0) {
      /* rearrange array members in new order */
      GArray *a = solution[0];

      memcpy (&solution[0], &solution[1], sizeof (GArray *) * idx);
      solution[idx] = a;
    }
  }
  return FALSE;
}

static guint32
gst_mve_find_solution (GArray ** approx, guint16 n, guint32 size, guint16 max)
{
  /* build an array of approximations we can shuffle around */
  GstMveApprox *sol_apx;
  GArray **solution = g_malloc (sizeof (GArray *) * n);
  GArray **current = solution;

  memcpy (solution, approx, sizeof (GArray *) * n);

  qsort (solution, n, sizeof (GArray *), mve_comp_solution);

  do {
    /* array is now sorted by error of the next to optimal approximation;
       drop optimal approximation for the best block */

    /* unable to reduce size further */
    if (current[0]->len <= 1)
      break;

    sol_apx = &g_array_index (current[0], GstMveApprox, current[0]->len - 1);
    size -= mve_encodings[sol_apx->type].size;
    g_array_remove_index_fast (current[0], current[0]->len - 1);
    sol_apx = &g_array_index (current[0], GstMveApprox, current[0]->len - 1);
    size += mve_encodings[sol_apx->type].size;

    if (mve_reorder_solution (current, n)) {
      ++current;
      --n;
    }
  } while (size > max);

  g_free (solution);

  return size;
}

GstFlowReturn
mve_encode_frame16 (GstMveMux * mve, GstBuffer * frame, guint16 max_data)
{
  guint16 *src;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint8 *cm = mve->chunk_code_map;
  GByteArray *pstream;
  GArray **approx;
  GstMveApprox apx;
  GstMveEncoderData enc;
  const guint16 blocks = (mve->width * mve->height) / 64;
  guint32 encoded_size = 2;     /* two initial bytes for the offset */
  guint i = 0, x, y;

  src = (guint16 *) GST_BUFFER_DATA (frame);

  approx = g_malloc (sizeof (GArray *) * blocks);

  enc.mve = mve;

  for (enc.y = 0; enc.y < mve->height; enc.y += 8) {
    for (enc.x = 0; enc.x < mve->width; enc.x += 8) {
      guint32 err, last_err = MVE_APPROX_MAX_ERROR;
      guint type = 0;
      guint best = 0;

      enc.q2available = enc.q4available = FALSE;
      approx[i] = g_array_new (FALSE, FALSE, sizeof (GstMveApprox));

      do {
        err = mve_encodings[type].approx (&enc, src, &apx);

        if (err < last_err) {
          apx.type = best = type;
          g_array_append_val (approx[i], apx);
          last_err = err;
        }

        ++type;
      } while (last_err != 0);

      encoded_size += mve_encodings[best].size;
      ++i;
      src += 8;
    }
    src += 7 * mve->width;
  }

  /* find best solution with size constraints */
  GST_DEBUG_OBJECT (mve, "encoded frame %u in %u bytes (lossless)",
      mve->video_frames + 1, encoded_size);

#if 0
  /* FIXME */
  src = (guint16 *) GST_BUFFER_DATA (frame);
  for (i = 0, y = 0; y < mve->height; y += 8) {
    for (x = 0; x < mve->width; x += 8, ++i) {
      GstMveApprox *sol =
          &g_array_index (approx[i], GstMveApprox, approx[i]->len - 1);
      guint opcode = mve_encodings[sol->type].opcode;
      guint j, k;

      if (sol->error > 0)
        GST_WARNING_OBJECT (mve, "error is %lu for %d/%d (0x%x)", sol->error, x,
            y, opcode);

      for (j = 0; j < 8; ++j) {
        guint16 *o = src + j * mve->width;
        guint16 *c = sol->block + j * 8;

        if (memcmp (o, c, 16)) {
          GST_WARNING_OBJECT (mve, "opcode 0x%x (type %d) at %d/%d, line %d:",
              opcode, sol->type, x, y, j + 1);
          for (k = 0; k < 8; ++k) {
            o = src + k * mve->width;
            c = sol->block + k * 8;
            GST_WARNING_OBJECT (mve,
                "%d should be: %4d  %4d  %4d  %4d  %4d  %4d  %4d  %4d", k, o[0],
                o[1], o[2], o[3], o[4], o[5], o[6], o[7]);
            GST_WARNING_OBJECT (mve,
                "%d but is   : %4d  %4d  %4d  %4d  %4d  %4d  %4d  %4d", k, c[0],
                c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
          }
        }
      }
      src += 8;
    }
    src += 7 * mve->width;
  }
#endif

  if (encoded_size > max_data) {
    encoded_size =
        gst_mve_find_solution (approx, blocks, encoded_size, max_data);
    if (encoded_size > max_data) {
      GST_ERROR_OBJECT (mve, "unable to compress frame to less than %d bytes",
          encoded_size);
      for (i = 0; i < blocks; ++i)
        g_array_free (approx[i], TRUE);

      goto done;
    }
    GST_DEBUG_OBJECT (mve, "compressed frame %u to %u bytes (lossy)",
        mve->video_frames + 1, encoded_size);
  }

  mve->chunk_video = g_byte_array_sized_new (encoded_size);
  /* reserve two bytes for the offset pointer we'll fill in later */
  g_byte_array_set_size (mve->chunk_video, 2);

  pstream = g_byte_array_new ();

  /* encode */
  src = (guint16 *) GST_BUFFER_DATA (frame);
  for (i = 0, y = 0; y < mve->height; y += 8) {
    for (x = 0; x < mve->width; x += 8, ++i) {
      GstMveApprox *sol =
          &g_array_index (approx[i], GstMveApprox, approx[i]->len - 1);
      guint opcode = mve_encodings[sol->type].opcode;
      GByteArray *dest;

      if (opcode >= 0x2 && opcode <= 0x4)
        dest = pstream;
      else
        dest = mve->chunk_video;

      g_byte_array_append (dest, sol->data, mve_encodings[sol->type].size);

      if (i & 1) {
        *cm |= opcode << 4;
        ++cm;
      } else
        *cm = opcode;

      /* modify the frame to match the image we actually encoded */
      if (sol->error > 0)
        mve_restore_block (mve, src, sol->block);

      src += 8;
      g_array_free (approx[i], TRUE);
    }
    src += 7 * mve->width;
  }

  /* now update the offset */
  GST_WRITE_UINT16_LE (mve->chunk_video->data, mve->chunk_video->len);
  g_byte_array_append (mve->chunk_video, pstream->data, pstream->len);
  g_byte_array_free (pstream, TRUE);

  ret = GST_FLOW_OK;

done:
  g_free (approx);

  return ret;
}
