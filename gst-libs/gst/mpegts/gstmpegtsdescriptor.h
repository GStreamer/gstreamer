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
 * GstMpegTsDescriptorType:
 *
 * The type of #GstMpegTsDescriptor
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

  /* 55-63 ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved */
} GstMpegTsDescriptorType;

typedef enum {
  /* 64-127 DVB tags ETSI EN 300 468
   * (Specification for Service Information (SI) in DVB systems)
   */
  GST_MTS_DESC_DVB_NETWORK_NAME                 = 0x40,
  GST_MTS_DESC_DVB_SERVICE_LIST                 = 0x41,
  GST_MTS_DESC_DVB_STUFFING                     = 0x42,
  GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM    = 0x43,
  GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM        = 0x44,
  GST_MTS_DESC_DVB_VBI_DATA                     = 0x45,
  GST_MTS_DESC_DVB_VBI_TELETEXT                 = 0x46,
  GST_MTS_DESC_DVB_BOUQUET_NAME                 = 0x47,
  GST_MTS_DESC_DVB_SERVICE                      = 0x48,
  GST_MTS_DESC_DVB_COUNTRY_AVAILABILITY         = 0x49,
  GST_MTS_DESC_DVB_LINKAGE                      = 0x4A,
  GST_MTS_DESC_DVB_NVOD_REFERENCE               = 0x4B,
  GST_MTS_DESC_DVB_TIME_SHIFTED_SERVICE         = 0x4C,
  GST_MTS_DESC_DVB_SHORT_EVENT                  = 0x4D,
  GST_MTS_DESC_DVB_EXTENDED_EVENT               = 0x4E,
  GST_MTS_DESC_DVB_TIME_SHIFTED_EVENT           = 0x4F,
  GST_MTS_DESC_DVB_COMPONENT                    = 0x50,
  GST_MTS_DESC_DVB_MOSAIC                       = 0x51,
  GST_MTS_DESC_DVB_STREAM_IDENTIFIER            = 0x52,
  GST_MTS_DESC_DVB_CA_IDENTIFIER                = 0x53,
  GST_MTS_DESC_DVB_CONTENT                      = 0x54,
  GST_MTS_DESC_DVB_PARENTAL_RATING              = 0x55,
  GST_MTS_DESC_DVB_TELETEXT                     = 0x56,
  GST_MTS_DESC_DVB_TELEPHONE                    = 0x57,
  GST_MTS_DESC_DVB_LOCAL_TIME_OFFSET            = 0x58,
  GST_MTS_DESC_DVB_SUBTITLING                   = 0x59,
  GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM  = 0x5A,
  GST_MTS_DESC_DVB_MULTILINGUAL_NETWORK_NAME    = 0x5B,
  GST_MTS_DESC_DVB_MULTILINGUAL_BOUQUET_NAME    = 0x5C,
  GST_MTS_DESC_DVB_MULTILINGUAL_SERVICE_NAME    = 0x5D,
  GST_MTS_DESC_DVB_MULTILINGUAL_COMPONENT       = 0x5E,
  GST_MTS_DESC_DVB_PRIVATE_DATA_SPECIFIER       = 0x5F,
  GST_MTS_DESC_DVB_SERVICE_MOVE                 = 0x60,
  GST_MTS_DESC_DVB_SHORT_SMOOTHING_BUFFER       = 0x61,
  GST_MTS_DESC_DVB_FREQUENCY_LIST               = 0x62,
  GST_MTS_DESC_DVB_PARTIAL_TRANSPORT_STREAM     = 0x63,
  GST_MTS_DESC_DVB_DATA_BROADCAST               = 0x64,
  GST_MTS_DESC_DVB_SCRAMBLING                   = 0x65,
  GST_MTS_DESC_DVB_DATA_BROADCAST_ID            = 0x66,
  GST_MTS_DESC_DVB_TRANSPORT_STREAM             = 0x67,
  GST_MTS_DESC_DVB_DSNG                         = 0x68,
  GST_MTS_DESC_DVB_PDC                          = 0x69,
  GST_MTS_DESC_DVB_AC3                          = 0x6A,
  GST_MTS_DESC_DVB_ANCILLARY_DATA               = 0x6B,
  GST_MTS_DESC_DVB_CELL_LIST                    = 0x6C,
  GST_MTS_DESC_DVB_CELL_FREQUENCY_LINK          = 0x6D,
  GST_MTS_DESC_DVB_ANNOUNCEMENT_SUPPORT         = 0x6E,
  GST_MTS_DESC_DVB_APPLICATION_SIGNALLING       = 0x6F,
  GST_MTS_DESC_DVB_ADAPTATION_FIELD_DATA        = 0x70,
  GST_MTS_DESC_DVB_SERVICE_IDENTIFIER           = 0x71,
  GST_MTS_DESC_DVB_SERVICE_AVAILABILITY         = 0x72,
  GST_MTS_DESC_DVB_DEFAULT_AUTHORITY            = 0x73,
  GST_MTS_DESC_DVB_RELATED_CONTENT              = 0x74,
  GST_MTS_DESC_DVB_TVA_ID                       = 0x75,
  GST_MTS_DESC_DVB_CONTENT_IDENTIFIER           = 0x76,
  GST_MTS_DESC_DVB_TIMESLICE_FEC_IDENTIFIER     = 0x77,
  GST_MTS_DESC_DVB_ECM_REPETITION_RATE          = 0x78,
  GST_MTS_DESC_DVB_S2_SATELLITE_DELIVERY_SYSTEM = 0x79,
  GST_MTS_DESC_DVB_ENHANCED_AC3                 = 0x7A,
  GST_MTS_DESC_DVB_DTS                          = 0x7B,
  GST_MTS_DESC_DVB_AAC                          = 0x7C,
  GST_MTS_DESC_DVB_XAIT_LOCATION                = 0x7D,
  GST_MTS_DESC_DVB_FTA_CONTENT_MANAGEMENT       = 0x7E,
  GST_MTS_DESC_DVB_EXTENSION                    = 0x7F,
} GstMpegTsDVBDescriptorType;

typedef enum {
  /* 0x80 - 0xFE are user defined */
  GST_MTS_DESC_AC3_AUDIO_STREAM                 = 0x81,
  GST_MTS_DESC_DTG_LOGICAL_CHANNEL              = 0x83,    /* from DTG D-Book */
} GstMpegTsMiscDescriptorType;

typedef enum {
  /* ATSC A/65 2009 */
  GST_MTS_DESC_ATSC_STUFFING                    = 0x80,
  GST_MTS_DESC_ATSC_AC3                         = 0x83,
  GST_MTS_DESC_ATSC_CAPTION_SERVICE             = 0x86,
  GST_MTS_DESC_ATSC_CONTENT_ADVISORY            = 0x87,
  GST_MTS_DESC_ATSC_EXTENDED_CHANNEL_NAME       = 0xA0,
  GST_MTS_DESC_ATSC_SERVICE_LOCATION            = 0xA1,
  GST_MTS_DESC_ATSC_TIME_SHIFTED_SERVICE        = 0xA2,
  GST_MTS_DESC_ATSC_COMPONENT_NAME              = 0xA3,
  GST_MTS_DESC_ATSC_DCC_DEPARTING_REQUEST       = 0xA8,
  GST_MTS_DESC_ATSC_DCC_ARRIVING_REQUEST        = 0xA9,
  GST_MTS_DESC_ATSC_REDISTRIBUTION_CONTROL      = 0xAA,
  GST_MTS_DESC_ATSC_GENRE                       = 0xAB,
  GST_MTS_DESC_ATSC_PRIVATE_INFORMATION         = 0xAD,

  /* ATSC A/53:3 2009 */
  GST_MTS_DESC_ATSC_ENHANCED_SIGNALING          = 0xB2,

  /* ATSC A/90 */
  GST_MTS_DESC_ATSC_DATA_SERVICE                = 0xA4,
  GST_MTS_DESC_ATSC_PID_COUNT                   = 0xA5,
  GST_MTS_DESC_ATSC_DOWNLOAD_DESCRIPTOR         = 0xA6,
  GST_MTS_DESC_ATSC_MULTIPROTOCOL_ENCAPSULATION = 0xA7,
  GST_MTS_DESC_ATSC_MODULE_LINK                 = 0xB4,
  GST_MTS_DESC_ATSC_CRC32                       = 0xB5,
  GST_MTS_DESC_ATSC_GROUP_LINK                  = 0xB8,
} GstMpegTsATSCDescriptorType;

typedef enum {
  /* ISDB ARIB B10 v4.6 */
  GST_MTS_DESC_ISDB_HIERARCHICAL_TRANSMISSION   = 0xC0,
  GST_MTS_DESC_ISDB_DIGITAL_COPY_CONTROL        = 0xC1,
  GST_MTS_DESC_ISDB_NETWORK_IDENTIFICATION      = 0xC2,
  GST_MTS_DESC_ISDB_PARTIAL_TS_TIME             = 0xc3,
  GST_MTS_DESC_ISDB_AUDIO_COMPONENT             = 0xc4,
  GST_MTS_DESC_ISDB_HYPERLINK                   = 0xc5,
  GST_MTS_DESC_ISDB_TARGET_REGION               = 0xc6,
  GST_MTS_DESC_ISDB_DATA_CONTENT                = 0xc7,
  GST_MTS_DESC_ISDB_VIDEO_DECODE_CONTROL        = 0xc8,
  GST_MTS_DESC_ISDB_DOWNLOAD_CONTENT            = 0xc9,
  GST_MTS_DESC_ISDB_CA_EMM_TS                   = 0xca,
  GST_MTS_DESC_ISDB_CA_CONTRACT_INFORMATION     = 0xcb,
  GST_MTS_DESC_ISDB_CA_SERVICE                  = 0xcc,
  GST_MTS_DESC_ISDB_TS_INFORMATION              = 0xcd,
  GST_MTS_DESC_ISDB_EXTENDED_BROADCASTER        = 0xce,
  GST_MTS_DESC_ISDB_LOGO_TRANSMISSION           = 0xcf,
  GST_MTS_DESC_ISDB_BASIC_LOCAL_EVENT           = 0xd0,
  GST_MTS_DESC_ISDB_REFERENCE                   = 0xd1,
  GST_MTS_DESC_ISDB_NODE_RELATION               = 0xd2,
  GST_MTS_DESC_ISDB_SHORT_NODE_INFORMATION      = 0xd3,
  GST_MTS_DESC_ISDB_STC_REFERENCE               = 0xd4,
  GST_MTS_DESC_ISDB_SERIES                      = 0xd5,
  GST_MTS_DESC_ISDB_EVENT_GROUP                 = 0xd6,
  GST_MTS_DESC_ISDB_SI_PARAMETER                = 0xd7,
  GST_MTS_DESC_ISDB_BROADCASTER_NAME            = 0xd8,
  GST_MTS_DESC_ISDB_COMPONENT_GROUP             = 0xd9,
  GST_MTS_DESC_ISDB_SI_PRIME_TS                 = 0xda,
  GST_MTS_DESC_ISDB_BOARD_INFORMATION           = 0xdb,
  GST_MTS_DESC_ISDB_LDT_LINKAGE                 = 0xdc,
  GST_MTS_DESC_ISDB_CONNECTED_TRANSMISSION      = 0xdd,
  GST_MTS_DESC_ISDB_CONTENT_AVAILABILITY        = 0xde,
  /* ... */
  GST_MTS_DESC_ISDB_SERVICE_GROUP               = 0xe0
  
} GstMpegTsISDBDescriptorType;

typedef struct _GstMpegTsDescriptor GstMpegTsDescriptor;

#define GST_TYPE_MPEGTS_DESCRIPTOR (gst_mpegts_descriptor_get_type())
GType gst_mpegts_descriptor_get_type (void);

/**
 * GstMpegTsDescriptor:
 * @descriptor_tag: the type of descriptor
 * @descriptor_tag_extension: the extended type (if @descriptor_tag is 0x7f)
 * @descriptor_length: the length of the descriptor content (excluding tag/length field)
 * @descriptor_data: the full descriptor data (including tag, extension, length)
 *
 * Mpeg-TS descriptor (ISO/IEC 13818-1).
 */
struct _GstMpegTsDescriptor
{
  guint8 descriptor_tag;
  guint8 descriptor_tag_extension;
  guint8 descriptor_length;
  const guint8 *descriptor_data;
};

GArray *gst_mpegts_parse_descriptors (guint8 * buffer, gsize buf_len);

const GstMpegTsDescriptor * gst_mpegts_find_descriptor (GArray *descriptors,
							guint8 tag);

/* GST_MTS_DESC_ISO_639_LANGUAGE (0x0A) */
/**
 * GstMpegTsISO639AudioType:
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
} GstMpegTsIso639AudioType;

/* FIXME: Make two methods. One for getting the number of languages,
 * and the other for getting the (allocated, null-terminated) language 
 * and audio type */
typedef struct _GstMpegTsISO639LanguageDescriptor GstMpegTsISO639LanguageDescriptor;
struct _GstMpegTsISO639LanguageDescriptor
{
  guint                    nb_language;
  gchar                    language[64][3];
  GstMpegTsIso639AudioType audio_type[64];
};

gboolean gst_mpegts_descriptor_parse_iso_639_language (const GstMpegTsDescriptor *descriptor,
						       GstMpegTsISO639LanguageDescriptor *res);


/* GST_MTS_DESC_DVB_CAROUSEL_IDENTIFIER (0x13) */
/* FIXME : Implement */

/* GST_MTS_DESC_DVB_NETWORK_NAME (0x40) */
gboolean gst_mpegts_descriptor_parse_dvb_network_name (const GstMpegTsDescriptor *descriptor,
						       gchar **name);

/* GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM (0x43) */
typedef struct _GstMpegTsSatelliteDeliverySystemDescriptor GstMpegTsSatelliteDeliverySystemDescriptor;

typedef enum {
  GST_MPEGTS_MODULATION_QPSK    = 0,
  GST_MPEGTS_MODULATION_QAM_16,
  GST_MPEGTS_MODULATION_QAM_32,
  GST_MPEGTS_MODULATION_QAM_64,
  GST_MPEGTS_MODULATION_QAM_128,
  GST_MPEGTS_MODULATION_QAM_256,
  GST_MPEGTS_MODULATION_QAM_AUTO,
  GST_MPEGTS_MODULATION_VSB_8,
  GST_MPEGTS_MODULATION_VSB_16,
  GST_MPEGTS_MODULATION_PSK_8,
  GST_MPEGTS_MODULATION_APSK_16,
  GST_MPEGTS_MODULATION_APSK_32,
  GST_MPEGTS_MODULATION_DQPSK,
  GST_MPEGTS_MODULATION_QAM_4_NR_,
  GST_MPEGTS_MODULATION_NONE
} GstMpegTsModulationType;

typedef enum {
  GST_MPEGTS_FEC_NONE = 0,
  GST_MPEGTS_FEC_1_2,
  GST_MPEGTS_FEC_2_3,
  GST_MPEGTS_FEC_3_4,
  GST_MPEGTS_FEC_4_5,
  GST_MPEGTS_FEC_5_6,
  GST_MPEGTS_FEC_6_7,
  GST_MPEGTS_FEC_7_8,
  GST_MPEGTS_FEC_8_9,
  GST_MPEGTS_FEC_AUTO,
  GST_MPEGTS_FEC_3_5,
  GST_MPEGTS_FEC_9_10,
  GST_MPEGTS_FEC_2_5
} GstMpegTsDVBCodeRate;

typedef enum {
  GST_MPEGTS_ROLLOFF_35 = 0,
  GST_MPEGTS_ROLLOFF_20,
  GST_MPEGTS_ROLLOFF_25,
  GST_MPEGTS_ROLLOFF_RESERVED,
  GST_MPEGTS_ROLLOFF_AUTO
} GstMpegTsSatelliteRolloff;

typedef enum {
  GST_MPEGTS_POLARIZATION_LINEAR_HORIZONTAL = 0,
  GST_MPEGTS_POLARIZATION_LINEAR_VERTICAL,
  GST_MPEGTS_POLARIZATION_CIRCULAR_LEFT,
  GST_MPEGTS_POLARIZATION_CIRCULAR_RIGHT
} GstMpegTsSatellitePolarizationType;

/**
 * GstMpegTsSatelliteDeliverySystemDescriptor:
 * @frequency: the frequency in kHz (kiloHertz)
 * @orbital_position: the orbital position in degrees
 * @west_east: If %TRUE, the satellite is in the eastern part of the orbit,
 * else in the western part.
 * @polarization: The polarization of the transmitted signal
 * @roll_off: Roll-off factor used in DVB-S2
 * @modulation_system: modulation system, %TRUE if DVB-S2, else DVB-S
 * @modulation_type: Modulation scheme used
 * @symbol_rate: Symbol rate (in symbols per second)
 * @fec_inner: inner FEC scheme used
 *
 * Satellite Delivery System Descriptor (EN 300 468 v.1.13.1)
 */
struct _GstMpegTsSatelliteDeliverySystemDescriptor
{
  guint32                            frequency;
  gfloat                             orbital_position;
  gboolean                           west_east; 
  GstMpegTsSatellitePolarizationType polarization;

  GstMpegTsSatelliteRolloff          roll_off;
  gboolean                           modulation_system;
  GstMpegTsModulationType            modulation_type;

  guint32                            symbol_rate;
  GstMpegTsDVBCodeRate               fec_inner;
};

gboolean gst_mpegts_descriptor_parse_satellite_delivery_system (const GstMpegTsDescriptor *descriptor,
								GstMpegTsSatelliteDeliverySystemDescriptor *res);


/* FIXME : Implement */

/* GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM (0x44) */
typedef enum {
  GST_MPEGTS_CABLE_OUTER_FEC_UNDEFINED = 0,
  GST_MPEGTS_CABLE_OUTER_FEC_NONE,
  GST_MPEGTS_CABLE_OUTER_FEC_RS_204_188,
} GstMpegTsCableOuterFECScheme;

typedef struct _GstMpegTsCableDeliverySystemDescriptor GstMpegTsCableDeliverySystemDescriptor;
/**
 * GstMpegTsCableDeliverySystemDescriptor:
 * @frequency: the frequency in Hz (Hertz)
 * @outer_fec: the outer FEC scheme used
 * @modulation: Modulation scheme used
 * @symbol_rate: Symbol rate (in symbols per second)
 * @fec_inner: inner FEC scheme used
 *
 * Cable Delivery System Descriptor (EN 300 468 v.1.13.1)
 */
struct _GstMpegTsCableDeliverySystemDescriptor
{
  guint32                            frequency;
  GstMpegTsCableOuterFECScheme       outer_fec;
  GstMpegTsModulationType            modulation;

  guint32                            symbol_rate;
  GstMpegTsDVBCodeRate               fec_inner;
};

gboolean gst_mpegts_descriptor_parse_cable_delivery_system (const GstMpegTsDescriptor *descriptor,
							    GstMpegTsCableDeliverySystemDescriptor *res);

/* GST_MTS_DESC_DVB_SERVICE (0x48) */
/**
 * GstMpegTsDVBServiceType:
 * 
 * The type of service of a channel.
 *
 * As specified in Table 87 of ETSI EN 300 468 v1.13.1 
 */
typedef enum {
  GST_DVB_SERVICE_RESERVED_00               = 0x00,
  GST_DVB_SERVICE_DIGITAL_TELEVISION,
  GST_DVB_SERVICE_DIGITAL_RADIO_SOUND,
  GST_DVB_SERVICE_TELETEXT,
  GST_DVB_SERVICE_NVOD_REFERENCE,
  GST_DVB_SERVICE_NVOD_TIME_SHIFTED,
  GST_DVB_SERVICE_MOSAIC,
  GST_DVB_SERVICE_FM_RADIO,
  GST_DVB_SERVICE_DVB_SRM,
  GST_DVB_SERVICE_RESERVED_09,
  GST_DVB_SERVICE_ADVANCED_CODEC_DIGITAL_RADIO_SOUND,
  GST_DVB_SERVICE_ADVANCED_CODEC_MOSAIC,
  GST_DVB_SERVICE_DATA_BROADCAST,
  GST_DVB_SERVICE_RESERVED_0D_COMMON_INTERFACE,
  GST_DVB_SERVICE_RCS_MAP,
  GST_DVB_SERVICE_RCS_FLS,
  GST_DVB_SERVICE_DVB_MHP,
  GST_DVB_SERVICE_MPEG2_HD_DIGITAL_TELEVISION,
  /* 0x12 - 015 Reserved for future use */
  GST_DVB_SERVICE_ADVANCED_CODEC_SD_DIGITAL_TELEVISION = 0x16,
  GST_DVB_SERVICE_ADVANCED_CODEC_SD_NVOD_TIME_SHIFTED,
  GST_DVB_SERVICE_ADVANCED_CODEC_SD_NVOD_REFERENCE,
  GST_DVB_SERVICE_ADVANCED_CODEC_HD_DIGITAL_TELEVISION,
  GST_DVB_SERVICE_ADVANCED_CODEC_HD_NVOD_TIME_SHIFTED,
  GST_DVB_SERVICE_ADVANCED_CODEC_HD_NVOD_REFERENCE,
  GST_DVB_SERVICE_ADVANCED_CODEC_STEREO_HD_DIGITAL_TELEVISION,
  GST_DVB_SERVICE_ADVANCED_CODEC_STEREO_HD_NVOD_TIME_SHIFTED,
  GST_DVB_SERVICE_ADVANCED_CODEC_STEREO_HD_NVOD_REFERENCE,
  /* 0x1F - 0x7f Reserved for future use */
  /* 0x80 - 0xfe user defined */
  /* 0xff Reserved for future use */
  GST_DVB_SERVICE_RESERVED_FF
} GstMpegTsDVBServiceType;

/* FIXME : enum type for service_type ? */
gboolean gst_mpegts_descriptor_parse_dvb_service (const GstMpegTsDescriptor *descriptor,
						  GstMpegTsDVBServiceType *service_type,
						  gchar **service_name,
						  gchar **provider_name);

/* GST_MTS_DESC_DVB_SHORT_EVENT (0x4D) */
gboolean gst_mpegts_descriptor_parse_dvb_short_event (const GstMpegTsDescriptor *descriptor,
						       gchar **language_code,
						       gchar **event_name,
						       gchar **text);

/* GST_MTS_DESC_DVB_EXTENDED_EVENT (0x4E) */
typedef struct _GstMpegTsExtendedEventDescriptor GstMpegTsExtendedEventDescriptor;
typedef struct _GstMpegTsExtendedEventItem GstMpegTsExtendedEventItem;

/* FIXME : Maybe make a separate method for getting a specific item entry ? */
struct _GstMpegTsExtendedEventItem
{
  gchar *item_description;
  gchar *item;
};

struct _GstMpegTsExtendedEventDescriptor
{
  guint8 descriptor_number;
  guint8 last_descriptor_number;
  gchar  language_code[3];
  guint8 nb_items;
  GstMpegTsExtendedEventItem items[128];
  gchar *text;
};

gboolean gst_mpegts_descriptor_parse_dvb_extended_event (const GstMpegTsDescriptor *descriptor,
							  GstMpegTsExtendedEventDescriptor *res);

/* GST_MTS_DESC_DVB_COMPONENT (0x50) */
typedef struct _GstMpegTsComponentDescriptor GstMpegTsComponentDescriptor;
struct _GstMpegTsComponentDescriptor
{
  guint8 stream_content;
  guint8 component_type;
  guint8 component_tag;
  /* FIXME : Make it a separate (allocated, null-terminated) return value  */
  gchar  language_code[3];
  gchar *text;
};

gboolean gst_mpegts_descriptor_parse_dvb_component (const GstMpegTsDescriptor *descriptor,
						    GstMpegTsComponentDescriptor *res);

/* GST_MTS_DESC_DVB_STREAM_IDENTIFIER (0x52) */
gboolean gst_mpegts_descriptor_parse_dvb_stream_identifier (const GstMpegTsDescriptor *descriptor,
							    guint8 *component_tag);

/* GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM (0x5A) */
/* FIXME : Implement */

/* GST_MTS_DESC_DVB_FREQUENCY_LIST (0x62) */
/* FIXME : Implement */

/* GST_MTS_DESC_DVB_DATA_BROADCAST (0x64) */
/* FIXME: Implement */

/* GST_MTS_DESC_DVB_DATA_BROADCAST_ID (0x66) */
/* FIXME : Implement */

/* GST_MTS_DESC_DVB_AC3 (0x6a) */
/* FIXME : Implement */


/* GST_MTS_DESC_DTG_LOGICAL_CHANNEL (0x83) */
typedef struct _GstMpegTsLogicalChannelDescriptor GstMpegTsLogicalChannelDescriptor;
typedef struct _GstMpegTsLogicalChannel GstMpegTsLogicalChannel;

struct _GstMpegTsLogicalChannel
{
  guint16   service_id;
  gboolean  visible_service;
  guint16   logical_channel_number;
};

struct _GstMpegTsLogicalChannelDescriptor
{
  guint                   nb_channels;
  GstMpegTsLogicalChannel channels[64];
};

/* FIXME : Maybe make two methods. One for getting the number of channels,
 * and the other for getting the content for one channel ? */
gboolean
gst_mpegts_descriptor_parse_logical_channel (const GstMpegTsDescriptor *descriptor,
					     GstMpegTsLogicalChannelDescriptor *res);

#endif				/* GST_MPEGTS_DESCRIPTOR_H */
