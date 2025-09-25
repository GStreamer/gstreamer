/*
 * gstmpegtsdescriptor.h -
 * Copyright (C) 2013 Edward Hervey
 *
 * Authors:
 *   Edward Hervey <edward@collabora.com>
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
 *
 * Some parts of this code come from the Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 */

#ifndef GST_MPEGTS_DESCRIPTOR_H
#define GST_MPEGTS_DESCRIPTOR_H

#include <gst/gst.h>
#include <gst/mpegts/mpegts-prelude.h>

G_BEGIN_DECLS

/*
 * descriptor_tag TS  PS                      Identification
 *        0       n/a n/a Reserved
 *        1       n/a n/a Reserved
 *        2        X   X  video_stream_descriptor
 *        3        X   X  audio_stream_descriptor
 *        4        X   X  hierarchy_descriptor
 *        5        X   X  registration_descriptor
 *        6        X   X  data_stream_alignment_descriptor
 *        7        X   X  target_background_grid_descriptor
 *        8        X   X  video_window_descriptor
 *        9        X   X  CA_descriptor
 *       10        X   X  ISO_639_language_descriptor
 *       11        X   X  system_clock_descriptor
 *       12        X   X  multiplex_buffer_utilization_descriptor
 *       13        X   X  copyright_descriptor
 *       14        X      maximum bitrate descriptor
 *       15        X   X  private data indicator descriptor
 *       16        X   X  smoothing buffer descriptor
 *       17        X      STD_descriptor
 *       18        X   X  IBP descriptor
 *      19-63     n/a n/a ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
 *     64-255     n/a n/a User Private
 */

/**
 * GstMpegtsDescriptorType:
 *
 * The type of #GstMpegtsDescriptor
 *
 * These values correspond to the registered descriptor type from
 * the base MPEG-TS specifications (ITU H.222.0 | ISO/IEC 13818-1).
 *
 * Consult the relevant specifications for more details.
 */
typedef enum {
  /* 0-18 ISO/IEC 13818-1 (H222.0 06/2012) */
  GST_MTS_DESC_RESERVED_00                      = 0x00,
  GST_MTS_DESC_RESERVED_01                      = 0x01,
  GST_MTS_DESC_VIDEO_STREAM                     = 0x02,
  GST_MTS_DESC_AUDIO_STREAM                     = 0x03,
  GST_MTS_DESC_HIERARCHY                        = 0x04,
  GST_MTS_DESC_REGISTRATION                     = 0x05,
  GST_MTS_DESC_DATA_STREAM_ALIGNMENT            = 0x06,
  GST_MTS_DESC_TARGET_BACKGROUND_GRID           = 0x07,
  GST_MTS_DESC_VIDEO_WINDOW                     = 0x08,
  GST_MTS_DESC_CA                               = 0x09,
  GST_MTS_DESC_ISO_639_LANGUAGE                 = 0x0A,
  GST_MTS_DESC_SYSTEM_CLOCK                     = 0x0B,
  GST_MTS_DESC_MULTIPLEX_BUFFER_UTILISATION     = 0x0C,
  GST_MTS_DESC_COPYRIGHT                        = 0x0D,
  GST_MTS_DESC_MAXIMUM_BITRATE                  = 0x0E,
  GST_MTS_DESC_PRIVATE_DATA_INDICATOR           = 0x0F,
  GST_MTS_DESC_SMOOTHING_BUFFER                 = 0x10,
  GST_MTS_DESC_STD                              = 0x11,
  GST_MTS_DESC_IBP                              = 0x12,

  /* 19-26 Defined in ISO/IEC 13818-6 (Extensions for DSM-CC) */
  GST_MTS_DESC_DSMCC_CAROUSEL_IDENTIFIER        = 0x13,
  GST_MTS_DESC_DSMCC_ASSOCIATION_TAG            = 0x14,
  GST_MTS_DESC_DSMCC_DEFERRED_ASSOCIATION_TAG   = 0x15,
  /* 0x16 is reserved (so far) */
  GST_MTS_DESC_DSMCC_NPT_REFERENCE              = 0x17,
  GST_MTS_DESC_DSMCC_NPT_ENDPOINT               = 0x18,
  GST_MTS_DESC_DSMCC_STREAM_MODE                = 0x19,
  GST_MTS_DESC_DSMCC_STREAM_EVENT               = 0x1A,

  /* 27-54 Later additions to ISO/IEC 13818-1 (H222.0 06/2012) */
  GST_MTS_DESC_MPEG4_VIDEO                      = 0x1B,
  GST_MTS_DESC_MPEG4_AUDIO                      = 0x1C,
  GST_MTS_DESC_IOD                              = 0x1D,
  GST_MTS_DESC_SL                               = 0x1E,
  GST_MTS_DESC_FMC                              = 0x1F,
  GST_MTS_DESC_EXTERNAL_ES_ID                   = 0x20,
  GST_MTS_DESC_MUX_CODE                         = 0x21,
  GST_MTS_DESC_FMX_BUFFER_SIZE                  = 0x22,
  GST_MTS_DESC_MULTIPLEX_BUFFER                 = 0x23,
  GST_MTS_DESC_CONTENT_LABELING                 = 0x24,
  GST_MTS_DESC_METADATA_POINTER                 = 0x25,
  GST_MTS_DESC_METADATA                         = 0x26,
  GST_MTS_DESC_METADATA_STD                     = 0x27,
  GST_MTS_DESC_AVC_VIDEO                        = 0x28,
  /* defined in ISO/IEC 13818-11, MPEG-2 IPMP */
  GST_MTS_DESC_IPMP                             = 0x29,
  GST_MTS_DESC_AVC_TIMING_AND_HRD               = 0x2A,
  GST_MTS_DESC_MPEG2_AAC_AUDIO                  = 0x2B,
  GST_MTS_DESC_FLEX_MUX_TIMING                  = 0x2C,
  GST_MTS_DESC_MPEG4_TEXT                       = 0x2D,
  GST_MTS_DESC_MPEG4_AUDIO_EXTENSION            = 0x2E,
  GST_MTS_DESC_AUXILIARY_VIDEO_STREAM           = 0x2F,
  GST_MTS_DESC_SVC_EXTENSION                    = 0x30,
  GST_MTS_DESC_MVC_EXTENSION                    = 0x31,
  GST_MTS_DESC_J2K_VIDEO                        = 0x32,
  GST_MTS_DESC_MVC_OPERATION_POINT              = 0x33,
  GST_MTS_DESC_MPEG2_STEREOSCOPIC_VIDEO_FORMAT  = 0x34,
  GST_MTS_DESC_STEREOSCOPIC_PROGRAM_INFO        = 0x35,
  GST_MTS_DESC_STEREOSCOPIC_VIDEO_INFO          = 0x36,

  /**
   * GST_MTS_DESC_EXTENSION:
   *
   * Extension Descriptor.
   *
   * Since: 1.26
   */
  GST_MTS_DESC_EXTENSION                        = 0x3f
  /* 55-63 ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved */
} GstMpegtsDescriptorType;

/**
 * GstMpegtsExtendedDescriptorType:
 *
 * The type of an extended descriptor
 *
 * The values correpond to the registered extended descriptor types from the
 * base ISO 13818 / ITU H.222.0 specifications
 *
 * Consult the specification for more details
 *
 * Since: 1.26
 */
typedef enum {
  GST_MTS_DESC_EXT_JXS_VIDEO                    = 0x14,
} GstMpegtsExtendedDescriptorType;

/**
 * GstMpegtsMiscDescriptorType:
 *
 * The type of #GstMpegtsDescriptor
 *
 * These values correspond to miscellaneous descriptor types that are
 * not yet identified from known specifications.
 */
typedef enum {
  /* 0x80 - 0xFE are user defined */
  GST_MTS_DESC_DTG_LOGICAL_CHANNEL              = 0x83,    /* from DTG D-Book, only present in NIT */
} GstMpegtsMiscDescriptorType;

/**
 * GstMpegtsSCTEDescriptorType:
 *
 * These values correspond to the ones defined by SCTE (amongst other in ANSI/SCTE 57)
 *
 * Since: 1.20
 */
typedef enum {
  GST_MTS_DESC_SCTE_STUFFING                    = 0x80,
  GST_MTS_DESC_SCTE_AC3				= 0x81,
  GST_MTS_DESC_SCTE_FRAME_RATE			= 0x82,
  GST_MTS_DESC_SCTE_EXTENDED_VIDEO		= 0x83,
  GST_MTS_DESC_SCTE_COMPONENT_NAME		= 0x84,
  GST_MTS_DESC_SCTE_FREQUENCY_SPEC		= 0x90,
  GST_MTS_DESC_SCTE_MODULATION_PARAMS		= 0x91,
  GST_MTS_DESC_SCTE_TRANSPORT_STREAM_ID		= 0x92
} GstMpegtsSCTEDescriptorType;



typedef struct _GstMpegtsDescriptor GstMpegtsDescriptor;

#define GST_TYPE_MPEGTS_DESCRIPTOR (gst_mpegts_descriptor_get_type())
GST_MPEGTS_API
GType gst_mpegts_descriptor_get_type (void);

/**
 * GstMpegtsDescriptor:
 * @tag: the type of descriptor
 * @tag_extension: the extended type (if @tag is 0x7f (for DVB) or 0x3f (for H.222.0))
 * @length: the length of the descriptor content (excluding tag/length field)
 * @data: the full descriptor data (including tag, extension, length). The first
 * two bytes are the @tag and @length.
 *
 * Mpeg-TS descriptor (ISO/IEC 13818-1).
 */
struct _GstMpegtsDescriptor
{
  guint8 tag;
  guint8 tag_extension;
  guint8 length;
  guint8 *data;

  /*< private >*/
  /* Padding for future extension */
  gpointer _gst_reserved[GST_PADDING];
};

GST_MPEGTS_API
void       gst_mpegts_descriptor_free (GstMpegtsDescriptor *desc);

GST_MPEGTS_API
GstMpegtsDescriptor       * gst_mpegts_descriptor_copy (GstMpegtsDescriptor *desc) G_GNUC_WARN_UNUSED_RESULT;

GST_MPEGTS_API
GPtrArray *gst_mpegts_parse_descriptors (guint8 * buffer, gsize buf_len);

GST_MPEGTS_API
const GstMpegtsDescriptor * gst_mpegts_find_descriptor (GPtrArray *descriptors,
							guint8 tag);

GST_MPEGTS_API
const GstMpegtsDescriptor * gst_mpegts_find_descriptor_with_extension (GPtrArray *descriptors,
							guint8 tag, guint8 tag_extension);
/**
 * GstMpegtsRegistrationId:
 * @GST_MTS_REGISTRATION_0: Undefined registration id
 * @GST_MTS_REGISTRATION_AC_3: Audio AC-3, ATSC A/52
 * @GST_MTS_REGISTRATION_AC_4: Audio AC-4, ETSI 103 190-2
 * @GST_MTS_REGISTRATION_CUEI: SCTE 35, "Digital Program Insertion Cueing Message"
 * @GST_MTS_REGISTRATION_drac: Dirac Video codec
 * @GST_MTS_REGISTRATION_DTS1: DTS Audio
 * @GST_MTS_REGISTRATION_DTS2: DTS Audio
 * @GST_MTS_REGISTRATION_DTS3: DTS Audio
 * @GST_MTS_REGISTRATION_EAC3: Enhanced AC-3 (i.e. EAC3)
 * @GST_MTS_REGISTRATION_ETV1: Cablelabs ETV
 * @GST_MTS_REGISTRATION_BSSD: SMPTE 302M, Mapping of AES3 Data in mpeg-ts
 * @GST_MTS_REGISTRATION_GA94: ATSC A/53 compliant stream (i.e. ATSC)
 * @GST_MTS_REGISTRATION_HDMV: Blu-ray, "System Description Blu-ray Disc
 *             Read-Only Format part 3 Audio Visual Basic Specifications"
 * @GST_MTS_REGISTRATION_KLVA: SMPTE RP217 : Non-synchronized Mapping of KLV
 *             Packets in mpeg-ts
 * @GST_MTS_REGISTRATION_OPUS: Opus Audio
 * @GST_MTS_REGISTRATION_TSHV: HDV (Sony)
 * @GST_MTS_REGISTRATION_VC_1: Video VC-1, SMPTE RP227 "VC-1 Bitstream Transport Encodings"
 * @GST_MTS_REGISTRATION_OTHER_HEVC: HEVC / h265
 *
 * Well-known registration ids, expressed as native-endian 32bit integers. These
 * are used in descriptors of type %GST_MTS_DESC_REGISTRATION. Unless specified
 * otherwise (by use of the "OTHER" prefix), they are all registered by the
 * [SMPTE Registration Authority](https://smpte-ra.org/) or specified in
 * "official" documentation for the given format.
 *
 * Since: 1.20
 */

/**
 * GST_MPEGTS_REG_TO_UINT32: (skip) (attributes doc.skip=true)
 */
#define GST_MPEGTS_REG_TO_UINT32(a,b,c,d)((a) << 24 | (b) << 16 | (c) << 8 | (d))

typedef enum {
  GST_MTS_REGISTRATION_0 = 0,

  /* SMPTE-RA registered */
  GST_MTS_REGISTRATION_AC_3 = GST_MPEGTS_REG_TO_UINT32 ('A', 'C', '-', '3'),
  GST_MTS_REGISTRATION_CUEI = GST_MPEGTS_REG_TO_UINT32 ('C', 'U', 'E', 'I'),
  GST_MTS_REGISTRATION_drac = GST_MPEGTS_REG_TO_UINT32 ('d', 'r', 'a', 'c'),
  GST_MTS_REGISTRATION_DTS1 = GST_MPEGTS_REG_TO_UINT32 ('D', 'T', 'S', '1'),
  GST_MTS_REGISTRATION_DTS2 = GST_MPEGTS_REG_TO_UINT32 ('D', 'T', 'S', '2'),
  GST_MTS_REGISTRATION_DTS3 = GST_MPEGTS_REG_TO_UINT32 ('D', 'T', 'S', '3'),
  GST_MTS_REGISTRATION_BSSD = GST_MPEGTS_REG_TO_UINT32 ('B', 'S', 'S', 'D'),
  GST_MTS_REGISTRATION_EAC3 = GST_MPEGTS_REG_TO_UINT32 ('E', 'A', 'C', '3'),
  GST_MTS_REGISTRATION_ETV1 = GST_MPEGTS_REG_TO_UINT32 ('E', 'T', 'V', '1'),
  GST_MTS_REGISTRATION_GA94 = GST_MPEGTS_REG_TO_UINT32 ('G', 'A', '9', '4'),
  GST_MTS_REGISTRATION_HDMV = GST_MPEGTS_REG_TO_UINT32 ('H', 'D', 'M', 'V'),
  GST_MTS_REGISTRATION_KLVA = GST_MPEGTS_REG_TO_UINT32 ('K', 'L', 'V', 'A'),
  GST_MTS_REGISTRATION_OPUS = GST_MPEGTS_REG_TO_UINT32 ('O', 'P', 'U', 'S'),
  GST_MTS_REGISTRATION_TSHV = GST_MPEGTS_REG_TO_UINT32 ('T', 'S', 'H', 'V'),
  GST_MTS_REGISTRATION_VC_1 = GST_MPEGTS_REG_TO_UINT32 ('V', 'C', '-', '1'),

  /* Self-registered by formats, but not in SMPTE-RA registry */
  GST_MTS_REGISTRATION_AC_4 = GST_MPEGTS_REG_TO_UINT32 ('A', 'C', '-', '4'),

  /* Found elsewhere */
  GST_MTS_REGISTRATION_OTHER_HEVC = GST_MPEGTS_REG_TO_UINT32 ('H', 'E', 'V', 'C')
} GstMpegtsRegistrationId;

#undef GST_MPEGTS_REG_TO_UINT32

/* GST_MTS_DESC_REGISTRATION (0x05) */

GST_MPEGTS_API
GstMpegtsDescriptor *gst_mpegts_descriptor_from_registration (
    const gchar *format_identifier,
    guint8 *additional_info, gsize additional_info_length);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_registration(GstMpegtsDescriptor *descriptor,
						  guint32 *registration_id,
						  guint8 **additional_info,
						  gsize *additional_info_length);

/* GST_MTS_DESC_CA (0x09) */

GST_MPEGTS_API
gboolean  gst_mpegts_descriptor_parse_ca (GstMpegtsDescriptor *descriptor,
					  guint16 *ca_system_id,
					  guint16 *ca_pid,
					  const guint8 **private_data,
					  gsize *private_data_size);

/* GST_MTS_DESC_ISO_639_LANGUAGE (0x0A) */
/**
 * GstMpegtsISO639AudioType:
 *
 * Type of audio streams
 *
 * Defined in ITU H.222.0 Table 2-60
 */
typedef enum {
  GST_MPEGTS_AUDIO_TYPE_UNDEFINED = 0,
  GST_MPEGTS_AUDIO_TYPE_CLEAN_EFFECTS,
  GST_MPEGTS_AUDIO_TYPE_HEARING_IMPAIRED,
  GST_MPEGTS_AUDIO_TYPE_VISUAL_IMPAIRED_COMMENTARY
} GstMpegtsIso639AudioType;

typedef struct _GstMpegtsISO639LanguageDescriptor GstMpegtsISO639LanguageDescriptor;
struct _GstMpegtsISO639LanguageDescriptor
{
  guint                    nb_language;
  gchar                    *language[64];
  GstMpegtsIso639AudioType audio_type[64];
};

#define GST_TYPE_MPEGTS_ISO_639_LANGUAGE (gst_mpegts_iso_639_language_get_type ())
GST_MPEGTS_API
GType gst_mpegts_iso_639_language_get_type (void);

GST_MPEGTS_API
void gst_mpegts_iso_639_language_descriptor_free (GstMpegtsISO639LanguageDescriptor * desc);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_iso_639_language (const GstMpegtsDescriptor *descriptor,
						       GstMpegtsISO639LanguageDescriptor **res);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_iso_639_language_idx (const GstMpegtsDescriptor *descriptor,
                                                           guint idx, gchar **lang,
                                                           GstMpegtsIso639AudioType *audio_type);

GST_MPEGTS_API
guint gst_mpegts_descriptor_parse_iso_639_language_nb (const GstMpegtsDescriptor *descriptor);

GST_MPEGTS_API
GstMpegtsDescriptor * gst_mpegts_descriptor_from_iso_639_language (const gchar * language);

/* GST_MTS_DESC_DTG_LOGICAL_CHANNEL (0x83) */
typedef struct _GstMpegtsLogicalChannelDescriptor GstMpegtsLogicalChannelDescriptor;
typedef struct _GstMpegtsLogicalChannel GstMpegtsLogicalChannel;

struct _GstMpegtsLogicalChannel
{
  guint16   service_id;
  gboolean  visible_service;
  guint16   logical_channel_number;
};

struct _GstMpegtsLogicalChannelDescriptor
{
  guint                   nb_channels;
  GstMpegtsLogicalChannel channels[64];
};

#define GST_TYPE_MPEGTS_LOGICAL_CHANNEL_DESCRIPTOR (gst_mpegts_logical_channel_descriptor_get_type())

GST_MPEGTS_API
GType gst_mpegts_logical_channel_descriptor_get_type(void);

#define GST_TYPE_MPEGTS_LOGICAL_CHANNEL (gst_mpegts_logical_channel_get_type())

GST_MPEGTS_API
GType gst_mpegts_logical_channel_get_type(void);

/* FIXME : Maybe make two methods. One for getting the number of channels,
 * and the other for getting the content for one channel ? */
GST_MPEGTS_API
gboolean
gst_mpegts_descriptor_parse_logical_channel (const GstMpegtsDescriptor *descriptor,
					     GstMpegtsLogicalChannelDescriptor *res);

GST_MPEGTS_API
GstMpegtsDescriptor *
gst_mpegts_descriptor_from_custom (guint8 tag, const guint8 *data, gsize length);


GST_MPEGTS_API
GstMpegtsDescriptor *
gst_mpegts_descriptor_from_custom_with_extension (guint8 tag, guint8 tag_extension, const guint8 *data, gsize length);

/**
 * GstMpegtsMetadataFormat:
 *
 * metadata_descriptor metadata_format valid values. See ISO/IEC 13818-1:2018(E) Table 2-85.
 *
 * Since: 1.24
 */
typedef enum
{
  /**
   * GST_MPEGTS_METADATA_FORMAT_TEM:
   *
   * ISO/IEC 15938-1 TeM.
   *
   * Since: 1.24
   */
  GST_MPEGTS_METADATA_FORMAT_TEM = 0x10,
  /**
   * GST_MPEGTS_METADATA_FORMAT_BIM:
   *
   * ISO/IEC 15938-1 BiM.
   *
   * Since: 1.24
   */
  GST_MPEGTS_METADATA_FORMAT_BIM = 0x11,
  /**
   * GST_MPEGTS_METADATA_FORMAT_APPLICATION_FORMAT:
   *
   * Defined by metadata application format.
   *
   * Since: 1.24
   */
  GST_MPEGTS_METADATA_FORMAT_APPLICATION_FORMAT = 0x3f,
  /**
   * GST_MPEGTS_METADATA_FORMAT_IDENTIFIER_FIELD:
   *
   * Defined by metadata_format_identifier field.
   *
   * Since: 1.24
   */
  GST_MPEGTS_METADATA_FORMAT_IDENTIFIER_FIELD = 0xff
} GstMpegtsMetadataFormat;

/**
 * GstMpegtsMetadataApplicationFormat:
 *
 * @GST_MPEGTS_METADATA_APPLICATION_FORMAT_ISAN ISO 15706-1 (ISAN) encoded in its binary form
 * @GST_MPEGTS_METADATA_APPLICATION_FORMAT_VSAN ISO 15706-2 (V-ISAN) encoded in its binary form
 * @GST_MPEGTS_METADATA_APPLICATION_FORMAT_IDENTIFIER_FIELD Defined by the metadata_application_format_identifier field
 *
 * metadata_application_format valid values. See ISO/IEC 13818-1:2023(E) Table 2-84.
 *
 * Since: 1.26
 */
typedef enum
{
  GST_MPEGTS_METADATA_APPLICATION_FORMAT_ISAN = 0x0010,
  GST_MPEGTS_METADATA_APPLICATION_FORMAT_VSAN = 0x0011,
  GST_MPEGTS_METADATA_APPLICATION_FORMAT_IDENTIFIER_FIELD = 0xffff,
} GstMpegtsMetadataApplicationFormat;

/* MPEG-TS Metadata Descriptor (0x26) */
typedef struct _GstMpegtsMetadataDescriptor GstMpegtsMetadataDescriptor;

/**
 * GstMpegtsMetadataDescriptor:
 * @metadata_application_format: specifies the application responsible for defining usage, syntax and semantics
 * @metadata_format: indicates the format and coding of the metadata
 * @metadata_format_identifier: format identifier (equivalent to registration descriptor), for example 0x4B4C4641 ('KLVA') to indicate SMPTE 336 KLV.
 * @metadata_service_id:  metadata service to which this metadata descriptor applies, typically 0x00
 * @decoder_config_flags: decoder flags, see ISO/IEC 13818-1:2018 Table 2-88.
 * @dsm_cc_flag: true if stream associated with this descriptor is in an ISO/IEC 13818-6 data or object carousel.
 *
 * The metadata descriptor specifies parameters of a metadata service carried in an MPEG-2 Transport Stream (or Program Stream). The descriptor is included in the PMT in the descriptor loop for the elementary stream that carries the
metadata service. The descriptor specifies the format of the associated metadata, and contains the value of the
metadata_service_id to identify the metadata service to which the metadata descriptor applies.
 *
 * Note that this structure does not include all of the metadata_descriptor items, and will need extension to support DSM-CC and private data.
 * See ISO/IEC 13818-1:2018 Section 2.6.60 and Section 2.6.61 for more information.
 *
 * Since: 1.24
 */
struct _GstMpegtsMetadataDescriptor
{
  GstMpegtsMetadataApplicationFormat metadata_application_format;
  GstMpegtsMetadataFormat metadata_format;
  guint32 metadata_format_identifier;
  guint8 metadata_service_id;
  guint8 decoder_config_flags;
  gboolean dsm_cc_flag;
};

/**
 * GST_TYPE_MPEGTS_METADATA_DESCRIPTOR:
 *
 * metadata_descriptor type
 *
 * Since: 1.24
 */
#define GST_TYPE_MPEGTS_METADATA_DESCRIPTOR (gst_mpegts_metadata_descriptor_get_type())

GST_MPEGTS_API
GType gst_mpegts_metadata_descriptor_get_type(void);

/**
 * gst_mpegts_descriptor_from_metadata
 *
 * Since: 1.26
 */
GST_MPEGTS_API
GstMpegtsDescriptor *gst_mpegts_descriptor_from_metadata(const GstMpegtsMetadataDescriptor *metadata_descriptor);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_metadata(const GstMpegtsDescriptor *descriptor, GstMpegtsMetadataDescriptor **res);

GST_MPEGTS_API
gboolean gst_mpegts_descriptor_parse_metadata_std(const GstMpegtsDescriptor *descriptor,
                                                  guint32 *metadata_input_leak_rate,
                                                  guint32 *metadata_buffer_size,
                                                  guint32 *metadata_output_leak_rate);

typedef struct _GstMpegtsPESMetadataMeta GstMpegtsPESMetadataMeta;

/**
 * gst_mpegts_pes_metadata_meta_api_get_type
 *
 * Return the #GType associated with #GstMpegtsPESMetadataMeta
 *
 * Returns: a #GType
 *
 * Since: 1.24
 */
GST_MPEGTS_API
GType gst_mpegts_pes_metadata_meta_api_get_type(void);

/**
 * GST_MPEGTS_PES_METADATA_META_API_TYPE:
 *
 * The #GType associated with #GstMpegtsPESMetadataMeta.
 *
 * Since: 1.24
 */
#define GST_MPEGTS_PES_METADATA_META_API_TYPE (gst_mpegts_pes_metadata_meta_api_get_type())

/**
 * GST_MPEGTS_PES_METADATA_META_INFO:
 *
 * The #GstMetaInfo associated with #GstMpegtsPESMetadataMeta.
 *
 * Since: 1.24
 */
#define GST_MPEGTS_PES_METADATA_META_INFO (gst_mpegts_pes_metadata_meta_get_info())

/**
 * gst_mpegts_pes_metadata_meta_get_info:
 *
 * Gets the global #GstMetaInfo describing the #GstMpegtsPESMetadataMeta meta.
 *
 * Returns: (transfer none): The #GstMetaInfo
 *
 * Since: 1.24
 */
GST_MPEGTS_API
const GstMetaInfo *gst_mpegts_pes_metadata_meta_get_info(void);

/**
 * GstMpegtsPESMetadataMeta:
 * @meta: parent #GstMeta
 * @metadata_service_id: metadata service identifier
 * @flags: bit flags, see spec for details
 *
 * Extra buffer metadata describing the PES Metadata context.
 * This is based on the Metadata AU cell header in
 * ISO/IEC 13818-1:2018 Section 2.12.4.
 *
 * AU_cell_data_length is not provided, since it matches the length of the buffer
 *
 * Since: 1.24
 */
struct _GstMpegtsPESMetadataMeta
{
  GstMeta meta;
  guint8 metadata_service_id;
  guint8 flags;
};

/**
 * gst_buffer_add_mpegts_pes_metadata_meta:
 * @buffer: a #GstBuffer
 *
 * Creates and adds a #GstMpegtsPESMetadataMeta to a @buffer.
 *
 * Returns: (transfer none): a newly created #GstMpegtsPESMetadataMeta
 *
 * Since: 1.24
 */
GST_MPEGTS_API
GstMpegtsPESMetadataMeta *
gst_buffer_add_mpegts_pes_metadata_meta(GstBuffer *buffer);

/* MPEG-TS Metadata Descriptor (0x25) */
typedef struct _GstMpegtsMetadataPointerDescriptor
    GstMpegtsMetadataPointerDescriptor;

/**
 * GstMpegtsMetadataPointerDescriptor:
 * @metadata_application_format: specifies the application responsible for defining usage, syntax and semantics
 * @metadata_format: indicates the format and coding of the metadata
 * @metadata_format_identifier: format identifier (equivalent to registration descriptor), for example 0x4B4C4641 ('KLVA') to indicate SMPTE 336 KLV, or 0x49443320 ('ID3 ').
 * @metadata_service_id:  metadata service to which this metadata descriptor applies, typically 0x00
 * @program_number: Indicates the program in which the metadata is carried.
 *
 * This structure is not complete. The following fields are missing in comparison to the standard (ISO/IEC 13818-1:2023 Section 2.6.58):
 * * metadata_locator_record_flag: hardcoded to 0. Indicating no metadata_locator_record present in the descriptor.
 * * MPEG_carriage_flags: hardcoded to 0b00, indicating the metadata is carried in the same transport steam.
 * * metadata_locator_record_length.
 * * transport_stream_location.
 * * transport_stream_id.
 *
 * See also: gst_mpegts_descriptor_from_metadata_pointer
 *
 * Since: 1.26
 */
struct _GstMpegtsMetadataPointerDescriptor
{
  GstMpegtsMetadataApplicationFormat metadata_application_format;
  GstMpegtsMetadataFormat metadata_format;
  guint32 metadata_format_identifier;
  guint8 metadata_service_id;
  guint16 program_number;
};

/**
 * GST_TYPE_MPEGTS_METADATA_POINTER_DESCRIPTOR
 *
 * Since: 1.26
 */
#define GST_TYPE_MPEGTS_METADATA_POINTER_DESCRIPTOR \
  (gst_mpegts_metadata_pointer_descriptor_get_type())
GST_MPEGTS_API
GType gst_mpegts_metadata_pointer_descriptor_get_type(void);

/**
 * gst_mpegts_descriptor_from_metadata_pointer:
 * @metadata_pointer_descriptor: a #GstMpegtsMetadataPointerDescriptor
 *
 * Returns: a #GstMpegtsDescriptor from the metadata pointer descriptor.
 *
 * Since: 1.26
 */
GST_MPEGTS_API
GstMpegtsDescriptor *gst_mpegts_descriptor_from_metadata_pointer(const GstMpegtsMetadataPointerDescriptor *metadata_pointer_descriptor);

/* JPEG-XS descriptor */

/**
 * GstMpegtsJpegXsDescriptor:
 *
 * JPEG-XS descriptor
 *
 * Since: 1.26
 */

typedef struct _GstMpegtsJpegXsDescriptor {
  guint8 descriptor_version;
  guint16 horizontal_size, vertical_size;
  guint32 brat, frat;
  guint16 schar, Ppih, Plev;
  guint32 max_buffer_size;
  guint8 buffer_model_type;
  guint8 colour_primaries;
  guint8 transfer_characteristics;
  guint8 matrix_coefficients;
  gboolean video_full_range_flag;
  gboolean still_mode;
  gboolean mdm_flag;
  guint16 X_c0, Y_c0, X_c1, Y_c1, X_c2, Y_c2;
  guint16 X_wp, Y_wp;
  guint32 L_max, L_min;
  guint16 MaxCLL, MaxFALL;
} GstMpegtsJpegXsDescriptor;

/**
 * GST_TYPE_MPEGTS_JPEG_XS_DESCRIPTOR:
 *
 * Since: 1.26
 */
#define GST_TYPE_MPEGTS_JPEG_XS_DESCRIPTOR	\
  (gst_mpegts_jpeg_xs_descriptor_get_type())

GST_MPEGTS_API
GType gst_mpegts_jpeg_xs_descriptor_get_type(void);

GST_MPEGTS_API
gboolean
gst_mpegts_descriptor_parse_jpeg_xs(const GstMpegtsDescriptor *descriptor,
                                   GstMpegtsJpegXsDescriptor *res);

GST_MPEGTS_API
GstMpegtsDescriptor *
gst_mpegts_descriptor_from_jpeg_xs(const GstMpegtsJpegXsDescriptor *jpegxs);

G_END_DECLS

#endif				/* GST_MPEGTS_DESCRIPTOR_H */
