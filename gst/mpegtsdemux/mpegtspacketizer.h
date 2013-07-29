/*
 * mpegtspacketizer.h - 
 * Copyright (C) 2007 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifndef GST_MPEGTS_PACKETIZER_H
#define GST_MPEGTS_PACKETIZER_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <glib.h>

#include <gst/mpegts/mpegts.h>
#include "gstmpegdefs.h"

#define MPEGTS_NORMAL_PACKETSIZE  188
#define MPEGTS_M2TS_PACKETSIZE    192
#define MPEGTS_DVB_ASI_PACKETSIZE 204
#define MPEGTS_ATSC_PACKETSIZE    208

#define MPEGTS_MIN_PACKETSIZE MPEGTS_NORMAL_PACKETSIZE
#define MPEGTS_MAX_PACKETSIZE MPEGTS_ATSC_PACKETSIZE

#define MPEGTS_AFC_DISCONTINUITY_FLAG           0x80
#define MPEGTS_AFC_RANDOM_ACCES_FLAGS           0x40
#define MPEGTS_AFC_ELEMENTARY_STREAM_PRIORITY   0x20
#define MPEGTS_AFC_PCR_FLAG                     0x10
#define MPEGTS_AFC_OPCR_FLAG                    0x08
#define MPEGTS_AFC_SPLICING_POINT_FLAG          0x04
#define MPEGTS_AFC_TRANSPORT_PRIVATE_DATA_FLAG  0x02
#define MPEGTS_AFC_EXTENSION_FLAG               0x01

#define MAX_WINDOW 512

G_BEGIN_DECLS

#define GST_TYPE_MPEGTS_PACKETIZER \
  (mpegts_packetizer_get_type())
#define GST_MPEGTS_PACKETIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEGTS_PACKETIZER,MpegTSPacketizer2))
#define GST_MPEGTS_PACKETIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEGTS_PACKETIZER,MpegTSPacketizer2Class))
#define GST_IS_MPEGTS_PACKETIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEGTS_PACKETIZER))
#define GST_IS_MPEGTS_PACKETIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEGTS_PACKETIZER))

typedef struct _MpegTSPacketizer2 MpegTSPacketizer2;
typedef struct _MpegTSPacketizer2Class MpegTSPacketizer2Class;
typedef struct _MpegTSPacketizerPrivate MpegTSPacketizerPrivate;

typedef struct
{
  guint16 pid;
  guint   continuity_counter;

  /* Section data (always newly allocated) */
  guint8 *section_data;
  /* Current offset in section_data */
  guint16 section_offset;

  /* Values for pending section */
  /* table_id of the pending section_data */
  guint8  table_id;
  guint   section_length;
  guint8  version_number;
  guint16 subtable_extension;
  guint8  section_number;
  guint8  last_section_number;

  GSList *subtables;

  /* Upstream offset of the data contained in the section */
  guint64 offset;
} MpegTSPacketizerStream;

struct _MpegTSPacketizer2 {
  GObject     parent;

  GstAdapter *adapter;
  /* streams hashed by pid */
  /* FIXME : be more memory efficient (see how it's done in mpegtsbase) */
  MpegTSPacketizerStream **streams;
  gboolean    disposed;
  guint16     packet_size;

  /* current offset of the tip of the adapter */
  guint64  offset;
  gboolean empty;

  /* clock skew calculation */
  gboolean       calculate_skew;

  /* offset/bitrate calculator */
  gboolean       calculate_offset;

  MpegTSPacketizerPrivate *priv;
};

struct _MpegTSPacketizer2Class {
  GObjectClass object_class;
};

#define FLAGS_SCRAMBLED(f) (f & 0xc0)
#define FLAGS_HAS_AFC(f) (f & 0x20)
#define FLAGS_HAS_PAYLOAD(f) (f & 0x10)
#define FLAGS_CONTINUITY_COUNTER(f) (f & 0x0f)

typedef struct
{
  gint16  pid;
  guint8  payload_unit_start_indicator;
  guint8  scram_afc_cc;
  guint8 *payload;

  guint8 *data_start;
  guint8 *data_end;
  guint8 *data;

  guint8  afc_flags;
  guint64 pcr;
  guint64 offset;
} MpegTSPacketizerPacket;

typedef struct
{
  guint8 table_id;
  /* the spec says sub_table_extension is the fourth and fifth byte of a 
   * section when the section_syntax_indicator is set to a value of "1". If 
   * section_syntax_indicator is 0, sub_table_extension will be set to 0 */
  guint16  subtable_extension;
  guint8   version_number;
  guint8   last_section_number;
  /* table of bits, whether the section was seen or not.
   * Use MPEGTS_BIT_* macros to check */
  /* Size is 32, because there's a maximum of 256 (32*8) section_number */
  guint8   seen_section[32];
} MpegTSPacketizerStreamSubtable;

#define MPEGTS_BIT_SET(field, offs)    ((field)[(offs) >> 3] |=  (1 << ((offs) & 0x7)))
#define MPEGTS_BIT_UNSET(field, offs)  ((field)[(offs) >> 3] &= ~(1 << ((offs) & 0x7)))
#define MPEGTS_BIT_IS_SET(field, offs) ((field)[(offs) >> 3] &   (1 << ((offs) & 0x7)))

typedef enum {
  PACKET_BAD       = FALSE,
  PACKET_OK        = TRUE,
  PACKET_NEED_MORE
} MpegTSPacketizerPacketReturn;

G_GNUC_INTERNAL GType mpegts_packetizer_get_type(void);

G_GNUC_INTERNAL MpegTSPacketizer2 *mpegts_packetizer_new (void);
G_GNUC_INTERNAL void mpegts_packetizer_clear (MpegTSPacketizer2 *packetizer);
G_GNUC_INTERNAL void mpegts_packetizer_flush (MpegTSPacketizer2 *packetizer, gboolean hard);
G_GNUC_INTERNAL void mpegts_packetizer_push (MpegTSPacketizer2 *packetizer, GstBuffer *buffer);
G_GNUC_INTERNAL gboolean mpegts_packetizer_has_packets (MpegTSPacketizer2 *packetizer);
G_GNUC_INTERNAL MpegTSPacketizerPacketReturn mpegts_packetizer_next_packet (MpegTSPacketizer2 *packetizer,
  MpegTSPacketizerPacket *packet);
G_GNUC_INTERNAL MpegTSPacketizerPacketReturn
mpegts_packetizer_process_next_packet(MpegTSPacketizer2 * packetizer);
G_GNUC_INTERNAL void mpegts_packetizer_clear_packet (MpegTSPacketizer2 *packetizer,
				     MpegTSPacketizerPacket *packet);
G_GNUC_INTERNAL void mpegts_packetizer_remove_stream(MpegTSPacketizer2 *packetizer,
  gint16 pid);

G_GNUC_INTERNAL GstMpegTsSection *mpegts_packetizer_push_section (MpegTSPacketizer2 *packetzer,
								  MpegTSPacketizerPacket *packet, GList **remaining);

/* Only valid if calculate_offset is TRUE */
G_GNUC_INTERNAL guint mpegts_packetizer_get_seen_pcr (MpegTSPacketizer2 *packetizer);

G_GNUC_INTERNAL GstClockTime
mpegts_packetizer_offset_to_ts (MpegTSPacketizer2 * packetizer,
				guint64 offset, guint16 pcr_pid);
G_GNUC_INTERNAL guint64
mpegts_packetizer_ts_to_offset (MpegTSPacketizer2 * packetizer,
				GstClockTime ts, guint16 pcr_pid);
G_GNUC_INTERNAL GstClockTime
mpegts_packetizer_pts_to_ts (MpegTSPacketizer2 * packetizer,
			     GstClockTime pts, guint16 pcr_pid);
G_GNUC_INTERNAL void
mpegts_packetizer_set_reference_offset (MpegTSPacketizer2 * packetizer,
					guint64 refoffset);
G_END_DECLS

#endif /* GST_MPEGTS_PACKETIZER_H */
