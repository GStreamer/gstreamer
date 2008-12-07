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

#ifndef __MXF_TYPES_H__
#define __MXF_TYPES_H__

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

/* SMPTE 377M 8.6 table 14 */
#define MXF_METADATA_PREFACE (0x012f)
#define MXF_METADATA_IDENTIFICATION (0x0130)
#define MXF_METADATA_CONTENT_STORAGE (0x0118)
#define MXF_METADATA_ESSENCE_CONTAINER_DATA (0x0123)
#define MXF_METADATA_MATERIAL_PACKAGE (0x0136)
#define MXF_METADATA_SOURCE_PACKAGE (0x0137)
#define MXF_METADATA_TRACK (0x013b)
#define MXF_METADATA_EVENT_TRACK (0x0139)
#define MXF_METADATA_STATIC_TRACK (0x013a)
#define MXF_METADATA_SEQUENCE (0x010f)
#define MXF_METADATA_SOURCE_CLIP (0x0111)
#define MXF_METADATA_TIMECODE_COMPONENT (0x0114)
#define MXF_METADATA_DM_SEGMENT (0x0141)
#define MXF_METADATA_DM_SOURCE_CLIP (0x0145)
#define MXF_METADATA_FILE_DESCRIPTOR (0x0125)
#define MXF_METADATA_GENERIC_PICTURE_ESSENCE_DESCRIPTOR (0x0127)
#define MXF_METADATA_CDCI_PICTURE_ESSENCE_DESCRIPTOR (0x0128)
#define MXF_METADATA_RGBA_PICTURE_ESSENCE_DESCRIPTOR (0x0129)
#define MXF_METADATA_GENERIC_SOUND_ESSENCE_DESCRIPTOR (0x0142)
#define MXF_METADATA_GENERIC_DATA_ESSENCE_DESCRIPTOR (0x0143)
#define MXF_METADATA_MULTIPLE_DESCRIPTOR (0x0144)
#define MXF_METADATA_NETWORK_LOCATOR (0x0132)
#define MXF_METADATA_TEXT_LOCATOR (0x0133)

/* SMPTE 377M Annex A, B, C, D */
typedef struct _MXFMetadataPreface MXFMetadataPreface;
typedef struct _MXFMetadataIdentification MXFMetadataIdentification;
typedef struct _MXFMetadataContentStorage MXFMetadataContentStorage;
typedef struct _MXFMetadataEssenceContainerData MXFMetadataEssenceContainerData;
typedef struct _MXFMetadataGenericPackage MXFMetadataGenericPackage;
typedef MXFMetadataGenericPackage MXFMetadataMaterialPackage;
typedef MXFMetadataGenericPackage MXFMetadataSourcePackage;
typedef struct _MXFMetadataTrack MXFMetadataTrack;
typedef struct _MXFMetadataSequence MXFMetadataSequence;
typedef struct _MXFMetadataStructuralComponent MXFMetadataStructuralComponent;
typedef struct _MXFMetadataGenericDescriptor MXFMetadataGenericDescriptor;
typedef struct _MXFMetadataFileDescriptor MXFMetadataFileDescriptor;
typedef struct _MXFMetadataGenericPictureEssenceDescriptor MXFMetadataGenericPictureEssenceDescriptor;
typedef struct _MXFMetadataCDCIPictureEssenceDescriptor MXFMetadataCDCIPictureEssenceDescriptor;
typedef struct _MXFMetadataRGBAPictureEssenceDescriptor MXFMetadataRGBAPictureEssenceDescriptor;
typedef struct _MXFMetadataGenericSoundEssenceDescriptor MXFMetadataGenericSoundEssenceDescriptor;
typedef struct _MXFMetadataGenericDataEssenceDescriptor MXFMetadataGenericDataEssenceDescriptor;
typedef struct _MXFMetadataMultipleDescriptor MXFMetadataMultipleDescriptor;
typedef struct _MXFMetadataLocator MXFMetadataLocator;

struct _MXFMetadataPreface {
  MXFUL instance_uid;
  MXFUL generation_uid;

  MXFTimestamp last_modified_date;
  guint16 version;

  guint32 object_model_version;

  MXFUL primary_package_uid;
  MXFMetadataGenericPackage *primary_package;

  guint32 n_identifications;
  MXFUL *identifications_uids;
  MXFMetadataIdentification **identifications;

  MXFUL content_storage_uid;
  MXFMetadataContentStorage *content_storage;

  MXFUL operational_pattern;

  guint32 n_essence_containers;
  MXFUL *essence_containers;

  guint32 n_dm_schemes;
  MXFUL *dm_schemes;

  GHashTable *other_tags;
};

struct _MXFMetadataIdentification {
  MXFUL instance_uid;
  MXFUL generation_uid;

  gchar *company_name;

  gchar *product_name;
  MXFProductVersion product_version;
  
  gchar *version_string;

  MXFUL product_uid;

  MXFTimestamp modification_date;

  MXFProductVersion toolkit_version;

  gchar *platform;

  GHashTable *other_tags;
};

struct _MXFMetadataContentStorage {
  MXFUL instance_uid;
  MXFUL generation_uid;

  guint32 n_packages;
  MXFUL *packages_uids;
  MXFMetadataGenericPackage **packages;

  guint32 n_essence_container_data;
  MXFUL *essence_container_data_uids;
  MXFMetadataEssenceContainerData **essence_container_data;

  GHashTable *other_tags;
};

struct _MXFMetadataEssenceContainerData {
  MXFUL instance_uid;

  MXFUMID linked_package_uid;
  MXFMetadataGenericPackage *linked_package;

  MXFUL generation_uid;

  guint32 index_sid;
  guint32 body_sid;

  GHashTable *other_tags;
};

typedef enum {
  MXF_METADATA_GENERIC_PACKAGE_SOURCE = 0,
  MXF_METADATA_GENERIC_PACKAGE_MATERIAL = 1,
  MXF_METADATA_GENERIC_PACKAGE_TOP_LEVEL_SOURCE = 2
} MXFMetadataGenericPackageType;

struct _MXFMetadataGenericPackage {
  MXFUL instance_uid;
  MXFUMID package_uid;
  MXFUL generation_uid;

  MXFMetadataGenericPackageType type;

  gchar *name;
  MXFTimestamp package_creation_date;
  MXFTimestamp package_modified_date;

  guint32 n_tracks;
  MXFUL *tracks_uids;
  MXFMetadataTrack **tracks;

  guint n_timecode_tracks;
  guint n_metadata_tracks;
  guint n_essence_tracks;
  guint n_other_tracks;

  /* Only in Source packages */
  MXFUL descriptors_uid;
  guint32 n_descriptors;
  MXFMetadataGenericDescriptor **descriptors;
  
  GHashTable *other_tags;
};

typedef enum {
  MXF_METADATA_TRACK_UNKNOWN               = 0x00,
  MXF_METADATA_TRACK_TIMECODE_12M_INACTIVE = 0x10,
  MXF_METADATA_TRACK_TIMECODE_12M_ACTIVE   = 0x11,
  MXF_METADATA_TRACK_TIMECODE_309M         = 0x12,
  MXF_METADATA_TRACK_METADATA              = 0x20,
  MXF_METADATA_TRACK_PICTURE_ESSENCE       = 0x30,
  MXF_METADATA_TRACK_SOUND_ESSENCE         = 0x31,
  MXF_METADATA_TRACK_DATA_ESSENCE          = 0x32,
  MXF_METADATA_TRACK_AUXILIARY_DATA        = 0x40,
  MXF_METADATA_TRACK_PARSED_TEXT           = 0x41
} MXFMetadataTrackType;

struct _MXFMetadataTrack {
  MXFUL instance_uid;
  MXFUL generation_uid;

  guint32 track_id;
  guint32 track_number;

  gchar *track_name;

  MXFFraction edit_rate;

  gint64 origin;

  MXFUL sequence_uid;
  MXFMetadataSequence *sequence;

  MXFMetadataFileDescriptor **descriptor;
  guint n_descriptor;
  
  GHashTable *other_tags;
};

struct _MXFMetadataSequence {
  MXFUL instance_uid;
  MXFUL generation_uid;

  MXFUL data_definition;

  gint64 duration;

  guint32 n_structural_components;
  MXFUL *structural_components_uids;
  MXFMetadataStructuralComponent **structural_components;
  
  GHashTable *other_tags;
};

struct _MXFMetadataStructuralComponent {
  guint16 type;

  MXFUL instance_uid;
  MXFUL generation_uid;

  MXFUL data_definition;

  gint64 duration;

  union {
    struct {
      gint64 start_timecode;
      guint16 rounded_timecode_base;
      gboolean drop_frame;
    } timecode_component;

    struct {
      gint64 start_position;
      MXFUMID source_package_id;
      MXFMetadataGenericPackage *source_package;

      guint32 source_track_id;
    } source_clip;
  };

  GHashTable *other_tags;
};

struct _MXFMetadataGenericDescriptor {
  guint16 type;

  MXFUL instance_uid;
  MXFUL generation_uid;

  guint32 n_locators;
  MXFUL *locators_uids;
  MXFMetadataLocator **locators;

  gboolean is_file_descriptor;

  GHashTable *other_tags;
};

struct _MXFMetadataFileDescriptor {
  MXFMetadataGenericDescriptor parent;

  guint32 linked_track_id;

  MXFFraction sample_rate;
  gint64 container_duration;
  MXFUL essence_container;
  MXFUL codec;
};

struct _MXFMetadataGenericPictureEssenceDescriptor {
  MXFMetadataFileDescriptor parent;

  guint8 signal_standard;
  guint8 frame_layout;

  guint32 stored_width;
  guint32 stored_height;
  gint32 stored_f2_offset;
  guint32 sampled_width;
  guint32 sampled_height;
  gint32 sampled_x_offset;
  gint32 sampled_y_offset;
  guint32 display_height;
  guint32 display_width;
  gint32 display_x_offset;
  gint32 display_y_offset;
  gint32 display_f2_offset;
  MXFFraction aspect_ratio;

  guint8 active_format_descriptor;
  gint32 video_line_map[2];
  guint8 alpha_transparency;
  MXFUL capture_gamma;

  guint32 image_alignment_offset;
  guint32 image_start_offset;
  guint32 image_end_offset;

  guint8 field_dominance;

  MXFUL picture_essence_coding;
};

struct _MXFMetadataCDCIPictureEssenceDescriptor {
  MXFMetadataGenericPictureEssenceDescriptor parent;

  guint32 component_depth;
  guint32 horizontal_subsampling;
  guint32 vertical_subsampling;
  guint8 color_siting;
  gboolean reversed_byte_order;
  gint16 padding_bits;
  guint32 alpha_sample_depth;
  guint32 black_ref_level;
  guint32 white_ref_level;
  guint32 color_range;
};

struct _MXFMetadataRGBAPictureEssenceDescriptor {
  MXFMetadataGenericPictureEssenceDescriptor parent;

  guint32 component_max_ref;
  guint32 component_min_ref;
  guint32 alpha_max_ref;
  guint32 alpha_min_ref;
  guint8 scanning_direction;

  guint32 n_pixel_layout;
  guint8 *pixel_layout;

  /* TODO: palette & palette layout */
};

struct _MXFMetadataGenericSoundEssenceDescriptor {
  MXFMetadataFileDescriptor parent;

  MXFFraction audio_sampling_rate;

  gboolean locked;

  gint8 audio_ref_level;

  guint8 electro_spatial_formulation;

  guint32 channel_count;
  guint32 quantization_bits;

  gint8 dial_norm;

  MXFUL sound_essence_compression;
};

struct _MXFMetadataGenericDataEssenceDescriptor {
  MXFMetadataFileDescriptor parent;

  MXFUL data_essence_compression;
};

struct _MXFMetadataMultipleDescriptor {
  MXFMetadataFileDescriptor parent;
  
  MXFUL *sub_descriptors_uids;
  guint32 n_sub_descriptors;
  MXFMetadataGenericDescriptor **sub_descriptors;
};

struct _MXFMetadataLocator {
  guint16 type;

  MXFUL instance_uid;
  MXFUL generation_uid;

  gchar *location;

  GHashTable *other_tags;
};

#endif /* __MXF_TYPES_H__ */
