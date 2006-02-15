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

#ifndef __ASFHEADERS_H__
#define __ASFHEADERS_H__

typedef struct {
  guint32 v1;
  guint32 v2;
  guint32 v3;
  guint32 v4;
} ASFGuid;
  


typedef struct {
  guint8       obj_id;
  const gchar *obj_id_str;
  ASFGuid      guid;
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
  ASF_OBJ_LANGUAGE_LIST,
  ASF_OBJ_METADATA_OBJECT,
  ASF_OBJ_EXTENDED_STREAM_PROPS
};

enum {
  ASF_STREAM_UNDEFINED = 0,
  ASF_STREAM_VIDEO,
  ASF_STREAM_AUDIO
};

enum {
  ASF_CORRECTION_UNDEFINED = 0,
  ASF_CORRECTION_ON,
  ASF_CORRECTION_OFF
};

extern const ASFGuidHash asf_correction_guids[];

extern const ASFGuidHash asf_stream_guids[];

extern const ASFGuidHash asf_object_guids[];

/* GUID utilities */
guint32        gst_asf_identify_guid (const ASFGuidHash * guids,
                                      ASFGuid           * guid);

const gchar   *gst_asf_get_guid_nick (const ASFGuidHash * guids,
                                      guint32             obj_id);


struct _asf_obj_header {
  guint32 num_objects;
  guint8  unknown1;
  guint8  unknown2;
};

typedef struct _asf_obj_header asf_obj_header;

struct _asf_obj_header_ext {
  ASFGuid reserved1;
  guint16 reserved2;
  guint32 data_size;
};

typedef struct _asf_obj_header_ext asf_obj_header_ext;

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
  guint8  span;
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
};

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
  /* guint8  unknown2; FIXME: this object is supposed to be 26 bytes?! */
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

#endif /* __ASFHEADERS_H__ */
