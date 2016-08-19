/* GStreamer ASF/WMV/WMA demuxer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2006-2009 Tim-Philipp Müller <tim centricular net>
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

/* TODO:
 *
 * - _loop():
 *   stop if at end of segment if != end of file, ie. demux->segment.stop
 *
 * - fix packet parsing:
 *   there's something wrong with timestamps for packets with keyframes,
 *   and durations too.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstutils.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/riff/riff-media.h>
#include <gst/tag/tag.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/video/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gstasfdemux.h"
#include "asfheaders.h"
#include "asfpacket.h"

static GstStaticPadTemplate gst_asf_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

/* size of an ASF object header, ie. GUID (16 bytes) + object size (8 bytes) */
#define ASF_OBJECT_HEADER_SIZE  (16+8)

/* FIXME: get rid of this */
/* abuse this GstFlowReturn enum for internal usage */
#define ASF_FLOW_NEED_MORE_DATA  99

#define gst_asf_get_flow_name(flow)    \
  (flow == ASF_FLOW_NEED_MORE_DATA) ?  \
  "need-more-data" : gst_flow_get_name (flow)

GST_DEBUG_CATEGORY (asfdemux_dbg);

static GstStateChangeReturn gst_asf_demux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_asf_demux_element_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_asf_demux_send_event_unlocked (GstASFDemux * demux,
    GstEvent * event);
static gboolean gst_asf_demux_handle_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_asf_demux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_asf_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_asf_demux_process_object (GstASFDemux * demux,
    guint8 ** p_data, guint64 * p_size);
static gboolean gst_asf_demux_activate (GstPad * sinkpad, GstObject * parent);
static gboolean gst_asf_demux_activate_mode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);
static void gst_asf_demux_loop (GstASFDemux * demux);
static void
gst_asf_demux_process_queued_extended_stream_objects (GstASFDemux * demux);
static gboolean gst_asf_demux_pull_headers (GstASFDemux * demux,
    GstFlowReturn * pflow);
static void gst_asf_demux_pull_indices (GstASFDemux * demux);
static void gst_asf_demux_reset_stream_state_after_discont (GstASFDemux * asf);
static gboolean
gst_asf_demux_parse_data_object_start (GstASFDemux * demux, guint8 * data);
static void gst_asf_demux_descramble_buffer (GstASFDemux * demux,
    AsfStream * stream, GstBuffer ** p_buffer);
static void gst_asf_demux_activate_stream (GstASFDemux * demux,
    AsfStream * stream);
static GstStructure *gst_asf_demux_get_metadata_for_stream (GstASFDemux * d,
    guint stream_num);
static GstFlowReturn gst_asf_demux_push_complete_payloads (GstASFDemux * demux,
    gboolean force);

#define gst_asf_demux_parent_class parent_class
G_DEFINE_TYPE (GstASFDemux, gst_asf_demux, GST_TYPE_ELEMENT);

static void
gst_asf_demux_class_init (GstASFDemuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class, "ASF Demuxer",
      "Codec/Demuxer",
      "Demultiplexes ASF Streams", "Owen Fraser-Green <owen@discobabe.net>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &audio_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &video_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_asf_demux_sink_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_asf_demux_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_asf_demux_element_send_event);
}

static void
gst_asf_demux_free_stream (GstASFDemux * demux, AsfStream * stream)
{
  gst_caps_replace (&stream->caps, NULL);
  if (stream->pending_tags) {
    gst_tag_list_unref (stream->pending_tags);
    stream->pending_tags = NULL;
  }
  if (stream->streamheader) {
    gst_buffer_unref (stream->streamheader);
    stream->streamheader = NULL;
  }
  if (stream->pad) {
    if (stream->active) {
      gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->pad);
      gst_flow_combiner_remove_pad (demux->flowcombiner, stream->pad);
    } else
      gst_object_unref (stream->pad);
    stream->pad = NULL;
  }

  if (stream->payloads) {
    while (stream->payloads->len > 0) {
      AsfPayload *payload;
      guint last;

      last = stream->payloads->len - 1;
      payload = &g_array_index (stream->payloads, AsfPayload, last);
      gst_buffer_replace (&payload->buf, NULL);
      g_array_remove_index (stream->payloads, last);
    }
    g_array_free (stream->payloads, TRUE);
    stream->payloads = NULL;
  }

  if (stream->payloads_rev) {
    while (stream->payloads_rev->len > 0) {
      AsfPayload *payload;
      guint last;

      last = stream->payloads_rev->len - 1;
      payload = &g_array_index (stream->payloads_rev, AsfPayload, last);
      gst_buffer_replace (&payload->buf, NULL);
      g_array_remove_index (stream->payloads_rev, last);
    }
    g_array_free (stream->payloads_rev, TRUE);
    stream->payloads_rev = NULL;
  }

  if (stream->ext_props.valid) {
    g_free (stream->ext_props.payload_extensions);
    stream->ext_props.payload_extensions = NULL;
  }
}

static void
gst_asf_demux_reset (GstASFDemux * demux, gboolean chain_reset)
{
  GST_LOG_OBJECT (demux, "resetting");

  gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
  demux->segment_running = FALSE;
  if (demux->adapter && !chain_reset) {
    gst_adapter_clear (demux->adapter);
    g_object_unref (demux->adapter);
    demux->adapter = NULL;
  }
  if (demux->taglist) {
    gst_tag_list_unref (demux->taglist);
    demux->taglist = NULL;
  }
  if (demux->metadata) {
    gst_caps_unref (demux->metadata);
    demux->metadata = NULL;
  }
  if (demux->global_metadata) {
    gst_structure_free (demux->global_metadata);
    demux->global_metadata = NULL;
  }
  if (demux->mut_ex_streams) {
    g_slist_free (demux->mut_ex_streams);
    demux->mut_ex_streams = NULL;
  }

  demux->state = GST_ASF_DEMUX_STATE_HEADER;
  g_free (demux->objpath);
  demux->objpath = NULL;
  g_strfreev (demux->languages);
  demux->languages = NULL;
  demux->num_languages = 0;
  g_slist_foreach (demux->ext_stream_props, (GFunc) gst_mini_object_unref,
      NULL);
  g_slist_free (demux->ext_stream_props);
  demux->ext_stream_props = NULL;

  while (demux->old_num_streams > 0) {
    gst_asf_demux_free_stream (demux,
        &demux->old_stream[demux->old_num_streams - 1]);
    --demux->old_num_streams;
  }
  memset (demux->old_stream, 0, sizeof (demux->old_stream));
  demux->old_num_streams = 0;

  /* when resetting for a new chained asf, we don't want to remove the pads
   * before adding the new ones */
  if (chain_reset) {
    memcpy (demux->old_stream, demux->stream, sizeof (demux->stream));
    demux->old_num_streams = demux->num_streams;
    demux->num_streams = 0;
  }

  while (demux->num_streams > 0) {
    gst_asf_demux_free_stream (demux, &demux->stream[demux->num_streams - 1]);
    --demux->num_streams;
  }
  memset (demux->stream, 0, sizeof (demux->stream));
  if (!chain_reset) {
    /* do not remove those for not adding pads with same name */
    demux->num_audio_streams = 0;
    demux->num_video_streams = 0;
    demux->have_group_id = FALSE;
    demux->group_id = G_MAXUINT;
  }
  demux->num_streams = 0;
  demux->activated_streams = FALSE;
  demux->first_ts = GST_CLOCK_TIME_NONE;
  demux->segment_ts = GST_CLOCK_TIME_NONE;
  demux->in_gap = 0;
  if (!chain_reset)
    gst_segment_init (&demux->in_segment, GST_FORMAT_UNDEFINED);
  demux->state = GST_ASF_DEMUX_STATE_HEADER;
  demux->seekable = FALSE;
  demux->broadcast = FALSE;
  demux->sidx_interval = 0;
  demux->sidx_num_entries = 0;
  g_free (demux->sidx_entries);
  demux->sidx_entries = NULL;

  demux->speed_packets = 1;

  demux->asf_3D_mode = GST_ASF_3D_NONE;

  if (chain_reset) {
    GST_LOG_OBJECT (demux, "Restarting");
    gst_segment_init (&demux->segment, GST_FORMAT_TIME);
    demux->need_newsegment = TRUE;
    demux->segment_seqnum = 0;
    demux->segment_running = FALSE;
    demux->keyunit_sync = FALSE;
    demux->accurate = FALSE;
    demux->metadata = gst_caps_new_empty ();
    demux->global_metadata = gst_structure_new_empty ("metadata");
    demux->data_size = 0;
    demux->data_offset = 0;
    demux->index_offset = 0;
  } else {
    demux->base_offset = 0;
  }

  g_slist_free (demux->other_streams);
  demux->other_streams = NULL;
}

static void
gst_asf_demux_init (GstASFDemux * demux)
{
  demux->sinkpad =
      gst_pad_new_from_static_template (&gst_asf_demux_sink_template, "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_sink_event));
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_activate));
  gst_pad_set_activatemode_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_activate_mode));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* set initial state */
  gst_asf_demux_reset (demux, FALSE);
}

static gboolean
gst_asf_demux_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  gst_query_unref (query);

  if (!pull_mode)
    goto activate_push;

  GST_DEBUG_OBJECT (sinkpad, "activating pull");
  return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);

activate_push:
  {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }
}

static gboolean
gst_asf_demux_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;
  GstASFDemux *demux;

  demux = GST_ASF_DEMUX (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      demux->state = GST_ASF_DEMUX_STATE_HEADER;
      demux->streaming = TRUE;
      res = TRUE;
      break;
    case GST_PAD_MODE_PULL:
      if (active) {
        demux->state = GST_ASF_DEMUX_STATE_HEADER;
        demux->streaming = FALSE;

        res = gst_pad_start_task (sinkpad, (GstTaskFunction) gst_asf_demux_loop,
            demux, NULL);
      } else {
        res = gst_pad_stop_task (sinkpad);
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_asf_demux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstASFDemux *demux;
  gboolean ret = TRUE;

  demux = GST_ASF_DEMUX (parent);

  GST_LOG_OBJECT (demux, "handling %s event", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:{
      const GstSegment *segment;

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_BYTES) {
        if (demux->packet_size && segment->start > demux->data_offset)
          demux->packet = (segment->start - demux->data_offset) /
              demux->packet_size;
        else
          demux->packet = 0;
      } else if (segment->format == GST_FORMAT_TIME) {
        /* do not know packet position, not really a problem */
        demux->packet = -1;
      } else {
        GST_WARNING_OBJECT (demux, "unsupported newsegment format, ignoring");
        gst_event_unref (event);
        break;
      }

      /* record upstream segment for interpolation */
      if (segment->format != demux->in_segment.format)
        gst_segment_init (&demux->in_segment, GST_FORMAT_UNDEFINED);
      gst_segment_copy_into (segment, &demux->in_segment);

      /* in either case, clear some state and generate newsegment later on */
      GST_OBJECT_LOCK (demux);
      demux->segment_ts = GST_CLOCK_TIME_NONE;
      demux->in_gap = GST_CLOCK_TIME_NONE;
      demux->need_newsegment = TRUE;
      demux->segment_seqnum = gst_event_get_seqnum (event);
      gst_asf_demux_reset_stream_state_after_discont (demux);
      /* if we seek back after reaching EOS, go back to packet reading state */
      if (demux->data_offset > 0 && segment->start >= demux->data_offset
          && demux->state == GST_ASF_DEMUX_STATE_INDEX) {
        demux->state = GST_ASF_DEMUX_STATE_DATA;
      }
      GST_OBJECT_UNLOCK (demux);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_EOS:{
      GstFlowReturn flow;

      if (demux->state == GST_ASF_DEMUX_STATE_HEADER) {
        GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
            (_("This stream contains no data.")),
            ("got eos and didn't receive a complete header object"));
        break;
      }
      flow = gst_asf_demux_push_complete_payloads (demux, TRUE);
      if (flow < GST_FLOW_EOS || flow == GST_FLOW_NOT_LINKED) {
        GST_ELEMENT_FLOW_ERROR (demux, flow);
        break;
      }

      GST_OBJECT_LOCK (demux);
      gst_adapter_clear (demux->adapter);
      GST_OBJECT_UNLOCK (demux);
      gst_asf_demux_send_event_unlocked (demux, event);
      break;
    }

    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (demux);
      gst_asf_demux_reset_stream_state_after_discont (demux);
      GST_OBJECT_UNLOCK (demux);
      gst_asf_demux_send_event_unlocked (demux, event);
      /* upon activation, latency is no longer introduced, e.g. after seek */
      if (demux->activated_streams)
        demux->latency = 0;
      break;

    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_asf_demux_seek_index_lookup (GstASFDemux * demux, guint * packet,
    GstClockTime seek_time, GstClockTime * p_idx_time, guint * speed,
    gboolean next, gboolean * eos)
{
  GstClockTime idx_time;
  guint idx;

  if (eos)
    *eos = FALSE;

  if (G_UNLIKELY (demux->sidx_num_entries == 0 || demux->sidx_interval == 0))
    return FALSE;

  idx = (guint) ((seek_time + demux->preroll) / demux->sidx_interval);

  if (next) {
    /* if we want the next keyframe, we have to go forward till we find
       a different packet number */
    guint idx2;
    if (idx >= demux->sidx_num_entries - 1) {
      /* If we get here, we're asking for next keyframe after the last one. There isn't one. */
      if (eos)
        *eos = TRUE;
      return FALSE;
    }
    for (idx2 = idx + 1; idx2 < demux->sidx_num_entries; ++idx2) {
      if (demux->sidx_entries[idx].packet != demux->sidx_entries[idx2].packet) {
        idx = idx2;
        break;
      }
    }
  }

  if (G_UNLIKELY (idx >= demux->sidx_num_entries)) {
    if (eos)
      *eos = TRUE;
    return FALSE;
  }

  *packet = demux->sidx_entries[idx].packet;
  if (speed)
    *speed = demux->sidx_entries[idx].count;

  /* so we get closer to the actual time of the packet ... actually, let's not
   * do this, since we throw away superfluous payloads before the seek position
   * anyway; this way, our key unit seek 'snap resolution' is a bit better
   * (ie. same as index resolution) */
  /*
     while (idx > 0 && demux->sidx_entries[idx-1] == demux->sidx_entries[idx])
     --idx;
   */

  idx_time = demux->sidx_interval * idx;
  if (G_LIKELY (idx_time >= demux->preroll))
    idx_time -= demux->preroll;

  GST_DEBUG_OBJECT (demux, "%" GST_TIME_FORMAT " => packet %u at %"
      GST_TIME_FORMAT, GST_TIME_ARGS (seek_time), *packet,
      GST_TIME_ARGS (idx_time));

  if (G_LIKELY (p_idx_time))
    *p_idx_time = idx_time;

  return TRUE;
}

static void
gst_asf_demux_reset_stream_state_after_discont (GstASFDemux * demux)
{
  guint n;

  gst_adapter_clear (demux->adapter);

  GST_DEBUG_OBJECT (demux, "reset stream state");

  gst_flow_combiner_reset (demux->flowcombiner);
  for (n = 0; n < demux->num_streams; n++) {
    demux->stream[n].discont = TRUE;
    demux->stream[n].first_buffer = TRUE;

    while (demux->stream[n].payloads->len > 0) {
      AsfPayload *payload;
      guint last;

      last = demux->stream[n].payloads->len - 1;
      payload = &g_array_index (demux->stream[n].payloads, AsfPayload, last);
      gst_buffer_replace (&payload->buf, NULL);
      g_array_remove_index (demux->stream[n].payloads, last);
    }
  }
}

static void
gst_asf_demux_mark_discont (GstASFDemux * demux)
{
  guint n;

  GST_DEBUG_OBJECT (demux, "Mark stream discont");

  for (n = 0; n < demux->num_streams; n++)
    demux->stream[n].discont = TRUE;
}

/* do a seek in push based mode */
static gboolean
gst_asf_demux_handle_seek_push (GstASFDemux * demux, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  guint packet;
  gboolean res;
  GstEvent *byte_event;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  stop_type = GST_SEEK_TYPE_NONE;
  stop = -1;

  GST_DEBUG_OBJECT (demux, "seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (cur));

  /* determine packet, by index or by estimation */
  if (!gst_asf_demux_seek_index_lookup (demux, &packet, cur, NULL, NULL, FALSE,
          NULL)) {
    packet =
        (guint) gst_util_uint64_scale (demux->num_packets, cur,
        demux->play_time);
  }

  if (packet > demux->num_packets) {
    GST_DEBUG_OBJECT (demux, "could not determine packet to seek to, "
        "seek aborted.");
    return FALSE;
  }

  GST_DEBUG_OBJECT (demux, "seeking to packet %d", packet);

  cur = demux->data_offset + ((guint64) packet * demux->packet_size);

  GST_DEBUG_OBJECT (demux, "Pushing BYTE seek rate %g, "
      "start %" G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT, rate, cur, stop);
  /* BYTE seek event */
  byte_event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type,
      cur, stop_type, stop);
  gst_event_set_seqnum (byte_event, gst_event_get_seqnum (event));
  res = gst_pad_push_event (demux->sinkpad, byte_event);

  return res;
}

static gboolean
gst_asf_demux_handle_seek_event (GstASFDemux * demux, GstEvent * event)
{
  GstClockTime idx_time;
  GstSegment segment;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  gboolean only_need_update;
  gboolean after, before, next;
  gboolean flush;
  gdouble rate;
  gint64 cur, stop;
  gint64 seek_time;
  guint packet, speed_count = 1;
  gboolean eos;
  guint32 seqnum;
  GstEvent *fevent;
  gint i;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  if (G_UNLIKELY (format != GST_FORMAT_TIME)) {
    GST_LOG_OBJECT (demux, "seeking is only supported in TIME format");
    return FALSE;
  }

  /* upstream might handle TIME seek, e.g. mms or rtsp, or not, e.g. http,
   * so first try to let it handle the seek event. */
  if (gst_pad_push_event (demux->sinkpad, gst_event_ref (event)))
    return TRUE;

  if (G_UNLIKELY (demux->seekable == FALSE || demux->packet_size == 0 ||
          demux->num_packets == 0 || demux->play_time == 0)) {
    GST_LOG_OBJECT (demux, "stream is not seekable");
    return FALSE;
  }

  if (G_UNLIKELY (!demux->activated_streams)) {
    GST_LOG_OBJECT (demux, "streams not yet activated, ignoring seek");
    return FALSE;
  }

  if (G_UNLIKELY (rate <= 0.0)) {
    GST_LOG_OBJECT (demux, "backward playback");
    demux->seek_to_cur_pos = TRUE;
    for (i = 0; i < demux->num_streams; i++) {
      demux->stream[i].reverse_kf_ready = FALSE;
    }
  }

  seqnum = gst_event_get_seqnum (event);
  flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);
  demux->accurate =
      ((flags & GST_SEEK_FLAG_ACCURATE) == GST_SEEK_FLAG_ACCURATE);
  demux->keyunit_sync =
      ((flags & GST_SEEK_FLAG_KEY_UNIT) == GST_SEEK_FLAG_KEY_UNIT);
  after = ((flags & GST_SEEK_FLAG_SNAP_AFTER) == GST_SEEK_FLAG_SNAP_AFTER);
  before = ((flags & GST_SEEK_FLAG_SNAP_BEFORE) == GST_SEEK_FLAG_SNAP_BEFORE);
  next = after && !before;

  if (G_UNLIKELY (demux->streaming)) {
    /* support it safely needs more segment handling, e.g. closing etc */
    if (!flush) {
      GST_LOG_OBJECT (demux, "streaming; non-flushing seek not supported");
      return FALSE;
    }
    /* we can (re)construct the start later on, but not the end */
    if (stop_type != GST_SEEK_TYPE_NONE &&
        (stop_type != GST_SEEK_TYPE_SET || GST_CLOCK_TIME_IS_VALID (stop))) {
      GST_LOG_OBJECT (demux, "streaming; end position must be NONE");
      return FALSE;
    }
    return gst_asf_demux_handle_seek_push (demux, event);
  }

  /* unlock the streaming thread */
  if (G_LIKELY (flush)) {
    fevent = gst_event_new_flush_start ();

    gst_event_set_seqnum (fevent, seqnum);
    gst_pad_push_event (demux->sinkpad, gst_event_ref (fevent));
    gst_asf_demux_send_event_unlocked (demux, fevent);
  } else {
    gst_pad_pause_task (demux->sinkpad);
  }

  /* grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever */
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  /* we now can stop flushing, since we have the stream lock now */
  fevent = gst_event_new_flush_stop (TRUE);
  gst_event_set_seqnum (fevent, seqnum);
  gst_pad_push_event (demux->sinkpad, gst_event_ref (fevent));

  if (G_LIKELY (flush))
    gst_asf_demux_send_event_unlocked (demux, fevent);
  else
    gst_event_unref (fevent);

  /* operating on copy of segment until we know the seek worked */
  segment = demux->segment;

  if (G_UNLIKELY (demux->segment_running && !flush)) {
    GstSegment newsegment;
    GstEvent *newseg;

    /* create the segment event to close the current segment */
    gst_segment_copy_into (&segment, &newsegment);
    newseg = gst_event_new_segment (&newsegment);
    gst_event_set_seqnum (newseg, seqnum);

    gst_asf_demux_send_event_unlocked (demux, newseg);
  }

  gst_segment_do_seek (&segment, rate, format, flags, cur_type,
      cur, stop_type, stop, &only_need_update);

  GST_DEBUG_OBJECT (demux, "seeking to time %" GST_TIME_FORMAT ", segment: "
      "%" GST_SEGMENT_FORMAT, GST_TIME_ARGS (segment.start), &segment);

  if (cur_type != GST_SEEK_TYPE_SET)
    seek_time = segment.start;
  else
    seek_time = cur;

  /* FIXME: should check the KEY_UNIT flag; need to adjust position to
   * real start of data and segment_start to indexed time for key unit seek*/
  if (G_UNLIKELY (!gst_asf_demux_seek_index_lookup (demux, &packet, seek_time,
              &idx_time, &speed_count, next, &eos))) {
    gint64 offset;

    if (eos) {
      demux->packet = demux->num_packets;
      goto skip;
    }

    /* First try to query our source to see if it can convert for us. This is
       the case when our source is an mms stream, notice that in this case
       gstmms will do a time based seek to get the byte offset, this is not a
       problem as the seek to this offset needs to happen anway. */
    if (gst_pad_peer_query_convert (demux->sinkpad, GST_FORMAT_TIME, seek_time,
            GST_FORMAT_BYTES, &offset)) {
      packet = (offset - demux->data_offset) / demux->packet_size;
      GST_LOG_OBJECT (demux, "convert %" GST_TIME_FORMAT
          " to bytes query result: %" G_GINT64_FORMAT ", data_ofset: %"
          G_GINT64_FORMAT ", packet_size: %u," " resulting packet: %u\n",
          GST_TIME_ARGS (seek_time), offset, demux->data_offset,
          demux->packet_size, packet);
    } else {
      /* FIXME: For streams containing video, seek to an earlier position in
       * the hope of hitting a keyframe and let the sinks throw away the stuff
       * before the segment start. For audio-only this is unnecessary as every
       * frame is 'key'. */
      if (flush && (demux->accurate || (demux->keyunit_sync && !next))
          && demux->num_video_streams > 0) {
        seek_time -= 5 * GST_SECOND;
        if (seek_time < 0)
          seek_time = 0;
      }

      packet = (guint) gst_util_uint64_scale (demux->num_packets,
          seek_time, demux->play_time);

      if (packet > demux->num_packets)
        packet = demux->num_packets;
    }
  } else {
    if (G_LIKELY (demux->keyunit_sync && !demux->accurate)) {
      GST_DEBUG_OBJECT (demux, "key unit seek, adjust seek_time = %"
          GST_TIME_FORMAT " to index_time = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (seek_time), GST_TIME_ARGS (idx_time));
      segment.start = idx_time;
      segment.position = idx_time;
      segment.time = idx_time;
    }
  }

  GST_DEBUG_OBJECT (demux, "seeking to packet %u (%d)", packet, speed_count);

  GST_OBJECT_LOCK (demux);
  demux->segment = segment;
  if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)) {
    demux->packet = (gint64) gst_util_uint64_scale (demux->num_packets,
        stop, demux->play_time);
  } else {
    demux->packet = packet;
  }

  demux->need_newsegment = TRUE;
  demux->segment_seqnum = seqnum;
  demux->speed_packets =
      GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment) ? 1 : speed_count;
  gst_asf_demux_reset_stream_state_after_discont (demux);
  GST_OBJECT_UNLOCK (demux);

skip:
  /* restart our task since it might have been stopped when we did the flush */
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_asf_demux_loop,
      demux, NULL);

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;
}

static gboolean
gst_asf_demux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstASFDemux *demux;
  gboolean ret;

  demux = GST_ASF_DEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_LOG_OBJECT (pad, "seek event");
      ret = gst_asf_demux_handle_seek_event (demux, event);
      gst_event_unref (event);
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
      /* just drop these two silently */
      gst_event_unref (event);
      ret = FALSE;
      break;
    default:
      GST_LOG_OBJECT (pad, "%s event", GST_EVENT_TYPE_NAME (event));
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static inline guint32
gst_asf_demux_identify_guid (const ASFGuidHash * guids, ASFGuid * guid)
{
  guint32 ret;

  ret = gst_asf_identify_guid (guids, guid);

  GST_LOG ("%s  0x%08x-0x%08x-0x%08x-0x%08x",
      gst_asf_get_guid_nick (guids, ret),
      guid->v1, guid->v2, guid->v3, guid->v4);

  return ret;
}

typedef struct
{
  AsfObjectID id;
  guint64 size;
} AsfObject;


/* expect is true when the user is expeting an object,
 * when false, it will give no warnings if the object
 * is not identified
 */
static gboolean
asf_demux_peek_object (GstASFDemux * demux, const guint8 * data,
    guint data_len, AsfObject * object, gboolean expect)
{
  ASFGuid guid;

  if (data_len < ASF_OBJECT_HEADER_SIZE)
    return FALSE;

  guid.v1 = GST_READ_UINT32_LE (data + 0);
  guid.v2 = GST_READ_UINT32_LE (data + 4);
  guid.v3 = GST_READ_UINT32_LE (data + 8);
  guid.v4 = GST_READ_UINT32_LE (data + 12);

  object->size = GST_READ_UINT64_LE (data + 16);

  /* FIXME: make asf_demux_identify_object_guid() */
  object->id = gst_asf_demux_identify_guid (asf_object_guids, &guid);
  if (object->id == ASF_OBJ_UNDEFINED && expect) {
    GST_WARNING_OBJECT (demux, "Unknown object %08x-%08x-%08x-%08x",
        guid.v1, guid.v2, guid.v3, guid.v4);
  }

  return TRUE;
}

static void
gst_asf_demux_release_old_pads (GstASFDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "Releasing old pads");

  while (demux->old_num_streams > 0) {
    gst_pad_push_event (demux->old_stream[demux->old_num_streams - 1].pad,
        gst_event_new_eos ());
    gst_asf_demux_free_stream (demux,
        &demux->old_stream[demux->old_num_streams - 1]);
    --demux->old_num_streams;
  }
  memset (demux->old_stream, 0, sizeof (demux->old_stream));
  demux->old_num_streams = 0;
}

static GstFlowReturn
gst_asf_demux_chain_headers (GstASFDemux * demux)
{
  GstFlowReturn flow;
  AsfObject obj;
  guint8 *header_data, *data = NULL;
  const guint8 *cdata = NULL;
  guint64 header_size;

  cdata = (guint8 *) gst_adapter_map (demux->adapter, ASF_OBJECT_HEADER_SIZE);
  if (cdata == NULL)
    goto need_more_data;

  asf_demux_peek_object (demux, cdata, ASF_OBJECT_HEADER_SIZE, &obj, TRUE);
  if (obj.id != ASF_OBJ_HEADER)
    goto wrong_type;

  GST_LOG_OBJECT (demux, "header size = %u", (guint) obj.size);

  /* + 50 for non-packet data at beginning of ASF_OBJ_DATA */
  if (gst_adapter_available (demux->adapter) < obj.size + 50)
    goto need_more_data;

  data = gst_adapter_take (demux->adapter, obj.size + 50);

  header_data = data;
  header_size = obj.size;
  flow = gst_asf_demux_process_object (demux, &header_data, &header_size);
  if (flow != GST_FLOW_OK)
    goto parse_failed;

  /* calculate where the packet data starts */
  demux->data_offset = obj.size + 50;

  /* now parse the beginning of the ASF_OBJ_DATA object */
  if (!gst_asf_demux_parse_data_object_start (demux, data + obj.size))
    goto wrong_type;

  if (demux->num_streams == 0)
    goto no_streams;

  g_free (data);
  return GST_FLOW_OK;

/* NON-FATAL */
need_more_data:
  {
    GST_LOG_OBJECT (demux, "not enough data in adapter yet");
    return GST_FLOW_OK;
  }

/* ERRORS */
wrong_type:
  {
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("This doesn't seem to be an ASF file"));
    g_free (data);
    return GST_FLOW_ERROR;
  }
no_streams:
parse_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("header parsing failed, or no streams found, flow = %s",
            gst_flow_get_name (flow)));
    g_free (data);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_asf_demux_pull_data (GstASFDemux * demux, guint64 offset, guint size,
    GstBuffer ** p_buf, GstFlowReturn * p_flow)
{
  gsize buffer_size;
  GstFlowReturn flow;

  GST_LOG_OBJECT (demux, "pulling buffer at %" G_GUINT64_FORMAT "+%u",
      offset, size);

  flow = gst_pad_pull_range (demux->sinkpad, offset, size, p_buf);

  if (G_LIKELY (p_flow))
    *p_flow = flow;

  if (G_UNLIKELY (flow != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (demux, "flow %s pulling buffer at %" G_GUINT64_FORMAT
        "+%u", gst_flow_get_name (flow), offset, size);
    *p_buf = NULL;
    return FALSE;
  }

  g_assert (*p_buf != NULL);

  buffer_size = gst_buffer_get_size (*p_buf);
  if (G_UNLIKELY (buffer_size < size)) {
    GST_DEBUG_OBJECT (demux, "short read pulling buffer at %" G_GUINT64_FORMAT
        "+%u (got only %" G_GSIZE_FORMAT " bytes)", offset, size, buffer_size);
    gst_buffer_unref (*p_buf);
    if (G_LIKELY (p_flow))
      *p_flow = GST_FLOW_EOS;
    *p_buf = NULL;
    return FALSE;
  }

  return TRUE;
}

static void
gst_asf_demux_pull_indices (GstASFDemux * demux)
{
  GstBuffer *buf = NULL;
  guint64 offset;
  guint num_read = 0;

  offset = demux->index_offset;

  if (G_UNLIKELY (offset == 0)) {
    GST_DEBUG_OBJECT (demux, "can't read indices, don't know index offset");
    return;
  }

  while (gst_asf_demux_pull_data (demux, offset, 16 + 8, &buf, NULL)) {
    GstFlowReturn flow;
    AsfObject obj;
    GstMapInfo map;
    guint8 *bufdata;

    gst_buffer_map (buf, &map, GST_MAP_READ);
    g_assert (map.size >= 16 + 8);
    asf_demux_peek_object (demux, map.data, 16 + 8, &obj, TRUE);
    gst_buffer_unmap (buf, &map);
    gst_buffer_replace (&buf, NULL);

    /* check for sanity */
    if (G_UNLIKELY (obj.size > (5 * 1024 * 1024))) {
      GST_DEBUG_OBJECT (demux, "implausible index object size, bailing out");
      break;
    }

    if (G_UNLIKELY (!gst_asf_demux_pull_data (demux, offset, obj.size, &buf,
                NULL)))
      break;

    GST_LOG_OBJECT (demux, "index object at offset 0x%" G_GINT64_MODIFIER "X"
        ", size %u", offset, (guint) obj.size);

    offset += obj.size;         /* increase before _process_object changes it */

    gst_buffer_map (buf, &map, GST_MAP_READ);
    g_assert (map.size >= obj.size);
    bufdata = (guint8 *) map.data;
    flow = gst_asf_demux_process_object (demux, &bufdata, &obj.size);
    gst_buffer_unmap (buf, &map);
    gst_buffer_replace (&buf, NULL);

    if (G_UNLIKELY (flow != GST_FLOW_OK))
      break;

    ++num_read;
  }
  GST_DEBUG_OBJECT (demux, "read %u index objects", num_read);
}

static gboolean
gst_asf_demux_parse_data_object_start (GstASFDemux * demux, guint8 * data)
{
  AsfObject obj;

  asf_demux_peek_object (demux, data, 50, &obj, TRUE);
  if (obj.id != ASF_OBJ_DATA) {
    GST_WARNING_OBJECT (demux, "headers not followed by a DATA object");
    return FALSE;
  }

  demux->state = GST_ASF_DEMUX_STATE_DATA;

  if (!demux->broadcast && obj.size > 50) {
    demux->data_size = obj.size - 50;
    /* CHECKME: for at least one file this is off by +158 bytes?! */
    demux->index_offset = demux->data_offset + demux->data_size;
  } else {
    demux->data_size = 0;
    demux->index_offset = 0;
  }

  demux->packet = 0;

  if (!demux->broadcast) {
    /* skip object header (24 bytes) and file GUID (16 bytes) */
    demux->num_packets = GST_READ_UINT64_LE (data + (16 + 8) + 16);
  } else {
    demux->num_packets = 0;
  }

  if (demux->num_packets == 0)
    demux->seekable = FALSE;

  /* fallback in the unlikely case that headers are inconsistent, can't hurt */
  if (demux->data_size == 0 && demux->num_packets > 0) {
    demux->data_size = demux->num_packets * demux->packet_size;
    demux->index_offset = demux->data_offset + demux->data_size;
  }

  /* process pending stream objects and create pads for those */
  gst_asf_demux_process_queued_extended_stream_objects (demux);

  GST_INFO_OBJECT (demux, "Stream has %" G_GUINT64_FORMAT " packets, "
      "data_offset=%" G_GINT64_FORMAT ", data_size=%" G_GINT64_FORMAT
      ", index_offset=%" G_GUINT64_FORMAT, demux->num_packets,
      demux->data_offset, demux->data_size, demux->index_offset);

  return TRUE;
}

static gboolean
gst_asf_demux_pull_headers (GstASFDemux * demux, GstFlowReturn * pflow)
{
  GstFlowReturn flow = GST_FLOW_OK;
  AsfObject obj;
  GstBuffer *buf = NULL;
  guint64 size;
  GstMapInfo map;
  guint8 *bufdata;

  GST_LOG_OBJECT (demux, "reading headers");

  /* pull HEADER object header, so we know its size */
  if (!gst_asf_demux_pull_data (demux, demux->base_offset, 16 + 8, &buf, &flow))
    goto read_failed;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  g_assert (map.size >= 16 + 8);
  asf_demux_peek_object (demux, map.data, 16 + 8, &obj, TRUE);
  gst_buffer_unmap (buf, &map);
  gst_buffer_replace (&buf, NULL);

  if (obj.id != ASF_OBJ_HEADER)
    goto wrong_type;

  GST_LOG_OBJECT (demux, "header size = %u", (guint) obj.size);

  /* pull HEADER object */
  if (!gst_asf_demux_pull_data (demux, demux->base_offset, obj.size, &buf,
          &flow))
    goto read_failed;

  size = obj.size;              /* don't want obj.size changed */
  gst_buffer_map (buf, &map, GST_MAP_READ);
  g_assert (map.size >= size);
  bufdata = (guint8 *) map.data;
  flow = gst_asf_demux_process_object (demux, &bufdata, &size);
  gst_buffer_unmap (buf, &map);
  gst_buffer_replace (&buf, NULL);

  if (flow != GST_FLOW_OK) {
    GST_WARNING_OBJECT (demux, "process_object: %s", gst_flow_get_name (flow));
    goto parse_failed;
  }

  /* calculate where the packet data starts */
  demux->data_offset = demux->base_offset + obj.size + 50;

  /* now pull beginning of DATA object before packet data */
  if (!gst_asf_demux_pull_data (demux, demux->base_offset + obj.size, 50, &buf,
          &flow))
    goto read_failed;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  g_assert (map.size >= size);
  bufdata = (guint8 *) map.data;
  if (!gst_asf_demux_parse_data_object_start (demux, bufdata))
    goto wrong_type;

  if (demux->num_streams == 0)
    goto no_streams;

  gst_buffer_unmap (buf, &map);
  gst_buffer_replace (&buf, NULL);

  return TRUE;

/* ERRORS */
wrong_type:
  {
    if (buf != NULL) {
      gst_buffer_unmap (buf, &map);
      gst_buffer_replace (&buf, NULL);
    }
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("This doesn't seem to be an ASF file"));
    *pflow = GST_FLOW_ERROR;
    return FALSE;
  }

no_streams:
  flow = GST_FLOW_ERROR;
  GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
      ("header parsing failed, or no streams found, flow = %s",
          gst_flow_get_name (flow)));
read_failed:
parse_failed:
  {
    if (buf)
      gst_buffer_unmap (buf, &map);
    gst_buffer_replace (&buf, NULL);
    *pflow = flow;
    return FALSE;
  }
}

static gboolean
all_streams_prerolled (GstASFDemux * demux)
{
  GstClockTime preroll_time;
  guint i, num_no_data = 0;

  /* Allow at least 500ms of preroll_time  */
  preroll_time = MAX (demux->preroll, 500 * GST_MSECOND);

  /* returns TRUE as long as there isn't a stream which (a) has data queued
   * and (b) the timestamp of last piece of data queued is < demux->preroll
   * AND there is at least one other stream with data queued */
  for (i = 0; i < demux->num_streams; ++i) {
    AsfPayload *last_payload = NULL;
    AsfStream *stream;
    gint last_idx;

    stream = &demux->stream[i];
    if (G_UNLIKELY (stream->payloads->len == 0)) {
      ++num_no_data;
      GST_LOG_OBJECT (stream->pad, "no data queued");
      continue;
    }

    /* find last payload with timestamp */
    for (last_idx = stream->payloads->len - 1;
        last_idx >= 0 && (last_payload == NULL
            || !GST_CLOCK_TIME_IS_VALID (last_payload->ts)); --last_idx) {
      last_payload = &g_array_index (stream->payloads, AsfPayload, last_idx);
    }

    GST_LOG_OBJECT (stream->pad, "checking if %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (last_payload->ts),
        GST_TIME_ARGS (preroll_time));
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (last_payload->ts)
            || last_payload->ts <= preroll_time)) {
      GST_LOG_OBJECT (stream->pad, "not beyond preroll point yet");
      return FALSE;
    }
  }

  if (G_UNLIKELY (num_no_data > 0))
    return FALSE;

  return TRUE;
}

#if 0
static gboolean
gst_asf_demux_have_mutually_exclusive_active_stream (GstASFDemux * demux,
    AsfStream * stream)
{
  GSList *l;

  for (l = demux->mut_ex_streams; l != NULL; l = l->next) {
    guint8 *mes;

    /* check for each mutual exclusion group whether it affects this stream */
    for (mes = (guint8 *) l->data; mes != NULL && *mes != 0xff; ++mes) {
      if (*mes == stream->id) {
        /* we are in this group; let's check if we've already activated streams
         * that are in the same group (and hence mutually exclusive to this
         * one) */
        for (mes = (guint8 *) l->data; mes != NULL && *mes != 0xff; ++mes) {
          guint i;

          for (i = 0; i < demux->num_streams; ++i) {
            if (demux->stream[i].id == *mes && demux->stream[i].active) {
              GST_LOG_OBJECT (demux, "stream with ID %d is mutually exclusive "
                  "to already active stream with ID %d", stream->id,
                  demux->stream[i].id);
              return TRUE;
            }
          }
        }
        /* we can only be in this group once, let's break out and move on to
         * the next mutual exclusion group */
        break;
      }
    }
  }

  return FALSE;
}
#endif

static void
gst_asf_demux_check_segment_ts (GstASFDemux * demux, GstClockTime payload_ts)
{
  /* remember the first queued timestamp for the segment */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (demux->segment_ts) &&
          GST_CLOCK_TIME_IS_VALID (demux->first_ts))) {
    GST_DEBUG_OBJECT (demux, "segment ts: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->first_ts));
    demux->segment_ts = payload_ts;
    /* always note, but only determines segment when streaming */
    if (demux->streaming)
      gst_segment_do_seek (&demux->segment, demux->in_segment.rate,
          GST_FORMAT_TIME, (GstSeekFlags) demux->segment.flags,
          GST_SEEK_TYPE_SET, demux->segment_ts, GST_SEEK_TYPE_NONE, 0, NULL);
  }
}

static gboolean
gst_asf_demux_check_first_ts (GstASFDemux * demux, gboolean force)
{
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (demux->first_ts))) {
    GstClockTime first_ts = GST_CLOCK_TIME_NONE;
    int i;

    /* go trhough each stream, find smallest timestamp */
    for (i = 0; i < demux->num_streams; ++i) {
      AsfStream *stream;
      int j;
      GstClockTime stream_min_ts = GST_CLOCK_TIME_NONE;
      GstClockTime stream_min_ts2 = GST_CLOCK_TIME_NONE;        /* second smallest timestamp */
      stream = &demux->stream[i];

      for (j = 0; j < stream->payloads->len; ++j) {
        AsfPayload *payload = &g_array_index (stream->payloads, AsfPayload, j);
        if (GST_CLOCK_TIME_IS_VALID (payload->ts) &&
            (!GST_CLOCK_TIME_IS_VALID (stream_min_ts)
                || stream_min_ts > payload->ts)) {
          stream_min_ts = payload->ts;
        }
        if (GST_CLOCK_TIME_IS_VALID (payload->ts) &&
            payload->ts > stream_min_ts &&
            (!GST_CLOCK_TIME_IS_VALID (stream_min_ts2)
                || stream_min_ts2 > payload->ts)) {
          stream_min_ts2 = payload->ts;
        }
      }

      /* there are some DVR ms files where first packet has TS of 0 (instead of -1) while subsequent packets have
         regular (singificantly larger) timestamps. If we don't deal with it, we may end up with huge gap in timestamps
         which makes playback stuck. The 0 timestamp may also be valid though, if the second packet timestamp continues 
         from it. I havent found a better way to distinguish between these two, except to set an arbitrary boundary
         and disregard the first 0 timestamp if the second timestamp is bigger than the boundary) */

      if (stream_min_ts == 0 && stream_min_ts2 == GST_CLOCK_TIME_NONE && !force)        /* still waiting for the second timestamp */
        return FALSE;

      if (stream_min_ts == 0 && stream_min_ts2 > GST_SECOND)    /* first timestamp is 0 and second is significantly larger, disregard the 0 */
        stream_min_ts = stream_min_ts2;

      /* if we don't have timestamp for this stream, wait for more data */
      if (!GST_CLOCK_TIME_IS_VALID (stream_min_ts) && !force)
        return FALSE;

      if (GST_CLOCK_TIME_IS_VALID (stream_min_ts) &&
          (!GST_CLOCK_TIME_IS_VALID (first_ts) || first_ts > stream_min_ts))
        first_ts = stream_min_ts;
    }

    if (!GST_CLOCK_TIME_IS_VALID (first_ts))    /* can happen with force = TRUE */
      first_ts = 0;

    demux->first_ts = first_ts;

    /* update packets queued before we knew first timestamp */
    for (i = 0; i < demux->num_streams; ++i) {
      AsfStream *stream;
      int j;
      stream = &demux->stream[i];

      for (j = 0; j < stream->payloads->len; ++j) {
        AsfPayload *payload = &g_array_index (stream->payloads, AsfPayload, j);
        if (GST_CLOCK_TIME_IS_VALID (payload->ts)) {
          if (payload->ts > first_ts)
            payload->ts -= first_ts;
          else
            payload->ts = 0;
        }
      }
    }
  }

  gst_asf_demux_check_segment_ts (demux, 0);

  return TRUE;
}

static gboolean
gst_asf_demux_update_caps_from_payload (GstASFDemux * demux, AsfStream * stream)
{
  /* try to determine whether the stream is AC-3 or MPEG; In dvr-ms the codecTag is unreliable
     and often set wrong, inspecting the data is the only way that seem to be working */
  GstTypeFindProbability prob = GST_TYPE_FIND_NONE;
  GstCaps *caps = NULL;
  int i;
  GstAdapter *adapter = gst_adapter_new ();

  for (i = 0; i < stream->payloads->len && prob < GST_TYPE_FIND_LIKELY; ++i) {
    const guint8 *data;
    AsfPayload *payload;
    int len;

    payload = &g_array_index (stream->payloads, AsfPayload, i);
    gst_adapter_push (adapter, gst_buffer_ref (payload->buf));
    len = gst_adapter_available (adapter);
    data = gst_adapter_map (adapter, len);

  again:

#define MIN_LENGTH 128

    /* look for the sync points */
    while (TRUE) {
      if (len < MIN_LENGTH ||   /* give typefind something to work on */
          (data[0] == 0x0b && data[1] == 0x77) ||       /* AC-3 sync point */
          (data[0] == 0xFF && ((data[1] & 0xF0) >> 4) == 0xF))  /* MPEG sync point */
        break;
      ++data;
      --len;
    }

    gst_caps_take (&caps, gst_type_find_helper_for_data (GST_OBJECT (demux),
            data, len, &prob));

    if (prob < GST_TYPE_FIND_LIKELY) {
      ++data;
      --len;
      if (len > MIN_LENGTH)
        /* this wasn't it, look for another sync point */
        goto again;
    }

    gst_adapter_unmap (adapter);
  }

  gst_object_unref (adapter);

  if (caps) {
    gst_caps_take (&stream->caps, caps);
    return TRUE;
  } else {
    return FALSE;
  }
}

static gboolean
gst_asf_demux_check_activate_streams (GstASFDemux * demux, gboolean force)
{
  guint i;

  if (demux->activated_streams)
    return TRUE;

  if (G_UNLIKELY (!gst_asf_demux_check_first_ts (demux, force)))
    return FALSE;

  if (!all_streams_prerolled (demux) && !force) {
    GST_DEBUG_OBJECT (demux, "not all streams with data beyond preroll yet");
    return FALSE;
  }

  for (i = 0; i < demux->num_streams; ++i) {
    AsfStream *stream = &demux->stream[i];

    if (stream->payloads->len > 0) {

      if (stream->inspect_payload &&    /* dvr-ms required payload inspection */
          !stream->active &&    /* do not inspect active streams (caps were already set) */
          !gst_asf_demux_update_caps_from_payload (demux, stream) &&    /* failed to determine caps */
          stream->payloads->len < 20) { /* if we couldn't determine the caps from 20 packets then just give up and use whatever was in codecTag */
        /* try to gather some more data  */
        return FALSE;
      }
      /* we don't check mutual exclusion stuff here; either we have data for
       * a stream, then we active it, or we don't, then we'll ignore it */
      GST_LOG_OBJECT (stream->pad, "is prerolled - activate!");
      gst_asf_demux_activate_stream (demux, stream);
    } else {
      GST_LOG_OBJECT (stream->pad, "no data, ignoring stream");
    }
  }

  gst_asf_demux_release_old_pads (demux);

  demux->activated_streams = TRUE;
  GST_LOG_OBJECT (demux, "signalling no more pads");
  gst_element_no_more_pads (GST_ELEMENT (demux));
  return TRUE;
}

/* returns the stream that has a complete payload with the lowest timestamp
 * queued, or NULL (we push things by timestamp because during the internal
 * prerolling we might accumulate more data then the external queues can take,
 * so we'd lock up if we pushed all accumulated data for stream N in one go) */
static AsfStream *
gst_asf_demux_find_stream_with_complete_payload (GstASFDemux * demux)
{
  AsfPayload *best_payload = NULL;
  AsfStream *best_stream = NULL;
  guint i;

  for (i = 0; i < demux->num_streams; ++i) {
    AsfStream *stream;
    int j;

    stream = &demux->stream[i];

    /* Don't push any data until we have at least one payload that falls within
     * the current segment. This way we can remove out-of-segment payloads that
     * don't need to be decoded after a seek, sending only data from the
     * keyframe directly before our segment start */
    if (stream->payloads->len > 0) {
      AsfPayload *payload = NULL;
      gint last_idx;

      if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)) {
        /* Reverse playback */

        if (stream->is_video) {
          /* We have to push payloads from KF to the first frame we accumulated (reverse order) */
          if (stream->reverse_kf_ready) {
            payload =
                &g_array_index (stream->payloads, AsfPayload, stream->kf_pos);
            if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (payload->ts))) {
              /* TODO : remove payload from the list? */
              continue;
            }
          } else {
            continue;
          }
        } else {
          /* find first complete payload with timestamp */
          for (j = stream->payloads->len - 1;
              j >= 0 && (payload == NULL
                  || !GST_CLOCK_TIME_IS_VALID (payload->ts)); --j) {
            payload = &g_array_index (stream->payloads, AsfPayload, j);
          }

          /* If there's a complete payload queued for this stream */
          if (!gst_asf_payload_is_complete (payload))
            continue;

        }
      } else {

        /* find last payload with timestamp */
        for (last_idx = stream->payloads->len - 1;
            last_idx >= 0 && (payload == NULL
                || !GST_CLOCK_TIME_IS_VALID (payload->ts)); --last_idx) {
          payload = &g_array_index (stream->payloads, AsfPayload, last_idx);
        }

        /* if this is first payload after seek we might need to update the segment */
        if (GST_CLOCK_TIME_IS_VALID (payload->ts))
          gst_asf_demux_check_segment_ts (demux, payload->ts);

        if (G_UNLIKELY (GST_CLOCK_TIME_IS_VALID (payload->ts) &&
                (payload->ts < demux->segment.start))) {
          if (G_UNLIKELY ((!demux->keyunit_sync) && (!demux->accurate)
                  && payload->keyframe)) {
            GST_DEBUG_OBJECT (stream->pad,
                "Found keyframe, updating segment start to %" GST_TIME_FORMAT,
                GST_TIME_ARGS (payload->ts));
            demux->segment.start = payload->ts;
            demux->segment.time = payload->ts;
          } else {
            GST_DEBUG_OBJECT (stream->pad, "Last queued payload has timestamp %"
                GST_TIME_FORMAT " which is before our segment start %"
                GST_TIME_FORMAT ", not pushing yet",
                GST_TIME_ARGS (payload->ts),
                GST_TIME_ARGS (demux->segment.start));
            continue;
          }
        }
        payload = NULL;
        /* find first complete payload with timestamp */
        for (j = 0;
            j < stream->payloads->len && (payload == NULL
                || !GST_CLOCK_TIME_IS_VALID (payload->ts)); ++j) {
          payload = &g_array_index (stream->payloads, AsfPayload, j);
        }

        /* Now see if there's a complete payload queued for this stream */
        if (!gst_asf_payload_is_complete (payload))
          continue;
      }

      /* ... and whether its timestamp is lower than the current best */
      if (best_stream == NULL || best_payload->ts > payload->ts) {
        best_stream = stream;
        best_payload = payload;
      }
    }
  }

  return best_stream;
}

static GstFlowReturn
gst_asf_demux_push_complete_payloads (GstASFDemux * demux, gboolean force)
{
  AsfStream *stream;
  GstFlowReturn ret = GST_FLOW_OK;

  if (G_UNLIKELY (!demux->activated_streams)) {
    if (!gst_asf_demux_check_activate_streams (demux, force))
      return GST_FLOW_OK;
    /* streams are now activated */
  }

  while ((stream = gst_asf_demux_find_stream_with_complete_payload (demux))) {
    AsfPayload *payload;
    GstClockTime timestamp = GST_CLOCK_TIME_NONE;
    GstClockTime duration = GST_CLOCK_TIME_NONE;

    /* wait until we had a chance to "lock on" some payload's timestamp */
    if (G_UNLIKELY (demux->need_newsegment
            && !GST_CLOCK_TIME_IS_VALID (demux->segment_ts)))
      return GST_FLOW_OK;

    if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment) && stream->is_video
        && stream->payloads->len) {
      payload = &g_array_index (stream->payloads, AsfPayload, stream->kf_pos);
    } else {
      payload = &g_array_index (stream->payloads, AsfPayload, 0);
    }

    /* do we need to send a newsegment event */
    if ((G_UNLIKELY (demux->need_newsegment))) {
      GstEvent *segment_event;

      /* safe default if insufficient upstream info */
      if (!GST_CLOCK_TIME_IS_VALID (demux->in_gap))
        demux->in_gap = 0;

      if (demux->segment.stop == GST_CLOCK_TIME_NONE &&
          demux->segment.duration > 0) {
        /* slight HACK; prevent clipping of last bit */
        demux->segment.stop = demux->segment.duration + demux->in_gap;
      }

      /* FIXME : only if ACCURATE ! */
      if (G_LIKELY (!demux->keyunit_sync && !demux->accurate
              && (GST_CLOCK_TIME_IS_VALID (payload->ts)))
          && !GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)) {
        GST_DEBUG ("Adjusting newsegment start to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (payload->ts));
        demux->segment.start = payload->ts;
        demux->segment.time = payload->ts;
      }

      GST_DEBUG_OBJECT (demux, "sending new-segment event %" GST_SEGMENT_FORMAT,
          &demux->segment);

      /* note: we fix up all timestamps to start from 0, so this should be ok */
      segment_event = gst_event_new_segment (&demux->segment);
      if (demux->segment_seqnum)
        gst_event_set_seqnum (segment_event, demux->segment_seqnum);
      gst_asf_demux_send_event_unlocked (demux, segment_event);

      /* now post any global tags we may have found */
      if (demux->taglist == NULL) {
        demux->taglist = gst_tag_list_new_empty ();
        gst_tag_list_set_scope (demux->taglist, GST_TAG_SCOPE_GLOBAL);
      }

      gst_tag_list_add (demux->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_CONTAINER_FORMAT, "ASF", NULL);

      GST_DEBUG_OBJECT (demux, "global tags: %" GST_PTR_FORMAT, demux->taglist);
      gst_asf_demux_send_event_unlocked (demux,
          gst_event_new_tag (demux->taglist));
      demux->taglist = NULL;

      demux->need_newsegment = FALSE;
      demux->segment_seqnum = 0;
      demux->segment_running = TRUE;
    }

    /* Do we have tags pending for this stream? */
    if (G_UNLIKELY (stream->pending_tags)) {
      GST_LOG_OBJECT (stream->pad, "%" GST_PTR_FORMAT, stream->pending_tags);
      gst_pad_push_event (stream->pad,
          gst_event_new_tag (stream->pending_tags));
      stream->pending_tags = NULL;
    }

    /* We have the whole packet now so we should push the packet to
     * the src pad now. First though we should check if we need to do
     * descrambling */
    if (G_UNLIKELY (stream->span > 1)) {
      gst_asf_demux_descramble_buffer (demux, stream, &payload->buf);
    }

    payload->buf = gst_buffer_make_writable (payload->buf);

    if (G_LIKELY (!payload->keyframe)) {
      GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    if (G_UNLIKELY (stream->discont)) {
      GST_DEBUG_OBJECT (stream->pad, "marking DISCONT on stream");
      GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }

    if (G_UNLIKELY (stream->is_video && payload->par_x && payload->par_y &&
            (payload->par_x != stream->par_x) &&
            (payload->par_y != stream->par_y))) {
      GST_DEBUG ("Updating PAR (%d/%d => %d/%d)",
          stream->par_x, stream->par_y, payload->par_x, payload->par_y);
      stream->par_x = payload->par_x;
      stream->par_y = payload->par_y;
      stream->caps = gst_caps_make_writable (stream->caps);
      gst_caps_set_simple (stream->caps, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, stream->par_x, stream->par_y, NULL);
      gst_pad_set_caps (stream->pad, stream->caps);
    }

    if (G_UNLIKELY (stream->interlaced != payload->interlaced)) {
      GST_DEBUG ("Updating interlaced status (%d => %d)", stream->interlaced,
          payload->interlaced);
      stream->interlaced = payload->interlaced;
      stream->caps = gst_caps_make_writable (stream->caps);
      gst_caps_set_simple (stream->caps, "interlace-mode", G_TYPE_BOOLEAN,
          (stream->interlaced ? "mixed" : "progressive"), NULL);
      gst_pad_set_caps (stream->pad, stream->caps);
    }

    /* (sort of) interpolate timestamps using upstream "frame of reference",
     * typically useful for live src, but might (unavoidably) mess with
     * position reporting if a live src is playing not so live content
     * (e.g. rtspsrc taking some time to fall back to tcp) */
    timestamp = payload->ts;
    if (GST_CLOCK_TIME_IS_VALID (timestamp)
        && !GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)) {
      timestamp += demux->in_gap;

      /* Check if we're after the segment already, if so no need to push
       * anything here */
      if (demux->segment.stop != -1 && timestamp > demux->segment.stop) {
        GST_DEBUG_OBJECT (stream->pad,
            "Payload after segment stop %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->segment.stop));
        ret =
            gst_flow_combiner_update_pad_flow (demux->flowcombiner, stream->pad,
            GST_FLOW_EOS);
        gst_buffer_unref (payload->buf);
        payload->buf = NULL;
        g_array_remove_index (stream->payloads, 0);
        /* Break out as soon as we have an issue */
        if (G_UNLIKELY (ret != GST_FLOW_OK))
          break;

        continue;
      }
    }

    GST_BUFFER_PTS (payload->buf) = timestamp;

    if (payload->duration == GST_CLOCK_TIME_NONE
        && stream->ext_props.avg_time_per_frame != 0) {
      duration = stream->ext_props.avg_time_per_frame * 100;
    } else {
      duration = payload->duration;
    }
    GST_BUFFER_DURATION (payload->buf) = duration;

    /* FIXME: we should really set durations on buffers if we can */

    GST_LOG_OBJECT (stream->pad, "pushing buffer, %" GST_PTR_FORMAT,
        payload->buf);

    if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment) && stream->is_video) {
      if (stream->reverse_kf_ready == TRUE && stream->kf_pos == 0) {
        GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DISCONT);
      }
    } else if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)) {
      GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DISCONT);
    }


    if (stream->active) {
      if (G_UNLIKELY (stream->first_buffer)) {
        if (stream->streamheader != NULL) {
          GST_DEBUG_OBJECT (stream->pad,
              "Pushing streamheader before first buffer");
          gst_pad_push (stream->pad, gst_buffer_ref (stream->streamheader));
        }
        stream->first_buffer = FALSE;
      }

      if (GST_CLOCK_TIME_IS_VALID (timestamp)
          && timestamp > demux->segment.position) {
        demux->segment.position = timestamp;
        if (GST_CLOCK_TIME_IS_VALID (duration))
          demux->segment.position += timestamp;
      }

      ret = gst_pad_push (stream->pad, payload->buf);
      ret =
          gst_flow_combiner_update_pad_flow (demux->flowcombiner, stream->pad,
          ret);
    } else {
      gst_buffer_unref (payload->buf);
      ret = GST_FLOW_OK;
    }
    payload->buf = NULL;
    if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment) && stream->is_video
        && stream->reverse_kf_ready) {
      g_array_remove_index (stream->payloads, stream->kf_pos);
      stream->kf_pos--;

      if (stream->reverse_kf_ready == TRUE && stream->kf_pos < 0) {
        stream->kf_pos = 0;
        stream->reverse_kf_ready = FALSE;
      }
    } else {
      g_array_remove_index (stream->payloads, 0);
    }

    /* Break out as soon as we have an issue */
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      break;
  }

  return ret;
}

static gboolean
gst_asf_demux_check_buffer_is_header (GstASFDemux * demux, GstBuffer * buf)
{
  AsfObject obj;
  GstMapInfo map;
  g_assert (buf != NULL);

  GST_LOG_OBJECT (demux, "Checking if buffer is a header");

  gst_buffer_map (buf, &map, GST_MAP_READ);

  /* we return false on buffer too small */
  if (map.size < ASF_OBJECT_HEADER_SIZE) {
    gst_buffer_unmap (buf, &map);
    return FALSE;
  }

  /* check if it is a header */
  asf_demux_peek_object (demux, map.data, ASF_OBJECT_HEADER_SIZE, &obj, TRUE);
  gst_buffer_unmap (buf, &map);
  if (obj.id == ASF_OBJ_HEADER) {
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_asf_demux_check_chained_asf (GstASFDemux * demux)
{
  guint64 off = demux->data_offset + (demux->packet * demux->packet_size);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;
  gboolean header = FALSE;

  /* TODO maybe we should skip index objects after the data and look
   * further for a new header */
  if (gst_asf_demux_pull_data (demux, off, ASF_OBJECT_HEADER_SIZE, &buf, &ret)) {
    g_assert (buf != NULL);
    /* check if it is a header */
    if (gst_asf_demux_check_buffer_is_header (demux, buf)) {
      GST_DEBUG_OBJECT (demux, "new base offset: %" G_GUINT64_FORMAT, off);
      demux->base_offset = off;
      header = TRUE;
    }

    gst_buffer_unref (buf);
  }

  return header;
}

static void
gst_asf_demux_loop (GstASFDemux * demux)
{
  GstFlowReturn flow = GST_FLOW_OK;
  GstBuffer *buf = NULL;
  guint64 off;

  if (G_UNLIKELY (demux->state == GST_ASF_DEMUX_STATE_HEADER)) {
    if (!gst_asf_demux_pull_headers (demux, &flow)) {
      goto pause;
    }

    gst_asf_demux_pull_indices (demux);
  }

  g_assert (demux->state == GST_ASF_DEMUX_STATE_DATA);

  if (G_UNLIKELY (demux->num_packets != 0
          && demux->packet >= demux->num_packets))
    goto eos;

  GST_LOG_OBJECT (demux, "packet %u/%u", (guint) demux->packet + 1,
      (guint) demux->num_packets);

  off = demux->data_offset + (demux->packet * demux->packet_size);

  if (G_UNLIKELY (!gst_asf_demux_pull_data (demux, off,
              demux->packet_size * demux->speed_packets, &buf, &flow))) {
    GST_DEBUG_OBJECT (demux, "got flow %s", gst_flow_get_name (flow));
    if (flow == GST_FLOW_EOS) {
      goto eos;
    } else if (flow == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (demux, "Not fatal");
      goto pause;
    } else {
      goto read_failed;
    }
  }

  if (G_LIKELY (demux->speed_packets == 1)) {
    GstAsfDemuxParsePacketError err;
    err = gst_asf_demux_parse_packet (demux, buf);
    if (G_UNLIKELY (err != GST_ASF_DEMUX_PARSE_PACKET_ERROR_NONE)) {
      /* when we don't know when the data object ends, we should check
       * for a chained asf */
      if (demux->num_packets == 0) {
        if (gst_asf_demux_check_buffer_is_header (demux, buf)) {
          GST_INFO_OBJECT (demux, "Chained asf found");
          demux->base_offset = off;
          gst_asf_demux_reset (demux, TRUE);
          gst_buffer_unref (buf);
          return;
        }
      }
      /* FIXME: We should tally up fatal errors and error out only
       * after a few broken packets in a row? */

      GST_INFO_OBJECT (demux, "Ignoring recoverable parse error");
      gst_buffer_unref (buf);

      if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)
          && !demux->seek_to_cur_pos) {
        --demux->packet;
        if (demux->packet < 0) {
          goto eos;
        }
      } else {
        ++demux->packet;
      }

      return;
    }

    flow = gst_asf_demux_push_complete_payloads (demux, FALSE);

    if (GST_ASF_DEMUX_IS_REVERSE_PLAYBACK (demux->segment)
        && !demux->seek_to_cur_pos) {
      --demux->packet;
      if (demux->packet < 0) {
        goto eos;
      }
    } else {
      ++demux->packet;
    }

  } else {
    guint n;
    for (n = 0; n < demux->speed_packets; n++) {
      GstBuffer *sub;
      GstAsfDemuxParsePacketError err;

      sub =
          gst_buffer_copy_region (buf, GST_BUFFER_COPY_ALL,
          n * demux->packet_size, demux->packet_size);
      err = gst_asf_demux_parse_packet (demux, sub);
      if (G_UNLIKELY (err != GST_ASF_DEMUX_PARSE_PACKET_ERROR_NONE)) {
        /* when we don't know when the data object ends, we should check
         * for a chained asf */
        if (demux->num_packets == 0) {
          if (gst_asf_demux_check_buffer_is_header (demux, sub)) {
            GST_INFO_OBJECT (demux, "Chained asf found");
            demux->base_offset = off + n * demux->packet_size;
            gst_asf_demux_reset (demux, TRUE);
            gst_buffer_unref (sub);
            gst_buffer_unref (buf);
            return;
          }
        }
        /* FIXME: We should tally up fatal errors and error out only
         * after a few broken packets in a row? */

        GST_INFO_OBJECT (demux, "Ignoring recoverable parse error");
        flow = GST_FLOW_OK;
      }

      gst_buffer_unref (sub);

      if (err == GST_ASF_DEMUX_PARSE_PACKET_ERROR_NONE)
        flow = gst_asf_demux_push_complete_payloads (demux, FALSE);

      ++demux->packet;

    }

    /* reset speed pull */
    demux->speed_packets = 1;
  }

  gst_buffer_unref (buf);

  if (G_UNLIKELY ((demux->num_packets > 0
              && demux->packet >= demux->num_packets)
          || flow == GST_FLOW_EOS)) {
    GST_LOG_OBJECT (demux, "reached EOS");
    goto eos;
  }

  if (G_UNLIKELY (flow != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (demux, "pushing complete payloads failed");
    goto pause;
  }

  /* check if we're at the end of the configured segment */
  /* FIXME: check if segment end reached etc. */

  return;

eos:
  {
    /* if we haven't activated our streams yet, this might be because we have
     * less data queued than required for preroll; force stream activation and
     * send any pending payloads before sending EOS */
    if (!demux->activated_streams)
      gst_asf_demux_push_complete_payloads (demux, TRUE);

    /* we want to push an eos or post a segment-done in any case */
    if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gint64 stop;

      /* for segment playback we need to post when (in stream time)
       * we stopped, this is either stop (when set) or the duration. */
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;

      GST_INFO_OBJECT (demux, "Posting segment-done, at end of segment");
      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
              stop));
      gst_asf_demux_send_event_unlocked (demux,
          gst_event_new_segment_done (GST_FORMAT_TIME, stop));
    } else if (flow != GST_FLOW_EOS) {
      /* check if we have a chained asf, in case, we don't eos yet */
      if (gst_asf_demux_check_chained_asf (demux)) {
        GST_INFO_OBJECT (demux, "Chained ASF starting");
        gst_asf_demux_reset (demux, TRUE);
        return;
      }
    }

    if (!(demux->segment.flags & GST_SEEK_FLAG_SEGMENT)) {
      /* normal playback, send EOS to all linked pads */
      GST_INFO_OBJECT (demux, "Sending EOS, at end of stream");
      gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    }
    /* ... and fall through to pause */
  }
pause:
  {
    GST_DEBUG_OBJECT (demux, "pausing task, flow return: %s",
        gst_flow_get_name (flow));
    demux->segment_running = FALSE;
    gst_pad_pause_task (demux->sinkpad);

    /* For the error cases */
    if (flow < GST_FLOW_EOS || flow == GST_FLOW_NOT_LINKED) {
      /* Post an error. Hopefully something else already has, but if not... */
      GST_ELEMENT_FLOW_ERROR (demux, flow);
      gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    }

    return;
  }

/* ERRORS */
read_failed:
  {
    GST_DEBUG_OBJECT (demux, "Read failed, doh");
    flow = GST_FLOW_EOS;
    goto pause;
  }
#if 0
  /* See FIXMEs above */
parse_error:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("Error parsing ASF packet %u", (guint) demux->packet));
    gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    flow = GST_FLOW_ERROR;
    goto pause;
  }
#endif
}

#define GST_ASF_DEMUX_CHECK_HEADER_YES       0
#define GST_ASF_DEMUX_CHECK_HEADER_NO        1
#define GST_ASF_DEMUX_CHECK_HEADER_NEED_DATA 2

static gint
gst_asf_demux_check_header (GstASFDemux * demux)
{
  AsfObject obj;
  guint8 *cdata = (guint8 *) gst_adapter_map (demux->adapter,
      ASF_OBJECT_HEADER_SIZE);
  if (cdata == NULL)            /* need more data */
    return GST_ASF_DEMUX_CHECK_HEADER_NEED_DATA;

  asf_demux_peek_object (demux, cdata, ASF_OBJECT_HEADER_SIZE, &obj, FALSE);
  if (obj.id != ASF_OBJ_HEADER) {
    return GST_ASF_DEMUX_CHECK_HEADER_NO;
  } else {
    return GST_ASF_DEMUX_CHECK_HEADER_YES;
  }
}

static GstFlowReturn
gst_asf_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstASFDemux *demux;

  demux = GST_ASF_DEMUX (parent);

  GST_LOG_OBJECT (demux,
      "buffer: size=%" G_GSIZE_FORMAT ", offset=%" G_GINT64_FORMAT ", time=%"
      GST_TIME_FORMAT, gst_buffer_get_size (buf), GST_BUFFER_OFFSET (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  if (G_UNLIKELY (GST_BUFFER_IS_DISCONT (buf))) {
    GST_DEBUG_OBJECT (demux, "received DISCONT");
    gst_asf_demux_mark_discont (demux);
  }

  if (G_UNLIKELY ((!GST_CLOCK_TIME_IS_VALID (demux->in_gap) &&
              GST_BUFFER_TIMESTAMP_IS_VALID (buf)))) {
    demux->in_gap = GST_BUFFER_TIMESTAMP (buf) - demux->in_segment.start;
    GST_DEBUG_OBJECT (demux, "upstream segment start %" GST_TIME_FORMAT
        ", interpolation gap: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (demux->in_segment.start), GST_TIME_ARGS (demux->in_gap));
  }

  gst_adapter_push (demux->adapter, buf);

  switch (demux->state) {
    case GST_ASF_DEMUX_STATE_INDEX:{
      gint result = gst_asf_demux_check_header (demux);
      if (result == GST_ASF_DEMUX_CHECK_HEADER_NEED_DATA)       /* need more data */
        break;

      if (result == GST_ASF_DEMUX_CHECK_HEADER_NO) {
        /* we don't care about this, probably an index */
        /* TODO maybe would be smarter to skip all the indices
         * until we got a new header or EOS to decide */
        GST_LOG_OBJECT (demux, "Received index object, its EOS");
        goto eos;
      } else {
        GST_INFO_OBJECT (demux, "Chained asf starting");
        /* cleanup and get ready for a chained asf */
        gst_asf_demux_reset (demux, TRUE);
        /* fall through */
      }
    }
    case GST_ASF_DEMUX_STATE_HEADER:{
      ret = gst_asf_demux_chain_headers (demux);
      if (demux->state != GST_ASF_DEMUX_STATE_DATA)
        break;
      /* otherwise fall through */
    }
    case GST_ASF_DEMUX_STATE_DATA:
    {
      guint64 data_size;

      data_size = demux->packet_size;

      while (gst_adapter_available (demux->adapter) >= data_size) {
        GstBuffer *buf;
        GstAsfDemuxParsePacketError err;

        /* we don't know the length of the stream
         * check for a chained asf everytime */
        if (demux->num_packets == 0) {
          gint result = gst_asf_demux_check_header (demux);

          if (result == GST_ASF_DEMUX_CHECK_HEADER_YES) {
            GST_INFO_OBJECT (demux, "Chained asf starting");
            /* cleanup and get ready for a chained asf */
            gst_asf_demux_reset (demux, TRUE);
            break;
          }
        } else if (G_UNLIKELY (demux->num_packets != 0 && demux->packet >= 0
                && demux->packet >= demux->num_packets)) {
          /* do not overshoot data section when streaming */
          break;
        }

        buf = gst_adapter_take_buffer (demux->adapter, data_size);

        /* FIXME: We should tally up fatal errors and error out only
         * after a few broken packets in a row? */
        err = gst_asf_demux_parse_packet (demux, buf);

        gst_buffer_unref (buf);

        if (G_LIKELY (err == GST_ASF_DEMUX_PARSE_PACKET_ERROR_NONE))
          ret = gst_asf_demux_push_complete_payloads (demux, FALSE);
        else
          GST_WARNING_OBJECT (demux, "Parse error");

        if (demux->packet >= 0)
          ++demux->packet;
      }
      if (G_UNLIKELY (demux->num_packets != 0 && demux->packet >= 0
              && demux->packet >= demux->num_packets)) {
        demux->state = GST_ASF_DEMUX_STATE_INDEX;
      }
      break;
    }
    default:
      g_assert_not_reached ();
  }

done:
  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (demux, "flow: %s", gst_flow_get_name (ret));

  return ret;

eos:
  {
    GST_DEBUG_OBJECT (demux, "Handled last packet, setting EOS");
    ret = GST_FLOW_EOS;
    goto done;
  }
}

static inline gboolean
gst_asf_demux_skip_bytes (guint num_bytes, guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < num_bytes)
    return FALSE;

  *p_data += num_bytes;
  *p_size -= num_bytes;
  return TRUE;
}

static inline guint8
gst_asf_demux_get_uint8 (guint8 ** p_data, guint64 * p_size)
{
  guint8 ret;

  g_assert (*p_size >= 1);
  ret = GST_READ_UINT8 (*p_data);
  *p_data += sizeof (guint8);
  *p_size -= sizeof (guint8);
  return ret;
}

static inline guint16
gst_asf_demux_get_uint16 (guint8 ** p_data, guint64 * p_size)
{
  guint16 ret;

  g_assert (*p_size >= 2);
  ret = GST_READ_UINT16_LE (*p_data);
  *p_data += sizeof (guint16);
  *p_size -= sizeof (guint16);
  return ret;
}

static inline guint32
gst_asf_demux_get_uint32 (guint8 ** p_data, guint64 * p_size)
{
  guint32 ret;

  g_assert (*p_size >= 4);
  ret = GST_READ_UINT32_LE (*p_data);
  *p_data += sizeof (guint32);
  *p_size -= sizeof (guint32);
  return ret;
}

static inline guint64
gst_asf_demux_get_uint64 (guint8 ** p_data, guint64 * p_size)
{
  guint64 ret;

  g_assert (*p_size >= 8);
  ret = GST_READ_UINT64_LE (*p_data);
  *p_data += sizeof (guint64);
  *p_size -= sizeof (guint64);
  return ret;
}

static gboolean
gst_asf_demux_get_buffer (GstBuffer ** p_buf, guint num_bytes_to_read,
    guint8 ** p_data, guint64 * p_size)
{
  *p_buf = NULL;

  if (*p_size < num_bytes_to_read)
    return FALSE;

  *p_buf = gst_buffer_new_and_alloc (num_bytes_to_read);
  gst_buffer_fill (*p_buf, 0, *p_data, num_bytes_to_read);

  *p_data += num_bytes_to_read;
  *p_size -= num_bytes_to_read;

  return TRUE;
}

static gboolean
gst_asf_demux_get_bytes (guint8 ** p_buf, guint num_bytes_to_read,
    guint8 ** p_data, guint64 * p_size)
{
  *p_buf = NULL;

  if (*p_size < num_bytes_to_read)
    return FALSE;

  *p_buf = g_memdup (*p_data, num_bytes_to_read);
  *p_data += num_bytes_to_read;
  *p_size -= num_bytes_to_read;
  return TRUE;
}

static gboolean
gst_asf_demux_get_string (gchar ** p_str, guint16 * p_strlen,
    guint8 ** p_data, guint64 * p_size)
{
  guint16 s_length;
  guint8 *s;

  *p_str = NULL;

  if (*p_size < 2)
    return FALSE;

  s_length = gst_asf_demux_get_uint16 (p_data, p_size);

  if (p_strlen)
    *p_strlen = s_length;

  if (s_length == 0) {
    GST_WARNING ("zero-length string");
    *p_str = g_strdup ("");
    return TRUE;
  }

  if (!gst_asf_demux_get_bytes (&s, s_length, p_data, p_size))
    return FALSE;

  g_assert (s != NULL);

  /* just because They don't exist doesn't
   * mean They are not out to get you ... */
  if (s[s_length - 1] != '\0') {
    s = g_realloc (s, s_length + 1);
    s[s_length] = '\0';
  }

  *p_str = (gchar *) s;
  return TRUE;
}


static void
gst_asf_demux_get_guid (ASFGuid * guid, guint8 ** p_data, guint64 * p_size)
{
  g_assert (*p_size >= 4 * sizeof (guint32));

  guid->v1 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v2 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v3 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v4 = gst_asf_demux_get_uint32 (p_data, p_size);
}

static gboolean
gst_asf_demux_get_stream_audio (asf_stream_audio * audio, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (2 + 2 + 4 + 4 + 2 + 2 + 2))
    return FALSE;

  /* WAVEFORMATEX Structure */
  audio->codec_tag = gst_asf_demux_get_uint16 (p_data, p_size);
  audio->channels = gst_asf_demux_get_uint16 (p_data, p_size);
  audio->sample_rate = gst_asf_demux_get_uint32 (p_data, p_size);
  audio->byte_rate = gst_asf_demux_get_uint32 (p_data, p_size);
  audio->block_align = gst_asf_demux_get_uint16 (p_data, p_size);
  audio->word_size = gst_asf_demux_get_uint16 (p_data, p_size);
  /* Codec specific data size */
  audio->size = gst_asf_demux_get_uint16 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_stream_video (asf_stream_video * video, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (4 + 4 + 1 + 2))
    return FALSE;

  video->width = gst_asf_demux_get_uint32 (p_data, p_size);
  video->height = gst_asf_demux_get_uint32 (p_data, p_size);
  video->unknown = gst_asf_demux_get_uint8 (p_data, p_size);
  video->size = gst_asf_demux_get_uint16 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_stream_video_format (asf_stream_video_format * fmt,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (4 + 4 + 4 + 2 + 2 + 4 + 4 + 4 + 4 + 4 + 4))
    return FALSE;

  fmt->size = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->width = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->height = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->planes = gst_asf_demux_get_uint16 (p_data, p_size);
  fmt->depth = gst_asf_demux_get_uint16 (p_data, p_size);
  fmt->tag = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->image_size = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->xpels_meter = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->ypels_meter = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->num_colors = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->imp_colors = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

AsfStream *
gst_asf_demux_get_stream (GstASFDemux * demux, guint16 id)
{
  guint i;

  for (i = 0; i < demux->num_streams; i++) {
    if (demux->stream[i].id == id)
      return &demux->stream[i];
  }

  if (gst_asf_demux_is_unknown_stream (demux, id))
    GST_WARNING ("Segment found for undefined stream: (%d)", id);
  return NULL;
}

static AsfStream *
gst_asf_demux_setup_pad (GstASFDemux * demux, GstPad * src_pad,
    GstCaps * caps, guint16 id, gboolean is_video, GstBuffer * streamheader,
    GstTagList * tags)
{
  AsfStream *stream;

  gst_pad_use_fixed_caps (src_pad);
  gst_pad_set_caps (src_pad, caps);

  gst_pad_set_event_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_handle_src_event));
  gst_pad_set_query_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_handle_src_query));

  stream = &demux->stream[demux->num_streams];
  stream->caps = caps;
  stream->pad = src_pad;
  stream->id = id;
  stream->fps_known = !is_video;        /* bit hacky for audio */
  stream->is_video = is_video;
  stream->pending_tags = tags;
  stream->discont = TRUE;
  stream->first_buffer = TRUE;
  stream->streamheader = streamheader;
  if (stream->streamheader) {
    stream->streamheader = gst_buffer_make_writable (streamheader);
    GST_BUFFER_FLAG_SET (stream->streamheader, GST_BUFFER_FLAG_HEADER);
  }
  if (is_video) {
    GstStructure *st;
    gint par_x, par_y;
    st = gst_caps_get_structure (caps, 0);
    if (gst_structure_get_fraction (st, "pixel-aspect-ratio", &par_x, &par_y) &&
        par_x > 0 && par_y > 0) {
      GST_DEBUG ("PAR %d/%d", par_x, par_y);
      stream->par_x = par_x;
      stream->par_y = par_y;
    }
  }

  stream->payloads = g_array_new (FALSE, FALSE, sizeof (AsfPayload));

  /* TODO: create this array during reverse play? */
  stream->payloads_rev = g_array_new (FALSE, FALSE, sizeof (AsfPayload));

  GST_INFO ("Created pad %s for stream %u with caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (src_pad), demux->num_streams, caps);

  ++demux->num_streams;

  stream->active = FALSE;

  return stream;
}

static void
gst_asf_demux_add_stream_headers_to_caps (GstASFDemux * demux,
    GstBuffer * buffer, GstStructure * structure)
{
  GValue arr_val = G_VALUE_INIT;
  GValue buf_val = G_VALUE_INIT;

  g_value_init (&arr_val, GST_TYPE_ARRAY);
  g_value_init (&buf_val, GST_TYPE_BUFFER);

  gst_value_set_buffer (&buf_val, buffer);
  gst_value_array_append_and_take_value (&arr_val, &buf_val);

  gst_structure_take_value (structure, "streamheader", &arr_val);
}

static AsfStream *
gst_asf_demux_add_audio_stream (GstASFDemux * demux,
    asf_stream_audio * audio, guint16 id, guint8 ** p_data, guint64 * p_size)
{
  GstTagList *tags = NULL;
  GstBuffer *extradata = NULL;
  GstPad *src_pad;
  GstCaps *caps;
  guint16 size_left = 0;
  gchar *codec_name = NULL;
  gchar *name = NULL;

  size_left = audio->size;

  /* Create the audio pad */
  name = g_strdup_printf ("audio_%u", demux->num_audio_streams);

  src_pad = gst_pad_new_from_static_template (&audio_src_template, name);
  g_free (name);

  /* Swallow up any left over data and set up the 
   * standard properties from the header info */
  if (size_left) {
    GST_INFO_OBJECT (demux, "Audio header contains %d bytes of "
        "codec specific data", size_left);

    g_assert (size_left <= *p_size);
    gst_asf_demux_get_buffer (&extradata, size_left, p_data, p_size);
  }

  /* asf_stream_audio is the same as gst_riff_strf_auds, but with an
   * additional two bytes indicating extradata. */
  /* FIXME: Handle the channel reorder map here */
  caps = gst_riff_create_audio_caps (audio->codec_tag, NULL,
      (gst_riff_strf_auds *) audio, extradata, NULL, &codec_name, NULL);

  if (caps == NULL) {
    caps = gst_caps_new_simple ("audio/x-asf-unknown", "codec_id",
        G_TYPE_INT, (gint) audio->codec_tag, NULL);
  }

  /* Informing about that audio format we just added */
  if (codec_name) {
    tags = gst_tag_list_new (GST_TAG_AUDIO_CODEC, codec_name, NULL);
    g_free (codec_name);
  }

  if (audio->byte_rate > 0) {
    /* Some ASF files have no bitrate props object (often seen with
     * ASF files that contain raw audio data). Example files can
     * be generated with FFmpeg (tested with v2.8.6), like this:
     *
     *   ffmpeg -i sine-wave.wav -c:a pcm_alaw file.asf
     *
     * In this case, if audio->byte_rate is nonzero, use that as
     * the bitrate. */

    guint bitrate = audio->byte_rate * 8;

    if (tags == NULL)
      tags = gst_tag_list_new_empty ();

    /* Add bitrate, but only if there is none set already, since
     * this is just a fallback in case there is no bitrate tag
     * already present */
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP, GST_TAG_BITRATE, bitrate, NULL);
  }

  if (extradata)
    gst_buffer_unref (extradata);

  GST_INFO ("Adding audio stream #%u, id %u codec %u (0x%04x), tags=%"
      GST_PTR_FORMAT, demux->num_audio_streams, id, audio->codec_tag,
      audio->codec_tag, tags);

  ++demux->num_audio_streams;

  return gst_asf_demux_setup_pad (demux, src_pad, caps, id, FALSE, NULL, tags);
}

static AsfStream *
gst_asf_demux_add_video_stream (GstASFDemux * demux,
    asf_stream_video_format * video, guint16 id,
    guint8 ** p_data, guint64 * p_size)
{
  GstTagList *tags = NULL;
  GstStructure *caps_s;
  GstBuffer *extradata = NULL;
  GstPad *src_pad;
  GstCaps *caps;
  gchar *str;
  gchar *name = NULL;
  gchar *codec_name = NULL;
  gint size_left = video->size - 40;
  GstBuffer *streamheader = NULL;
  guint par_w = 1, par_h = 1;

  /* Create the video pad */
  name = g_strdup_printf ("video_%u", demux->num_video_streams);
  src_pad = gst_pad_new_from_static_template (&video_src_template, name);
  g_free (name);

  /* Now try some gstreamer formatted MIME types (from gst_avi_demux_strf_vids) */
  if (size_left) {
    GST_LOG ("Video header has %d bytes of codec specific data", size_left);
    g_assert (size_left <= *p_size);
    gst_asf_demux_get_buffer (&extradata, size_left, p_data, p_size);
  }

  GST_DEBUG ("video codec %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (video->tag));

  /* yes, asf_stream_video_format and gst_riff_strf_vids are the same */
  caps = gst_riff_create_video_caps (video->tag, NULL,
      (gst_riff_strf_vids *) video, extradata, NULL, &codec_name);

  if (caps == NULL) {
    caps = gst_caps_new_simple ("video/x-asf-unknown", "fourcc",
        G_TYPE_UINT, video->tag, NULL);
  } else {
    GstStructure *s;
    gint ax, ay;

    s = gst_asf_demux_get_metadata_for_stream (demux, id);
    if (gst_structure_get_int (s, "AspectRatioX", &ax) &&
        gst_structure_get_int (s, "AspectRatioY", &ay) && (ax > 0 && ay > 0)) {
      par_w = ax;
      par_h = ay;
      gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          ax, ay, NULL);
    } else {
      guint ax, ay;
      /* retry with the global metadata */
      GST_DEBUG ("Retrying with global metadata %" GST_PTR_FORMAT,
          demux->global_metadata);
      s = demux->global_metadata;
      if (gst_structure_get_uint (s, "AspectRatioX", &ax) &&
          gst_structure_get_uint (s, "AspectRatioY", &ay)) {
        GST_DEBUG ("ax:%d, ay:%d", ax, ay);
        if (ax > 0 && ay > 0) {
          par_w = ax;
          par_h = ay;
          gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              ax, ay, NULL);
        }
      }
    }
    s = gst_caps_get_structure (caps, 0);
    gst_structure_remove_field (s, "framerate");
  }

  caps_s = gst_caps_get_structure (caps, 0);

  /* add format field with fourcc to WMV/VC1 caps to differentiate variants */
  if (gst_structure_has_name (caps_s, "video/x-wmv")) {
    str = g_strdup_printf ("%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (video->tag));
    gst_caps_set_simple (caps, "format", G_TYPE_STRING, str, NULL);
    g_free (str);

    /* check if h264 has codec_data (avc) or streamheaders (bytestream) */
  } else if (gst_structure_has_name (caps_s, "video/x-h264")) {
    const GValue *value = gst_structure_get_value (caps_s, "codec_data");
    if (value) {
      GstBuffer *buf = gst_value_get_buffer (value);
      GstMapInfo mapinfo;

      if (gst_buffer_map (buf, &mapinfo, GST_MAP_READ)) {
        if (mapinfo.size >= 4 && GST_READ_UINT32_BE (mapinfo.data) == 1) {
          /* this looks like a bytestream start */
          streamheader = gst_buffer_ref (buf);
          gst_asf_demux_add_stream_headers_to_caps (demux, buf, caps_s);
          gst_structure_remove_field (caps_s, "codec_data");
        }

        gst_buffer_unmap (buf, &mapinfo);
      }
    }
  }

  /* For a 3D video, set multiview information into the caps based on
   * what was detected during object parsing */
  if (demux->asf_3D_mode != GST_ASF_3D_NONE) {
    GstVideoMultiviewMode mv_mode = GST_VIDEO_MULTIVIEW_MODE_NONE;
    GstVideoMultiviewFlags mv_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
    const gchar *mview_mode_str;

    switch (demux->asf_3D_mode) {
      case GST_ASF_3D_SIDE_BY_SIDE_HALF_LR:
        mv_mode = GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE;
        break;
      case GST_ASF_3D_SIDE_BY_SIDE_HALF_RL:
        mv_mode = GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE;
        mv_flags = GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST;
        break;
      case GST_ASF_3D_TOP_AND_BOTTOM_HALF_LR:
        mv_mode = GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM;
        break;
      case GST_ASF_3D_TOP_AND_BOTTOM_HALF_RL:
        mv_mode = GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM;
        mv_flags = GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST;
        break;
      case GST_ASF_3D_DUAL_STREAM:{
        gboolean is_right_view = FALSE;
        /* if Advanced_Mutual_Exclusion object exists, use it
         * to figure out which is the left view (lower ID) */
        if (demux->mut_ex_streams != NULL) {
          guint length;
          gint i;

          length = g_slist_length (demux->mut_ex_streams);

          for (i = 0; i < length; i++) {
            gpointer v_s_id;

            v_s_id = g_slist_nth_data (demux->mut_ex_streams, i);

            GST_DEBUG_OBJECT (demux,
                "has Mutual_Exclusion object. stream id in object is %d",
                GPOINTER_TO_INT (v_s_id));

            if (id > GPOINTER_TO_INT (v_s_id))
              is_right_view = TRUE;
          }
        } else {
          /* if the Advaced_Mutual_Exclusion object doesn't exist, assume the
           * first video stream encountered has the lower ID */
          if (demux->num_video_streams > 0) {
            /* This is not the first video stream, assuming right eye view */
            is_right_view = TRUE;
          }
        }
        if (is_right_view)
          mv_mode = GST_VIDEO_MULTIVIEW_MODE_RIGHT;
        else
          mv_mode = GST_VIDEO_MULTIVIEW_MODE_LEFT;
        break;
      }
      default:
        break;
    }

    GST_INFO_OBJECT (demux,
        "stream_id %d, has multiview-mode %d flags 0x%x", id, mv_mode,
        (guint) mv_flags);

    mview_mode_str = gst_video_multiview_mode_to_caps_string (mv_mode);
    if (mview_mode_str != NULL) {
      if (gst_video_multiview_guess_half_aspect (mv_mode, video->width,
              video->height, par_w, par_h))
        mv_flags |= GST_VIDEO_MULTIVIEW_FLAGS_HALF_ASPECT;

      gst_caps_set_simple (caps,
          "multiview-mode", G_TYPE_STRING, mview_mode_str,
          "multiview-flags", GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, mv_flags,
          GST_FLAG_SET_MASK_EXACT, NULL);
    }
  }

  if (codec_name) {
    tags = gst_tag_list_new (GST_TAG_VIDEO_CODEC, codec_name, NULL);
    g_free (codec_name);
  }

  if (extradata)
    gst_buffer_unref (extradata);

  GST_INFO ("Adding video stream #%u, id %u, codec %"
      GST_FOURCC_FORMAT " (0x%08x)", demux->num_video_streams, id,
      GST_FOURCC_ARGS (video->tag), video->tag);

  ++demux->num_video_streams;

  return gst_asf_demux_setup_pad (demux, src_pad, caps, id, TRUE,
      streamheader, tags);
}

static void
gst_asf_demux_activate_stream (GstASFDemux * demux, AsfStream * stream)
{
  if (!stream->active) {
    GstEvent *event;
    gchar *stream_id;

    GST_INFO_OBJECT (demux, "Activating stream %2u, pad %s, caps %"
        GST_PTR_FORMAT, stream->id, GST_PAD_NAME (stream->pad), stream->caps);
    gst_pad_set_active (stream->pad, TRUE);

    stream_id =
        gst_pad_create_stream_id_printf (stream->pad, GST_ELEMENT_CAST (demux),
        "%03u", stream->id);

    event =
        gst_pad_get_sticky_event (demux->sinkpad, GST_EVENT_STREAM_START, 0);
    if (event) {
      if (gst_event_parse_group_id (event, &demux->group_id))
        demux->have_group_id = TRUE;
      else
        demux->have_group_id = FALSE;
      gst_event_unref (event);
    } else if (!demux->have_group_id) {
      demux->have_group_id = TRUE;
      demux->group_id = gst_util_group_id_next ();
    }

    event = gst_event_new_stream_start (stream_id);
    if (demux->have_group_id)
      gst_event_set_group_id (event, demux->group_id);

    gst_pad_push_event (stream->pad, event);
    g_free (stream_id);
    gst_pad_set_caps (stream->pad, stream->caps);

    gst_element_add_pad (GST_ELEMENT_CAST (demux), stream->pad);
    gst_flow_combiner_add_pad (demux->flowcombiner, stream->pad);
    stream->active = TRUE;
  }
}

static AsfStream *
gst_asf_demux_parse_stream_object (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  AsfCorrectionType correction_type;
  AsfStreamType stream_type;
  GstClockTime time_offset;
  gboolean is_encrypted G_GNUC_UNUSED;
  guint16 stream_id;
  guint16 flags;
  ASFGuid guid;
  guint stream_specific_size;
  guint type_specific_size G_GNUC_UNUSED;
  guint unknown G_GNUC_UNUSED;
  gboolean inspect_payload = FALSE;
  AsfStream *stream = NULL;

  /* Get the rest of the header's header */
  if (size < (16 + 16 + 8 + 4 + 4 + 2 + 4))
    goto not_enough_data;

  gst_asf_demux_get_guid (&guid, &data, &size);
  stream_type = gst_asf_demux_identify_guid (asf_stream_guids, &guid);

  gst_asf_demux_get_guid (&guid, &data, &size);
  correction_type = gst_asf_demux_identify_guid (asf_correction_guids, &guid);

  time_offset = gst_asf_demux_get_uint64 (&data, &size) * 100;

  type_specific_size = gst_asf_demux_get_uint32 (&data, &size);
  stream_specific_size = gst_asf_demux_get_uint32 (&data, &size);

  flags = gst_asf_demux_get_uint16 (&data, &size);
  stream_id = flags & 0x7f;
  is_encrypted = ! !((flags & 0x8000) << 15);
  unknown = gst_asf_demux_get_uint32 (&data, &size);

  GST_DEBUG_OBJECT (demux, "Found stream %u, time_offset=%" GST_TIME_FORMAT,
      stream_id, GST_TIME_ARGS (time_offset));

  /* dvr-ms has audio stream declared in stream specific data */
  if (stream_type == ASF_STREAM_EXT_EMBED_HEADER) {
    AsfExtStreamType ext_stream_type;
    gst_asf_demux_get_guid (&guid, &data, &size);
    ext_stream_type = gst_asf_demux_identify_guid (asf_ext_stream_guids, &guid);

    if (ext_stream_type == ASF_EXT_STREAM_AUDIO) {
      inspect_payload = TRUE;

      gst_asf_demux_get_guid (&guid, &data, &size);
      gst_asf_demux_get_uint32 (&data, &size);
      gst_asf_demux_get_uint32 (&data, &size);
      gst_asf_demux_get_uint32 (&data, &size);
      gst_asf_demux_get_guid (&guid, &data, &size);
      gst_asf_demux_get_uint32 (&data, &size);
      stream_type = ASF_STREAM_AUDIO;
    }
  }

  switch (stream_type) {
    case ASF_STREAM_AUDIO:{
      asf_stream_audio audio_object;

      if (!gst_asf_demux_get_stream_audio (&audio_object, &data, &size))
        goto not_enough_data;

      GST_INFO ("Object is an audio stream with %u bytes of additional data",
          audio_object.size);

      stream = gst_asf_demux_add_audio_stream (demux, &audio_object, stream_id,
          &data, &size);

      switch (correction_type) {
        case ASF_CORRECTION_ON:{
          guint span, packet_size, chunk_size, data_size, silence_data;

          GST_INFO ("Using error correction");

          if (size < (1 + 2 + 2 + 2 + 1))
            goto not_enough_data;

          span = gst_asf_demux_get_uint8 (&data, &size);
          packet_size = gst_asf_demux_get_uint16 (&data, &size);
          chunk_size = gst_asf_demux_get_uint16 (&data, &size);
          data_size = gst_asf_demux_get_uint16 (&data, &size);
          silence_data = gst_asf_demux_get_uint8 (&data, &size);

          stream->span = span;

          GST_DEBUG_OBJECT (demux, "Descrambling ps:%u cs:%u ds:%u s:%u sd:%u",
              packet_size, chunk_size, data_size, span, silence_data);

          if (stream->span > 1) {
            if (chunk_size == 0 || ((packet_size / chunk_size) <= 1)) {
              /* Disable descrambling */
              stream->span = 0;
            } else {
              /* FIXME: this else branch was added for
               * weird_al_yankovic - the saga begins.asf */
              stream->ds_packet_size = packet_size;
              stream->ds_chunk_size = chunk_size;
            }
          } else {
            /* Descambling is enabled */
            stream->ds_packet_size = packet_size;
            stream->ds_chunk_size = chunk_size;
          }
#if 0
          /* Now skip the rest of the silence data */
          if (data_size > 1)
            gst_bytestream_flush (demux->bs, data_size - 1);
#else
          /* FIXME: CHECKME. And why -1? */
          if (data_size > 1) {
            if (!gst_asf_demux_skip_bytes (data_size - 1, &data, &size)) {
              goto not_enough_data;
            }
          }
#endif
          break;
        }
        case ASF_CORRECTION_OFF:{
          GST_INFO ("Error correction off");
          if (!gst_asf_demux_skip_bytes (stream_specific_size, &data, &size))
            goto not_enough_data;
          break;
        }
        default:
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
              ("Audio stream using unknown error correction"));
          return NULL;
      }

      break;
    }

    case ASF_STREAM_VIDEO:{
      asf_stream_video_format video_format_object;
      asf_stream_video video_object;
      guint16 vsize;

      if (!gst_asf_demux_get_stream_video (&video_object, &data, &size))
        goto not_enough_data;

      vsize = video_object.size - 40;   /* Byte order gets offset by single byte */

      GST_INFO ("object is a video stream with %u bytes of "
          "additional data", vsize);

      if (!gst_asf_demux_get_stream_video_format (&video_format_object,
              &data, &size)) {
        goto not_enough_data;
      }

      stream = gst_asf_demux_add_video_stream (demux, &video_format_object,
          stream_id, &data, &size);

      break;
    }

    default:
      GST_WARNING_OBJECT (demux, "Unknown stream type for stream %u",
          stream_id);
      demux->other_streams =
          g_slist_append (demux->other_streams, GINT_TO_POINTER (stream_id));
      break;
  }

  if (stream)
    stream->inspect_payload = inspect_payload;
  return stream;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "Unexpected end of data parsing stream object");
    /* we'll error out later if we found no streams */
    return NULL;
  }
}

static const gchar *
gst_asf_demux_get_gst_tag_from_tag_name (const gchar * name_utf8)
{
  const struct
  {
    const gchar *asf_name;
    const gchar *gst_name;
  } tags[] = {
    {
    "WM/Genre", GST_TAG_GENRE}, {
    "WM/AlbumTitle", GST_TAG_ALBUM}, {
    "WM/AlbumArtist", GST_TAG_ARTIST}, {
    "WM/Picture", GST_TAG_IMAGE}, {
    "WM/Track", GST_TAG_TRACK_NUMBER}, {
    "WM/TrackNumber", GST_TAG_TRACK_NUMBER}, {
    "WM/Year", GST_TAG_DATE_TIME}
    /* { "WM/Composer", GST_TAG_COMPOSER } */
  };
  gsize out;
  guint i;

  if (name_utf8 == NULL) {
    GST_WARNING ("Failed to convert name to UTF8, skipping");
    return NULL;
  }

  out = strlen (name_utf8);

  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (strncmp (tags[i].asf_name, name_utf8, out) == 0) {
      GST_LOG ("map tagname '%s' -> '%s'", name_utf8, tags[i].gst_name);
      return tags[i].gst_name;
    }
  }

  return NULL;
}

/* gst_asf_demux_add_global_tags() takes ownership of taglist! */
static void
gst_asf_demux_add_global_tags (GstASFDemux * demux, GstTagList * taglist)
{
  GstTagList *t;

  GST_DEBUG_OBJECT (demux, "adding global tags: %" GST_PTR_FORMAT, taglist);

  if (taglist == NULL)
    return;

  if (gst_tag_list_is_empty (taglist)) {
    gst_tag_list_unref (taglist);
    return;
  }

  t = gst_tag_list_merge (demux->taglist, taglist, GST_TAG_MERGE_APPEND);
  gst_tag_list_set_scope (t, GST_TAG_SCOPE_GLOBAL);
  if (demux->taglist)
    gst_tag_list_unref (demux->taglist);
  gst_tag_list_unref (taglist);
  demux->taglist = t;
  GST_LOG_OBJECT (demux, "global tags now: %" GST_PTR_FORMAT, demux->taglist);
}

#define ASF_DEMUX_DATA_TYPE_UTF16LE_STRING  0
#define ASF_DEMUX_DATA_TYPE_BYTE_ARRAY      1
#define ASF_DEMUX_DATA_TYPE_BOOL			2
#define ASF_DEMUX_DATA_TYPE_DWORD           3

static void
asf_demux_parse_picture_tag (GstTagList * tags, const guint8 * tag_data,
    guint tag_data_len)
{
  GstByteReader r;
  const guint8 *img_data = NULL;
  guint32 img_data_len = 0;
  guint8 pic_type = 0;

  gst_byte_reader_init (&r, tag_data, tag_data_len);

  /* skip mime type string (we don't trust it and do our own typefinding),
   * and also skip the description string, since we don't use it */
  if (!gst_byte_reader_get_uint8 (&r, &pic_type) ||
      !gst_byte_reader_get_uint32_le (&r, &img_data_len) ||
      !gst_byte_reader_skip_string_utf16 (&r) ||
      !gst_byte_reader_skip_string_utf16 (&r) ||
      !gst_byte_reader_get_data (&r, img_data_len, &img_data)) {
    goto not_enough_data;
  }


  if (!gst_tag_list_add_id3_image (tags, img_data, img_data_len, pic_type))
    GST_DEBUG ("failed to add image extracted from WM/Picture tag to taglist");

  return;

not_enough_data:
  {
    GST_DEBUG ("Failed to read WM/Picture tag: not enough data");
    GST_MEMDUMP ("WM/Picture data", tag_data, tag_data_len);
    return;
  }
}

/* Extended Content Description Object */
static GstFlowReturn
gst_asf_demux_process_ext_content_desc (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  /* Other known (and unused) 'text/unicode' metadata available :
   *
   *   WM/Lyrics =
   *   WM/MediaPrimaryClassID = {D1607DBC-E323-4BE2-86A1-48A42A28441E}
   *   WMFSDKVersion = 9.00.00.2980
   *   WMFSDKNeeded = 0.0.0.0000
   *   WM/UniqueFileIdentifier = AMGa_id=R    15334;AMGp_id=P     5149;AMGt_id=T  2324984
   *   WM/Publisher = 4AD
   *   WM/Provider = AMG
   *   WM/ProviderRating = 8
   *   WM/ProviderStyle = Rock (similar to WM/Genre)
   *   WM/GenreID (similar to WM/Genre)
   *   WM/TrackNumber (same as WM/Track but as a string)
   *
   * Other known (and unused) 'non-text' metadata available :
   *
   *   WM/EncodingTime
   *   WM/MCDI
   *   IsVBR
   *
   * We might want to read WM/TrackNumber and use atoi() if we don't have
   * WM/Track
   */

  GstTagList *taglist;
  guint16 blockcount, i;
  gboolean content3D = FALSE;

  struct
  {
    const gchar *interleave_name;
    GstASF3DMode interleaving_type;
  } stereoscopic_layout_map[] = {
    {
    "SideBySideRF", GST_ASF_3D_SIDE_BY_SIDE_HALF_RL}, {
    "SideBySideLF", GST_ASF_3D_SIDE_BY_SIDE_HALF_LR}, {
    "OverUnderRT", GST_ASF_3D_TOP_AND_BOTTOM_HALF_RL}, {
    "OverUnderLT", GST_ASF_3D_TOP_AND_BOTTOM_HALF_LR}, {
    "DualStream", GST_ASF_3D_DUAL_STREAM}
  };
  GST_INFO_OBJECT (demux, "object is an extended content description");

  taglist = gst_tag_list_new_empty ();

  /* Content Descriptor Count */
  if (size < 2)
    goto not_enough_data;

  blockcount = gst_asf_demux_get_uint16 (&data, &size);

  for (i = 1; i <= blockcount; ++i) {
    const gchar *gst_tag_name;
    guint16 datatype;
    guint16 value_len;
    guint16 name_len;
    GValue tag_value = { 0, };
    gsize in, out;
    gchar *name;
    gchar *name_utf8 = NULL;
    gchar *value;

    /* Descriptor */
    if (!gst_asf_demux_get_string (&name, &name_len, &data, &size))
      goto not_enough_data;

    if (size < 2) {
      g_free (name);
      goto not_enough_data;
    }
    /* Descriptor Value Data Type */
    datatype = gst_asf_demux_get_uint16 (&data, &size);

    /* Descriptor Value (not really a string, but same thing reading-wise) */
    if (!gst_asf_demux_get_string (&value, &value_len, &data, &size)) {
      g_free (name);
      goto not_enough_data;
    }

    name_utf8 =
        g_convert (name, name_len, "UTF-8", "UTF-16LE", &in, &out, NULL);

    if (name_utf8 != NULL) {
      GST_DEBUG ("Found tag/metadata %s", name_utf8);

      gst_tag_name = gst_asf_demux_get_gst_tag_from_tag_name (name_utf8);
      GST_DEBUG ("gst_tag_name %s", GST_STR_NULL (gst_tag_name));

      switch (datatype) {
        case ASF_DEMUX_DATA_TYPE_UTF16LE_STRING:{
          gchar *value_utf8;

          value_utf8 = g_convert (value, value_len, "UTF-8", "UTF-16LE",
              &in, &out, NULL);

          /* get rid of tags with empty value */
          if (value_utf8 != NULL && *value_utf8 != '\0') {
            GST_DEBUG ("string value %s", value_utf8);

            value_utf8[out] = '\0';

            if (gst_tag_name != NULL) {
              if (strcmp (gst_tag_name, GST_TAG_DATE_TIME) == 0) {
                guint year = atoi (value_utf8);

                if (year > 0) {
                  g_value_init (&tag_value, GST_TYPE_DATE_TIME);
                  g_value_take_boxed (&tag_value, gst_date_time_new_y (year));
                }
              } else if (strcmp (gst_tag_name, GST_TAG_GENRE) == 0) {
                guint id3v1_genre_id;
                const gchar *genre_str;

                if (sscanf (value_utf8, "(%u)", &id3v1_genre_id) == 1 &&
                    ((genre_str = gst_tag_id3_genre_get (id3v1_genre_id)))) {
                  GST_DEBUG ("Genre: %s -> %s", value_utf8, genre_str);
                  g_free (value_utf8);
                  value_utf8 = g_strdup (genre_str);
                }
              } else {
                GType tag_type;

                /* convert tag from string to other type if required */
                tag_type = gst_tag_get_type (gst_tag_name);
                g_value_init (&tag_value, tag_type);
                if (!gst_value_deserialize (&tag_value, value_utf8)) {
                  GValue from_val = { 0, };

                  g_value_init (&from_val, G_TYPE_STRING);
                  g_value_set_string (&from_val, value_utf8);
                  if (!g_value_transform (&from_val, &tag_value)) {
                    GST_WARNING_OBJECT (demux,
                        "Could not transform string tag to " "%s tag type %s",
                        gst_tag_name, g_type_name (tag_type));
                    g_value_unset (&tag_value);
                  }
                  g_value_unset (&from_val);
                }
              }
            } else {
              /* metadata ! */
              GST_DEBUG ("Setting metadata");
              g_value_init (&tag_value, G_TYPE_STRING);
              g_value_set_string (&tag_value, value_utf8);
              /* If we found a stereoscopic marker, look for StereoscopicLayout
               * metadata */
              if (content3D) {
                guint i;
                if (strncmp ("StereoscopicLayout", name_utf8,
                        strlen (name_utf8)) == 0) {
                  for (i = 0; i < G_N_ELEMENTS (stereoscopic_layout_map); i++) {
                    if (g_str_equal (stereoscopic_layout_map[i].interleave_name,
                            value_utf8)) {
                      demux->asf_3D_mode =
                          stereoscopic_layout_map[i].interleaving_type;
                      GST_INFO ("find interleave type %u", demux->asf_3D_mode);
                    }
                  }
                }
                GST_INFO_OBJECT (demux, "3d type is %u", demux->asf_3D_mode);
              } else {
                demux->asf_3D_mode = GST_ASF_3D_NONE;
                GST_INFO_OBJECT (demux, "None 3d type");
              }
            }
          } else if (value_utf8 == NULL) {
            GST_WARNING ("Failed to convert string value to UTF8, skipping");
          } else {
            GST_DEBUG ("Skipping empty string value for %s",
                GST_STR_NULL (gst_tag_name));
          }
          g_free (value_utf8);
          break;
        }
        case ASF_DEMUX_DATA_TYPE_BYTE_ARRAY:{
          if (gst_tag_name) {
            if (!g_str_equal (gst_tag_name, GST_TAG_IMAGE)) {
              GST_FIXME ("Unhandled byte array tag %s",
                  GST_STR_NULL (gst_tag_name));
              break;
            } else {
              asf_demux_parse_picture_tag (taglist, (guint8 *) value,
                  value_len);
            }
          }
          break;
        }
        case ASF_DEMUX_DATA_TYPE_DWORD:{
          guint uint_val = GST_READ_UINT32_LE (value);

          /* this is the track number */
          g_value_init (&tag_value, G_TYPE_UINT);

          /* WM/Track counts from 0 */
          if (!strcmp (name_utf8, "WM/Track"))
            ++uint_val;

          g_value_set_uint (&tag_value, uint_val);
          break;
        }
          /* Detect 3D */
        case ASF_DEMUX_DATA_TYPE_BOOL:{
          gboolean bool_val = GST_READ_UINT32_LE (value);

          if (strncmp ("Stereoscopic", name_utf8, strlen (name_utf8)) == 0) {
            if (bool_val) {
              GST_INFO_OBJECT (demux, "This is 3D contents");
              content3D = TRUE;
            } else {
              GST_INFO_OBJECT (demux, "This is not 3D contenst");
              content3D = FALSE;
            }
          }

          break;
        }
        default:{
          GST_DEBUG ("Skipping tag %s of type %d", gst_tag_name, datatype);
          break;
        }
      }

      if (G_IS_VALUE (&tag_value)) {
        if (gst_tag_name) {
          GstTagMergeMode merge_mode = GST_TAG_MERGE_APPEND;

          /* WM/TrackNumber is more reliable than WM/Track, since the latter
           * is supposed to have a 0 base but is often wrongly written to start
           * from 1 as well, so prefer WM/TrackNumber when we have it: either
           * replace the value added earlier from WM/Track or put it first in
           * the list, so that it will get picked up by _get_uint() */
          if (strcmp (name_utf8, "WM/TrackNumber") == 0)
            merge_mode = GST_TAG_MERGE_REPLACE;

          gst_tag_list_add_values (taglist, merge_mode, gst_tag_name,
              &tag_value, NULL);
        } else {
          GST_DEBUG ("Setting global metadata %s", name_utf8);
          gst_structure_set_value (demux->global_metadata, name_utf8,
              &tag_value);
        }

        g_value_unset (&tag_value);
      }
    }

    g_free (name);
    g_free (value);
    g_free (name_utf8);
  }

  gst_asf_demux_add_global_tags (demux, taglist);

  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING ("Unexpected end of data parsing ext content desc object");
    gst_tag_list_unref (taglist);
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstStructure *
gst_asf_demux_get_metadata_for_stream (GstASFDemux * demux, guint stream_num)
{
  gchar sname[32];
  guint i;

  g_snprintf (sname, sizeof (sname), "stream-%u", stream_num);

  for (i = 0; i < gst_caps_get_size (demux->metadata); ++i) {
    GstStructure *s;

    s = gst_caps_get_structure (demux->metadata, i);
    if (gst_structure_has_name (s, sname))
      return s;
  }

  gst_caps_append_structure (demux->metadata, gst_structure_new_empty (sname));

  /* try lookup again; demux->metadata took ownership of the structure, so we
   * can't really make any assumptions about what happened to it, so we can't
   * just return it directly after appending it */
  return gst_asf_demux_get_metadata_for_stream (demux, stream_num);
}

static GstFlowReturn
gst_asf_demux_process_metadata (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  guint16 blockcount, i;

  GST_INFO_OBJECT (demux, "object is a metadata object");

  /* Content Descriptor Count */
  if (size < 2)
    goto not_enough_data;

  blockcount = gst_asf_demux_get_uint16 (&data, &size);

  for (i = 0; i < blockcount; ++i) {
    GstStructure *s;
    guint16 stream_num, name_len, data_type, lang_idx G_GNUC_UNUSED;
    guint32 data_len, ival;
    gchar *name_utf8;

    if (size < (2 + 2 + 2 + 2 + 4))
      goto not_enough_data;

    lang_idx = gst_asf_demux_get_uint16 (&data, &size);
    stream_num = gst_asf_demux_get_uint16 (&data, &size);
    name_len = gst_asf_demux_get_uint16 (&data, &size);
    data_type = gst_asf_demux_get_uint16 (&data, &size);
    data_len = gst_asf_demux_get_uint32 (&data, &size);

    if (size < name_len + data_len)
      goto not_enough_data;

    /* convert name to UTF-8 */
    name_utf8 = g_convert ((gchar *) data, name_len, "UTF-8", "UTF-16LE",
        NULL, NULL, NULL);
    gst_asf_demux_skip_bytes (name_len, &data, &size);

    if (name_utf8 == NULL) {
      GST_WARNING ("Failed to convert value name to UTF8, skipping");
      gst_asf_demux_skip_bytes (data_len, &data, &size);
      continue;
    }

    if (data_type != ASF_DEMUX_DATA_TYPE_DWORD) {
      gst_asf_demux_skip_bytes (data_len, &data, &size);
      g_free (name_utf8);
      continue;
    }

    /* read DWORD */
    if (size < 4) {
      g_free (name_utf8);
      goto not_enough_data;
    }

    ival = gst_asf_demux_get_uint32 (&data, &size);

    /* skip anything else there may be, just in case */
    gst_asf_demux_skip_bytes (data_len - 4, &data, &size);

    s = gst_asf_demux_get_metadata_for_stream (demux, stream_num);
    gst_structure_set (s, name_utf8, G_TYPE_INT, ival, NULL);
    g_free (name_utf8);
  }

  GST_INFO_OBJECT (demux, "metadata = %" GST_PTR_FORMAT, demux->metadata);
  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING ("Unexpected end of data parsing metadata object");
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_header (GstASFDemux * demux, guint8 * data, guint64 size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 i, num_objects;
  guint8 unknown G_GNUC_UNUSED;

  /* Get the rest of the header's header */
  if (size < (4 + 1 + 1))
    goto not_enough_data;

  num_objects = gst_asf_demux_get_uint32 (&data, &size);
  unknown = gst_asf_demux_get_uint8 (&data, &size);
  unknown = gst_asf_demux_get_uint8 (&data, &size);

  GST_INFO_OBJECT (demux, "object is a header with %u parts", num_objects);

  /* Loop through the header's objects, processing those */
  for (i = 0; i < num_objects; ++i) {
    GST_INFO_OBJECT (demux, "reading header part %u", i);
    ret = gst_asf_demux_process_object (demux, &data, &size);
    if (ret != GST_FLOW_OK) {
      GST_WARNING ("process_object returned %s", gst_asf_get_flow_name (ret));
      break;
    }
  }

  return ret;

not_enough_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("short read parsing HEADER object"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_asf_demux_process_file (GstASFDemux * demux, guint8 * data, guint64 size)
{
  guint64 creation_time G_GNUC_UNUSED;
  guint64 file_size G_GNUC_UNUSED;
  guint64 send_time G_GNUC_UNUSED;
  guint64 packets_count, play_time, preroll;
  guint32 flags, min_pktsize, max_pktsize, min_bitrate G_GNUC_UNUSED;

  if (size < (16 + 8 + 8 + 8 + 8 + 8 + 8 + 4 + 4 + 4 + 4))
    goto not_enough_data;

  gst_asf_demux_skip_bytes (16, &data, &size);  /* skip GUID */
  file_size = gst_asf_demux_get_uint64 (&data, &size);
  creation_time = gst_asf_demux_get_uint64 (&data, &size);
  packets_count = gst_asf_demux_get_uint64 (&data, &size);
  play_time = gst_asf_demux_get_uint64 (&data, &size);
  send_time = gst_asf_demux_get_uint64 (&data, &size);
  preroll = gst_asf_demux_get_uint64 (&data, &size);
  flags = gst_asf_demux_get_uint32 (&data, &size);
  min_pktsize = gst_asf_demux_get_uint32 (&data, &size);
  max_pktsize = gst_asf_demux_get_uint32 (&data, &size);
  min_bitrate = gst_asf_demux_get_uint32 (&data, &size);

  demux->broadcast = ! !(flags & 0x01);
  demux->seekable = ! !(flags & 0x02);

  GST_DEBUG_OBJECT (demux, "min_pktsize = %u", min_pktsize);
  GST_DEBUG_OBJECT (demux, "flags::broadcast = %d", demux->broadcast);
  GST_DEBUG_OBJECT (demux, "flags::seekable  = %d", demux->seekable);

  if (demux->broadcast) {
    /* these fields are invalid if the broadcast flag is set */
    play_time = 0;
    file_size = 0;
  }

  if (min_pktsize != max_pktsize)
    goto non_fixed_packet_size;

  demux->packet_size = max_pktsize;

  /* FIXME: do we need send_time as well? what is it? */
  if ((play_time * 100) >= (preroll * GST_MSECOND))
    demux->play_time = (play_time * 100) - (preroll * GST_MSECOND);
  else
    demux->play_time = 0;

  demux->preroll = preroll * GST_MSECOND;

  /* initial latency */
  demux->latency = demux->preroll;

  if (demux->play_time == 0)
    demux->seekable = FALSE;

  GST_DEBUG_OBJECT (demux, "play_time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->play_time));
  GST_DEBUG_OBJECT (demux, "preroll   %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->preroll));

  if (demux->play_time > 0) {
    demux->segment.duration = demux->play_time;
  }

  GST_INFO ("object is a file with %" G_GUINT64_FORMAT " data packets",
      packets_count);
  GST_INFO ("preroll = %" G_GUINT64_FORMAT, demux->preroll);

  return GST_FLOW_OK;

/* ERRORS */
non_fixed_packet_size:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("packet size must be fixed"));
    return GST_FLOW_ERROR;
  }
not_enough_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("short read parsing FILE object"));
    return GST_FLOW_ERROR;
  }
}

/* Content Description Object */
static GstFlowReturn
gst_asf_demux_process_comment (GstASFDemux * demux, guint8 * data, guint64 size)
{
  struct
  {
    const gchar *gst_tag;
    guint16 val_length;
    gchar *val_utf8;
  } tags[5] = {
    {
    GST_TAG_TITLE, 0, NULL}, {
    GST_TAG_ARTIST, 0, NULL}, {
    GST_TAG_COPYRIGHT, 0, NULL}, {
    GST_TAG_DESCRIPTION, 0, NULL}, {
    GST_TAG_COMMENT, 0, NULL}
  };
  GstTagList *taglist;
  GValue value = { 0 };
  gsize in, out;
  gint i = -1;

  GST_INFO_OBJECT (demux, "object is a comment");

  if (size < (2 + 2 + 2 + 2 + 2))
    goto not_enough_data;

  tags[0].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[1].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[2].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[3].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[4].val_length = gst_asf_demux_get_uint16 (&data, &size);

  GST_DEBUG_OBJECT (demux, "Comment lengths: title=%d author=%d copyright=%d "
      "description=%d rating=%d", tags[0].val_length, tags[1].val_length,
      tags[2].val_length, tags[3].val_length, tags[4].val_length);

  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (size < tags[i].val_length)
      goto not_enough_data;

    /* might be just '/0', '/0'... */
    if (tags[i].val_length > 2 && tags[i].val_length % 2 == 0) {
      /* convert to UTF-8 */
      tags[i].val_utf8 = g_convert ((gchar *) data, tags[i].val_length,
          "UTF-8", "UTF-16LE", &in, &out, NULL);
    }
    gst_asf_demux_skip_bytes (tags[i].val_length, &data, &size);
  }

  /* parse metadata into taglist */
  taglist = gst_tag_list_new_empty ();
  g_value_init (&value, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (tags[i].val_utf8 && strlen (tags[i].val_utf8) > 0 && tags[i].gst_tag) {
      g_value_set_string (&value, tags[i].val_utf8);
      gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND,
          tags[i].gst_tag, &value, NULL);
    }
  }
  g_value_unset (&value);

  gst_asf_demux_add_global_tags (demux, taglist);

  for (i = 0; i < G_N_ELEMENTS (tags); ++i)
    g_free (tags[i].val_utf8);

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "unexpectedly short of data while processing "
        "comment tag section %d, skipping comment object", i);
    for (i = 0; i < G_N_ELEMENTS (tags); i++)
      g_free (tags[i].val_utf8);
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_bitrate_props_object (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  guint16 num_streams, i;
  AsfStream *stream;

  if (size < 2)
    goto not_enough_data;

  num_streams = gst_asf_demux_get_uint16 (&data, &size);

  GST_INFO ("object is a bitrate properties object with %u streams",
      num_streams);

  if (size < (num_streams * (2 + 4)))
    goto not_enough_data;

  for (i = 0; i < num_streams; ++i) {
    guint32 bitrate;
    guint16 stream_id;

    stream_id = gst_asf_demux_get_uint16 (&data, &size);
    bitrate = gst_asf_demux_get_uint32 (&data, &size);

    if (stream_id < GST_ASF_DEMUX_NUM_STREAM_IDS) {
      GST_DEBUG_OBJECT (demux, "bitrate of stream %u = %u", stream_id, bitrate);
      stream = gst_asf_demux_get_stream (demux, stream_id);
      if (stream) {
        if (stream->pending_tags == NULL)
          stream->pending_tags = gst_tag_list_new_empty ();
        gst_tag_list_add (stream->pending_tags, GST_TAG_MERGE_REPLACE,
            GST_TAG_BITRATE, bitrate, NULL);
      } else {
        GST_WARNING_OBJECT (demux, "Stream id %u wasn't found", stream_id);
      }
    } else {
      GST_WARNING ("stream id %u is too large", stream_id);
    }
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing bitrate props object!");
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_header_ext (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 hdr_size;

  /* Get the rest of the header's header */
  if (size < (16 + 2 + 4))
    goto not_enough_data;

  /* skip GUID and two other bytes */
  gst_asf_demux_skip_bytes (16 + 2, &data, &size);
  hdr_size = gst_asf_demux_get_uint32 (&data, &size);

  GST_INFO ("extended header object with a size of %u bytes", (guint) size);

  /* FIXME: does data_size include the rest of the header that we have read? */
  if (hdr_size > size)
    goto not_enough_data;

  while (hdr_size > 0) {
    ret = gst_asf_demux_process_object (demux, &data, &hdr_size);
    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;

not_enough_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("short read parsing extended header object"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_asf_demux_process_language_list (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  guint i;

  if (size < 2)
    goto not_enough_data;

  if (demux->languages) {
    GST_WARNING ("More than one LANGUAGE_LIST object in stream");
    g_strfreev (demux->languages);
    demux->languages = NULL;
    demux->num_languages = 0;
  }

  demux->num_languages = gst_asf_demux_get_uint16 (&data, &size);
  GST_LOG ("%u languages:", demux->num_languages);

  demux->languages = g_new0 (gchar *, demux->num_languages + 1);
  for (i = 0; i < demux->num_languages; ++i) {
    guint8 len, *lang_data = NULL;

    if (size < 1)
      goto not_enough_data;
    len = gst_asf_demux_get_uint8 (&data, &size);
    if (gst_asf_demux_get_bytes (&lang_data, len, &data, &size)) {
      gchar *utf8;

      utf8 = g_convert ((gchar *) lang_data, len, "UTF-8", "UTF-16LE", NULL,
          NULL, NULL);

      /* truncate "en-us" etc. to just "en" */
      if (utf8 && strlen (utf8) >= 5 && (utf8[2] == '-' || utf8[2] == '_')) {
        utf8[2] = '\0';
      }
      GST_DEBUG ("[%u] %s", i, GST_STR_NULL (utf8));
      demux->languages[i] = utf8;
      g_free (lang_data);
    } else {
      goto not_enough_data;
    }
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing language list object!");
    g_free (demux->languages);
    demux->languages = NULL;
    return GST_FLOW_OK;         /* not fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_simple_index (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  GstClockTime interval;
  guint32 count, i;

  if (size < (16 + 8 + 4 + 4))
    goto not_enough_data;

  /* skip file id */
  gst_asf_demux_skip_bytes (16, &data, &size);
  interval = gst_asf_demux_get_uint64 (&data, &size) * (GstClockTime) 100;
  gst_asf_demux_skip_bytes (4, &data, &size);
  count = gst_asf_demux_get_uint32 (&data, &size);
  if (count > 0) {
    demux->sidx_interval = interval;
    demux->sidx_num_entries = count;
    g_free (demux->sidx_entries);
    demux->sidx_entries = g_new0 (AsfSimpleIndexEntry, count);

    for (i = 0; i < count; ++i) {
      if (G_UNLIKELY (size < 6)) {
        /* adjust for broken files, to avoid having entries at the end
         * of the parsed index that point to time=0. Resulting in seeking to
         * the end of the file leading back to the beginning */
        demux->sidx_num_entries -= (count - i);
        break;
      }
      demux->sidx_entries[i].packet = gst_asf_demux_get_uint32 (&data, &size);
      demux->sidx_entries[i].count = gst_asf_demux_get_uint16 (&data, &size);
      GST_LOG_OBJECT (demux, "%" GST_TIME_FORMAT " = packet %4u  count : %2d",
          GST_TIME_ARGS (i * interval), demux->sidx_entries[i].packet,
          demux->sidx_entries[i].count);
    }
  } else {
    GST_DEBUG_OBJECT (demux, "simple index object with 0 entries");
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing simple index object!");
    return GST_FLOW_OK;         /* not fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_advanced_mutual_exclusion (GstASFDemux * demux,
    guint8 * data, guint64 size)
{
  ASFGuid guid;
  guint16 num, i;

  if (size < 16 + 2 + (2 * 2))
    goto not_enough_data;

  gst_asf_demux_get_guid (&guid, &data, &size);
  num = gst_asf_demux_get_uint16 (&data, &size);

  if (num < 2) {
    GST_WARNING_OBJECT (demux, "nonsensical mutually exclusive streams count");
    return GST_FLOW_OK;
  }

  if (size < (num * sizeof (guint16)))
    goto not_enough_data;

  /* read mutually exclusive stream numbers */
  for (i = 0; i < num; ++i) {
    guint8 mes;
    mes = gst_asf_demux_get_uint16 (&data, &size) & 0x7f;
    GST_LOG_OBJECT (demux, "mutually exclusive: stream %d", mes);

    demux->mut_ex_streams =
        g_slist_append (demux->mut_ex_streams, GINT_TO_POINTER (mes));
  }


  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing advanced mutual exclusion");
    return GST_FLOW_OK;         /* not absolutely fatal */
  }
}

gboolean
gst_asf_demux_is_unknown_stream (GstASFDemux * demux, guint stream_num)
{
  return g_slist_find (demux->other_streams,
      GINT_TO_POINTER (stream_num)) == NULL;
}

static GstFlowReturn
gst_asf_demux_process_ext_stream_props (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  AsfStreamExtProps esp;
  AsfStream *stream = NULL;
  AsfObject stream_obj;
  guint16 stream_name_count;
  guint16 num_payload_ext;
  guint64 len;
  guint8 *stream_obj_data = NULL;
  guint8 *data_start;
  guint obj_size;
  guint i, stream_num;

  data_start = data;
  obj_size = (guint) size;

  if (size < 64)
    goto not_enough_data;

  esp.valid = TRUE;
  esp.start_time = gst_asf_demux_get_uint64 (&data, &size) * GST_MSECOND;
  esp.end_time = gst_asf_demux_get_uint64 (&data, &size) * GST_MSECOND;
  esp.data_bitrate = gst_asf_demux_get_uint32 (&data, &size);
  esp.buffer_size = gst_asf_demux_get_uint32 (&data, &size);
  esp.intial_buf_fullness = gst_asf_demux_get_uint32 (&data, &size);
  esp.data_bitrate2 = gst_asf_demux_get_uint32 (&data, &size);
  esp.buffer_size2 = gst_asf_demux_get_uint32 (&data, &size);
  esp.intial_buf_fullness2 = gst_asf_demux_get_uint32 (&data, &size);
  esp.max_obj_size = gst_asf_demux_get_uint32 (&data, &size);
  esp.flags = gst_asf_demux_get_uint32 (&data, &size);
  stream_num = gst_asf_demux_get_uint16 (&data, &size);
  esp.lang_idx = gst_asf_demux_get_uint16 (&data, &size);
  esp.avg_time_per_frame = gst_asf_demux_get_uint64 (&data, &size);
  stream_name_count = gst_asf_demux_get_uint16 (&data, &size);
  num_payload_ext = gst_asf_demux_get_uint16 (&data, &size);

  GST_INFO ("start_time             = %" GST_TIME_FORMAT,
      GST_TIME_ARGS (esp.start_time));
  GST_INFO ("end_time               = %" GST_TIME_FORMAT,
      GST_TIME_ARGS (esp.end_time));
  GST_INFO ("flags                  = %08x", esp.flags);
  GST_INFO ("average time per frame = %" GST_TIME_FORMAT,
      GST_TIME_ARGS (esp.avg_time_per_frame * 100));
  GST_INFO ("stream number          = %u", stream_num);
  GST_INFO ("stream language ID idx = %u (%s)", esp.lang_idx,
      (esp.lang_idx < demux->num_languages) ?
      GST_STR_NULL (demux->languages[esp.lang_idx]) : "??");
  GST_INFO ("stream name count      = %u", stream_name_count);

  /* read stream names */
  for (i = 0; i < stream_name_count; ++i) {
    guint16 stream_lang_idx G_GNUC_UNUSED;
    gchar *stream_name = NULL;

    if (size < 2)
      goto not_enough_data;
    stream_lang_idx = gst_asf_demux_get_uint16 (&data, &size);
    if (!gst_asf_demux_get_string (&stream_name, NULL, &data, &size))
      goto not_enough_data;
    GST_INFO ("stream name %d: %s", i, GST_STR_NULL (stream_name));
    g_free (stream_name);       /* TODO: store names in struct */
  }

  /* read payload extension systems stuff */
  GST_LOG ("payload extension systems count = %u", num_payload_ext);

  if (num_payload_ext > 0)
    esp.payload_extensions = g_new0 (AsfPayloadExtension, num_payload_ext + 1);
  else
    esp.payload_extensions = NULL;

  for (i = 0; i < num_payload_ext; ++i) {
    AsfPayloadExtension ext;
    ASFGuid ext_guid;
    guint32 sys_info_len;

    if (size < 16 + 2 + 4)
      goto not_enough_data;

    gst_asf_demux_get_guid (&ext_guid, &data, &size);
    ext.id = gst_asf_demux_identify_guid (asf_payload_ext_guids, &ext_guid);
    ext.len = gst_asf_demux_get_uint16 (&data, &size);

    sys_info_len = gst_asf_demux_get_uint32 (&data, &size);
    GST_LOG ("payload systems info len = %u", sys_info_len);
    if (!gst_asf_demux_skip_bytes (sys_info_len, &data, &size))
      goto not_enough_data;

    esp.payload_extensions[i] = ext;
  }

  GST_LOG ("bytes read: %u/%u", (guint) (data - data_start), obj_size);

  /* there might be an optional STREAM_INFO object here now; if not, we
   * should have parsed the corresponding stream info object already (since
   * we are parsing the extended stream properties objects delayed) */
  if (size == 0) {
    stream = gst_asf_demux_get_stream (demux, stream_num);
    goto done;
  }

  /* get size of the stream object */
  if (!asf_demux_peek_object (demux, data, size, &stream_obj, TRUE))
    goto not_enough_data;

  if (stream_obj.id != ASF_OBJ_STREAM)
    goto expected_stream_object;

  if (stream_obj.size < ASF_OBJECT_HEADER_SIZE ||
      stream_obj.size > (10 * 1024 * 1024))
    goto not_enough_data;

  gst_asf_demux_skip_bytes (ASF_OBJECT_HEADER_SIZE, &data, &size);

  /* process this stream object later after all the other 'normal' ones
   * have been processed (since the others are more important/non-hidden) */
  len = stream_obj.size - ASF_OBJECT_HEADER_SIZE;
  if (!gst_asf_demux_get_bytes (&stream_obj_data, len, &data, &size))
    goto not_enough_data;

  /* parse stream object */
  stream = gst_asf_demux_parse_stream_object (demux, stream_obj_data, len);
  g_free (stream_obj_data);

done:

  if (stream) {
    stream->ext_props = esp;

    /* try to set the framerate */
    if (stream->is_video && stream->caps) {
      GValue framerate = { 0 };
      GstStructure *s;
      gint num, denom;

      g_value_init (&framerate, GST_TYPE_FRACTION);

      num = GST_SECOND / 100;
      denom = esp.avg_time_per_frame;
      if (denom == 0) {
        /* avoid division by 0, assume 25/1 framerate */
        denom = GST_SECOND / 2500;
      }

      gst_value_set_fraction (&framerate, num, denom);

      stream->caps = gst_caps_make_writable (stream->caps);
      s = gst_caps_get_structure (stream->caps, 0);
      gst_structure_set_value (s, "framerate", &framerate);
      g_value_unset (&framerate);
      GST_DEBUG_OBJECT (demux, "setting framerate of %d/%d = %f",
          num, denom, ((gdouble) num) / denom);
    }

    /* add language info now if we have it */
    if (stream->ext_props.lang_idx < demux->num_languages) {
      if (stream->pending_tags == NULL)
        stream->pending_tags = gst_tag_list_new_empty ();
      GST_LOG_OBJECT (demux, "stream %u has language '%s'", stream->id,
          demux->languages[stream->ext_props.lang_idx]);
      gst_tag_list_add (stream->pending_tags, GST_TAG_MERGE_APPEND,
          GST_TAG_LANGUAGE_CODE, demux->languages[stream->ext_props.lang_idx],
          NULL);
    }
  } else if (gst_asf_demux_is_unknown_stream (demux, stream_num)) {
    GST_WARNING_OBJECT (demux, "Ext. stream properties for unknown stream");
  }

  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing ext stream props object!");
    return GST_FLOW_OK;         /* not absolutely fatal */
  }
expected_stream_object:
  {
    GST_WARNING_OBJECT (demux, "error parsing extended stream properties "
        "object: expected embedded stream object, but got %s object instead!",
        gst_asf_get_guid_nick (asf_object_guids, stream_obj.id));
    return GST_FLOW_OK;         /* not absolutely fatal */
  }
}

static const gchar *
gst_asf_demux_push_obj (GstASFDemux * demux, guint32 obj_id)
{
  const gchar *nick;

  nick = gst_asf_get_guid_nick (asf_object_guids, obj_id);
  if (g_str_has_prefix (nick, "ASF_OBJ_"))
    nick += strlen ("ASF_OBJ_");

  if (demux->objpath == NULL) {
    demux->objpath = g_strdup (nick);
  } else {
    gchar *newpath;

    newpath = g_strdup_printf ("%s/%s", demux->objpath, nick);
    g_free (demux->objpath);
    demux->objpath = newpath;
  }

  return (const gchar *) demux->objpath;
}

static void
gst_asf_demux_pop_obj (GstASFDemux * demux)
{
  gchar *s;

  if ((s = g_strrstr (demux->objpath, "/"))) {
    *s = '\0';
  } else {
    g_free (demux->objpath);
    demux->objpath = NULL;
  }
}

static void
gst_asf_demux_process_queued_extended_stream_objects (GstASFDemux * demux)
{
  GSList *l;
  guint i;

  /* Parse the queued extended stream property objects and add the info
   * to the existing streams or add the new embedded streams, but without
   * activating them yet */
  GST_LOG_OBJECT (demux, "%u queued extended stream properties objects",
      g_slist_length (demux->ext_stream_props));

  for (l = demux->ext_stream_props, i = 0; l != NULL; l = l->next, ++i) {
    GstBuffer *buf = GST_BUFFER (l->data);
    GstMapInfo map;

    gst_buffer_map (buf, &map, GST_MAP_READ);

    GST_LOG_OBJECT (demux, "parsing ext. stream properties object #%u", i);
    gst_asf_demux_process_ext_stream_props (demux, map.data, map.size);
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
  }
  g_slist_free (demux->ext_stream_props);
  demux->ext_stream_props = NULL;
}

#if 0
static void
gst_asf_demux_activate_ext_props_streams (GstASFDemux * demux)
{
  guint i, j;

  for (i = 0; i < demux->num_streams; ++i) {
    AsfStream *stream;
    gboolean is_hidden;
    GSList *x;

    stream = &demux->stream[i];

    GST_LOG_OBJECT (demux, "checking  stream %2u", stream->id);

    if (stream->active) {
      GST_LOG_OBJECT (demux, "stream %2u is already activated", stream->id);
      continue;
    }

    is_hidden = FALSE;
    for (x = demux->mut_ex_streams; x != NULL; x = x->next) {
      guint8 *mes;

      /* check for each mutual exclusion whether it affects this stream */
      for (mes = (guint8 *) x->data; mes != NULL && *mes != 0xff; ++mes) {
        if (*mes == stream->id) {
          /* if yes, check if we've already added streams that are mutually
           * exclusive with the stream we're about to add */
          for (mes = (guint8 *) x->data; mes != NULL && *mes != 0xff; ++mes) {
            for (j = 0; j < demux->num_streams; ++j) {
              /* if the broadcast flag is set, assume the hidden streams aren't
               * actually streamed and hide them (or playbin won't work right),
               * otherwise assume their data is available */
              if (demux->stream[j].id == *mes && demux->broadcast) {
                is_hidden = TRUE;
                GST_LOG_OBJECT (demux, "broadcast stream ID %d to be added is "
                    "mutually exclusive with already existing stream ID %d, "
                    "hiding stream", stream->id, demux->stream[j].id);
                goto next;
              }
            }
          }
          break;
        }
      }
    }

  next:

    /* FIXME: we should do stream activation based on preroll data in
     * streaming mode too */
    if (demux->streaming && !is_hidden)
      gst_asf_demux_activate_stream (demux, stream);
  }
}
#endif

static GstFlowReturn
gst_asf_demux_process_object (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  AsfObject obj;
  guint64 obj_data_size;

  if (*p_size < ASF_OBJECT_HEADER_SIZE)
    return ASF_FLOW_NEED_MORE_DATA;

  asf_demux_peek_object (demux, *p_data, ASF_OBJECT_HEADER_SIZE, &obj, TRUE);
  gst_asf_demux_skip_bytes (ASF_OBJECT_HEADER_SIZE, p_data, p_size);

  obj_data_size = obj.size - ASF_OBJECT_HEADER_SIZE;

  if (*p_size < obj_data_size)
    return ASF_FLOW_NEED_MORE_DATA;

  gst_asf_demux_push_obj (demux, obj.id);

  GST_INFO ("%s: size %" G_GUINT64_FORMAT, demux->objpath, obj.size);

  switch (obj.id) {
    case ASF_OBJ_STREAM:
      gst_asf_demux_parse_stream_object (demux, *p_data, obj_data_size);
      ret = GST_FLOW_OK;
      break;
    case ASF_OBJ_FILE:
      ret = gst_asf_demux_process_file (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_HEADER:
      ret = gst_asf_demux_process_header (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_COMMENT:
      ret = gst_asf_demux_process_comment (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_HEAD1:
      ret = gst_asf_demux_process_header_ext (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_BITRATE_PROPS:
      ret =
          gst_asf_demux_process_bitrate_props_object (demux, *p_data,
          obj_data_size);
      break;
    case ASF_OBJ_EXT_CONTENT_DESC:
      ret =
          gst_asf_demux_process_ext_content_desc (demux, *p_data,
          obj_data_size);
      break;
    case ASF_OBJ_METADATA_OBJECT:
      ret = gst_asf_demux_process_metadata (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_EXTENDED_STREAM_PROPS:{
      GstBuffer *buf;

      /* process these later, we might not have parsed the corresponding
       * stream object yet */
      GST_LOG ("%s: queued for later parsing", demux->objpath);
      buf = gst_buffer_new_and_alloc (obj_data_size);
      gst_buffer_fill (buf, 0, *p_data, obj_data_size);
      demux->ext_stream_props = g_slist_append (demux->ext_stream_props, buf);
      ret = GST_FLOW_OK;
      break;
    }
    case ASF_OBJ_LANGUAGE_LIST:
      ret = gst_asf_demux_process_language_list (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_ADVANCED_MUTUAL_EXCLUSION:
      ret = gst_asf_demux_process_advanced_mutual_exclusion (demux, *p_data,
          obj_data_size);
      break;
    case ASF_OBJ_SIMPLE_INDEX:
      ret = gst_asf_demux_process_simple_index (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_CONTENT_ENCRYPTION:
    case ASF_OBJ_EXT_CONTENT_ENCRYPTION:
    case ASF_OBJ_DIGITAL_SIGNATURE_OBJECT:
    case ASF_OBJ_UNKNOWN_ENCRYPTION_OBJECT:
      goto error_encrypted;
    case ASF_OBJ_CONCEAL_NONE:
    case ASF_OBJ_HEAD2:
    case ASF_OBJ_UNDEFINED:
    case ASF_OBJ_CODEC_COMMENT:
    case ASF_OBJ_INDEX:
    case ASF_OBJ_PADDING:
    case ASF_OBJ_BITRATE_MUTEX:
    case ASF_OBJ_COMPATIBILITY:
    case ASF_OBJ_INDEX_PLACEHOLDER:
    case ASF_OBJ_INDEX_PARAMETERS:
    case ASF_OBJ_STREAM_PRIORITIZATION:
    case ASF_OBJ_SCRIPT_COMMAND:
    case ASF_OBJ_METADATA_LIBRARY_OBJECT:
    default:
      /* Unknown/unhandled object, skip it and hope for the best */
      GST_INFO ("%s: skipping object", demux->objpath);
      ret = GST_FLOW_OK;
      break;
  }

  /* this can't fail, we checked the number of bytes available before */
  gst_asf_demux_skip_bytes (obj_data_size, p_data, p_size);

  GST_LOG ("%s: ret = %s", demux->objpath, gst_asf_get_flow_name (ret));

  gst_asf_demux_pop_obj (demux);

  return ret;

/* ERRORS */
error_encrypted:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DECRYPT, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}

static void
gst_asf_demux_descramble_buffer (GstASFDemux * demux, AsfStream * stream,
    GstBuffer ** p_buffer)
{
  GstBuffer *descrambled_buffer;
  GstBuffer *scrambled_buffer;
  GstBuffer *sub_buffer;
  guint offset;
  guint off;
  guint row;
  guint col;
  guint idx;

  /* descrambled_buffer is initialised in the first iteration */
  descrambled_buffer = NULL;
  scrambled_buffer = *p_buffer;

  if (gst_buffer_get_size (scrambled_buffer) <
      stream->ds_packet_size * stream->span)
    return;

  for (offset = 0; offset < gst_buffer_get_size (scrambled_buffer);
      offset += stream->ds_chunk_size) {
    off = offset / stream->ds_chunk_size;
    row = off / stream->span;
    col = off % stream->span;
    idx = row + col * stream->ds_packet_size / stream->ds_chunk_size;
    GST_DEBUG ("idx=%u, row=%u, col=%u, off=%u, ds_chunk_size=%u", idx, row,
        col, off, stream->ds_chunk_size);
    GST_DEBUG ("scrambled buffer size=%" G_GSIZE_FORMAT
        ", span=%u, packet_size=%u", gst_buffer_get_size (scrambled_buffer),
        stream->span, stream->ds_packet_size);
    GST_DEBUG ("gst_buffer_get_size (scrambled_buffer) = %" G_GSIZE_FORMAT,
        gst_buffer_get_size (scrambled_buffer));
    sub_buffer =
        gst_buffer_copy_region (scrambled_buffer, GST_BUFFER_COPY_MEMORY,
        idx * stream->ds_chunk_size, stream->ds_chunk_size);
    if (!offset) {
      descrambled_buffer = sub_buffer;
    } else {
      descrambled_buffer = gst_buffer_append (descrambled_buffer, sub_buffer);
    }
  }

  GST_BUFFER_TIMESTAMP (descrambled_buffer) =
      GST_BUFFER_TIMESTAMP (scrambled_buffer);
  GST_BUFFER_DURATION (descrambled_buffer) =
      GST_BUFFER_DURATION (scrambled_buffer);
  GST_BUFFER_OFFSET (descrambled_buffer) = GST_BUFFER_OFFSET (scrambled_buffer);
  GST_BUFFER_OFFSET_END (descrambled_buffer) =
      GST_BUFFER_OFFSET_END (scrambled_buffer);

  /* FIXME/CHECK: do we need to transfer buffer flags here too? */

  gst_buffer_unref (scrambled_buffer);
  *p_buffer = descrambled_buffer;
}

static gboolean
gst_asf_demux_element_send_event (GstElement * element, GstEvent * event)
{
  GstASFDemux *demux = GST_ASF_DEMUX (element);
  gint i;

  GST_DEBUG ("handling element event of type %s", GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; ++i) {
    gst_event_ref (event);
    if (gst_asf_demux_handle_src_event (demux->stream[i].pad,
            GST_OBJECT_CAST (element), event)) {
      gst_event_unref (event);
      return TRUE;
    }
  }

  gst_event_unref (event);
  return FALSE;
}

/* takes ownership of the passed event */
static gboolean
gst_asf_demux_send_event_unlocked (GstASFDemux * demux, GstEvent * event)
{
  gboolean ret = TRUE;
  gint i;

  GST_DEBUG_OBJECT (demux, "sending %s event to all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; ++i) {
    gst_event_ref (event);
    ret &= gst_pad_push_event (demux->stream[i].pad, event);
  }
  gst_event_unref (event);
  return ret;
}

static gboolean
gst_asf_demux_handle_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstASFDemux *demux;
  gboolean res = FALSE;

  demux = GST_ASF_DEMUX (parent);

  GST_DEBUG ("handling %s query",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_LOG ("only support duration queries in TIME format");
        break;
      }

      res = gst_pad_query_default (pad, parent, query);
      if (!res) {
        GST_OBJECT_LOCK (demux);

        if (demux->segment.duration != GST_CLOCK_TIME_NONE) {
          GST_LOG ("returning duration: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (demux->segment.duration));

          gst_query_set_duration (query, GST_FORMAT_TIME,
              demux->segment.duration);

          res = TRUE;
        } else {
          GST_LOG ("duration not known yet");
        }

        GST_OBJECT_UNLOCK (demux);
      }
      break;
    }

    case GST_QUERY_POSITION:{
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_LOG ("only support position queries in TIME format");
        break;
      }

      GST_OBJECT_LOCK (demux);

      if (demux->segment.position != GST_CLOCK_TIME_NONE) {
        GST_LOG ("returning position: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->segment.position));

        gst_query_set_position (query, GST_FORMAT_TIME,
            demux->segment.position);

        res = TRUE;
      } else {
        GST_LOG ("position not known yet");
      }

      GST_OBJECT_UNLOCK (demux);
      break;
    }

    case GST_QUERY_SEEKING:{
      GstFormat format;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format == GST_FORMAT_TIME) {
        gint64 duration;

        GST_OBJECT_LOCK (demux);
        duration = demux->segment.duration;
        GST_OBJECT_UNLOCK (demux);

        if (!demux->streaming || !demux->seekable) {
          gst_query_set_seeking (query, GST_FORMAT_TIME, demux->seekable, 0,
              duration);
          res = TRUE;
        } else {
          GstFormat fmt;
          gboolean seekable;

          /* try upstream first in TIME */
          res = gst_pad_query_default (pad, parent, query);

          gst_query_parse_seeking (query, &fmt, &seekable, NULL, NULL);
          GST_LOG_OBJECT (demux, "upstream %s seekable %d",
              GST_STR_NULL (gst_format_get_name (fmt)), seekable);
          /* if no luck, maybe in BYTES */
          if (!seekable || fmt != GST_FORMAT_TIME) {
            GstQuery *q;

            q = gst_query_new_seeking (GST_FORMAT_BYTES);
            if ((res = gst_pad_peer_query (demux->sinkpad, q))) {
              gst_query_parse_seeking (q, &fmt, &seekable, NULL, NULL);
              GST_LOG_OBJECT (demux, "upstream %s seekable %d",
                  GST_STR_NULL (gst_format_get_name (fmt)), seekable);
              if (fmt != GST_FORMAT_BYTES)
                seekable = FALSE;
            }
            gst_query_unref (q);
            gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0,
                duration);
            res = TRUE;
          }
        }
      } else
        GST_LOG_OBJECT (demux, "only support seeking in TIME format");
      break;
    }

    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;

      /* preroll delay does not matter in non-live pipeline,
       * but we might end up in a live (rtsp) one ... */

      /* first forward */
      res = gst_pad_query_default (pad, parent, query);
      if (!res)
        break;

      gst_query_parse_latency (query, &live, &min, &max);

      GST_DEBUG_OBJECT (demux, "Peer latency: live %d, min %"
          GST_TIME_FORMAT " max %" GST_TIME_FORMAT, live,
          GST_TIME_ARGS (min), GST_TIME_ARGS (max));

      GST_OBJECT_LOCK (demux);
      min += demux->latency;
      if (max != -1)
        max += demux->latency;
      GST_OBJECT_UNLOCK (demux);

      gst_query_set_latency (query, live, min, max);
      break;
    }
    case GST_QUERY_SEGMENT:
    {
      GstFormat format;
      gint64 start, stop;

      format = demux->segment.format;

      start =
          gst_segment_to_stream_time (&demux->segment, format,
          demux->segment.start);
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;
      else
        stop = gst_segment_to_stream_time (&demux->segment, format, stop);

      gst_query_set_segment (query, demux->segment.rate, format, start, stop);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_asf_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstASFDemux *demux = GST_ASF_DEMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      gst_segment_init (&demux->segment, GST_FORMAT_TIME);
      demux->need_newsegment = TRUE;
      demux->segment_running = FALSE;
      demux->keyunit_sync = FALSE;
      demux->accurate = FALSE;
      demux->adapter = gst_adapter_new ();
      demux->metadata = gst_caps_new_empty ();
      demux->global_metadata = gst_structure_new_empty ("metadata");
      demux->data_size = 0;
      demux->data_offset = 0;
      demux->index_offset = 0;
      demux->base_offset = 0;
      demux->flowcombiner = gst_flow_combiner_new ();

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_asf_demux_reset (demux, FALSE);
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_asf_demux_reset (demux, FALSE);
      gst_flow_combiner_free (demux->flowcombiner);
      demux->flowcombiner = NULL;
      break;
    default:
      break;
  }

  return ret;
}
