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
#include <gst/tag/tag.h>

#include "mpegtsbase.h"
#include "tsdemux.h"
#include "gstmpegdesc.h"
#include "gstmpegdefs.h"
#include "mpegtspacketizer.h"
#include "pesparse.h"

/*
 * tsdemux
 *
 * See TODO for explanations on improvements needed
 */

/* latency in mseconds */
#define TS_LATENCY 700

#define TABLE_ID_UNSET 0xFF

#define PCR_WRAP_SIZE_128KBPS (((gint64)1490)*(1024*1024))
/* small PCR for wrap detection */
#define PCR_SMALL 17775000
/* maximal PCR time */
#define PCR_MAX_VALUE (((((guint64)1)<<33) * 300) + 298)
#define PTS_DTS_MAX_VALUE (((guint64)1) << 33)

/* Seeking/Scanning related variables */

/* seek to SEEK_TIMESTAMP_OFFSET before the desired offset and search then
 * either accurately or for the next timestamp
 */
#define SEEK_TIMESTAMP_OFFSET (500 * GST_MSECOND)

#define SEGMENT_FORMAT "[format:%s, rate:%f, start:%"			\
  GST_TIME_FORMAT", stop:%"GST_TIME_FORMAT", time:%"GST_TIME_FORMAT	\
  ", base:%"GST_TIME_FORMAT", position:%"GST_TIME_FORMAT		\
  ", duration:%"GST_TIME_FORMAT"]"

#define SEGMENT_ARGS(a) gst_format_get_name((a).format), (a).rate,	\
    GST_TIME_ARGS((a).start), GST_TIME_ARGS((a).stop),			\
    GST_TIME_ARGS((a).time), GST_TIME_ARGS((a).base),			\
    GST_TIME_ARGS((a).position), GST_TIME_ARGS((a).duration)


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
  /* Whether the pad was added or not */
  gboolean active;

  /* the return of the latest push */
  GstFlowReturn flow_return;

  /* Output data */
  PendingPacketState state;

  /* Data to push (allocated) */
  guint8 *data;

  /* Size of data to push (if known) */
  guint expected_size;

  /* Size of currently queued data */
  guint current_size;
  guint allocated_size;

  /* Current PTS/DTS for this stream */
  GstClockTime pts;
  GstClockTime dts;
  /* Raw value of current PTS/DTS */
  guint64 raw_pts;
  guint64 raw_dts;
  /* PTS/DTS with rollover fixed */
  guint64 fixed_pts;
  guint64 fixed_dts;
  /* Number of rollover seen for PTS/DTS (default:0) */
  guint nb_pts_rollover;
  guint nb_dts_rollover;

  /* Whether this stream needs to send a newsegment */
  gboolean need_newsegment;

  GstTagList *taglist;
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
      "format = (string) WVC1" \
  )

#define AUDIO_CAPS \
  GST_STATIC_CAPS ( \
    "audio/mpeg, " \
      "mpegversion = (int) 1;" \
    "audio/mpeg, " \
      "mpegversion = (int) 2, " \
      "stream-format = (string) adts; " \
    "audio/mpeg, " \
      "mpegversion = (int) 4, " \
      "stream-format = (string) loas; " \
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
    GST_STATIC_CAPS ("subpicture/x-pgs; subpicture/x-dvd")

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


/* mpegtsbase methods */
static void
gst_ts_demux_program_started (MpegTSBase * base, MpegTSBaseProgram * program);
static void
gst_ts_demux_program_stopped (MpegTSBase * base, MpegTSBaseProgram * program);
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
static GstFlowReturn gst_ts_demux_do_seek (MpegTSBase * base, GstEvent * event);
static void gst_ts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ts_demux_flush_streams (GstTSDemux * tsdemux);
static GstFlowReturn
gst_ts_demux_push_pending_data (GstTSDemux * demux, TSDemuxStream * stream);
static void gst_ts_demux_stream_flush (TSDemuxStream * stream);

static gboolean push_event (MpegTSBase * base, GstEvent * event);

static void
_extra_init (void)
{
  QUARK_TSDEMUX = g_quark_from_string ("tsdemux");
  QUARK_PID = g_quark_from_string ("pid");
  QUARK_PCR = g_quark_from_string ("pcr");
  QUARK_OPCR = g_quark_from_string ("opcr");
  QUARK_PTS = g_quark_from_string ("pts");
  QUARK_DTS = g_quark_from_string ("dts");
  QUARK_OFFSET = g_quark_from_string ("offset");
}

#define gst_ts_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstTSDemux, gst_ts_demux, GST_TYPE_MPEGTS_BASE,
    _extra_init ());

static void
gst_ts_demux_class_init (GstTSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  MpegTSBaseClass *ts_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_ts_demux_set_property;
  gobject_class->get_property = gst_ts_demux_get_property;

  g_object_class_install_property (gobject_class, PROP_PROGRAM_NUMBER,
      g_param_spec_int ("program-number", "Program number",
          "Program Number to demux for (-1 to ignore)", -1, G_MAXINT,
          -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EMIT_STATS,
      g_param_spec_boolean ("emit-stats", "Emit statistics",
          "Emit messages for every pcr/opcr/pts/dts", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subpicture_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&private_template));

  gst_element_class_set_static_metadata (element_class,
      "MPEG transport stream demuxer",
      "Codec/Demuxer",
      "Demuxes MPEG2 transport streams",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>\n"
      "Edward Hervey <edward.hervey@collabora.co.uk>");

  ts_class = GST_MPEGTS_BASE_CLASS (klass);
  ts_class->reset = GST_DEBUG_FUNCPTR (gst_ts_demux_reset);
  ts_class->push = GST_DEBUG_FUNCPTR (gst_ts_demux_push);
  ts_class->push_event = GST_DEBUG_FUNCPTR (push_event);
  ts_class->program_started = GST_DEBUG_FUNCPTR (gst_ts_demux_program_started);
  ts_class->program_stopped = GST_DEBUG_FUNCPTR (gst_ts_demux_program_stopped);
  ts_class->stream_added = gst_ts_demux_stream_added;
  ts_class->stream_removed = gst_ts_demux_stream_removed;
  ts_class->seek = GST_DEBUG_FUNCPTR (gst_ts_demux_do_seek);
  ts_class->flush = GST_DEBUG_FUNCPTR (gst_ts_demux_flush);
}

static void
gst_ts_demux_reset (MpegTSBase * base)
{
  GstTSDemux *demux = (GstTSDemux *) base;

  demux->program_number = -1;
  demux->calculate_update_segment = FALSE;

  gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
  if (demux->segment_event) {
    gst_event_unref (demux->segment_event);
    demux->segment_event = NULL;
  }

  if (demux->update_segment) {
    gst_event_unref (demux->update_segment);
    demux->update_segment = NULL;
  }
}

static void
gst_ts_demux_init (GstTSDemux * demux)
{
  GST_MPEGTS_BASE (demux)->stream_size = sizeof (TSDemuxStream);

  gst_ts_demux_reset ((MpegTSBase *) demux);
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

static gboolean
gst_ts_demux_srcpad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstFormat format;
  GstTSDemux *demux;
  MpegTSBase *base;

  demux = GST_TS_DEMUX (parent);
  base = GST_MPEGTS_BASE (demux);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GST_DEBUG ("query duration");
      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        if (!gst_pad_peer_query (base->sinkpad, query)) {
          gint64 val;

          format = GST_FORMAT_BYTES;
          if (!gst_pad_peer_query_duration (base->sinkpad, format, &val))
            res = FALSE;
          else {
            GstClockTime dur =
                mpegts_packetizer_offset_to_ts (base->packetizer, val,
                demux->program->pcr_pid);
            if (GST_CLOCK_TIME_IS_VALID (dur))
              gst_query_set_duration (query, GST_FORMAT_TIME, dur);
            else
              res = FALSE;
          }
        }
      } else {
        GST_DEBUG_OBJECT (demux, "only query duration on TIME is supported");
        res = FALSE;
      }
      break;
    }
    case GST_QUERY_LATENCY:
    {
      GST_DEBUG ("query latency");
      res = gst_pad_peer_query (base->sinkpad, query);
      if (res && base->upstream_live) {
        GstClockTime min_lat, max_lat;
        gboolean live;

        /* According to H.222.0
           Annex D.0.3 (System Time Clock recovery in the decoder)
           and D.0.2 (Audio and video presentation synchronization)

           We can end up with an interval of up to 700ms between valid
           PCR/SCR. We therefore allow a latency of 700ms for that.
         */
        gst_query_parse_latency (query, &live, &min_lat, &max_lat);
        if (min_lat != -1)
          min_lat += 700 * GST_MSECOND;
        if (max_lat != -1)
          max_lat += 700 * GST_MSECOND;
        gst_query_set_latency (query, live, min_lat, max_lat);
      }
      break;
    }
    case GST_QUERY_SEEKING:
    {
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
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
  }

  return res;

}

static GstFlowReturn
gst_ts_demux_do_seek (MpegTSBase * base, GstEvent * event)
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
  guint64 start_offset;

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
  if (demux->segment_event) {
    gst_event_unref (demux->segment_event);
    demux->segment_event = NULL;
  }
  /* configure the segment with the seek variables */
  GST_DEBUG_OBJECT (demux, "configuring seek");
  GST_DEBUG ("seeksegment before set_seek " SEGMENT_FORMAT,
      SEGMENT_ARGS (seeksegment));

  gst_segment_do_seek (&seeksegment, rate, format, flags, start_type, start,
      stop_type, stop, &update);

  GST_DEBUG ("seeksegment after set_seek " SEGMENT_FORMAT,
      SEGMENT_ARGS (seeksegment));

  /* Convert start/stop to offset */
  start_offset =
      mpegts_packetizer_ts_to_offset (base->packetizer, MAX (0,
          start - SEEK_TIMESTAMP_OFFSET), demux->program->pcr_pid);

  if (G_UNLIKELY (start_offset == -1)) {
    GST_WARNING ("Couldn't convert start position to an offset");
    goto done;
  }

  /* record offset */
  base->seek_offset = start_offset;
  res = GST_FLOW_OK;

  /* commit the new segment */
  memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));

  if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_segment_start (GST_OBJECT_CAST (demux),
            demux->segment.format, demux->segment.stop));
  }

done:
  return res;
}

static gboolean
gst_ts_demux_srcpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  GstTSDemux *demux = GST_TS_DEMUX (parent);

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
      res = gst_pad_event_default (pad, parent, event);
  }

  return res;
}

static gboolean
push_event (MpegTSBase * base, GstEvent * event)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  GList *tmp;

  if (G_UNLIKELY (demux->program == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
    if (stream->pad) {
      gst_event_ref (event);
      gst_pad_push_event (stream->pad, event);
    }
  }

  gst_event_unref (event);

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

static void
gst_ts_demux_create_tags (TSDemuxStream * stream)
{
  guint8 *desc = NULL;
  int i;

  desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
      DESC_ISO_639_LANGUAGE);
  if (desc) {
    if (!stream->taglist)
      stream->taglist = gst_tag_list_new_empty ();

    for (i = 0; i < DESC_ISO_639_LANGUAGE_codes_n (desc); i++) {
      const gchar *lc;
      gchar lang_code[4];
      gchar *language_n;

      language_n = (gchar *)
          DESC_ISO_639_LANGUAGE_language_code_nth (desc, i);

      /* Language codes should be 3 character long, we allow
       * a bit more flexibility by allowing 2 characters. */
      if (!language_n[0] || !language_n[1])
        continue;

      GST_LOG ("Add language code for stream: %s", language_n);

      lang_code[0] = language_n[0];
      lang_code[1] = language_n[1];
      lang_code[2] = language_n[2];
      lang_code[3] = 0;

      /* descriptor contains ISO 639-2 code, we want the ISO 639-1 code */
      lc = gst_tag_get_language_code (lang_code);
      gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_LANGUAGE_CODE, (lc) ? lc : lang_code, NULL);
    }

    g_free (desc);
  }
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

  gst_ts_demux_create_tags (stream);

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
      desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_AC3);
      if (desc) {
        GST_LOG ("ac3 audio");
        template = gst_static_pad_template_get (&audio_template);
        name = g_strdup_printf ("audio_%04x", bstream->pid);
        caps = gst_caps_new_empty_simple ("audio/x-ac3");
        g_free (desc);
        break;
      }

      desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_ENHANCED_AC3);
      if (desc) {
        GST_LOG ("ac3 audio");
        template = gst_static_pad_template_get (&audio_template);
        name = g_strdup_printf ("audio_%04x", bstream->pid);
        caps = gst_caps_new_empty_simple ("audio/x-eac3");
        g_free (desc);
        break;
      }
      desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_TELETEXT);
      if (desc) {
        GST_LOG ("teletext");
        template = gst_static_pad_template_get (&private_template);
        name = g_strdup_printf ("private_%04x", bstream->pid);
        caps = gst_caps_new_empty_simple ("private/teletext");
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
        caps = gst_caps_new_empty_simple ("subpicture/x-dvb");
        g_free (desc);
        break;
      }

      desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_REGISTRATION);
      if (desc) {
        switch (DESC_REGISTRATION_format_identifier (desc)) {
          case DRF_ID_DTS1:
          case DRF_ID_DTS2:
          case DRF_ID_DTS3:
            /* SMPTE registered DTS */
            GST_LOG ("subtitling");
            template = gst_static_pad_template_get (&private_template);
            name = g_strdup_printf ("private_%04x", bstream->pid);
            caps = gst_caps_new_empty_simple ("audio/x-dts");
            break;
        }
        g_free (desc);
      }
      if (template)
        break;

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
    case ST_AUDIO_AAC_ADTS:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 2,
          "stream-format", G_TYPE_STRING, "adts", NULL);
      break;
    case ST_AUDIO_AAC_LATM:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "stream-format", G_TYPE_STRING, "loas", NULL);
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
            caps = gst_caps_new_empty_simple ("video/x-dirac");
          }
        }
        g_free (desc);
      }
      break;
    case ST_PRIVATE_EA:        /* Try to detect a VC1 stream */
    {
      gboolean is_vc1 = FALSE;
      desc =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_REGISTRATION);
      if (desc) {
        if (DESC_LENGTH (desc) >= 4) {
          if (DESC_REGISTRATION_format_identifier (desc) == DRF_ID_VC1) {
            is_vc1 = TRUE;
          }
        }
        g_free (desc);
      }
      if (!is_vc1) {
        GST_WARNING ("0xea private stream type found but no descriptor "
            "for VC1. Assuming plain VC1.");
      }

      template = gst_static_pad_template_get (&video_template);
      name = g_strdup_printf ("video_%04x", bstream->pid);
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, "format", G_TYPE_STRING, "WVC1", NULL);

      break;
    }
    case ST_BD_AUDIO_AC3:
    {
      /* REGISTRATION DRF_ID_HDMV */
      desc = mpegts_get_descriptor_from_program (program, DESC_REGISTRATION);
      if (desc) {
        if (DESC_REGISTRATION_format_identifier (desc) == DRF_ID_HDMV) {
          guint8 *ac3_desc;

          /* ATSC ac3 audio descriptor */
          ac3_desc =
              mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
              DESC_AC3_AUDIO_STREAM);
          if (ac3_desc && DESC_AC_AUDIO_STREAM_bsid (ac3_desc) != 16) {
            GST_LOG ("ac3 audio");
            template = gst_static_pad_template_get (&audio_template);
            name = g_strdup_printf ("audio_%04x", bstream->pid);
            caps = gst_caps_new_empty_simple ("audio/x-ac3");

            g_free (ac3_desc);
          } else {
            template = gst_static_pad_template_get (&audio_template);
            name = g_strdup_printf ("audio_%04x", bstream->pid);
            caps = gst_caps_new_empty_simple ("audio/x-eac3");
          }

        }

        g_free (desc);
      }
      if (template)
        break;


      /* DVB_ENHANCED_AC3 */
      desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          DESC_DVB_ENHANCED_AC3);
      if (desc) {
        template = gst_static_pad_template_get (&audio_template);
        name = g_strdup_printf ("audio_%04x", bstream->pid);
        caps = gst_caps_new_empty_simple ("audio/x-eac3");
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
      caps = gst_caps_new_empty_simple ("audio/x-ac3");
      break;
    }
    case ST_BD_AUDIO_EAC3:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_empty_simple ("audio/x-eac3");
      break;
    case ST_PS_AUDIO_DTS:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_empty_simple ("audio/x-dts");
      break;
    case ST_PS_AUDIO_LPCM:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_empty_simple ("audio/x-lpcm");
      break;
    case ST_BD_AUDIO_LPCM:
      template = gst_static_pad_template_get (&audio_template);
      name = g_strdup_printf ("audio_%04x", bstream->pid);
      caps = gst_caps_new_empty_simple ("audio/x-private-ts-lpcm");
      break;
    case ST_PS_DVD_SUBPICTURE:
      template = gst_static_pad_template_get (&subpicture_template);
      name = g_strdup_printf ("subpicture_%04x", bstream->pid);
      caps = gst_caps_new_empty_simple ("subpicture/x-dvd");
      break;
    case ST_BD_PGS_SUBPICTURE:
      template = gst_static_pad_template_get (&subpicture_template);
      name = g_strdup_printf ("subpicture_%04x", bstream->pid);
      caps = gst_caps_new_empty_simple ("subpicture/x-pgs");
      break;
    default:
      GST_WARNING ("Non-media stream (stream_type:0x%x). Not creating pad",
          bstream->stream_type);
      break;
  }
  if (template && name && caps) {
    gchar *stream_id;

    GST_LOG ("stream:%p creating pad with name %s and caps %s", stream, name,
        gst_caps_to_string (caps));
    pad = gst_pad_new_from_template (template, name);
    gst_pad_set_active (pad, TRUE);
    gst_pad_use_fixed_caps (pad);
    stream_id =
        gst_pad_create_stream_id_printf (pad, GST_ELEMENT_CAST (base), "%08x",
        bstream->pid);
    gst_pad_push_event (pad, gst_event_new_stream_start (stream_id));
    g_free (stream_id);
    gst_pad_set_caps (pad, caps);
    gst_pad_set_query_function (pad, gst_ts_demux_srcpad_query);
    gst_pad_set_event_function (pad, gst_ts_demux_srcpad_event);
  }

  if (name)
    g_free (name);
  if (template)
    gst_object_unref (template);
  if (caps)
    gst_caps_unref (caps);

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
    stream->active = FALSE;

    stream->need_newsegment = TRUE;
    stream->pts = GST_CLOCK_TIME_NONE;
    stream->dts = GST_CLOCK_TIME_NONE;
    stream->raw_pts = 0;
    stream->raw_dts = 0;
    stream->fixed_pts = 0;
    stream->fixed_dts = 0;
    stream->nb_pts_rollover = 0;
    stream->nb_dts_rollover = 0;
  }
  stream->flow_return = GST_FLOW_OK;
}

static void
gst_ts_demux_stream_removed (MpegTSBase * base, MpegTSBaseStream * bstream)
{
  TSDemuxStream *stream = (TSDemuxStream *) bstream;

  if (stream->pad) {
    if (stream->active && gst_pad_is_active (stream->pad)) {
      /* Flush out all data */
      GST_DEBUG_OBJECT (stream->pad, "Flushing out pending data");
      gst_ts_demux_push_pending_data ((GstTSDemux *) base, stream);

      GST_DEBUG_OBJECT (stream->pad, "Pushing out EOS");
      gst_pad_push_event (stream->pad, gst_event_new_eos ());
      GST_DEBUG_OBJECT (stream->pad, "Deactivating and removing pad");
      gst_pad_set_active (stream->pad, FALSE);
      gst_element_remove_pad (GST_ELEMENT_CAST (base), stream->pad);
      stream->active = FALSE;
    }
    stream->pad = NULL;
  }
  gst_ts_demux_stream_flush (stream);
  stream->flow_return = GST_FLOW_NOT_LINKED;
}

static void
activate_pad_for_stream (GstTSDemux * tsdemux, TSDemuxStream * stream)
{
  GList *tmp;
  gboolean alldone = TRUE;

  if (stream->pad) {
    GST_DEBUG_OBJECT (tsdemux, "Activating pad %s:%s for stream %p",
        GST_DEBUG_PAD_NAME (stream->pad), stream);
    gst_element_add_pad ((GstElement *) tsdemux, stream->pad);
    stream->active = TRUE;
    GST_DEBUG_OBJECT (stream->pad, "done adding pad");

    /* Check if all pads were activated, and if so emit no-more-pads */
    for (tmp = tsdemux->program->stream_list; tmp; tmp = tmp->next) {
      stream = (TSDemuxStream *) tmp->data;
      if (stream->pad && !stream->active)
        alldone = FALSE;
    }
    if (alldone) {
      GST_DEBUG_OBJECT (tsdemux, "All pads were activated, emit no-more-pads");
      gst_element_no_more_pads ((GstElement *) tsdemux);
    }
  } else
    GST_WARNING_OBJECT (tsdemux,
        "stream %p (pid 0x%04x, type:0x%03x) has no pad", stream,
        ((MpegTSBaseStream *) stream)->pid,
        ((MpegTSBaseStream *) stream)->stream_type);
}

static void
gst_ts_demux_stream_flush (TSDemuxStream * stream)
{
  stream->pts = GST_CLOCK_TIME_NONE;

  GST_DEBUG ("flushing stream %p", stream);

  if (stream->data)
    g_free (stream->data);
  stream->data = NULL;
  stream->state = PENDING_PACKET_EMPTY;
  stream->expected_size = 0;
  stream->allocated_size = 0;
  stream->current_size = 0;
  stream->need_newsegment = TRUE;
  stream->pts = GST_CLOCK_TIME_NONE;
  stream->dts = GST_CLOCK_TIME_NONE;
  stream->raw_pts = 0;
  stream->raw_dts = 0;
  stream->fixed_pts = 0;
  stream->fixed_dts = 0;
  stream->nb_pts_rollover = 0;
  stream->nb_dts_rollover = 0;
  if (stream->flow_return == GST_FLOW_FLUSHING) {
    stream->flow_return = GST_FLOW_OK;
  }
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

    GST_LOG ("program %d started", program->program_number);
    demux->program_number = program->program_number;
    demux->program = program;

    /* If this is not the initial program, we need to calculate
     * an update newsegment */
    demux->calculate_update_segment = !program->initial_program;

    /* FIXME : When do we emit no_more_pads ? */

    /* Inform scanner we have got our program */
    demux->current_program_number = program->program_number;
  }
}

static void
gst_ts_demux_program_stopped (MpegTSBase * base, MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);

  if (demux->program == program) {
    demux->program = NULL;
    demux->program_number = -1;
  }
}


static inline void
gst_ts_demux_record_pts (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 pts, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  if (pts == -1) {
    stream->pts = GST_CLOCK_TIME_NONE;
    return;
  }

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
  stream->fixed_pts = pts + stream->nb_pts_rollover * PTS_DTS_MAX_VALUE;
  stream->pts = MPEGTIME_TO_GSTTIME (stream->fixed_pts);

  GST_LOG ("pid 0x%04x Stored PTS %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT ")",
      bs->pid, stream->raw_pts, GST_TIME_ARGS (stream->pts));


  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_new_id_empty (QUARK_TSDEMUX);
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

  if (dts == -1) {
    stream->dts = GST_CLOCK_TIME_NONE;
    return;
  }

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
  stream->fixed_dts = dts + stream->nb_dts_rollover * PTS_DTS_MAX_VALUE;
  stream->dts = MPEGTIME_TO_GSTTIME (stream->fixed_dts);

  GST_LOG ("pid 0x%04x Stored DTS %" G_GUINT64_FORMAT " (%" GST_TIME_FORMAT ")",
      bs->pid, stream->raw_dts, GST_TIME_ARGS (stream->dts));

  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_new_id_empty (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset, QUARK_DTS, G_TYPE_UINT64, dts,
        NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

static void
gst_ts_demux_parse_pes_header (GstTSDemux * demux, TSDemuxStream * stream,
    guint8 * data, guint32 length, guint64 bufferoffset)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  PESHeader header;
  gint offset = 0;
  PESParsingResult parseres;

  GST_MEMDUMP ("Header buffer", data, MIN (length, 32));

  parseres = mpegts_parse_pes_header (data, length, &header, &offset);
  if (G_UNLIKELY (parseres == PES_PARSING_NEED_MORE))
    goto discont;
  if (G_UNLIKELY (parseres == PES_PARSING_BAD)) {
    GST_WARNING ("Error parsing PES header. pid: 0x%x stream_type: 0x%x",
        stream->stream.pid, stream->stream.stream_type);
    goto discont;
  }

  gst_ts_demux_record_dts (demux, stream, header.DTS, bufferoffset);
  gst_ts_demux_record_pts (demux, stream, header.PTS, bufferoffset);

  GST_DEBUG_OBJECT (base,
      "stream PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->pts), GST_TIME_ARGS (stream->dts));

  /* Remove PES headers */
  GST_DEBUG ("Moving data forward by %d bytes (packet_size:%d, have:%d)",
      header.header_size, header.packet_length, length);
  stream->expected_size = header.packet_length;
  if (stream->expected_size) {
    if (G_LIKELY (stream->expected_size > header.header_size)) {
      stream->expected_size -= header.header_size;
    } else {
      /* next packet will have to complete this one */
      GST_ERROR ("invalid header and packet size combination");
      stream->expected_size = 0;
    }
  }
  data += header.header_size;
  length -= header.header_size;

  /* Create the output buffer */
  if (stream->expected_size)
    stream->allocated_size = stream->expected_size;
  else
    stream->allocated_size = 8192;
  g_assert (stream->data == NULL);
  stream->data = g_malloc (stream->allocated_size);
  memcpy (stream->data, data, length);
  stream->current_size = length;

  stream->state = PENDING_PACKET_BUFFER;

  return;

discont:
  stream->state = PENDING_PACKET_DISCONT;
  return;
}

 /* ONLY CALL THIS:
  * * WITH packet->payload != NULL
  * * WITH pending/current flushed out if beginning of new PES packet
  */
static inline void
gst_ts_demux_queue_data (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSPacketizerPacket * packet)
{
  guint8 *data;
  guint size;

  GST_DEBUG ("state:%d", stream->state);

  size = packet->data_end - packet->payload;
  data = packet->payload;

  if (stream->state == PENDING_PACKET_EMPTY) {
    if (G_UNLIKELY (!packet->payload_unit_start_indicator)) {
      stream->state = PENDING_PACKET_DISCONT;
      GST_WARNING ("Didn't get the first packet of this PES");
    } else {
      GST_LOG ("EMPTY=>HEADER");
      stream->state = PENDING_PACKET_HEADER;
    }
  }

  switch (stream->state) {
    case PENDING_PACKET_HEADER:
    {
      GST_LOG ("HEADER: Parsing PES header");

      /* parse the header */
      gst_ts_demux_parse_pes_header (demux, stream, data, size, packet->offset);
      break;
    }
    case PENDING_PACKET_BUFFER:
    {
      GST_LOG ("BUFFER: appending data");
      if (G_UNLIKELY (stream->current_size + size > stream->allocated_size)) {
        GST_LOG ("resizing buffer");
        stream->allocated_size = stream->allocated_size * 2;
        stream->data = g_realloc (stream->data, stream->allocated_size);
      }
      memcpy (stream->data + stream->current_size, data, size);
      stream->current_size += size;
      break;
    }
    case PENDING_PACKET_DISCONT:
    {
      GST_LOG ("DISCONT: not storing/pushing");
      if (G_UNLIKELY (stream->data)) {
        g_free (stream->data);
        stream->data = NULL;
      }
      break;
    }
    default:
      break;
  }

  return;
}

static void
calculate_and_push_newsegment (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  GstClockTime lowest_pts = GST_CLOCK_TIME_NONE;
  GstClockTime firstts = 0;
  GList *tmp;

  GST_DEBUG ("Creating new newsegment for stream %p", stream);

  /* 0) If we don't have a time segment yet try to recover segment info from
   *    base when it's in time otherwise just initialize segment with
   *    defaults.
   *    It will happen only if it's first program or after flushes. */
  if (demux->segment.format == GST_FORMAT_UNDEFINED) {
    if (base->segment.format == GST_FORMAT_TIME) {
      demux->segment = base->segment;
      /* We can shortcut and create the segment event directly */
      demux->segment_event = gst_event_new_segment (&demux->segment);
    } else {
      gst_segment_init (&demux->segment, GST_FORMAT_TIME);
    }
  }

  /* 1) If we need to calculate an update newsegment, do it
   * 2) If we need to calculate a new newsegment, do it
   * 3) If an update_segment is valid, push it
   * 4) If a newsegment is valid, push it */

  /* Speedup : if we don't need to calculate anything, go straight to pushing */
  if (!demux->calculate_update_segment && demux->segment_event)
    goto push_new_segment;

  /* Calculate the 'new_start' value, used for both updates and newsegment */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *pstream = (TSDemuxStream *) tmp->data;

    if (GST_CLOCK_TIME_IS_VALID (pstream->pts)) {
      if (!GST_CLOCK_TIME_IS_VALID (lowest_pts) || pstream->pts < lowest_pts)
        lowest_pts = pstream->pts;
    }
    if (GST_CLOCK_TIME_IS_VALID (pstream->dts)) {
      if (!GST_CLOCK_TIME_IS_VALID (lowest_pts) || pstream->dts < lowest_pts)
        lowest_pts = pstream->dts;
    }
  }
  if (GST_CLOCK_TIME_IS_VALID (lowest_pts))
    firstts =
        mpegts_packetizer_pts_to_ts (base->packetizer, lowest_pts,
        demux->program->pcr_pid);
  GST_DEBUG ("lowest_pts %" G_GUINT64_FORMAT " => clocktime %" GST_TIME_FORMAT,
      lowest_pts, GST_TIME_ARGS (firstts));

  if (demux->calculate_update_segment) {
    GST_DEBUG ("Calculating update segment");
    /* If we have a valid segment, create an update of that */
    if (demux->segment.format == GST_FORMAT_TIME) {
      GstSegment update_segment;
      GST_DEBUG ("Re-using segment " SEGMENT_FORMAT,
          SEGMENT_ARGS (demux->segment));
      gst_segment_copy_into (&demux->segment, &update_segment);
      update_segment.stop = firstts;
      demux->update_segment = gst_event_new_segment (&update_segment);
    }
    demux->calculate_update_segment = FALSE;
  }

  if (!demux->segment_event) {
    GstSegment new_segment;

    GST_DEBUG ("Calculating actual segment");

    gst_segment_copy_into (&demux->segment, &new_segment);
    if (new_segment.format != GST_FORMAT_TIME) {
      /* Start from the first ts/pts */
      new_segment.start = firstts;
      new_segment.stop = GST_CLOCK_TIME_NONE;
      new_segment.position = firstts;
    }

    demux->segment_event = gst_event_new_segment (&new_segment);
  }

push_new_segment:
  if (demux->update_segment) {
    GST_DEBUG_OBJECT (stream->pad, "Pushing update segment");
    gst_event_ref (demux->update_segment);
    gst_pad_push_event (stream->pad, demux->update_segment);
  }

  if (demux->segment_event) {
    GST_DEBUG_OBJECT (stream->pad, "Pushing newsegment event");
    gst_event_ref (demux->segment_event);
    gst_pad_push_event (stream->pad, demux->segment_event);
  }

  /* Push pending tags */
  if (stream->taglist) {
    GST_DEBUG_OBJECT (stream->pad, "Sending tags %" GST_PTR_FORMAT,
        stream->taglist);
    gst_pad_push_event (stream->pad, gst_event_new_tag (stream->taglist));
    stream->taglist = NULL;
  }

  stream->need_newsegment = FALSE;
}

static GstFlowReturn
gst_ts_demux_push_pending_data (GstTSDemux * demux, TSDemuxStream * stream)
{
  GstFlowReturn res = GST_FLOW_OK;
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  GstBuffer *buffer = NULL;
  MpegTSPacketizer2 *packetizer = MPEG_TS_BASE_PACKETIZER (demux);

  GST_DEBUG_OBJECT (stream->pad,
      "stream:%p, pid:0x%04x stream_type:%d state:%d", stream, bs->pid,
      bs->stream_type, stream->state);

  if (G_UNLIKELY (stream->data == NULL)) {
    GST_LOG ("stream->data == NULL");
    goto beach;
  }

  if (G_UNLIKELY (stream->state == PENDING_PACKET_EMPTY)) {
    GST_LOG ("EMPTY: returning");
    goto beach;
  }

  if (G_UNLIKELY (stream->state != PENDING_PACKET_BUFFER)) {
    GST_LOG ("state:%d, returning", stream->state);
    goto beach;
  }

  if (G_UNLIKELY (!stream->active))
    activate_pad_for_stream (demux, stream);

  if (G_UNLIKELY (stream->pad == NULL)) {
    g_free (stream->data);
    goto beach;
  }

  if (G_UNLIKELY (demux->program == NULL)) {
    GST_LOG_OBJECT (demux, "No program");
    g_free (stream->data);
    goto beach;
  }

  if (G_UNLIKELY (stream->need_newsegment))
    calculate_and_push_newsegment (demux, stream);

  buffer = gst_buffer_new_wrapped (stream->data, stream->current_size);

  GST_DEBUG_OBJECT (stream->pad, "stream->pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->pts));
  if (GST_CLOCK_TIME_IS_VALID (stream->pts))
    GST_BUFFER_PTS (buffer) =
        mpegts_packetizer_pts_to_ts (packetizer, stream->pts,
        demux->program->pcr_pid);
  if (GST_CLOCK_TIME_IS_VALID (stream->dts))
    GST_BUFFER_DTS (buffer) =
        mpegts_packetizer_pts_to_ts (packetizer, stream->dts,
        demux->program->pcr_pid);

  GST_DEBUG_OBJECT (stream->pad,
      "Pushing buffer with PTS: %" GST_TIME_FORMAT " , DTS: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DTS (buffer)));

  res = gst_pad_push (stream->pad, buffer);
  GST_DEBUG_OBJECT (stream->pad, "Returned %s", gst_flow_get_name (res));
  res = tsdemux_combine_flows (demux, stream, res);
  GST_DEBUG_OBJECT (stream->pad, "combined %s", gst_flow_get_name (res));

beach:
  /* Reset everything */
  GST_LOG ("Resetting to EMPTY, returning %s", gst_flow_get_name (res));
  stream->state = PENDING_PACKET_EMPTY;
  stream->data = NULL;
  stream->expected_size = 0;
  stream->current_size = 0;

  return res;
}

static GstFlowReturn
gst_ts_demux_handle_packet (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSPacketizerPacket * packet, MpegTSPacketizerSection * section)
{
  GstFlowReturn res = GST_FLOW_OK;

  GST_DEBUG ("data:%p", packet->data);
  GST_LOG ("pid 0x%04x pusi:%d, afc:%d, cont:%d, payload:%p", packet->pid,
      packet->payload_unit_start_indicator, packet->adaptation_field_control,
      packet->continuity_counter, packet->payload);

  if (section) {
    GST_DEBUG ("section complete:%d, buffer size %d",
        section->complete, section->section_length);
    return res;
  }

  if (G_UNLIKELY (packet->payload_unit_start_indicator) &&
      packet->adaptation_field_control & 0x1)
    /* Flush previous data */
    res = gst_ts_demux_push_pending_data (demux, stream);

  if (packet->payload && (res == GST_FLOW_OK || res == GST_FLOW_NOT_LINKED)
      && stream->pad) {
    gst_ts_demux_queue_data (demux, stream, packet);
    GST_DEBUG ("current_size:%d, expected_size:%d",
        stream->current_size, stream->expected_size);
    /* Finally check if the data we queued completes a packet */
    if (stream->expected_size && stream->current_size == stream->expected_size) {
      GST_LOG ("pushing complete packet");
      res = gst_ts_demux_push_pending_data (demux, stream);
    }
  }

  return res;
}

static void
gst_ts_demux_flush (MpegTSBase * base)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);

  gst_ts_demux_flush_streams (demux);

  if (demux->segment_event) {
    gst_event_unref (demux->segment_event);
    demux->segment_event = NULL;
  }
  demux->calculate_update_segment = FALSE;
  gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
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
    }
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
      GST_RANK_PRIMARY, GST_TYPE_TS_DEMUX);
}
