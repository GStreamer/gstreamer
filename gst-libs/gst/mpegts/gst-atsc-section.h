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

/* TVCT */
#define GST_TYPE_MPEGTS_ATSC_TVCT (gst_mpegts_atsc_tvct_get_type ())
#define GST_TYPE_MPEGTS_ATSC_TVCT_SOURCE (gst_mpegts_atsc_tvct_source_get_type ())

typedef struct _GstMpegTsAtscTVCTSource GstMpegTsAtscTVCTSource;
typedef struct _GstMpegTsAtscTVCT GstMpegTsAtscTVCT;

/**
 * GstMpegTsAtscTVCTSource:
 *
 * Source from a @GstMpegTsAtscTVCT
 */
struct _GstMpegTsAtscTVCTSource
{
  gunichar2 *short_name;
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
  gboolean  hide_guide;
  /* FIXME: */
  guint8    service_type;
  guint16   source_id;
  GPtrArray *descriptors;
};

/**
 * GstMpegTsAtscTVCT:
 *
 * Terrestrial Virtual Channel Table (A65)
 *
 */
struct _GstMpegTsAtscTVCT
{
  guint16   transport_stream_id;
  guint8    protocol_version;
  GPtrArray *sources;
  GPtrArray *descriptors;
};

GType gst_mpegts_atsc_tvct_get_type (void);
GType gst_mpegts_atsc_tvct_source_get_type (void);

const GstMpegTsAtscTVCT * gst_mpegts_section_get_atsc_tvct (GstMpegTsSection * section);

G_END_DECLS

#endif				/* GST_MPEGTS_SECTION_H */
