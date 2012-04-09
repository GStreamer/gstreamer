/* GStreamer RealAudio demuxer
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-rademux
 *
 * Demuxes/parses a RealAudio (.ra) file or stream into compressed audio.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=interview.ra ! rademux ! ffdec_real_288 ! audioconvert ! audioresample ! alsasink
 * ]| Read a RealAudio file and decode it and output it to the soundcard using
 * the ALSA element. The .ra file is assumed to contain RealAudio version 2.
 * |[
 * gst-launch gnomevfssrc location=http://www.example.org/interview.ra ! rademux ! a52dec ! audioconvert ! audioresample ! alsasink
 * ]| Stream RealAudio data containing AC3 (dnet) compressed audio and decode it
 * and output it to the soundcard using the ALSA element.
 * </refsect2>
 *
 * Last reviewed on 2006-10-24 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rademux.h"
#include "rmdemux.h"
#include "rmutils.h"

#include <string.h>

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-pn-realaudio")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (real_audio_demux_debug);
#define GST_CAT_DEFAULT real_audio_demux_debug

#define gst_real_audio_demux_parent_class parent_class
G_DEFINE_TYPE (GstRealAudioDemux, gst_real_audio_demux, GST_TYPE_ELEMENT);

static GstStateChangeReturn gst_real_audio_demux_change_state (GstElement * e,
    GstStateChange transition);
static GstFlowReturn gst_real_audio_demux_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_real_audio_demux_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * ev);
static gboolean gst_real_audio_demux_src_event (GstPad * pad,
    GstObject * parent, GstEvent * ev);
static gboolean gst_real_audio_demux_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static void gst_real_audio_demux_loop (GstRealAudioDemux * demux);
static gboolean gst_real_audio_demux_sink_activate (GstPad * sinkpad,
    GstObject * parent);
static gboolean gst_real_audio_demux_sink_activate_mode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);

static void
gst_real_audio_demux_finalize (GObject * obj)
{
  GstRealAudioDemux *demux = GST_REAL_AUDIO_DEMUX (obj);

  g_object_unref (demux->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_real_audio_demux_class_init (GstRealAudioDemuxClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_real_audio_demux_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (gstelement_class, "RealAudio Demuxer",
      "Codec/Demuxer",
      "Demultiplex a RealAudio file",
      "Tim-Philipp Müller <tim centricular net>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_change_state);

  GST_DEBUG_CATEGORY_INIT (real_audio_demux_debug, "rademux",
      0, "Demuxer for RealAudio streams");
}

static void
gst_real_audio_demux_reset (GstRealAudioDemux * demux)
{
  gst_adapter_clear (demux->adapter);

  if (demux->srcpad) {
    GST_DEBUG_OBJECT (demux, "Removing source pad");
    gst_element_remove_pad (GST_ELEMENT (demux), demux->srcpad);
    demux->srcpad = NULL;
  }

  if (demux->pending_tags) {
    gst_tag_list_free (demux->pending_tags);
    demux->pending_tags = NULL;
  }

  demux->state = REAL_AUDIO_DEMUX_STATE_MARKER;
  demux->ra_version = 0;
  demux->data_offset = 0;
  demux->packet_size = 0;

  demux->sample_rate = 0;
  demux->sample_width = 0;
  demux->channels = 0;
  demux->fourcc = 0;

  demux->need_newsegment = TRUE;

  demux->segment_running = FALSE;

  demux->byterate_num = 0;
  demux->byterate_denom = 0;

  demux->duration = 0;
  demux->upstream_size = 0;

  demux->offset = 0;

  gst_adapter_clear (demux->adapter);
}

static void
gst_real_audio_demux_init (GstRealAudioDemux * demux)
{
  demux->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");

  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_sink_event));
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_sink_activate));
  gst_pad_set_activatemode_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_sink_activate_mode));

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->adapter = gst_adapter_new ();
  gst_real_audio_demux_reset (demux);
}

static gboolean
gst_real_audio_demux_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode (query, GST_PAD_MODE_PULL);
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
gst_real_audio_demux_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;
  GstRealAudioDemux *demux;

  demux = GST_REAL_AUDIO_DEMUX (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      demux->seekable = FALSE;
      res = TRUE;
      break;
    case GST_PAD_MODE_PULL:
      if (active) {
        demux->seekable = TRUE;

        res = gst_pad_start_task (sinkpad,
            (GstTaskFunction) gst_real_audio_demux_loop, demux);
      } else {
        demux->seekable = FALSE;
        res = gst_pad_stop_task (sinkpad);
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static GstFlowReturn
gst_real_audio_demux_parse_marker (GstRealAudioDemux * demux)
{
  guint8 data[6];

  if (gst_adapter_available (demux->adapter) < 6) {
    GST_LOG_OBJECT (demux, "need at least 6 bytes, waiting for more data");
    return GST_FLOW_OK;
  }

  gst_adapter_copy (demux->adapter, data, 0, 6);
  if (memcmp (data, ".ra\375", 4) != 0)
    goto wrong_format;

  demux->ra_version = GST_READ_UINT16_BE (data + 4);
  GST_DEBUG_OBJECT (demux, "ra_version   = %u", demux->ra_version);
  if (demux->ra_version != 4 && demux->ra_version != 3)
    goto unsupported_ra_version;

  gst_adapter_flush (demux->adapter, 6);
  demux->state = REAL_AUDIO_DEMUX_STATE_HEADER;
  return GST_FLOW_OK;

/* ERRORS */
wrong_format:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (demux), STREAM, WRONG_TYPE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }

unsupported_ra_version:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (demux), STREAM, DECODE,
        ("Cannot decode this RealAudio file, please file a bug"),
        ("ra_version = %u", demux->ra_version));
    return GST_FLOW_ERROR;
  }
}

static GstClockTime
gst_real_demux_get_timestamp_from_offset (GstRealAudioDemux * demux,
    guint64 offset)
{
  if (offset >= demux->data_offset && demux->byterate_num > 0 &&
      demux->byterate_denom > 0) {
    return gst_util_uint64_scale (offset - demux->data_offset,
        demux->byterate_denom * GST_SECOND, demux->byterate_num);
  } else if (offset == demux->data_offset) {
    return (GstClockTime) 0;
  } else {
    return GST_CLOCK_TIME_NONE;
  }
}

static gboolean
gst_real_audio_demux_get_data_offset_from_header (GstRealAudioDemux * demux)
{
  guint8 data[16];

  gst_adapter_copy (demux->adapter, data, 0, 16);

  switch (demux->ra_version) {
    case 3:
      demux->data_offset = GST_READ_UINT16_BE (data) + 8;
      break;
    case 4:
      demux->data_offset = GST_READ_UINT32_BE (data + 12) + 16;
      break;
    default:
      demux->data_offset = 0;
      g_return_val_if_reached (FALSE);
  }

  return TRUE;
}

static GstFlowReturn
gst_real_audio_demux_parse_header (GstRealAudioDemux * demux)
{
  const guint8 *data;
  gchar *codec_name = NULL;
  GstCaps *caps = NULL;
  guint avail;

  g_assert (demux->ra_version == 4 || demux->ra_version == 3);

  avail = gst_adapter_available (demux->adapter);
  if (avail < 16)
    return GST_FLOW_OK;

  if (!gst_real_audio_demux_get_data_offset_from_header (demux))
    return GST_FLOW_ERROR;      /* shouldn't happen */

  GST_DEBUG_OBJECT (demux, "data_offset  = %u", demux->data_offset);

  if (avail + 6 < demux->data_offset) {
    GST_DEBUG_OBJECT (demux, "Need %u bytes, but only %u available now",
        demux->data_offset - 6, avail);
    return GST_FLOW_OK;
  }

  data = gst_adapter_map (demux->adapter, demux->data_offset - 6);
  g_assert (data);

  switch (demux->ra_version) {
    case 3:
      demux->fourcc = GST_RM_AUD_14_4;
      demux->packet_size = 20;
      demux->sample_rate = 8000;
      demux->channels = 1;
      demux->sample_width = 16;
      demux->flavour = 1;
      demux->leaf_size = 0;
      demux->height = 0;
      break;
    case 4:
      demux->flavour = GST_READ_UINT16_BE (data + 16);
      /* demux->frame_size = GST_READ_UINT32_BE (data + 36); */
      demux->leaf_size = GST_READ_UINT16_BE (data + 38);
      demux->height = GST_READ_UINT16_BE (data + 34);
      demux->packet_size = GST_READ_UINT32_BE (data + 18);
      demux->sample_rate = GST_READ_UINT16_BE (data + 42);
      demux->sample_width = GST_READ_UINT16_BE (data + 46);
      demux->channels = GST_READ_UINT16_BE (data + 48);
      demux->fourcc = GST_READ_UINT32_LE (data + 56);
      demux->pending_tags = gst_rm_utils_read_tags (data + 63,
          demux->data_offset - 63, gst_rm_utils_read_string8);
      break;
    default:
      g_assert_not_reached ();
#if 0
    case 5:
      demux->flavour = GST_READ_UINT16_BE (data + 16);
      /* demux->frame_size = GST_READ_UINT32_BE (data + 36); */
      demux->leaf_size = GST_READ_UINT16_BE (data + 38);
      demux->height = GST_READ_UINT16_BE (data + 34);

      demux->sample_rate = GST_READ_UINT16_BE (data + 48);
      demux->sample_width = GST_READ_UINT16_BE (data + 52);
      demux->n_channels = GST_READ_UINT16_BE (data + 54);
      demux->fourcc = RMDEMUX_FOURCC_GET (data + 60);
      break;
#endif
  }

  GST_INFO_OBJECT (demux, "packet_size  = %u", demux->packet_size);
  GST_INFO_OBJECT (demux, "sample_rate  = %u", demux->sample_rate);
  GST_INFO_OBJECT (demux, "sample_width = %u", demux->sample_width);
  GST_INFO_OBJECT (demux, "channels     = %u", demux->channels);
  GST_INFO_OBJECT (demux, "fourcc       = '%" GST_FOURCC_FORMAT "' (%08X)",
      GST_FOURCC_ARGS (demux->fourcc), demux->fourcc);

  switch (demux->fourcc) {
    case GST_RM_AUD_14_4:
      caps = gst_caps_new_simple ("audio/x-pn-realaudio", "raversion",
          G_TYPE_INT, 1, NULL);
      demux->byterate_num = 1000;
      demux->byterate_denom = 1;
      break;

    case GST_RM_AUD_28_8:
      /* FIXME: needs descrambling */
      caps = gst_caps_new_simple ("audio/x-pn-realaudio", "raversion",
          G_TYPE_INT, 2, NULL);
      break;

    case GST_RM_AUD_DNET:
      caps = gst_caps_new_simple ("audio/x-ac3", "rate", G_TYPE_INT,
          demux->sample_rate, NULL);
      if (demux->packet_size == 0 || demux->sample_rate == 0)
        goto broken_file;
      demux->byterate_num = demux->packet_size * demux->sample_rate;
      demux->byterate_denom = 1536;
      break;

      /* Sipro/ACELP.NET Voice Codec (MIME unknown) */
    case GST_RM_AUD_SIPR:
      caps = gst_caps_new_empty_simple ("audio/x-sipro");
      break;

    default:
      GST_WARNING_OBJECT (demux, "unknown fourcc %08X", demux->fourcc);
      break;
  }

  if (caps == NULL)
    goto unknown_fourcc;

  gst_caps_set_simple (caps,
      "flavor", G_TYPE_INT, demux->flavour,
      "rate", G_TYPE_INT, demux->sample_rate,
      "channels", G_TYPE_INT, demux->channels,
      "width", G_TYPE_INT, demux->sample_width,
      "leaf_size", G_TYPE_INT, demux->leaf_size,
      "packet_size", G_TYPE_INT, demux->packet_size,
      "height", G_TYPE_INT, demux->height, NULL);

  GST_INFO_OBJECT (demux, "Adding source pad, caps %" GST_PTR_FORMAT, caps);
  demux->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (demux->srcpad);
  gst_pad_set_caps (demux->srcpad, caps);
  codec_name = gst_pb_utils_get_codec_description (caps);
  gst_caps_unref (caps);
  gst_pad_set_event_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_src_event));
  gst_pad_set_query_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_real_audio_demux_src_query));
  gst_pad_set_active (demux->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad);

  if (demux->byterate_num > 0 && demux->byterate_denom > 0) {
    GstFormat bformat = GST_FORMAT_BYTES;
    gint64 size_bytes = 0;

    GST_INFO_OBJECT (demux, "byte rate = %u/%u = %u bytes/sec",
        demux->byterate_num, demux->byterate_denom,
        demux->byterate_num / demux->byterate_denom);

    if (gst_pad_peer_query_duration (demux->sinkpad, bformat, &size_bytes)) {
      demux->duration =
          gst_real_demux_get_timestamp_from_offset (demux, size_bytes);
      demux->upstream_size = size_bytes;
      GST_INFO_OBJECT (demux, "upstream_size = %" G_GUINT64_FORMAT,
          demux->upstream_size);
      GST_INFO_OBJECT (demux, "duration      = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (demux->duration));
    }
  }

  demux->need_newsegment = TRUE;

  if (codec_name) {
    if (demux->pending_tags == NULL)
      demux->pending_tags = gst_tag_list_new_empty ();

    gst_tag_list_add (demux->pending_tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_AUDIO_CODEC, codec_name, NULL);
    g_free (codec_name);
  }

  gst_adapter_unmap (demux->adapter);
  gst_adapter_flush (demux->adapter, demux->data_offset - 6);

  demux->state = REAL_AUDIO_DEMUX_STATE_DATA;
  demux->need_newsegment = TRUE;

  return GST_FLOW_OK;

/* ERRORS */
unknown_fourcc:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (demux), STREAM, DECODE, (NULL),
        ("Unknown fourcc '%" GST_FOURCC_FORMAT "'",
            GST_FOURCC_ARGS (demux->fourcc)));
    return GST_FLOW_ERROR;
  }
broken_file:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (demux), STREAM, DECODE, (NULL),
        ("Broken file - invalid sample_rate or other header value"));
    return GST_FLOW_ERROR;
  }

}

static GstFlowReturn
gst_real_audio_demux_parse_data (GstRealAudioDemux * demux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint avail, unit_size;

  avail = gst_adapter_available (demux->adapter);

  if (demux->packet_size > 0)
    unit_size = demux->packet_size;
  else
    unit_size = avail & 0xfffffff0;     /* round down to next multiple of 16 */

  GST_LOG_OBJECT (demux, "available = %u, unit_size = %u", avail, unit_size);

  while (ret == GST_FLOW_OK && unit_size > 0 && avail >= unit_size) {
    GstClockTime ts;
    GstBuffer *buf;

    buf = gst_adapter_take_buffer (demux->adapter, unit_size);
    avail -= unit_size;

    if (demux->need_newsegment) {
      gst_pad_push_event (demux->srcpad,
          gst_event_new_segment (&demux->segment));
      demux->need_newsegment = FALSE;
    }

    if (demux->pending_tags) {
      gst_pad_push_event (demux->srcpad,
          gst_event_new_tag (demux->pending_tags));
      demux->pending_tags = NULL;
    }

    if (demux->fourcc == GST_RM_AUD_DNET) {
      buf = gst_rm_utils_descramble_dnet_buffer (buf);
    }

    ts = gst_real_demux_get_timestamp_from_offset (demux, demux->offset);
    GST_BUFFER_TIMESTAMP (buf) = ts;

    demux->segment.position = ts;

    ret = gst_pad_push (demux->srcpad, buf);
  }

  return ret;
}

static GstFlowReturn
gst_real_audio_demux_handle_buffer (GstRealAudioDemux * demux, GstBuffer * buf)
{
  GstFlowReturn ret;

  gst_adapter_push (demux->adapter, buf);
  buf = NULL;

  switch (demux->state) {
    case REAL_AUDIO_DEMUX_STATE_MARKER:{
      ret = gst_real_audio_demux_parse_marker (demux);
      if (ret != GST_FLOW_OK || demux->state != REAL_AUDIO_DEMUX_STATE_HEADER)
        break;
      /* otherwise fall through */
    }
    case REAL_AUDIO_DEMUX_STATE_HEADER:{
      ret = gst_real_audio_demux_parse_header (demux);
      if (ret != GST_FLOW_OK || demux->state != REAL_AUDIO_DEMUX_STATE_DATA)
        break;
      /* otherwise fall through */
    }
    case REAL_AUDIO_DEMUX_STATE_DATA:{
      ret = gst_real_audio_demux_parse_data (demux);
      break;
    }
    default:
      g_return_val_if_reached (GST_FLOW_ERROR);
  }

  return ret;
}

static GstFlowReturn
gst_real_audio_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstRealAudioDemux *demux;

  demux = GST_REAL_AUDIO_DEMUX (parent);

  return gst_real_audio_demux_handle_buffer (demux, buf);
}

static void
gst_real_audio_demux_loop (GstRealAudioDemux * demux)
{
  GstFlowReturn ret;
  GstBuffer *buf;
  guint bytes_needed;

  /* check how much data we need */
  switch (demux->state) {
    case REAL_AUDIO_DEMUX_STATE_MARKER:
      bytes_needed = 6 + 16;    /* 16 are beginning of header */
      break;
    case REAL_AUDIO_DEMUX_STATE_HEADER:
      if (!gst_real_audio_demux_get_data_offset_from_header (demux))
        goto parse_header_error;
      bytes_needed = demux->data_offset - (6 + 16);
      break;
    case REAL_AUDIO_DEMUX_STATE_DATA:
      if (demux->packet_size > 0) {
        /* TODO: should probably take into account width/height as well? */
        bytes_needed = demux->packet_size;
      } else {
        bytes_needed = 1024;
      }
      break;
    default:
      g_return_if_reached ();
  }

  /* now get the data */
  GST_LOG_OBJECT (demux, "getting data: %5u bytes @ %8" G_GINT64_MODIFIER "u",
      bytes_needed, demux->offset);

  if (demux->upstream_size > 0 && demux->offset >= demux->upstream_size)
    goto eos;

  buf = NULL;
  ret = gst_pad_pull_range (demux->sinkpad, demux->offset, bytes_needed, &buf);

  if (ret != GST_FLOW_OK)
    goto pull_range_error;

  if (gst_buffer_get_size (buf) != bytes_needed)
    goto pull_range_short_read;

  ret = gst_real_audio_demux_handle_buffer (demux, buf);
  if (ret != GST_FLOW_OK)
    goto handle_flow_error;

  /* TODO: increase this in chain function too (for timestamps)? */
  demux->offset += bytes_needed;

  /* check for the end of the segment */
  if (demux->segment.stop != -1 && demux->segment.position != -1 &&
      demux->segment.position > demux->segment.stop) {
    GST_DEBUG_OBJECT (demux, "reached end of segment");
    goto eos;
  }

  return;

/* ERRORS */
parse_header_error:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL), (NULL));
    goto pause_task;
  }
handle_flow_error:
  {
    GST_WARNING_OBJECT (demux, "handle_buf flow: %s", gst_flow_get_name (ret));
    goto pause_task;
  }
pull_range_error:
  {
    GST_WARNING_OBJECT (demux, "pull range flow: %s", gst_flow_get_name (ret));
    goto pause_task;
  }
pull_range_short_read:
  {
    GST_WARNING_OBJECT (demux, "pull range short read: wanted %u bytes, but "
        "got only %" G_GSIZE_FORMAT " bytes", bytes_needed,
        gst_buffer_get_size (buf));
    gst_buffer_unref (buf);
    goto eos;
  }
eos:
  {
    if (demux->state != REAL_AUDIO_DEMUX_STATE_DATA) {
      GST_WARNING_OBJECT (demux, "reached EOS before finished parsing header");
      goto parse_header_error;
    }
    GST_INFO_OBJECT (demux, "EOS");
    if ((demux->segment.flags & GST_SEEK_FLAG_SEGMENT) != 0) {
      gint64 stop;

      /* for segment playback we need to post when (in stream time)
       * we stopped, this is either stop (when set) or the duration. */
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;

      GST_DEBUG_OBJECT (demux, "sending segment done, at end of segment");
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
              stop));
    } else {
      /* normal playback, send EOS event downstream */
      GST_DEBUG_OBJECT (demux, "sending EOS event, at end of stream");
      gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    }
    goto pause_task;
  }
pause_task:
  {
    demux->segment_running = FALSE;
    gst_pad_pause_task (demux->sinkpad);
    GST_DEBUG_OBJECT (demux, "pausing task");
    return;
  }
}

static gboolean
gst_real_audio_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRealAudioDemux *demux;
  gboolean ret;

  demux = GST_REAL_AUDIO_DEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:{
      /* FIXME */
      gst_event_unref (event);
      demux->need_newsegment = TRUE;
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static gboolean
gst_real_audio_demux_handle_seek (GstRealAudioDemux * demux, GstEvent * event)
{
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gboolean flush, update;
  gdouble rate;
  guint64 seek_pos;
  gint64 cur, stop;

  if (!demux->seekable)
    goto not_seekable;

  if (demux->byterate_num == 0 || demux->byterate_denom == 0)
    goto no_bitrate;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  if (format != GST_FORMAT_TIME)
    goto only_time_format_supported;

  if (rate <= 0.0)
    goto cannot_do_backwards_playback;

  flush = ((flags & GST_SEEK_FLAG_FLUSH) != 0);

  GST_DEBUG_OBJECT (demux, "flush=%d, rate=%g", flush, rate);

  /* unlock streaming thread and make streaming stop */
  if (flush) {
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_start ());
    gst_pad_push_event (demux->srcpad, gst_event_new_flush_start ());
  } else {
    gst_pad_pause_task (demux->sinkpad);
  }

  GST_PAD_STREAM_LOCK (demux->sinkpad);

  gst_segment_do_seek (&demux->segment, rate, format, flags,
      cur_type, cur, stop_type, stop, &update);

  GST_DEBUG_OBJECT (demux, "segment: %" GST_SEGMENT_FORMAT, &demux->segment);

  seek_pos = gst_util_uint64_scale (demux->segment.start,
      demux->byterate_num, demux->byterate_denom * GST_SECOND);
  if (demux->packet_size > 0) {
    seek_pos -= seek_pos % demux->packet_size;
  }
  seek_pos += demux->data_offset;

  GST_DEBUG_OBJECT (demux, "seek_pos = %" G_GUINT64_FORMAT, seek_pos);

  /* stop flushing */
  gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop (TRUE));
  gst_pad_push_event (demux->srcpad, gst_event_new_flush_stop (TRUE));

  demux->offset = seek_pos;
  demux->need_newsegment = TRUE;

  /* notify start of new segment */
  if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT (demux),
        gst_message_new_segment_start (GST_OBJECT (demux),
            GST_FORMAT_TIME, demux->segment.position));
  }

  demux->segment_running = TRUE;
  /* restart our task since it might have been stopped when we did the flush */
  gst_pad_start_task (demux->sinkpad,
      (GstTaskFunction) gst_real_audio_demux_loop, demux);

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;

/* ERRORS */
not_seekable:
  {
    GST_DEBUG_OBJECT (demux, "seek failed: cannot seek in streaming mode");
    return FALSE;
  }
no_bitrate:
  {
    GST_DEBUG_OBJECT (demux, "seek failed: bitrate unknown");
    return FALSE;
  }
only_time_format_supported:
  {
    GST_DEBUG_OBJECT (demux, "can only seek in TIME format");
    return FALSE;
  }
cannot_do_backwards_playback:
  {
    GST_DEBUG_OBJECT (demux, "can only seek with positive rate, not %lf", rate);
    return FALSE;
  }
}

static gboolean
gst_real_audio_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRealAudioDemux *demux;
  gboolean ret = FALSE;

  demux = GST_REAL_AUDIO_DEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      gst_event_unref (event);
      break;
    case GST_EVENT_SEEK:
      ret = gst_real_audio_demux_handle_seek (demux, event);
      gst_event_unref (event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_real_audio_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstRealAudioDemux *demux;
  gboolean ret = FALSE;

  demux = GST_REAL_AUDIO_DEMUX (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME && demux->duration > 0) {
        gst_query_set_duration (query, GST_FORMAT_TIME, demux->duration);
        ret = TRUE;
      } else if (format == GST_FORMAT_BYTES && demux->upstream_size > 0) {
        gst_query_set_duration (query, GST_FORMAT_BYTES,
            demux->upstream_size - demux->data_offset);
        ret = TRUE;
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat format;
      gboolean seekable;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      seekable = (format == GST_FORMAT_TIME && demux->seekable);
      gst_query_set_seeking (query, format, seekable, 0,
          (format == GST_FORMAT_TIME) ? demux->duration : -1);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static GstStateChangeReturn
gst_real_audio_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstRealAudioDemux *demux = GST_REAL_AUDIO_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      demux->state = REAL_AUDIO_DEMUX_STATE_MARKER;
      demux->segment_running = FALSE;
      gst_segment_init (&demux->segment, GST_FORMAT_TIME);
      gst_adapter_clear (demux->adapter);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gst_real_audio_demux_reset (demux);
      gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_rademux_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rademux",
      GST_RANK_SECONDARY, GST_TYPE_REAL_AUDIO_DEMUX);
}
