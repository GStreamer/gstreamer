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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_FLX_FMT_H__
#define __GST_FLX_FMT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

enum Flx_TypeChunk
{
	/* frame chunks */
	FLX_PREFIX_TYPE		= 0xf100,
	FLX_SCRIPT_CHUNK	= 0xf1e0,
	FLX_FRAME_TYPE		= 0xf1fa,
	FLX_SEGMENT_TABLE	= 0xf1fb,
	FLX_HUFFMAN_TABLE	= 0xf1fc,

	/* sub chunks */
	FLX_CEL_DATA		= 3,
	FLX_COLOR256		= 4,
	FLX_SS2			= 7,
        FLX_COLOR64		= 11,
        FLX_LC			= 12,
        FLX_BLACK		= 13,
        FLX_BRUN		= 15,
        FLX_COPY		= 16,
        FLX_MINI		= 18,
	FLX_DTA_RUN		= 25,
	FLX_DTA_COPY		= 26,
	FLX_DTA_LC		= 27,
	FLX_LABEL		= 31,
	FLX_BMP_MASK		= 32,
	FLX_MLEV_MASK		= 33,
	FLX_SEGMENT		= 34,
	FLX_KEY_IMAGE		= 35,
	FLX_KEY_PAL		= 36,
	FLX_REGION		= 37,
	FLX_WAVE		= 38,
	FLX_USERSTRING		= 39,
	FLX_RGN_MASK		= 40

};

enum Flx_MagicHdr
{
	FLX_MAGICHDR_FLI	= 0xaf11,
	FLX_MAGICHDR_FLC	= 0xaf12,
	FLX_MAGICHDR_FLX	= 0xaf44,
	FLX_MAGICHDR_HUFFBWT	= 0xaf30
};

typedef struct _FlxHeader 
{
	guint32	size;
	guint16 type;
	guint16	frames;
	guint16 width,height,depth,flags;
	guint32 speed;
	guint16 reserved1;
	/* FLC */ 
	guint32 created,creator,updated,updater;
	guint16 aspect_dx, aspect_dy;
	/* EGI */
	guint16 ext_flags,keyframes,totalframes;
	guint32 req_memory;
	guint16 max_regions,transp_num;
	guchar	reserved2[24];
	/* FLC */
	guint32 oframe1,oframe2;
	guchar	reserved3[40];
} FlxHeader;
#define FlxHeaderSize 128

typedef struct _FlxFrameChunk
{
	guint32	size;
	guint16	id;
} FlxFrameChunk;
#define FlxFrameChunkSize 6

typedef struct _FlxPrefixChunk
{
	guint16 chunks;
	guchar  reserved[8];
} FlxPrefixChunk;

typedef struct _FlxSegmentTable
{
	guint16 segments;
} FlxSegmentTable;

typedef struct _FlxHuffmanTable
{
	guint16 codelength;
	guint16 numcodes;
	guchar	reserved[6];
} FlxHuffmanTable;

typedef struct _FlxFrameType
{
	guint16 chunks;
	guint16 delay;
	guchar	reserved[6];
} FlxFrameType;
#define FlxFrameTypeSize 10

#if G_BYTE_ORDER == G_BIG_ENDIAN 
#define LE_TO_BE_16(i16) ((guint16) (((i16) << 8) | ((i16) >> 8)))
#define LE_TO_BE_32(i32) \
    (((guint32) (LE_TO_BE_16((guint16) (i32))) << 16) | (LE_TO_BE_16((i32) >> 16)))

#define FLX_FRAME_TYPE_FIX_ENDIANNESS(frm_type_p) \
    do { \
     (frm_type_p)->chunks = LE_TO_BE_16((frm_type_p)->chunks); \
     (frm_type_p)->delay = LE_TO_BE_16((frm_type_p)->delay); \
    } while(0)

#define FLX_HUFFMAN_TABLE_FIX_ENDIANNESS(hffmn_table_p) \
    do { \
     (hffmn_table_p)->codelength = \
	LE_TO_BE_16((hffmn_table_p)->codelength); \
     (hffmn_table_p)->numcodes = LE_TO_BE_16((hffmn_table_p)->numcodes); \
    } while(0)

#define FLX_SEGMENT_TABLE_FIX_ENDIANNESS(sgmnt_table_p) \
     ((sgmnt_table_p)->segments = LE_TO_BE_16((sgmnt_table_p)->segments))

#define FLX_PREFIX_CHUNK_FIX_ENDIANNESS(prfx_chnk_p) \
    do { \
     (prfx_chnk_p)->chunks = LE_TO_BE_16((prfx_chnk_p)->chunks); \
    } while(0)

#define FLX_FRAME_CHUNK_FIX_ENDIANNESS(frm_chnk_p) \
    do { \
     (frm_chnk_p)->size = LE_TO_BE_32((frm_chnk_p)->size); \
     (frm_chnk_p)->id = LE_TO_BE_16((frm_chnk_p)->id); \
    } while(0)

#define FLX_HDR_FIX_ENDIANNESS(hdr_p) \
    do { \
     (hdr_p)->size = LE_TO_BE_32((hdr_p)->size); \
     (hdr_p)->type = LE_TO_BE_16((hdr_p)->type); \
     (hdr_p)->frames = LE_TO_BE_16((hdr_p)->frames); \
     (hdr_p)->width = LE_TO_BE_16((hdr_p)->width); \
     (hdr_p)->height = LE_TO_BE_16((hdr_p)->height); \
     (hdr_p)->depth = LE_TO_BE_16((hdr_p)->depth); \
     (hdr_p)->flags = LE_TO_BE_16((hdr_p)->flags); \
     (hdr_p)->speed = LE_TO_BE_32((hdr_p)->speed); \
     (hdr_p)->reserved1 = LE_TO_BE_16((hdr_p)->reserved1); \
     (hdr_p)->created = LE_TO_BE_32((hdr_p)->created); \
     (hdr_p)->creator = LE_TO_BE_32((hdr_p)->creator); \
     (hdr_p)->updated = LE_TO_BE_32((hdr_p)->updated); \
     (hdr_p)->updater = LE_TO_BE_32((hdr_p)->updater); \
     (hdr_p)->aspect_dx = LE_TO_BE_16((hdr_p)->aspect_dx); \
     (hdr_p)->aspect_dy = LE_TO_BE_16((hdr_p)->aspect_dy); \
     (hdr_p)->ext_flags = LE_TO_BE_16((hdr_p)->ext_flags); \
     (hdr_p)->keyframes = LE_TO_BE_16((hdr_p)->keyframes); \
     (hdr_p)->totalframes = LE_TO_BE_16((hdr_p)->totalframes); \
     (hdr_p)->req_memory = LE_TO_BE_32((hdr_p)->req_memory); \
     (hdr_p)->max_regions = LE_TO_BE_16((hdr_p)->max_regions); \
     (hdr_p)->transp_num = LE_TO_BE_16((hdr_p)->transp_num); \
     (hdr_p)->oframe1 = LE_TO_BE_32((hdr_p)->oframe1); \
     (hdr_p)->oframe2 = LE_TO_BE_32((hdr_p)->oframe2); \
    } while(0)
#else

#define LE_TO_BE_16(i16) ((i16))
#define LE_TO_BE_32(i32) ((i32))

#define FLX_FRAME_TYPE_FIX_ENDIANNESS(frm_type_p)
#define FLX_HUFFMAN_TABLE_FIX_ENDIANNESS(hffmn_table_p)
#define FLX_SEGMENT_TABLE_FIX_ENDIANNESS(sgmnt_table_p)
#define FLX_PREFIX_CHUNK_FIX_ENDIANNESS(prfx_chnk_p)
#define FLX_FRAME_CHUNK_FIX_ENDIANNESS(frm_chnk_p)
#define FLX_HDR_FIX_ENDIANNESS(hdr_p)

#endif /* G_BYTE_ORDER == G_BIG_ENDIAN */

G_END_DECLS

#endif /* __GST_FLX_FMT_H__ */
