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

#ifndef ASFHEADERS_H
#define ASFHEADERS_H

typedef struct {
  guint32 v1;
  guint32 v2;
  guint32 v3;
  guint32 v4;
} ASFGuid;
  


typedef struct {
  guint8     obj_id;
  ASFGuid guid;
} ASFGuidHash;

enum {
  ASF_OBJ_UNDEFINED = 0,
  ASF_OBJ_STREAM,
  ASF_OBJ_DATA,
  ASF_OBJ_FILE,
  ASF_OBJ_HEADER,
  ASF_OBJ_CONCEAL_NONE,
  ASF_OBJ_COMMENT,
  ASF_OBJ_CODEC_COMMENT,
  ASF_OBJ_CODEC_COMMENT1,
  ASF_OBJ_INDEX,
  ASF_OBJ_HEAD1,
  ASF_OBJ_HEAD2,
  ASF_OBJ_PADDING,
  ASF_OBJ_BITRATE_PROPS,
  ASF_OBJ_EXT_CONTENT_DESC,
  ASF_OBJ_BITRATE_MUTEX,
};

enum {
  ASF_STREAM_UNDEFINED = 0,
  ASF_STREAM_VIDEO,
  ASF_STREAM_AUDIO,
};

enum {
  ASF_CORRECTION_UNDEFINED = 0,
  ASF_CORRECTION_ON,
  ASF_CORRECTION_OFF,
};

static ASFGuidHash asf_correction_guids[] = {
  { ASF_CORRECTION_ON,     { 0xBFC3CD50, 0x11CF618F, 0xAA00B28B, 0x20E2B400 }},
/*  { ASF_CORRECTION_OFF,    { 0x20FB5700, 0x11CF5B55, 0x8000FDA8, 0x2B445C5F }},*/
  { ASF_CORRECTION_OFF,    { 0x49F1A440, 0x11D04ECE, 0xA000ACA3, 0xF64803C9 }},
  { ASF_CORRECTION_UNDEFINED,  { 0, 0, 0, 0 }},
};

static ASFGuidHash asf_stream_guids[] = {
  { ASF_STREAM_VIDEO,      { 0xBC19EFC0, 0x11CF5B4D, 0x8000FDA8, 0x2B445C5F }},
  { ASF_STREAM_AUDIO,      { 0xF8699E40, 0x11CF5B4D, 0x8000FDA8, 0x2B445C5F }},
  { ASF_STREAM_UNDEFINED,  { 0, 0, 0, 0 }},
};

struct _asf_obj_header {
  guint32 num_objects;
  guint8  unknown1;
  guint8  unknown2;
};

typedef struct _asf_obj_header asf_obj_header;

struct _asf_obj_comment {
  guint16 title_length;
  guint16 author_length;
  guint16 copyright_length;
  guint16 description_length;
  guint16 rating_length;
};

typedef struct _asf_obj_comment asf_obj_comment;

struct _asf_obj_file {
  ASFGuid file_id;
  guint64 file_size;
  guint64 creation_time;
  guint64 packets_count;
  guint64 play_time;
  guint64 send_time;
  guint64 preroll;
  guint32 flags;
  guint32 min_pktsize;
  guint32 max_pktsize;
  guint32 min_bitrate;
};

typedef struct _asf_obj_file asf_obj_file;

struct _asf_obj_stream {
  ASFGuid type;
  ASFGuid correction;
  guint64 unknown1;
  guint32 type_specific_size;
  guint32 stream_specific_size;
  guint16 id;
  guint32 unknown2;
};

typedef struct _asf_obj_stream asf_obj_stream;

struct _asf_stream_audio {
  guint16 codec_tag;
  guint16 channels;
  guint32 sample_rate;
  guint32 byte_rate;
  guint16 block_align;
  guint16 word_size;
  guint16 size;
};

typedef struct _asf_stream_audio asf_stream_audio;

struct _asf_stream_correction {
  guint16 packet_size;
  guint16 chunk_size;
  guint16 data_size;
  guint8  silence_data;
};

typedef struct _asf_stream_correction asf_stream_correction;

struct _asf_stream_video {
  guint32 width;
  guint32 height;
  guint8  unknown;
  guint16 size;
} __attribute__ ((__packed__));
/* the packed attribute is needed to prevent this thing
 * from expanding 'unknown' to 16 bits */

typedef struct _asf_stream_video asf_stream_video;

struct _asf_stream_video_format {
  guint32 size;
  guint32 width;
  guint32 height;
  guint16 planes;
  guint16 depth;
  guint32 tag;
  guint32 image_size;
  guint32 xpels_meter;
  guint32 ypels_meter;
  guint32 num_colors;
  guint32 imp_colors;
};

typedef struct _asf_stream_video_format asf_stream_video_format;

struct _asf_obj_data {
  ASFGuid file_id;
  guint64 packets;
  guint8  unknown1;
  guint8  unknown2;
  guint8  correction;
};

typedef struct _asf_obj_data asf_obj_data;

struct _asf_obj_data_correction {
  guint8 type;
  guint8 cycle;
};

typedef struct _asf_obj_data_correction asf_obj_data_correction;

struct _asf_obj_data_packet {
  guint8  flags;
  guint8  property;
};

typedef struct _asf_obj_data_packet asf_obj_data_packet;

struct _asf_packet_info {
  guint32  padsize;
  guint8   replicsizetype;
  guint8   fragoffsettype;
  guint8   seqtype;
  guint8   segsizetype;
  gboolean multiple;
  guint32  size_left;
};

typedef struct _asf_packet_info asf_packet_info;

struct _asf_segment_info {
  guint8   stream_number;
  guint32  chunk_size;
  guint32  frag_offset;
  guint32  segment_size;
  guint32  sequence;
  guint32  frag_timestamp;
  gboolean compressed;
};

typedef struct _asf_segment_info asf_segment_info;

struct _asf_replicated_data {
  guint32 object_size;
  guint32 frag_timestamp;
};

typedef struct _asf_replicated_data asf_replicated_data;

struct _asf_bitrate_record {
  guint16 stream_id;
  guint32 bitrate;
};

typedef struct _asf_bitrate_record asf_bitrate_record;

static ASFGuidHash asf_object_guids[] = {
  { ASF_OBJ_STREAM,           { 0xB7DC0791, 0x11CFA9B7, 0xC000E68E, 0x6553200C }},
  { ASF_OBJ_DATA,             { 0x75b22636, 0x11cf668e, 0xAA00D9a6, 0x6Cce6200 }},
  { ASF_OBJ_FILE,             { 0x8CABDCA1, 0x11CFA947, 0xC000E48E, 0x6553200C }},
  { ASF_OBJ_HEADER,           { 0x75B22630, 0x11CF668E, 0xAA00D9A6, 0x6CCE6200 }},
  { ASF_OBJ_CONCEAL_NONE,     { 0x20fb5700, 0x11cf5b55, 0x8000FDa8, 0x2B445C5f }},
  { ASF_OBJ_COMMENT,          { 0x75b22633, 0x11cf668e, 0xAA00D9a6, 0x6Cce6200 }},
  { ASF_OBJ_CODEC_COMMENT,    { 0x86D15240, 0x11D0311D, 0xA000A4A3, 0xF64803C9 }},
  { ASF_OBJ_CODEC_COMMENT1,   { 0x86d15241, 0x11d0311d, 0xA000A4a3, 0xF64803c9 }},
  { ASF_OBJ_INDEX,            { 0x33000890, 0x11cfe5b1, 0xA000F489, 0xCB4903c9 }},
  { ASF_OBJ_HEAD1,            { 0x5fbf03b5, 0x11cfa92e, 0xC000E38e, 0x6553200c }},
  { ASF_OBJ_HEAD2,            { 0xabd3d211, 0x11cfa9ba, 0xC000E68e, 0x6553200c }},
  { ASF_OBJ_PADDING,          { 0x1806D474, 0x4509CADF, 0xAB9ABAA4, 0xE8AA96CD }},
  { ASF_OBJ_BITRATE_PROPS,    { 0x7bf875ce, 0x11d1468d, 0x6000828d, 0xb2a2c997 }},
  { ASF_OBJ_EXT_CONTENT_DESC, { 0xd2d0a440, 0x11d2e307, 0xa000f097, 0x50a85ec9 }},
  { ASF_OBJ_BITRATE_MUTEX,    { 0xd6e229dc, 0x11d135da, 0xa0003490, 0xbe4903c9 }},
  { ASF_OBJ_UNDEFINED,        { 0, 0, 0, 0 }},
};


/* ASFHEADERS_H */
#endif
