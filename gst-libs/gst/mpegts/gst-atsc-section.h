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

#ifndef GST_ATSC_SECTION_H
#define GST_ATSC_SECTION_H

#include <gst/gst.h>
#include <gst/mpegts/gstmpegtssection.h>
#include <gst/mpegts/gstmpegtsdescriptor.h>

G_BEGIN_DECLS

/**
 * GstMpegTsSectionATSCTableID:
 *
 * Values for a #GstMpegTsSection table_id.
 *
 * These are the registered ATSC table_id variants.
 *
 * see also: #GstMpegTsSectionTableID
 */
typedef enum {


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
} GstMpegTsSectionATSCTableID;

/* TVCT/CVCT */
#define GST_TYPE_MPEGTS_ATSC_VCT (gst_mpegts_atsc_vct_get_type ())
#define GST_TYPE_MPEGTS_ATSC_VCT_SOURCE (gst_mpegts_atsc_vct_source_get_type ())

typedef struct _GstMpegTsAtscVCTSource GstMpegTsAtscVCTSource;
typedef struct _GstMpegTsAtscVCT GstMpegTsAtscVCT;

/**
 * GstMpegTsAtscVCTSource:
 *
 * Source from a @GstMpegTsAtscVCT, can be used both for TVCT and CVCT tables
 */
struct _GstMpegTsAtscVCTSource
{
  gchar    *short_name;
  guint16   major_channel_number;
  guint16   minor_channel_number;
  guint8    modulation_mode;
  guint32   carrier_frequency;
  guint16   channel_TSID;
  guint16   program_number;
  /* FIXME: */
  guint8    ETM_location;
  gboolean  access_controlled;
  gboolean  hidden;
  gboolean  path_select; /* CVCT only - reserved bit in TVCT */
  gboolean  out_of_band; /* CVCT only - reserved bit in TVCT */
  gboolean  hide_guide;
  /* FIXME: */
  guint8    service_type;
  guint16   source_id;
  GPtrArray *descriptors;
};

/**
 * GstMpegTsAtscVCT:
 *
 * Represents both:
 *   Terrestrial Virtual Channel Table (A65)
 *   Cable Virtual Channel Table (A65)
 *
 */
struct _GstMpegTsAtscVCT
{
  guint16   transport_stream_id;
  guint8    protocol_version;
  GPtrArray *sources;
  GPtrArray *descriptors;
};

GType gst_mpegts_atsc_vct_get_type (void);
GType gst_mpegts_atsc_vct_source_get_type (void);

const GstMpegTsAtscVCT * gst_mpegts_section_get_atsc_tvct (GstMpegTsSection * section);
const GstMpegTsAtscVCT * gst_mpegts_section_get_atsc_cvct (GstMpegTsSection * section);

/* MGT */
#define GST_TYPE_MPEGTS_ATSC_MGT (gst_mpegts_atsc_mgt_get_type ())
#define GST_TYPE_MPEGTS_ATSC_MGT_TABLE (gst_mpegts_atsc_mgt_table_get_type ())

typedef struct _GstMpegTsAtscMGTTable GstMpegTsAtscMGTTable;
typedef struct _GstMpegTsAtscMGT GstMpegTsAtscMGT;

typedef enum {
  GST_MPEG_TS_ATSC_MGT_TABLE_TYPE_EIT0 = 0x0100,
  GST_MPEG_TS_ATSC_MGT_TABLE_TYPE_EIT127 = 0x017F,
  GST_MPEG_TS_ATSC_MGT_TABLE_TYPE_ETT0 = 0x0200,
  GST_MPEG_TS_ATSC_MGT_TABLE_TYPE_ETT127 = 0x027F
} GstMpegTsAtscMGTTableType;

/**
 * GstMpegTsAtscMGTTable:
 *
 * Source from a @GstMpegTsAtscMGT
 */
struct _GstMpegTsAtscMGTTable
{
  guint16 table_type;
  guint16 pid;
  guint8  version_number;
  guint32 number_bytes;
  GPtrArray *descriptors;
};

/**
 * GstMpegTsAtscMGT:
 *
 * Terrestrial Virtual Channel Table (A65)
 *
 */
struct _GstMpegTsAtscMGT
{
  guint8  protocol_version;
  guint16 tables_defined;
  GPtrArray *tables;
  GPtrArray *descriptors;
};

GType gst_mpegts_atsc_mgt_get_type (void);
GType gst_mpegts_atsc_mgt_table_get_type (void);

const GstMpegTsAtscMGT * gst_mpegts_section_get_atsc_mgt (GstMpegTsSection * section);

/* Multiple string structure (used in ETT and EIT */

#define GST_TYPE_MPEGTS_ATSC_STRING_SEGMENT (gst_mpegts_atsc_string_segment_get_type())
#define GST_TYPE_MPEGTS_ATSC_MULT_STRING (gst_mpegts_atsc_mult_string_get_type())

typedef struct _GstMpegTsAtscStringSegment GstMpegTsAtscStringSegment;
typedef struct _GstMpegTsAtscMultString GstMpegTsAtscMultString;

struct _GstMpegTsAtscStringSegment {
  guint8 compression_type;
  guint8 mode;
  guint8 compressed_data_size;
  guint8 *compressed_data;

  gchar *cached_string;
};

const gchar * gst_mpegts_atsc_string_segment_get_string (GstMpegTsAtscStringSegment * seg);

struct _GstMpegTsAtscMultString {
  gchar      iso_639_langcode[4];
  GPtrArray *segments;
};

GType gst_mpegts_atsc_string_segment_get_type (void);
GType gst_mpegts_atsc_mult_string_get_type (void);

/* EIT */

#define GST_TYPE_MPEGTS_ATSC_EIT_EVENT (gst_mpegts_atsc_eit_event_get_type())
#define GST_TYPE_MPEGTS_ATSC_EIT (gst_mpegts_atsc_eit_get_type())

typedef struct _GstMpegTsAtscEITEvent GstMpegTsAtscEITEvent;
typedef struct _GstMpegTsAtscEIT GstMpegTsAtscEIT;

struct _GstMpegTsAtscEITEvent {
  guint16        event_id;
  guint32        start_time;
  guint8         etm_location;
  guint32        length_in_seconds;
  GPtrArray     *titles;

  GPtrArray *descriptors;
};

/**
 * GstMpegTsAtscEIT:
 * @events: (element-type FIXME): Events
 *
 * Event Information Table (ATSC)
 *
 */
struct _GstMpegTsAtscEIT
{
  guint16        source_id;
  guint8         protocol_version;

  GPtrArray     *events;
};

GType gst_mpegts_atsc_eit_event_get_type (void);
GType gst_mpegts_atsc_eit_get_type (void);

const GstMpegTsAtscEIT *gst_mpegts_section_get_atsc_eit (GstMpegTsSection *section);

/* ETT */

#define GST_TYPE_MPEGTS_ATSC_ETT (gst_mpegts_atsc_ett_get_type())

typedef struct _GstMpegTsAtscETT GstMpegTsAtscETT;

/**
 * GstMpegTsAtscETT:
 * @messages: (element-type FIXME): List of texts
 *
 * Extended Text Table (ATSC)
 *
 */
struct _GstMpegTsAtscETT
{
  guint16        ett_table_id_extension;
  guint16        protocol_version;
  guint32        etm_id;

  GPtrArray     *messages;
};

GType gst_mpegts_atsc_ett_get_type (void);

const GstMpegTsAtscETT *gst_mpegts_section_get_atsc_ett (GstMpegTsSection *section);

/* STT */
#define GST_TYPE_MPEGTS_ATSC_STT (gst_mpegts_atsc_stt_get_type ())

typedef struct _GstMpegTsAtscSTT GstMpegTsAtscSTT;

/**
 * GstMpegTsAtscSTT:
 *
 * System Time Table (A65)
 *
 */
struct _GstMpegTsAtscSTT
{
  guint8     protocol_version;
  guint32    system_time;
  guint8     gps_utc_offset;
  gboolean   ds_status;
  guint8     ds_dayofmonth;
  guint8     ds_hour;
  GPtrArray *descriptors;

  GstDateTime *utc_datetime;
};

GType gst_mpegts_atsc_stt_get_type (void);

const GstMpegTsAtscSTT * gst_mpegts_section_get_atsc_stt (GstMpegTsSection * section);
/* FIXME receive a non-const parameter but we only provide a const getter */
GstDateTime * gst_mpegts_atsc_stt_get_datetime_utc (GstMpegTsAtscSTT * stt);

G_END_DECLS

#endif				/* GST_MPEGTS_SECTION_H */
