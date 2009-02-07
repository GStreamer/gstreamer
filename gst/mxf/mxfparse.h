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

/* Handling of the basic MXF types */

#ifndef __MXF_PARSE_H__
#define __MXF_PARSE_H__

#include <string.h>

#include "mxftypes.h"
#include "mxfmetadata.h"

typedef GstFlowReturn (*MXFEssenceElementHandleFunc) (const MXFUL *key, GstBuffer *buffer, GstCaps *caps, MXFMetadataTimelineTrack *track, gpointer mapping_data, GstBuffer **outbuf);

typedef struct {
  gboolean (*handles_track) (const MXFMetadataTimelineTrack *track);
  GstCaps * (*create_caps) (MXFMetadataTimelineTrack *track, GstTagList **tags, MXFEssenceElementHandleFunc *handler, gpointer *mapping_data);
} MXFEssenceElementHandler;

gchar * mxf_ul_to_string (const MXFUL *ul, gchar str[48]);
gboolean mxf_ul_is_equal (const MXFUL *a, const MXFUL *b);
gboolean mxf_ul_is_zero (const MXFUL *ul);
guint mxf_ul_hash (const MXFUL *ul);

gchar *mxf_umid_to_string (const MXFUMID * umid, gchar str[96]);
MXFUMID *mxf_umid_from_string (const gchar *str, MXFUMID * umid);
gboolean mxf_umid_is_equal (const MXFUMID *a, const MXFUMID *b);
gboolean mxf_umid_is_zero (const MXFUMID *umid);

gboolean mxf_is_mxf_packet (const MXFUL *key);

gboolean mxf_is_partition_pack (const MXFUL *key);
gboolean mxf_is_header_partition_pack (const MXFUL *key);
gboolean mxf_is_body_partition_pack (const MXFUL *key);
gboolean mxf_is_footer_partition_pack (const MXFUL *key);

gboolean mxf_is_primer_pack (const MXFUL *key);

gboolean mxf_is_metadata (const MXFUL *key);
gboolean mxf_is_descriptive_metadata (const MXFUL *key);

gboolean mxf_is_random_index_pack (const MXFUL *key);
gboolean mxf_is_index_table_segment (const MXFUL *key);

gboolean mxf_is_generic_container_system_item (const MXFUL *key);
gboolean mxf_is_generic_container_essence_element (const MXFUL *key);

gboolean mxf_is_generic_container_essence_container_label (const MXFUL *key);
gboolean mxf_is_avid_essence_container_label (const MXFUL *key);

gboolean mxf_is_fill (const MXFUL *key);

gchar * mxf_utf16_to_utf8 (const guint8 * data, guint size);

gboolean mxf_product_version_parse (MXFProductVersion * product_version,
    const guint8 * data, guint size);

gboolean mxf_fraction_parse (MXFFraction *fraction, const guint8 *data, guint size);

gboolean mxf_timestamp_parse (MXFTimestamp * timestamp, const guint8 * data, guint size);
gboolean mxf_timestamp_is_unknown (const MXFTimestamp *a);
gint mxf_timestamp_compare (const MXFTimestamp *a, const MXFTimestamp *b);

gboolean mxf_ul_array_parse (MXFUL **array, guint32 *count, const guint8 *data, guint size);

gboolean mxf_partition_pack_parse (const MXFUL *key, MXFPartitionPack *pack, const guint8 *data, guint size);
void mxf_partition_pack_reset (MXFPartitionPack *pack);

gboolean mxf_primer_pack_parse (const MXFUL *key, MXFPrimerPack *pack, const guint8 *data, guint size);
void mxf_primer_pack_reset (MXFPrimerPack *pack);

gboolean mxf_random_index_pack_parse (const MXFUL *key, const guint8 *data, guint size, GArray **array);

gboolean mxf_index_table_segment_parse (const MXFUL *key, MXFIndexTableSegment *segment, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_index_table_segment_reset (MXFIndexTableSegment *segment);

gboolean mxf_local_tag_parse (const guint8 * data, guint size, guint16 * tag,
    guint16 * tag_size, const guint8 ** tag_data);
void gst_mxf_local_tag_free (MXFLocalTag *tag);

gboolean mxf_local_tag_add_to_hash_table (const MXFPrimerPack *primer,
  guint16 tag, const guint8 *tag_data, guint16 tag_size,
  GHashTable **hash_table);

void mxf_essence_element_handler_register (const MXFEssenceElementHandler *handler);
const MXFEssenceElementHandler * mxf_essence_element_handler_find (const MXFMetadataTimelineTrack *track);

#endif /* __MXF_PARSE_H__ */

