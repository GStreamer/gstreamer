/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __MPEG_PARSE_H__
#define __MPEG_PARSE_H__

#include <gst/gst.h>
#include "gstmpegpacketize.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEG_PARSE \
  (gst_mpeg_parse_get_type())
#define GST_MPEG_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG_PARSE,GstMPEGParse))
#define GST_MPEG_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG_PARSE,GstMPEGParseClass))
#define GST_IS_MPEG_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG_PARSE))
#define GST_IS_MPEG_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG_PARSE))

#define GST_MPEG_PARSE_IS_MPEG2(parse) (GST_MPEG_PACKETIZE_IS_MPEG2 (GST_MPEG_PARSE (parse)->packetize))

#define CLOCK_BASE G_GINT64_CONSTANT (9)
#define CLOCK_FREQ CLOCK_BASE * 10000

#define MPEGTIME_TO_GSTTIME(time) (((time) * (GST_MSECOND/10)) / CLOCK_BASE)
#define GSTTIME_TO_MPEGTIME(time) (((time) * CLOCK_BASE) / (GST_MSECOND/10))

typedef struct _GstMPEGParse GstMPEGParse;
typedef struct _GstMPEGParseClass GstMPEGParseClass;

struct _GstMPEGParse
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstMPEGPacketize *packetize;

  /* Keep track of total rate using SCR and use hysteresis */
  guint64 first_scr;            /* Earliest SCR value for reference */
  guint64 first_scr_pos;        /* Byte position of reference SCR */
  guint64 last_scr;             /* Latest SCR value for reference */
  guint64 last_scr_pos;         /* Byte position of reference SCR */
  guint64 scr_rate;             /* Remember the last rate for hysteresis */

  /* Compute a rolling average for SCR interpolation (for MPEG1) */
  guint64 avg_bitrate_time;     /* Time total for local average bitrate */
  guint64 avg_bitrate_bytes;    /* Bytes total for local average bitrate */

  /* Pack header values */
  guint32 mux_rate;             /* Mux rate in bytes/sec derived from Pack 
                                   header */
  guint64 current_scr;          /* Current SCR from the stream */
  guint64 next_scr;             /* Expected next SCR */
  guint64 bytes_since_scr;      /* Bytes since current_scr */

  GstClockTime current_ts;      /* Current timestamp (i.e., SCR
                                   adjusted with the value of
                                   'adjust') */

  /* Timestamp handling. */
  gboolean do_adjust;           /* If true, adjust SCR values using
                                   the 'adjust' attribute. */
  gint64 adjust;                /* Value added to SCR values to
                                   produce buffer timestamps */
  gboolean pending_newsegment;	/* The element should send a
				   newsegment event as soon as it sees
				   the next SCR. This option is only
				   meaningful in do_adjust mode. */
  gint max_scr_gap;             /* The maximum allowed SCR gap without
                                   making a timestamp adjustment */
  GstSegment current_segment;   /* Segment currently being played. */

  GstIndex *index;
  gint index_id;

  guint64 byte_offset;
};

struct _GstMPEGParseClass
{
  GstElementClass parent_class;

  /* Process packets of different types */
  gboolean      (*parse_packhead) (GstMPEGParse * parse, GstBuffer * buffer);
  gboolean      (*parse_syshead)  (GstMPEGParse * parse, GstBuffer * buffer);
  GstFlowReturn (*parse_packet)   (GstMPEGParse * parse, GstBuffer * buffer);
  GstFlowReturn (*parse_pes)      (GstMPEGParse * parse, GstBuffer * buffer);

  /* Optional method to send out the data */
  GstFlowReturn (*send_buffer)    (GstMPEGParse * parse,
                                   GstBuffer * buffer, GstClockTime time);

  /* Process an event */
  gboolean      (*process_event)  (GstMPEGParse * parse,
                                   GstEvent * event);

  /* Send an event */
  gboolean      (*send_event)     (GstMPEGParse * parse, GstEvent *event);

  /* Adjust a timestamp */
  GstClockTime	(*adjust_ts)      (GstMPEGParse * parse, GstClockTime ts);

  /* Signals */
  void          (*reached_offset) (GstMPEGParse *parse,
                                   GstClockTime timeval);
};

GType gst_mpeg_parse_get_type (void);

gboolean gst_mpeg_parse_plugin_init (GstPlugin * plugin);

const GstFormat *gst_mpeg_parse_get_src_formats (GstPad * pad);

gboolean gst_mpeg_parse_handle_src_event (GstPad * pad, GstEvent * event);

const GstQueryType *gst_mpeg_parse_get_src_query_types (GstPad * pad);
gboolean gst_mpeg_parse_handle_src_query (GstPad * pad, GstQuery * query);

G_END_DECLS

#endif                          /* __MPEG_PARSE_H__ */
