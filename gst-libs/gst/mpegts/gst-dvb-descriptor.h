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

#ifndef GST_DVB_DESCRIPTOR_H
#define GST_DVB_DESCRIPTOR_H

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstMpegTsDVBDescriptorType:
 *
 * The type of #GstMpegTsDescriptor
 *
 * These values correspond to the registered descriptor type from
 * the various DVB specifications.
 *
 * Consult the relevant specifications for more details.
 */
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

/**
 * GstMpegTsDVBExtendedDescriptorType:
 *
 * The type of #GstMpegTsDescriptor
 *
 * These values correspond to the registered extended descriptor
 * type from the various DVB specifications.
 *
 * Consult the relevant specifications for more details.
 */
typedef enum {
  /* 00 - 0x7F DVB extended tags ETSI EN 300 468
   * (Specification for Service Information (SI) in DVB systems)
   */
  GST_MTS_DESC_EXT_DVB_IMAGE_ICON               = 0x00,
  /* TS/TR 102 825 */
  GST_MTS_DESC_EXT_DVB_CPCM_DELIVERY_SIGNALLING = 0x01,
  GST_MTS_DESC_EXT_DVB_CP                       = 0x02,
  GST_MTS_DESC_EXT_DVB_CP_IDENTIFIER            = 0x03,
  GST_MTS_DESC_EXT_DVB_T2_DELIVERY_SYSTEM       = 0x04,
  GST_MTS_DESC_EXT_DVB_SH_DELIVERY_SYSTEM       = 0x05,
  GST_MTS_DESC_EXT_DVB_SUPPLEMENTARY_AUDIO      = 0x06,
  GST_MTS_DESC_EXT_DVB_NETWORK_CHANGE_NOTIFY    = 0x07,
  GST_MTS_DESC_EXT_DVB_MESSAGE                  = 0x08,
  GST_MTS_DESC_EXT_DVB_TARGET_REGION            = 0x09,
  GST_MTS_DESC_EXT_DVB_TARGET_REGION_NAME       = 0x0A,
  GST_MTS_DESC_EXT_DVB_SERVICE_RELOCATED        = 0x0B,
  GST_MTS_DESC_EXT_DVB_XAIT_PID                 = 0x0C,
  GST_MTS_DESC_EXT_DVB_C2_DELIVERY_SYSTEM       = 0x0D,
  GST_MTS_DESC_EXT_DVB_DTS_HD_AUDIO_STREAM      = 0x0E,
  GST_MTS_DESC_EXT_DVB_DTS_NEUTRAL              = 0x0F,
  GST_MTS_DESC_EXT_DVB_VIDEO_DEPTH_RANGE        = 0x10,
  GST_MTS_DESC_EXT_DVB_T2MI                     = 0x11,
  GST_MTS_DESC_EXT_DVB_URI_LINKAGE              = 0x13,
} GstMpegTsDVBExtendedDescriptorType;

/* GST_MTS_DESC_DVB_CAROUSEL_IDENTIFIER (0x13) */
/* FIXME : Implement */

/* GST_MTS_DESC_DVB_NETWORK_NAME (0x40) */
gboolean gst_mpegts_descriptor_parse_dvb_network_name (const GstMpegTsDescriptor *descriptor,
						       gchar **name);

GstMpegTsDescriptor *gst_mpegts_descriptor_from_dvb_network_name (const gchar * name);

/* GST_MTS_DESC_DVB_STUFFING (0x42) */
gboolean gst_mpegts_descriptor_parse_dvb_stuffing (const GstMpegTsDescriptor * descriptor,
                                                   guint8 ** stuffing_bytes);


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

/* GST_MTS_DESC_DVB_BOUQUET_NAME (0x47) */
gboolean gst_mpegts_descriptor_parse_dvb_bouquet_name (const GstMpegTsDescriptor * descriptor,
                                                       gchar ** bouquet_name);

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

gboolean gst_mpegts_descriptor_parse_dvb_service (const GstMpegTsDescriptor *descriptor,
						  GstMpegTsDVBServiceType *service_type,
						  gchar **service_name,
						  gchar **provider_name);

GstMpegTsDescriptor *gst_mpegts_descriptor_from_dvb_service (GstMpegTsDVBServiceType service_type,
							     const gchar * service_name,
							     const gchar * service_provider);

/* GST_MTS_DESC_DVB_SERVICE_LIST (0x41) */
typedef struct _GstMpegTsDVBServiceListItem GstMpegTsDVBServiceListItem;

/**
 * GstMpegTsDVBServiceListItem:
 * @service_id: the id of a service
 * @type: the type of a service
 */
struct _GstMpegTsDVBServiceListItem
{
  guint16                 service_id;
  GstMpegTsDVBServiceType type;
};

gboolean gst_mpegts_descriptor_parse_dvb_service_list (const GstMpegTsDescriptor * descriptor,
    GPtrArray ** list);

/* GST_MTS_DESC_DVB_LINKAGE (0x4A) */
/**
 * GstMpegTsDVBLinkageType:
 *
 * Linkage Type (EN 300 468 v.1.13.1)
 */
typedef enum {
  /* 0x00, 0x0F-0x7F reserved for future use */
  GST_MPEGTS_DVB_LINKAGE_RESERVED_00               = 0x00,
  GST_MPEGTS_DVB_LINKAGE_INFORMATION               = 0x01,
  GST_MPEGTS_DVB_LINKAGE_EPG                       = 0x02,
  GST_MPEGTS_DVB_LINKAGE_CA_REPLACEMENT            = 0x03,
  GST_MPEGTS_DVB_LINKAGE_TS_CONTAINING_COMPLETE_SI = 0x04,
  GST_MPEGTS_DVB_LINKAGE_SERVICE_REPLACEMENT       = 0x05,
  GST_MPEGTS_DVB_LINKAGE_DATA_BROADCAST            = 0x06,
  GST_MPEGTS_DVB_LINKAGE_RCS_MAP                   = 0x07,
  GST_MPEGTS_DVB_LINKAGE_MOBILE_HAND_OVER          = 0x08,
  GST_MPEGTS_DVB_LINKAGE_SYSTEM_SOFTWARE_UPDATE    = 0x09,
  GST_MPEGTS_DVB_LINKAGE_TS_CONTAINING_SSU         = 0x0A,
  GST_MPEGTS_DVB_LINKAGE_IP_MAC_NOTIFICATION       = 0x0B,
  GST_MPEGTS_DVB_LINKAGE_TS_CONTAINING_INT         = 0x0C,
  GST_MPEGTS_DVB_LINKAGE_EVENT                     = 0x0D,
  GST_MPEGTS_DVB_LINKAGE_EXTENDED_EVENT            = 0x0E,
} GstMpegTsDVBLinkageType;

typedef enum {
  GST_MPEGTS_DVB_LINKAGE_HAND_OVER_RESERVED        = 0x00,
  GST_MPEGTS_DVB_LINKAGE_HAND_OVER_IDENTICAL       = 0x01,
  GST_MPEGTS_DVB_LINKAGE_HAND_OVER_LOCAL_VARIATION = 0x02,
  GST_MPEGTS_DVB_LINKAGE_HAND_OVER_ASSOCIATED      = 0x03,
} GstMpegTsDVBLinkageHandOverType;

typedef struct _GstMpegTsDVBLinkageMobileHandOver GstMpegTsDVBLinkageMobileHandOver;
typedef struct _GstMpegTsDVBLinkageEvent GstMpegTsDVBLinkageEvent;
typedef struct _GstMpegTsDVBLinkageExtendedEvent GstMpegTsDVBLinkageExtendedEvent;
typedef struct _GstMpegTsDVBLinkageDescriptor GstMpegTsDVBLinkageDescriptor;

struct _GstMpegTsDVBLinkageMobileHandOver
{
  GstMpegTsDVBLinkageHandOverType hand_over_type;
  /* 0 = NIT, 1 = SDT */
  gboolean                        origin_type;
  guint16                         network_id;
  guint16                         initial_service_id;
};

struct _GstMpegTsDVBLinkageEvent
{
  guint16  target_event_id;
  gboolean target_listed;
  gboolean event_simulcast;
};

struct _GstMpegTsDVBLinkageExtendedEvent
{
  guint16        target_event_id;
  gboolean       target_listed;
  gboolean       event_simulcast;
  /* FIXME: */
  guint8         link_type;
  /* FIXME: */
  guint8         target_id_type;
  gboolean       original_network_id_flag;
  gboolean       service_id_flag;
  /* if (target_id_type == 3) */
  guint16        user_defined_id;
  /* else */
  guint16        target_transport_stream_id;
  guint16        target_original_network_id;
  guint16        target_service_id;
};

/**
 * GstMpegTsDVBLinkageDescriptor:
 * @transport_stream_id: the transport id
 * @original_network_id: the original network id
 * @service_id: the service id
 * @linkage_type: the type which %linkage_data has
 * @linkage_data: the linkage structure depending from %linkage_type
 * @private_data_length: the length for %private_data_bytes
 * @private_data_bytes: additional data bytes
 */
struct _GstMpegTsDVBLinkageDescriptor
{
  guint16                           transport_stream_id;
  guint16                           original_network_id;
  guint16                           service_id;
  GstMpegTsDVBLinkageType           linkage_type;
  gpointer                          linkage_data;
  guint8                            private_data_length;
  guint8                            *private_data_bytes;
};

gboolean gst_mpegts_descriptor_parse_dvb_linkage (const GstMpegTsDescriptor * descriptor,
                                                  GstMpegTsDVBLinkageDescriptor * res);

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

/**
 * GstMpegTsExtendedEventDescriptor:
 * @desctiptor_number:
 * @last_descriptor_number:
 * @language_code:
 * @nb_items:
 * @items: (element-type GstMpegTsExtendedEventItem): the #GstMpegTsExtendedEventItem
 * @text:
 *
 * Extended Event Descriptor (EN 300 468 v.1.13.1)
 */
struct _GstMpegTsExtendedEventDescriptor
{
  guint8 descriptor_number;
  guint8 last_descriptor_number;
  gchar  language_code[3];
  guint8 nb_items;
  GPtrArray *items;
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

/* GST_MTS_DESC_DVB_CA_IDENTIFIER (0x53) */
gboolean gst_mpegts_descriptor_parse_dvb_ca_identifier (const GstMpegTsDescriptor * descriptor,
                                                        GArray ** list);

/* GST_MTS_DESC_DVB_CONTENT (0x54) */
typedef struct _GstMpegTsContent GstMpegTsContent;
struct _GstMpegTsContent
{
  guint8 content_nibble_1;
  guint8 content_nibble_2;
  guint8 user_byte;
};

gboolean gst_mpegts_descriptor_parse_dvb_content (const GstMpegTsDescriptor *
	descriptor, GPtrArray ** content);

/* GST_MTS_DESC_DVB_PARENTAL_RATING (0x55) */
typedef struct _GstMpegTsDVBParentalRatingItem GstMpegTsDVBParentalRatingItem;

/**
 * GstMpegTsDVBParentalRating:
 * @country_code: This 24-bit field identifies a country using the 3-character
 * code as specified in ISO 3166
 * @rating: the rating age
 */
struct _GstMpegTsDVBParentalRatingItem
{
  gchar  country_code[3];
  guint8 rating;
};

gboolean gst_mpegts_descriptor_parse_dvb_parental_rating (const GstMpegTsDescriptor
        * descriptor, GPtrArray ** rating);

/* GST_MTS_DESC_DVB_TELETEXT (0x56) */
/**
 * GstMpegTsDVBTeletextType:
 *
 * The type of teletext page.
 *
 * As specified in Table 100 of ETSI EN 300 468 v1.13.1
 */
typedef enum {
	INITIAL_PAGE = 0x01,
	SUBTITLE_PAGE,
	ADDITIONAL_INFO_PAGE,
	PROGRAMME_SCHEDULE_PAGE,
	HEARING_IMPAIRED_PAGE
} GstMpegTsDVBTeletextType;

gboolean gst_mpegts_descriptor_parse_dvb_teletext_idx (const GstMpegTsDescriptor *
    descriptor, guint idx, gchar (*language_code)[4],
    GstMpegTsDVBTeletextType * teletext_type, guint8 * magazine_number,
    guint8 * page_number);

guint gst_mpegts_descriptor_parse_dvb_teletext_nb (const GstMpegTsDescriptor *
    descriptor);

/* GST_MTS_DESC_DVB_SUBTITLING (0x59) */
gboolean gst_mpegts_descriptor_parse_dvb_subtitling_idx (const GstMpegTsDescriptor *descriptor,
							 guint idx, gchar (*lang)[4],
							 guint8 *type, guint16 *composition_page_id,
							 guint16 *ancillary_page_id);
guint gst_mpegts_descriptor_parse_dvb_subtitling_nb (const GstMpegTsDescriptor *descriptor);

GstMpegTsDescriptor * gst_mpegts_descriptor_from_dvb_subtitling (const gchar *lang,
    guint8 type, guint16 composition, guint16 ancillary);



/* GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM (0x5A) */
typedef struct _GstMpegTsTerrestrialDeliverySystemDescriptor GstMpegTsTerrestrialDeliverySystemDescriptor;

typedef enum {
  GST_MPEGTS_TRANSMISSION_MODE_2K = 0,
  GST_MPEGTS_TRANSMISSION_MODE_8K,
  GST_MPEGTS_TRANSMISSION_MODE_AUTO,
  GST_MPEGTS_TRANSMISSION_MODE_4K,
  GST_MPEGTS_TRANSMISSION_MODE_1K,
  GST_MPEGTS_TRANSMISSION_MODE_16K,
  GST_MPEGTS_TRANSMISSION_MODE_32K,
  GST_MPEGTS_TRANSMISSION_MODE_C1,
  GST_MPEGTS_TRANSMISSION_MODE_C3780
} GstMpegTsTerrestrialTransmissionMode;

typedef enum {
  GST_MPEGTS_GUARD_INTERVAL_1_32 = 0,
  GST_MPEGTS_GUARD_INTERVAL_1_16,
  GST_MPEGTS_GUARD_INTERVAL_1_8,
  GST_MPEGTS_GUARD_INTERVAL_1_4,
  GST_MPEGTS_GUARD_INTERVAL_AUTO,
  GST_MPEGTS_GUARD_INTERVAL_1_128,
  GST_MPEGTS_GUARD_INTERVAL_19_128,
  GST_MPEGTS_GUARD_INTERVAL_19_256,
  GST_MPEGTS_GUARD_INTERVAL_PN420,
  GST_MPEGTS_GUARD_INTERVAL_PN595,
  GST_MPEGTS_GUARD_INTERVAL_PN945
} GstMpegTsTerrestrialGuardInterval;

typedef enum {
  GST_MPEGTS_HIERARCHY_NONE = 0,
  GST_MPEGTS_HIERARCHY_1,
  GST_MPEGTS_HIERARCHY_2,
  GST_MPEGTS_HIERARCHY_4,
  GST_MPEGTS_HIERARCHY_AUTO
} GstMpegTsTerrestrialHierarchy;

/**
 * GstMpegTsTerrestrialDeliverySystemDescriptor:
 * @frequency: the frequency in Hz (Hertz)
 * @bandwidth: the bandwidth in Hz (Hertz)
 * @priority: %TRUE High Priority %FALSE Low Priority
 * @time_slicing: %TRUE no time slicing %FALSE time slicing
 * @mpe_fec: %TRUE no mpe-fec is used %FALSE mpe-fec is use
 * @constellation: the constallation
 * @hierarchy: the hierarchy
 * @code_rate_hp:
 * @code_rate_lp:
 * @guard_interval:
 * @transmission_mode:
 * @other_frequency: %TRUE more frequency are use, else not
 *
 * Terrestrial Delivery System Descriptor (EN 300 468 v.1.13.1)
 */

struct _GstMpegTsTerrestrialDeliverySystemDescriptor
{
  guint32				frequency;
  guint32				bandwidth;
  gboolean				priority;
  gboolean				time_slicing;
  gboolean				mpe_fec;
  GstMpegTsModulationType		constellation;
  GstMpegTsTerrestrialHierarchy		hierarchy;
  GstMpegTsDVBCodeRate			code_rate_hp;
  GstMpegTsDVBCodeRate			code_rate_lp;
  GstMpegTsTerrestrialGuardInterval	guard_interval;
  GstMpegTsTerrestrialTransmissionMode	transmission_mode;
  gboolean				other_frequency;
};

gboolean gst_mpegts_descriptor_parse_terrestrial_delivery_system (const GstMpegTsDescriptor
              *descriptor, GstMpegTsTerrestrialDeliverySystemDescriptor * res);

/* GST_MTS_DESC_DVB_MULTILINGUAL_NETWORK_NAME (0x5B) */
typedef struct _GstMpegTsDvbMultilingualNetworkNameItem GstMpegTsDvbMultilingualNetworkNameItem;

/**
 * GstMpegTsDvbMultilingualNetworkNameItem:
 * @language_code: the ISO 639 language code
 * @network_name: the network name
 *
 * a multilingual network name entry
 */
struct _GstMpegTsDvbMultilingualNetworkNameItem
{
  gchar language_code[3];
  gchar *network_name;
};

gboolean gst_mpegts_descriptor_parse_dvb_multilingual_network_name (const GstMpegTsDescriptor
              *descriptor, GPtrArray ** network_name_items);

/* GST_MTS_DESC_DVB_MULTILINGUAL_BOUQUET_NAME (0x5C) */
typedef struct _GstMpegTsDvbMultilingualBouquetNameItem GstMpegTsDvbMultilingualBouquetNameItem;

/**
 * GstMpegTsDvbMultilingualBouquetNameItem:
 * @language_code: the ISO 639 language code
 * @bouquet_name: the bouquet name
 *
 * a multilingual bouquet name entry
 */
struct _GstMpegTsDvbMultilingualBouquetNameItem
{
  gchar language_code[3];
  gchar *bouquet_name;
};

gboolean gst_mpegts_descriptor_parse_dvb_multilingual_bouquet_name (const GstMpegTsDescriptor
              *descriptor, GPtrArray ** bouquet_name_items);

/* GST_MTS_DESC_DVB_MULTILINGUAL_SERVICE_NAME (0x5D) */
typedef struct _GstMpegTsDvbMultilingualServiceNameItem GstMpegTsDvbMultilingualServiceNameItem;

/**
 * GstMpegTsDvbMultilingualServiceNameItem:
 * @language_code: the ISO 639 language code
 * @provider_name: the provider name
 * @service_name: the service name
 *
 * a multilingual service name entry
 */
struct _GstMpegTsDvbMultilingualServiceNameItem
{
  gchar language_code[3];
  gchar *provider_name;
  gchar *service_name;
};

gboolean gst_mpegts_descriptor_parse_dvb_multilingual_service_name (const GstMpegTsDescriptor
              *descriptor, GPtrArray ** service_name_items);

/* GST_MTS_DESC_DVB_PRIVATE_DATA_SPECIFIER (0x5F) */
gboolean gst_mpegts_descriptor_parse_dvb_private_data_specifier (const GstMpegTsDescriptor
              * descriptor, guint32 * private_data_specifier, guint8 ** private_data,
              guint8 * length);

/* GST_MTS_DESC_DVB_FREQUENCY_LIST (0x62) */
gboolean gst_mpegts_descriptor_parse_dvb_frequency_list (const GstMpegTsDescriptor
    * descriptor, gboolean * offset, GArray ** list);

/* GST_MTS_DESC_DVB_DATA_BROADCAST (0x64) */
typedef struct _GstMpegTsDataBroadcastDescriptor GstMpegTsDataBroadcastDescriptor;

/**
 * GstMpegTsDataBroadcastDescriptor:
 * @data_broadcast_id: the data broadcast id
 * @component_tag: the component tag
 * @selector_bytes: the selector byte field
 * @language_code: language of @text
 * @text: description of data broadcast
 */
struct _GstMpegTsDataBroadcastDescriptor
{
  guint16     data_broadcast_id;
  guint8      component_tag;
  guint8      *selector_bytes;
  gchar       language_code[3];
  gchar       *text;
};

gboolean gst_mpegts_descriptor_parse_dvb_data_broadcast (const GstMpegTsDescriptor
              *descriptor, GstMpegTsDataBroadcastDescriptor * res);

/* GST_MTS_DESC_DVB_SCRAMBLING (0x65) */
typedef enum
{
  GST_MPEGTS_DVB_SCRAMBLING_MODE_RESERVED              = 0x00,
  GST_MPEGTS_DVB_SCRAMBLING_MODE_CSA1                  = 0x01,
  GST_MPEGTS_DVB_SCRAMBLING_MODE_CSA2                  = 0x02,
  GST_MPEGTS_DVB_SCRAMBLING_MODE_CSA3_STANDARD         = 0x03,
  GST_MPEGTS_DVB_SCRAMBLING_MODE_CSA3_MINIMAL_ENHANCED = 0x04,
  GST_MPEGTS_DVB_SCRAMBLING_MODE_CSA3_FULL_ENHANCED    = 0x05,
  /* 0x06 - 0x0f reserved for future use */
  GST_MPEGTS_DVB_SCRAMBLING_MODE_CISSA                 = 0x10,
  /* 0x11 - 0x1f reserved for future DVB-CISSA versions */
  GST_MPEGTS_DVB_SCRAMBLING_MODE_ATIS_0                = 0x70,
  GST_MPEGTS_DVB_SCRAMBLING_MODE_ATIS_F                = 0x7f,
} GstMpegTsDVBScramblingModeType;

gboolean gst_mpegts_descriptor_parse_dvb_scrambling (const GstMpegTsDescriptor * descriptor,
       GstMpegTsDVBScramblingModeType * scrambling_mode);

/* GST_MTS_DESC_DVB_DATA_BROADCAST_ID (0x66) */
gboolean gst_mpegts_descriptor_parse_dvb_data_broadcast_id (const GstMpegTsDescriptor
       * descriptor, guint16 * data_broadcast_id, guint8 ** id_selector_bytes, guint8 * len);

/* GST_MTS_DESC_DVB_AC3 (0x6a) */
/* FIXME : Implement */

/* GST_MTS_DESC_EXT_DVB_T2_DELIVERY_SYSTEM (0x7F && 0x04) */
typedef struct _GstMpegTsT2DeliverySystemCellExtension GstMpegTsT2DeliverySystemCellExtension;

/**
 * GstMpegTsT2DeliverySystemCellExtension:
 * @cell_id_extension: id of the sub cell
 * @transposer_frequency: centre frequency of the sub cell in Hz
 */
struct _GstMpegTsT2DeliverySystemCellExtension
{
  guint8  cell_id_extension;
  guint32 transposer_frequency;
};

typedef struct _GstMpegTsT2DeliverySystemCell GstMpegTsT2DeliverySystemCell;

/**
 * GstMpegTsT2DeliverySystemCell:
 * @cell_id: id of the cell
 * @centre_frequencies: centre frequencies in Hz
 * @sub_cells: (element-type GstMpegTsT2DeliverySystemCellExtension):
 */
struct _GstMpegTsT2DeliverySystemCell
{
  guint16      cell_id;
  GArray       *centre_frequencies;
  GPtrArray    *sub_cells;
};

typedef struct _GstMpegTsT2DeliverySystemDescriptor GstMpegTsT2DeliverySystemDescriptor;

/**
 * GstMpegTsT2DeliverySystemDescriptor:
 * @plp_id:
 * @t2_system_id:
 * @siso_miso:
 * @bandwidth:
 * @guard_interval:
 * @transmission_mode:
 * @other_frequency:
 * @tfs:
 * @cells: (element-type GstMpegTsT2DeliverySystemCell):
 *
 * describe DVB-T2 transmissions according to EN 302 755
 */
struct _GstMpegTsT2DeliverySystemDescriptor
{
  guint8                                plp_id;
  guint16                               t2_system_id;
  /* FIXME: */
  guint8                                siso_miso;
  guint32                               bandwidth;
  GstMpegTsTerrestrialGuardInterval     guard_interval;
  GstMpegTsTerrestrialTransmissionMode  transmission_mode;
  gboolean                              other_frequency;
  gboolean                              tfs;
  GPtrArray                             *cells;
};

gboolean gst_mpegts_descriptor_parse_dvb_t2_delivery_system (const GstMpegTsDescriptor
              *descriptor, GstMpegTsT2DeliverySystemDescriptor * res);

G_END_DECLS

#endif				/* GST_MPEGTS_DESCRIPTOR_H */
