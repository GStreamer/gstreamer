/* GStreamer wavpack plugin
 * Copyright (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (c) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackparse.c: wavpack file parser
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

/**
 * SECTION:element-wavpackparse
 *
 * WavpackParse takes raw, unframed Wavpack streams and splits them into
 * single Wavpack chunks with information like bit depth and the position
 * in the stream.
 * <ulink url="http://www.wavpack.com/">Wavpack</ulink> is an open-source
 * audio codec that features both lossless and lossy encoding.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.wv ! wavpackparse ! wavpackdec ! fakesink
 * ]| This pipeline decodes the Wavpack file test.wv into raw audio buffers.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>

#include <math.h>
#include <string.h>

#include <wavpack/wavpack.h>
#include "gstwavpackparse.h"
#include "gstwavpackstreamreader.h"
#include "gstwavpackcommon.h"

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_parse_debug);
#define GST_CAT_DEFAULT gst_wavpack_parse_debug

static inline GstWavpackParseIndexEntry *
gst_wavpack_parse_index_entry_new (void)
{
  return g_slice_new (GstWavpackParseIndexEntry);
}

static inline void
gst_wavpack_parse_index_entry_free (GstWavpackParseIndexEntry * entry)
{
  g_slice_free (GstWavpackParseIndexEntry, entry);
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "framed = (boolean) false; "
        "audio/x-wavpack-correction, " "framed = (boolean) false")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) [ 1, 32 ], "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true")
    );

static GstStaticPadTemplate wvc_src_factory = GST_STATIC_PAD_TEMPLATE ("wvcsrc",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, " "framed = (boolean) true")
    );

static gboolean gst_wavpack_parse_sink_activate (GstPad * sinkpad);

static gboolean
gst_wavpack_parse_sink_activate_pull (GstPad * sinkpad, gboolean active);

static void gst_wavpack_parse_loop (GstElement * element);

static GstStateChangeReturn gst_wavpack_parse_change_state (GstElement *
    element, GstStateChange transition);
static void gst_wavpack_parse_reset (GstWavpackParse * parse);

static gint64 gst_wavpack_parse_get_upstream_length (GstWavpackParse * wvparse);

static GstBuffer *gst_wavpack_parse_pull_buffer (GstWavpackParse * wvparse,
    gint64 offset, guint size, GstFlowReturn * flow);
static GstFlowReturn gst_wavpack_parse_chain (GstPad * pad, GstBuffer * buf);

GST_BOILERPLATE (GstWavpackParse, gst_wavpack_parse, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_wavpack_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &wvc_src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "Wavpack parser",
      "Codec/Demuxer/Audio",
      "Parses Wavpack files",
      "Arwed v. Merkatz <v.merkatz@gmx.net>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");
}

static void
gst_wavpack_parse_finalize (GObject * object)
{
  gst_wavpack_parse_reset (GST_WAVPACK_PARSE (object));

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wavpack_parse_class_init (GstWavpackParseClass * klass)
{
  GObjectClass *gobject_class;

  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_wavpack_parse_finalize;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_change_state);
}

static GstWavpackParseIndexEntry *
gst_wavpack_parse_index_get_last_entry (GstWavpackParse * wvparse)
{
  g_assert (wvparse->entries != NULL);

  return wvparse->entries->data;
}

static GstWavpackParseIndexEntry *
gst_wavpack_parse_index_get_entry_from_sample (GstWavpackParse * wvparse,
    gint64 sample_offset)
{
  gint i;

  GSList *node;

  if (wvparse->entries == NULL)
    return NULL;

  for (node = wvparse->entries, i = 0; node; node = node->next, i++) {
    GstWavpackParseIndexEntry *entry;

    entry = node->data;

    GST_LOG_OBJECT (wvparse, "Index entry %03u: sample %" G_GINT64_FORMAT " @"
        " byte %" G_GINT64_FORMAT, i, entry->sample_offset, entry->byte_offset);

    if (entry->sample_offset <= sample_offset &&
        sample_offset < entry->sample_offset_end) {
      GST_LOG_OBJECT (wvparse, "found match");
      return entry;
    }

    /* as the list is sorted and we first look at the latest entry
     * we can abort searching for an entry if the sample we want is
     * after the latest one */
    if (sample_offset >= entry->sample_offset_end)
      break;
  }
  GST_LOG_OBJECT (wvparse, "no match in index");
  return NULL;
}

static void
gst_wavpack_parse_index_append_entry (GstWavpackParse * wvparse,
    gint64 byte_offset, gint64 sample_offset, gint64 num_samples)
{
  GstWavpackParseIndexEntry *entry;

  /* do we have this one already? */
  if (wvparse->entries) {
    entry = gst_wavpack_parse_index_get_last_entry (wvparse);
    if (entry->byte_offset >= byte_offset
        || entry->sample_offset >= sample_offset)
      return;
  }

  GST_LOG_OBJECT (wvparse, "Adding index entry %8" G_GINT64_FORMAT " - %"
      GST_TIME_FORMAT " @ offset 0x%08" G_GINT64_MODIFIER "x", sample_offset,
      GST_TIME_ARGS (gst_util_uint64_scale_int (sample_offset,
              GST_SECOND, wvparse->samplerate)), byte_offset);

  entry = gst_wavpack_parse_index_entry_new ();
  entry->byte_offset = byte_offset;
  entry->sample_offset = sample_offset;
  entry->sample_offset_end = sample_offset + num_samples;
  wvparse->entries = g_slist_prepend (wvparse->entries, entry);
}

static void
gst_wavpack_parse_reset (GstWavpackParse * parse)
{
  parse->total_samples = G_GINT64_CONSTANT (-1);
  parse->samplerate = 0;
  parse->channels = 0;

  gst_segment_init (&parse->segment, GST_FORMAT_UNDEFINED);
  parse->next_block_index = 0;

  parse->current_offset = 0;
  parse->need_newsegment = TRUE;
  parse->discont = TRUE;
  parse->upstream_length = -1;

  if (parse->entries) {
    g_slist_foreach (parse->entries, (GFunc) gst_wavpack_parse_index_entry_free,
        NULL);
    g_slist_free (parse->entries);
    parse->entries = NULL;
  }

  if (parse->adapter) {
    gst_adapter_clear (parse->adapter);
    g_object_unref (parse->adapter);
    parse->adapter = NULL;
  }

  if (parse->srcpad != NULL) {
    gboolean res;

    GST_DEBUG_OBJECT (parse, "Removing src pad");
    res = gst_element_remove_pad (GST_ELEMENT (parse), parse->srcpad);
    g_return_if_fail (res != FALSE);
    gst_object_unref (parse->srcpad);
    parse->srcpad = NULL;
  }

  g_list_foreach (parse->queued_events, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (parse->queued_events);
  parse->queued_events = NULL;

  if (parse->pending_buffer)
    gst_buffer_unref (parse->pending_buffer);

  parse->pending_buffer = NULL;
}

static const GstQueryType *
gst_wavpack_parse_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return types;
}

static gboolean
gst_wavpack_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstWavpackParse *parse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));

  GstFormat format;

  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      gint64 cur;

      guint rate;

      GST_OBJECT_LOCK (parse);
      cur = parse->segment.last_stop;
      rate = parse->samplerate;
      GST_OBJECT_UNLOCK (parse);

      if (rate == 0) {
        GST_DEBUG_OBJECT (parse, "haven't read header yet");
        break;
      }

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          cur = gst_util_uint64_scale_int (cur, GST_SECOND, rate);
          gst_query_set_position (query, GST_FORMAT_TIME, cur);
          ret = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, GST_FORMAT_DEFAULT, cur);
          ret = TRUE;
          break;
        default:
          GST_DEBUG_OBJECT (parse, "cannot handle position query in "
              "%s format. Forwarding upstream.", gst_format_get_name (format));
          ret = gst_pad_query_default (pad, query);
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:{
      gint64 len;

      guint rate;

      GST_OBJECT_LOCK (parse);
      rate = parse->samplerate;
      len = parse->total_samples;
      GST_OBJECT_UNLOCK (parse);

      if (rate == 0) {
        GST_DEBUG_OBJECT (parse, "haven't read header yet");
        break;
      }

      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          if (len != G_GINT64_CONSTANT (-1))
            len = gst_util_uint64_scale_int (len, GST_SECOND, rate);
          gst_query_set_duration (query, GST_FORMAT_TIME, len);
          ret = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_duration (query, GST_FORMAT_DEFAULT, len);
          ret = TRUE;
          break;
        default:
          GST_DEBUG_OBJECT (parse, "cannot handle duration query in "
              "%s format. Forwarding upstream.", gst_format_get_name (format));
          ret = gst_pad_query_default (pad, query);
          break;
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format == GST_FORMAT_TIME || format == GST_FORMAT_DEFAULT) {
        gboolean seekable;

        gint64 duration = -1;

        /* only fails if we didn't read the headers yet and can't say
         * anything about our seeking capabilities */
        if (!gst_pad_query_duration (pad, &format, &duration))
          break;

        /* can't seek in streaming mode yet */
        GST_OBJECT_LOCK (parse);
        seekable = (parse->adapter == NULL);
        GST_OBJECT_UNLOCK (parse);

        gst_query_set_seeking (query, format, seekable, 0, duration);
        ret = TRUE;
      }
      break;
    }
    default:{
      ret = gst_pad_query_default (pad, query);
      break;
    }
  }

  gst_object_unref (parse);
  return ret;

}

/* returns TRUE on success, with byte_offset set to the offset of the
 * wavpack chunk containing the sample requested. start_sample will be
 * set to the first sample in the chunk starting at byte_offset.
 * Scanning from the last known header offset to the wanted position
 * when seeking forward isn't very clever, but seems fast enough in
 * practice and has the nice side effect of populating our index
 * table */
static gboolean
gst_wavpack_parse_scan_to_find_sample (GstWavpackParse * parse,
    gint64 sample, gint64 * byte_offset, gint64 * start_sample)
{
  GstWavpackParseIndexEntry *entry;

  GstFlowReturn ret;

  gint64 off = 0;

  /* first, check if we have to scan at all */
  entry = gst_wavpack_parse_index_get_entry_from_sample (parse, sample);
  if (entry) {
    *byte_offset = entry->byte_offset;
    *start_sample = entry->sample_offset;
    GST_LOG_OBJECT (parse, "Found index entry: sample %" G_GINT64_FORMAT
        " @ offset %" G_GINT64_FORMAT, entry->sample_offset,
        entry->byte_offset);
    return TRUE;
  }

  GST_LOG_OBJECT (parse, "No matching entry in index, scanning file ...");

  /* if we have an index, we can start scanning from the last known offset
   * in there, after all we know our wanted sample is not in the index */
  if (parse->entries) {
    GstWavpackParseIndexEntry *entry;

    entry = gst_wavpack_parse_index_get_last_entry (parse);
    off = entry->byte_offset;
  }

  /* now scan forward until we find the chunk we're looking for or hit EOS */
  do {
    WavpackHeader header;

    GstBuffer *buf;

    buf = gst_wavpack_parse_pull_buffer (parse, off, sizeof (WavpackHeader),
        &ret);

    if (buf == NULL)
      break;

    gst_wavpack_read_header (&header, GST_BUFFER_DATA (buf));
    gst_buffer_unref (buf);

    if (header.flags & INITIAL_BLOCK)
      gst_wavpack_parse_index_append_entry (parse, off, header.block_index,
          header.block_samples);
    else
      continue;

    if (header.block_index <= sample &&
        sample < (header.block_index + header.block_samples)) {
      *byte_offset = off;
      *start_sample = header.block_index;
      return TRUE;
    }

    off += header.ckSize + 8;
  } while (1);

  GST_DEBUG_OBJECT (parse, "scan failed: %s (off=0x%08" G_GINT64_MODIFIER "x)",
      gst_flow_get_name (ret), off);

  return FALSE;
}

static gboolean
gst_wavpack_parse_send_newsegment (GstWavpackParse * wvparse, gboolean update)
{
  GstSegment *s = &wvparse->segment;

  gboolean ret;

  gint64 stop_time = -1;

  gint64 start_time = 0;

  gint64 cur_pos_time;

  gint64 diff;

  /* segment is in DEFAULT format, but we want to send a TIME newsegment */
  start_time = gst_util_uint64_scale_int (s->start, GST_SECOND,
      wvparse->samplerate);

  if (s->stop != -1) {
    stop_time = gst_util_uint64_scale_int (s->stop, GST_SECOND,
        wvparse->samplerate);
  }

  GST_DEBUG_OBJECT (wvparse, "sending newsegment from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT, GST_TIME_ARGS (start_time),
      GST_TIME_ARGS (stop_time));

  /* after a seek, s->last_stop will point to a chunk boundary, ie. from
   * which sample we will start sending data again, while s->start will
   * point to the sample we actually want to seek to and want to start
   * playing right after the seek. Adjust clock-time for the difference
   * so we start playing from start_time */
  cur_pos_time = gst_util_uint64_scale_int (s->last_stop, GST_SECOND,
      wvparse->samplerate);
  diff = start_time - cur_pos_time;

  ret = gst_pad_push_event (wvparse->srcpad,
      gst_event_new_new_segment (update, s->rate, GST_FORMAT_TIME,
          start_time, stop_time, start_time - diff));

  return ret;
}

static gboolean
gst_wavpack_parse_handle_seek_event (GstWavpackParse * wvparse,
    GstEvent * event)
{
  GstSeekFlags seek_flags;

  GstSeekType start_type;

  GstSeekType stop_type;

  GstSegment segment;

  GstFormat format;

  gboolean only_update;

  gboolean flush, ret;

  gdouble speed;

  gint64 stop;

  gint64 start;                 /* sample we want to seek to                  */

  gint64 byte_offset;           /* byte offset the chunk we seek to starts at */

  gint64 chunk_start;           /* first sample in chunk we seek to           */

  guint rate;

  gint64 last_stop;

  if (wvparse->adapter) {
    GST_DEBUG_OBJECT (wvparse, "seeking in streaming mode not implemented yet");
    return FALSE;
  }

  gst_event_parse_seek (event, &speed, &format, &seek_flags, &start_type,
      &start, &stop_type, &stop);

  if (format != GST_FORMAT_DEFAULT && format != GST_FORMAT_TIME) {
    GST_DEBUG ("seeking is only supported in TIME or DEFAULT format");
    return FALSE;
  }

  if (speed < 0.0) {
    GST_DEBUG ("only forward playback supported, rate %f not allowed", speed);
    return FALSE;
  }

  GST_OBJECT_LOCK (wvparse);

  rate = wvparse->samplerate;
  if (rate == 0) {
    GST_OBJECT_UNLOCK (wvparse);
    GST_DEBUG ("haven't read header yet");
    return FALSE;
  }

  /* figure out the last position we need to play. If it's configured (stop !=
   * -1), use that, else we play until the total duration of the file */
  if (stop == -1)
    stop = wvparse->segment.duration;

  /* convert from time to samples if necessary */
  if (format == GST_FORMAT_TIME) {
    if (start_type != GST_SEEK_TYPE_NONE)
      start = gst_util_uint64_scale_int (start, rate, GST_SECOND);
    if (stop_type != GST_SEEK_TYPE_NONE)
      stop = gst_util_uint64_scale_int (stop, rate, GST_SECOND);
  }

  if (start < 0) {
    GST_OBJECT_UNLOCK (wvparse);
    GST_DEBUG_OBJECT (wvparse, "Invalid start sample %" G_GINT64_FORMAT, start);
    return FALSE;
  }

  flush = ((seek_flags & GST_SEEK_FLAG_FLUSH) != 0);

  /* operate on segment copy until we know the seek worked */
  segment = wvparse->segment;

  gst_segment_set_seek (&segment, speed, GST_FORMAT_DEFAULT,
      seek_flags, start_type, start, stop_type, stop, &only_update);

#if 0
  if (only_update) {
    wvparse->segment = segment;
    gst_wavpack_parse_send_newsegment (wvparse, TRUE);
    goto done;
  }
#endif

  gst_pad_push_event (wvparse->sinkpad, gst_event_new_flush_start ());

  if (flush) {
    gst_pad_push_event (wvparse->srcpad, gst_event_new_flush_start ());
  } else {
    gst_pad_pause_task (wvparse->sinkpad);
  }

  GST_PAD_STREAM_LOCK (wvparse->sinkpad);

  /* Save current position */
  last_stop = wvparse->segment.last_stop;

  gst_pad_push_event (wvparse->sinkpad, gst_event_new_flush_stop ());

  if (flush) {
    gst_pad_push_event (wvparse->srcpad, gst_event_new_flush_stop ());
  }

  GST_DEBUG_OBJECT (wvparse, "Performing seek to %" GST_TIME_FORMAT " sample %"
      G_GINT64_FORMAT, GST_TIME_ARGS (segment.start * GST_SECOND / rate),
      start);

  ret = gst_wavpack_parse_scan_to_find_sample (wvparse, segment.start,
      &byte_offset, &chunk_start);

  if (ret) {
    GST_DEBUG_OBJECT (wvparse, "new offset: %" G_GINT64_FORMAT, byte_offset);
    wvparse->current_offset = byte_offset;
    /* we want to send a newsegment event with the actual seek position
     * as start, even though our first buffer might start before the
     * configured segment. We leave it up to the decoder or sink to crop
     * the output buffers accordingly */
    wvparse->segment = segment;
    wvparse->segment.last_stop = chunk_start;
    wvparse->need_newsegment = TRUE;
    wvparse->discont = (last_stop != chunk_start) ? TRUE : FALSE;

    /* if we're doing a segment seek, post a SEGMENT_START message */
    if (wvparse->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT_CAST (wvparse),
          gst_message_new_segment_start (GST_OBJECT_CAST (wvparse),
              wvparse->segment.format, wvparse->segment.last_stop));
    }
  } else {
    GST_DEBUG_OBJECT (wvparse, "seek failed: don't know where to seek to");
  }

  GST_PAD_STREAM_UNLOCK (wvparse->sinkpad);
  GST_OBJECT_UNLOCK (wvparse);

  gst_pad_start_task (wvparse->sinkpad,
      (GstTaskFunction) gst_wavpack_parse_loop, wvparse);

  return ret;
}

static gboolean
gst_wavpack_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackParse *parse;

  gboolean ret = TRUE;

  parse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:{
      if (parse->adapter) {
        gst_adapter_clear (parse->adapter);
      }
      if (parse->pending_buffer) {
        gst_buffer_unref (parse->pending_buffer);
        parse->pending_buffer = NULL;
        parse->pending_offset = 0;
      }
      ret = gst_pad_push_event (parse->srcpad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:{
      parse->need_newsegment = TRUE;
      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_EOS:{
      if (parse->adapter) {
        /* remove all bytes that are left in the adapter after EOS. They can't
         * be a complete Wavpack block and we can't do anything with them */
        gst_adapter_clear (parse->adapter);
      }
      if (parse->pending_buffer) {
        gst_buffer_unref (parse->pending_buffer);
        parse->pending_buffer = NULL;
        parse->pending_offset = 0;
      }
      ret = gst_pad_push_event (parse->srcpad, event);
      break;
    }
    default:{
      /* stream lock is recursive, should be fine for all events */
      GST_PAD_STREAM_LOCK (pad);
      if (parse->srcpad == NULL) {
        parse->queued_events = g_list_append (parse->queued_events, event);
      } else {
        ret = gst_pad_push_event (parse->srcpad, event);
      }
      GST_PAD_STREAM_UNLOCK (pad);
    }
  }


  gst_object_unref (parse);
  return ret;
}

static gboolean
gst_wavpack_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstWavpackParse *parse;

  gboolean ret;

  parse = GST_WAVPACK_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      ret = gst_wavpack_parse_handle_seek_event (parse, event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (parse);
  return ret;
}

static void
gst_wavpack_parse_init (GstWavpackParse * parse, GstWavpackParseClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (parse);

  GstPadTemplate *tmpl;

  tmpl = gst_element_class_get_pad_template (klass, "sink");
  parse->sinkpad = gst_pad_new_from_template (tmpl, "sink");

  gst_pad_set_activate_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_sink_activate));
  gst_pad_set_activatepull_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_sink_activate_pull));
  gst_pad_set_event_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_sink_event));
  gst_pad_set_chain_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_chain));

  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad = NULL;
  gst_wavpack_parse_reset (parse);
}

static gint64
gst_wavpack_parse_get_upstream_length (GstWavpackParse * parse)
{
  gint64 length = -1;

  GstFormat format = GST_FORMAT_BYTES;

  if (!gst_pad_query_peer_duration (parse->sinkpad, &format, &length)) {
    length = -1;
  } else {
    GST_DEBUG ("upstream length: %" G_GINT64_FORMAT, length);
  }
  return length;
}

static GstBuffer *
gst_wavpack_parse_pull_buffer (GstWavpackParse * wvparse, gint64 offset,
    guint size, GstFlowReturn * flow)
{
  GstFlowReturn flow_ret;

  GstBuffer *buf = NULL;

  if (offset + size > wvparse->upstream_length) {
    wvparse->upstream_length = gst_wavpack_parse_get_upstream_length (wvparse);
    if (offset + size > wvparse->upstream_length) {
      GST_DEBUG_OBJECT (wvparse, "EOS: %" G_GINT64_FORMAT " + %u > %"
          G_GINT64_FORMAT, offset, size, wvparse->upstream_length);
      flow_ret = GST_FLOW_UNEXPECTED;
      goto done;
    }
  }

  flow_ret = gst_pad_pull_range (wvparse->sinkpad, offset, size, &buf);

  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (wvparse, "pull_range (%" G_GINT64_FORMAT ", %u) "
        "failed, flow: %s", offset, size, gst_flow_get_name (flow_ret));
    buf = NULL;
    goto done;
  }

  if (GST_BUFFER_SIZE (buf) < size) {
    GST_DEBUG_OBJECT (wvparse, "Short read at offset %" G_GINT64_FORMAT
        ", got only %u of %u bytes", offset, GST_BUFFER_SIZE (buf), size);
    gst_buffer_unref (buf);
    buf = NULL;
    flow_ret = GST_FLOW_UNEXPECTED;
  }

done:
  if (flow)
    *flow = flow_ret;
  return buf;
}

static gboolean
gst_wavpack_parse_create_src_pad (GstWavpackParse * wvparse, GstBuffer * buf,
    WavpackHeader * header)
{
  GstWavpackMetadata meta;

  GstCaps *caps = NULL;

  guchar *bufptr;

  g_assert (wvparse->srcpad == NULL);

  bufptr = GST_BUFFER_DATA (buf) + sizeof (WavpackHeader);

  while (gst_wavpack_read_metadata (&meta, GST_BUFFER_DATA (buf), &bufptr)) {
    switch (meta.id) {
      case ID_WVC_BITSTREAM:{
        caps = gst_caps_new_simple ("audio/x-wavpack-correction",
            "framed", G_TYPE_BOOLEAN, TRUE, NULL);
        wvparse->srcpad =
            gst_pad_new_from_template (gst_element_class_get_pad_template
            (GST_ELEMENT_GET_CLASS (wvparse), "wvcsrc"), "wvcsrc");
        break;
      }
      case ID_WV_BITSTREAM:
      case ID_WVX_BITSTREAM:{
        WavpackStreamReader *stream_reader = gst_wavpack_stream_reader_new ();

        WavpackContext *wpc;

        gchar error_msg[80];

        read_id rid;

        gint channel_mask;

        rid.buffer = GST_BUFFER_DATA (buf);
        rid.length = GST_BUFFER_SIZE (buf);
        rid.position = 0;

        wpc =
            WavpackOpenFileInputEx (stream_reader, &rid, NULL, error_msg, 0, 0);

        if (!wpc)
          return FALSE;

        wvparse->samplerate = WavpackGetSampleRate (wpc);
        wvparse->channels = WavpackGetNumChannels (wpc);
        wvparse->total_samples =
            (header->total_samples ==
            0xffffffff) ? G_GINT64_CONSTANT (-1) : header->total_samples;

        caps = gst_caps_new_simple ("audio/x-wavpack",
            "width", G_TYPE_INT, WavpackGetBitsPerSample (wpc),
            "channels", G_TYPE_INT, wvparse->channels,
            "rate", G_TYPE_INT, wvparse->samplerate,
            "framed", G_TYPE_BOOLEAN, TRUE, NULL);
#ifdef WAVPACK_OLD_API
        channel_mask = wpc->config.channel_mask;
#else
        channel_mask = WavpackGetChannelMask (wpc);
#endif
        if (channel_mask == 0)
          channel_mask =
              gst_wavpack_get_default_channel_mask (wvparse->channels);

        if (channel_mask != 0) {
          if (!gst_wavpack_set_channel_layout (caps, channel_mask)) {
            GST_WARNING_OBJECT (wvparse, "Failed to set channel layout");
            gst_caps_unref (caps);
            caps = NULL;
            WavpackCloseFile (wpc);
            g_free (stream_reader);
            break;
          }
        }

        wvparse->srcpad =
            gst_pad_new_from_template (gst_element_class_get_pad_template
            (GST_ELEMENT_GET_CLASS (wvparse), "src"), "src");
        WavpackCloseFile (wpc);
        g_free (stream_reader);
        break;
      }
      default:{
        GST_LOG_OBJECT (wvparse, "unhandled ID: 0x%02x", meta.id);
        break;
      }
    }
    if (caps != NULL)
      break;
  }

  if (caps == NULL || wvparse->srcpad == NULL)
    return FALSE;

  GST_DEBUG_OBJECT (wvparse, "Added src pad with caps %" GST_PTR_FORMAT, caps);

  gst_pad_set_query_function (wvparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_src_query));
  gst_pad_set_query_type_function (wvparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_get_src_query_types));
  gst_pad_set_event_function (wvparse->srcpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_parse_src_event));

  gst_pad_set_caps (wvparse->srcpad, caps);
  gst_caps_unref (caps);
  gst_pad_use_fixed_caps (wvparse->srcpad);

  gst_object_ref (wvparse->srcpad);
  gst_pad_set_active (wvparse->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (wvparse), wvparse->srcpad);
  gst_element_no_more_pads (GST_ELEMENT (wvparse));

  return TRUE;
}

static GstFlowReturn
gst_wavpack_parse_push_buffer (GstWavpackParse * wvparse, GstBuffer * buf,
    WavpackHeader * header)
{
  GstFlowReturn ret;
  wvparse->current_offset += header->ckSize + 8;

  wvparse->segment.last_stop = header->block_index;

  if (wvparse->need_newsegment) {
    if (gst_wavpack_parse_send_newsegment (wvparse, FALSE))
      wvparse->need_newsegment = FALSE;
  }

  /* send any queued events */
  if (wvparse->queued_events) {
    GList *l;

    for (l = wvparse->queued_events; l != NULL; l = l->next) {
      gst_pad_push_event (wvparse->srcpad, GST_EVENT (l->data));
    }
    g_list_free (wvparse->queued_events);
    wvparse->queued_events = NULL;
  }

  if (wvparse->pending_buffer == NULL) {
    wvparse->pending_buffer = buf;
    wvparse->pending_offset = header->block_index;
  } else if (wvparse->pending_offset == header->block_index) {
    wvparse->pending_buffer = gst_buffer_join (wvparse->pending_buffer, buf);
  } else {
    GST_ERROR ("Got incomplete block, dropping");
    gst_buffer_unref (wvparse->pending_buffer);
    wvparse->pending_buffer = buf;
    wvparse->pending_offset = header->block_index;
  }

  if (!(header->flags & FINAL_BLOCK))
    return GST_FLOW_OK;

  buf = wvparse->pending_buffer;
  wvparse->pending_buffer = NULL;

  GST_BUFFER_TIMESTAMP (buf) = gst_util_uint64_scale_int (header->block_index,
      GST_SECOND, wvparse->samplerate);
  GST_BUFFER_DURATION (buf) = gst_util_uint64_scale_int (header->block_samples,
      GST_SECOND, wvparse->samplerate);
  GST_BUFFER_OFFSET (buf) = header->block_index;
  GST_BUFFER_OFFSET_END (buf) = header->block_index + header->block_samples;

  if (wvparse->discont || wvparse->next_block_index != header->block_index) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    wvparse->discont = FALSE;
  }

  wvparse->next_block_index = header->block_index + header->block_samples;

  gst_buffer_set_caps (buf, GST_PAD_CAPS (wvparse->srcpad));

  GST_LOG_OBJECT (wvparse, "Pushing buffer with time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  ret = gst_pad_push (wvparse->srcpad, buf);

  wvparse->segment.last_stop = wvparse->next_block_index;

  return ret;
}

static guint8 *
gst_wavpack_parse_find_marker (guint8 * buf, guint size)
{
  int i;

  guint8 *ret = NULL;

  if (G_UNLIKELY (size < 4))
    return NULL;

  for (i = 0; i < size - 4; i++) {
    if (memcmp (buf + i, "wvpk", 4) == 0) {
      ret = buf + i;
      break;
    }
  }
  return ret;
}

static GstFlowReturn
gst_wavpack_parse_resync_loop (GstWavpackParse * parse, WavpackHeader * header)
{
  GstFlowReturn flow_ret = GST_FLOW_UNEXPECTED;

  GstBuffer *buf = NULL;

  /* loop until we have a frame header or reach the end of the stream */
  while (1) {
    guint8 *data, *marker;

    guint len, size;

    if (buf) {
      gst_buffer_unref (buf);
      buf = NULL;
    }

    if (parse->upstream_length == 0 ||
        parse->upstream_length <= parse->current_offset) {
      parse->upstream_length = gst_wavpack_parse_get_upstream_length (parse);
      if (parse->upstream_length == 0 ||
          parse->upstream_length <= parse->current_offset) {
        break;
      }
    }

    len = MIN (parse->upstream_length - parse->current_offset, 2048);

    GST_LOG_OBJECT (parse, "offset: %" G_GINT64_FORMAT, parse->current_offset);

    buf = gst_wavpack_parse_pull_buffer (parse, parse->current_offset,
        len, &flow_ret);

    /* whatever the problem is, there's nothing more for us to do for now */
    if (flow_ret != GST_FLOW_OK)
      break;

    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    /* not enough data for a header? */
    if (size < sizeof (WavpackHeader))
      break;

    /* got a header right where we are at now? */
    if (gst_wavpack_read_header (header, data))
      break;

    /* nope, let's see if we can find one */
    marker = gst_wavpack_parse_find_marker (data + 1, size - 1);

    if (marker) {
      parse->current_offset += marker - data;
      /* do one more loop iteration to make sure we pull enough
       * data for a full header, we'll bail out then */
    } else {
      parse->current_offset += len - 4;
    }
  }

  if (buf)
    gst_buffer_unref (buf);

  return flow_ret;
}

static void
gst_wavpack_parse_loop (GstElement * element)
{
  GstWavpackParse *parse = GST_WAVPACK_PARSE (element);

  GstFlowReturn flow_ret;
  WavpackHeader header = { {0,}, 0, };
  GstBuffer *buf = NULL;

  flow_ret = gst_wavpack_parse_resync_loop (parse, &header);

  if (flow_ret != GST_FLOW_OK)
    goto pause;

  GST_LOG_OBJECT (parse, "Read header at offset %" G_GINT64_FORMAT
      ": chunk size = %u+8", parse->current_offset, header.ckSize);

  buf = gst_wavpack_parse_pull_buffer (parse, parse->current_offset,
      header.ckSize + 8, &flow_ret);

  if (flow_ret != GST_FLOW_OK)
    goto pause;

  if (parse->srcpad == NULL) {
    if (!gst_wavpack_parse_create_src_pad (parse, buf, &header)) {
      GST_ERROR_OBJECT (parse, "Failed to create src pad");
      flow_ret = GST_FLOW_ERROR;
      goto pause;
    }
  }
  if (header.flags & INITIAL_BLOCK)
    gst_wavpack_parse_index_append_entry (parse, parse->current_offset,
        header.block_index, header.block_samples);

  flow_ret = gst_wavpack_parse_push_buffer (parse, buf, &header);
  if (flow_ret != GST_FLOW_OK)
    goto pause;

  return;

pause:
  {
    const gchar *reason = gst_flow_get_name (flow_ret);

    GST_LOG_OBJECT (parse, "pausing task, reason %s", reason);
    gst_pad_pause_task (parse->sinkpad);

    if (flow_ret == GST_FLOW_UNEXPECTED && parse->srcpad) {
      if (parse->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        GstClockTime stop;

        GST_LOG_OBJECT (parse, "Sending segment done");

        if ((stop = parse->segment.stop) == -1)
          stop = parse->segment.duration;

        gst_element_post_message (GST_ELEMENT_CAST (parse),
            gst_message_new_segment_done (GST_OBJECT_CAST (parse),
                parse->segment.format, stop));
      } else {
        GST_LOG_OBJECT (parse, "Sending EOS, at end of stream");
        gst_pad_push_event (parse->srcpad, gst_event_new_eos ());
      }
    } else if (flow_ret == GST_FLOW_NOT_LINKED
        || flow_ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (parse, STREAM, FAILED,
          (_("Internal data stream error.")), ("stream stopped, reason %s",
              reason));
      if (parse->srcpad)
        gst_pad_push_event (parse->srcpad, gst_event_new_eos ());
    }
    return;
  }
}

static gboolean
gst_wavpack_parse_resync_adapter (GstAdapter * adapter)
{
  const guint8 *buf, *marker;

  guint avail = gst_adapter_available (adapter);

  if (avail < 4)
    return FALSE;

  /* if the marker is at the beginning don't do the expensive search */
  buf = gst_adapter_peek (adapter, 4);
  if (memcmp (buf, "wvpk", 4) == 0)
    return TRUE;

  if (avail == 4)
    return FALSE;

  /* search for the marker in the complete content of the adapter */
  buf = gst_adapter_peek (adapter, avail);
  if (buf && (marker = gst_wavpack_parse_find_marker ((guint8 *) buf, avail))) {
    gst_adapter_flush (adapter, marker - buf);
    return TRUE;
  }

  /* flush everything except the last 4 bytes. they could contain
   * the start of a new marker */
  gst_adapter_flush (adapter, avail - 4);

  return FALSE;
}

static GstFlowReturn
gst_wavpack_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstWavpackParse *wvparse = GST_WAVPACK_PARSE (GST_PAD_PARENT (pad));

  GstFlowReturn ret = GST_FLOW_OK;

  WavpackHeader wph;

  const guint8 *tmp_buf;

  if (!wvparse->adapter) {
    wvparse->adapter = gst_adapter_new ();
  }

  if (GST_BUFFER_IS_DISCONT (buf)) {
    gst_adapter_clear (wvparse->adapter);
    wvparse->discont = TRUE;
  }

  gst_adapter_push (wvparse->adapter, buf);

  if (gst_adapter_available (wvparse->adapter) < sizeof (WavpackHeader))
    return ret;

  if (!gst_wavpack_parse_resync_adapter (wvparse->adapter))
    return ret;

  tmp_buf = gst_adapter_peek (wvparse->adapter, sizeof (WavpackHeader));
  gst_wavpack_read_header (&wph, (guint8 *) tmp_buf);

  while (gst_adapter_available (wvparse->adapter) >= wph.ckSize + 4 * 1 + 4) {
    GstBuffer *outbuf =
        gst_adapter_take_buffer (wvparse->adapter, wph.ckSize + 4 * 1 + 4);

    if (!outbuf)
      return GST_FLOW_ERROR;

    if (wvparse->srcpad == NULL) {
      if (!gst_wavpack_parse_create_src_pad (wvparse, outbuf, &wph)) {
        GST_ERROR_OBJECT (wvparse, "Failed to create src pad");
        ret = GST_FLOW_ERROR;
        break;
      }
    }

    ret = gst_wavpack_parse_push_buffer (wvparse, outbuf, &wph);

    if (ret != GST_FLOW_OK)
      break;

    if (gst_adapter_available (wvparse->adapter) >= sizeof (WavpackHeader)) {
      tmp_buf = gst_adapter_peek (wvparse->adapter, sizeof (WavpackHeader));

      if (!gst_wavpack_parse_resync_adapter (wvparse->adapter))
        break;

      gst_wavpack_read_header (&wph, (guint8 *) tmp_buf);
    }
  }

  return ret;
}

static GstStateChangeReturn
gst_wavpack_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstWavpackParse *wvparse = GST_WAVPACK_PARSE (element);

  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&wvparse->segment, GST_FORMAT_DEFAULT);
      wvparse->segment.last_stop = 0;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_wavpack_parse_reset (wvparse);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_wavpack_parse_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

static gboolean
gst_wavpack_parse_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  gboolean result;

  if (active) {
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_wavpack_parse_loop, GST_PAD_PARENT (sinkpad));
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

  return result;
}

gboolean
gst_wavpack_parse_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "wavpackparse",
          GST_RANK_PRIMARY, GST_TYPE_WAVPACK_PARSE)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_wavpack_parse_debug, "wavpack_parse", 0,
      "Wavpack file parser");

  return TRUE;
}
