/* GStreamer
 * Copyright (C) 2003, 2004 Martin Soto <martinsoto@users.sourceforge.net>
 *               2005 Michael Smith <msmith@fluendo.com>
 *
 * ac3_padder.c: Pad AC3 frames for use with an SPDIF interface.
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

#include <stdio.h>
#include <string.h>

#include "ac3_padder.h"

struct frmsize_s
{
  unsigned short bit_rate;
  unsigned short frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[] = {
  {32, {64, 69, 96}},
  {32, {64, 70, 96}},
  {40, {80, 87, 120}},
  {40, {80, 88, 120}},
  {48, {96, 104, 144}},
  {48, {96, 105, 144}},
  {56, {112, 121, 168}},
  {56, {112, 122, 168}},
  {64, {128, 139, 192}},
  {64, {128, 140, 192}},
  {80, {160, 174, 240}},
  {80, {160, 175, 240}},
  {96, {192, 208, 288}},
  {96, {192, 209, 288}},
  {112, {224, 243, 336}},
  {112, {224, 244, 336}},
  {128, {256, 278, 384}},
  {128, {256, 279, 384}},
  {160, {320, 348, 480}},
  {160, {320, 349, 480}},
  {192, {384, 417, 576}},
  {192, {384, 418, 576}},
  {224, {448, 487, 672}},
  {224, {448, 488, 672}},
  {256, {512, 557, 768}},
  {256, {512, 558, 768}},
  {320, {640, 696, 960}},
  {320, {640, 697, 960}},
  {384, {768, 835, 1152}},
  {384, {768, 836, 1152}},
  {448, {896, 975, 1344}},
  {448, {896, 976, 1344}},
  {512, {1024, 1114, 1536}},
  {512, {1024, 1115, 1536}},
  {576, {1152, 1253, 1728}},
  {576, {1152, 1254, 1728}},
  {640, {1280, 1393, 1920}},
  {640, {1280, 1394, 1920}}
};

static const guint16 ac3_crc_lut[256] = {
  0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011,
  0x8033, 0x0036, 0x003c, 0x8039, 0x0028, 0x802d, 0x8027, 0x0022,
  0x8063, 0x0066, 0x006c, 0x8069, 0x0078, 0x807d, 0x8077, 0x0072,
  0x0050, 0x8055, 0x805f, 0x005a, 0x804b, 0x004e, 0x0044, 0x8041,
  0x80c3, 0x00c6, 0x00cc, 0x80c9, 0x00d8, 0x80dd, 0x80d7, 0x00d2,
  0x00f0, 0x80f5, 0x80ff, 0x00fa, 0x80eb, 0x00ee, 0x00e4, 0x80e1,
  0x00a0, 0x80a5, 0x80af, 0x00aa, 0x80bb, 0x00be, 0x00b4, 0x80b1,
  0x8093, 0x0096, 0x009c, 0x8099, 0x0088, 0x808d, 0x8087, 0x0082,
  0x8183, 0x0186, 0x018c, 0x8189, 0x0198, 0x819d, 0x8197, 0x0192,
  0x01b0, 0x81b5, 0x81bf, 0x01ba, 0x81ab, 0x01ae, 0x01a4, 0x81a1,
  0x01e0, 0x81e5, 0x81ef, 0x01ea, 0x81fb, 0x01fe, 0x01f4, 0x81f1,
  0x81d3, 0x01d6, 0x01dc, 0x81d9, 0x01c8, 0x81cd, 0x81c7, 0x01c2,
  0x0140, 0x8145, 0x814f, 0x014a, 0x815b, 0x015e, 0x0154, 0x8151,
  0x8173, 0x0176, 0x017c, 0x8179, 0x0168, 0x816d, 0x8167, 0x0162,
  0x8123, 0x0126, 0x012c, 0x8129, 0x0138, 0x813d, 0x8137, 0x0132,
  0x0110, 0x8115, 0x811f, 0x011a, 0x810b, 0x010e, 0x0104, 0x8101,
  0x8303, 0x0306, 0x030c, 0x8309, 0x0318, 0x831d, 0x8317, 0x0312,
  0x0330, 0x8335, 0x833f, 0x033a, 0x832b, 0x032e, 0x0324, 0x8321,
  0x0360, 0x8365, 0x836f, 0x036a, 0x837b, 0x037e, 0x0374, 0x8371,
  0x8353, 0x0356, 0x035c, 0x8359, 0x0348, 0x834d, 0x8347, 0x0342,
  0x03c0, 0x83c5, 0x83cf, 0x03ca, 0x83db, 0x03de, 0x03d4, 0x83d1,
  0x83f3, 0x03f6, 0x03fc, 0x83f9, 0x03e8, 0x83ed, 0x83e7, 0x03e2,
  0x83a3, 0x03a6, 0x03ac, 0x83a9, 0x03b8, 0x83bd, 0x83b7, 0x03b2,
  0x0390, 0x8395, 0x839f, 0x039a, 0x838b, 0x038e, 0x0384, 0x8381,
  0x0280, 0x8285, 0x828f, 0x028a, 0x829b, 0x029e, 0x0294, 0x8291,
  0x82b3, 0x02b6, 0x02bc, 0x82b9, 0x02a8, 0x82ad, 0x82a7, 0x02a2,
  0x82e3, 0x02e6, 0x02ec, 0x82e9, 0x02f8, 0x82fd, 0x82f7, 0x02f2,
  0x02d0, 0x82d5, 0x82df, 0x02da, 0x82cb, 0x02ce, 0x02c4, 0x82c1,
  0x8243, 0x0246, 0x024c, 0x8249, 0x0258, 0x825d, 0x8257, 0x0252,
  0x0270, 0x8275, 0x827f, 0x027a, 0x826b, 0x026e, 0x0264, 0x8261,
  0x0220, 0x8225, 0x822f, 0x022a, 0x823b, 0x023e, 0x0234, 0x8231,
  0x8213, 0x0216, 0x021c, 0x8219, 0x0208, 0x820d, 0x8207, 0x0202
};

static gint ac3_sample_rates[] = { 48000, 44100, 32000, -1 };

typedef guint16 ac3_crc_state;

static void
ac3_crc_init (ac3_crc_state * state)
{
  *state = 0;
}

static void
ac3_crc_update (ac3_crc_state * state, guint8 * data, guint32 num_bytes)
{
  int i;

  for (i = 0; i < num_bytes; i++)
    *state = ac3_crc_lut[data[i] ^ (*state >> 8)] ^ (*state << 8);
}

static int
ac3_crc_validate (ac3_crc_state * state)
{
  return (*state == 0);
}

/**
 * ac3p_init
 * @padder: The padder structure to initialize.
 *
 * Initializes an AC3 stream padder.  This structure can be
 * subsequently used to parse an AC3 stream and convert it to IEC958
 * (S/PDIF) padded packets.
 */
void
ac3p_init (ac3_padder * padder)
{
  const guint8 sync[4] = { 0xF8, 0x72, 0x4E, 0x1F };

  padder->state = AC3P_STATE_SYNC1;

  padder->skipped = 0;

  /* No material to read yet. */
  padder->buffer = NULL;
  padder->buffer_end = 0;
  padder->buffer_cur = 0;
  padder->buffer_size = 0;

  /* Initialize the sync bytes in the frame. */
  memcpy (padder->frame.header, sync, 4);
}

void
ac3p_clear (ac3_padder * padder)
{
  g_free (padder->buffer);
}

static void
resync (ac3_padder * padder, int offset, int skipped)
{
  padder->buffer_cur -= offset;
  padder->state = AC3P_STATE_SYNC1;
  padder->skipped += skipped;

  /* We don't want our buffer to grow unboundedly if we fail to find sync, but
   * nor do we want to do this every time we call resync() */
  if (padder->buffer_cur > 4096) {
    memmove (padder->buffer, padder->buffer + padder->buffer_cur,
        padder->buffer_end - padder->buffer_cur);
    padder->buffer_end -= padder->buffer_cur;
    padder->buffer_cur = 0;
  }
}

/**
 * ac3_push_data:
 * @padder: The padder structure.
 * @data: A pointer to a buffer with new data to parse.  This should 
 * correspond to a new piece of a stream containing raw AC3 data.
 * @size: The number of available bytes in the buffer.
 *
 * Pushes a new buffer of data to be parsed by the ac3 padder.  The
 * ac3_parse() function will actually parse the data and report when
 * new frames are found.  This funcion should only be called once at
 * the beginning of the parsing process, or when the ac3_parse()
 * function returns the %AC3P_EVENT_PUSH event.
 */
extern void
ac3p_push_data (ac3_padder * padder, guchar * data, guint size)
{
  if (padder->buffer_end + size > padder->buffer_size) {
    padder->buffer_size = padder->buffer_end + size;
    padder->buffer = g_realloc (padder->buffer, padder->buffer_size);
  }

  memcpy (padder->buffer + padder->buffer_end, data, size);
  padder->buffer_end += size;
}

/**
 * ac3p_parse:
 * @padder: The padder structure.
 * 
 * Parses the bytes already pushed into the padder structure (see
 * ac3p_push_data()) and returns an event value depending on the
 * results of the parsing.
 *
 * Returns: %AC3P_EVENT_FRAME to indicate that a new AC3 was found and
 * padded for IEC958 transmission.  This frame can be read inmediatly
 * with ac3p_frame(). %AC3P_EVENT_PUSH to indicate that new data from
 * the input stream must be pushed into the padder using
 * ac3p_push_data().  This function should be called again after
 * pushing the data.
 *
 * Note that the returned data (which naturally comes in 16 bit sub-frames) is
 * big-endian, and may need to be byte-swapped for little-endian output.
 */
extern int
ac3p_parse (ac3_padder * padder)
{
  while (padder->buffer_cur < padder->buffer_end) {
    switch (padder->state) {
      case AC3P_STATE_SYNC1:
        if (padder->buffer[padder->buffer_cur++] == 0x0b) {
          /* The first sync byte was found.  Go to the next state. */
          padder->frame.sync_byte1 = 0x0b;
          padder->state = AC3P_STATE_SYNC2;
        } else
          resync (padder, 0, 1);
        break;

      case AC3P_STATE_SYNC2:
        if (padder->buffer[padder->buffer_cur++] == 0x77) {
          /* The second sync byte was seen right after the first.  Go to
             the next state. */
          padder->frame.sync_byte2 = 0x77;
          padder->state = AC3P_STATE_HEADER;

          /* Prepare for reading the header. */
          padder->out_ptr = (guchar *) & (padder->frame.crc1);
          /* Discount the 2 sync bytes from the header size. */
          padder->bytes_to_copy = AC3P_AC3_HEADER_SIZE - 2;
        } else {
          /* The second sync byte was not seen.  Go back to the
             first state. */
          resync (padder, 0, 2);
        }
        break;

      case AC3P_STATE_HEADER:
        if (padder->bytes_to_copy > 0) {
          /* Copy one byte. */
          *(padder->out_ptr++) = padder->buffer[padder->buffer_cur++];
          padder->bytes_to_copy--;
        } else {
          int fscod;

          /* The header is ready: */

          fscod = (padder->frame.code >> 6) & 0x03;

          /* fscod == 3 is a reserved code, we're not meant to do playback in
           * this case. frmsizecod being out-of-range (there are 38 entries) 
           * doesn't appear to be well-defined, but treat the same. 
           * The likely cause of both of these is false sync, so skip back to 
           * just in front of the previous sync word and start looking again.
           */
          if (fscod == 3 || (padder->frame.code & 0x3f) >= 38) {
            resync (padder, AC3P_AC3_HEADER_SIZE - 2, 2);
            continue;
          }

          padder->rate = ac3_sample_rates[fscod];

          /* Calculate the frame size (in 16 bit units). */
          padder->ac3_frame_size =
              frmsizecod_tbl[padder->frame.code & 0x3f].frm_size[fscod];

          /* Prepare for reading the body. */
          padder->bytes_to_copy = padder->ac3_frame_size * 2
              - AC3P_AC3_HEADER_SIZE;

          padder->state = AC3P_STATE_CONTENT;
        }
        break;

      case AC3P_STATE_CONTENT:
        if (padder->bytes_to_copy > 0) {
          /* Copy one byte. */
          *(padder->out_ptr++) = padder->buffer[padder->buffer_cur++];
          padder->bytes_to_copy--;
        } else {
          int framesize;
          int crclen1, crclen2;
          guint8 *tmp;
          ac3_crc_state state;

          /* Frame ready.  Prepare for output: */

          /* Zero the non AC3 portion of the padded frame. */
          memset (&(padder->frame.sync_byte1) + padder->ac3_frame_size * 2, 0,
              AC3P_IEC_FRAME_SIZE - AC3P_IEC_HEADER_SIZE -
              padder->ac3_frame_size * 2);

          /* Now checking the two CRCs. If either fails, then we re-feed all
           * the data starting immediately after the 16-bit syncword (which we
           * can now assume was a false sync) */

          /* Length of CRC1 is defined as 
             truncate(framesize/2) + truncate(framesize/8) 
             units (each of which is 16 bit, as is 'framesize'), but this 
             includes the syncword, which is NOT calculated as part of 
             the CRC. 
           */
          framesize = padder->ac3_frame_size;
          crclen1 = (framesize / 2 + framesize / 8) * 2 - 2;
          tmp = (guint8 *) (&(padder->frame.crc1));

          ac3_crc_init (&state);
          ac3_crc_update (&state, tmp, crclen1);

          if (!ac3_crc_validate (&state)) {
            /* Rewind current stream pointer to immediately following the last 
             * attempted sync point, then continue parsing in initial state */
            resync (padder, padder->ac3_frame_size - 2, 2);
            continue;
          }

          /* Now check CRC2, which covers the entire frame other than the 
           * 16-bit syncword */
          crclen2 = padder->ac3_frame_size * 2 - 2;

          ac3_crc_init (&state);
          ac3_crc_update (&state, tmp, crclen2);

          if (!ac3_crc_validate (&state)) {
            /* Rewind current stream pointer to immediately following the last 
             * attempted sync point, then continue parsing in initial state */
            resync (padder, padder->ac3_frame_size - 2, 2);
            continue;
          }

          /* Now set up the rest of the IEC header (we already filled in the 
             32 bit sync word. */

          /* Byte 5 has:
             bits 0-4: data type dependent. For AC3, the bottom 3 of these bits
             are copied from the AC3 frame (bsmod value), the top 2
             bits are reserved, and set to zero.
             bits 5-7: data stream number. We only do one data stream, so it's
             zero.
           */
          padder->frame.header[4] = padder->frame.bsidmod & 0x07;

          /* Byte 6:
             bits 0-4: data type. 1 for AC3.
             bits 5-6: reserved, zero.
             bit  7:   error_flag. Zero if frame contains no errors
           */
          padder->frame.header[5] = 1;  /* AC3 is defined as datatype 1 */

          /* Now, 16 bit frame size, in bits. */
          padder->frame.header[6] = ((padder->ac3_frame_size * 16) >> 8) & 0xFF;
          padder->frame.header[7] = (padder->ac3_frame_size * 16) & 0xFF;

          /* We're done, reset state and signal that we have a frame */
          padder->skipped = 0;
          padder->state = AC3P_STATE_SYNC1;

          memmove (padder->buffer, padder->buffer + padder->buffer_cur,
              padder->buffer_end - padder->buffer_cur);
          padder->buffer_end -= padder->buffer_cur;
          padder->buffer_cur = 0;

          return AC3P_EVENT_FRAME;
        }

        break;
    }
  }

  return AC3P_EVENT_PUSH;

}
