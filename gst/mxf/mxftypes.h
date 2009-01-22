/* GStreamer
 * Copyright (C) 2008-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/* Definitions of the basic MXF types, excluding structural metadata */

#ifndef __MXF_TYPES_H__
#define __MXF_TYPES_H__

#include <gst/gst.h>

/* SMPTE 377M 3.2 */
typedef struct {
  guint8 u[16];
} MXFUL;

/* SMPTE 377M 3.2 */
typedef struct {
  guint8 u[32];
} MXFUMID;

/* SMPTE 377M 3.3 */
typedef struct {
  gint16 year;
  guint8 month;
  guint8 day;
  guint8 hour;
  guint8 minute;
  guint8 second;
  guint8 quarter_msecond;
} MXFTimestamp;

/* SMPTE 377M 3.3 */
typedef struct {
  guint16 major;
  guint16 minor;
  guint16 patch;
  guint16 build;
  guint16 release;
} MXFProductVersion;

/* SMPTE 377M 3.3 */
typedef struct {
  gint32 n;
  gint32 d;
} MXFFraction;

/* SMPTE 377M 8.3 */
typedef struct {
  MXFUL key;
  guint16 size;
  guint8 *data;
} MXFLocalTag;

/* SMPTE 377M 11.1 */
typedef struct {
  guint32 body_sid;
  guint64 offset;
} MXFRandomIndexPackEntry;

typedef enum {
  MXF_PARTITION_PACK_HEADER,
  MXF_PARTITION_PACK_BODY,
  MXF_PARTITION_PACK_FOOTER
} MXFPartitionPackType;

/* SMPTE 377M 6.1, Table 1 and 2 */
typedef struct {
  gboolean valid;

  MXFPartitionPackType type;

  gboolean closed;
  gboolean complete;

  guint16 major_version;
  guint16 minor_version;

  guint32 kag_size;

  guint64 this_partition;
  guint64 prev_partition;
  guint64 footer_partition;

  guint64 header_byte_count;
  guint64 index_byte_count;

  guint32 index_sid;

  guint64 body_offset;

  guint32 body_sid;

  MXFUL operational_pattern;

  guint32 n_essence_containers;
  MXFUL *essence_containers;
} MXFPartitionPack;

/* SMPTE 377M 8.1 */
typedef struct {
  gboolean valid;

  GHashTable *mappings;
} MXFPrimerPack;

/* SMPTE 377M 10.2.3 */
typedef struct {
  gint8 pos_table_index;
  guint8 slice;
  guint32 element_delta;
} MXFDeltaEntry;

typedef struct {
  gint8 temporal_offset;
  gint8 key_frame_offset;

  guint8 flags;
  guint64 stream_offset;
  
  guint32 *slice_offset;
  MXFFraction *pos_table;
} MXFIndexEntry;

typedef struct {
  MXFUL instance_id;
  MXFFraction index_edit_rate;
  gint64 index_start_position;
  gint64 index_duration;
  guint32 edit_unit_byte_count;
  guint32 index_sid;
  guint32 body_sid;
  guint8 slice_count;
  guint8 pos_table_count;

  guint32 n_delta_entries;
  MXFDeltaEntry *delta_entries;

  guint32 n_index_entries;
  MXFIndexEntry *index_entries;

  GHashTable *other_tags;
} MXFIndexTableSegment;

#endif /* __MXF_TYPES_H__ */
