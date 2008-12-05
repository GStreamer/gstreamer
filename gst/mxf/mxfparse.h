/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __MXF_PARSE_H__
#define __MXF_PARSE_H__

#include <string.h>

#include "mxftypes.h"

typedef GstFlowReturn (*MXFEssenceElementHandler) (const MXFUL *key, GstBuffer *buffer, GstCaps *caps, MXFMetadataGenericPackage *package, MXFMetadataTrack *track, MXFMetadataStructuralComponent *component, gpointer mapping_data, GstBuffer **outbuf);

typedef gboolean (*MXFMetadataDescriptorHandleTag) (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
typedef void (*MXFMetadataDescriptorReset) (MXFMetadataGenericDescriptor *descriptor);

gchar * mxf_ul_to_string (const MXFUL *ul, gchar str[48]);
gboolean mxf_ul_is_equal (const MXFUL *a, const MXFUL *b);
gboolean mxf_ul_is_zero (const MXFUL *ul);

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

gboolean mxf_is_fill (const MXFUL *key);

gchar * mxf_utf16_to_utf8 (const guint8 * data, guint size);

gboolean mxf_product_version_parse (MXFProductVersion * product_version,
    const guint8 * data, guint size);

gboolean mxf_fraction_parse (MXFFraction *fraction, const guint8 *data, guint size);

gboolean mxf_timestamp_parse (MXFTimestamp * timestamp, const guint8 * data, guint size);
gboolean mxf_timestamp_is_unknown (const MXFTimestamp *a);
gint mxf_timestamp_compare (const MXFTimestamp *a, const MXFTimestamp *b);

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

gboolean gst_metadata_add_custom_tag (const MXFPrimerPack *primer,
  guint16 tag, const guint8 *tag_data, guint16 tag_size,
  GHashTable **hash_table);

gboolean mxf_metadata_preface_parse (const MXFUL *key, MXFMetadataPreface *preface, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_preface_reset (MXFMetadataPreface *preface);

gboolean mxf_metadata_identification_parse (const MXFUL *key, MXFMetadataIdentification *identification, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_identification_reset (MXFMetadataIdentification *identification);

gboolean mxf_metadata_content_storage_parse (const MXFUL *key, MXFMetadataContentStorage *content_storage, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_content_storage_reset (MXFMetadataContentStorage *content_storage);

gboolean mxf_metadata_essence_container_data_parse (const MXFUL *key, MXFMetadataEssenceContainerData *essence_container_data, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_essence_container_data_reset (MXFMetadataEssenceContainerData *essence_container_data);

gboolean mxf_metadata_generic_package_parse (const MXFUL *key, MXFMetadataGenericPackage *generic_package, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_generic_package_reset (MXFMetadataGenericPackage *generic_package);

gboolean mxf_metadata_track_parse (const MXFUL *key, MXFMetadataTrack *track, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_track_reset (MXFMetadataTrack *track);

MXFMetadataTrackType mxf_metadata_track_identifier_parse (const MXFUL *track_identifier);

gboolean mxf_metadata_sequence_parse (const MXFUL *key, MXFMetadataSequence *sequence, const MXFPrimerPack *primer, const guint8 *data, guint size);
void mxf_metadata_sequence_reset (MXFMetadataSequence *sequence);

gboolean mxf_metadata_structural_component_parse (const MXFUL *key, MXFMetadataStructuralComponent *component, const MXFPrimerPack *primer, guint16 type, const guint8 *data, guint size);
void mxf_metadata_structural_component_reset (MXFMetadataStructuralComponent *component);

gboolean
mxf_metadata_descriptor_parse (const MXFUL * key, MXFMetadataGenericDescriptor * descriptor, const MXFPrimerPack * primer, guint16 type, const guint8 * data, guint size, MXFMetadataDescriptorHandleTag handle_tag, MXFMetadataDescriptorReset reset);

#define MXF_METADATA_DESCRIPTOR_CLEAR(descriptor, type, parent_type) \
  G_STMT_START { \
    guint8 *___data = (guint8 *) descriptor + sizeof (parent_type); \
    memset (___data, 0, sizeof (type) - sizeof (parent_type)); \
  } G_STMT_END

gboolean mxf_metadata_generic_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_generic_descriptor_reset (MXFMetadataGenericDescriptor *descriptor);

gboolean mxf_metadata_file_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_file_descriptor_reset (MXFMetadataFileDescriptor *descriptor);

gboolean mxf_metadata_generic_sound_essence_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_generic_sound_essence_descriptor_reset (MXFMetadataGenericSoundEssenceDescriptor *descriptor);

gboolean mxf_metadata_generic_picture_essence_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_generic_picture_essence_descriptor_reset (MXFMetadataGenericPictureEssenceDescriptor *descriptor);
void mxf_metadata_generic_picture_essence_descriptor_set_caps (MXFMetadataGenericPictureEssenceDescriptor *descriptor, GstCaps *caps);

gboolean mxf_metadata_cdci_picture_essence_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_cdci_picture_essence_descriptor_reset (MXFMetadataCDCIPictureEssenceDescriptor *descriptor);

gboolean mxf_metadata_rgba_picture_essence_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_rgba_picture_essence_descriptor_reset (MXFMetadataRGBAPictureEssenceDescriptor *descriptor);

gboolean mxf_metadata_generic_data_essence_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_generic_data_essence_descriptor_reset (MXFMetadataGenericDataEssenceDescriptor *descriptor);

gboolean mxf_metadata_multiple_descriptor_handle_tag (MXFMetadataGenericDescriptor *descriptor,
    const MXFPrimerPack *primer, guint16 tag, const guint8 *tag_data, guint16 tag_size);
void mxf_metadata_multiple_descriptor_reset (MXFMetadataMultipleDescriptor *descriptor);

gboolean mxf_metadata_locator_parse (const MXFUL *key, MXFMetadataLocator *locator, const MXFPrimerPack *primer, guint16 type, const guint8 *data, guint size);
void mxf_metadata_locator_reset (MXFMetadataLocator *locator);

#endif /* __MXF_PARSE_H__ */

