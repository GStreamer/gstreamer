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

#ifndef __ASFHEADERS_H__
#define __ASFHEADERS_H__

G_BEGIN_DECLS

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

typedef enum {
  ASF_OBJ_UNDEFINED = 0,
  ASF_OBJ_STREAM,
  ASF_OBJ_DATA,
  ASF_OBJ_FILE,
  ASF_OBJ_HEADER,
  ASF_OBJ_CONCEAL_NONE,
  ASF_OBJ_COMMENT,
  ASF_OBJ_CODEC_COMMENT,
  ASF_OBJ_CODEC_COMMENT1,
  ASF_OBJ_SIMPLE_INDEX,
  ASF_OBJ_INDEX,
  ASF_OBJ_HEAD1,
  ASF_OBJ_HEAD2,
  ASF_OBJ_PADDING,
  ASF_OBJ_BITRATE_PROPS,
  ASF_OBJ_EXT_CONTENT_DESC,
  ASF_OBJ_BITRATE_MUTEX,
  ASF_OBJ_LANGUAGE_LIST,
  ASF_OBJ_METADATA_OBJECT,
  ASF_OBJ_EXTENDED_STREAM_PROPS,
  ASF_OBJ_COMPATIBILITY,
  ASF_OBJ_INDEX_PLACEHOLDER,
  ASF_OBJ_INDEX_PARAMETERS,
  ASF_OBJ_ADVANCED_MUTUAL_EXCLUSION,
  ASF_OBJ_STREAM_PRIORITIZATION,
  ASF_OBJ_CONTENT_ENCRYPTION,
  ASF_OBJ_EXT_CONTENT_ENCRYPTION,
  ASF_OBJ_DIGITAL_SIGNATURE_OBJECT,
  ASF_OBJ_SCRIPT_COMMAND,
  ASF_OBJ_MARKER,
  ASF_OBJ_UNKNOWN_ENCRYPTION_OBJECT
} AsfObjectID;

typedef enum {
  ASF_STREAM_UNDEFINED = 0,
  ASF_STREAM_VIDEO,
  ASF_STREAM_AUDIO
} AsfStreamType;

typedef enum {
  ASF_CORRECTION_UNDEFINED = 0,
  ASF_CORRECTION_ON,
  ASF_CORRECTION_OFF
} AsfCorrectionType;

typedef enum {
  ASF_PAYLOAD_EXTENSION_UNDEFINED = 0,
  ASF_PAYLOAD_EXTENSION_DURATION,
  ASF_PAYLOAD_EXTENSION_SYSTEM_CONTENT,
  ASF_PAYLOAD_EXTENSION_SYSTEM_PIXEL_ASPECT_RATIO
} AsfPayloadExtensionID;

extern const ASFGuidHash asf_payload_ext_guids[];

extern const ASFGuidHash asf_correction_guids[];

extern const ASFGuidHash asf_stream_guids[];

extern const ASFGuidHash asf_object_guids[];

/* GUID utilities */
guint32        gst_asf_identify_guid (const ASFGuidHash * guids,
                                      ASFGuid           * guid);

const gchar   *gst_asf_get_guid_nick (const ASFGuidHash * guids,
                                      guint32             obj_id);

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

struct _asf_obj_data_correction {
  guint8 type;
  guint8 cycle;
};

typedef struct _asf_obj_data_correction asf_obj_data_correction;

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

G_END_DECLS

#endif /* __ASFHEADERS_H__ */
