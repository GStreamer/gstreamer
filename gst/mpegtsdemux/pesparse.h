/*
 * pesparse.h : MPEG PES parsing utility
 * Copyright (C) 2011 Edward Hervey <bilboed@gmail.com>
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

#ifndef __PES_PARSE_H__
#define __PES_PARSE_H__

#include <gst/gst.h>
#include "gstmpegdefs.h"

G_BEGIN_DECLS

typedef enum {
  PES_FLAG_PRIORITY		= 1 << 3,	/* PES_priority (present: high-priority) */
  PES_FLAG_DATA_ALIGNMENT	= 1 << 2,	/* data_alignment_indicator */
  PES_FLAG_COPYRIGHT		= 1 << 1,	/* copyright */
  PES_FLAG_ORIGINAL_OR_COPY	= 1 << 0	/* original_or_copy */
} PESHeaderFlags;

typedef enum {
  PES_TRICK_MODE_FAST_FORWARD	= 0x000,
  PES_TRICK_MODE_SLOW_MOTION    = 0x001,
  PES_TRICK_MODE_FREEZE_FRAME	= 0x010,
  PES_TRICK_MODE_FAST_REVERSE	= 0x011,
  PES_TRICK_MODE_SLOW_REVERSE	= 0x100,
  /* ... */
  PES_TRICK_MODE_INVALID	= 0xfff	/* Not present or invalid */
} PESTrickModeControl;

typedef enum {
  PES_FIELD_ID_TOP_ONLY		= 0x00, /* Display from top field only */
  PES_FIELD_ID_BOTTOM_ONLY	= 0x01, /* Display from bottom field only */
  PES_FIELD_ID_COMPLETE_FRAME	= 0x10, /* Display complete frame */
  PES_FIELD_ID_INVALID		= 0x11	/* Reserved/Invalid */
} PESFieldID;

typedef enum {
  PES_PARSING_OK	= 0,	/* Header fully parsed and valid */
  PES_PARSING_BAD	= 1,	/* Header invalid (CRC error for ex) */
  PES_PARSING_NEED_MORE = 2	/* Not enough data to parse header */
} PESParsingResult;

typedef struct {
  guint8	stream_id;	/* See ID_* in gstmpegdefs.h */
  guint16	packet_length;	/* The size of the PES header and PES data
				 * (if 0 => unbounded packet) */
  guint16	header_size;	/* The complete size of the PES header */

  /* All remaining entries in this structure are optional */
  guint8	scrambling_control; /* 0x00  : Not scrambled/unspecified,
				     * The following are according to ETSI TS 101 154
				     * 0x01  : reserved for future DVB use
				     * 0x10  : PES packet scrambled with Even key
				     * 0x11  : PES packet scrambled with Odd key
				     */
  PESHeaderFlags flags;

  guint64	PTS;		/* PTS (-1 if not present or invalid) */
  guint64	DTS;		/* DTS (-1 if not present or invalid) */
  guint64	ESCR;		/* ESCR (-1 if not present or invalid) */

  guint32	ES_rate;	/* in bytes/seconds (0 if not present or invalid) */
  PESTrickModeControl	trick_mode;
  
  /* Only valid for _FAST_FORWARD, _FAST_REVERSE and _FREEZE_FRAME */
  PESFieldID	field_id;
  /* Only valid for _FAST_FORWARD and _FAST_REVERSE */
  gboolean	intra_slice_refresh;
  guint8	frequency_truncation;
  /* Only valid for _SLOW_FORWARD and _SLOW_REVERSE */
  guint8	rep_cntrl;

  guint8	additional_copy_info; /* Private data */
  guint16	previous_PES_packet_CRC;

  /* Extension fields */
  const guint8*	private_data;			/* PES_private_data, 16 bytes long */
  guint8	pack_header_size;		/* Size of pack_header in bytes */
  const guint8*	pack_header;
  gint8		program_packet_sequence_counter; /* -1 if not present or invalid */
  gboolean	MPEG1_MPEG2_identifier;
  guint8	original_stuff_length;

  guint32	P_STD_buffer_size; /* P-STD buffer size in bytes (0 if invalid
				    * or not present */

  gsize		extension_field_length;
  guint8	stream_id_extension; /* Only valid if stream_id == ID_EXTENDED_STREAM_ID */
  const guint8*	stream_id_extension_data;
} PESHeader;

PESParsingResult mpegts_parse_pes_header (const guint8* data, gsize size,
					  PESHeader *res, gint *offset);
void init_pes_parser (void);
G_END_DECLS
#endif /* __PES_PARSE_H__ */
