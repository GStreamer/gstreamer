/*
 * tsdemux.c
 * Copyright (C) 2009 Zaheer Abbas Merali
 *               2010 Edward Hervey
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *  Author: Edward Hervey <bilboed@bilboed.com>, Collabora Ltd.
 *
 * Authors:
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
 *   Edward Hervey <edward.hervey@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "mpegtsbase.h"
#include "tsdemux.h"
#include "gstmpegdesc.h"
#include "gstmpegdefs.h"
#include "mpegtspacketizer.h"
#include "payload_parsers.h"
#include "pesparse.h"

/* 
 * tsdemux
 *
 * See TODO for explanations on improvements needed
 */

/* latency in mseconds */
#define TS_LATENCY 700

#define TABLE_ID_UNSET 0xFF

/* Size of the pendingbuffers array. */
#define TS_MAX_PENDING_BUFFERS	256

#define PCR_WRAP_SIZE_128KBPS (((gint64)1490)*(1024*1024))
/* small PCR for wrap detection */
#define PCR_SMALL 17775000
/* maximal PCR time */
#define PCR_MAX_VALUE (((((guint64)1)<<33) * 300) + 298)
#define PTS_DTS_MAX_VALUE (((guint64)1) << 33)

/* seek to SEEK_TIMESTAMP_OFFSET before the desired offset and search then
 * either accurately or for the next timestamp
 */
#define SEEK_TIMESTAMP_OFFSET (1000 * GST_MSECOND)

GST_DEBUG_CATEGORY_STATIC (ts_demux_debug);
#define GST_CAT_DEFAULT ts_demux_debug

#define ABSDIFF(a,b) (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))

static GQuark QUARK_TSDEMUX;
static GQuark QUARK_PID;
static GQuark QUARK_PCR;
static GQuark QUARK_OPCR;
static GQuark QUARK_PTS;
static GQuark QUARK_DTS;
static GQuark QUARK_OFFSET;

typedef enum
{
  PENDING_PACKET_EMPTY = 0,     /* No pending packet/buffer
                                 * Push incoming buffers to the array */
  PENDING_PACKET_HEADER,        /* PES header needs to be parsed
                                 * Push incoming buffers to the array */
  PENDING_PACKET_BUFFER,        /* Currently filling up output buffer
                                 * Push incoming buffers to the bufferlist */
  PENDING_PACKET_DISCONT        /* Discontinuity in incoming packets
                                 * Drop all incoming buffers */
} PendingPacketState;

typedef struct _TSDemuxStream TSDemuxStream;

struct _TSDemuxStream
{
  MpegTSBaseStream stream;

  GstPad *pad;

  /* the return of the latest push */
  GstFlowReturn flow_return;

  /* Output data */
  PendingPacketState state;
  /* Pending buffers array. */
  /* These buffers are stored in this array until the PES header (if needed)
   * is succesfully parsed. */
  GstBuffer *pendingbuffers[TS_MAX_PENDING_BUFFERS];
  guint8 nbpending;

  /* Current data to be pushed out */
  GstBufferList *current;
  GstBufferListIterator *currentit;
  GList *currentlist;

  /* Current PTS/DTS for this stream */
  GstClockTime pts;
  GstClockTime dts;
  /* Raw value of current PTS/DTS */
  guint64 raw_pts;
  guint64 raw_dts;
  /* Number of rollover seen for PTS/DTS (default:0) */
  guint nb_pts_rollover;
  guint nb_dts_rollover;
};

#define VIDEO_CAPS \
  GST_STATIC_CAPS (\
    "video/mpeg, " \
      "mpegversion = (int) { 1, 2, 4 }, " \
      "systemstream = (boolean) FALSE; " \
    "video/x-h264,stream-format=(string)byte-stream," \
      "alignment=(string)nal;" \
    "video/x-dirac;" \
    "video/x-wmv," \
      "wmvversion = (int) 3, " \
      "format = (fourcc) WVC1" \
  )

#define AUDIO_CAPS \
  GST_STATIC_CAPS ( \
    "audio/mpeg, " \
      "mpegversion = (int) 1;" \
    "audio/mpeg, " \
      "mpegversion = (int) 4, " \
      "stream-format = (string) adts; " \
    "audio/x-lpcm, " \
      "width = (int) { 16, 20, 24 }, " \
      "rate = (int) { 48000, 96000 }, " \
      "channels = (int) [ 1, 8 ], " \
      "dynamic_range = (int) [ 0, 255 ], " \
      "emphasis = (boolean) { FALSE, TRUE }, " \
      "mute = (boolean) { FALSE, TRUE }; " \
    "audio/x-ac3; audio/x-eac3;" \
    "audio/x-dts;" \
    "audio/x-private-ts-lpcm" \
  )

/* Can also use the subpicture pads for text subtitles? */
#define SUBPICTURE_CAPS \
    GST_STATIC_CAPS ("subpicture/x-pgs; video/x-dvd-subpicture")

static GstStaticPadTemplate video_template =
GST_STATIC_PAD_TEMPLATE ("video_%04x", GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    VIDEO_CAPS);

static GstStaticPadTemplate audio_template =
GST_STATIC_PAD_TEMPLATE ("audio_%04x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    AUDIO_CAPS);

static GstStaticPadTemplate subpicture_template =
GST_STATIC_PAD_TEMPLATE ("subpicture_%04x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    SUBPICTURE_CAPS);

static GstStaticPadTemplate private_template =
GST_STATIC_PAD_TEMPLATE ("private_%04x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

enum
{
  ARG_0,
  PROP_PROGRAM_NUMBER,
  PROP_EMIT_STATS,
  /* FILL ME */
};

/* Pad functions */
static const GstQueryType *gst_ts_demux_srcpad_query_types (GstPad * pad);
static gboolean gst_ts_demux_srcpad_query (GstPad * pad, GstQuery * query);


/* mpegtsbase methods */
static void
gst_ts_demux_program_started (MpegTSBase * base, MpegTSBaseProgram * program);
static void gst_ts_demux_reset (MpegTSBase * base);
static GstFlowReturn
gst_ts_demux_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    MpegTSPacketizerSection * section);
static void gst_ts_demux_flush (MpegTSBase * base);
static void
gst_ts_demux_stream_added (MpegTSBase * base, MpegTSBaseStream * stream,
    MpegTSBaseProgram * program);
static void
gst_ts_demux_stream_removed (MpegTSBase * base, MpegTSBaseStream * stream);
static GstFlowReturn gst_ts_demux_do_seek (MpegTSBase * base, GstEvent * event,
    guint16 pid);
static GstFlowReturn find_pcr_packet (MpegTSBase * base, guint64 offset,
    gint64 length, TSPcrOffset * pcroffset);
static GstFlowReturn find_timestamps (MpegTSBase * base, guint64 initoff,
    guint64 * offset);
static void gst_ts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ts_demux_finalize (GObject * object);
static GstFlowReturn
process_pcr (MpegTSBase * base, guint64 initoff, TSPcrOffset * pcroffset,
    guint numpcr, gboolean isinitial);
static void gst_ts_demux_flush_streams (GstTSDemux * tsdemux);
static GstFlowReturn
gst_ts_demux_push_pending_data (GstTSDemux * demux, TSDemuxStream * stream);

static gboolean push_event (MpegTSBase * base, GstEvent * event);
static void _extra_init (GType type);

GST_BOILERPLATE_FULL (GstTSDemux, gst_ts_demux, MpegTSBase,
    GST_TYPE_MPEGTS_BASE, _extra_init);

static void
_extra_init (GType type)
{
  QUARK_TSDEMUX = g_quark_from_string ("tsdemux");
  QUARK_PID = g_quark_from_string ("pid");
  QUARK_PCR = g_quark_from_string ("pcr");
  QUARK_OPCR = g_quark_from_string ("opcr");
  QUARK_PTS = g_quark_from_string ("pts");
  QUARK_DTS = g_quark_from_string ("dts");
  QUARK_OFFSET = g_quark_from_string ("offset");
}

static void
gst_ts_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &video_template);
  gst_element_class_add_static_pad_template (element_class,
      &audio_template);
  gst_element_class_add_static_pad_template (element_class,
      &subpicture_template);
  gst_element_class_add_static_pad_template (element_class,
      &private_template);

  gst_element_class_set_details_simple (element_class,
      "MPEG transport stream demuxer",
      "Codec/Demuxer",
      "Demuxes MPEG2 transport streams",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>\n"
      "Edward Hervey <edward.hervey@collabora.co.uk>");
}

static void
gst_ts_demux_class_init (GstTSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  MpegTSBaseClass *ts_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_ts_demux_set_property;
  gobject_class->get_property = gst_ts_demux_get_property;
  gobject_class->finalize = gst_ts_demux_finalize;

  g_object_class_install_property (gobject_class, PROP_PROGRAM_NUMBER,
      g_param_spec_int ("program-number", "Program number",
          "Program Number to demux for (-1 to ignore)", -1, G_MAXINT,
          -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EMIT_STATS,
      g_param_spec_boolean ("emit-stats", "Emit statistics",
          "Emit messages for every pcr/opcr/pts/dts", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  ts_class = GST_MPEGTS_BASE_CLASS (klass);
  ts_class->reset = GST_DEBUG_FUNCPTR (gst_ts_demux_reset);
  ts_class->push = GST_DEBUG_FUNCPTR (gst_ts_demux_push);
  ts_class->push_event = GST_DEBUG_FUNCPTR (push_event);
  ts_class->program_started = GST_DEBUG_FUNCPTR (gst_ts_demux_program_started);
  ts_class->stream_added = gst_ts_demux_stream_added;
  ts_class->stream_removed = gst_ts_demux_stream_removed;
  ts_class->find_timestamps = GST_DEBUG_FUNCPTR (find_timestamps);
  ts_class->seek = GST_DEBUG_FUNCPTR (gst_ts_demux_do_seek);
  ts_class->flush = GST_DEBUG_FUNCPTR (gst_ts_demux_flush);
}

static void
gst_ts_demux_init (GstTSDemux * demux, GstTSDemuxClass * klass)
{
  demux->need_newsegment = TRUE;
  demux->program_number = -1;
  demux->duration = GST_CLOCK_TIME_NONE;
  GST_MPEGTS_BASE (demux)->stream_size = sizeof (TSDemuxStream);
  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->first_pcr = (TSPcrOffset) {
  GST_CLOCK_TIME_NONE, 0, 0};
  demux->cur_pcr = (TSPcrOffset) {
  0};
  demux->last_pcr = (TSPcrOffset) {
  0};
}

static void
gst_ts_demux_reset (MpegTSBase * base)
{
  GstTSDemux *demux = (GstTSDemux *) base;

  if (demux->index) {
    g_array_free (demux->index, TRUE);
    demux->index = NULL;
  }
  demux->index_size = 0;
  demux->need_newsegment = TRUE;
  demux->program_number = -1;
  demux->duration = GST_CLOCK_TIME_NONE;
  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->first_pcr = (TSPcrOffset) {
  GST_CLOCK_TIME_NONE, 0, 0};
  demux->cur_pcr = (TSPcrOffset) {
  0};
  demux->last_pcr = (TSPcrOffset) {
  0};
}

static void
gst_ts_demux_finalize (GObject * object)
{
  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}



static void
gst_ts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTSDemux *demux = GST_TS_DEMUX (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBER:
      /* FIXME: do something if program is switched as opposed to set at
       * beginning */
      demux->program_number = g_value_get_int (value);
      break;
    case PROP_EMIT_STATS:
      demux->emit_statistics = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_ts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTSDemux *demux = GST_TS_DEMUX (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBER:
      g_value_set_int (value, demux->program_number);
      break;
    case PROP_EMIT_STATS:
      g_value_set_boolean (value, demux->emit_statistics);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static const GstQueryType *
gst_ts_demux_srcpad_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return query_types;
}

static gboolean
gst_ts_demux_srcpad_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstFormat format;
  GstTSDemux *demux;
  MpegTSBase *base;

  demux = GST_TS_DEMUX (gst_pad_get_parent (pad));
  base = GST_MPEGTS_BASE (demux);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      GST_DEBUG ("query duration");
      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        if (!gst_pad_peer_query (base->sinkpad, query))
          gst_query_set_duration (query, GST_FORMAT_TIME,
              demux->segment.duration);
      } else {
        GST_DEBUG_OBJECT (demux, "only query duration on TIME is supported");
        res = FALSE;
      }
      break;
    case GST_QUERY_SEEKING:
      GST_DEBUG ("query seeking");
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format == GST_FORMAT_TIME) {
        gboolean seekable = FALSE;

        if (gst_pad_peer_query (base->sinkpad, query))
          gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);

        /* If upstream is not seekable in TIME format we use
         * our own values here */
        if (!seekable)
          gst_query_set_seeking (query, GST_FORMAT_TIME,
              demux->parent.mode != BASE_MODE_PUSHING, 0,
              demux->segment.duration);
      } else {
        GST_DEBUG_OBJECT (demux, "only TIME is supported for query seeking");
        res = FALSE;
      }
      break;
    default:
      res = gst_pad_query_default (pad, query);
  }

  gst_object_unref (demux);
  return res;

}

static inline GstClockTime
calculate_gsttime (TSPcrOffset * start, guint64 pcr)
{

  GstClockTime time = start->gsttime;

  if (start->pcr > pcr)
    time += PCRTIME_TO_GSTTIME (PCR_MAX_VALUE - start->pcr) +
        PCRTIME_TO_GSTTIME (pcr);
  else
    time += PCRTIME_TO_GSTTIME (pcr - start->pcr);

  return time;
}

static GstFlowReturn
gst_ts_demux_parse_pes_header_pts (GstTSDemux * demux,
    MpegTSPacketizerPacket * packet, guint64 * time)
{
  PESHeader header;
  gint offset = 0;

  if (mpegts_parse_pes_header (packet->payload,
          packet->data_end - packet->payload, &header, &offset))
    return GST_FLOW_ERROR;

  *time = header.PTS;
  return GST_FLOW_OK;
}

/* performs a accurate/key_unit seek */
static GstFlowReturn
gst_ts_demux_perform_auxiliary_seek (MpegTSBase * base, GstClockTime seektime,
    TSPcrOffset * pcroffset, gint64 length, gint16 pid, GstSeekFlags flags,
    payload_parse_keyframe auxiliary_seek_fn)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  GstFlowReturn res = GST_FLOW_ERROR;
  gboolean done = FALSE;
  gboolean found_keyframe = FALSE, found_accurate = FALSE, need_more = TRUE;
  GstBuffer *buf;
  MpegTSPacketizerPacket packet;
  MpegTSPacketizerPacketReturn pret;
  gint64 offset = pcroffset->offset;
  gint64 scan_offset = MIN (length, 50 * MPEGTS_MAX_PACKETSIZE);
  guint32 state = 0xffffffff;
  TSPcrOffset key_pos = { 0 };

  GST_DEBUG ("auxiliary seek for %" GST_TIME_FORMAT " from offset: %"
      G_GINT64_FORMAT " in %" G_GINT64_FORMAT " bytes for PID: %d "
      "%s %s", GST_TIME_ARGS (seektime), pcroffset->offset, length, pid,
      (flags & GST_SEEK_FLAG_ACCURATE) ? "accurate" : "",
      (flags & GST_SEEK_FLAG_KEY_UNIT) ? "key_unit" : "");

  mpegts_packetizer_flush (base->packetizer);

  if (base->packetizer->packet_size == MPEGTS_M2TS_PACKETSIZE)
    offset -= 4;

  while (!done && scan_offset <= length) {
    res =
        gst_pad_pull_range (base->sinkpad, offset + scan_offset,
        50 * MPEGTS_MAX_PACKETSIZE, &buf);
    if (res != GST_FLOW_OK)
      goto beach;
    mpegts_packetizer_push (base->packetizer, buf);

    while ((!done)
        && ((pret =
                mpegts_packetizer_next_packet (base->packetizer,
                    &packet)) != PACKET_NEED_MORE)) {
      if (G_UNLIKELY (pret == PACKET_BAD))
        /* bad header, skip the packet */
        goto next;

      if (packet.payload_unit_start_indicator)
        GST_DEBUG ("found packet for PID: %d with pcr: %" GST_TIME_FORMAT
            " at offset: %" G_GINT64_FORMAT, packet.pid,
            GST_TIME_ARGS (packet.pcr), packet.offset);

      if (packet.payload != NULL && packet.pid == pid) {

        if (packet.payload_unit_start_indicator) {
          guint64 pts = 0;
          GstFlowReturn ok =
              gst_ts_demux_parse_pes_header_pts (demux, &packet, &pts);
          if (ok == GST_FLOW_OK) {
            GstClockTime time = calculate_gsttime (pcroffset, pts * 300);

            GST_DEBUG ("packet has PTS: %" GST_TIME_FORMAT,
                GST_TIME_ARGS (time));

            if (time <= seektime) {
              pcroffset->gsttime = time;
              pcroffset->pcr = packet.pcr;
              pcroffset->offset = packet.offset;
            } else
              found_accurate = TRUE;
          } else
            goto next;
          /* reset state for new packet */
          state = 0xffffffff;
          need_more = TRUE;
        }

        if (auxiliary_seek_fn) {
          if (need_more) {
            if (auxiliary_seek_fn (&state, &packet, &need_more)) {
              found_keyframe = TRUE;
              key_pos = *pcroffset;
              GST_DEBUG ("found keyframe: time: %" GST_TIME_FORMAT " pcr: %"
                  GST_TIME_FORMAT " offset %" G_GINT64_FORMAT,
                  GST_TIME_ARGS (pcroffset->gsttime),
                  GST_TIME_ARGS (pcroffset->pcr), pcroffset->offset);
            }
          }
        } else {
          /* if we don't have a payload parsing function
           * every frame is a keyframe */
          found_keyframe = TRUE;
        }
      }
      if (flags & GST_SEEK_FLAG_ACCURATE)
        done = found_accurate && found_keyframe;
      else
        done = found_keyframe;
      if (done)
        *pcroffset = key_pos;
    next:
      mpegts_packetizer_clear_packet (base->packetizer, &packet);
    }
    scan_offset += 50 * MPEGTS_MAX_PACKETSIZE;
  }

beach:
  if (done)
    res = GST_FLOW_OK;
  else if (GST_FLOW_OK == res)
    res = GST_FLOW_CUSTOM_ERROR_1;

  mpegts_packetizer_flush (base->packetizer);
  return res;
}

static gint
TSPcrOffset_find (gconstpointer a, gconstpointer b, gpointer user_data)
{

/*   GST_INFO ("a: %" GST_TIME_FORMAT " offset: %" G_GINT64_FORMAT, */
/*       GST_TIME_ARGS (((TSPcrOffset *) a)->gsttime), ((TSPcrOffset *) a)->offset); */
/*   GST_INFO ("b: %" GST_TIME_FORMAT " offset: %" G_GINT64_FORMAT, */
/*       GST_TIME_ARGS (((TSPcrOffset *) b)->gsttime), ((TSPcrOffset *) b)->offset); */

  if (((TSPcrOffset *) a)->gsttime < ((TSPcrOffset *) b)->gsttime)
    return -1;
  else if (((TSPcrOffset *) a)->gsttime > ((TSPcrOffset *) b)->gsttime)
    return 1;
  else
    return 0;
}

static GstFlowReturn
gst_ts_demux_perform_seek (MpegTSBase * base, GstSegment * segment, guint16 pid)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  GstFlowReturn res = GST_FLOW_ERROR;
  int max_loop_cnt, loop_cnt = 0;
  gint64 seekpos = 0;
  gint64 time_diff;
  GstClockTime seektime;
  TSPcrOffset seekpcroffset, pcr_start, pcr_stop, *tmp;

  max_loop_cnt = (segment->flags & GST_SEEK_FLAG_ACCURATE) ? 25 : 10;

  seektime =
      MAX (0,
      segment->last_stop - SEEK_TIMESTAMP_OFFSET) + demux->first_pcr.gsttime;
  seekpcroffset.gsttime = seektime;

  GST_DEBUG ("seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (seektime));

  gst_ts_demux_flush_streams (demux);

  if (G_UNLIKELY (!demux->index)) {
    GST_ERROR ("no index");
    goto done;
  }

  /* get the first index entry before the seek position */
  tmp = gst_util_array_binary_search (demux->index->data, demux->index_size,
      sizeof (*tmp), TSPcrOffset_find, GST_SEARCH_MODE_BEFORE, &seekpcroffset,
      NULL);

  if (G_UNLIKELY (!tmp)) {
    GST_ERROR ("value not found");
    goto done;
  }

  pcr_start = *tmp;
  pcr_stop = *(++tmp);

  if (G_UNLIKELY (!pcr_stop.offset)) {
    GST_ERROR ("invalid entry");
    goto done;
  }

  /* check if the last recorded pcr can be used */
  if (pcr_start.offset < demux->cur_pcr.offset
      && demux->cur_pcr.offset < pcr_stop.offset) {
    demux->cur_pcr.gsttime = calculate_gsttime (&pcr_start, demux->cur_pcr.pcr);
    if (demux->cur_pcr.gsttime < seekpcroffset.gsttime)
      pcr_start = demux->cur_pcr;
    else
      pcr_stop = demux->cur_pcr;
  }

  GST_DEBUG ("start %" GST_TIME_FORMAT " offset: %" G_GINT64_FORMAT,
      GST_TIME_ARGS (pcr_start.gsttime), pcr_start.offset);
  GST_DEBUG ("stop  %" GST_TIME_FORMAT " offset: %" G_GINT64_FORMAT,
      GST_TIME_ARGS (pcr_stop.gsttime), pcr_stop.offset);

  time_diff = seektime - pcr_start.gsttime;
  seekpcroffset = pcr_start;

  GST_DEBUG ("cur  %" GST_TIME_FORMAT " offset: %" G_GINT64_FORMAT
      " time diff: %" G_GINT64_FORMAT,
      GST_TIME_ARGS (demux->cur_pcr.gsttime), demux->cur_pcr.offset, time_diff);

  /* seek loop */
  while (loop_cnt++ < max_loop_cnt && (time_diff > SEEK_TIMESTAMP_OFFSET >> 1)
      && (pcr_stop.gsttime - pcr_start.gsttime > SEEK_TIMESTAMP_OFFSET)) {
    gint64 duration = pcr_stop.gsttime - pcr_start.gsttime;
    gint64 size = pcr_stop.offset - pcr_start.offset;

    if (loop_cnt & 1)
      seekpos = pcr_start.offset + (size >> 1);
    else
      seekpos =
          pcr_start.offset + size * ((double) (seektime -
              pcr_start.gsttime) / duration);

    /* look a litle bit behind */
    seekpos =
        MAX (pcr_start.offset + 188, seekpos - 55 * MPEGTS_MAX_PACKETSIZE);

    GST_DEBUG ("looking for time: %" GST_TIME_FORMAT " .. %" GST_TIME_FORMAT
        " .. %" GST_TIME_FORMAT,
        GST_TIME_ARGS (pcr_start.gsttime),
        GST_TIME_ARGS (seektime), GST_TIME_ARGS (pcr_stop.gsttime));
    GST_DEBUG ("looking in bytes: %" G_GINT64_FORMAT " .. %" G_GINT64_FORMAT
        " .. %" G_GINT64_FORMAT, pcr_start.offset, seekpos, pcr_stop.offset);

    res =
        find_pcr_packet (&demux->parent, seekpos, 4000 * MPEGTS_MAX_PACKETSIZE,
        &seekpcroffset);
    if (G_UNLIKELY (res == GST_FLOW_UNEXPECTED)) {
      seekpos =
          MAX ((gint64) pcr_start.offset,
          seekpos - 2000 * MPEGTS_MAX_PACKETSIZE) + 188;
      res =
          find_pcr_packet (&demux->parent, seekpos,
          8000 * MPEGTS_MAX_PACKETSIZE, &seekpcroffset);
    }
    if (G_UNLIKELY (res != GST_FLOW_OK)) {
      GST_WARNING ("seeking failed %s", gst_flow_get_name (res));
      goto done;
    }

    seekpcroffset.gsttime = calculate_gsttime (&pcr_start, seekpcroffset.pcr);

    /* validate */
    if (G_UNLIKELY ((seekpcroffset.gsttime < pcr_start.gsttime) ||
            (seekpcroffset.gsttime > pcr_stop.gsttime))) {
      GST_ERROR ("Unexpected timestamp found, seeking failed! %"
          GST_TIME_FORMAT, GST_TIME_ARGS (seekpcroffset.gsttime));
      res = GST_FLOW_ERROR;
      goto done;
    }

    if (seekpcroffset.gsttime > seektime) {
      pcr_stop = seekpcroffset;
    } else {
      pcr_start = seekpcroffset;
    }
    time_diff = seektime - pcr_start.gsttime;
    GST_DEBUG ("seeking: %" GST_TIME_FORMAT " found: %" GST_TIME_FORMAT
        " diff = %" G_GINT64_FORMAT, GST_TIME_ARGS (seektime),
        GST_TIME_ARGS (seekpcroffset.gsttime), time_diff);
  }

  GST_DEBUG ("seeking finished after %d loops", loop_cnt);

  /* use correct seek position for the auxiliary search */
  seektime += SEEK_TIMESTAMP_OFFSET;

  {
    payload_parse_keyframe keyframe_seek = NULL;
    MpegTSBaseProgram *program = demux->program;
    guint64 avg_bitrate, length;

    if (program->streams[pid]) {
      switch (program->streams[pid]->stream_type) {
        case ST_VIDEO_MPEG1:
        case ST_VIDEO_MPEG2:
          keyframe_seek = gst_tsdemux_has_mpeg2_keyframe;
          break;
        case ST_VIDEO_H264:
          keyframe_seek = gst_tsdemux_has_h264_keyframe;
          break;
        case ST_VIDEO_MPEG4:
        case ST_VIDEO_DIRAC:
          GST_WARNING ("no payload parser for stream 0x%04x type: 0x%02x", pid,
              program->streams[pid]->stream_type);
          break;
      }
    } else
      GST_WARNING ("no stream info for PID: 0x%04x", pid);

    avg_bitrate =
        (pcr_stop.offset -
        pcr_start.offset) * 1000 * GST_MSECOND / (pcr_stop.gsttime -
        pcr_start.gsttime);

    seekpcroffset = pcr_start;
    /* search in 2500ms for a keyframe */
    length =
        MIN (demux->last_pcr.offset - pcr_start.offset,
        (avg_bitrate * 25) / 10);
    res =
        gst_ts_demux_perform_auxiliary_seek (base, seektime, &seekpcroffset,
        length, pid, segment->flags, keyframe_seek);

    if (res == GST_FLOW_CUSTOM_ERROR_1) {
      GST_ERROR ("no keyframe found in %" G_GUINT64_FORMAT
          " bytes starting from %" G_GUINT64_FORMAT, length,
          seekpcroffset.offset);
      res = GST_FLOW_ERROR;
    }
    if (res != GST_FLOW_OK)
      goto done;
  }


  /* update seektime to the actual timestamp of the found keyframe */
  if (segment->flags & GST_SEEK_FLAG_KEY_UNIT)
    seektime = seekpcroffset.gsttime;

  seektime -= demux->first_pcr.gsttime;

  segment->last_stop = seektime;
  segment->time = seektime;

  /* we stop at the end */
  if (segment->stop == -1)
    segment->stop = demux->first_pcr.gsttime + segment->duration;

  demux->need_newsegment = TRUE;
  demux->parent.seek_offset = seekpcroffset.offset;
  GST_DEBUG ("seeked to postion:%" GST_TIME_FORMAT, GST_TIME_ARGS (seektime));
  res = GST_FLOW_OK;

done:
  return res;
}


static GstFlowReturn
gst_ts_demux_do_seek (MpegTSBase * base, GstEvent * event, guint16 pid)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  GstFlowReturn res = GST_FLOW_ERROR;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  GstSegment seeksegment;
  gboolean update;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    goto done;
  }

  GST_DEBUG ("seek event, rate: %f start: %" GST_TIME_FORMAT
      " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop));

  if (flags & (GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_SKIP)) {
    GST_WARNING ("seek flags 0x%x are not supported", (int) flags);
    goto done;
  }

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &demux->segment, sizeof (GstSegment));
  /* configure the segment with the seek variables */
  GST_DEBUG_OBJECT (demux, "configuring seek");
  GST_DEBUG ("seeksegment: start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " time: %" GST_TIME_FORMAT " accum: %" GST_TIME_FORMAT
      " last_stop: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (seeksegment.start), GST_TIME_ARGS (seeksegment.stop),
      GST_TIME_ARGS (seeksegment.time), GST_TIME_ARGS (seeksegment.accum),
      GST_TIME_ARGS (seeksegment.last_stop),
      GST_TIME_ARGS (seeksegment.duration));
  gst_segment_set_seek (&seeksegment, rate, format, flags, start_type, start,
      stop_type, stop, &update);
  GST_DEBUG ("seeksegment: start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " time: %" GST_TIME_FORMAT " accum: %" GST_TIME_FORMAT
      " last_stop: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (seeksegment.start), GST_TIME_ARGS (seeksegment.stop),
      GST_TIME_ARGS (seeksegment.time), GST_TIME_ARGS (seeksegment.accum),
      GST_TIME_ARGS (seeksegment.last_stop),
      GST_TIME_ARGS (seeksegment.duration));

  res = gst_ts_demux_perform_seek (base, &seeksegment, pid);
  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING ("seeking failed %s", gst_flow_get_name (res));
    goto done;
  }

  /* commit the new segment */
  memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));

  if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_segment_start (GST_OBJECT_CAST (demux),
            demux->segment.format, demux->segment.last_stop));
  }

done:
  return res;
}

static gboolean
gst_ts_demux_srcpad_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstTSDemux *demux = GST_TS_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "Got event %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = mpegts_base_handle_seek_event ((MpegTSBase *) demux, pad, event);
      if (!res)
        GST_WARNING ("seeking failed");
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
  }

  gst_object_unref (demux);
  return res;
}

static gboolean
push_event (MpegTSBase * base, GstEvent * event)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  GList *tmp;

  if (G_UNLIKELY (demux->program == NULL))
    return FALSE;

  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
    if (stream->pad) {
      gst_event_ref (event);
      gst_pad_push_event (stream->pad, event);
    }
  }

  return TRUE;
}

static GstFlowReturn
tsdemux_combine_flows (GstTSDemux * demux, TSDemuxStream * stream,
    GstFlowReturn ret)
{
  GList *tmp;

  /* Store the value */
  stream->flow_return = ret;

  /* any other error that is not-linked can be returned right away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* Only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    stream = (TSDemuxStream *) tmp->data;
    if (stream->pad) {
      ret = stream->flow_return;
      /* some other return value (must be SUCCESS but we can return
       * other values as well) */
      if (ret != GST_FLOW_NOT_LINKED)
        goto done;
    }
    /* if we get here, all other pads were unlinked and we return
     * NOT_LINKED then */
  }

done:
  return ret;
}

static GstPad *
create_pad_for_stream (MpegTSBase * base, MpegTSBaseStream * bstream,
    MpegTSBaseProgram * program)
{
  TSDemuxStream *stream = (TSDemuxStream *) bstream;
  gchar *name = NULL;
  GstCaps *caps = NULL;
  GstPadTemplate *template = NULL;
  guint8 *desc = NULL;
  GstPad *pad = NULL;


  GST_LOG ("Attempting to create pad for stream 0x%04x with stream_type %d",
      bstream->pid, bstream->stream_type);

  switch (bstream->stream_type) {
    case ST_VIDEO_MPEG1:
    case ST_VIDEO_MPEG2:
      GST_LOG ("mpeg video");
      template = gst_static_pad_template_get (&video_template);
      name = g_strdup_printf ("video_%04x", bstream->pid);
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT,
          bstream->stream_type == ST_VIDEO_MPEG1 ? 1 : 2, "systemstream",
          G_TYPE_BOOLEAN, FALSE, NULL);

      break;
    case ST_AUDIO_MPEG1:
    case ST_AUDIO_MPEG2:
      GST_LOG ("mpeg audio");
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          NULL);
      break;
    case ST_PRIVATE_DATA:
      GST_LOG ("private data");
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_AC3);
      if (desc) {
        GST_LOG ("ac3 audio");
        template = gst_static_pad_template_get (&audio_template);
        name = g_strdup_printf ("audio_%04x", bstream->pid);
        caps = gst_caps_new_simple ("audio/x-ac3", NULL);
        g_free (desc);
        break;
      }
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_ENHANCED_AC3);
      if (desc) {
        GST_LOG ("ac3 audio");
        template = gst_static_pad_template_get (&audio_template);
        name = g_strdup_printf ("audio_%04x", bstream->pid);
        caps = gst_caps_new_simple ("audio/x-eac3", NULL);
        g_free (desc);
        break;
      }
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_TELETEXT);
      if (desc) {
        GST_LOG ("teletext");
        template = gst_static_pad_template_get (&private_template);
        name = g_strdup_printf ("private_%04x", bstream->pid);
        caps = gst_caps_new_simple ("private/teletext", NULL);
        g_free (desc);
        break;
      }
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_SUBTITLING);
      if (desc) {
        GST_LOG ("subtitling");
        template = gst_static_pad_template_get (&private_template);
        name = g_strdup_printf ("private_%04x", bstream->pid);
        caps = gst_caps_new_simple ("subpicture/x-dvb", NULL);
        g_free (desc);
        break;
      }
      /* hack for itv hd (sid 10510, video pid 3401 */
      if (program->program_number == 10510 && bstream->pid == 3401) {
        template = gst_static_pad_template_get (&video_template);
        name = g_strdup_printf ("video_%04x", bstream->pid);
        caps = gst_caps_new_simple ("video/x-h264",
            "stream-format", G_TYPE_STRING, "byte-stream",
            "alignment", G_TYPE_STRING, "nal", NULL);
      }
      break;
    case ST_HDV_AUX_V:
      /* We don't expose those streams since they're only helper streams */
      /* template = gst_static_pad_template_get (&private_template); */
      /* name = g_strdup_printf ("private_%04x", bstream->pid); */
      /* caps = gst_caps_new_simple ("hdv/aux-v", NULL); */
      break;
    case ST_HDV_AUX_A:
      /* We don't expose those streams since they're only helper streams */
      /* template = gst_static_pad_template_get (&private_template); */
      /* name = g_strdup_printf ("private_%04x", bstream->pid); */
      /* caps = gst_caps_new_simple ("hdv/aux-a", NULL); */
      break;
    case ST_PRIVATE_SECTIONS:
    case ST_MHEG:
    case ST_DSMCC:
    case ST_DSMCC_A:
    case ST_DSMCC_B:
    case ST_DSMCC_C:
    case ST_DSMCC_D:
      MPEGTS_BIT_UNSET (base->is_pes, bstream->pid);
      break;
    case ST_AUDIO_AAC:         /* ADTS */
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "stream-format", G_TYPE_STRING, "adts", NULL);
      break;
    case ST_VIDEO_MPEG4:
      template = gst_static_pad_template_get (&video_template);
      name = g_strdup_printf ("video_%04x", bstream->pid);
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case ST_VIDEO_H264:
      template = gst_static_pad_template_get (&video_template);
      name = g_strdup_printf ("video_%04x", bstream->pid);
      caps = gst_caps_new_simple ("video/x-h264",
          "stream-format", G_TYPE_STRING, "byte-stream",
          "alignment", G_TYPE_STRING, "nal", NULL);
      break;
    case ST_VIDEO_DIRAC:
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_REGISTRATION);
      if (desc) {
        if (DESC_LENGTH (desc) >= 4) {
          if (DESC_REGISTRATION_format_identifier (desc) == 0x64726163) {
            GST_LOG ("dirac");
            /* dirac in hex */
            template = gst_static_pad_template_get (&video_template);
            name = g_strdup_printf ("video_%04x", bstream->pid);
            caps = gst_caps_new_simple ("video/x-dirac", NULL);
          }
        }
        g_free (desc);
      }
      break;
    case ST_PRIVATE_EA:        /* Try to detect a VC1 stream */
    {
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_REGISTRATION);
      if (desc) {
        if (DESC_LENGTH (desc) >= 4) {
          if (DESC_REGISTRATION_format_identifier (desc) == DRF_ID_VC1) {
            GST_WARNING ("0xea private stream type found but no descriptor "
                "for VC1. Assuming plain VC1.");
            template = gst_static_pad_template_get (&video_template);
            name = g_strdup_printf ("video_%04x", bstream->pid);
            caps = gst_caps_new_simple ("video/x-wmv",
                "wmvversion", G_TYPE_INT, 3,
                "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('W', 'V', 'C', '1'),
                NULL);
          }
        }
        g_free (desc);
      }
      break;
    }
    case ST_BD_AUDIO_AC3:
    {
      /* REGISTRATION DRF_ID_HDMV */
      desc = mpegts_get_descriptor_from_program (program, DESC_REGISTRATION);
      if (desc) {
        if (DESC_REGISTRATION_format_identifier (desc) == DRF_ID_HDMV) {
          template = gst_static_pad_template_get (&audio_template);
          name = g_strdup_printf ("audio_%04x", bstream->pid);
          caps = gst_caps_new_simple ("audio/x-eac3", NULL);
        }
        g_free (desc);
      }
      if (template)
        break;

      /* DVB_ENHANCED_AC3 */
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_ENHANCED_AC3);
      if (desc) {
        template = gst_static_pad_template_get (&audio_template);
        name = g_strdup_printf ("audio_%04x", bstream->pid);
        caps = gst_caps_new_simple ("audio/x-eac3", NULL);
        g_free (desc);
        break;
      }

      /* DVB_AC3 */
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_AC3);
      if (!desc)
        GST_WARNING ("AC3 stream type found but no corresponding "
            "descriptor to differentiate between AC3 and EAC3. "
            "Assuming plain AC3.");
      else
        g_free (desc);
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      break;
    }
    case ST_BD_AUDIO_EAC3:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/x-eac3", NULL);
      break;
    case ST_PS_AUDIO_DTS:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/x-dts", NULL);
      break;
    case ST_PS_AUDIO_LPCM:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/x-lpcm", NULL);
      break;
    case ST_BD_AUDIO_LPCM:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/x-private-ts-lpcm", NULL);
      break;
    case ST_PS_DVD_SUBPICTURE:
      template = gst_static_pad_template_get (&subpicture_template);
      name = g_strdup_printf ("subpicture_%04x", bstream->pid);
      caps = gst_caps_new_simple ("video/x-dvd-subpicture", NULL);
      break;
    case ST_BD_PGS_SUBPICTURE:
      template = gst_static_pad_template_get (&subpicture_template);
      name = g_strdup_printf ("subpicture_%04x", bstream->pid);
      caps = gst_caps_new_simple ("subpicture/x-pgs", NULL);
      break;
    default:
      GST_WARNING ("Non-media stream (stream_type:0x%x). Not creating pad",
          bstream->stream_type);
      break;
  }
  if (template && name && caps) {
    GST_LOG ("stream:%p creating pad with name %s and caps %s", stream, name,
        gst_caps_to_string (caps));
    pad = gst_pad_new_from_template (template, name);
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_caps (pad, caps);
    gst_pad_set_query_type_function (pad, gst_ts_demux_srcpad_query_types);
    gst_pad_set_query_function (pad, gst_ts_demux_srcpad_query);
    gst_pad_set_event_function (pad, gst_ts_demux_srcpad_event);
    gst_caps_unref (caps);
  }

  g_free (name);

  return pad;
}

static void
gst_ts_demux_stream_added (MpegTSBase * base, MpegTSBaseStream * bstream,
    MpegTSBaseProgram * program)
{
  TSDemuxStream *stream = (TSDemuxStream *) bstream;

  if (!stream->pad) {
    /* Create the pad */
    if (bstream->stream_type != 0xff)
      stream->pad = create_pad_for_stream (base, bstream, program);

    stream->pts = GST_CLOCK_TIME_NONE;
    stream->dts = GST_CLOCK_TIME_NONE;
    stream->raw_pts = 0;
    stream->raw_dts = 0;
    stream->nb_pts_rollover = 0;
    stream->nb_dts_rollover = 0;
  }
  stream->flow_return = GST_FLOW_OK;
}

static void
gst_ts_demux_stream_removed (MpegTSBase * base, MpegTSBaseStream * bstream)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);
  TSDemuxStream *stream = (TSDemuxStream *) bstream;

  if (stream->pad) {
    if (gst_pad_is_active (stream->pad)) {
      gboolean need_newsegment = demux->need_newsegment;

      /* We must not send the newsegment when flushing the pending data
         on the removed stream. We should only push it when the newly added
         stream finishes parsing its PTS */
      demux->need_newsegment = FALSE;

      /* Flush out all data */
      GST_DEBUG_OBJECT (stream->pad, "Flushing out pending data");
      gst_ts_demux_push_pending_data ((GstTSDemux *) base, stream);

      demux->need_newsegment = need_newsegment;

      GST_DEBUG_OBJECT (stream->pad, "Pushing out EOS");
      gst_pad_push_event (stream->pad, gst_event_new_eos ());
      GST_DEBUG_OBJECT (stream->pad, "Deactivating and removing pad");
      gst_pad_set_active (stream->pad, FALSE);
      gst_element_remove_pad (GST_ELEMENT_CAST (base), stream->pad);
    }
    stream->pad = NULL;
  }
  stream->flow_return = GST_FLOW_NOT_LINKED;
}

static void
activate_pad_for_stream (GstTSDemux * tsdemux, TSDemuxStream * stream)
{
  if (stream->pad) {
    GST_DEBUG_OBJECT (tsdemux, "Activating pad %s:%s for stream %p",
        GST_DEBUG_PAD_NAME (stream->pad), stream);
    gst_pad_set_active (stream->pad, TRUE);
    gst_element_add_pad ((GstElement *) tsdemux, stream->pad);
    GST_DEBUG_OBJECT (stream->pad, "done adding pad");
  } else
    GST_WARNING_OBJECT (tsdemux,
        "stream %p (pid 0x%04x, type:0x%03x) has no pad", stream,
        ((MpegTSBaseStream *) stream)->pid,
        ((MpegTSBaseStream *) stream)->stream_type);
}

static void
gst_ts_demux_stream_flush (TSDemuxStream * stream)
{
  gint i;

  stream->pts = GST_CLOCK_TIME_NONE;

  for (i = 0; i < stream->nbpending; i++)
    gst_buffer_unref (stream->pendingbuffers[i]);
  memset (stream->pendingbuffers, 0, TS_MAX_PENDING_BUFFERS);
  stream->nbpending = 0;

  stream->current = NULL;
}

static void
gst_ts_demux_flush_streams (GstTSDemux * demux)
{
  g_list_foreach (demux->program->stream_list,
      (GFunc) gst_ts_demux_stream_flush, NULL);
}

static void
gst_ts_demux_program_started (MpegTSBase * base, MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);

  GST_DEBUG ("Current program %d, new program %d",
      demux->program_number, program->program_number);

  if (demux->program_number == -1 ||
      demux->program_number == program->program_number) {
    GList *tmp;

    GST_LOG ("program %d started", program->program_number);
    demux->program_number = program->program_number;
    demux->program = program;

    /* Activate all stream pads, pads will already have been created */
    if (base->mode != BASE_MODE_SCANNING) {
      for (tmp = program->stream_list; tmp; tmp = tmp->next)
        activate_pad_for_stream (demux, (TSDemuxStream *) tmp->data);
      gst_element_no_more_pads ((GstElement *) demux);
    }

    /* Inform scanner we have got our program */
    demux->current_program_number = program->program_number;
    demux->need_newsegment = TRUE;
  }
}

static gboolean
process_section (MpegTSBase * base)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);
  gboolean based;
  gboolean done = FALSE;
  MpegTSPacketizerPacket packet;
  MpegTSPacketizerPacketReturn pret;

  while ((!done)
      && ((pret =
              mpegts_packetizer_next_packet (base->packetizer,
                  &packet)) != PACKET_NEED_MORE)) {
    if (G_UNLIKELY (pret == PACKET_BAD))
      /* bad header, skip the packet */
      goto next;

    /* base PSI data */
    if (packet.payload != NULL && mpegts_base_is_psi (base, &packet)) {
      MpegTSPacketizerSection section;

      based =
          mpegts_packetizer_push_section (base->packetizer, &packet, &section);
      if (G_UNLIKELY (!based))
        /* bad section data */
        goto next;

      if (G_LIKELY (section.complete)) {
        /* section complete */
        GST_DEBUG ("Section Complete");
        based = mpegts_base_handle_psi (base, &section);
        gst_buffer_unref (section.buffer);
        if (G_UNLIKELY (!based))
          /* bad PSI table */
          goto next;

      }

      if (demux->program != NULL) {
        GST_DEBUG ("Got Program");
        done = TRUE;
      }
    }
  next:
    mpegts_packetizer_clear_packet (base->packetizer, &packet);
  }
  return done;
}

static gboolean
process_pes (MpegTSBase * base, TSPcrOffset * pcroffset)
{
  gboolean based, done = FALSE;
  MpegTSPacketizerPacket packet;
  MpegTSPacketizerPacketReturn pret;
  GstTSDemux *demux = GST_TS_DEMUX (base);
  guint16 pcr_pid = 0;

  while ((!done)
      && ((pret =
              mpegts_packetizer_next_packet (base->packetizer,
                  &packet)) != PACKET_NEED_MORE)) {
    if (G_UNLIKELY (pret == PACKET_BAD))
      /* bad header, skip the packet */
      goto next;

    if (demux->program != NULL) {
      pcr_pid = demux->program->pcr_pid;
    }

    /* base PSI data */
    if (packet.payload != NULL && mpegts_base_is_psi (base, &packet)) {
      MpegTSPacketizerSection section;

      based =
          mpegts_packetizer_push_section (base->packetizer, &packet, &section);
      if (G_UNLIKELY (!based))
        /* bad section data */
        goto next;

      if (G_LIKELY (section.complete)) {
        /* section complete */
        GST_DEBUG ("Section Complete");
        based = mpegts_base_handle_psi (base, &section);
        gst_buffer_unref (section.buffer);
        if (G_UNLIKELY (!based))
          /* bad PSI table */
          goto next;

      }
    }
    if (packet.pid == pcr_pid && (packet.adaptation_field_control & 0x02)
        && (packet.afc_flags & MPEGTS_AFC_PCR_FLAG)) {
      GST_DEBUG ("PCR[0x%x]: %" G_GINT64_FORMAT, packet.pid, packet.pcr);
      pcroffset->pcr = packet.pcr;
      pcroffset->offset = packet.offset;
      done = TRUE;
    }
  next:
    mpegts_packetizer_clear_packet (base->packetizer, &packet);
  }
  return done;
}

static GstFlowReturn
find_pcr_packet (MpegTSBase * base, guint64 offset, gint64 length,
    TSPcrOffset * pcroffset)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstTSDemux *demux = GST_TS_DEMUX (base);
  MpegTSBaseProgram *program;
  GstBuffer *buf;
  gboolean done = FALSE;
  guint64 scan_offset = 0;

  GST_DEBUG ("Scanning for PCR between:%" G_GINT64_FORMAT
      " and the end:%" G_GINT64_FORMAT, offset, offset + length);

  /* Get the program */
  program = demux->program;
  if (G_UNLIKELY (program == NULL))
    return GST_FLOW_ERROR;

  mpegts_packetizer_flush (base->packetizer);
  if (offset >= 4 && base->packetizer->packet_size == MPEGTS_M2TS_PACKETSIZE)
    offset -= 4;

  while (!done && scan_offset < length) {
    ret =
        gst_pad_pull_range (base->sinkpad, offset + scan_offset,
        50 * MPEGTS_MAX_PACKETSIZE, &buf);
    if (ret != GST_FLOW_OK)
      goto beach;
    mpegts_packetizer_push (base->packetizer, buf);
    done = process_pes (base, pcroffset);
    scan_offset += 50 * MPEGTS_MAX_PACKETSIZE;
  }

  if (!done || scan_offset >= length) {
    GST_WARNING ("No PCR found!");
    ret = GST_FLOW_ERROR;
    goto beach;
  }

beach:
  mpegts_packetizer_flush (base->packetizer);
  return ret;
}

static gboolean
verify_timestamps (MpegTSBase * base, TSPcrOffset * first, TSPcrOffset * last)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);
  guint64 length = 4000 * MPEGTS_MAX_PACKETSIZE;
  guint64 offset = PCR_WRAP_SIZE_128KBPS;

  demux->index =
      g_array_sized_new (TRUE, TRUE, sizeof (*first),
      2 + 1 + ((last->offset - first->offset) / PCR_WRAP_SIZE_128KBPS));

  first->gsttime = PCRTIME_TO_GSTTIME (first->pcr);
  demux->index = g_array_append_val (demux->index, *first);
  demux->index_size++;
  demux->first_pcr = *first;
  demux->index_pcr = *first;
  GST_DEBUG ("first time: %" GST_TIME_FORMAT " pcr: %" GST_TIME_FORMAT
      " offset: %" G_GINT64_FORMAT
      " last  pcr: %" GST_TIME_FORMAT " offset: %" G_GINT64_FORMAT,
      GST_TIME_ARGS (first->gsttime),
      GST_TIME_ARGS (PCRTIME_TO_GSTTIME (first->pcr)), first->offset,
      GST_TIME_ARGS (PCRTIME_TO_GSTTIME (last->pcr)), last->offset);

  while (offset + length < last->offset) {
    TSPcrOffset half;
    GstFlowReturn ret;
    gint tries = 0;

  retry:
    ret = find_pcr_packet (base, offset, length, &half);
    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      GST_WARNING ("no pcr found, retrying");
      if (tries++ < 3) {
        offset += length;
        length *= 2;
        goto retry;
      }
      return FALSE;
    }

    half.gsttime = calculate_gsttime (first, half.pcr);

    GST_DEBUG ("add half time: %" GST_TIME_FORMAT " pcr: %" GST_TIME_FORMAT
        " offset: %" G_GINT64_FORMAT,
        GST_TIME_ARGS (half.gsttime),
        GST_TIME_ARGS (PCRTIME_TO_GSTTIME (half.pcr)), half.offset);
    demux->index = g_array_append_val (demux->index, half);
    demux->index_size++;

    length = 4000 * MPEGTS_MAX_PACKETSIZE;
    offset += PCR_WRAP_SIZE_128KBPS;
    *first = half;
  }

  last->gsttime = calculate_gsttime (first, last->pcr);

  GST_DEBUG ("add last time: %" GST_TIME_FORMAT " pcr: %" GST_TIME_FORMAT
      " offset: %" G_GINT64_FORMAT,
      GST_TIME_ARGS (last->gsttime),
      GST_TIME_ARGS (PCRTIME_TO_GSTTIME (last->pcr)), last->offset);

  demux->index = g_array_append_val (demux->index, *last);
  demux->index_size++;

  demux->last_pcr = *last;
  return TRUE;
}

static GstFlowReturn
find_timestamps (MpegTSBase * base, guint64 initoff, guint64 * offset)
{

  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf;
  gboolean done = FALSE;
  GstFormat format = GST_FORMAT_BYTES;
  gint64 total_bytes;
  guint64 scan_offset;
  guint i = 0;
  TSPcrOffset initial, final;
  GstTSDemux *demux = GST_TS_DEMUX (base);

  GST_DEBUG ("Scanning for timestamps");

  /* Flush what remained from before */
  mpegts_packetizer_clear (base->packetizer);

  /* Start scanning from know PAT offset */
  while (!done) {
    ret =
        gst_pad_pull_range (base->sinkpad, i * 50 * MPEGTS_MAX_PACKETSIZE,
        50 * MPEGTS_MAX_PACKETSIZE, &buf);
    if (ret != GST_FLOW_OK)
      goto beach;
    mpegts_packetizer_push (base->packetizer, buf);
    done = process_section (base);
    i++;
  }
  mpegts_packetizer_clear (base->packetizer);
  done = FALSE;
  i = 1;


  *offset = base->seek_offset;

  /* Search for the first PCRs */
  ret = process_pcr (base, base->first_pat_offset, &initial, 10, TRUE);

  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED) {
    GST_WARNING ("Problem getting initial PCRs");
    goto beach;
  }

  mpegts_packetizer_clear (base->packetizer);
  /* Remove current program so we ensure looking for a PAT when scanning the 
   * for the final PCR */
  gst_structure_free (base->pat);
  base->pat = NULL;
  mpegts_base_remove_program (base, demux->current_program_number);
  demux->program = NULL;

  /* Find end position */
  if (G_UNLIKELY (!gst_pad_query_peer_duration (base->sinkpad, &format,
              &total_bytes) || format != GST_FORMAT_BYTES)) {
    GST_WARNING_OBJECT (base, "Couldn't get upstream size in bytes");
    ret = GST_FLOW_ERROR;
    mpegts_packetizer_clear (base->packetizer);
    return ret;
  }
  GST_DEBUG ("Upstream is %" G_GINT64_FORMAT " bytes", total_bytes);


  /* Let's start scanning 4000 packets from the end */
  scan_offset = MAX (188, total_bytes - 4000 * MPEGTS_MAX_PACKETSIZE);

  GST_DEBUG ("Scanning for last sync point between:%" G_GINT64_FORMAT
      " and the end:%" G_GINT64_FORMAT, scan_offset, total_bytes);
  while ((!done) && (scan_offset < total_bytes)) {
    ret =
        gst_pad_pull_range (base->sinkpad,
        scan_offset, 50 * MPEGTS_MAX_PACKETSIZE, &buf);
    if (ret != GST_FLOW_OK)
      goto beach;

    mpegts_packetizer_push (base->packetizer, buf);
    done = process_section (base);
    scan_offset += 50 * MPEGTS_MAX_PACKETSIZE;
  }

  mpegts_packetizer_clear (base->packetizer);

  GST_DEBUG ("Searching PCR");
  ret =
      process_pcr (base, scan_offset - 50 * MPEGTS_MAX_PACKETSIZE, &final, 10,
      FALSE);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("Problem getting last PCRs");
    goto beach;
  }

  verify_timestamps (base, &initial, &final);

  gst_segment_set_duration (&demux->segment, GST_FORMAT_TIME,
      demux->last_pcr.gsttime - demux->first_pcr.gsttime);
  demux->duration = demux->last_pcr.gsttime - demux->first_pcr.gsttime;
  GST_DEBUG ("Done, duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->duration));

beach:

  mpegts_packetizer_clear (base->packetizer);
  /* Remove current program */
  if (base->pat) {
    gst_structure_free (base->pat);
    base->pat = NULL;
  }
  mpegts_base_remove_program (base, demux->current_program_number);
  demux->program = NULL;

  return ret;
}

static GstFlowReturn
process_pcr (MpegTSBase * base, guint64 initoff, TSPcrOffset * pcroffset,
    guint numpcr, gboolean isinitial)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);
  GstFlowReturn ret = GST_FLOW_OK;
  MpegTSBaseProgram *program;
  GstBuffer *buf;
  guint nbpcr, i = 0;
  guint32 pcrmask, pcrpattern;
  guint64 pcrs[50];
  guint64 pcroffs[50];
  GstByteReader br;

  GST_DEBUG ("initoff:%" G_GUINT64_FORMAT ", numpcr:%d, isinitial:%d",
      initoff, numpcr, isinitial);

  /* Get the program */
  program = demux->program;
  if (G_UNLIKELY (program == NULL))
    return GST_FLOW_ERROR;

  /* First find the first X PCR */
  nbpcr = 0;
  /* Mask/pattern is PID:PCR_PID, AFC&0x02 */
  /* sync_byte (0x47)                   : 8bits => 0xff
   * transport_error_indicator          : 1bit  ACTIVATE
   * payload_unit_start_indicator       : 1bit  IGNORE
   * transport_priority                 : 1bit  IGNORE
   * PID                                : 13bit => 0x9f 0xff
   * transport_scrambling_control       : 2bit
   * adaptation_field_control           : 2bit
   * continuity_counter                 : 4bit  => 0x30
   */
  pcrmask = 0xff9fff20;
  pcrpattern = 0x47000020 | ((program->pcr_pid & 0x1fff) << 8);

  for (i = 0; (i < 20) && (nbpcr < numpcr); i++) {
    guint offset, size;

    ret =
        gst_pad_pull_range (base->sinkpad,
        initoff + i * 500 * base->packetsize, 500 * base->packetsize, &buf);

    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto beach;

    gst_byte_reader_init_from_buffer (&br, buf);

    offset = 0;
    size = GST_BUFFER_SIZE (buf);

  resync:
    offset = gst_byte_reader_masked_scan_uint32 (&br, 0xff000000, 0x47000000,
        0, base->packetsize);

    if (offset == -1)
      continue;

    while ((nbpcr < numpcr) && (size >= base->packetsize)) {

      guint32 header = GST_READ_UINT32_BE (br.data + offset);

      if ((header >> 24) != 0x47)
        goto resync;

      if ((header & pcrmask) != pcrpattern) {
        /* Move offset forward by 1 packet */
        size -= base->packetsize;
        offset += base->packetsize;
        continue;
      }

      /* Potential PCR */
/*      GST_DEBUG ("offset %" G_GUINT64_FORMAT, GST_BUFFER_OFFSET (buf) + offset);
      GST_MEMDUMP ("something", GST_BUFFER_DATA (buf) + offset, 16);*/
      if ((*(br.data + offset + 5)) & MPEGTS_AFC_PCR_FLAG) {
        guint64 lpcr = mpegts_packetizer_compute_pcr (br.data + offset + 6);

        GST_INFO ("Found PCR %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT
            " at offset %" G_GUINT64_FORMAT, lpcr,
            GST_TIME_ARGS (PCRTIME_TO_GSTTIME (lpcr)),
            GST_BUFFER_OFFSET (buf) + offset);
        pcrs[nbpcr] = lpcr;
        pcroffs[nbpcr] = GST_BUFFER_OFFSET (buf) + offset;
        /* Safeguard against bogus PCR (by detecting if it's the same as the
         * previous one or wheter the difference with the previous one is
         * greater than 10mins */
        if (nbpcr > 1) {
          if (pcrs[nbpcr] == pcrs[nbpcr - 1]) {
            GST_WARNING ("Found same PCR at different offset");
          } else if (pcrs[nbpcr] < pcrs[nbpcr - 1]) {
            GST_WARNING ("Found PCR wraparound");
            nbpcr += 1;
          } else if ((pcrs[nbpcr] - pcrs[nbpcr - 1]) >
              (guint64) 10 * 60 * 27000000) {
            GST_WARNING ("PCR differs with previous PCR by more than 10 mins");
          } else
            nbpcr += 1;
        } else
          nbpcr += 1;
      }
      /* Move offset forward by 1 packet */
      size -= base->packetsize;
      offset += base->packetsize;
    }
  }

beach:
  GST_DEBUG ("Found %d PCR", nbpcr);
  if (nbpcr) {
    if (isinitial) {
      pcroffset->pcr = pcrs[0];
      pcroffset->offset = pcroffs[0];
    } else {
      pcroffset->pcr = pcrs[nbpcr - 1];
      pcroffset->offset = pcroffs[nbpcr - 1];
    }
    if (nbpcr > 1) {
      GST_DEBUG ("pcrdiff:%" GST_TIME_FORMAT " offsetdiff %" G_GUINT64_FORMAT,
          GST_TIME_ARGS (PCRTIME_TO_GSTTIME (pcrs[nbpcr - 1] - pcrs[0])),
          pcroffs[nbpcr - 1] - pcroffs[0]);
      GST_DEBUG ("Estimated bitrate %" G_GUINT64_FORMAT,
          gst_util_uint64_scale (GST_SECOND, pcroffs[nbpcr - 1] - pcroffs[0],
              PCRTIME_TO_GSTTIME (pcrs[nbpcr - 1] - pcrs[0])));
      GST_DEBUG ("Average PCR interval %" G_GUINT64_FORMAT,
          (pcroffs[nbpcr - 1] - pcroffs[0]) / nbpcr);
    }
  }
  /* Swallow any errors if it happened during the end scanning */
  if (!isinitial)
    ret = GST_FLOW_OK;
  return ret;
}




static inline void
gst_ts_demux_record_pcr (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 pcr, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  GST_LOG ("pid 0x%04x pcr:%" GST_TIME_FORMAT " at offset %"
      G_GUINT64_FORMAT, bs->pid,
      GST_TIME_ARGS (PCRTIME_TO_GSTTIME (pcr)), offset);

  if (G_LIKELY (bs->pid == demux->program->pcr_pid)) {
    demux->cur_pcr.gsttime = GST_CLOCK_TIME_NONE;
    demux->cur_pcr.offset = offset;
    demux->cur_pcr.pcr = pcr;
    /* set first_pcr in push mode */
    if (G_UNLIKELY (!demux->first_pcr.gsttime == GST_CLOCK_TIME_NONE)) {
      demux->first_pcr.gsttime = PCRTIME_TO_GSTTIME (pcr);
      demux->first_pcr.offset = offset;
      demux->first_pcr.pcr = pcr;
    }
  }

  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_id_empty_new (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset, QUARK_PCR, G_TYPE_UINT64, pcr,
        NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

static inline void
gst_ts_demux_record_opcr (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 opcr, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  GST_LOG ("pid 0x%04x opcr:%" GST_TIME_FORMAT " at offset %"
      G_GUINT64_FORMAT, bs->pid,
      GST_TIME_ARGS (PCRTIME_TO_GSTTIME (opcr)), offset);

  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_id_empty_new (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset,
        QUARK_OPCR, G_TYPE_UINT64, opcr, NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

static inline void
gst_ts_demux_record_pts (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 pts, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  GST_LOG ("pid 0x%04x pts:%" G_GUINT64_FORMAT " at offset %"
      G_GUINT64_FORMAT, bs->pid, pts, offset);

  if (G_UNLIKELY (GST_CLOCK_TIME_IS_VALID (stream->pts) &&
          ABSDIFF (stream->raw_pts, pts) > 900000)) {
    /* Detect rollover if diff > 10s */
    GST_LOG ("Detected rollover (previous:%" G_GUINT64_FORMAT " new:%"
        G_GUINT64_FORMAT ")", stream->raw_pts, pts);
    if (pts < stream->raw_pts) {
      /* Forward rollover */
      GST_LOG ("Forward rollover, incrementing nb_pts_rollover");
      stream->nb_pts_rollover++;
    } else {
      /* Reverse rollover */
      GST_LOG ("Reverse rollover, decrementing nb_pts_rollover");
      stream->nb_pts_rollover--;
    }
  }

  /* Compute PTS in GstClockTime */
  stream->raw_pts = pts;
  stream->pts =
      MPEGTIME_TO_GSTTIME (pts + stream->nb_pts_rollover * PTS_DTS_MAX_VALUE);

  GST_LOG ("pid 0x%04x Stored PTS %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT ")",
      bs->pid, stream->raw_pts, GST_TIME_ARGS (stream->pts));


  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_id_empty_new (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset, QUARK_PTS, G_TYPE_UINT64, pts,
        NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

static inline void
gst_ts_demux_record_dts (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 dts, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  GST_LOG ("pid 0x%04x dts:%" G_GUINT64_FORMAT " at offset %"
      G_GUINT64_FORMAT, bs->pid, dts, offset);

  if (G_UNLIKELY (GST_CLOCK_TIME_IS_VALID (stream->dts) &&
          ABSDIFF (stream->raw_dts, dts) > 900000)) {
    /* Detect rollover if diff > 10s */
    GST_LOG ("Detected rollover (previous:%" G_GUINT64_FORMAT " new:%"
        G_GUINT64_FORMAT ")", stream->raw_dts, dts);
    if (dts < stream->raw_dts) {
      /* Forward rollover */
      GST_LOG ("Forward rollover, incrementing nb_dts_rollover");
      stream->nb_dts_rollover++;
    } else {
      /* Reverse rollover */
      GST_LOG ("Reverse rollover, decrementing nb_dts_rollover");
      stream->nb_dts_rollover--;
    }
  }

  /* Compute DTS in GstClockTime */
  stream->raw_dts = dts;
  stream->dts =
      MPEGTIME_TO_GSTTIME (dts + stream->nb_dts_rollover * PTS_DTS_MAX_VALUE);

  GST_LOG ("pid 0x%04x Stored DTS %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT ")",
      bs->pid, stream->raw_dts, GST_TIME_ARGS (stream->dts));

  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_id_empty_new (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset, QUARK_DTS, G_TYPE_UINT64, dts,
        NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

static inline GstClockTime
calc_gsttime_from_pts (TSPcrOffset * start, guint64 pts)
{
  GstClockTime time = start->gsttime - PCRTIME_TO_GSTTIME (start->pcr);

  if (start->pcr > pts * 300)
    time += PCRTIME_TO_GSTTIME (PCR_MAX_VALUE) + MPEGTIME_TO_GSTTIME (pts);
  else
    time += MPEGTIME_TO_GSTTIME (pts);

  return time;
}

#if 0
static gint
TSPcrOffset_find_offset (gconstpointer a, gconstpointer b, gpointer user_data)
{
  if (((TSPcrOffset *) a)->offset < ((TSPcrOffset *) b)->offset)
    return -1;
  else if (((TSPcrOffset *) a)->offset > ((TSPcrOffset *) b)->offset)
    return 1;
  else
    return 0;
}
#endif

static GstFlowReturn
gst_ts_demux_parse_pes_header (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  PESHeader header;
  GstFlowReturn res = GST_FLOW_OK;
  gint offset = 0;
  guint8 *data;
  guint32 length;
  guint64 bufferoffset;
  PESParsingResult parseres;

  data = GST_BUFFER_DATA (stream->pendingbuffers[0]);
  length = GST_BUFFER_SIZE (stream->pendingbuffers[0]);
  bufferoffset = GST_BUFFER_OFFSET (stream->pendingbuffers[0]);

  GST_MEMDUMP ("Header buffer", data, MIN (length, 32));

  parseres = mpegts_parse_pes_header (data, length, &header, &offset);
  if (G_UNLIKELY (parseres == PES_PARSING_NEED_MORE))
    goto discont;
  if (G_UNLIKELY (parseres == PES_PARSING_BAD)) {
    GST_WARNING ("Error parsing PES header. pid: 0x%x stream_type: 0x%x",
        stream->stream.pid, stream->stream.stream_type);
    goto discont;
  }

  if (header.DTS != -1)
    gst_ts_demux_record_dts (demux, stream, header.DTS, bufferoffset);

  if (header.PTS != -1) {
    gst_ts_demux_record_pts (demux, stream, header.PTS, bufferoffset);

#if 0
    /* WTH IS THIS ??? */
    if (demux->index_pcr.offset + PCR_WRAP_SIZE_128KBPS + 1000 * 128 < offset
        || (demux->index_pcr.offset > offset)) {
      /* find next entry */
      TSPcrOffset *next;
      demux->index_pcr.offset = offset;
      next = gst_util_array_binary_search (demux->index->data,
          demux->index_size, sizeof (*next), TSPcrOffset_find_offset,
          GST_SEARCH_MODE_BEFORE, &demux->index_pcr, NULL);
      if (next) {
        GST_INFO ("new index_pcr %" GST_TIME_FORMAT " offset: %"
            G_GINT64_FORMAT, GST_TIME_ARGS (next->gsttime), next->offset);

        demux->index_pcr = *next;
      }
    }
    time = calc_gsttime_from_pts (&demux->index_pcr, pts);
#endif

    GST_DEBUG_OBJECT (base, "stream PTS %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stream->pts));

    /* safe default if insufficient upstream info */
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (base->in_gap) &&
            GST_CLOCK_TIME_IS_VALID (base->first_buf_ts) &&
            base->mode == BASE_MODE_PUSHING &&
            base->segment.format == GST_FORMAT_TIME)) {
      /* Find the earliest current PTS we're going to push */
      GstClockTime firstpts = GST_CLOCK_TIME_NONE;
      GList *tmp;

      for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
        TSDemuxStream *pstream = (TSDemuxStream *) tmp->data;
        if (!GST_CLOCK_TIME_IS_VALID (firstpts) || pstream->pts < firstpts)
          firstpts = pstream->pts;
      }

      base->in_gap = base->first_buf_ts - firstpts;
      GST_DEBUG_OBJECT (base, "upstream segment start %" GST_TIME_FORMAT
          ", first buffer timestamp: %" GST_TIME_FORMAT
          ", first PTS: %" GST_TIME_FORMAT
          ", interpolation gap: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (base->segment.start),
          GST_TIME_ARGS (base->first_buf_ts), GST_TIME_ARGS (firstpts),
          GST_TIME_ARGS (base->in_gap));
    }

    if (!GST_CLOCK_TIME_IS_VALID (base->in_gap))
      base->in_gap = 0;

    GST_BUFFER_TIMESTAMP (stream->pendingbuffers[0]) =
        stream->pts + base->in_gap;
  }

  /* Remove PES headers */
  GST_DEBUG ("Moving data forward  by %d bytes", header.header_size);
  GST_BUFFER_DATA (stream->pendingbuffers[0]) += header.header_size;
  GST_BUFFER_SIZE (stream->pendingbuffers[0]) -= header.header_size;

  /* FIXME : responsible for switching to PENDING_PACKET_BUFFER and
   * creating the bufferlist */
  if (1) {
    /* Append to the buffer list */
    if (G_UNLIKELY (stream->current == NULL)) {
      guint8 i;

      /* Create a new bufferlist */
      stream->current = gst_buffer_list_new ();
      stream->currentit = gst_buffer_list_iterate (stream->current);
      stream->currentlist = NULL;
      gst_buffer_list_iterator_add_group (stream->currentit);

      /* Push pending buffers into the list */
      for (i = stream->nbpending; i; i--)
        stream->currentlist =
            g_list_prepend (stream->currentlist, stream->pendingbuffers[i - 1]);
      memset (stream->pendingbuffers, 0, TS_MAX_PENDING_BUFFERS);
      stream->nbpending = 0;
    }
    stream->state = PENDING_PACKET_BUFFER;
  }

  return res;

discont:
  stream->state = PENDING_PACKET_DISCONT;
  return res;
}

 /* ONLY CALL THIS:
  * * WITH packet->payload != NULL
  * * WITH pending/current flushed out if beginning of new PES packet
  */
static inline void
gst_ts_demux_queue_data (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSPacketizerPacket * packet)
{
  GstBuffer *buf;

  GST_DEBUG ("state:%d", stream->state);

  buf = packet->buffer;
  /* HACK : Instead of creating a new buffer, we just modify the data/size
   * of the buffer to point to the payload */
  GST_BUFFER_DATA (buf) = packet->payload;
  GST_BUFFER_SIZE (buf) = packet->data_end - packet->payload;

  if (stream->state == PENDING_PACKET_EMPTY) {
    if (G_UNLIKELY (!packet->payload_unit_start_indicator)) {
      stream->state = PENDING_PACKET_DISCONT;
      GST_WARNING ("Didn't get the first packet of this PES");
    } else {
      GST_LOG ("EMPTY=>HEADER");
      stream->state = PENDING_PACKET_HEADER;
      if (stream->pad) {
        GST_DEBUG ("Setting pad caps on buffer %p", buf);
        gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));
      }
    }
  }

  if (stream->state == PENDING_PACKET_HEADER) {
    GST_LOG ("HEADER: appending data to array");
    /* Append to the array */
    stream->pendingbuffers[stream->nbpending++] = buf;

    /* parse the header */
    gst_ts_demux_parse_pes_header (demux, stream);
  } else if (stream->state == PENDING_PACKET_BUFFER) {
    GST_LOG ("BUFFER: appending data to bufferlist");
    stream->currentlist = g_list_prepend (stream->currentlist, buf);
  }


  return;
}

static void
calculate_and_push_newsegment (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  GstEvent *newsegmentevent;
  gint64 start = 0, stop = GST_CLOCK_TIME_NONE, position = 0;
  GstClockTime firstpts = GST_CLOCK_TIME_NONE;
  GList *tmp;

  GST_DEBUG ("Creating new newsegment for stream %p", stream);

  /* Outgoing newsegment values
   * start    : The first/start PTS
   * stop     : The last PTS (or -1)
   * position : The stream time corresponding to start
   *
   * Except for live mode with incoming GST_TIME_FORMAT newsegment where
   * it is the same values as that incoming newsegment (and we convert the
   * PTS to that remote clock).
   */

  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *pstream = (TSDemuxStream *) tmp->data;

    if (!GST_CLOCK_TIME_IS_VALID (firstpts) || pstream->pts < firstpts)
      firstpts = pstream->pts;
  }

  if (base->mode == BASE_MODE_PUSHING) {
    /* FIXME : We're just ignore the upstream format for the time being */
    /* FIXME : We should use base->segment.format and a upstream latency query
     * to decide if we need to use live values or not */
    GST_DEBUG ("push-based. base Segment start:%" GST_TIME_FORMAT " duration:%"
        GST_TIME_FORMAT ", stop:%" GST_TIME_FORMAT ", time:%" GST_TIME_FORMAT,
        GST_TIME_ARGS (base->segment.start),
        GST_TIME_ARGS (base->segment.duration),
        GST_TIME_ARGS (base->segment.stop), GST_TIME_ARGS (base->segment.time));
    GST_DEBUG ("push-based. demux Segment start:%" GST_TIME_FORMAT " duration:%"
        GST_TIME_FORMAT ", stop:%" GST_TIME_FORMAT ", time:%" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->segment.start),
        GST_TIME_ARGS (demux->segment.duration),
        GST_TIME_ARGS (demux->segment.stop),
        GST_TIME_ARGS (demux->segment.time));

    GST_DEBUG ("stream pts: %" GST_TIME_FORMAT " first pts: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stream->pts), GST_TIME_ARGS (firstpts));

    if (base->segment.format == GST_FORMAT_TIME) {
      start = base->segment.start;
      stop = base->segment.stop;
    }
    /* Shift the start depending on our position in the stream */
    start += firstpts + base->in_gap - base->first_buf_ts;
    position = start;
  } else {
    /* pull mode */
    GST_DEBUG ("pull-based. Segment start:%" GST_TIME_FORMAT " duration:%"
        GST_TIME_FORMAT ", time:%" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->segment.start),
        GST_TIME_ARGS (demux->segment.duration),
        GST_TIME_ARGS (demux->segment.time));

    GST_DEBUG ("firstpcr gsttime : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->first_pcr.gsttime));

    /* FIXME : This is not entirely correct. We should be using the PTS time
     * realm and not the PCR one. Doesn't matter *too* much if PTS/PCR values
     * aren't too far apart, but still.  */
    start = demux->first_pcr.gsttime + demux->segment.start;
    stop = demux->first_pcr.gsttime + demux->segment.duration;
    position = demux->segment.time;
  }

  GST_DEBUG ("new segment:   start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " time: %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop), GST_TIME_ARGS (position));
  newsegmentevent =
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, start, stop,
      position);

  push_event ((MpegTSBase *) demux, newsegmentevent);

  demux->need_newsegment = FALSE;
}

static GstFlowReturn
gst_ts_demux_push_pending_data (GstTSDemux * demux, TSDemuxStream * stream)
{
  GstFlowReturn res = GST_FLOW_OK;
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  GST_DEBUG ("stream:%p, pid:0x%04x stream_type:%d state:%d pad:%s:%s",
      stream, bs->pid, bs->stream_type, stream->state,
      GST_DEBUG_PAD_NAME (stream->pad));

  if (G_UNLIKELY (stream->current == NULL)) {
    GST_LOG ("stream->current == NULL");
    goto beach;
  }

  if (G_UNLIKELY (stream->state == PENDING_PACKET_EMPTY)) {
    GST_LOG ("EMPTY: returning");
    goto beach;
  }

  if (G_UNLIKELY (stream->state != PENDING_PACKET_BUFFER))
    goto beach;

  if (G_UNLIKELY (stream->pad == NULL)) {
    g_list_foreach (stream->currentlist, (GFunc) gst_buffer_unref, NULL);
    g_list_free (stream->currentlist);
    gst_buffer_list_iterator_free (stream->currentit);
    gst_buffer_list_unref (stream->current);
    goto beach;
  }

  if (G_UNLIKELY (demux->need_newsegment))
    calculate_and_push_newsegment (demux, stream);

  /* We have a confirmed buffer, let's push it out */
  GST_LOG ("Putting pending data into GstBufferList");
  stream->currentlist = g_list_reverse (stream->currentlist);
  gst_buffer_list_iterator_add_list (stream->currentit, stream->currentlist);
  gst_buffer_list_iterator_free (stream->currentit);

  GST_DEBUG_OBJECT (stream->pad,
      "Pushing buffer list with timestamp: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (gst_buffer_list_get
              (stream->current, 0, 0))));

  res = gst_pad_push_list (stream->pad, stream->current);
  GST_DEBUG_OBJECT (stream->pad, "Returned %s", gst_flow_get_name (res));
  res = tsdemux_combine_flows (demux, stream, res);
  GST_DEBUG_OBJECT (stream->pad, "combined %s", gst_flow_get_name (res));

beach:
  /* Reset everything */
  GST_LOG ("Resetting to EMPTY");
  stream->state = PENDING_PACKET_EMPTY;
  memset (stream->pendingbuffers, 0, TS_MAX_PENDING_BUFFERS);
  stream->nbpending = 0;
  stream->current = NULL;

  return res;
}

static GstFlowReturn
gst_ts_demux_handle_packet (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSPacketizerPacket * packet, MpegTSPacketizerSection * section)
{
  GstFlowReturn res = GST_FLOW_OK;

  GST_DEBUG ("buffer:%p, data:%p", GST_BUFFER_DATA (packet->buffer),
      packet->data);
  GST_LOG ("pid 0x%04x pusi:%d, afc:%d, cont:%d, payload:%p",
      packet->pid,
      packet->payload_unit_start_indicator,
      packet->adaptation_field_control,
      packet->continuity_counter, packet->payload);

  if (section) {
    GST_DEBUG ("section complete:%d, buffer size %d",
        section->complete, GST_BUFFER_SIZE (section->buffer));
    gst_buffer_unref (packet->buffer);
    return res;
  }

  if (G_UNLIKELY (packet->payload_unit_start_indicator))
    /* Flush previous data */
    res = gst_ts_demux_push_pending_data (demux, stream);

  if (packet->adaptation_field_control & 0x2) {
    if (packet->afc_flags & MPEGTS_AFC_PCR_FLAG)
      gst_ts_demux_record_pcr (demux, stream, packet->pcr,
          GST_BUFFER_OFFSET (packet->buffer));
    if (packet->afc_flags & MPEGTS_AFC_OPCR_FLAG)
      gst_ts_demux_record_opcr (demux, stream, packet->opcr,
          GST_BUFFER_OFFSET (packet->buffer));
  }

  if (packet->payload)
    gst_ts_demux_queue_data (demux, stream, packet);
  else
    gst_buffer_unref (packet->buffer);

  return res;
}

static void
gst_ts_demux_flush (MpegTSBase * base)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);

  demux->need_newsegment = TRUE;
  gst_ts_demux_flush_streams (demux);
}

static GstFlowReturn
gst_ts_demux_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    MpegTSPacketizerSection * section)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);
  TSDemuxStream *stream = NULL;
  GstFlowReturn res = GST_FLOW_OK;

  if (G_LIKELY (demux->program)) {
    stream = (TSDemuxStream *) demux->program->streams[packet->pid];

    if (stream) {
      res = gst_ts_demux_handle_packet (demux, stream, packet, section);
    } else if (packet->buffer)
      gst_buffer_unref (packet->buffer);
  } else {
    if (packet->buffer)
      gst_buffer_unref (packet->buffer);
  }
  return res;
}

gboolean
gst_ts_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ts_demux_debug, "tsdemux", 0,
      "MPEG transport stream demuxer");
  init_pes_parser ();

  return gst_element_register (plugin, "tsdemux",
      GST_RANK_SECONDARY, GST_TYPE_TS_DEMUX);
}
