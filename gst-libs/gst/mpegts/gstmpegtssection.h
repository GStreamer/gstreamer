/*
 * mpegtspacketizer.h - 
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
 */

#ifndef GST_MPEGTS_SECTION_H
#define GST_MPEGTS_SECTION_H

#include <gst/gst.h>
#include <gst/mpegts/gstmpegtsdescriptor.h>

typedef struct _GstMpegTsSection GstMpegTsSection;

#define GST_TYPE_MPEGTS_SECTION (gst_mpegts_section_get_type())
#define GST_MPEGTS_SECTION(section) ((GstMpegTsSection*) section)

#define GST_MPEGTS_SECTION_TYPE(section) (GST_MPEGTS_SECTION (section)->section_type)

GType gst_mpegts_section_get_type (void);

/**
 * GstMpegTsSectionType:
 * @GST_MPEGTS_SECTION_UNKNOWN: Unknown section type
 * @GST_MPEGTS_SECTION_PAT: Program Association Table (ISO/IEC 13818-1)
 * @GST_MPEGTS_SECTION_PMT: Program Map Table (ISO/IEC 13818-1)
 * @GST_MPEGTS_SECTION_CAT: Conditional Access Table (ISO/IEC 13818-1)
 * @GST_MPEGTS_SECTION_TSDT: Transport Stream Description Table (ISO/IEC 13818-1)
 * @GST_MPEGTS_SECTION_EIT: Event Information Table (EN 300 468)
 * @GST_MPEGTS_SECTION_NIT: Network Information Table (ISO/IEC 13818-1 / EN 300 468)
 * @GST_MPEGTS_SECTION_BAT: Bouquet Association Table ((EN 300 468)
 * @GST_MPEGTS_SECTION_SDT: Service Description Table (EN 300 468)
 * @GST_MPEGTS_SECTION_TDT: Time and Date Table (EN 300 468)
 * @GST_MPEGTS_SECTION_TOT: Time Offset Table (EN 300 468)
 * @GST_MPEGTS_SECTION_LAST:
 *
 * Types of #GstMpegTsSection that the library handles.
 */
typedef enum {
  GST_MPEGTS_SECTION_UNKNOWN           = 0,
  GST_MPEGTS_SECTION_PAT, 
  GST_MPEGTS_SECTION_PMT, 
  GST_MPEGTS_SECTION_CAT, 
  GST_MPEGTS_SECTION_TSDT,
  GST_MPEGTS_SECTION_EIT, 
  GST_MPEGTS_SECTION_NIT, 
  GST_MPEGTS_SECTION_BAT, 
  GST_MPEGTS_SECTION_SDT, 
  GST_MPEGTS_SECTION_TDT, 
  GST_MPEGTS_SECTION_TOT, 
  GST_MPEGTS_SECTION_LAST
} GstMpegTsSectionType;

/* FIXME : How do we deal with clashing table_id for the various standards:
 * * ISO/IEC 13818-1 and ITU H.222.0 : Takes precedence over all
 * * DVB : most used ?
 * * ATSC
 * * ISDB (and the brazilian variant)
 * * DTMB (China)
 * * DMB (South Korea)
 *
 * Do we create a different enum for variants ?
 */
/**
 * GstMpegTsSectionTableID:
 *
 * Values for a #GstMpegTsSection table_id
 */
typedef enum {
  /* ITU H.222.0 / IEC 13818-1 */
  GST_MTS_TABLE_ID_PROGRAM_ASSOCIATION		= 0x00,
  GST_MTS_TABLE_ID_CONDITIONAL_ACCESS		= 0x01,
  GST_MTS_TABLE_ID_TS_PROGRAM_MAP		= 0x02,
  GST_MTS_TABLE_ID_TS_DESCRIPTION		= 0x03,
  GST_MTS_TABLE_ID_14496_SCENE_DESCRIPTION	= 0x04,
  GST_MTS_TABLE_ID_14496_OBJET_DESCRIPTOR	= 0x05,
  GST_MTS_TABLE_ID_METADATA			= 0x06,
  GST_MTS_TABLE_ID_IPMP_CONTROL_INFORMATION	= 0x07,
  
  /* IEC 13818-6 (DSM-CC) */
  GST_MTS_TABLE_ID_DSM_CC_MULTIPROTO_ENCAPSULATED_DATA	= 0x3A,
  GST_MTS_TABLE_ID_DSM_CC_U_N_MESSAGES			= 0x3B,
  GST_MTS_TABLE_ID_DSM_CC_DOWNLOAD_DATA_MESSAGES	= 0x3C,
  GST_MTS_TABLE_ID_DSM_CC_STREAM_DESCRIPTORS		= 0x3D,
  GST_MTS_TABLE_ID_DSM_CC_PRIVATE_DATA			= 0x3E,
  GST_MTS_TABLE_ID_DSM_CC_ADDRESSABLE_SECTIONS		= 0x3F,

  /* EN 300 468 (DVB) v 1.12.1 */
  GST_MTS_TABLE_ID_NETWORK_INFORMATION_ACTUAL_NETWORK	= 0x40,
  GST_MTS_TABLE_ID_NETWORK_INFORMATION_OTHER_NETWORK	= 0x41,
  GST_MTS_TABLE_ID_SERVICE_DESCRIPTION_ACTUAL_TS	= 0x42,
  GST_MTS_TABLE_ID_SERVICE_DESCRIPTION_OTHER_TS		= 0x46,
  GST_MTS_TABLE_ID_BOUQUET_ASSOCIATION			= 0x4A,
  GST_MTS_TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_PRESENT	= 0x4E,
  GST_MTS_TABLE_ID_EVENT_INFORMATION_OTHER_TS_PRESENT	= 0x4F,
  GST_MTS_TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_SCHEDULE_1	= 0x50,
  GST_MTS_TABLE_ID_EVENT_INFORMATION_ACTUAL_TS_SCHEDULE_N	= 0x5F,
  GST_MTS_TABLE_ID_EVENT_INFORMATION_OTHER_TS_SCHEDULE_1	= 0x60,
  GST_MTS_TABLE_ID_EVENT_INFORMATION_OTHER_TS_SCHEDULE_N	= 0x6F,
  GST_MTS_TABLE_ID_TIME_DATE				= 0x70,
  GST_MTS_TABLE_ID_RUNNING_STATUS			= 0x71,
  GST_MTS_TABLE_ID_STUFFING				= 0x72,
  GST_MTS_TABLE_ID_TIME_OFFSET				= 0x73,

  /* TS 102 812 (MHP v1.1.3) */
  GST_MTS_TABLE_ID_APPLICATION_INFORMATION_TABLE	= 0x74,

  /* TS 102 323 (DVB TV Anytime v1.5.1) */
  GST_MTS_TABLE_ID_CONTAINER				= 0x75,
  GST_MTS_TABLE_ID_RELATED_CONTENT			= 0x76,
  GST_MTS_TABLE_ID_CONTENT_IDENTIFIER			= 0x77,
  
  /* EN 301 192 (DVB specification for data broadcasting) */
  GST_MTS_TABLE_ID_MPE_FEC				= 0x78,

  /* TS 102 323 (DVB TV Anytime v1.5.1) */
  GST_MTS_TABLE_ID_RESOLUTION_NOTIFICATION		= 0x79,

  /* TS 102 772 (DVB-SH Multi-Protocol Encapsulation) */
  GST_MTS_TABLE_ID_MPE_IFEC				= 0x7A,
  
  /* EN 300 468 (DVB) v 1.12.1 */
  GST_MTS_TABLE_ID_DISCONTINUITY_INFORMATION		= 0x7E,
  GST_MTS_TABLE_ID_SELECTION_INFORMATION		= 0x7F,

  /* ETR 289 (DVB Support for use of scrambling and CA) */
  GST_MTS_TABLE_ID_CA_MESSAGE_ECM_0			= 0x80,
  GST_MTS_TABLE_ID_CA_MESSAGE_ECM_1			= 0x81,
  GST_MTS_TABLE_ID_CA_MESSAGE_SYSTEM_PRIVATE_1		= 0x82,
  GST_MTS_TABLE_ID_CA_MESSAGE_SYSTEM_PRIVATE_N		= 0x8F,

  /* ... */

  /* EN 301 790 (DVB interaction channel for satellite distribution channels) */
  GST_MTS_TABLE_ID_SCT					= 0xA0,
  GST_MTS_TABLE_ID_FCT					= 0xA1,
  GST_MTS_TABLE_ID_TCT					= 0xA2,
  GST_MTS_TABLE_ID_SPT					= 0xA3,
  GST_MTS_TABLE_ID_CMT					= 0xA4,
  GST_MTS_TABLE_ID_TBTP					= 0xA5,
  GST_MTS_TABLE_ID_PCR_PACKET_PAYLOAD			= 0xA6,
  GST_MTS_TABLE_ID_TRANSMISSION_MODE_SUPPORT_PAYLOAD	= 0xAA,
  GST_MTS_TABLE_ID_TIM					= 0xB0,
  GST_MTS_TABLE_ID_LL_FEC_PARITY_DATA_TABLE		= 0xB1,

  /* ATSC (A/65) */
  GST_MTS_TABLE_ID_ATSC_MASTER_GUIDE                    = 0xC7,
  GST_MTS_TABLE_ID_ATSC_TERRESTRIAL_VIRTUAL_CHANNEL     = 0xC8,
  GST_MTS_TABLE_ID_ATSC_CABLE_VIRTUAL_CHANNEL           = 0xC9,
  GST_MTS_TABLE_ID_ATSC_RATING_REGION                   = 0xCA,
  GST_MTS_TABLE_ID_ATSC_EVENT_INFORMATION               = 0xCB,
  GST_MTS_TABLE_ID_ATSC_CHANNEL_OR_EVENT_EXTENDED_TEXT  = 0xCC,
  GST_MTS_TABLE_ID_ATSC_SYSTEM_TIME                     = 0xCD,
  /* ATSC (A/90) */
  GST_MTS_TABLE_ID_ATSC_DATA_EVENT                      = 0xCE,
  GST_MTS_TABLE_ID_ATSC_DATA_SERVICE                    = 0xCF,
  /* 0xD0 ?? */
  GST_MTS_TABLE_ID_ATSC_NETWORK_RESOURCE                = 0xD1,
  GST_MTS_TABLE_ID_ATSC_LONG_TERM_SERVICE               = 0xD2,
  GST_MTS_TABLE_ID_ATSC_DIRECTED_CHANNEL_CHANGE         = 0xD3,
  GST_MTS_TABLE_ID_ATSC_DIRECTED_CHANNEL_CHANGE_SECTION_CODE = 0xD4,
  /* 0xD5 ?? */
  GST_MTS_TABLE_ID_ATSC_AGGREGATE_EVENT_INFORMATION     = 0xD6,
  GST_MTS_TABLE_ID_ATSC_AGGREGATE_EXTENDED_TEXT         = 0xD7,
  /* 0xD8 ?? */
  GST_MTS_TABLE_ID_ATSC_AGGREGATE_DATA_EVENT            = 0xD9,
  GST_MTS_TABLE_ID_ATSC_SATELLITE_VIRTUAL_CHANNEL       = 0xDA,

  /* ISDB (FILLME) Might interfere with ATSC table id */

  /* Unset */
  GST_MTS_TABLE_ID_UNSET = 0xFF
  
} GstMpegTsSectionTableID;

/**
 * GstMpegTsSection:
 * @section_type: The type of section
 * @pid: The pid on which this section was found
 * @table_id: The table id of this section
 * @subtable_extension: This meaning differs per section. See the documentation
 * of the parsed section type for the meaning of this field
 * @version_number: Version of the section.
 * @current_next_indicator: Applies to current/next stream or not
 * @section_number: Number of the section (if multiple)
 * @last_section_number: Number of the last expected section (if multiple)
 * @crc: CRC
 *
 * Mpeg-TS Section Information (SI) (ISO/IEC 13818-1)
 */
struct _GstMpegTsSection
{
  /*< private >*/
  GstMiniObject parent;

  /*< public >*/
  GstMpegTsSectionType   section_type;

  guint16       pid;
  GstMpegTsSectionTableID        table_id;

  guint16       subtable_extension;
  guint8        version_number;

  gboolean      current_next_indicator;

  guint8        section_number;
  guint8        last_section_number;

  guint32       crc;

  /*< private >*/
  /* data: Points to beginning of section data
   * i.e. the first byte is the table_id field */
  guint8       *data;
  /* section_length: length of data (including final CRC if present) */
  guint		section_length;
  /* cached_parsed: cached copy of parsed section */
  gpointer     *cached_parsed;
  /* offset: offset of the section within the container stream */
  guint64       offset;
  /* short_section: TRUE if section_syntax_indicator == 0
   * FIXME : Maybe make public later on when allowing creation of
   * sections to that people can create private short sections ? */
  gboolean      short_section;
};

/**
 * GstMpegTsRunningStatus:
 *
 * Running status of a service.
 *
 * Corresponds to table 6 of ETSI EN 300 468 (v1.13.0)
 */
typedef enum
{
  GST_MPEGTS_RUNNING_STATUS_UNDEFINED = 0,
  GST_MPEGTS_RUNNING_STATUS_NOT_RUNNING,
  GST_MPEGTS_RUNNING_STATUS_STARTS_IN_FEW_SECONDS,
  GST_MPEGTS_RUNNING_STATUS_PAUSING,
  GST_MPEGTS_RUNNING_STATUS_RUNNING,
  GST_MPEGTS_RUNNING_STATUS_OFF_AIR
} GstMpegTsRunningStatus;


/* PAT */
typedef struct _GstMpegTsPatProgram GstMpegTsPatProgram;
/**
 * GstMpegTsPatProgram:
 * @program_number: the program number
 * @network_or_program_map_PID: the network of program map PID
 *
 * A program entry from a Program Association Table (ITU H.222.0, ISO/IEC 13818-1).
 */
struct _GstMpegTsPatProgram
{
  guint16 program_number;
  guint16 network_or_program_map_PID;
};

GArray *gst_mpegts_section_get_pat (GstMpegTsSection *section);

/* CAT */

GArray *gst_mpegts_section_get_cat (GstMpegTsSection *section);

/* PMT */
typedef struct _GstMpegTsPMTStream GstMpegTsPMTStream;
typedef struct _GstMpegTsPMT GstMpegTsPMT;
#define GST_TYPE_MPEGTS_PMT (gst_mpegts_pmt_get_type())
#define GST_TYPE_MPEGTS_PMT_STREAM (gst_mpegts_pmt_stream_get_type())

/**
 * GstMpegTsPMTStream:
 * @stream_type: the type of stream
 * @pid: the PID of the stream
 * @descriptors: (element-type GstMpegTsDescriptor): the descriptors of the
 * stream
 *
 * An individual stream definition.
 */
struct _GstMpegTsPMTStream
{
  guint8      stream_type;
  guint16     pid;

  GArray     *descriptors;
};

/**
 * GstMpegTsPMT:
 * @pcr_pid: PID of the stream containing PCR
 * @descriptors: (element-type GstMpegTsDescriptor): array of #GstMpegTsDescriptor
 * @streams: (element-type GstMpegTsPMTStream): Array of #GstMpegTsPMTStream
 *
 * Program Map Table (ISO/IEC 13818-1).
 *
 * The program_number is contained in the subtable_extension field of the
 * container #GstMpegTsSection.
 */
struct _GstMpegTsPMT
{
  guint16    pcr_pid;

  GArray    *descriptors;
  GPtrArray *streams;
};

GType gst_mpegts_pmt_get_type (void);
GType gst_mpegts_pmt_stream_get_type (void);

const GstMpegTsPMT *gst_mpegts_section_get_pmt (GstMpegTsSection *section);

/* TSDT */

GArray *gst_mpegts_section_get_tsdt (GstMpegTsSection *section);

/* NIT */

typedef struct _GstMpegTsNITStream GstMpegTsNITStream;
typedef struct _GstMpegTsNIT GstMpegTsNIT;

#define GST_TYPE_MPEGTS_NIT (gst_mpegts_nit_get_type())
#define GST_TYPE_MPEGTS_NIT_STREAM (gst_mpegts_nit_stream_get_type())

/**
 * GstMpegTsNITStream:
 * @transport_stream_id:
 * @original_network_id:
 * @descriptors: (element-type GstMpegTsDescriptor)
 *
 */
struct _GstMpegTsNITStream
{
  guint16  transport_stream_id;
  guint16  original_network_id;

  GArray  *descriptors;
};

/**
 * GstMpegTsNIT:
 * @actual_network: Whether this NIT corresponds to the actual stream
 * @descriptors: (element-type GstMpegTsDescriptor) the global descriptors
 * @streams: (element-type GstMpegTsNITStream) the streams
 *
 * Network Information Table (ISO/IEC 13818-1 / EN 300 468)
 *
 * The network_id is contained in the subtable_extension field of the
 * container #GstMpegTsSection.
 */
struct _GstMpegTsNIT
{
  gboolean   actual_network;

  GArray    *descriptors;

  GPtrArray *streams;
};

GType gst_mpegts_nit_get_type (void);
GType gst_mpegts_nit_stream_get_type (void);

const GstMpegTsNIT *gst_mpegts_section_get_nit (GstMpegTsSection *section);

/* BAT */

typedef struct _GstMpegTsBATStream GstMpegTsBATStream;
typedef struct _GstMpegTsBAT GstMpegTsBAT;

#define GST_TYPE_MPEGTS_BAT (gst_mpegts_bat_get_type())

struct _GstMpegTsBATStream
{
  guint16   transport_stream_id;
  guint16   original_network_id;

  GArray   *descriptors;
};

/**
 * GstMpegTsBAT:
 *
 * DVB Bouquet Association Table (EN 300 468)
 */
struct _GstMpegTsBAT
{
  gboolean    actual_network;

  GArray     *descriptors;

  GPtrArray  *streams;
};

GType gst_mpegts_bat_get_type (void);

/* SDT */
#define GST_TYPE_MPEGTS_SDT (gst_mpegts_sdt_get_type())
#define GST_TYPE_MPEGTS_SDT_SERVICE (gst_mpegts_sdt_service_get_type())

typedef struct _GstMpegTsSDTService GstMpegTsSDTService;
typedef struct _GstMpegTsSDT GstMpegTsSDT;

struct _GstMpegTsSDTService
{
  guint16    service_id;

  gboolean   EIT_schedule_flag;
  gboolean   EIT_present_following_flag;
  GstMpegTsRunningStatus running_status;
  gboolean   free_CA_mode;

  GArray    *descriptors;
};

/**
 * GstMpegTsSDT:
 *
 * Service Description Table (EN 300 468)
 */
struct _GstMpegTsSDT
{
  guint16    original_network_id;
  gboolean   actual_ts;

  GPtrArray *services;
};

GType gst_mpegts_sdt_get_type (void);
GType gst_mpegts_sdt_service_get_type (void);

const GstMpegTsSDT *gst_mpegts_section_get_sdt (GstMpegTsSection *section);

/* EIT */

#define GST_TYPE_MPEGTS_EIT (gst_mpegts_eit_get_type())
#define GST_TYPE_MPEGTS_EIT_EVENT (gst_mpegts_eit_event_get_type())

typedef struct _GstMpegTsEITEvent GstMpegTsEITEvent;
typedef struct _GstMpegTsEIT GstMpegTsEIT;

/**
 * GstMpegTsEITEvent:
 *
 * Event from a @GstMpegTsEIT
 */
struct _GstMpegTsEITEvent
{
  guint16      event_id;

  GstDateTime *start_time;
  guint32      duration;

  GstMpegTsRunningStatus running_status;
  gboolean     free_CA_mode;

  GArray      *descriptors;
};

/**
 * GstMpegTsEIT:
 *
 * Event Information Table (EN 300 468)
 */
struct _GstMpegTsEIT
{
  guint16        transport_stream_id;
  guint16        original_network_id;
  guint8         segment_last_section_number;
  guint8         last_table_id;

  gboolean       actual_stream;
  gboolean       present_following;

  GPtrArray     *events;
};

GType gst_mpegts_eit_get_type (void);
GType gst_mpegts_eit_event_get_type (void);

const GstMpegTsEIT *gst_mpegts_section_get_eit (GstMpegTsSection *section);

/* TDT */
GstDateTime *gst_mpegts_section_get_tdt (GstMpegTsSection *section);

/* TOT */

typedef struct _GstMpegTsTOT GstMpegTsTOT;
#define GST_TYPE_MPEGTS_TOT (gst_mpegts_tot_get_type())
/**
 * GstMpegTsTOT:
 *
 * Time Offset Table (EN 300 468)
 */
struct _GstMpegTsTOT
{
  GstDateTime   *utc_time;

  GArray        *descriptors;
};

GType gst_mpegts_tot_get_type (void);
const GstMpegTsTOT *gst_mpegts_section_get_tot (GstMpegTsSection *section);

/* generic */

#define gst_mpegts_section_ref(section)   ((GstMpegTsSection*) gst_mini_object_ref (GST_MINI_OBJECT_CAST (section)))
#define gst_mpegts_section_unref(section) (gst_mini_object_unref (GST_MINI_OBJECT_CAST (section)))

GstMessage *gst_message_new_mpegts_section (GstObject *parent, GstMpegTsSection *section);

GstMpegTsSection *gst_message_parse_mpegts_section (GstMessage *message);

GstMpegTsSection *gst_mpegts_section_new (guint16 pid,
					   guint8 * data,
					   gsize data_size);

#endif				/* GST_MPEGTS_SECTION_H */
