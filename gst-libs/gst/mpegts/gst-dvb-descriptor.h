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

#endif				/* GST_MPEGTS_DESCRIPTOR_H */
