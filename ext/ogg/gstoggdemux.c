/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstoggdemux.c: ogg stream demuxer
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
 * SECTION:element-oggdemux
 * @see_also: <link linkend="gst-plugins-base-plugins-oggmux">oggmux</link>
 *
 * This element demuxes ogg files into their encoded audio and video components.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=test.ogg ! oggdemux ! vorbisdec ! audioconvert ! alsasink
 * ]| Decodes the vorbis audio stored inside an ogg container.
 * </refsect2>
 *
 * Last reviewed on 2006-12-30 (0.10.5)
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/tag/tag.h>

#include "gstoggdemux.h"

#define CHUNKSIZE (8500)        /* this is out of vorbisfile */

#define GST_FLOW_LIMIT GST_FLOW_CUSTOM_ERROR

#define GST_CHAIN_LOCK(ogg)     g_mutex_lock((ogg)->chain_lock)
#define GST_CHAIN_UNLOCK(ogg)   g_mutex_unlock((ogg)->chain_lock)

GST_DEBUG_CATEGORY (gst_ogg_demux_debug);
GST_DEBUG_CATEGORY (gst_ogg_demux_setup_debug);
#define GST_CAT_DEFAULT gst_ogg_demux_debug


static ogg_packet *
_ogg_packet_copy (const ogg_packet * packet)
{
  ogg_packet *ret = g_new0 (ogg_packet, 1);

  *ret = *packet;
  ret->packet = g_memdup (packet->packet, packet->bytes);

  return ret;
}

static void
_ogg_packet_free (ogg_packet * packet)
{
  g_free (packet->packet);
  g_free (packet);
}

static ogg_page *
gst_ogg_page_copy (ogg_page * page)
{
  ogg_page *p = g_new0 (ogg_page, 1);

  /* make a copy of the page */
  p->header = g_memdup (page->header, page->header_len);
  p->header_len = page->header_len;
  p->body = g_memdup (page->body, page->body_len);
  p->body_len = page->body_len;

  return p;
}

static void
gst_ogg_page_free (ogg_page * page)
{
  g_free (page->header);
  g_free (page->body);
  g_free (page);
}

static gboolean gst_ogg_demux_collect_chain_info (GstOggDemux * ogg,
    GstOggChain * chain);
static gboolean gst_ogg_demux_activate_chain (GstOggDemux * ogg,
    GstOggChain * chain, GstEvent * event);
static void gst_ogg_chain_mark_discont (GstOggChain * chain);

static gboolean gst_ogg_demux_perform_seek (GstOggDemux * ogg,
    GstEvent * event);
static gboolean gst_ogg_demux_receive_event (GstElement * element,
    GstEvent * event);

static void gst_ogg_pad_dispose (GObject * object);
static void gst_ogg_pad_finalize (GObject * object);

static const GstQueryType *gst_ogg_pad_query_types (GstPad * pad);
static gboolean gst_ogg_pad_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_ogg_pad_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_ogg_pad_getcaps (GstPad * pad);
static GstOggPad *gst_ogg_chain_get_stream (GstOggChain * chain,
    glong serialno);

static GstFlowReturn gst_ogg_demux_combine_flows (GstOggDemux * ogg,
    GstOggPad * pad, GstFlowReturn ret);
static void gst_ogg_demux_sync_streams (GstOggDemux * ogg);

GType gst_ogg_pad_get_type (void);
G_DEFINE_TYPE (GstOggPad, gst_ogg_pad, GST_TYPE_PAD);

static void
gst_ogg_pad_class_init (GstOggPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = gst_ogg_pad_dispose;
  gobject_class->finalize = gst_ogg_pad_finalize;
}

static void
gst_ogg_pad_init (GstOggPad * pad)
{
  gst_pad_set_event_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_event));
  gst_pad_set_getcaps_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_getcaps));
  gst_pad_set_query_type_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_query_types));
  gst_pad_set_query_function (GST_PAD (pad),
      GST_DEBUG_FUNCPTR (gst_ogg_pad_src_query));

  pad->mode = GST_OGG_PAD_MODE_INIT;

  pad->current_granule = -1;
  pad->keyframe_granule = -1;

  pad->start_time = GST_CLOCK_TIME_NONE;

  pad->last_stop = GST_CLOCK_TIME_NONE;

  pad->have_type = FALSE;
  pad->continued = NULL;
  pad->map.headers = NULL;
  pad->map.queued = NULL;
}

static void
gst_ogg_pad_dispose (GObject * object)
{
  GstOggPad *pad = GST_OGG_PAD (object);

  pad->chain = NULL;
  pad->ogg = NULL;

  g_list_foreach (pad->map.headers, (GFunc) _ogg_packet_free, NULL);
  g_list_free (pad->map.headers);
  pad->map.headers = NULL;
  g_list_foreach (pad->map.queued, (GFunc) _ogg_packet_free, NULL);
  g_list_free (pad->map.queued);
  pad->map.queued = NULL;

  g_free (pad->map.index);
  pad->map.index = NULL;

  /* clear continued pages */
  g_list_foreach (pad->continued, (GFunc) gst_ogg_page_free, NULL);
  g_list_free (pad->continued);
  pad->continued = NULL;

  ogg_stream_reset (&pad->map.stream);

  G_OBJECT_CLASS (gst_ogg_pad_parent_class)->dispose (object);
}

static void
gst_ogg_pad_finalize (GObject * object)
{
  GstOggPad *pad = GST_OGG_PAD (object);

  ogg_stream_clear (&pad->map.stream);

  G_OBJECT_CLASS (gst_ogg_pad_parent_class)->finalize (object);
}

static const GstQueryType *
gst_ogg_pad_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return query_types;
}

static GstCaps *
gst_ogg_pad_getcaps (GstPad * pad)
{
  return gst_caps_ref (GST_PAD_CAPS (pad));
}

static gboolean
gst_ogg_pad_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 total_time = -1;

      gst_query_parse_duration (query, &format, NULL);
      /* can only get position in time */
      if (format != GST_FORMAT_TIME)
        goto wrong_format;

      if (ogg->total_time != -1) {
        /* we can return the total length */
        total_time = ogg->total_time;
      } else {
        gint bitrate = ogg->bitrate;

        /* try with length and bitrate */
        if (bitrate > 0) {
          GstQuery *uquery;

          /* ask upstream for total length in bytes */
          uquery = gst_query_new_duration (GST_FORMAT_BYTES);
          if (gst_pad_peer_query (ogg->sinkpad, uquery)) {
            gint64 length;

            gst_query_parse_duration (uquery, NULL, &length);

            /* estimate using the bitrate */
            total_time =
                gst_util_uint64_scale (length, 8 * GST_SECOND, bitrate);

            GST_LOG_OBJECT (ogg,
                "length: %" G_GINT64_FORMAT ", bitrate %d, total_time %"
                GST_TIME_FORMAT, length, bitrate, GST_TIME_ARGS (total_time));
          }
          gst_query_unref (uquery);
        }
      }

      gst_query_set_duration (query, GST_FORMAT_TIME, total_time);
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GstFormat format;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format == GST_FORMAT_TIME) {
        gboolean seekable = FALSE;
        gint64 stop = -1;

        if (ogg->pullmode) {
          seekable = TRUE;
          stop = ogg->total_time;
        } else if (ogg->current_chain->streams->len) {
          gint i;

          seekable = FALSE;
          for (i = 0; i < ogg->current_chain->streams->len; i++) {
            GstOggPad *pad =
                g_array_index (ogg->current_chain->streams, GstOggPad *, i);

            seekable |= (pad->map.index != NULL && pad->map.n_index != 0);

            if (pad->map.index != NULL && pad->map.n_index != 0) {
              GstOggIndex *idx;
              GstClockTime idx_time;

              idx = &pad->map.index[pad->map.n_index - 1];
              idx_time =
                  gst_util_uint64_scale (idx->timestamp, GST_SECOND,
                  pad->map.kp_denom);
              if (stop == -1)
                stop = idx_time;
              else
                stop = MAX (idx_time, stop);
            }
          }
        }

        gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0, stop);
      } else {
        res = FALSE;
      }
      break;
    }

    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (ogg);

  return res;

  /* ERRORS */
wrong_format:
  {
    GST_DEBUG_OBJECT (ogg, "only query duration on TIME is supported");
    res = FALSE;
    goto done;
  }
}

static gboolean
gst_ogg_demux_receive_event (GstElement * element, GstEvent * event)
{
  gboolean res;
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (element);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* now do the seek */
      res = gst_ogg_demux_perform_seek (ogg, event);
      gst_event_unref (event);
      break;
    default:
      GST_DEBUG_OBJECT (ogg, "We only handle seek events here");
      goto error;
  }

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (ogg, "error handling event");
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_ogg_pad_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* now do the seek */
      res = gst_ogg_demux_perform_seek (ogg, event);
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (ogg);

  return res;
}

static void
gst_ogg_pad_reset (GstOggPad * pad)
{
  ogg_stream_reset (&pad->map.stream);

  GST_DEBUG_OBJECT (pad, "doing reset");

  /* clear continued pages */
  g_list_foreach (pad->continued, (GFunc) gst_ogg_page_free, NULL);
  g_list_free (pad->continued);
  pad->continued = NULL;

  pad->last_ret = GST_FLOW_OK;
  pad->last_stop = GST_CLOCK_TIME_NONE;
  pad->current_granule = -1;
  pad->keyframe_granule = -1;
  pad->is_eos = FALSE;
}

/* queue data, basically takes the packet, puts it in a buffer and store the
 * buffer in the queued list.  */
static GstFlowReturn
gst_ogg_demux_queue_data (GstOggPad * pad, ogg_packet * packet)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstOggDemux *ogg = pad->ogg;
#endif

  GST_DEBUG_OBJECT (ogg, "%p queueing data serial %08lx", pad,
      pad->map.serialno);

  pad->map.queued = g_list_append (pad->map.queued, _ogg_packet_copy (packet));

  /* we are ok now */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ogg_demux_chain_peer (GstOggPad * pad, ogg_packet * packet,
    gboolean push_headers)
{
  GstBuffer *buf = NULL;
  GstFlowReturn ret, cret;
  GstOggDemux *ogg = pad->ogg;
  gint64 current_time;
  GstOggChain *chain;
  gint64 duration;
  gint offset;
  gint trim;
  GstClockTime out_timestamp, out_duration;
  guint64 out_offset, out_offset_end;
  gboolean delta_unit = FALSE;

  GST_DEBUG_OBJECT (ogg,
      "%p streaming to peer serial %08lx", pad, pad->map.serialno);

  if (pad->map.is_ogm) {
    const guint8 *data;
    long bytes;

    data = packet->packet;
    bytes = packet->bytes;

    if (bytes < 1)
      goto empty_packet;

    if (data[0] & 1) {
      /* We don't push header packets for OGM */
      cret = gst_ogg_demux_combine_flows (ogg, pad, GST_FLOW_OK);
      goto done;
    } else if (data[0] & 3 && pad->map.is_ogm_text) {
      GstTagList *tags;

      /* We don't push comment packets either for text streams,
       * other streams will handle the comment packets in the
       * decoder */
      buf = gst_buffer_new ();

      GST_BUFFER_DATA (buf) = (guint8 *) data;
      GST_BUFFER_SIZE (buf) = bytes;

      tags = gst_tag_list_from_vorbiscomment_buffer (buf,
          (const guint8 *) "\003vorbis", 7, NULL);
      gst_buffer_unref (buf);
      buf = NULL;

      if (tags) {
        GST_DEBUG_OBJECT (ogg, "tags = %" GST_PTR_FORMAT, tags);
        gst_element_found_tags_for_pad (GST_ELEMENT (ogg), GST_PAD_CAST (pad),
            tags);
      } else {
        GST_DEBUG_OBJECT (ogg, "failed to extract tags from vorbis comment");
      }

      cret = gst_ogg_demux_combine_flows (ogg, pad, GST_FLOW_OK);
      goto done;
    }

    offset = 1 + (((data[0] & 0xc0) >> 6) | ((data[0] & 0x02) << 1));
    delta_unit = (((data[0] & 0x08) >> 3) == 0);

    trim = 0;

    /* Strip trailing \0 for subtitles */
    if (pad->map.is_ogm_text) {
      while (bytes && data[bytes - 1] == 0) {
        trim++;
        bytes--;
      }
    }
  } else if (pad->map.is_vp8) {
    if (packet->bytes >= 7 && memcmp (packet->packet, "OVP80\2 ", 7) == 0) {
      GstTagList *tags;

      buf = gst_buffer_new ();

      GST_BUFFER_DATA (buf) = (guint8 *) packet->packet;
      GST_BUFFER_SIZE (buf) = packet->bytes;

      tags = gst_tag_list_from_vorbiscomment_buffer (buf,
          (const guint8 *) "OVP80\2 ", 7, NULL);
      gst_buffer_unref (buf);
      buf = NULL;

      if (tags) {
        GST_DEBUG_OBJECT (ogg, "tags = %" GST_PTR_FORMAT, tags);
        gst_element_found_tags_for_pad (GST_ELEMENT (ogg), GST_PAD_CAST (pad),
            tags);
      } else {
        GST_DEBUG_OBJECT (ogg,
            "failed to extract VP8 tags from vorbis comment");
      }
      /* We don't push header packets for VP8 */
      cret = gst_ogg_demux_combine_flows (ogg, pad, GST_FLOW_OK);
      goto done;
    } else if (packet->b_o_s || (packet->bytes >= 5
            && memcmp (packet->packet, "OVP80", 5) == 0)) {
      /* We don't push header packets for VP8 */
      cret = gst_ogg_demux_combine_flows (ogg, pad, GST_FLOW_OK);
      goto done;
    }
    offset = 0;
    trim = 0;
  } else {
    offset = 0;
    trim = 0;
  }

  /* get timing info for the packet */
  duration = gst_ogg_stream_get_packet_duration (&pad->map, packet);
  GST_DEBUG_OBJECT (ogg, "packet duration %" G_GUINT64_FORMAT, duration);

  if (packet->b_o_s) {
    out_timestamp = GST_CLOCK_TIME_NONE;
    out_duration = GST_CLOCK_TIME_NONE;
    out_offset = 0;
    out_offset_end = -1;
  } else {
    if (packet->granulepos != -1) {
      pad->current_granule = gst_ogg_stream_granulepos_to_granule (&pad->map,
          packet->granulepos);
      pad->keyframe_granule =
          gst_ogg_stream_granulepos_to_key_granule (&pad->map,
          packet->granulepos);
      GST_DEBUG_OBJECT (ogg, "new granule %" G_GUINT64_FORMAT,
          pad->current_granule);
    } else if (ogg->segment.rate > 0.0 && pad->current_granule != -1) {
      pad->current_granule += duration;
      GST_DEBUG_OBJECT (ogg, "interpollating granule %" G_GUINT64_FORMAT,
          pad->current_granule);
    }
    if (ogg->segment.rate < 0.0 && packet->granulepos == -1) {
      /* negative rates, only set timestamp on the packets with a granulepos */
      out_timestamp = -1;
      out_duration = -1;
      out_offset = -1;
      out_offset_end = -1;
    } else {
      /* we only push buffers after we have a valid granule. This is done so that
       * we nicely skip packets without a timestamp after a seek. This is ok
       * because we base or seek on the packet after the page with the smaller
       * timestamp. */
      if (pad->current_granule == -1)
        goto no_timestamp;

      if (pad->map.is_ogm) {
        out_timestamp = gst_ogg_stream_granule_to_time (&pad->map,
            pad->current_granule);
        out_duration = gst_util_uint64_scale (duration,
            GST_SECOND * pad->map.granulerate_d, pad->map.granulerate_n);
      } else if (pad->is_sparse) {
        out_timestamp = gst_ogg_stream_granule_to_time (&pad->map,
            pad->current_granule);
        out_duration = GST_CLOCK_TIME_NONE;
      } else {
        out_timestamp = gst_ogg_stream_granule_to_time (&pad->map,
            pad->current_granule - duration);
        out_duration =
            gst_ogg_stream_granule_to_time (&pad->map,
            pad->current_granule) - out_timestamp;
      }
      out_offset_end =
          gst_ogg_stream_granule_to_granulepos (&pad->map,
          pad->current_granule, pad->keyframe_granule);
      out_offset =
          gst_ogg_stream_granule_to_time (&pad->map, pad->current_granule);
    }
  }

  if (pad->map.is_ogm_text) {
    /* check for invalid buffer sizes */
    if (G_UNLIKELY (offset + trim >= packet->bytes))
      goto empty_packet;
  }

  ret =
      gst_pad_alloc_buffer_and_set_caps (GST_PAD_CAST (pad),
      GST_BUFFER_OFFSET_NONE, packet->bytes - offset - trim,
      GST_PAD_CAPS (pad), &buf);

  /* combine flows */
  cret = gst_ogg_demux_combine_flows (ogg, pad, ret);
  if (ret != GST_FLOW_OK)
    goto no_buffer;

  /* set delta flag for OGM content */
  if (delta_unit)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  /* copy packet in buffer */
  memcpy (buf->data, packet->packet + offset, packet->bytes - offset - trim);

  GST_BUFFER_TIMESTAMP (buf) = out_timestamp;
  GST_BUFFER_DURATION (buf) = out_duration;
  GST_BUFFER_OFFSET (buf) = out_offset;
  GST_BUFFER_OFFSET_END (buf) = out_offset_end;

  /* Mark discont on the buffer */
  if (pad->discont) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    pad->discont = FALSE;
  }

  pad->last_stop = ogg->segment.last_stop;

  /* don't push the header packets when we are asked to skip them */
  if (!packet->b_o_s || push_headers) {
    ret = gst_pad_push (GST_PAD_CAST (pad), buf);
    buf = NULL;

    /* combine flows */
    cret = gst_ogg_demux_combine_flows (ogg, pad, ret);
  }

  /* we're done with skeleton stuff */
  if (pad->map.is_skeleton)
    goto done;

  /* check if valid granulepos, then we can calculate the current
   * position. We know the granule for each packet but we only want to update
   * the last_stop when we have a valid granulepos on the packet because else
   * our time jumps around for the different streams. */
  if (packet->granulepos < 0)
    goto done;

  /* convert to time */
  current_time = gst_ogg_stream_get_end_time_for_granulepos (&pad->map,
      packet->granulepos);

  /* convert to stream time */
  if ((chain = pad->chain)) {
    gint64 chain_start = 0;

    if (chain->segment_start != GST_CLOCK_TIME_NONE)
      chain_start = chain->segment_start;

    current_time = current_time - chain_start + chain->begin_time;
  }

  /* and store as the current position */
  gst_segment_set_last_stop (&ogg->segment, GST_FORMAT_TIME, current_time);

  GST_DEBUG_OBJECT (ogg, "ogg current time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (current_time));

  /* check stream eos */
  if ((ogg->segment.rate > 0.0 && ogg->segment.stop != GST_CLOCK_TIME_NONE &&
          current_time > ogg->segment.stop) ||
      (ogg->segment.rate < 0.0 && ogg->segment.start != GST_CLOCK_TIME_NONE &&
          current_time < ogg->segment.start)) {
    GST_DEBUG_OBJECT (ogg, "marking pad %p EOS", pad);
    pad->is_eos = TRUE;
  }

done:
  if (buf)
    gst_buffer_unref (buf);
  /* return combined flow result */
  return cret;

  /* special cases */
empty_packet:
  {
    GST_DEBUG_OBJECT (ogg, "Skipping empty packet");
    cret = gst_ogg_demux_combine_flows (ogg, pad, GST_FLOW_OK);
    goto done;
  }

no_timestamp:
  {
    GST_DEBUG_OBJECT (ogg, "skipping packet: no valid granule found yet");
    cret = gst_ogg_demux_combine_flows (ogg, pad, GST_FLOW_OK);
    goto done;
  }

no_buffer:
  {
    GST_DEBUG_OBJECT (ogg,
        "%p could not get buffer from peer %08lx, %d (%s), combined %d (%s)",
        pad, pad->map.serialno, ret, gst_flow_get_name (ret),
        cret, gst_flow_get_name (cret));
    goto done;
  }
}

static guint64
gst_ogg_demux_collect_start_time (GstOggDemux * ogg, GstOggChain * chain)
{
  gint i;
  guint64 start_time = G_MAXUINT64;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    if (pad->map.is_skeleton)
      continue;

    /*  can do this if the pad start time is not defined */
    if (pad->start_time == GST_CLOCK_TIME_NONE) {
      start_time = G_MAXUINT64;
      break;
    } else {
      start_time = MIN (start_time, pad->start_time);
    }
  }
  return start_time;
}

/* submit a packet to the oggpad, this function will run the
 * typefind code for the pad if this is the first packet for this
 * stream 
 */
static GstFlowReturn
gst_ogg_pad_submit_packet (GstOggPad * pad, ogg_packet * packet)
{
  gint64 granule;
  GstFlowReturn ret = GST_FLOW_OK;

  GstOggDemux *ogg = pad->ogg;

  GST_DEBUG_OBJECT (ogg, "%p submit packet serial %08lx", pad,
      pad->map.serialno);

  if (!pad->have_type) {
    pad->have_type = gst_ogg_stream_setup_map (&pad->map, packet);
    if (!pad->have_type) {
      pad->map.caps = gst_caps_new_simple ("application/x-unknown", NULL);
    }
    if (pad->map.is_skeleton) {
      GST_DEBUG_OBJECT (ogg, "we have a fishead");
      /* copy values over to global ogg level */
      ogg->basetime = pad->map.basetime;
      ogg->prestime = pad->map.prestime;

      /* use total time to update the total ogg time */
      if (ogg->total_time == -1) {
        ogg->total_time = pad->map.total_time;
      } else if (pad->map.total_time > 0) {
        ogg->total_time = MAX (ogg->total_time, pad->map.total_time);
      }
    }
    if (pad->map.caps) {
      gst_pad_set_caps (GST_PAD (pad), pad->map.caps);
    } else {
      GST_WARNING_OBJECT (ogg, "stream parser didn't create src pad caps");
    }
  }

  if (pad->map.is_skeleton) {
    guint32 serialno;
    GstOggPad *skel_pad;
    GstOggSkeleton type;

    /* try to parse the serialno first */
    if (gst_ogg_map_parse_fisbone (&pad->map, packet->packet, packet->bytes,
            &serialno, &type)) {

      GST_WARNING_OBJECT (pad->ogg,
          "got skeleton packet for stream 0x%08x", serialno);

      skel_pad = gst_ogg_chain_get_stream (pad->chain, serialno);
      if (skel_pad) {
        switch (type) {
          case GST_OGG_SKELETON_FISBONE:
            /* parse the remainder of the fisbone in the pad with the serialno,
             * note that we ignore the start_time as this is usually wrong for
             * live streams */
            gst_ogg_map_add_fisbone (&skel_pad->map, &pad->map, packet->packet,
                packet->bytes, NULL);
            break;
          case GST_OGG_SKELETON_INDEX:
            gst_ogg_map_add_index (&skel_pad->map, &pad->map, packet->packet,
                packet->bytes);

            /* use total time to update the total ogg time */
            if (ogg->total_time == -1) {
              ogg->total_time = skel_pad->map.total_time;
            } else if (skel_pad->map.total_time > 0) {
              ogg->total_time = MAX (ogg->total_time, skel_pad->map.total_time);
            }
            break;
          default:
            break;
        }

      } else {
        GST_WARNING_OBJECT (pad->ogg,
            "found skeleton fisbone for an unknown stream 0x%08x", serialno);
      }
    }
  }

  granule = gst_ogg_stream_granulepos_to_granule (&pad->map,
      packet->granulepos);
  if (granule != -1) {
    GST_DEBUG_OBJECT (ogg, "%p has granulepos %" G_GINT64_FORMAT, pad, granule);
    pad->current_granule = granule;
  }

  /* restart header packet count when seeing a b_o_s page;
   * particularly useful following a seek or even following chain finding */
  if (packet->b_o_s) {
    GST_DEBUG_OBJECT (ogg, "b_o_s packet, resetting header packet count");
    pad->map.n_header_packets_seen = 0;
    if (!pad->map.have_headers) {
      GST_DEBUG_OBJECT (ogg, "clearing header packets");
      g_list_foreach (pad->map.headers, (GFunc) _ogg_packet_free, NULL);
      g_list_free (pad->map.headers);
      pad->map.headers = NULL;
    }
  }

  /* Overload the value of b_o_s in ogg_packet with a flag whether or
   * not this is a header packet.  Maybe some day this could be cleaned
   * up.  */
  packet->b_o_s = gst_ogg_stream_packet_is_header (&pad->map, packet);
  if (!packet->b_o_s) {
    pad->map.have_headers = TRUE;
    if (pad->start_time == GST_CLOCK_TIME_NONE) {
      gint64 duration = gst_ogg_stream_get_packet_duration (&pad->map, packet);
      GST_DEBUG ("duration %" G_GINT64_FORMAT, duration);
      if (duration != -1) {
        pad->map.accumulated_granule += duration;
        GST_DEBUG ("accumulated granule %" G_GINT64_FORMAT,
            pad->map.accumulated_granule);
      }

      if (packet->granulepos != -1) {
        ogg_int64_t start_granule;
        gint64 granule;

        granule = gst_ogg_stream_granulepos_to_granule (&pad->map,
            packet->granulepos);

        if (granule > pad->map.accumulated_granule)
          start_granule = granule - pad->map.accumulated_granule;
        else
          start_granule = 0;

        pad->start_time = gst_ogg_stream_granule_to_time (&pad->map,
            start_granule);
        GST_DEBUG ("start time %" G_GINT64_FORMAT, pad->start_time);
      } else {
        packet->granulepos = gst_ogg_stream_granule_to_granulepos (&pad->map,
            pad->map.accumulated_granule, pad->keyframe_granule);
      }
    }
  } else {
    pad->map.n_header_packets_seen++;
    if (!pad->map.have_headers) {
      pad->map.headers =
          g_list_append (pad->map.headers, _ogg_packet_copy (packet));
      GST_DEBUG ("keeping header packet %d", pad->map.n_header_packets_seen);
    }
  }

  /* we know the start_time of the pad data, see if we
   * can activate the complete chain if this is a dynamic
   * chain. We need all the headers too for this. */
  if (pad->start_time != GST_CLOCK_TIME_NONE && pad->map.have_headers) {
    GstOggChain *chain = pad->chain;

    /* check if complete chain has start time */
    if (chain == ogg->building_chain) {
      GstEvent *event = NULL;

      if (ogg->resync) {
        guint64 start_time;

        GST_DEBUG_OBJECT (ogg, "need to resync");

        /* when we need to resync after a seek, we wait until we have received
         * timestamps on all streams */
        start_time = gst_ogg_demux_collect_start_time (ogg, chain);

        if (start_time != G_MAXUINT64) {
          gint64 segment_time;

          GST_DEBUG_OBJECT (ogg, "start_time:  %" GST_TIME_FORMAT,
              GST_TIME_ARGS (start_time));

          if (chain->segment_start < start_time)
            segment_time =
                (start_time - chain->segment_start) + chain->begin_time;
          else
            segment_time = chain->begin_time;

          /* create the newsegment event we are going to send out */
          event = gst_event_new_new_segment (FALSE, ogg->segment.rate,
              GST_FORMAT_TIME, start_time, chain->segment_stop, segment_time);

          ogg->resync = FALSE;
        }
      } else {
        /* see if we have enough info to activate the chain, we have enough info
         * when all streams have a valid start time. */
        if (gst_ogg_demux_collect_chain_info (ogg, chain)) {

          GST_DEBUG_OBJECT (ogg, "segment_start: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (chain->segment_start));
          GST_DEBUG_OBJECT (ogg, "segment_stop:  %" GST_TIME_FORMAT,
              GST_TIME_ARGS (chain->segment_stop));
          GST_DEBUG_OBJECT (ogg, "segment_time:  %" GST_TIME_FORMAT,
              GST_TIME_ARGS (chain->begin_time));

          /* create the newsegment event we are going to send out */
          event = gst_event_new_new_segment (FALSE, ogg->segment.rate,
              GST_FORMAT_TIME, chain->segment_start, chain->segment_stop,
              chain->begin_time);
        }
      }

      if (event) {
        gst_event_set_seqnum (event, ogg->seqnum);

        gst_ogg_demux_activate_chain (ogg, chain, event);

        ogg->building_chain = NULL;
      }
    }
  }

  /* if we are building a chain, store buffer for when we activate
   * it. This path is taken if we operate in streaming mode. */
  if (ogg->building_chain) {
    /* bos packets where stored in the header list so we can discard
     * them here*/
    if (!packet->b_o_s)
      ret = gst_ogg_demux_queue_data (pad, packet);
  }
  /* else we are completely streaming to the peer */
  else {
    ret = gst_ogg_demux_chain_peer (pad, packet, !ogg->pullmode);
  }
  return ret;
}

/* flush at most @npackets from the stream layer. All packets if 
 * @npackets is 0;
 */
static GstFlowReturn
gst_ogg_pad_stream_out (GstOggPad * pad, gint npackets)
{
  GstFlowReturn result = GST_FLOW_OK;
  gboolean done = FALSE;
  GstOggDemux *ogg;

  ogg = pad->ogg;

  while (!done) {
    int ret;
    ogg_packet packet;

    ret = ogg_stream_packetout (&pad->map.stream, &packet);
    switch (ret) {
      case 0:
        GST_LOG_OBJECT (ogg, "packetout done");
        done = TRUE;
        break;
      case -1:
        GST_LOG_OBJECT (ogg, "packetout discont");
        gst_ogg_chain_mark_discont (pad->chain);
        break;
      case 1:
        GST_LOG_OBJECT (ogg, "packetout gave packet of size %ld", packet.bytes);
        result = gst_ogg_pad_submit_packet (pad, &packet);
        if (GST_FLOW_IS_FATAL (result))
          goto could_not_submit;
        break;
      default:
        GST_WARNING_OBJECT (ogg,
            "invalid return value %d for ogg_stream_packetout, resetting stream",
            ret);
        gst_ogg_pad_reset (pad);
        break;
    }
    if (npackets > 0) {
      npackets--;
      done = (npackets == 0);
    }
  }
  return result;

  /* ERRORS */
could_not_submit:
  {
    GST_WARNING_OBJECT (ogg,
        "could not submit packet for stream %08lx, error: %d",
        pad->map.serialno, result);
    gst_ogg_pad_reset (pad);
    return result;
  }
}

/* submit a page to an oggpad, this function will then submit all
 * the packets in the page.
 */
static GstFlowReturn
gst_ogg_pad_submit_page (GstOggPad * pad, ogg_page * page)
{
  GstFlowReturn result = GST_FLOW_OK;
  GstOggDemux *ogg;
  gboolean continued = FALSE;

  ogg = pad->ogg;

  /* for negative rates we read pages backwards and must therefore be carefull
   * with continued pages */
  if (ogg->segment.rate < 0.0) {
    gint npackets;

    continued = ogg_page_continued (page);

    /* number of completed packets in the page */
    npackets = ogg_page_packets (page);
    if (!continued) {
      /* page is not continued so it contains at least one packet start. It's
       * possible that no packet ends on this page (npackets == 0). In that
       * case, the next (continued) page(s) we kept contain the remainder of the
       * packets. We mark npackets=1 to make us start decoding the pages in the
       * remainder of the algorithm. */
      if (npackets == 0)
        npackets = 1;
    }
    GST_LOG_OBJECT (ogg, "continued: %d, %d packets", continued, npackets);

    if (npackets == 0) {
      GST_LOG_OBJECT (ogg, "no decodable packets, we need a previous page");
      goto done;
    }
  }

  if (ogg_stream_pagein (&pad->map.stream, page) != 0)
    goto choked;

  /* flush all packets in the stream layer, this might not give a packet if
   * the page had no packets finishing on the page (npackets == 0). */
  result = gst_ogg_pad_stream_out (pad, 0);

  if (pad->continued) {
    ogg_packet packet;

    /* now send the continued pages to the stream layer */
    while (pad->continued) {
      ogg_page *p = (ogg_page *) pad->continued->data;

      GST_LOG_OBJECT (ogg, "submitting continued page %p", p);
      if (ogg_stream_pagein (&pad->map.stream, p) != 0)
        goto choked;

      pad->continued = g_list_delete_link (pad->continued, pad->continued);

      /* free the page */
      gst_ogg_page_free (p);
    }

    GST_LOG_OBJECT (ogg, "flushing last continued packet");
    /* flush 1 continued packet in the stream layer */
    result = gst_ogg_pad_stream_out (pad, 1);

    /* flush all remaining packets, we pushed them in the previous round.
     * We don't use _reset() because we still want to get the discont when
     * we submit a next page. */
    while (ogg_stream_packetout (&pad->map.stream, &packet) != 0);
  }

done:
  /* keep continued pages (only in reverse mode) */
  if (continued) {
    ogg_page *p = gst_ogg_page_copy (page);

    GST_LOG_OBJECT (ogg, "keeping continued page %p", p);
    pad->continued = g_list_prepend (pad->continued, p);
  }

  return result;

choked:
  {
    GST_WARNING_OBJECT (ogg,
        "ogg stream choked on page (serial %08lx), resetting stream",
        pad->map.serialno);
    gst_ogg_pad_reset (pad);
    /* we continue to recover */
    return GST_FLOW_OK;
  }
}


static GstOggChain *
gst_ogg_chain_new (GstOggDemux * ogg)
{
  GstOggChain *chain = g_new0 (GstOggChain, 1);

  GST_DEBUG_OBJECT (ogg, "creating new chain %p", chain);
  chain->ogg = ogg;
  chain->offset = -1;
  chain->bytes = -1;
  chain->have_bos = FALSE;
  chain->streams = g_array_new (FALSE, TRUE, sizeof (GstOggPad *));
  chain->begin_time = GST_CLOCK_TIME_NONE;
  chain->segment_start = GST_CLOCK_TIME_NONE;
  chain->segment_stop = GST_CLOCK_TIME_NONE;
  chain->total_time = GST_CLOCK_TIME_NONE;

  return chain;
}

static void
gst_ogg_chain_free (GstOggChain * chain)
{
  gint i;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    gst_object_unref (pad);
  }
  g_array_free (chain->streams, TRUE);
  g_free (chain);
}

static void
gst_ogg_pad_mark_discont (GstOggPad * pad)
{
  pad->discont = TRUE;
  pad->map.last_size = 0;
}

static void
gst_ogg_chain_mark_discont (GstOggChain * chain)
{
  gint i;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    gst_ogg_pad_mark_discont (pad);
  }
}

static void
gst_ogg_chain_reset (GstOggChain * chain)
{
  gint i;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    gst_ogg_pad_reset (pad);
  }
}

static GstOggPad *
gst_ogg_chain_new_stream (GstOggChain * chain, glong serialno)
{
  GstOggPad *ret;
  GstTagList *list;
  gchar *name;

  GST_DEBUG_OBJECT (chain->ogg, "creating new stream %08lx in chain %p",
      serialno, chain);

  ret = g_object_new (GST_TYPE_OGG_PAD, NULL);
  /* we own this one */
  gst_object_ref (ret);
  gst_object_sink (ret);

  GST_PAD_DIRECTION (ret) = GST_PAD_SRC;
  gst_ogg_pad_mark_discont (ret);

  ret->chain = chain;
  ret->ogg = chain->ogg;

  ret->map.serialno = serialno;
  if (ogg_stream_init (&ret->map.stream, serialno) != 0)
    goto init_failed;

  name = g_strdup_printf ("serial_%08lx", serialno);
  gst_object_set_name (GST_OBJECT (ret), name);
  g_free (name);

  /* FIXME: either do something with it or remove it */
  list = gst_tag_list_new ();
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_SERIAL, serialno,
      NULL);
  gst_tag_list_free (list);

  GST_DEBUG_OBJECT (chain->ogg,
      "created new ogg src %p for stream with serial %08lx", ret, serialno);

  g_array_append_val (chain->streams, ret);

  return ret;

  /* ERRORS */
init_failed:
  {
    GST_ERROR ("Could not initialize ogg_stream struct for serial %08lx.",
        serialno);
    gst_object_unref (ret);
    return NULL;
  }
}

static GstOggPad *
gst_ogg_chain_get_stream (GstOggChain * chain, glong serialno)
{
  gint i;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    if (pad->map.serialno == serialno)
      return pad;
  }
  return NULL;
}

static gboolean
gst_ogg_chain_has_stream (GstOggChain * chain, glong serialno)
{
  return gst_ogg_chain_get_stream (chain, serialno) != NULL;
}

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static GstStaticPadTemplate ogg_demux_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate ogg_demux_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/ogg; application/x-annodex")
    );

static void gst_ogg_demux_finalize (GObject * object);

static GstFlowReturn gst_ogg_demux_read_chain (GstOggDemux * ogg,
    GstOggChain ** chain);
static GstFlowReturn gst_ogg_demux_read_end_chain (GstOggDemux * ogg,
    GstOggChain * chain);

static gboolean gst_ogg_demux_sink_event (GstPad * pad, GstEvent * event);
static void gst_ogg_demux_loop (GstOggPad * pad);
static GstFlowReturn gst_ogg_demux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_ogg_demux_sink_activate (GstPad * sinkpad);
static gboolean gst_ogg_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_ogg_demux_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_ogg_demux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_ogg_demux_send_event (GstOggDemux * ogg, GstEvent * event);

static void gst_ogg_print (GstOggDemux * demux);

GST_BOILERPLATE (GstOggDemux, gst_ogg_demux, GstElement, GST_TYPE_ELEMENT);

static void
gst_ogg_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Ogg demuxer", "Codec/Demuxer",
      "demux ogg streams (info about ogg: http://xiph.org)",
      "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogg_demux_src_template_factory));
}

static void
gst_ogg_demux_class_init (GstOggDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gstelement_class->change_state = gst_ogg_demux_change_state;
  gstelement_class->send_event = gst_ogg_demux_receive_event;

  gobject_class->finalize = gst_ogg_demux_finalize;
}

static void
gst_ogg_demux_init (GstOggDemux * ogg, GstOggDemuxClass * g_class)
{
  /* create the sink pad */
  ogg->sinkpad =
      gst_pad_new_from_static_template (&ogg_demux_sink_template_factory,
      "sink");

  gst_pad_set_event_function (ogg->sinkpad, gst_ogg_demux_sink_event);
  gst_pad_set_chain_function (ogg->sinkpad, gst_ogg_demux_chain);
  gst_pad_set_activate_function (ogg->sinkpad, gst_ogg_demux_sink_activate);
  gst_pad_set_activatepull_function (ogg->sinkpad,
      gst_ogg_demux_sink_activate_pull);
  gst_pad_set_activatepush_function (ogg->sinkpad,
      gst_ogg_demux_sink_activate_push);
  gst_element_add_pad (GST_ELEMENT (ogg), ogg->sinkpad);

  ogg->chain_lock = g_mutex_new ();
  ogg->chains = g_array_new (FALSE, TRUE, sizeof (GstOggChain *));

  ogg->newsegment = NULL;
}

static void
gst_ogg_demux_finalize (GObject * object)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (object);

  g_array_free (ogg->chains, TRUE);
  g_mutex_free (ogg->chain_lock);
  ogg_sync_clear (&ogg->sync);

  if (ogg->newsegment)
    gst_event_unref (ogg->newsegment);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ogg_demux_reset_streams (GstOggDemux * ogg)
{
  GstOggChain *chain;
  guint i;

  chain = ogg->current_chain;
  if (chain == NULL)
    return;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *stream = g_array_index (chain->streams, GstOggPad *, i);

    stream->start_time = -1;
    stream->map.accumulated_granule = 0;
  }
  ogg->building_chain = chain;
  ogg->current_chain = NULL;
  ogg->resync = TRUE;
}

static gboolean
gst_ogg_demux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_ogg_demux_send_event (ogg, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (ogg, "got a flush stop event");
      ogg_sync_reset (&ogg->sync);
      res = gst_ogg_demux_send_event (ogg, event);
      gst_ogg_demux_reset_streams (ogg);
      break;
    case GST_EVENT_NEWSEGMENT:
      GST_DEBUG_OBJECT (ogg, "got a new segment event");
      gst_event_unref (event);
      res = TRUE;
      break;
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (ogg, "got an EOS event");
      res = gst_ogg_demux_send_event (ogg, event);
      if (ogg->current_chain == NULL) {
        GST_ELEMENT_ERROR (ogg, STREAM, DEMUX, (NULL),
            ("can't get first chain"));
      }
      break;
    }
    default:
      res = gst_ogg_demux_send_event (ogg, event);
      break;
  }
  gst_object_unref (ogg);

  return res;
}

/* submit the given buffer to the ogg sync.
 *
 * Returns the number of bytes submited.
 */
static GstFlowReturn
gst_ogg_demux_submit_buffer (GstOggDemux * ogg, GstBuffer * buffer)
{
  gint size;
  guint8 *data;
  gchar *oggbuffer;
  GstFlowReturn ret = GST_FLOW_OK;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  GST_DEBUG_OBJECT (ogg, "submitting %u bytes", size);
  if (G_UNLIKELY (size == 0))
    goto done;

  oggbuffer = ogg_sync_buffer (&ogg->sync, size);
  if (G_UNLIKELY (oggbuffer == NULL))
    goto no_buffer;

  memcpy (oggbuffer, data, size);
  if (G_UNLIKELY (ogg_sync_wrote (&ogg->sync, size) < 0))
    goto write_failed;

done:
  gst_buffer_unref (buffer);

  return ret;

  /* ERRORS */
no_buffer:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DECODE,
        (NULL), ("failed to get ogg sync buffer"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
write_failed:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DECODE,
        (NULL), ("failed to write %d bytes to the sync buffer", size));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

/* in random access mode this code updates the current read position
 * and resets the ogg sync buffer so that the next read will happen
 * from this new location.
 */
static void
gst_ogg_demux_seek (GstOggDemux * ogg, gint64 offset)
{
  GST_LOG_OBJECT (ogg, "seeking to %" G_GINT64_FORMAT, offset);

  ogg->offset = offset;
  ogg->read_offset = offset;
  ogg_sync_reset (&ogg->sync);
}

/* read more data from the current offset and submit to
 * the ogg sync layer.
 */
static GstFlowReturn
gst_ogg_demux_get_data (GstOggDemux * ogg, gint64 end_offset)
{
  GstFlowReturn ret;
  GstBuffer *buffer;

  GST_LOG_OBJECT (ogg,
      "get data %" G_GINT64_FORMAT " %" G_GINT64_FORMAT " %" G_GINT64_FORMAT,
      ogg->read_offset, ogg->length, end_offset);

  if (end_offset > 0 && ogg->read_offset >= end_offset)
    goto boundary_reached;

  if (ogg->read_offset == ogg->length)
    goto eos;

  ret = gst_pad_pull_range (ogg->sinkpad, ogg->read_offset, CHUNKSIZE, &buffer);
  if (ret != GST_FLOW_OK)
    goto error;

  ogg->read_offset += GST_BUFFER_SIZE (buffer);

  ret = gst_ogg_demux_submit_buffer (ogg, buffer);

  return ret;

  /* ERROR */
boundary_reached:
  {
    GST_LOG_OBJECT (ogg, "reached boundary");
    return GST_FLOW_LIMIT;
  }
eos:
  {
    GST_LOG_OBJECT (ogg, "reached EOS");
    return GST_FLOW_UNEXPECTED;
  }
error:
  {
    GST_WARNING_OBJECT (ogg, "got %d (%s) from pull range", ret,
        gst_flow_get_name (ret));
    return ret;
  }
}

/* Read the next page from the current offset.
 * boundary: number of bytes ahead we allow looking for;
 * -1 if no boundary
 *
 * @offset will contain the offset the next page starts at when this function
 * returns GST_FLOW_OK.
 *
 * GST_FLOW_UNEXPECTED is returned on EOS.
 *
 * GST_FLOW_LIMIT is returned when we did not find a page before the
 * boundary. If @boundary is -1, this is never returned.
 *
 * Any other error returned while retrieving data from the peer is returned as
 * is.
 */
static GstFlowReturn
gst_ogg_demux_get_next_page (GstOggDemux * ogg, ogg_page * og,
    gint64 boundary, gint64 * offset)
{
  gint64 end_offset = -1;
  GstFlowReturn ret;

  GST_LOG_OBJECT (ogg,
      "get next page, current offset %" G_GINT64_FORMAT ", bytes boundary %"
      G_GINT64_FORMAT, ogg->offset, boundary);

  if (boundary >= 0)
    end_offset = ogg->offset + boundary;

  while (TRUE) {
    glong more;

    if (end_offset > 0 && ogg->offset >= end_offset)
      goto boundary_reached;

    more = ogg_sync_pageseek (&ogg->sync, og);

    GST_LOG_OBJECT (ogg, "pageseek gave %ld", more);

    if (more < 0) {
      /* skipped n bytes */
      ogg->offset -= more;
      GST_LOG_OBJECT (ogg, "skipped %ld bytes, offset %" G_GINT64_FORMAT,
          more, ogg->offset);
    } else if (more == 0) {
      /* we need more data */
      if (boundary == 0)
        goto boundary_reached;

      GST_LOG_OBJECT (ogg, "need more data");
      ret = gst_ogg_demux_get_data (ogg, end_offset);
      if (ret != GST_FLOW_OK)
        break;
    } else {
      gint64 res_offset = ogg->offset;

      /* got a page.  Return the offset at the page beginning,
         advance the internal offset past the page end */
      if (offset)
        *offset = res_offset;
      ret = GST_FLOW_OK;

      ogg->offset += more;

      GST_LOG_OBJECT (ogg,
          "got page at %" G_GINT64_FORMAT ", serial %08x, end at %"
          G_GINT64_FORMAT ", granule %" G_GINT64_FORMAT, res_offset,
          ogg_page_serialno (og), ogg->offset,
          (gint64) ogg_page_granulepos (og));
      break;
    }
  }
  GST_LOG_OBJECT (ogg, "returning %d", ret);

  return ret;

  /* ERRORS */
boundary_reached:
  {
    GST_LOG_OBJECT (ogg,
        "offset %" G_GINT64_FORMAT " >= end_offset %" G_GINT64_FORMAT,
        ogg->offset, end_offset);
    return GST_FLOW_LIMIT;
  }
}

/* from the current offset, find the previous page, seeking backwards
 * until we find the page. 
 */
static GstFlowReturn
gst_ogg_demux_get_prev_page (GstOggDemux * ogg, ogg_page * og, gint64 * offset)
{
  GstFlowReturn ret;
  gint64 begin = ogg->offset;
  gint64 end = begin;
  gint64 cur_offset = -1;

  GST_LOG_OBJECT (ogg, "getting page before %" G_GINT64_FORMAT, begin);

  while (cur_offset == -1) {
    begin -= CHUNKSIZE;
    if (begin < 0)
      begin = 0;

    /* seek CHUNKSIZE back */
    gst_ogg_demux_seek (ogg, begin);

    /* now continue reading until we run out of data, if we find a page
     * start, we save it. It might not be the final page as there could be
     * another page after this one. */
    while (ogg->offset < end) {
      gint64 new_offset;

      ret =
          gst_ogg_demux_get_next_page (ogg, og, end - ogg->offset, &new_offset);
      /* we hit the upper limit, offset contains the last page start */
      if (ret == GST_FLOW_LIMIT) {
        GST_LOG_OBJECT (ogg, "hit limit");
        break;
      }
      /* something went wrong */
      if (ret == GST_FLOW_UNEXPECTED) {
        new_offset = 0;
        GST_LOG_OBJECT (ogg, "got unexpected");
      } else if (ret != GST_FLOW_OK) {
        GST_LOG_OBJECT (ogg, "got error %d", ret);
        return ret;
      }

      GST_LOG_OBJECT (ogg, "found page at %" G_GINT64_FORMAT, new_offset);

      /* offset is next page start */
      cur_offset = new_offset;
    }
  }

  GST_LOG_OBJECT (ogg, "found previous page at %" G_GINT64_FORMAT, cur_offset);

  /* we have the offset.  Actually snork and hold the page now */
  gst_ogg_demux_seek (ogg, cur_offset);
  ret = gst_ogg_demux_get_next_page (ogg, og, -1, NULL);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (ogg, "can't get last page at %" G_GINT64_FORMAT,
        cur_offset);
    /* this shouldn't be possible */
    return ret;
  }

  if (offset)
    *offset = cur_offset;

  return ret;
}

static gboolean
gst_ogg_demux_deactivate_current_chain (GstOggDemux * ogg)
{
  gint i;
  GstOggChain *chain = ogg->current_chain;

  if (chain == NULL)
    return TRUE;

  GST_DEBUG_OBJECT (ogg, "deactivating chain %p", chain);

  /* send EOS on all the pads */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);
    GstEvent *event;

    if (!pad->added)
      continue;

    event = gst_event_new_eos ();
    gst_event_set_seqnum (event, ogg->seqnum);
    gst_pad_push_event (GST_PAD_CAST (pad), event);

    GST_DEBUG_OBJECT (ogg, "removing pad %" GST_PTR_FORMAT, pad);

    /* deactivate first */
    gst_pad_set_active (GST_PAD_CAST (pad), FALSE);

    gst_element_remove_pad (GST_ELEMENT (ogg), GST_PAD_CAST (pad));

    pad->added = FALSE;
  }
  /* if we cannot seek back to the chain, we can destroy the chain 
   * completely */
  if (!ogg->pullmode) {
    gst_ogg_chain_free (chain);
  }
  ogg->current_chain = NULL;

  return TRUE;
}

static gboolean
gst_ogg_demux_activate_chain (GstOggDemux * ogg, GstOggChain * chain,
    GstEvent * event)
{
  gint i;
  gint bitrate, idx_bitrate;

  g_return_val_if_fail (chain != NULL, FALSE);

  if (chain == ogg->current_chain) {
    if (event)
      gst_event_unref (event);
    return TRUE;
  }


  GST_DEBUG_OBJECT (ogg, "activating chain %p", chain);

  bitrate = idx_bitrate = 0;

  /* first add the pads */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad;
    GstStructure *structure;

    pad = g_array_index (chain->streams, GstOggPad *, i);

    if (pad->map.idx_bitrate)
      idx_bitrate = MAX (idx_bitrate, pad->map.idx_bitrate);

    bitrate += pad->map.bitrate;

    /* mark discont */
    gst_ogg_pad_mark_discont (pad);
    pad->last_ret = GST_FLOW_OK;

    if (pad->map.is_skeleton || pad->added || GST_PAD_CAPS (pad) == NULL)
      continue;

    GST_DEBUG_OBJECT (ogg, "adding pad %" GST_PTR_FORMAT, pad);

    structure = gst_caps_get_structure (GST_PAD_CAPS (pad), 0);
    pad->is_sparse =
        gst_structure_has_name (structure, "application/x-ogm-text") ||
        gst_structure_has_name (structure, "text/x-cmml") ||
        gst_structure_has_name (structure, "subtitle/x-kate") ||
        gst_structure_has_name (structure, "application/x-kate");

    /* activate first */
    gst_pad_set_active (GST_PAD_CAST (pad), TRUE);

    gst_element_add_pad (GST_ELEMENT (ogg), GST_PAD_CAST (pad));
    pad->added = TRUE;
  }
  /* prefer the index bitrate over the ones encoded in the streams */
  ogg->bitrate = (idx_bitrate ? idx_bitrate : bitrate);

  /* after adding the new pads, remove the old pads */
  gst_ogg_demux_deactivate_current_chain (ogg);

  ogg->current_chain = chain;

  /* we are finished now */
  gst_element_no_more_pads (GST_ELEMENT (ogg));

  /* FIXME, must be sent from the streaming thread */
  if (event) {
    gst_ogg_demux_send_event (ogg, event);

    gst_element_found_tags (GST_ELEMENT_CAST (ogg),
        gst_tag_list_new_full (GST_TAG_CONTAINER_FORMAT, "Ogg", NULL));
  }

  GST_DEBUG_OBJECT (ogg, "starting chain");

  /* then send out any headers and queued packets */
  for (i = 0; i < chain->streams->len; i++) {
    GList *walk;
    GstOggPad *pad;

    pad = g_array_index (chain->streams, GstOggPad *, i);

    GST_DEBUG_OBJECT (ogg, "pushing headers");
    /* push headers */
    for (walk = pad->map.headers; walk; walk = g_list_next (walk)) {
      ogg_packet *p = walk->data;

      gst_ogg_demux_chain_peer (pad, p, TRUE);
    }

    GST_DEBUG_OBJECT (ogg, "pushing queued buffers");
    /* push queued packets */
    for (walk = pad->map.queued; walk; walk = g_list_next (walk)) {
      ogg_packet *p = walk->data;

      gst_ogg_demux_chain_peer (pad, p, TRUE);
      _ogg_packet_free (p);
    }
    /* and free the queued buffers */
    g_list_free (pad->map.queued);
    pad->map.queued = NULL;
  }
  return TRUE;
}

static gboolean
do_binary_search (GstOggDemux * ogg, GstOggChain * chain, gint64 begin,
    gint64 end, gint64 begintime, gint64 endtime, gint64 target,
    gint64 * offset)
{
  gint64 best;
  GstFlowReturn ret;
  gint64 result = 0;

  best = begin;

  GST_DEBUG_OBJECT (ogg,
      "chain offset %" G_GINT64_FORMAT ", end offset %" G_GINT64_FORMAT,
      begin, end);
  GST_DEBUG_OBJECT (ogg,
      "chain begin time %" GST_TIME_FORMAT ", end time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (begintime), GST_TIME_ARGS (endtime));
  GST_DEBUG_OBJECT (ogg, "target %" GST_TIME_FORMAT, GST_TIME_ARGS (target));

  /* perform the seek */
  while (begin < end) {
    gint64 bisect;

    if ((end - begin < CHUNKSIZE) || (endtime == begintime)) {
      bisect = begin;
    } else {
      /* take a (pretty decent) guess, avoiding overflow */
      gint64 rate = (end - begin) * GST_MSECOND / (endtime - begintime);

      bisect = (target - begintime) / GST_MSECOND * rate + begin - CHUNKSIZE;

      if (bisect <= begin)
        bisect = begin;
      GST_DEBUG_OBJECT (ogg, "Initial guess: %" G_GINT64_FORMAT, bisect);
    }
    gst_ogg_demux_seek (ogg, bisect);

    while (begin < end) {
      ogg_page og;

      GST_DEBUG_OBJECT (ogg,
          "after seek, bisect %" G_GINT64_FORMAT ", begin %" G_GINT64_FORMAT
          ", end %" G_GINT64_FORMAT, bisect, begin, end);

      ret = gst_ogg_demux_get_next_page (ogg, &og, end - ogg->offset, &result);
      GST_LOG_OBJECT (ogg, "looking for next page returned %" G_GINT64_FORMAT,
          result);

      if (ret == GST_FLOW_LIMIT) {
        /* we hit the upper limit, go back a bit */
        if (bisect <= begin + 1) {
          end = begin;          /* found it */
        } else {
          if (bisect == 0)
            goto seek_error;

          bisect -= CHUNKSIZE;
          if (bisect <= begin)
            bisect = begin + 1;

          gst_ogg_demux_seek (ogg, bisect);
        }
      } else if (ret == GST_FLOW_OK) {
        /* found offset of next ogg page */
        gint64 granulepos;
        GstClockTime granuletime;
        GstOggPad *pad;

        /* get the granulepos */
        GST_LOG_OBJECT (ogg, "found next ogg page at %" G_GINT64_FORMAT,
            result);
        granulepos = ogg_page_granulepos (&og);
        if (granulepos == -1) {
          GST_LOG_OBJECT (ogg, "granulepos of next page is -1");
          continue;
        }

        /* get the stream */
        pad = gst_ogg_chain_get_stream (chain, ogg_page_serialno (&og));
        if (pad == NULL || pad->map.is_skeleton)
          continue;

        /* convert granulepos to time */
        granuletime = gst_ogg_stream_get_end_time_for_granulepos (&pad->map,
            granulepos);
        if (granuletime < pad->start_time)
          continue;

        GST_LOG_OBJECT (ogg, "granulepos %" G_GINT64_FORMAT " maps to time %"
            GST_TIME_FORMAT, granulepos, GST_TIME_ARGS (granuletime));

        granuletime -= pad->start_time;
        granuletime += chain->begin_time;

        GST_DEBUG_OBJECT (ogg,
            "found page with granule %" G_GINT64_FORMAT " and time %"
            GST_TIME_FORMAT, granulepos, GST_TIME_ARGS (granuletime));

        if (granuletime < target) {
          best = result;        /* raw offset of packet with granulepos */
          begin = ogg->offset;  /* raw offset of next page */
          begintime = granuletime;

          bisect = begin;       /* *not* begin + 1 */
        } else {
          if (bisect <= begin + 1) {
            end = begin;        /* found it */
          } else {
            if (end == ogg->offset) {   /* we're pretty close - we'd be stuck in */
              end = result;
              bisect -= CHUNKSIZE;      /* an endless loop otherwise. */
              if (bisect <= begin)
                bisect = begin + 1;
              gst_ogg_demux_seek (ogg, bisect);
            } else {
              end = result;
              endtime = granuletime;
              break;
            }
          }
        }
      } else
        goto seek_error;
    }
  }
  GST_DEBUG_OBJECT (ogg, "seeking to %" G_GINT64_FORMAT, best);
  gst_ogg_demux_seek (ogg, best);
  *offset = best;

  return TRUE;

  /* ERRORS */
seek_error:
  {
    GST_DEBUG_OBJECT (ogg, "got a seek error");
    return FALSE;
  }
}

static gboolean
do_index_search (GstOggDemux * ogg, GstOggChain * chain, gint64 begin,
    gint64 end, gint64 begintime, gint64 endtime, gint64 target,
    gint64 * p_offset, gint64 * p_timestamp)
{
  guint i;
  guint64 timestamp, offset;
  guint64 r_timestamp, r_offset;
  gboolean result = FALSE;

  target -= begintime;

  r_offset = -1;
  r_timestamp = -1;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    timestamp = target;
    if (gst_ogg_map_search_index (&pad->map, TRUE, &timestamp, &offset)) {
      GST_INFO ("found %" G_GUINT64_FORMAT " at offset %" G_GUINT64_FORMAT,
          timestamp, offset);

      if (r_offset == -1 || offset < r_offset) {
        r_offset = offset;
        r_timestamp = timestamp;
      }
      result |= TRUE;
    }
  }

  if (p_timestamp)
    *p_timestamp = r_timestamp;
  if (p_offset)
    *p_offset = r_offset;

  return result;
}

/*
 * do seek to time @position, return FALSE or chain and TRUE
 */
static gboolean
gst_ogg_demux_do_seek (GstOggDemux * ogg, GstSegment * segment,
    gboolean accurate, gboolean keyframe, GstOggChain ** rchain)
{
  guint64 position;
  GstOggChain *chain = NULL;
  gint64 begin, end;
  gint64 begintime, endtime;
  gint64 target, keytarget;
  gint64 best;
  gint64 total;
  gint64 result = 0;
  GstFlowReturn ret;
  gint i, pending, len;

  position = segment->last_stop;

  /* first find the chain to search in */
  total = ogg->total_time;
  if (ogg->chains->len == 0)
    goto no_chains;

  for (i = ogg->chains->len - 1; i >= 0; i--) {
    chain = g_array_index (ogg->chains, GstOggChain *, i);
    total -= chain->total_time;
    if (position >= total)
      break;
  }

  /* first step, locate page containing the required data */
  begin = chain->offset;
  end = chain->end_offset;
  begintime = chain->begin_time;
  endtime = begintime + chain->total_time;
  target = position - total + begintime;

  if (!do_binary_search (ogg, chain, begin, end, begintime, endtime, target,
          &best))
    goto seek_error;

  /* second step: find pages for all streams, we use the keyframe_granule to keep
   * track of which ones we saw. If we have seen a page for each stream we can
   * calculate the positions of each keyframe. */
  GST_DEBUG_OBJECT (ogg, "find keyframes");
  len = pending = chain->streams->len;

  /* figure out where the keyframes are */
  keytarget = target;

  while (TRUE) {
    ogg_page og;
    gint64 granulepos;
    GstOggPad *pad;
    GstClockTime keyframe_time, granule_time;

    ret = gst_ogg_demux_get_next_page (ogg, &og, end - ogg->offset, &result);
    GST_LOG_OBJECT (ogg, "looking for next page returned %" G_GINT64_FORMAT,
        result);
    if (ret == GST_FLOW_LIMIT) {
      GST_LOG_OBJECT (ogg, "reached limit");
      break;
    } else if (ret != GST_FLOW_OK)
      goto seek_error;

    /* get the stream */
    pad = gst_ogg_chain_get_stream (chain, ogg_page_serialno (&og));
    if (pad == NULL)
      continue;

    if (pad->map.is_skeleton)
      goto next;

    granulepos = ogg_page_granulepos (&og);
    if (granulepos == -1) {
      GST_LOG_OBJECT (ogg, "granulepos of next page is -1");
      continue;
    }

    /* in reverse we want to go past the page with the lower timestamp */
    if (segment->rate < 0.0) {
      /* get time for this pad */
      granule_time = gst_ogg_stream_get_end_time_for_granulepos (&pad->map,
          granulepos);

      GST_LOG_OBJECT (ogg,
          "looking at page with ts %" GST_TIME_FORMAT ", target %"
          GST_TIME_FORMAT, GST_TIME_ARGS (granule_time),
          GST_TIME_ARGS (target));
      if (granule_time < target)
        continue;
    }

    /* we've seen this pad before */
    if (pad->keyframe_granule != -1)
      continue;

    /* convert granule of this pad to the granule of the keyframe */
    pad->keyframe_granule =
        gst_ogg_stream_granulepos_to_key_granule (&pad->map, granulepos);
    GST_LOG_OBJECT (ogg, "marking stream granule %" G_GINT64_FORMAT,
        pad->keyframe_granule);

    /* get time of the keyframe */
    keyframe_time =
        gst_ogg_stream_granule_to_time (&pad->map, pad->keyframe_granule);
    GST_LOG_OBJECT (ogg, "stream %08lx granule time %" GST_TIME_FORMAT,
        pad->map.serialno, GST_TIME_ARGS (keyframe_time));

    /* collect smallest value */
    if (keyframe_time != -1) {
      keyframe_time += begintime;
      if (keyframe_time < keytarget)
        keytarget = keyframe_time;
    }

  next:
    pending--;
    if (pending == 0)
      break;
  }

  /* for negative rates we will get to the keyframe backwards */
  if (segment->rate < 0.0)
    goto done;

  if (keytarget != target) {
    GST_LOG_OBJECT (ogg, "final seek to target %" GST_TIME_FORMAT,
        GST_TIME_ARGS (keytarget));

    /* last step, seek to the location of the keyframe */
    if (!do_binary_search (ogg, chain, begin, end, begintime, endtime,
            keytarget, &best))
      goto seek_error;
  } else {
    /* seek back to previous position */
    GST_LOG_OBJECT (ogg, "keyframe on target");
    gst_ogg_demux_seek (ogg, best);
  }

done:
  if (keyframe) {
    if (segment->rate > 0.0)
      segment->time = keytarget;
    segment->last_stop = keytarget - begintime;
  }

  *rchain = chain;

  return TRUE;

no_chains:
  {
    GST_DEBUG_OBJECT (ogg, "no chains");
    return FALSE;
  }
seek_error:
  {
    GST_DEBUG_OBJECT (ogg, "got a seek error");
    return FALSE;
  }
}

/* does not take ownership of the event */
static gboolean
gst_ogg_demux_perform_seek_pull (GstOggDemux * ogg, GstEvent * event)
{
  GstOggChain *chain = NULL;
  gboolean res;
  gboolean flush, accurate, keyframe;
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean update;
  guint32 seqnum;
  GstEvent *tevent;

  if (event) {
    GST_DEBUG_OBJECT (ogg, "seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we can only seek on time */
    if (format != GST_FORMAT_TIME) {
      GST_DEBUG_OBJECT (ogg, "can only seek on TIME");
      goto error;
    }
    seqnum = gst_event_get_seqnum (event);
  } else {
    GST_DEBUG_OBJECT (ogg, "seek without event");

    flags = 0;
    rate = 1.0;
    seqnum = gst_util_seqnum_next ();
  }

  GST_DEBUG_OBJECT (ogg, "seek, rate %g", rate);

  flush = flags & GST_SEEK_FLAG_FLUSH;
  accurate = flags & GST_SEEK_FLAG_ACCURATE;
  keyframe = flags & GST_SEEK_FLAG_KEY_UNIT;

  /* first step is to unlock the streaming thread if it is
   * blocked in a chain call, we do this by starting the flush. because
   * we cannot yet hold any streaming lock, we have to protect the chains
   * with their own lock. */
  if (flush) {
    gint i;

    tevent = gst_event_new_flush_start ();
    gst_event_set_seqnum (tevent, seqnum);

    gst_event_ref (tevent);
    gst_pad_push_event (ogg->sinkpad, tevent);

    GST_CHAIN_LOCK (ogg);
    for (i = 0; i < ogg->chains->len; i++) {
      GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);
      gint j;

      for (j = 0; j < chain->streams->len; j++) {
        GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, j);

        gst_event_ref (tevent);
        gst_pad_push_event (GST_PAD (pad), tevent);
      }
    }
    GST_CHAIN_UNLOCK (ogg);

    gst_event_unref (tevent);
  } else {
    gst_pad_pause_task (ogg->sinkpad);
  }

  /* now grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever. */
  GST_PAD_STREAM_LOCK (ogg->sinkpad);

  if (ogg->segment_running && !flush) {
    /* create the segment event to close the current segment */
    if ((chain = ogg->current_chain)) {
      GstEvent *newseg;
      gint64 chain_start = 0;

      if (chain->segment_start != GST_CLOCK_TIME_NONE)
        chain_start = chain->segment_start;

      newseg = gst_event_new_new_segment (TRUE, ogg->segment.rate,
          GST_FORMAT_TIME, ogg->segment.start + chain_start,
          ogg->segment.last_stop + chain_start, ogg->segment.time);
      /* set the seqnum of the running segment */
      gst_event_set_seqnum (newseg, ogg->seqnum);

      /* send segment on old chain, FIXME, must be sent from streaming thread. */
      gst_ogg_demux_send_event (ogg, newseg);
    }
  }

  if (event) {
    gst_segment_set_seek (&ogg->segment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (ogg, "segment positions set to %" GST_TIME_FORMAT "-%"
      GST_TIME_FORMAT, GST_TIME_ARGS (ogg->segment.start),
      GST_TIME_ARGS (ogg->segment.stop));

  /* we need to stop flushing on the srcpad as we're going to use it
   * next. We can do this as we have the STREAM lock now. */
  if (flush) {
    tevent = gst_event_new_flush_stop ();
    gst_event_set_seqnum (tevent, seqnum);
    gst_pad_push_event (ogg->sinkpad, tevent);
  }

  {
    gint i;

    /* reset all ogg streams now, need to do this from within the lock to
     * make sure the streaming thread is not messing with the stream */
    for (i = 0; i < ogg->chains->len; i++) {
      GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

      gst_ogg_chain_reset (chain);
    }
  }

  /* for reverse we will already seek accurately */
  res = gst_ogg_demux_do_seek (ogg, &ogg->segment, accurate, keyframe, &chain);

  /* seek failed, make sure we continue the current chain */
  if (!res) {
    GST_DEBUG_OBJECT (ogg, "seek failed");
    chain = ogg->current_chain;
  } else {
    GST_DEBUG_OBJECT (ogg, "seek success");
  }

  if (!chain)
    goto no_chain;

  /* now we have a new position, prepare for streaming again */
  {
    GstEvent *event;
    gint64 stop;
    gint64 start;
    gint64 last_stop, begin_time;

    /* we have to send the flush to the old chain, not the new one */
    if (flush) {
      tevent = gst_event_new_flush_stop ();
      gst_event_set_seqnum (tevent, seqnum);
      gst_ogg_demux_send_event (ogg, tevent);
    }

    /* we need this to see how far inside the chain we need to start */
    if (chain->begin_time != GST_CLOCK_TIME_NONE)
      begin_time = chain->begin_time;
    else
      begin_time = 0;

    /* segment.start gives the start over all chains, we calculate the amount
     * of time into this chain we need to start */
    start = ogg->segment.start - begin_time;
    if (chain->segment_start != GST_CLOCK_TIME_NONE)
      start += chain->segment_start;

    if ((stop = ogg->segment.stop) == -1)
      stop = ogg->segment.duration;

    /* segment.stop gives the stop time over all chains, calculate the amount of
     * time we need to stop in this chain */
    if (stop != -1) {
      if (stop > begin_time)
        stop -= begin_time;
      else
        stop = 0;
      stop += chain->segment_start;
      /* we must stop when this chain ends and switch to the next chain to play
       * the remainder of the segment. */
      stop = MIN (stop, chain->segment_stop);
    }

    last_stop = ogg->segment.last_stop;
    if (chain->segment_start != GST_CLOCK_TIME_NONE)
      last_stop += chain->segment_start;

    /* create the segment event we are going to send out */
    if (ogg->segment.rate >= 0.0)
      event = gst_event_new_new_segment (FALSE, ogg->segment.rate,
          ogg->segment.format, last_stop, stop, ogg->segment.time);
    else
      event = gst_event_new_new_segment (FALSE, ogg->segment.rate,
          ogg->segment.format, start, last_stop, ogg->segment.time);

    gst_event_set_seqnum (event, seqnum);

    if (chain != ogg->current_chain) {
      /* switch to different chain, send segment on new chain */
      gst_ogg_demux_activate_chain (ogg, chain, event);
    } else {
      /* mark discont and send segment on current chain */
      gst_ogg_chain_mark_discont (chain);
      /* This event should be sent from the streaming thread (sink pad task) */
      if (ogg->newsegment)
        gst_event_unref (ogg->newsegment);
      ogg->newsegment = event;
    }

    /* notify start of new segment */
    if (ogg->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      GstMessage *message;

      message = gst_message_new_segment_start (GST_OBJECT (ogg),
          GST_FORMAT_TIME, ogg->segment.last_stop);
      gst_message_set_seqnum (message, seqnum);

      gst_element_post_message (GST_ELEMENT (ogg), message);
    }

    ogg->segment_running = TRUE;
    ogg->seqnum = seqnum;
    /* restart our task since it might have been stopped when we did the 
     * flush. */
    gst_pad_start_task (ogg->sinkpad, (GstTaskFunction) gst_ogg_demux_loop,
        ogg->sinkpad);
  }

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (ogg->sinkpad);

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (ogg, "seek failed");
    return FALSE;
  }
no_chain:
  {
    GST_DEBUG_OBJECT (ogg, "no chain to seek in");
    GST_PAD_STREAM_UNLOCK (ogg->sinkpad);
    return FALSE;
  }
}

static gboolean
gst_ogg_demux_perform_seek_push (GstOggDemux * ogg, GstEvent * event)
{
  gint bitrate;
  gboolean res = TRUE;
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  GstEvent *sevent;
  GstOggChain *chain;
  gint64 best, best_time;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &start_type, &start, &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (ogg, "can only seek on TIME");
    goto error;
  }

  chain = ogg->current_chain;
  if (!chain)
    return FALSE;

  if (do_index_search (ogg, chain, 0, -1, 0, -1, start, &best, &best_time)) {
    /* the index gave some result */
    GST_DEBUG_OBJECT (ogg,
        "found offset %" G_GINT64_FORMAT " with time %" G_GUINT64_FORMAT,
        best, best_time);
    start = best;
  } else if ((bitrate = ogg->bitrate) > 0) {
    /* try with bitrate convert the seek positions to bytes */
    if (start_type != GST_SEEK_TYPE_NONE) {
      start = gst_util_uint64_scale (start, bitrate, 8 * GST_SECOND);
    }
    if (stop_type != GST_SEEK_TYPE_NONE) {
      stop = gst_util_uint64_scale (stop, bitrate, 8 * GST_SECOND);
    }
  } else {
    /* we don't know */
    res = FALSE;
  }

  if (res) {
    GST_DEBUG_OBJECT (ogg,
        "seeking to %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT, start, stop);
    /* do seek */
    sevent = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
        start_type, start, stop_type, stop);

    res = gst_pad_push_event (ogg->sinkpad, sevent);
  }

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (ogg, "seek failed");
    return FALSE;
  }
}

static gboolean
gst_ogg_demux_perform_seek (GstOggDemux * ogg, GstEvent * event)
{
  gboolean res;

  if (ogg->pullmode) {
    res = gst_ogg_demux_perform_seek_pull (ogg, event);
  } else {
    res = gst_ogg_demux_perform_seek_push (ogg, event);
  }
  return res;
}


/* finds each bitstream link one at a time using a bisection search
 * (has to begin by knowing the offset of the lb's initial page).
 * Recurses for each link so it can alloc the link storage after
 * finding them all, then unroll and fill the cache at the same time
 */
static GstFlowReturn
gst_ogg_demux_bisect_forward_serialno (GstOggDemux * ogg,
    gint64 begin, gint64 searched, gint64 end, GstOggChain * chain, glong m)
{
  gint64 endsearched = end;
  gint64 next = end;
  ogg_page og;
  GstFlowReturn ret;
  gint64 offset;
  GstOggChain *nextchain;

  GST_LOG_OBJECT (ogg,
      "bisect begin: %" G_GINT64_FORMAT ", searched: %" G_GINT64_FORMAT
      ", end %" G_GINT64_FORMAT ", chain: %p", begin, searched, end, chain);

  /* the below guards against garbage seperating the last and
   * first pages of two links. */
  while (searched < endsearched) {
    gint64 bisect;

    if (endsearched - searched < CHUNKSIZE) {
      bisect = searched;
    } else {
      bisect = (searched + endsearched) / 2;
    }

    gst_ogg_demux_seek (ogg, bisect);
    ret = gst_ogg_demux_get_next_page (ogg, &og, -1, &offset);

    if (ret == GST_FLOW_UNEXPECTED) {
      endsearched = bisect;
    } else if (ret == GST_FLOW_OK) {
      glong serial = ogg_page_serialno (&og);

      if (!gst_ogg_chain_has_stream (chain, serial)) {
        endsearched = bisect;
        next = offset;
      } else {
        searched = offset + og.header_len + og.body_len;
      }
    } else
      return ret;
  }

  GST_LOG_OBJECT (ogg, "current chain ends at %" G_GINT64_FORMAT, searched);

  chain->end_offset = searched;
  ret = gst_ogg_demux_read_end_chain (ogg, chain);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_LOG_OBJECT (ogg, "found begin at %" G_GINT64_FORMAT, next);

  gst_ogg_demux_seek (ogg, next);
  ret = gst_ogg_demux_read_chain (ogg, &nextchain);
  if (ret == GST_FLOW_UNEXPECTED) {
    nextchain = NULL;
    ret = GST_FLOW_OK;
    GST_LOG_OBJECT (ogg, "no next chain");
  } else if (ret != GST_FLOW_OK)
    goto done;

  if (searched < end && nextchain != NULL) {
    ret = gst_ogg_demux_bisect_forward_serialno (ogg, next, ogg->offset,
        end, nextchain, m + 1);
    if (ret != GST_FLOW_OK)
      goto done;
  }
  GST_LOG_OBJECT (ogg, "adding chain %p", chain);

  g_array_insert_val (ogg->chains, 0, chain);

done:
  return ret;
}

/* read a chain from the ogg file. This code will
 * read all BOS pages and will create and return a GstOggChain 
 * structure with the results. 
 * 
 * This function will also read N pages from each stream in the
 * chain and submit them to the decoders. When the decoder has
 * decoded the first buffer, we know the timestamp of the first
 * page in the chain.
 */
static GstFlowReturn
gst_ogg_demux_read_chain (GstOggDemux * ogg, GstOggChain ** res_chain)
{
  GstFlowReturn ret;
  GstOggChain *chain = NULL;
  gint64 offset = ogg->offset;
  ogg_page op;
  gboolean done;
  gint i;

  GST_LOG_OBJECT (ogg, "reading chain at %" G_GINT64_FORMAT, offset);

  /* first read the BOS pages, do typefind on them, create
   * the decoders, send data to the decoders. */
  while (TRUE) {
    GstOggPad *pad;
    glong serial;

    ret = gst_ogg_demux_get_next_page (ogg, &op, -1, NULL);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (ogg, "problem reading BOS page: ret=%d", ret);
      break;
    }
    if (!ogg_page_bos (&op)) {
      GST_WARNING_OBJECT (ogg, "page is not BOS page");
      /* if we did not find a chain yet, assume this is a bogus stream and
       * ignore it */
      if (!chain)
        ret = GST_FLOW_UNEXPECTED;
      break;
    }

    if (chain == NULL) {
      chain = gst_ogg_chain_new (ogg);
      chain->offset = offset;
    }

    serial = ogg_page_serialno (&op);
    if (gst_ogg_chain_get_stream (chain, serial) != NULL) {
      GST_WARNING_OBJECT (ogg, "found serial %08lx BOS page twice, ignoring",
          serial);
      continue;
    }

    pad = gst_ogg_chain_new_stream (chain, serial);
    gst_ogg_pad_submit_page (pad, &op);
  }

  if (ret != GST_FLOW_OK || chain == NULL) {
    if (ret == GST_FLOW_OK) {
      GST_WARNING_OBJECT (ogg, "no chain was found");
      ret = GST_FLOW_ERROR;
    } else if (ret != GST_FLOW_UNEXPECTED) {
      GST_WARNING_OBJECT (ogg, "failed to read chain");
    } else {
      GST_DEBUG_OBJECT (ogg, "done reading chains");
    }
    if (chain) {
      gst_ogg_chain_free (chain);
    }
    if (res_chain)
      *res_chain = NULL;
    return ret;
  }

  chain->have_bos = TRUE;
  GST_LOG_OBJECT (ogg, "read bos pages, init decoder now");

  /* now read pages until we receive a buffer from each of the
   * stream decoders, this will tell us the timestamp of the
   * first packet in the chain then */

  /* save the offset to the first non bos page in the chain: if searching for
   * pad->first_time we read past the end of the chain, we'll seek back to this
   * position
   */
  offset = ogg->offset;

  done = FALSE;
  while (!done) {
    glong serial;
    gboolean known_serial = FALSE;
    GstFlowReturn ret;

    serial = ogg_page_serialno (&op);
    done = TRUE;
    for (i = 0; i < chain->streams->len; i++) {
      GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

      GST_LOG_OBJECT (ogg, "serial %08lx time %" GST_TIME_FORMAT,
          pad->map.serialno, GST_TIME_ARGS (pad->start_time));

      if (pad->map.serialno == serial) {
        known_serial = TRUE;

        /* submit the page now, this will fill in the start_time when the
         * internal decoder finds it */
        gst_ogg_pad_submit_page (pad, &op);

        if (!pad->map.is_skeleton && pad->start_time == -1
            && ogg_page_eos (&op)) {
          /* got EOS on a pad before we could find its start_time.
           * We have no chance of finding a start_time for every pad so
           * stop searching for the other start_time(s).
           */
          done = TRUE;
          break;
        }
      }
      /* the timestamp will be filled in when we submit the pages */
      if (!pad->map.is_skeleton)
        done &= (pad->start_time != GST_CLOCK_TIME_NONE);

      GST_LOG_OBJECT (ogg, "done %08lx now %d", pad->map.serialno, done);
    }

    /* we read a page not belonging to the current chain: seek back to the
     * beginning of the chain
     */
    if (!known_serial) {
      GST_LOG_OBJECT (ogg, "unknown serial %08lx", serial);
      gst_ogg_demux_seek (ogg, offset);
      break;
    }

    if (!done) {
      ret = gst_ogg_demux_get_next_page (ogg, &op, -1, NULL);
      if (ret != GST_FLOW_OK)
        break;
    }
  }
  GST_LOG_OBJECT (ogg, "done reading chain");
  /* now we can fill in the missing info using queries */
  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

    if (pad->map.is_skeleton)
      continue;

    pad->mode = GST_OGG_PAD_MODE_STREAMING;
  }

  if (res_chain)
    *res_chain = chain;

  return GST_FLOW_OK;
}

/* read the last pages from the ogg stream to get the final
 * page end_offsets.
 */
static GstFlowReturn
gst_ogg_demux_read_end_chain (GstOggDemux * ogg, GstOggChain * chain)
{
  gint64 begin = chain->end_offset;
  gint64 end = begin;
  gint64 last_granule = -1;
  GstOggPad *last_pad = NULL;
  GstFlowReturn ret;
  gboolean done = FALSE;
  ogg_page og;
  gint i;

  while (!done) {
    begin -= CHUNKSIZE;
    if (begin < 0)
      begin = 0;

    gst_ogg_demux_seek (ogg, begin);

    /* now continue reading until we run out of data, if we find a page
     * start, we save it. It might not be the final page as there could be
     * another page after this one. */
    while (ogg->offset < end) {
      ret = gst_ogg_demux_get_next_page (ogg, &og, end - ogg->offset, NULL);

      if (ret == GST_FLOW_LIMIT)
        break;
      if (ret != GST_FLOW_OK)
        return ret;

      for (i = 0; i < chain->streams->len; i++) {
        GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

        if (pad->map.is_skeleton)
          continue;

        if (pad->map.serialno == ogg_page_serialno (&og)) {
          gint64 granulepos = ogg_page_granulepos (&og);

          if (last_granule == -1 || last_granule < granulepos) {
            last_granule = granulepos;
            last_pad = pad;
          }
          if (last_granule != -1) {
            done = TRUE;
          }
          break;
        }
      }
    }
  }

  if (last_pad) {
    chain->segment_stop =
        gst_ogg_stream_get_end_time_for_granulepos (&last_pad->map,
        last_granule);
  } else {
    chain->segment_stop = GST_CLOCK_TIME_NONE;
  }

  GST_INFO ("segment stop %" G_GUINT64_FORMAT, chain->segment_stop);

  return GST_FLOW_OK;
}

/* find a pad with a given serial number
 */
static GstOggPad *
gst_ogg_demux_find_pad (GstOggDemux * ogg, glong serialno)
{
  GstOggPad *pad;
  gint i;

  /* first look in building chain if any */
  if (ogg->building_chain) {
    pad = gst_ogg_chain_get_stream (ogg->building_chain, serialno);
    if (pad)
      return pad;
  }

  /* then look in current chain if any */
  if (ogg->current_chain) {
    pad = gst_ogg_chain_get_stream (ogg->current_chain, serialno);
    if (pad)
      return pad;
  }

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    pad = gst_ogg_chain_get_stream (chain, serialno);
    if (pad)
      return pad;
  }
  return NULL;
}

/* find a chain with a given serial number
 */
static GstOggChain *
gst_ogg_demux_find_chain (GstOggDemux * ogg, glong serialno)
{
  GstOggPad *pad;

  pad = gst_ogg_demux_find_pad (ogg, serialno);
  if (pad) {
    return pad->chain;
  }
  return NULL;
}

/* returns TRUE if all streams have valid start time */
static gboolean
gst_ogg_demux_collect_chain_info (GstOggDemux * ogg, GstOggChain * chain)
{
  gboolean res = TRUE;

  chain->total_time = GST_CLOCK_TIME_NONE;
  GST_DEBUG_OBJECT (ogg, "trying to collect chain info");

  /* see if we have a start time on all streams */
  chain->segment_start = gst_ogg_demux_collect_start_time (ogg, chain);

  if (chain->segment_start == G_MAXUINT64) {
    /* not yet, stream some more data */
    res = FALSE;
  } else if (chain->segment_stop != GST_CLOCK_TIME_NONE) {
    /* we can calculate a total time */
    chain->total_time = chain->segment_stop - chain->segment_start;
  }

  GST_DEBUG ("total time %" G_GUINT64_FORMAT, chain->total_time);

  GST_DEBUG_OBJECT (ogg, "return %d", res);

  return res;
}

static void
gst_ogg_demux_collect_info (GstOggDemux * ogg)
{
  gint i;

  /* collect all info */
  ogg->total_time = 0;

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    chain->begin_time = ogg->total_time;

    gst_ogg_demux_collect_chain_info (ogg, chain);

    ogg->total_time += chain->total_time;
  }
  gst_segment_set_duration (&ogg->segment, GST_FORMAT_TIME, ogg->total_time);
}

/* find all the chains in the ogg file, this reads the first and
 * last page of the ogg stream, if they match then the ogg file has
 * just one chain, else we do a binary search for all chains.
 */
static GstFlowReturn
gst_ogg_demux_find_chains (GstOggDemux * ogg)
{
  ogg_page og;
  GstPad *peer;
  GstFormat format;
  gboolean res;
  gulong serialno;
  GstOggChain *chain;
  GstFlowReturn ret;

  /* get peer to figure out length */
  if ((peer = gst_pad_get_peer (ogg->sinkpad)) == NULL)
    goto no_peer;

  /* find length to read last page, we store this for later use. */
  format = GST_FORMAT_BYTES;
  res = gst_pad_query_duration (peer, &format, &ogg->length);
  gst_object_unref (peer);
  if (!res || ogg->length <= 0)
    goto no_length;

  GST_DEBUG_OBJECT (ogg, "file length %" G_GINT64_FORMAT, ogg->length);

  /* read chain from offset 0, this is the first chain of the
   * ogg file. */
  gst_ogg_demux_seek (ogg, 0);
  ret = gst_ogg_demux_read_chain (ogg, &chain);
  if (ret != GST_FLOW_OK)
    goto no_first_chain;

  /* read page from end offset, we use this page to check if its serial
   * number is contained in the first chain. If this is the case then
   * this ogg is not a chained ogg and we can skip the scanning. */
  gst_ogg_demux_seek (ogg, ogg->length);
  ret = gst_ogg_demux_get_prev_page (ogg, &og, NULL);
  if (ret != GST_FLOW_OK)
    goto no_last_page;

  serialno = ogg_page_serialno (&og);

  if (!gst_ogg_chain_has_stream (chain, serialno)) {
    /* the last page is not in the first stream, this means we should
     * find all the chains in this chained ogg. */
    ret =
        gst_ogg_demux_bisect_forward_serialno (ogg, 0, 0, ogg->length, chain,
        0);
  } else {
    /* we still call this function here but with an empty range so that
     * we can reuse the setup code in this routine. */
    ret =
        gst_ogg_demux_bisect_forward_serialno (ogg, 0, ogg->length,
        ogg->length, chain, 0);
  }
  if (ret != GST_FLOW_OK)
    goto done;

  /* all fine, collect and print */
  gst_ogg_demux_collect_info (ogg);

  /* dump our chains and streams */
  gst_ogg_print (ogg);

done:
  return ret;

  /*** error cases ***/
no_peer:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DEMUX, (NULL), ("we don't have a peer"));
    return GST_FLOW_NOT_LINKED;
  }
no_length:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DEMUX, (NULL), ("can't get file length"));
    return GST_FLOW_NOT_SUPPORTED;
  }
no_first_chain:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DEMUX, (NULL), ("can't get first chain"));
    return GST_FLOW_ERROR;
  }
no_last_page:
  {
    GST_DEBUG_OBJECT (ogg, "can't get last page");
    if (chain)
      gst_ogg_chain_free (chain);
    return ret;
  }
}

static GstFlowReturn
gst_ogg_demux_handle_page (GstOggDemux * ogg, ogg_page * page)
{
  GstOggPad *pad;
  gint64 granule;
  glong serialno;
  GstFlowReturn result = GST_FLOW_OK;

  serialno = ogg_page_serialno (page);
  granule = ogg_page_granulepos (page);

  GST_LOG_OBJECT (ogg,
      "processing ogg page (serial %08lx, pageno %ld, granulepos %"
      G_GINT64_FORMAT ", bos %d)",
      serialno, ogg_page_pageno (page), granule, ogg_page_bos (page));

  if (ogg_page_bos (page)) {
    GstOggChain *chain;

    /* first page */
    /* see if we know about the chain already */
    chain = gst_ogg_demux_find_chain (ogg, serialno);
    if (chain) {
      GstEvent *event;
      gint64 start = 0;

      if (chain->segment_start != GST_CLOCK_TIME_NONE)
        start = chain->segment_start;

      /* create the newsegment event we are going to send out */
      event = gst_event_new_new_segment (FALSE, ogg->segment.rate,
          GST_FORMAT_TIME, start, chain->segment_stop, chain->begin_time);
      gst_event_set_seqnum (event, ogg->seqnum);

      GST_DEBUG_OBJECT (ogg,
          "segment: start %" GST_TIME_FORMAT ", stop %" GST_TIME_FORMAT
          ", time %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
          GST_TIME_ARGS (chain->segment_stop),
          GST_TIME_ARGS (chain->begin_time));

      /* activate it as it means we have a non-header, this will also deactivate
       * the currently running chain. */
      gst_ogg_demux_activate_chain (ogg, chain, event);
      pad = gst_ogg_demux_find_pad (ogg, serialno);
    } else {
      GstClockTime chain_time;
      GstOggChain *current_chain;
      gint64 current_time;

      /* this can only happen in push mode */
      if (ogg->pullmode)
        goto unknown_chain;

      current_chain = ogg->current_chain;
      current_time = ogg->segment.last_stop;

      /* time of new chain is current time */
      chain_time = current_time;

      if (ogg->building_chain == NULL) {
        GstOggChain *newchain;

        newchain = gst_ogg_chain_new (ogg);
        newchain->offset = 0;
        /* set new chain begin time aligned with end time of old chain */
        newchain->begin_time = chain_time;
        GST_DEBUG_OBJECT (ogg, "new chain, begin time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (chain_time));

        /* and this is the one we are building now */
        ogg->building_chain = newchain;
      }
      pad = gst_ogg_chain_new_stream (ogg->building_chain, serialno);
    }
  } else {
    pad = gst_ogg_demux_find_pad (ogg, serialno);
  }
  if (pad) {
    result = gst_ogg_pad_submit_page (pad, page);
  } else {
    /* no pad. This means an ogg page without bos has been seen for this
     * serialno. we just ignore it but post a warning... */
    GST_ELEMENT_WARNING (ogg, STREAM, DECODE,
        (NULL), ("unknown ogg pad for serial %08lx detected", serialno));
    return GST_FLOW_OK;
  }
  return result;

  /* ERRORS */
unknown_chain:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DECODE,
        (NULL), ("unknown ogg chain for serial %08lx detected", serialno));
    return GST_FLOW_ERROR;
  }
}

/* streaming mode, receive a buffer, parse it, create pads for
 * the serialno, submit pages and packets to the oggpads
 */
static GstFlowReturn
gst_ogg_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstOggDemux *ogg;
  gint ret = 0;
  GstFlowReturn result = GST_FLOW_OK;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (pad));

  GST_DEBUG_OBJECT (ogg, "chain");
  result = gst_ogg_demux_submit_buffer (ogg, buffer);

  while (result == GST_FLOW_OK) {
    ogg_page page;

    ret = ogg_sync_pageout (&ogg->sync, &page);
    if (ret == 0)
      /* need more data */
      break;
    if (ret == -1) {
      /* discontinuity in the pages */
      GST_DEBUG_OBJECT (ogg, "discont in page found, continuing");
    } else {
      result = gst_ogg_demux_handle_page (ogg, &page);
    }
  }
  if (ret == 0 || result == GST_FLOW_OK) {
    gst_ogg_demux_sync_streams (ogg);
  }
  return result;
}

static gboolean
gst_ogg_demux_send_event (GstOggDemux * ogg, GstEvent * event)
{
  GstOggChain *chain = ogg->current_chain;
  gboolean res = TRUE;

  if (chain) {
    gint i;

    for (i = 0; i < chain->streams->len; i++) {
      GstOggPad *pad = g_array_index (chain->streams, GstOggPad *, i);

      gst_event_ref (event);
      GST_DEBUG_OBJECT (pad, "Pushing event %" GST_PTR_FORMAT, event);
      res &= gst_pad_push_event (GST_PAD (pad), event);
    }
  }
  gst_event_unref (event);

  return res;
}

static GstFlowReturn
gst_ogg_demux_combine_flows (GstOggDemux * ogg, GstOggPad * pad,
    GstFlowReturn ret)
{
  GstOggChain *chain;

  /* store the value */
  pad->last_ret = ret;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  chain = ogg->current_chain;
  if (chain) {
    gint i;

    for (i = 0; i < chain->streams->len; i++) {
      GstOggPad *opad = g_array_index (chain->streams, GstOggPad *, i);

      ret = opad->last_ret;
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

/* returns TRUE if all streams in current chain reached EOS, FALSE otherwise */
static gboolean
gst_ogg_demux_check_eos (GstOggDemux * ogg)
{
  GstOggChain *chain;
  gboolean eos = TRUE;

  chain = ogg->current_chain;
  if (G_LIKELY (chain)) {
    gint i;

    for (i = 0; i < chain->streams->len; i++) {
      GstOggPad *opad = g_array_index (chain->streams, GstOggPad *, i);

      eos = eos && opad->is_eos;
    }
  } else {
    eos = FALSE;
  }

  return eos;
}

static GstFlowReturn
gst_ogg_demux_loop_forward (GstOggDemux * ogg)
{
  GstFlowReturn ret;
  GstBuffer *buffer;

  if (ogg->offset == ogg->length) {
    GST_LOG_OBJECT (ogg, "no more data to pull %" G_GINT64_FORMAT
        " == %" G_GINT64_FORMAT, ogg->offset, ogg->length);
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  GST_LOG_OBJECT (ogg, "pull data %" G_GINT64_FORMAT, ogg->offset);
  ret = gst_pad_pull_range (ogg->sinkpad, ogg->offset, CHUNKSIZE, &buffer);
  if (ret != GST_FLOW_OK) {
    GST_LOG_OBJECT (ogg, "Failed pull_range");
    goto done;
  }

  ogg->offset += GST_BUFFER_SIZE (buffer);

  if (G_UNLIKELY (ogg->newsegment)) {
    gst_ogg_demux_send_event (ogg, ogg->newsegment);
    ogg->newsegment = NULL;
  }

  ret = gst_ogg_demux_chain (ogg->sinkpad, buffer);
  if (ret != GST_FLOW_OK) {
    GST_LOG_OBJECT (ogg, "Failed demux_chain");
    goto done;
  }

  /* check for the end of the segment */
  if (gst_ogg_demux_check_eos (ogg)) {
    GST_LOG_OBJECT (ogg, "got EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }
done:
  return ret;
}

/* reverse mode.
 *
 * We read the pages backwards and send the packets forwards. The first packet
 * in the page will be pushed with the DISCONT flag set.
 *
 * Special care has to be taken for continued pages, which we can only decode
 * when we have the previous page(s).
 */
static GstFlowReturn
gst_ogg_demux_loop_reverse (GstOggDemux * ogg)
{
  GstFlowReturn ret;
  ogg_page page;
  gint64 offset;

  if (ogg->offset == 0) {
    GST_LOG_OBJECT (ogg, "no more data to pull %" G_GINT64_FORMAT
        " == 0", ogg->offset);
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  GST_LOG_OBJECT (ogg, "read page from %" G_GINT64_FORMAT, ogg->offset);
  ret = gst_ogg_demux_get_prev_page (ogg, &page, &offset);
  if (ret != GST_FLOW_OK)
    goto done;

  ogg->offset = offset;

  if (G_UNLIKELY (ogg->newsegment)) {
    gst_ogg_demux_send_event (ogg, ogg->newsegment);
    ogg->newsegment = NULL;
  }

  ret = gst_ogg_demux_handle_page (ogg, &page);
  if (ret != GST_FLOW_OK)
    goto done;

  /* check for the end of the segment */
  if (gst_ogg_demux_check_eos (ogg)) {
    GST_LOG_OBJECT (ogg, "got EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }
done:
  return ret;
}

static void
gst_ogg_demux_sync_streams (GstOggDemux * ogg)
{
  GstClockTime cur;
  GstOggChain *chain;
  guint i;

  chain = ogg->current_chain;
  cur = ogg->segment.last_stop;
  if (chain == NULL || cur == -1)
    return;

  for (i = 0; i < chain->streams->len; i++) {
    GstOggPad *stream = g_array_index (chain->streams, GstOggPad *, i);

    /* Theoretically, we should be doing this for all streams, but we're only
     * doing it for known-to-be-sparse streams at the moment in order not to
     * break things for wrongly-muxed streams (like we used to produce once) */
    if (stream->is_sparse && stream->last_stop != GST_CLOCK_TIME_NONE) {

      /* Does this stream lag? Random threshold of 2 seconds */
      if (GST_CLOCK_DIFF (stream->last_stop, cur) > (2 * GST_SECOND)) {
        GST_DEBUG_OBJECT (stream, "synchronizing stream with others by "
            "advancing time from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (stream->last_stop), GST_TIME_ARGS (cur));
        stream->last_stop = cur;
        /* advance stream time (FIXME: is this right, esp. time_pos?) */
        gst_pad_push_event (GST_PAD_CAST (stream),
            gst_event_new_new_segment (TRUE, ogg->segment.rate,
                GST_FORMAT_TIME, stream->last_stop, -1, stream->last_stop));
      }
    }
  }
}

/* random access code
 *
 * - first find all the chains and streams by scanning the file.
 * - then get and chain buffers, just like the streaming case.
 * - when seeking, we can use the chain info to perform the seek.
 */
static void
gst_ogg_demux_loop (GstOggPad * pad)
{
  GstOggDemux *ogg;
  GstFlowReturn ret;
  GstEvent *event;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (pad));

  if (ogg->need_chains) {
    gboolean res;

    /* this is the only place where we write chains and thus need to lock. */
    GST_CHAIN_LOCK (ogg);
    ret = gst_ogg_demux_find_chains (ogg);
    GST_CHAIN_UNLOCK (ogg);
    if (ret != GST_FLOW_OK)
      goto chain_read_failed;

    ogg->need_chains = FALSE;

    GST_OBJECT_LOCK (ogg);
    ogg->running = TRUE;
    event = ogg->event;
    ogg->event = NULL;
    GST_OBJECT_UNLOCK (ogg);

    /* and seek to configured positions without FLUSH */
    res = gst_ogg_demux_perform_seek_pull (ogg, event);
    if (event)
      gst_event_unref (event);

    if (!res)
      goto seek_failed;
  }

  if (ogg->segment.rate >= 0.0)
    ret = gst_ogg_demux_loop_forward (ogg);
  else
    ret = gst_ogg_demux_loop_reverse (ogg);

  if (ret != GST_FLOW_OK)
    goto pause;

  gst_ogg_demux_sync_streams (ogg);
  return;

  /* ERRORS */
chain_read_failed:
  {
    /* error was posted */
    goto pause;
  }
seek_failed:
  {
    GST_ELEMENT_ERROR (ogg, STREAM, DEMUX, (NULL),
        ("failed to start demuxing ogg"));
    ret = GST_FLOW_ERROR;
    goto pause;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);
    GstEvent *event = NULL;

    GST_LOG_OBJECT (ogg, "pausing task, reason %s", reason);
    ogg->segment_running = FALSE;
    gst_pad_pause_task (ogg->sinkpad);

    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (ogg->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;
          GstMessage *message;

          /* for segment playback we need to post when (in stream time)
           * we stopped, this is either stop (when set) or the duration. */
          if ((stop = ogg->segment.stop) == -1)
            stop = ogg->segment.duration;

          GST_LOG_OBJECT (ogg, "Sending segment done, at end of segment");
          message =
              gst_message_new_segment_done (GST_OBJECT (ogg), GST_FORMAT_TIME,
              stop);
          gst_message_set_seqnum (message, ogg->seqnum);

          gst_element_post_message (GST_ELEMENT (ogg), message);
        } else {
          /* normal playback, send EOS to all linked pads */
          GST_LOG_OBJECT (ogg, "Sending EOS, at end of stream");
          event = gst_event_new_eos ();
        }
      } else {
        GST_ELEMENT_ERROR (ogg, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("stream stopped, reason %s", reason));
        event = gst_event_new_eos ();
      }
      if (event) {
        gst_event_set_seqnum (event, ogg->seqnum);
        gst_ogg_demux_send_event (ogg, event);
      }
    }
    return;
  }
}

static void
gst_ogg_demux_clear_chains (GstOggDemux * ogg)
{
  gint i;

  gst_ogg_demux_deactivate_current_chain (ogg);

  GST_CHAIN_LOCK (ogg);
  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    gst_ogg_chain_free (chain);
  }
  ogg->chains = g_array_set_size (ogg->chains, 0);
  GST_CHAIN_UNLOCK (ogg);
}

/* this function is called when the pad is activated and should start
 * processing data.
 *
 * We check if we can do random access to decide if we work push or
 * pull based.
 */
static gboolean
gst_ogg_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    GST_DEBUG_OBJECT (sinkpad, "activating pull");
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

/* this function gets called when we activate ourselves in push mode.
 * We cannot seek (ourselves) in the stream */
static gboolean
gst_ogg_demux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (sinkpad));

  ogg->pullmode = FALSE;
  ogg->resync = FALSE;

  return TRUE;
}

/* this function gets called when we activate ourselves in pull mode.
 * We can perform  random access to the resource and we start a task
 * to start reading */
static gboolean
gst_ogg_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstOggDemux *ogg;

  ogg = GST_OGG_DEMUX (GST_OBJECT_PARENT (sinkpad));

  if (active) {
    ogg->need_chains = TRUE;
    ogg->pullmode = TRUE;

    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_ogg_demux_loop,
        sinkpad);
  } else {
    return gst_pad_stop_task (sinkpad);
  }
}

static GstStateChangeReturn
gst_ogg_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstOggDemux *ogg;
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  ogg = GST_OGG_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ogg->basetime = 0;
      ogg_sync_init (&ogg->sync);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ogg_sync_reset (&ogg->sync);
      ogg->running = FALSE;
      ogg->bitrate = 0;
      ogg->segment_running = FALSE;
      ogg->total_time = -1;
      gst_segment_init (&ogg->segment, GST_FORMAT_TIME);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  result = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ogg_demux_clear_chains (ogg);
      GST_OBJECT_LOCK (ogg);
      ogg->running = FALSE;
      ogg->segment_running = FALSE;
      GST_OBJECT_UNLOCK (ogg);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      ogg_sync_clear (&ogg->sync);
      break;
    default:
      break;
  }
  return result;
}

gboolean
gst_ogg_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_debug, "oggdemux", 0, "ogg demuxer");
  GST_DEBUG_CATEGORY_INIT (gst_ogg_demux_setup_debug, "oggdemux_setup", 0,
      "ogg demuxer setup stage when parsing pipeline");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  return gst_element_register (plugin, "oggdemux", GST_RANK_PRIMARY,
      GST_TYPE_OGG_DEMUX);
}

/* prints all info about the element */
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_ogg_demux_setup_debug

#ifdef GST_DISABLE_GST_DEBUG

static void
gst_ogg_print (GstOggDemux * ogg)
{
  /* NOP */
}

#else /* !GST_DISABLE_GST_DEBUG */

static void
gst_ogg_print (GstOggDemux * ogg)
{
  guint j, i;

  GST_INFO_OBJECT (ogg, "%u chains", ogg->chains->len);
  GST_INFO_OBJECT (ogg, " total time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ogg->total_time));

  for (i = 0; i < ogg->chains->len; i++) {
    GstOggChain *chain = g_array_index (ogg->chains, GstOggChain *, i);

    GST_INFO_OBJECT (ogg, " chain %d (%u streams):", i, chain->streams->len);
    GST_INFO_OBJECT (ogg,
        "  offset: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT, chain->offset,
        chain->end_offset);
    GST_INFO_OBJECT (ogg, "  begin time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->begin_time));
    GST_INFO_OBJECT (ogg, "  total time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->total_time));
    GST_INFO_OBJECT (ogg, "  segment start: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->segment_start));
    GST_INFO_OBJECT (ogg, "  segment stop:  %" GST_TIME_FORMAT,
        GST_TIME_ARGS (chain->segment_stop));

    for (j = 0; j < chain->streams->len; j++) {
      GstOggPad *stream = g_array_index (chain->streams, GstOggPad *, j);

      GST_INFO_OBJECT (ogg, "  stream %08lx:", stream->map.serialno);
      GST_INFO_OBJECT (ogg, "   start time:       %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->start_time));
    }
  }
}
#endif /* GST_DISABLE_GST_DEBUG */
