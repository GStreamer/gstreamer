/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "ac3_padder.h"

#define IEC61937_DATA_TYPE_AC3 1

struct frmsize_s
{
  unsigned short bit_rate;
  unsigned short frm_size[3];
};


static const struct frmsize_s frmsizecod_tbl[64] = {
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



/* Go one byte forward in the input buffer. */
#define ac3p_in_fw(padder) ((padder)->in_ptr++, (padder)->remaining--)

/* Go one byte forward in the output buffer. */
#define ac3p_out_fw(padder) ((padder)->out_ptr++, (padder)->bytes_to_copy--)


/**
 * ac3p_init
 * @padder: The padder structure to initialize.
 *
 * Initializes an AC3 stream padder.  This structure can be
 * subsequently used to parse an AC3 stream and convert it to IEC958
 * (S/PDIF) padded packets.
 */
extern void
ac3p_init (ac3_padder * padder)
{
  const char sync[4] = { 0x72, 0xF8, 0x1F, 0x4E };

  padder->state = AC3P_STATE_SYNC1;

  /* No material to read yet. */
  padder->remaining = 0;

  /* Initialize the sync bytes in the frame. */
  memcpy (padder->frame.header, sync, 4);
}


/**
 * ac3_push_data
 * @padder The padder structure.
 * @data A pointer to a buffer with new data to parse.  This should 
 * correspond to a new piece of a stream containing raw AC3 data.
 * @size The number of available bytes in the buffer.
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
  padder->in_ptr = data;
  padder->remaining = size;
}


/**
 * ac3p_parse
 * @padder The padder structure.
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
 */
extern int
ac3p_parse (ac3_padder * padder)
{
  while (padder->remaining > 0) {
    switch (padder->state) {
      case AC3P_STATE_SYNC1:
	if (*(padder->in_ptr) == 0x0b) {
	  /* The first sync byte was found.  Go to the next state. */
	  padder->frame.sync_byte1 = 0x0b;
	  padder->state = AC3P_STATE_SYNC2;
	}
	ac3p_in_fw (padder);
	break;

      case AC3P_STATE_SYNC2:
	if (*(padder->in_ptr) == 0x77) {
	  /* The second sync byte was seen right after the first.  Go to
	     the next state. */
	  padder->frame.sync_byte2 = 0x77;
	  padder->state = AC3P_STATE_HEADER;

	  /* Skip one byte. */
	  ac3p_in_fw (padder);

	  /* Prepare for reading the header. */
	  padder->out_ptr = (guchar *) & (padder->frame.crc1);
	  /* Discount the 2 sync bytes from the header size. */
	  padder->bytes_to_copy = AC3P_AC3_HEADER_SIZE - 2;
	} else {
	  /* The second sync byte was not seen.  Go back to the
	     first state. */
	  padder->state = AC3P_STATE_SYNC1;
	}
	break;

      case AC3P_STATE_HEADER:
	if (padder->bytes_to_copy > 0) {
	  /* Copy one byte. */
	  *(padder->out_ptr) = *(padder->in_ptr);
	  ac3p_in_fw (padder);
	  ac3p_out_fw (padder);
	} else {
	  int fscod;

	  /* The header is ready: */

	  fscod = (padder->frame.code >> 6) & 0x03;

	  /* Calculate the frame size. */
	  padder->ac3_frame_size =
	      2 * frmsizecod_tbl[padder->frame.code & 0x3f].frm_size[fscod];

	  /* Set up the IEC header. */
	  if (padder->ac3_frame_size > 0) {
	    padder->frame.header[4] = IEC61937_DATA_TYPE_AC3;
	  } else {
	    /* Don't know what it is, better be careful. */
	    padder->state = AC3P_STATE_SYNC1;
	    break;
	  }
	  padder->frame.header[5] = 0x00;
	  padder->frame.header[6] = (padder->ac3_frame_size * 8) & 0xFF;
	  padder->frame.header[7] = ((padder->ac3_frame_size * 8) >> 8) & 0xFF;

	  /* Prepare for reading the body. */
	  padder->bytes_to_copy = padder->ac3_frame_size - AC3P_AC3_HEADER_SIZE;
	  padder->state = AC3P_STATE_CONTENT;
	}
	break;

      case AC3P_STATE_CONTENT:
	if (padder->bytes_to_copy > 0) {
	  /* Copy one byte. */
	  *(padder->out_ptr) = *(padder->in_ptr);
	  ac3p_in_fw (padder);
	  ac3p_out_fw (padder);
	} else {
	  guint16 *ptr, i;

	  /* Frame ready.  Prepare for output: */

	  /* Zero the non AC3 portion of the padded frame. */
	  memset (&(padder->frame.sync_byte1) + padder->ac3_frame_size, 0,
	      AC3P_IEC_FRAME_SIZE - AC3P_IEC_HEADER_SIZE -
	      padder->ac3_frame_size);

	  /* Fix the byte order in the AC3 portion: */
	  ptr = (guint16 *) & (padder->frame.sync_byte1);
	  i = padder->ac3_frame_size / 2;
	  while (i > 0) {
	    *ptr = GUINT16_TO_BE (*ptr);
	    ptr++;
	    i--;
	  }

	  /* Start over again. */
	  padder->state = AC3P_STATE_SYNC1;

	  return AC3P_EVENT_FRAME;
	}
	break;
    }
  }

  return AC3P_EVENT_PUSH;
}
