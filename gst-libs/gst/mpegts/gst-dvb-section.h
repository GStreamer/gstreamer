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

#ifndef GST_DVB_SECTION_H
#define GST_DVB_SECTION_H

#include <gst/gst.h>
#include <gst/mpegts/gstmpegtssection.h>
#include <gst/mpegts/gstmpegtsdescriptor.h>

/**
 * GstMpegTsSectionDVBTableID:
 *
 * Values for a #GstMpegTsSection table_id.
 *
 * These are the registered DVB table_id variants.
 *
 * see also: #GstMpegTsSectionTableID
 */
typedef enum {
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
  GST_MTS_TABLE_ID_LL_FEC_PARITY_DATA_TABLE		= 0xB1

} GstMpegTsSectionDVBTableID;

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



/* NIT */

typedef struct _GstMpegTsNITStream GstMpegTsNITStream;
typedef struct _GstMpegTsNIT GstMpegTsNIT;

#define GST_TYPE_MPEGTS_NIT (gst_mpegts_nit_get_type())
#define GST_TYPE_MPEGTS_NIT_STREAM (gst_mpegts_nit_stream_get_type())

/**
 * GstMpegTsNITStream:
 * @transport_stream_id:
 * @original_network_id:
 * @descriptors: (element-type GstMpegTsDescriptor):
 *
 */
struct _GstMpegTsNITStream
{
  guint16  transport_stream_id;
  guint16  original_network_id;

  GPtrArray  *descriptors;
};

/**
 * GstMpegTsNIT:
 * @actual_network: Whether this NIT corresponds to the actual stream
 * @descriptors: (element-type GstMpegTsDescriptor): the global descriptors
 * @streams: (element-type GstMpegTsNITStream): the streams
 *
 * Network Information Table (ISO/IEC 13818-1 / EN 300 468)
 *
 * The network_id is contained in the subtable_extension field of the
 * container #GstMpegTsSection.
 */
struct _GstMpegTsNIT
{
  gboolean   actual_network;

  GPtrArray  *descriptors;

  GPtrArray *streams;
};

GType gst_mpegts_nit_get_type (void);
GType gst_mpegts_nit_stream_get_type (void);

const GstMpegTsNIT *gst_mpegts_section_get_nit (GstMpegTsSection *section);

/* BAT */

typedef struct _GstMpegTsBATStream GstMpegTsBATStream;
typedef struct _GstMpegTsBAT GstMpegTsBAT;

#define GST_TYPE_MPEGTS_BAT (gst_mpegts_bat_get_type())
#define GST_TYPE_MPEGTS_BAT_STREAM (gst_mpegts_bat_get_type())

struct _GstMpegTsBATStream
{
  guint16   transport_stream_id;
  guint16   original_network_id;

  GPtrArray   *descriptors;
};

/**
 * GstMpegTsBAT:
 *
 * @descriptors: (element-type GstMpegTsDescriptor):
 * @streams: (element-type GstMpegTsBATStream):
 *
 * DVB Bouquet Association Table (EN 300 468)
 */
struct _GstMpegTsBAT
{
  GPtrArray     *descriptors;

  GPtrArray  *streams;
};

GType gst_mpegts_bat_get_type (void);
GType gst_mpegts_bat_stream_get_type (void);

const GstMpegTsBAT *gst_mpegts_section_get_bat (GstMpegTsSection *section);

/* SDT */
#define GST_TYPE_MPEGTS_SDT (gst_mpegts_sdt_get_type())
#define GST_TYPE_MPEGTS_SDT_SERVICE (gst_mpegts_sdt_service_get_type())

typedef struct _GstMpegTsSDTService GstMpegTsSDTService;
typedef struct _GstMpegTsSDT GstMpegTsSDT;

/**
 * GstMpegTsSDTService:
 *
 * @descriptors: (element-type GstMpegTsDescriptor): List of descriptors
 *
 */
struct _GstMpegTsSDTService
{
  guint16    service_id;

  gboolean   EIT_schedule_flag;
  gboolean   EIT_present_following_flag;
  GstMpegTsRunningStatus running_status;
  gboolean   free_CA_mode;

  GPtrArray    *descriptors;
};

/**
 * GstMpegTsSDT:
 *
 * @services: (element-type GstMpegTsSDTService): List of services
 *
 * Service Description Table (EN 300 468)
 *
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
 * @descriptors: (element-type GstMpegTsDescriptor): List of descriptors
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

  GPtrArray      *descriptors;
};

/**
 * GstMpegTsEIT:
 *
 * @events: (element-type GstMpegTsEITEvent): List of events
 *
 * Event Information Table (EN 300 468)
 *
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
 * @descriptors: (element-type GstMpegTsDescriptor): List of descriptors
 *
 * Time Offset Table (EN 300 468)
 *
 */
struct _GstMpegTsTOT
{
  GstDateTime   *utc_time;

  GPtrArray        *descriptors;
};

GType gst_mpegts_tot_get_type (void);
const GstMpegTsTOT *gst_mpegts_section_get_tot (GstMpegTsSection *section);

#endif				/* GST_MPEGTS_SECTION_H */
