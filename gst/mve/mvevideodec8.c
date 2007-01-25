/*
 * Interplay MVE Video Decoder (8 bit)
 * Copyright (C) 2003 the ffmpeg project, Mike Melanson
 *           (C) 2006 Jens Granseuer
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * For more information about the Interplay MVE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/interplay-mve.txt
 */

#include "gstmvedemux.h"
#include <string.h>

#define CHECK_STREAM(l, n) \
  do { \
    if (G_UNLIKELY (*(l) < (n))) { \
      GST_ERROR ("wanted to read %d bytes from stream, %d available", (n), *(l)); \
      return -1; \
    } \
    *(l) -= (n); \
  } while (0)


/* copy an 8x8 block from the stream to the frame buffer */
static int
ipvideo_copy_block (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char *src, int offset)
{
  int i;
  long frame_offset;

  frame_offset = frame - s->back_buf1 + offset;

  if (G_UNLIKELY (frame_offset < 0)) {
    GST_ERROR ("frame offset < 0 (%ld)", frame_offset);
    return -1;
  } else if (G_UNLIKELY (frame_offset > s->max_block_offset)) {
    GST_ERROR ("frame offset above limit (%ld > %u)",
        frame_offset, s->max_block_offset);
    return -1;
  }

  for (i = 0; i < 8; ++i) {
    memcpy (frame, src, 8);
    frame += s->width;
    src += s->width;
  }

  return 0;
}

static int
ipvideo_decode_0x2 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  unsigned char B;
  int x, y;
  int offset;

  /* copy block from 2 frames ago using a motion vector */
  CHECK_STREAM (len, 1);
  B = *(*data)++;

  if (B < 56) {
    x = 8 + (B % 7);
    y = B / 7;
  } else {
    x = -14 + ((B - 56) % 29);
    y = 8 + ((B - 56) / 29);
  }
  offset = y * s->width + x;

  return ipvideo_copy_block (s, frame, frame + offset, offset);
}

static int
ipvideo_decode_0x3 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  unsigned char B;
  int x, y;
  int offset;

  /* copy 8x8 block from current frame from an up/left block */
  CHECK_STREAM (len, 1);
  B = *(*data)++;

  if (B < 56) {
    x = -(8 + (B % 7));
    y = -(B / 7);
  } else {
    x = -(-14 + ((B - 56) % 29));
    y = -(8 + ((B - 56) / 29));
  }
  offset = y * s->width + x;

  return ipvideo_copy_block (s, frame, frame + offset, offset);
}

static int
ipvideo_decode_0x4 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  unsigned char B;
  int x, y;
  int offset;

  /* copy a block from the previous frame */
  CHECK_STREAM (len, 1);
  B = *(*data)++;
  x = -8 + (B & 0x0F);
  y = -8 + (B >> 4);
  offset = y * s->width + x;

  return ipvideo_copy_block (s, frame,
      frame + (s->back_buf2 - s->back_buf1) + offset, offset);
}

static int
ipvideo_decode_0x5 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  signed char x, y;
  int offset;

  /* copy a block from the previous frame using an expanded range */
  CHECK_STREAM (len, 2);

  x = (signed char) *(*data)++;
  y = (signed char) *(*data)++;
  offset = y * s->width + x;

  return ipvideo_copy_block (s, frame,
      frame + (s->back_buf2 - s->back_buf1) + offset, offset);
}

static int
ipvideo_decode_0x7 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char P0, P1;
  unsigned int flags;
  int bitmask;

  /* 2-color encoding */
  CHECK_STREAM (len, 2 + 2);

  P0 = *(*data)++;
  P1 = *(*data)++;

  if (P0 <= P1) {

    /* need 8 more bytes from the stream */
    CHECK_STREAM (len, 8 - 2);

    for (y = 0; y < 8; ++y) {
      flags = *(*data)++;
      for (x = 0x01; x <= 0x80; x <<= 1) {
        if (flags & x)
          *frame++ = P1;
        else
          *frame++ = P0;
      }
      frame += s->width - 8;
    }

  } else {

    /* need 2 more bytes from the stream */
    flags = ((*data)[1] << 8) | (*data)[0];
    (*data) += 2;
    bitmask = 0x0001;
    for (y = 0; y < 8; y += 2) {
      for (x = 0; x < 8; x += 2, bitmask <<= 1) {
        if (flags & bitmask) {
          *(frame + x) = P1;
          *(frame + x + 1) = P1;
          *(frame + s->width + x) = P1;
          *(frame + s->width + x + 1) = P1;
        } else {
          *(frame + x) = P0;
          *(frame + x + 1) = P0;
          *(frame + s->width + x) = P0;
          *(frame + s->width + x + 1) = P0;
        }
      }
      frame += s->width * 2;
    }
  }

  return 0;
}

static int
ipvideo_decode_0x8 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char P[8];
  unsigned char B[8];
  unsigned int flags = 0;
  unsigned int bitmask = 0;
  unsigned char P0 = 0, P1 = 0;
  int lower_half = 0;

  /* 2-color encoding for each 4x4 quadrant, or 2-color encoding on
   * either top and bottom or left and right halves */
  CHECK_STREAM (len, 4 + 8);

  P[0] = (*data)[0];
  P[1] = (*data)[1];
  B[0] = (*data)[2];
  B[1] = (*data)[3];
  (*data) += 4;

  if (P[0] <= P[1]) {

    /* need 12 more bytes */
    CHECK_STREAM (len, 12 - 8);

    P[2] = (*data)[0];
    P[3] = (*data)[1];
    B[2] = (*data)[2];
    B[3] = (*data)[3];
    P[4] = (*data)[4];
    P[5] = (*data)[5];
    B[4] = (*data)[6];
    B[5] = (*data)[7];
    P[6] = (*data)[8];
    P[7] = (*data)[9];
    B[6] = (*data)[10];
    B[7] = (*data)[11];
    (*data) += 12;

    flags =
        ((B[0] & 0xF0) << 4) | ((B[4] & 0xF0) << 8) |
        ((B[0] & 0x0F)) | ((B[4] & 0x0F) << 4) |
        ((B[1] & 0xF0) << 20) | ((B[5] & 0xF0) << 24) |
        ((B[1] & 0x0F) << 16) | ((B[5] & 0x0F) << 20);
    bitmask = 0x00000001;
    lower_half = 0;             /* still on top half */

    for (y = 0; y < 8; ++y) {

      /* time to reload flags? */
      if (y == 4) {
        flags =
            ((B[2] & 0xF0) << 4) | ((B[6] & 0xF0) << 8) |
            ((B[2] & 0x0F)) | ((B[6] & 0x0F) << 4) |
            ((B[3] & 0xF0) << 20) | ((B[7] & 0xF0) << 24) |
            ((B[3] & 0x0F) << 16) | ((B[7] & 0x0F) << 20);
        bitmask = 0x00000001;
        lower_half = 2;
      }

      /* get the pixel values ready for this quadrant */
      P0 = P[lower_half + 0];
      P1 = P[lower_half + 1];

      for (x = 0; x < 8; ++x, bitmask <<= 1) {
        if (x == 4) {
          P0 = P[lower_half + 4];
          P1 = P[lower_half + 5];
        }

        if (flags & bitmask)
          *frame++ = P1;
        else
          *frame++ = P0;
      }
      frame += s->width - 8;
    }

  } else {

    /* need 8 more bytes */
    B[2] = (*data)[0];
    B[3] = (*data)[1];
    P[2] = (*data)[2];
    P[3] = (*data)[3];
    B[4] = (*data)[4];
    B[5] = (*data)[5];
    B[6] = (*data)[6];
    B[7] = (*data)[7];
    (*data) += 8;

    if (P[2] <= P[3]) {

      /* vertical split; left & right halves are 2-color encoded */

      flags =
          ((B[0] & 0xF0) << 4) | ((B[4] & 0xF0) << 8) |
          ((B[0] & 0x0F)) | ((B[4] & 0x0F) << 4) |
          ((B[1] & 0xF0) << 20) | ((B[5] & 0xF0) << 24) |
          ((B[1] & 0x0F) << 16) | ((B[5] & 0x0F) << 20);
      bitmask = 0x00000001;

      for (y = 0; y < 8; ++y) {

        /* time to reload flags? */
        if (y == 4) {
          flags =
              ((B[2] & 0xF0) << 4) | ((B[6] & 0xF0) << 8) |
              ((B[2] & 0x0F)) | ((B[6] & 0x0F) << 4) |
              ((B[3] & 0xF0) << 20) | ((B[7] & 0xF0) << 24) |
              ((B[3] & 0x0F) << 16) | ((B[7] & 0x0F) << 20);
          bitmask = 0x00000001;
        }

        /* get the pixel values ready for this half */
        P0 = P[0];
        P1 = P[1];

        for (x = 0; x < 8; ++x, bitmask <<= 1) {
          if (x == 4) {
            P0 = P[2];
            P1 = P[3];
          }

          if (flags & bitmask)
            *frame++ = P1;
          else
            *frame++ = P0;
        }
        frame += s->width - 8;
      }

    } else {

      /* horizontal split; top & bottom halves are 2-color encoded */

      P0 = P[0];
      P1 = P[1];

      for (y = 0; y < 8; ++y) {

        flags = B[y];
        if (y == 4) {
          P0 = P[2];
          P1 = P[3];
        }

        for (bitmask = 0x01; bitmask <= 0x80; bitmask <<= 1) {

          if (flags & bitmask)
            *frame++ = P1;
          else
            *frame++ = P0;
        }
        frame += s->width - 8;
      }
    }
  }

  return 0;
}

static int
ipvideo_decode_0x9 (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char P[4];
  unsigned char B[4];
  unsigned long flags = 0;
  int shifter = 0;
  unsigned char pix;

  /* 4-color encoding */
  CHECK_STREAM (len, 4 + 4);

  P[0] = (*data)[0];
  P[1] = (*data)[1];
  P[2] = (*data)[2];
  P[3] = (*data)[3];
  (*data) += 4;

  if ((P[0] <= P[1]) && (P[2] <= P[3])) {

    /* 1 of 4 colors for each pixel, need 16 more bytes */
    CHECK_STREAM (len, 16 - 4);

    for (y = 0; y < 8; ++y) {
      /* get the next set of 8 2-bit flags */
      flags = ((*data)[1] << 8) | (*data)[0];
      (*data) += 2;
      for (x = 0, shifter = 0; x < 8; ++x, shifter += 2) {
        *frame++ = P[(flags >> shifter) & 0x03];
      }
      frame += s->width - 8;
    }

  } else if ((P[0] <= P[1]) && (P[2] > P[3])) {

    /* 1 of 4 colors for each 2x2 block, need 4 more bytes */
    B[0] = (*data)[0];
    B[1] = (*data)[1];
    B[2] = (*data)[2];
    B[3] = (*data)[3];
    (*data) += 4;
    flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
    shifter = 0;

    for (y = 0; y < 8; y += 2) {
      for (x = 0; x < 8; x += 2, shifter += 2) {
        pix = P[(flags >> shifter) & 0x03];
        *(frame + x) = pix;
        *(frame + x + 1) = pix;
        *(frame + s->width + x) = pix;
        *(frame + s->width + x + 1) = pix;
      }
      frame += s->width * 2;
    }

  } else if ((P[0] > P[1]) && (P[2] <= P[3])) {

    /* 1 of 4 colors for each 2x1 block, need 8 more bytes */
    CHECK_STREAM (len, 8 - 4);

    for (y = 0; y < 8; ++y) {
      /* time to reload flags? */
      if ((y == 0) || (y == 4)) {
        B[0] = (*data)[0];
        B[1] = (*data)[1];
        B[2] = (*data)[2];
        B[3] = (*data)[3];
        (*data) += 4;
        flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
        shifter = 0;
      }
      for (x = 0; x < 8; x += 2, shifter += 2) {
        pix = P[(flags >> shifter) & 0x03];
        *(frame + x) = pix;
        *(frame + x + 1) = pix;
      }
      frame += s->width;
    }

  } else {

    /* 1 of 4 colors for each 1x2 block, need 8 more bytes */
    CHECK_STREAM (len, 8 - 4);

    for (y = 0; y < 8; y += 2) {
      /* time to reload flags? */
      if ((y == 0) || (y == 4)) {
        B[0] = (*data)[0];
        B[1] = (*data)[1];
        B[2] = (*data)[2];
        B[3] = (*data)[3];
        (*data) += 4;
        flags = (B[3] << 24) | (B[2] << 16) | (B[1] << 8) | B[0];
        shifter = 0;
      }
      for (x = 0; x < 8; ++x, shifter += 2) {
        pix = P[(flags >> shifter) & 0x03];
        *(frame + x) = pix;
        *(frame + s->width + x) = pix;
      }
      frame += s->width * 2;
    }
  }

  return 0;
}

static int
ipvideo_decode_0xa (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char P[16];
  unsigned char B[16];
  int flags = 0;
  int shifter = 0;
  int index;
  int split;
  int lower_half;

  /* 4-color encoding for each 4x4 quadrant, or 4-color encoding on
   * either top and bottom or left and right halves */
  CHECK_STREAM (len, 8 + 16);

  P[0] = (*data)[0];
  P[1] = (*data)[1];
  P[2] = (*data)[2];
  P[3] = (*data)[3];
  B[0] = (*data)[4];
  B[1] = (*data)[5];
  B[2] = (*data)[6];
  B[3] = (*data)[7];
  (*data) += 8;

  if (P[0] <= P[1]) {

    /* 4-color encoding for each quadrant; need 24 more bytes */
    CHECK_STREAM (len, 24 - 16);

    for (y = 4; y < 16; y += 4) {
      for (x = y; x < y + 4; ++x)
        P[x] = *(*data)++;
      for (x = y; x < y + 4; ++x)
        B[x] = *(*data)++;
    }

    for (y = 0; y < 8; ++y) {

      lower_half = (y >= 4) ? 4 : 0;
      flags = (B[y + 8] << 8) | B[y];

      for (x = 0, shifter = 0; x < 8; ++x, shifter += 2) {
        split = (x >= 4) ? 8 : 0;
        index = split + lower_half + ((flags >> shifter) & 0x03);
        *frame++ = P[index];
      }

      frame += s->width - 8;
    }

  } else {

    /* 4-color encoding for either left and right or top and bottom
     * halves; need 16 more bytes */

    B[4] = (*data)[0];
    B[5] = (*data)[1];
    B[6] = (*data)[2];
    B[7] = (*data)[3];
    P[4] = (*data)[4];
    P[5] = (*data)[5];
    P[6] = (*data)[6];
    P[7] = (*data)[7];
    (*data) += 8;
    memcpy (&B[8], *data, 8);
    (*data) += 8;

    if (P[4] <= P[5]) {

      /* block is divided into left and right halves */
      for (y = 0; y < 8; ++y) {

        flags = (B[y + 8] << 8) | B[y];
        split = 0;

        for (x = 0, shifter = 0; x < 8; ++x, shifter += 2) {
          if (x == 4)
            split = 4;
          *frame++ = P[split + ((flags >> shifter) & 0x03)];
        }

        frame += s->width - 8;
      }

    } else {

      /* block is divided into top and bottom halves */
      split = 0;
      for (y = 0; y < 8; ++y) {

        flags = (B[y * 2 + 1] << 8) | B[y * 2];
        if (y == 4)
          split = 4;

        for (x = 0, shifter = 0; x < 8; ++x, shifter += 2)
          *frame++ = P[split + ((flags >> shifter) & 0x03)];

        frame += s->width - 8;
      }
    }
  }

  return 0;
}

static int
ipvideo_decode_0xb (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int y;

  /* 64-color encoding (each pixel in block is a different color) */
  CHECK_STREAM (len, 64);

  for (y = 0; y < 8; ++y) {
    memcpy (frame, *data, 8);
    frame += s->width;
    (*data) += 8;
  }

  return 0;
}

static int
ipvideo_decode_0xc (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char pix;

  /* 16-color block encoding: each 2x2 block is a different color */
  CHECK_STREAM (len, 16);

  for (y = 0; y < 8; y += 2) {
    for (x = 0; x < 8; x += 2) {
      pix = *(*data)++;
      *(frame + x) = pix;
      *(frame + x + 1) = pix;
      *(frame + s->width + x) = pix;
      *(frame + s->width + x + 1) = pix;
    }
    frame += s->width * 2;
  }

  return 0;
}

static int
ipvideo_decode_0xd (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char P[4];
  unsigned char index = 0;

  /* 4-color block encoding: each 4x4 block is a different color */
  CHECK_STREAM (len, 4);

  P[0] = (*data)[0];
  P[1] = (*data)[1];
  P[2] = (*data)[2];
  P[3] = (*data)[3];
  (*data) += 4;

  for (y = 0; y < 8; ++y) {
    if (y < 4)
      index = 0;
    else
      index = 2;

    for (x = 0; x < 8; ++x) {
      if (x == 4)
        ++index;
      *frame++ = P[index];
    }
    frame += s->width - 8;
  }

  return 0;
}

static int
ipvideo_decode_0xe (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int y;
  unsigned char pix;

  /* 1-color encoding: the whole block is 1 solid color */
  CHECK_STREAM (len, 1);
  pix = *(*data)++;

  for (y = 0; y < 8; ++y) {
    memset (frame, pix, 8);
    frame += s->width;
  }

  return 0;
}

static int
ipvideo_decode_0xf (const GstMveDemuxStream * s, unsigned char *frame,
    const unsigned char **data, unsigned short *len)
{
  int x, y;
  unsigned char P[2];

  /* dithered encoding */
  CHECK_STREAM (len, 2);

  P[0] = *(*data)++;
  P[1] = *(*data)++;

  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 4; ++x) {
      *frame++ = P[y & 1];
      *frame++ = P[(y & 1) ^ 1];
    }
    frame += s->width - 8;
  }

  return 0;
}

int
ipvideo_decode_frame8 (const GstMveDemuxStream * s, const unsigned char *data,
    unsigned short len)
{
  int rc = 0;
  int x, y, xx, yy;
  int index = 0;
  unsigned char opcode;
  unsigned char *frame;

  frame = s->back_buf1;

  /* decoding is done in 8x8 blocks */
  xx = s->width >> 3;
  yy = s->height >> 3;

  for (y = 0; y < yy; ++y) {
    for (x = 0; x < xx; ++x) {
      /* decoding map contains 4 bits of information per 8x8 block */
      /* bottom nibble first, then top nibble */
      if (index & 1)
        opcode = s->code_map[index >> 1] >> 4;
      else
        opcode = s->code_map[index >> 1] & 0x0F;
      ++index;

      /* GST_DEBUG ("block @ (%3d, %3d): encoding 0x%X, data ptr @ %p",
         x, y, opcode, data); */

      switch (opcode) {
        case 0x00:
          /* copy a block from the previous frame */
          rc = ipvideo_copy_block (s, frame,
              frame + (s->back_buf2 - s->back_buf1), 0);
          break;
        case 0x01:
          /* copy block from 2 frames ago; since we switched the back
           * buffers we don't actually have to do anything here */
          break;
        case 0x02:
          rc = ipvideo_decode_0x2 (s, frame, &data, &len);
          break;
        case 0x03:
          rc = ipvideo_decode_0x3 (s, frame, &data, &len);
          break;
        case 0x04:
          rc = ipvideo_decode_0x4 (s, frame, &data, &len);
          break;
        case 0x05:
          rc = ipvideo_decode_0x5 (s, frame, &data, &len);
          break;
        case 0x06:
          /* mystery opcode? skip multiple blocks? */
          GST_WARNING ("encountered unsupported opcode 0x6");
          rc = -1;
          break;
        case 0x07:
          rc = ipvideo_decode_0x7 (s, frame, &data, &len);
          break;
        case 0x08:
          rc = ipvideo_decode_0x8 (s, frame, &data, &len);
          break;
        case 0x09:
          rc = ipvideo_decode_0x9 (s, frame, &data, &len);
          break;
        case 0x0a:
          rc = ipvideo_decode_0xa (s, frame, &data, &len);
          break;
        case 0x0b:
          rc = ipvideo_decode_0xb (s, frame, &data, &len);
          break;
        case 0x0c:
          rc = ipvideo_decode_0xc (s, frame, &data, &len);
          break;
        case 0x0d:
          rc = ipvideo_decode_0xd (s, frame, &data, &len);
          break;
        case 0x0e:
          rc = ipvideo_decode_0xe (s, frame, &data, &len);
          break;
        case 0x0f:
          rc = ipvideo_decode_0xf (s, frame, &data, &len);
          break;
      }

      if (rc != 0)
        return rc;

      frame += 8;
    }
    frame += 7 * s->width;
  }

  return 0;
}
