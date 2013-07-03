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
  GST_MPEGTS_SECTION_TOT
} GstMpegTsSectionType;

/**
 * GstMpegTsSectionTableID:
 *
 * Values for a #GstMpegTsSection table_id
 *
 * These are the registered ITU H.222.0 | ISO/IEC 13818-1 table_id variants.
 *
 * see also #GstMpegTsSectionATSCTableID and #GstMpegTsSectionDVBTableID.
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

  /* 0x08 - 0x39 : ITU H.222.0 | ISO/IEC 13818-1 reserved */

  /* IEC 13818-6 (DSM-CC) */
  GST_MTS_TABLE_ID_DSM_CC_MULTIPROTO_ENCAPSULATED_DATA	= 0x3A,
  GST_MTS_TABLE_ID_DSM_CC_U_N_MESSAGES			= 0x3B,
  GST_MTS_TABLE_ID_DSM_CC_DOWNLOAD_DATA_MESSAGES	= 0x3C,
  GST_MTS_TABLE_ID_DSM_CC_STREAM_DESCRIPTORS		= 0x3D,
  GST_MTS_TABLE_ID_DSM_CC_PRIVATE_DATA			= 0x3E,
  GST_MTS_TABLE_ID_DSM_CC_ADDRESSABLE_SECTIONS		= 0x3F,

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
  guint8        table_id;

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
  /* destroy_parsed: function to clear cached_parsed */
  GDestroyNotify destroy_parsed;
  /* offset: offset of the section within the container stream */
  guint64       offset;
  /* short_section: TRUE if section_syntax_indicator == 0
   * FIXME : Maybe make public later on when allowing creation of
   * sections to that people can create private short sections ? */
  gboolean      short_section;
};


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


/* generic */

#define gst_mpegts_section_ref(section)   ((GstMpegTsSection*) gst_mini_object_ref (GST_MINI_OBJECT_CAST (section)))
#define gst_mpegts_section_unref(section) (gst_mini_object_unref (GST_MINI_OBJECT_CAST (section)))

GstMessage *gst_message_new_mpegts_section (GstObject *parent, GstMpegTsSection *section);

GstMpegTsSection *gst_message_parse_mpegts_section (GstMessage *message);

GstMpegTsSection *gst_mpegts_section_new (guint16 pid,
					   guint8 * data,
					   gsize data_size);

#endif				/* GST_MPEGTS_SECTION_H */
