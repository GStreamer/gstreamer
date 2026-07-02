/*
 * LZO 1x decompression
 * Copyright (c) 2006 Reimar Doeffinger
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "lzo.h"

/// Define if we may write up to 12 bytes beyond the output buffer.
// #define OUTBUF_PADDED 1
/// Define if we may read up to 8 bytes beyond the input buffer.
// #define INBUF_PADDED 1

typedef struct LZOContext
{
  const uint8_t *in, *in_end;
  uint8_t *out_start, *out, *out_end;
  int error;
} LZOContext;

/*
 * @brief Reads one byte from the input buffer, avoiding an overrun.
 * @return byte read
 */
static inline int
get_byte (LZOContext * c)
{
  if (c->in < c->in_end)
    return *c->in++;
  c->error |= LZO_INPUT_DEPLETED;
  return 1;
}

#ifdef INBUF_PADDED
#define GETB(c) (*(c).in++)
#else
#define GETB(c) get_byte(&(c))
#endif

/*
 * @brief Decodes a length value in the coding used by lzo.
 * @param x previous byte value
 * @param mask bits used from x
 * @return decoded length value
 */
static inline int
get_len (LZOContext * c, int x, int mask)
{
  int cnt = x & mask;
  if (!cnt) {
    while (!(x = get_byte (c))) {
      if (cnt >= INT_MAX - 1000) {
        c->error |= LZO_ERROR;
        break;
      }
      cnt += 255;
    }
    cnt += mask + x;
  }
  return cnt;
}

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif
#ifndef AV_WB16
#   define AV_WB16(p, val) do {                 \
        uint16_t d = (val);                     \
        ((uint8_t*)(p))[1] = (d);               \
        ((uint8_t*)(p))[0] = (d)>>8;            \
    } while(0)
#endif

#ifndef AV_RL16
#   define AV_RL16(x)                           \
    ((((const uint8_t*)(x))[1] << 8) |          \
      ((const uint8_t*)(x))[0])
#endif
#ifndef AV_WL16
#   define AV_WL16(p, val) do {                 \
        uint16_t d = (val);                     \
        ((uint8_t*)(p))[0] = (d);               \
        ((uint8_t*)(p))[1] = (d)>>8;            \
    } while(0)
#endif

#ifndef AV_RB32
#   define AV_RB32(x)                                \
    (((uint32_t)((const uint8_t*)(x))[0] << 24) |    \
               (((const uint8_t*)(x))[1] << 16) |    \
               (((const uint8_t*)(x))[2] <<  8) |    \
                ((const uint8_t*)(x))[3])
#endif
#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

#ifndef AV_RL32
#   define AV_RL32(x)                                \
    (((uint32_t)((const uint8_t*)(x))[3] << 24) |    \
               (((const uint8_t*)(x))[2] << 16) |    \
               (((const uint8_t*)(x))[1] <<  8) |    \
                ((const uint8_t*)(x))[0])
#endif
#ifndef AV_WL32
#   define AV_WL32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[0] = (d);               \
        ((uint8_t*)(p))[1] = (d)>>8;            \
        ((uint8_t*)(p))[2] = (d)>>16;           \
        ((uint8_t*)(p))[3] = (d)>>24;           \
    } while(0)
#endif

#ifndef AV_RB64
#   define AV_RB64(x)                                   \
    (((uint64_t)((const uint8_t*)(x))[0] << 56) |       \
     ((uint64_t)((const uint8_t*)(x))[1] << 48) |       \
     ((uint64_t)((const uint8_t*)(x))[2] << 40) |       \
     ((uint64_t)((const uint8_t*)(x))[3] << 32) |       \
     ((uint64_t)((const uint8_t*)(x))[4] << 24) |       \
     ((uint64_t)((const uint8_t*)(x))[5] << 16) |       \
     ((uint64_t)((const uint8_t*)(x))[6] <<  8) |       \
      (uint64_t)((const uint8_t*)(x))[7])
#endif
#ifndef AV_WB64
#   define AV_WB64(p, val) do {                 \
        uint64_t d = (val);                     \
        ((uint8_t*)(p))[7] = (d);               \
        ((uint8_t*)(p))[6] = (d)>>8;            \
        ((uint8_t*)(p))[5] = (d)>>16;           \
        ((uint8_t*)(p))[4] = (d)>>24;           \
        ((uint8_t*)(p))[3] = (d)>>32;           \
        ((uint8_t*)(p))[2] = (d)>>40;           \
        ((uint8_t*)(p))[1] = (d)>>48;           \
        ((uint8_t*)(p))[0] = (d)>>56;           \
    } while(0)
#endif

#ifndef AV_RL64
#   define AV_RL64(x)                                   \
    (((uint64_t)((const uint8_t*)(x))[7] << 56) |       \
     ((uint64_t)((const uint8_t*)(x))[6] << 48) |       \
     ((uint64_t)((const uint8_t*)(x))[5] << 40) |       \
     ((uint64_t)((const uint8_t*)(x))[4] << 32) |       \
     ((uint64_t)((const uint8_t*)(x))[3] << 24) |       \
     ((uint64_t)((const uint8_t*)(x))[2] << 16) |       \
     ((uint64_t)((const uint8_t*)(x))[1] <<  8) |       \
      (uint64_t)((const uint8_t*)(x))[0])
#endif
#ifndef AV_WL64
#   define AV_WL64(p, val) do {                 \
        uint64_t d = (val);                     \
        ((uint8_t*)(p))[0] = (d);               \
        ((uint8_t*)(p))[1] = (d)>>8;            \
        ((uint8_t*)(p))[2] = (d)>>16;           \
        ((uint8_t*)(p))[3] = (d)>>24;           \
        ((uint8_t*)(p))[4] = (d)>>32;           \
        ((uint8_t*)(p))[5] = (d)>>40;           \
        ((uint8_t*)(p))[6] = (d)>>48;           \
        ((uint8_t*)(p))[7] = (d)>>56;           \
    } while(0)
#endif

#ifndef AV_RB24
#   define AV_RB24(x)                           \
    ((((const uint8_t*)(x))[0] << 16) |         \
     (((const uint8_t*)(x))[1] <<  8) |         \
      ((const uint8_t*)(x))[2])
#endif
#ifndef AV_WB24
#   define AV_WB24(p, d) do {                   \
        ((uint8_t*)(p))[2] = (d);               \
        ((uint8_t*)(p))[1] = (d)>>8;            \
        ((uint8_t*)(p))[0] = (d)>>16;           \
    } while(0)
#endif

#ifndef AV_RL24
#   define AV_RL24(x)                           \
    ((((const uint8_t*)(x))[2] << 16) |         \
     (((const uint8_t*)(x))[1] <<  8) |         \
      ((const uint8_t*)(x))[0])
#endif
#ifndef AV_WL24
#   define AV_WL24(p, d) do {                   \
        ((uint8_t*)(p))[0] = (d);               \
        ((uint8_t*)(p))[1] = (d)>>8;            \
        ((uint8_t*)(p))[2] = (d)>>16;           \
    } while(0)
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
#   define AV_RN(s, p)    AV_RB##s(p)
#   define AV_WN(s, p, v) AV_WB##s(p, v)
#else
#   define AV_RN(s, p)    AV_RL##s(p)
#   define AV_WN(s, p, v) AV_WL##s(p, v)
#endif

#ifndef AV_RN16
#   define AV_RN16(p) AV_RN(16, p)
#endif

#ifndef AV_RN32
#   define AV_RN32(p) AV_RN(32, p)
#endif

#ifndef AV_RN64
#   define AV_RN64(p) AV_RN(64, p)
#endif

#ifndef AV_WN16
#   define AV_WN16(p, v) AV_WN(16, p, v)
#endif

#ifndef AV_WN32
#   define AV_WN32(p, v) AV_WN(32, p, v)
#endif

#ifndef AV_WN64
#   define AV_WN64(p, v) AV_WN(64, p, v)
#endif

#define AV_COPYU(n, d, s) AV_WN##n(d, AV_RN##n(s));

#ifndef AV_COPY16U
#   define AV_COPY16U(d, s) AV_COPYU(16, d, s)
#endif

#ifndef AV_COPY32U
#   define AV_COPY32U(d, s) AV_COPYU(32, d, s)
#endif

#ifndef AV_COPY64U
#   define AV_COPY64U(d, s) AV_COPYU(64, d, s)
#endif

/*
 * @brief Copies bytes from input to output buffer with checking.
 * @param cnt number of bytes to copy, must be >= 0
 */
static inline void
copy (LZOContext * c, int cnt)
{
  register const uint8_t *src = c->in;
  register uint8_t *dst = c->out;
  g_assert (cnt >= 0);
  if (cnt > c->in_end - src) {
    cnt = MAX (c->in_end - src, 0);
    c->error |= LZO_INPUT_DEPLETED;
  }
  if (cnt > c->out_end - dst) {
    cnt = MAX (c->out_end - dst, 0);
    c->error |= LZO_OUTPUT_FULL;
  }
#if defined(INBUF_PADDED) && defined(OUTBUF_PADDED)
  COPY32U (dst, src);
  src += 4;
  dst += 4;
  cnt -= 4;
  if (cnt > 0)
#endif
    memcpy (dst, src, cnt);
  c->in = src + cnt;
  c->out = dst + cnt;
}

static void
fill16 (uint8_t * dst, int len)
{
  uint32_t v = AV_RN16 (dst - 2);

  v |= v << 16;

  while (len >= 4) {
    AV_WN32 (dst, v);
    dst += 4;
    len -= 4;
  }

  while (len--) {
    *dst = dst[-2];
    dst++;
  }
}

static void
fill24 (uint8_t * dst, int len)
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
  uint32_t v = AV_RB24 (dst - 3);
  uint32_t a = v << 8 | v >> 16;
  uint32_t b = v << 16 | v >> 8;
  uint32_t c = v << 24 | v;
#else
  uint32_t v = AV_RL24 (dst - 3);
  uint32_t a = v | v << 24;
  uint32_t b = v >> 8 | v << 16;
  uint32_t c = v >> 16 | v << 8;
#endif

  while (len >= 12) {
    AV_WN32 (dst, a);
    AV_WN32 (dst + 4, b);
    AV_WN32 (dst + 8, c);
    dst += 12;
    len -= 12;
  }

  if (len >= 4) {
    AV_WN32 (dst, a);
    dst += 4;
    len -= 4;
  }

  if (len >= 4) {
    AV_WN32 (dst, b);
    dst += 4;
    len -= 4;
  }

  while (len--) {
    *dst = dst[-3];
    dst++;
  }
}

static void
fill32 (uint8_t * dst, int len)
{
  uint32_t v = AV_RN32 (dst - 4);

#if GLIB_SIZEOF_VOID_P >= 8
  uint64_t v2 = v + ((uint64_t) v << 32);
  while (len >= 32) {
    AV_WN64 (dst, v2);
    AV_WN64 (dst + 8, v2);
    AV_WN64 (dst + 16, v2);
    AV_WN64 (dst + 24, v2);
    dst += 32;
    len -= 32;
  }
#endif

  while (len >= 4) {
    AV_WN32 (dst, v);
    dst += 4;
    len -= 4;
  }

  while (len--) {
    *dst = dst[-4];
    dst++;
  }
}

static void
av_memcpy_backptr (uint8_t * dst, int back, int cnt)
{
  const uint8_t *src = &dst[-back];
  if (!back)
    return;

  if (back == 1) {
    memset (dst, *src, cnt);
  } else if (back == 2) {
    fill16 (dst, cnt);
  } else if (back == 3) {
    fill24 (dst, cnt);
  } else if (back == 4) {
    fill32 (dst, cnt);
  } else {
    if (cnt >= 16) {
      int blocklen = back;
      while (cnt > blocklen) {
        memcpy (dst, src, blocklen);
        dst += blocklen;
        cnt -= blocklen;
        blocklen <<= 1;
      }
      memcpy (dst, src, cnt);
      return;
    }
    if (cnt >= 8) {
      AV_COPY32U (dst, src);
      AV_COPY32U (dst + 4, src + 4);
      src += 8;
      dst += 8;
      cnt -= 8;
    }
    if (cnt >= 4) {
      AV_COPY32U (dst, src);
      src += 4;
      dst += 4;
      cnt -= 4;
    }
    if (cnt >= 2) {
      AV_COPY16U (dst, src);
      src += 2;
      dst += 2;
      cnt -= 2;
    }
    if (cnt)
      *dst = *src;
  }
}

/*
 * @brief Copies previously decoded bytes to current position.
 * @param back how many bytes back we start, must be > 0
 * @param cnt number of bytes to copy, must be > 0
 *
 * cnt > back is valid, this will copy the bytes we just copied,
 * thus creating a repeating pattern with a period length of back.
 */
static inline void
copy_backptr (LZOContext * c, int back, int cnt)
{
  register uint8_t *dst = c->out;
  g_assert (cnt > 0);
  if (dst - c->out_start < back) {
    c->error |= LZO_INVALID_BACKPTR;
    return;
  }
  if (cnt > c->out_end - dst) {
    cnt = MAX (c->out_end - dst, 0);
    c->error |= LZO_OUTPUT_FULL;
  }
  av_memcpy_backptr (dst, back, cnt);
  c->out = dst + cnt;
}

int
lzo1x_decode (void *out, int *outlen, const void *in, int *inlen)
{
  int state = 0;
  int x;
  LZOContext c;
  if (*outlen <= 0 || *inlen <= 0) {
    int res = 0;
    if (*outlen <= 0)
      res |= LZO_OUTPUT_FULL;
    if (*inlen <= 0)
      res |= LZO_INPUT_DEPLETED;
    return res;
  }
  c.in = in;
  c.in_end = (const uint8_t *) in + *inlen;
  c.out = c.out_start = out;
  c.out_end = (uint8_t *) out + *outlen;
  c.error = 0;
  x = GETB (c);
  if (x > 17) {
    copy (&c, x - 17);
    x = GETB (c);
    if (x < 16)
      c.error |= LZO_ERROR;
  }
  if (c.in > c.in_end)
    c.error |= LZO_INPUT_DEPLETED;
  while (!c.error) {
    int cnt, back;
    if (x > 15) {
      if (x > 63) {
        cnt = (x >> 5) - 1;
        back = (GETB (c) << 3) + ((x >> 2) & 7) + 1;
      } else if (x > 31) {
        cnt = get_len (&c, x, 31);
        x = GETB (c);
        back = (GETB (c) << 6) + (x >> 2) + 1;
      } else {
        cnt = get_len (&c, x, 7);
        back = (1 << 14) + ((x & 8) << 11);
        x = GETB (c);
        back += (GETB (c) << 6) + (x >> 2);
        if (back == (1 << 14)) {
          if (cnt != 1)
            c.error |= LZO_ERROR;
          break;
        }
      }
    } else if (!state) {
      cnt = get_len (&c, x, 15);
      copy (&c, cnt + 3);
      x = GETB (c);
      if (x > 15)
        continue;
      cnt = 1;
      back = (1 << 11) + (GETB (c) << 2) + (x >> 2) + 1;
    } else {
      cnt = 0;
      back = (GETB (c) << 2) + (x >> 2) + 1;
    }
    copy_backptr (&c, back, cnt + 2);
    state = cnt = x & 3;
    copy (&c, cnt);
    x = GETB (c);
  }
  *inlen = c.in_end - c.in;
  if (c.in > c.in_end)
    *inlen = 0;
  *outlen = c.out_end - c.out;
  return c.error;
}
