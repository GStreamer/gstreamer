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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmpegdemux.h"


GST_DEBUG_CATEGORY_STATIC (gstmpegdemux_debug);
#define GST_CAT_DEFAULT (gstmpegdemux_debug)

#define PARSE_CLASS(o)  GST_MPEG_PARSE_CLASS (G_OBJECT_GET_CLASS (o))
#define CLASS(o)  GST_MPEG_DEMUX_CLASS (G_OBJECT_GET_CLASS (o))

/* MPEG2Demux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BIT_RATE,
  ARG_MPEG2
      /* FILL ME */
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2 }, " "systemstream = (boolean) TRUE")
    );

static GstStaticPadTemplate video_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2 }, " "systemstream = (boolean) FALSE")
    );

static GstStaticPadTemplate audio_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1"
        /* FIXME "layer = (int) { 1, 2 }" */
    )
    );

static GstStaticPadTemplate private_template =
GST_STATIC_PAD_TEMPLATE ("private_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gstmpegdemux_debug, "mpegdemux", 0, \
        "MPEG demuxer element");

GST_BOILERPLATE_FULL (GstMPEGDemux, gst_mpeg_demux, GstMPEGParse,
    GST_TYPE_MPEG_PARSE, _do_init);

static gboolean gst_mpeg_demux_process_event (GstMPEGParse * mpeg_parse,
    GstEvent * event);

static GstPad *gst_mpeg_demux_new_output_pad (GstMPEGDemux * mpeg_demux,
    const gchar * name, GstPadTemplate * temp);
static void gst_mpeg_demux_init_stream (GstMPEGDemux * mpeg_demux,
    gint type,
    GstMPEGStream * str,
    gint number, const gchar * name, GstPadTemplate * temp);
static GstMPEGStream *gst_mpeg_demux_get_video_stream (GstMPEGDemux *
    mpeg_demux, guint8 stream_nr, gint type, const gpointer info);
static GstMPEGStream *gst_mpeg_demux_get_audio_stream (GstMPEGDemux *
    mpeg_demux, guint8 stream_nr, gint type, const gpointer info);
static GstMPEGStream *gst_mpeg_demux_get_private_stream (GstMPEGDemux *
    mpeg_demux, guint8 stream_nr, gint type, const gpointer info);

static gboolean gst_mpeg_demux_parse_packhead (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);
static gboolean gst_mpeg_demux_parse_syshead (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);
static GstFlowReturn gst_mpeg_demux_parse_packet (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);
static GstFlowReturn gst_mpeg_demux_parse_pes (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);

static GstFlowReturn gst_mpeg_demux_send_subbuffer (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * outstream, GstBuffer * buffer,
    GstClockTime timestamp, guint offset, guint size);
static GstFlowReturn gst_mpeg_demux_combine_flows (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * stream, GstFlowReturn flow);
static GstFlowReturn gst_mpeg_demux_process_private (GstMPEGDemux * mpeg_demux,
    GstBuffer * buffer,
    guint stream_nr, GstClockTime timestamp, guint headerlen, guint datalen);
static void gst_mpeg_demux_synchronise_pads (GstMPEGDemux * mpeg_demux,
    GstClockTime threshold, GstClockTime new_ts);
static void gst_mpeg_demux_sync_stream_to_time (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * stream, GstClockTime last_ts);

#if 0
const GstFormat *gst_mpeg_demux_get_src_formats (GstPad * pad);

static gboolean index_seek (GstPad * pad, GstEvent * event, gint64 * offset);
static gboolean normal_seek (GstPad * pad, GstEvent * event, gint64 * offset);

static gboolean gst_mpeg_demux_handle_src_event (GstPad * pad,
    GstEvent * event);
#endif
static void gst_mpeg_demux_reset (GstMPEGDemux * mpeg_demux);

#if 0
static gboolean gst_mpeg_demux_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);
#endif

static GstStateChangeReturn gst_mpeg_demux_change_state (GstElement * element,
    GstStateChange transition);

static void gst_mpeg_demux_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_mpeg_demux_get_index (GstElement * element);


/*static guint gst_mpeg_demux_signals[LAST_SIGNAL] = { 0 };*/

static void
gst_mpeg_demux_base_init (gpointer klass_ptr)
{
  GstMPEGDemuxClass *klass = GST_MPEG_DEMUX_CLASS (klass_ptr);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass_ptr);

  klass->video_template = gst_static_pad_template_get (&video_template);
  klass->audio_template = gst_static_pad_template_get (&audio_template);
  klass->private_template = gst_static_pad_template_get (&private_template);

  gst_element_class_add_pad_template (element_class, klass->video_template);
  gst_element_class_add_pad_template (element_class, klass->audio_template);
  gst_element_class_add_pad_template (element_class, klass->private_template);

  gst_element_class_set_details_simple (element_class, "MPEG Demuxer",
      "Codec/Demuxer",
      "Demultiplexes MPEG1 and MPEG2 System Streams",
      "Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim.taymans@chello.be>");
}

static void
gst_mpeg_demux_class_init (GstMPEGDemuxClass * klass)
{
  GstElementClass *gstelement_class;
  GstMPEGParseClass *mpeg_parse_class;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class = (GstElementClass *) klass;
  mpeg_parse_class = (GstMPEGParseClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mpeg_demux_change_state);
  gstelement_class->set_index = GST_DEBUG_FUNCPTR (gst_mpeg_demux_set_index);
  gstelement_class->get_index = GST_DEBUG_FUNCPTR (gst_mpeg_demux_get_index);

  mpeg_parse_class->parse_packhead = gst_mpeg_demux_parse_packhead;
  mpeg_parse_class->parse_syshead = gst_mpeg_demux_parse_syshead;
  mpeg_parse_class->parse_packet = gst_mpeg_demux_parse_packet;
  mpeg_parse_class->parse_pes = gst_mpeg_demux_parse_pes;
  mpeg_parse_class->send_buffer = NULL;
  mpeg_parse_class->process_event = gst_mpeg_demux_process_event;

  klass->new_output_pad = gst_mpeg_demux_new_output_pad;
  klass->init_stream = gst_mpeg_demux_init_stream;
  klass->get_video_stream = gst_mpeg_demux_get_video_stream;
  klass->get_audio_stream = gst_mpeg_demux_get_audio_stream;
  klass->get_private_stream = gst_mpeg_demux_get_private_stream;
  klass->send_subbuffer = gst_mpeg_demux_send_subbuffer;
  klass->combine_flows = gst_mpeg_demux_combine_flows;
  klass->process_private = gst_mpeg_demux_process_private;
  klass->synchronise_pads = gst_mpeg_demux_synchronise_pads;
  klass->sync_stream_to_time = gst_mpeg_demux_sync_stream_to_time;

  /* we have our own sink pad template, but don't use it in subclasses */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
}

static void
gst_mpeg_demux_init (GstMPEGDemux * mpeg_demux, GstMPEGDemuxClass * klass)
{
  gint i;

  /* i think everything is already zero'd, but oh well */
  for (i = 0; i < GST_MPEG_DEMUX_NUM_VIDEO_STREAMS; i++) {
    mpeg_demux->video_stream[i] = NULL;
  }
  for (i = 0; i < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS; i++) {
    mpeg_demux->audio_stream[i] = NULL;
  }
  for (i = 0; i < GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS; i++) {
    mpeg_demux->private_stream[i] = NULL;
  }

  mpeg_demux->max_gap = GST_CLOCK_TIME_NONE;
  mpeg_demux->max_gap_tolerance = GST_CLOCK_TIME_NONE;

  mpeg_demux->last_pts = -1;
  mpeg_demux->pending_tags = FALSE;
}


static gboolean
gst_mpeg_demux_process_event (GstMPEGParse * mpeg_parse, GstEvent * event)
{
  GstMPEGDemux *demux = GST_MPEG_DEMUX (mpeg_parse);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      ret = GST_MPEG_PARSE_CLASS (parent_class)->process_event (mpeg_parse,
          event);

      demux->pending_tags = TRUE;

      gst_mpeg_streams_reset_last_flow (demux->video_stream,
          GST_MPEG_DEMUX_NUM_VIDEO_STREAMS);
      gst_mpeg_streams_reset_last_flow (demux->audio_stream,
          GST_MPEG_DEMUX_NUM_AUDIO_STREAMS);
      gst_mpeg_streams_reset_last_flow (demux->private_stream,
          GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS);
      break;
    case GST_EVENT_NEWSEGMENT:
      /* reset stream synchronization */
      gst_mpeg_streams_reset_cur_ts (demux->video_stream,
          GST_MPEG_DEMUX_NUM_VIDEO_STREAMS, 0);
      gst_mpeg_streams_reset_cur_ts (demux->audio_stream,
          GST_MPEG_DEMUX_NUM_AUDIO_STREAMS, 0);
      gst_mpeg_streams_reset_cur_ts (demux->private_stream,
          GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS, 0);
      /* fallthrough */
    default:
      ret = GST_MPEG_PARSE_CLASS (parent_class)->process_event (mpeg_parse,
          event);
      break;
  }

  return ret;
}

static gint
_demux_get_writer_id (GstIndex * index, GstPad * pad)
{
  gint id;

  if (!gst_index_get_writer_id (index, GST_OBJECT (pad), &id)) {
    GST_WARNING_OBJECT (index,
        "can't get index id for %s:%s", GST_DEBUG_PAD_NAME (pad));
    return -1;
  } else {
    GST_LOG_OBJECT (index,
        "got index id %d for %s:%s", id, GST_DEBUG_PAD_NAME (pad));
    return id;
  }
}

static GstPad *
gst_mpeg_demux_new_output_pad (GstMPEGDemux * mpeg_demux,
    const gchar * name, GstPadTemplate * temp)
{
  GstPad *pad;

  pad = gst_pad_new_from_template (temp, name);

#if 0
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_mpeg_demux_handle_src_event));
#endif
  gst_pad_set_query_type_function (pad,
      GST_DEBUG_FUNCPTR (gst_mpeg_parse_get_src_query_types));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_mpeg_parse_handle_src_query));
  gst_pad_use_fixed_caps (pad);

  return pad;
}

static void
gst_mpeg_demux_init_stream (GstMPEGDemux * mpeg_demux,
    gint type,
    GstMPEGStream * str, gint number, const gchar * name, GstPadTemplate * temp)
{
  str->type = type;
  str->number = number;

  str->pad = CLASS (mpeg_demux)->new_output_pad (mpeg_demux, name, temp);
  gst_pad_set_element_private (str->pad, str);

  if (mpeg_demux->index) {
    str->index_id = _demux_get_writer_id (mpeg_demux->index, str->pad);
  }

  str->cur_ts = 0;
  str->scr_offs = 0;

  str->last_flow = GST_FLOW_OK;
  str->buffers_sent = 0;
  str->tags = NULL;
  str->caps = NULL;
}

static GstMPEGStream *
gst_mpeg_demux_get_video_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  gint mpeg_version = *((gint *) info);
  GstMPEGStream *str;
  GstMPEGVideoStream *video_str;
  gchar *name;
  gboolean set_caps = FALSE;

  g_return_val_if_fail (stream_nr < GST_MPEG_DEMUX_NUM_VIDEO_STREAMS, NULL);
  g_return_val_if_fail (type > GST_MPEG_DEMUX_VIDEO_UNKNOWN &&
      type < GST_MPEG_DEMUX_VIDEO_LAST, NULL);

  str = mpeg_demux->video_stream[stream_nr];

  if (str == NULL) {
    video_str = g_new0 (GstMPEGVideoStream, 1);
    str = (GstMPEGStream *) video_str;

    name = g_strdup_printf ("video_%02d", stream_nr);
    CLASS (mpeg_demux)->init_stream (mpeg_demux, type, str, stream_nr, name,
        CLASS (mpeg_demux)->video_template);
    g_free (name);

    set_caps = TRUE;
  } else {
    /* This stream may have been created by a derived class, reset the
       size. */
    video_str = g_renew (GstMPEGVideoStream, str, 1);
    str = (GstMPEGStream *) video_str;
  }

  mpeg_demux->video_stream[stream_nr] = str;

  if (set_caps || video_str->mpeg_version != mpeg_version) {
    gchar *codec;
    GstTagList *list;

    /* We need to set new caps for this pad. */
    if (str->caps)
      gst_caps_unref (str->caps);
    str->caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, mpeg_version,
        "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    if (!gst_pad_set_caps (str->pad, str->caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
          CORE, NEGOTIATION, (NULL), ("failed to set caps"));
      gst_caps_unref (str->caps);
      str->caps = NULL;
      gst_pad_set_active (str->pad, TRUE);
      gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);
      return str;
    }
    gst_pad_set_active (str->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);

    /* Store the current values. */
    video_str->mpeg_version = mpeg_version;

    /* set stream metadata */
    codec = g_strdup_printf ("MPEG-%d video", mpeg_version);
    list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_VIDEO_CODEC, codec, NULL);
    g_free (codec);
    gst_element_found_tags_for_pad (GST_ELEMENT (mpeg_demux), str->pad, list);
  }

  return str;
}

static GstMPEGStream *
gst_mpeg_demux_get_audio_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  GstMPEGStream *str;
  gchar *name;
  gboolean set_caps = FALSE;

  g_return_val_if_fail (stream_nr < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS, NULL);
  g_return_val_if_fail (type > GST_MPEG_DEMUX_AUDIO_UNKNOWN &&
      type < GST_MPEG_DEMUX_AUDIO_LAST, NULL);

  str = mpeg_demux->audio_stream[stream_nr];

  if (str && str->type != type) {
    gst_element_remove_pad (GST_ELEMENT (mpeg_demux), str->pad);
    g_free (str);
    str = mpeg_demux->audio_stream[stream_nr] = NULL;
  }

  if (str == NULL) {
    str = g_new0 (GstMPEGStream, 1);

    name = g_strdup_printf ("audio_%02d", stream_nr);
    CLASS (mpeg_demux)->init_stream (mpeg_demux, type, str, stream_nr, name,
        CLASS (mpeg_demux)->audio_template);
    g_free (name);

    /* new pad, set caps */
    set_caps = TRUE;
  } else {
    /* This stream may have been created by a derived class, reset the
       size. */
    str = g_renew (GstMPEGStream, str, 1);
  }

  mpeg_demux->audio_stream[stream_nr] = str;

  if (set_caps) {
    GstTagList *list;

    /* We need to set new caps for this pad. */
    if (str->caps)
      gst_caps_unref (str->caps);
    str->caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 1, NULL);
    if (!gst_pad_set_caps (str->pad, str->caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
          CORE, NEGOTIATION, (NULL), ("failed to set caps"));
      gst_caps_unref (str->caps);
      str->caps = NULL;
      gst_pad_set_active (str->pad, TRUE);
      gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);
      return str;
    }
    gst_pad_set_active (str->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);

    /* stream metadata */
    list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_AUDIO_CODEC, "MPEG-1 audio", NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (mpeg_demux), str->pad, list);
  }

  return str;
}

static GstMPEGStream *
gst_mpeg_demux_get_private_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  GstMPEGStream *str;
  gchar *name;

  g_return_val_if_fail (stream_nr < GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS, NULL);

  str = mpeg_demux->private_stream[stream_nr];

  if (str == NULL) {
    name = g_strdup_printf ("private_%d", stream_nr + 1);
    str = g_new0 (GstMPEGStream, 1);
    CLASS (mpeg_demux)->init_stream (mpeg_demux, type, str, stream_nr, name,
        CLASS (mpeg_demux)->private_template);
    g_free (name);
    gst_pad_set_active (str->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);

    mpeg_demux->private_stream[stream_nr] = str;
  }

  return str;
}

static gboolean
gst_mpeg_demux_parse_packhead (GstMPEGParse * mpeg_parse, GstBuffer * buffer)
{
  GstMPEGDemux *demux = GST_MPEG_DEMUX (mpeg_parse);

  parent_class->parse_packhead (mpeg_parse, buffer);

  /* do something useful here */

  if (demux->pending_tags) {
    GstMPEGStream **streams;
    guint i, num;

    streams = demux->audio_stream;
    num = GST_MPEG_DEMUX_NUM_AUDIO_STREAMS;
    for (i = 0; i < num; ++i) {
      if (streams[i] != NULL && streams[i]->tags != NULL)
        gst_pad_push_event (streams[i]->pad,
            gst_event_new_tag (gst_tag_list_copy (streams[i]->tags)));
    }
    demux->pending_tags = FALSE;
  }

  return TRUE;
}

static gboolean
gst_mpeg_demux_parse_syshead (GstMPEGParse * mpeg_parse, GstBuffer * buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  guint16 header_length;
  guchar *buf;

  buf = GST_BUFFER_DATA (buffer);
  buf += 4;

  header_length = GST_READ_UINT16_BE (buf);
  GST_DEBUG_OBJECT (mpeg_demux, "header_length %d", header_length);
  buf += 2;

  /* marker:1==1 ! rate_bound:22 | marker:1==1 */
  buf += 3;

  /* audio_bound:6==1 ! fixed:1 | constrained:1 */
  buf += 1;

  /* audio_lock:1 | video_lock:1 | marker:1==1 | video_bound:5 */
  buf += 1;

  /* apacket_rate_restriction:1 | reserved:7==0x7F */
  buf += 1;

  if (!GST_MPEG_PARSE_IS_MPEG2 (mpeg_demux)) {
    gint stream_count = (header_length - 6) / 3;
    gint i, j = 0;

    /* Reset the total_size_bound before counting it up */
    mpeg_demux->total_size_bound = 0;

    GST_DEBUG_OBJECT (mpeg_demux, "number of streams: %d ", stream_count);

    for (i = 0; i < stream_count; i++) {
      guint8 stream_id;
      gboolean STD_buffer_bound_scale;
      guint16 STD_buffer_size_bound;
      guint32 buf_byte_size_bound;
      GstMPEGStream *outstream = NULL;

      stream_id = *buf++;
      if (!(stream_id & 0x80)) {
        GST_DEBUG_OBJECT (mpeg_demux, "error in system header length");
        return FALSE;
      }

      /* check marker bits */
      if ((*buf & 0xC0) != 0xC0) {
        GST_DEBUG_OBJECT (mpeg_demux, "expecting placeholder bit values"
            " '11' after stream id");
        return FALSE;
      }

      STD_buffer_bound_scale = *buf & 0x20;
      STD_buffer_size_bound = ((guint16) (*buf++ & 0x1F)) << 8;
      STD_buffer_size_bound |= *buf++;

      if (STD_buffer_bound_scale == 0) {
        buf_byte_size_bound = STD_buffer_size_bound * 128;
      } else {
        buf_byte_size_bound = STD_buffer_size_bound * 1024;
      }

      if (stream_id == 0xBD) {
        /* Private stream 1. */
        outstream = CLASS (mpeg_demux)->get_private_stream (mpeg_demux,
            0, GST_MPEG_DEMUX_PRIVATE_UNKNOWN, NULL);
      } else if (stream_id == 0xBF) {
        /* Private stream 2. */
        outstream = CLASS (mpeg_demux)->get_private_stream (mpeg_demux,
            1, GST_MPEG_DEMUX_PRIVATE_UNKNOWN, NULL);
      } else if (stream_id >= 0xC0 && stream_id <= 0xDF) {
        /* Audio. */
        outstream = CLASS (mpeg_demux)->get_audio_stream (mpeg_demux,
            stream_id - 0xC0, GST_MPEG_DEMUX_AUDIO_MPEG, NULL);
      } else if (stream_id >= 0xE0 && stream_id <= 0xEF) {
        /* Video. */
        gint mpeg_version = !GST_MPEG_PARSE_IS_MPEG2 (mpeg_demux) ? 1 : 2;

        outstream = CLASS (mpeg_demux)->get_video_stream (mpeg_demux,
            stream_id - 0xE0, GST_MPEG_DEMUX_VIDEO_MPEG, &mpeg_version);
      } else {
        GST_WARNING_OBJECT (mpeg_demux, "unknown stream id 0x%02x", stream_id);
      }

      GST_DEBUG_OBJECT (mpeg_demux, "STD_buffer_bound_scale %d",
          STD_buffer_bound_scale);
      GST_DEBUG_OBJECT (mpeg_demux, "STD_buffer_size_bound %d or %d bytes",
          STD_buffer_size_bound, buf_byte_size_bound);

      if (outstream != NULL) {
        outstream->size_bound = buf_byte_size_bound;
        mpeg_demux->total_size_bound += buf_byte_size_bound;

        if (mpeg_demux->index) {
          outstream->index_id =
              _demux_get_writer_id (mpeg_demux->index, outstream->pad);
        }
      }

      j++;
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_mpeg_demux_parse_packet (GstMPEGParse * mpeg_parse, GstBuffer * buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  guint8 id;
  guint16 headerlen;

  guint16 packet_length;
  guint16 STD_buffer_size_bound G_GNUC_UNUSED;
  guint64 dts;
  gint64 pts = -1;

  guint16 datalen;

  GstMPEGStream *outstream = NULL;
  guint8 *buf;
  gint64 timestamp;

  GstFlowReturn ret = GST_FLOW_OK;

  buf = GST_BUFFER_DATA (buffer);
  id = *(buf + 3);
  buf += 4;

  /* start parsing */
  packet_length = GST_READ_UINT16_BE (buf);

  GST_DEBUG_OBJECT (mpeg_demux, "got packet_length %d", packet_length);
  headerlen = 2;
  buf += 2;

  /* loop through looping for stuffing bits, STD, PTS, DTS, etc */
  do {
    guint8 bits = *buf++;

    /* stuffing bytes */
    switch (bits & 0xC0) {
      case 0xC0:
        if (bits == 0xff) {
          GST_DEBUG_OBJECT (mpeg_demux, "have stuffing byte");
        } else {
          GST_DEBUG_OBJECT (mpeg_demux, "expected stuffing byte");
        }
        headerlen++;
        break;
      case 0x40:
        GST_DEBUG_OBJECT (mpeg_demux, "have STD");

        /* STD_buffer_bound_scale = ((bits & 0x20) == 0x20); */
        STD_buffer_size_bound = ((guint16) (bits & 0x1F)) << 8;
        STD_buffer_size_bound |= *buf++;

        headerlen += 2;
        break;
      case 0x00:
        switch (bits & 0x30) {
          case 0x20:
            /* pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
            pts = ((guint64) (bits & 0x0E)) << 29;
            pts |= ((guint64) (*buf++)) << 22;
            pts |= ((guint64) (*buf++ & 0xFE)) << 14;
            pts |= ((guint64) (*buf++)) << 7;
            pts |= ((guint64) (*buf++ & 0xFE)) >> 1;

            GST_DEBUG_OBJECT (mpeg_demux, "PTS = %" G_GUINT64_FORMAT, pts);
            headerlen += 5;
            goto done;
          case 0x30:
            /* pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
            pts = ((guint64) (bits & 0x0E)) << 29;
            pts |= ((guint64) (*buf++)) << 22;
            pts |= ((guint64) (*buf++ & 0xFE)) << 14;
            pts |= ((guint64) (*buf++)) << 7;
            pts |= ((guint64) (*buf++ & 0xFE)) >> 1;

            /* sync:4 ! pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
            dts = ((guint64) (*buf++ & 0x0E)) << 29;
            dts |= ((guint64) * buf++) << 22;
            dts |= ((guint64) (*buf++ & 0xFE)) << 14;
            dts |= ((guint64) * buf++) << 7;
            dts |= ((guint64) (*buf++ & 0xFE)) >> 1;

            GST_DEBUG_OBJECT (mpeg_demux, "PTS = %" G_GUINT64_FORMAT
                ", DTS = %" G_GUINT64_FORMAT, pts, dts);
            headerlen += 10;
            goto done;
          case 0x00:
            GST_DEBUG_OBJECT (mpeg_demux, "have no pts/dts");
            GST_DEBUG_OBJECT (mpeg_demux, "got trailer bits %x", (bits & 0x0f));
            if ((bits & 0x0f) != 0xf) {
              GST_DEBUG_OBJECT (mpeg_demux, "not a valid packet time sequence");
              return FALSE;
            }
            headerlen++;
          default:
            goto done;
        }
      default:
        goto done;
    }
  } while (1);
  GST_DEBUG_OBJECT (mpeg_demux, "done with header loop");

done:

  /* calculate the amount of real data in this packet */
  datalen = packet_length - headerlen + 2;

  GST_DEBUG_OBJECT (mpeg_demux, "headerlen is %d, datalen is %d",
      headerlen, datalen);

  if (pts != -1) {
    /* Check for pts overflow */
    if (mpeg_demux->last_pts != -1) {
      gint32 diff = pts - mpeg_demux->last_pts;

      if (diff > -4 * CLOCK_FREQ && diff < 4 * CLOCK_FREQ)
        pts = mpeg_demux->last_pts + diff;
    }
    mpeg_demux->last_pts = pts;

    timestamp = PARSE_CLASS (mpeg_parse)->adjust_ts (mpeg_parse,
        MPEGTIME_TO_GSTTIME (pts));

    /* this apparently happens for some input were headers are
     * rewritten to make time start at zero... */
    if ((gint64) timestamp < 0)
      timestamp = 0;
  } else {
    timestamp = GST_CLOCK_TIME_NONE;
  }

  if (id == 0xBD) {
    /* Private stream 1. */
    GST_DEBUG_OBJECT (mpeg_demux, "we have a private 1 packet");
    ret = CLASS (mpeg_demux)->process_private (mpeg_demux, buffer, 0, timestamp,
        headerlen, datalen);
  } else if (id == 0xBF) {
    /* Private stream 2. */
    GST_DEBUG_OBJECT (mpeg_demux, "we have a private 2 packet");
    ret = CLASS (mpeg_demux)->process_private (mpeg_demux, buffer, 1, timestamp,
        headerlen, datalen);
  } else if (id >= 0xC0 && id <= 0xDF) {
    /* Audio. */
    GST_DEBUG_OBJECT (mpeg_demux, "we have an audio packet");
    outstream = CLASS (mpeg_demux)->get_audio_stream (mpeg_demux,
        id - 0xC0, GST_MPEG_DEMUX_AUDIO_MPEG, NULL);
    ret = CLASS (mpeg_demux)->send_subbuffer (mpeg_demux, outstream, buffer,
        timestamp, headerlen + 4, datalen);
  } else if (id >= 0xE0 && id <= 0xEF) {
    /* Video. */
    gint mpeg_version = !GST_MPEG_PARSE_IS_MPEG2 (mpeg_demux) ? 1 : 2;

    GST_DEBUG_OBJECT (mpeg_demux, "we have a video packet");

    outstream = CLASS (mpeg_demux)->get_video_stream (mpeg_demux,
        id - 0xE0, GST_MPEG_DEMUX_VIDEO_MPEG, &mpeg_version);
    ret = CLASS (mpeg_demux)->send_subbuffer (mpeg_demux, outstream, buffer,
        timestamp, headerlen + 4, datalen);
  } else if (id == 0xBE) {
    /* padding stream */
    GST_DEBUG_OBJECT (mpeg_demux, "we have a padding packet");
  } else {
    GST_WARNING_OBJECT (mpeg_demux, "unknown stream id 0x%02x", id);
  }

  return ret;
}

static GstFlowReturn
gst_mpeg_demux_parse_pes (GstMPEGParse * mpeg_parse, GstBuffer * buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  guint8 id;

  guint16 packet_length;
  guint8 header_data_length = 0;

  guint16 datalen;
  guint16 headerlen;
  GstClockTime timestamp;

  GstFlowReturn ret = GST_FLOW_OK;

  GstMPEGStream *outstream = NULL;
  guint8 *buf;

  buf = GST_BUFFER_DATA (buffer);
  id = *(buf + 3);
  buf += 4;

  /* start parsing */
  packet_length = GST_READ_UINT16_BE (buf);

  GST_DEBUG_OBJECT (mpeg_demux, "packet_length %d", packet_length);
  buf += 2;

  /* we don't operate on: program_stream_map, padding_stream, */
  /* private_stream_2, ECM, EMM, or program_stream_directory  */
  if ((id != 0xBC) && (id != 0xBE) && (id != 0xBF) && (id != 0xF0) &&
      (id != 0xF1) && (id != 0xFF)) {
    guchar flags1 = *buf++;
    guchar flags2 = *buf++;

    if ((flags1 & 0xC0) != 0x80) {
      return FALSE;
    }

    header_data_length = *buf++;

    GST_DEBUG_OBJECT (mpeg_demux, "header_data_length: %d", header_data_length);

    /* check for PTS */
    if ((flags2 & 0x80)) {
      gint64 pts;

      pts = ((guint64) (*buf++ & 0x0E)) << 29;
      pts |= ((guint64) * buf++) << 22;
      pts |= ((guint64) (*buf++ & 0xFE)) << 14;
      pts |= ((guint64) * buf++) << 7;
      pts |= ((guint64) (*buf++ & 0xFE)) >> 1;

      /* Check for pts overflow */
      if (mpeg_demux->last_pts != -1) {
        gint32 diff = pts - mpeg_demux->last_pts;

        if (diff > -4 * CLOCK_FREQ && diff < 4 * CLOCK_FREQ)
          pts = mpeg_demux->last_pts + diff;
      }
      mpeg_demux->last_pts = pts;

      timestamp = PARSE_CLASS (mpeg_parse)->adjust_ts (mpeg_parse,
          MPEGTIME_TO_GSTTIME (pts));

      GST_DEBUG_OBJECT (mpeg_demux,
          "0x%02x (% " G_GINT64_FORMAT ") PTS = %" G_GUINT64_FORMAT, id, pts,
          MPEGTIME_TO_GSTTIME (pts));
    } else {
      timestamp = GST_CLOCK_TIME_NONE;
    }

    if ((flags2 & 0x40)) {
      GST_DEBUG_OBJECT (mpeg_demux, "%x DTS found", id);
      buf += 5;
    }

    if ((flags2 & 0x20)) {
      GST_DEBUG_OBJECT (mpeg_demux, "%x ESCR found", id);
      buf += 6;
    }

    if ((flags2 & 0x10)) {
      guint32 es_rate;

      es_rate = ((guint32) (*buf++ & 0x07)) << 14;
      es_rate |= ((guint32) (*buf++)) << 7;
      es_rate |= ((guint32) (*buf++ & 0xFE)) >> 1;
      GST_DEBUG_OBJECT (mpeg_demux, "%x ES Rate found", id);
    }
    /* FIXME: lots of PES parsing missing here... */

    /* calculate the amount of real data in this PES packet */
    /* constant is 2 bytes packet_length, 2 bytes of bits, 1 byte header len */
    headerlen = 5 + header_data_length;
    /* constant is 2 bytes of bits, 1 byte header len */
    datalen = packet_length - (3 + header_data_length);
  } else {
    /* Deliver the whole packet. */
    /* constant corresponds to the 2 bytes of the packet length. */
    headerlen = 2;
    datalen = packet_length;

    timestamp = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG_OBJECT (mpeg_demux, "headerlen is %d, datalen is %d",
      headerlen, datalen);

  if (id == 0xBD) {
    /* Private stream 1. */
    GST_DEBUG_OBJECT (mpeg_demux, "we have a private 1 packet");
    ret = CLASS (mpeg_demux)->process_private (mpeg_demux, buffer, 0,
        timestamp, headerlen, datalen);
  } else if (id == 0xBF) {
    /* Private stream 2. */
    GST_DEBUG_OBJECT (mpeg_demux, "we have a private 2 packet");
    ret = CLASS (mpeg_demux)->process_private (mpeg_demux, buffer, 1,
        timestamp, headerlen, datalen);
  } else if (id >= 0xC0 && id <= 0xDF) {
    /* Audio. */
    GST_DEBUG_OBJECT (mpeg_demux, "we have an audio packet");
    outstream = CLASS (mpeg_demux)->get_audio_stream (mpeg_demux,
        id - 0xC0, GST_MPEG_DEMUX_AUDIO_MPEG, NULL);
    ret = CLASS (mpeg_demux)->send_subbuffer (mpeg_demux, outstream, buffer,
        timestamp, headerlen + 4, datalen);
  } else if (id >= 0xE0 && id <= 0xEF) {
    /* Video. */
    gint mpeg_version = !GST_MPEG_PARSE_IS_MPEG2 (mpeg_demux) ? 1 : 2;

    GST_DEBUG_OBJECT (mpeg_demux, "we have a video packet");

    outstream = CLASS (mpeg_demux)->get_video_stream (mpeg_demux,
        id - 0xE0, GST_MPEG_DEMUX_VIDEO_MPEG, &mpeg_version);
    ret = CLASS (mpeg_demux)->send_subbuffer (mpeg_demux, outstream, buffer,
        timestamp, headerlen + 4, datalen);
  } else if (id != 0xBE /* Ignore padding stream */ ) {
    GST_WARNING_OBJECT (mpeg_demux, "unknown stream id 0x%02x", id);
  }

  return ret;
}

/* random magic value */
#define MIN_BUFS_FOR_NO_MORE_PADS 100

static GstFlowReturn
gst_mpeg_demux_combine_flows (GstMPEGDemux * demux, GstMPEGStream * stream,
    GstFlowReturn flow)
{
  gint i;

  /* store the value */
  stream->last_flow = flow;

  /* if it's success we can return the value right away */
  if (flow == GST_FLOW_OK)
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (flow != GST_FLOW_NOT_LINKED) {
    GST_DEBUG_OBJECT (demux, "flow %s on pad %" GST_PTR_FORMAT,
        gst_flow_get_name (flow), stream->pad);
    goto done;
  }

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (i = 0; i < GST_MPEG_DEMUX_NUM_VIDEO_STREAMS; i++) {
    if (demux->video_stream[i] != NULL) {
      flow = demux->video_stream[i]->last_flow;
      /* some other return value (must be SUCCESS but we can return
       * other values as well) */
      if (flow != GST_FLOW_NOT_LINKED)
        goto done;
      if (demux->video_stream[i]->buffers_sent < MIN_BUFS_FOR_NO_MORE_PADS) {
        flow = GST_FLOW_OK;
        goto done;
      }
    }
  }
  for (i = 0; i < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS; i++) {
    if (demux->audio_stream[i] != NULL) {
      flow = demux->audio_stream[i]->last_flow;
      /* some other return value (must be SUCCESS but we can return
       * other values as well) */
      if (flow != GST_FLOW_NOT_LINKED)
        goto done;
      if (demux->audio_stream[i]->buffers_sent < MIN_BUFS_FOR_NO_MORE_PADS) {
        flow = GST_FLOW_OK;
        goto done;
      }
    }
  }
  for (i = 0; i < GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS; i++) {
    if (demux->private_stream[i] != NULL) {
      flow = demux->private_stream[i]->last_flow;
      /* some other return value (must be SUCCESS but we can return
       * other values as well) */
      if (flow != GST_FLOW_NOT_LINKED)
        goto done;
      if (demux->private_stream[i]->buffers_sent < MIN_BUFS_FOR_NO_MORE_PADS) {
        flow = GST_FLOW_OK;
        goto done;
      }
    }
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
  GST_DEBUG_OBJECT (demux, "all pads combined have not-linked flow");

done:
  return flow;
}

static GstFlowReturn
gst_mpeg_demux_send_subbuffer (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * outstream, GstBuffer * buffer,
    GstClockTime timestamp, guint offset, guint size)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (mpeg_demux);
  GstFlowReturn ret;
  GstBuffer *outbuf;

  if (timestamp != GST_CLOCK_TIME_NONE) {
    outstream->cur_ts = timestamp;
    if (timestamp > mpeg_parse->current_ts)
      outstream->scr_offs = timestamp - mpeg_parse->current_ts;
    else
      outstream->scr_offs = 0;

    if (mpeg_demux->index != NULL) {
      /* Register a new index position.
       * FIXME: check for keyframes
       */
      gst_index_add_association (mpeg_demux->index,
          outstream->index_id, GST_ASSOCIATION_FLAG_DELTA_UNIT,
          GST_FORMAT_BYTES,
          GST_BUFFER_OFFSET (buffer), GST_FORMAT_TIME, timestamp, 0);
    }
  } else if (mpeg_parse->current_ts != GST_CLOCK_TIME_NONE)
    outstream->cur_ts = mpeg_parse->current_ts + outstream->scr_offs;

  if (size == 0)
    return GST_FLOW_OK;

  if (timestamp != GST_CLOCK_TIME_NONE) {
    GST_DEBUG_OBJECT (mpeg_demux, "Creating subbuffer size %d, time=%"
        GST_TIME_FORMAT, size, GST_TIME_ARGS (timestamp));
  } else {
    GST_DEBUG_OBJECT (mpeg_demux, "Creating subbuffer size %d", size);
  }

  if (G_UNLIKELY (offset + size > GST_BUFFER_SIZE (buffer)))
    goto broken_file;

  outbuf = gst_buffer_create_sub (buffer, offset, size);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (outstream->pad));

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buffer) + offset;

  if (GST_CLOCK_TIME_IS_VALID (timestamp) &&
      GST_CLOCK_TIME_IS_VALID (mpeg_parse->current_segment.last_stop)) {
    GstClockTimeDiff diff;
    guint64 update_time;

    update_time = MAX (timestamp, mpeg_parse->current_segment.start);
    diff = GST_CLOCK_DIFF (mpeg_parse->current_segment.last_stop, update_time);
    if (diff > GST_SECOND * 2) {
      GST_DEBUG_OBJECT (mpeg_demux, "Gap of %" GST_TIME_FORMAT " detected in "
          "stream %d. Sending updated NEWSEGMENT events", GST_TIME_ARGS (diff),
          outstream->number);
      PARSE_CLASS (mpeg_parse)->send_event (mpeg_parse,
          gst_event_new_new_segment (TRUE, mpeg_parse->current_segment.rate,
              GST_FORMAT_TIME, mpeg_parse->current_segment.last_stop,
              mpeg_parse->current_segment.last_stop,
              mpeg_parse->current_segment.last_stop));
      gst_segment_set_newsegment (&mpeg_parse->current_segment,
          FALSE, mpeg_parse->current_segment.rate, GST_FORMAT_TIME,
          update_time, mpeg_parse->current_segment.stop, update_time);
      PARSE_CLASS (mpeg_parse)->send_event (mpeg_parse,
          gst_event_new_new_segment (FALSE, mpeg_parse->current_segment.rate,
              GST_FORMAT_TIME, update_time,
              mpeg_parse->current_segment.stop, update_time));
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    }
    gst_segment_set_last_stop (&mpeg_parse->current_segment,
        GST_FORMAT_TIME, update_time);
  }

  ret = gst_pad_push (outstream->pad, outbuf);
  GST_LOG_OBJECT (outstream->pad, "flow: %s", gst_flow_get_name (ret));
  ++outstream->buffers_sent;

  GST_LOG_OBJECT (mpeg_demux, "current: %" GST_TIME_FORMAT
      ", gap %" GST_TIME_FORMAT ", tol: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (mpeg_parse->current_ts),
      GST_TIME_ARGS (mpeg_demux->max_gap),
      GST_TIME_ARGS (mpeg_demux->max_gap_tolerance));
  if (GST_CLOCK_TIME_IS_VALID (mpeg_demux->max_gap) &&
      GST_CLOCK_TIME_IS_VALID (mpeg_parse->current_ts) &&
      (mpeg_parse->current_ts > mpeg_demux->max_gap)) {
    CLASS (mpeg_demux)->synchronise_pads (mpeg_demux,
        mpeg_parse->current_ts - mpeg_demux->max_gap,
        mpeg_parse->current_ts - mpeg_demux->max_gap_tolerance);
  }

  ret = CLASS (mpeg_demux)->combine_flows (mpeg_demux, outstream, ret);

  return ret;

/* ERRORS */
broken_file:
  {
    GST_ELEMENT_ERROR (mpeg_demux, STREAM, DEMUX, (NULL),
        ("Either broken file or not an MPEG stream"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_mpeg_demux_process_private (GstMPEGDemux * mpeg_demux,
    GstBuffer * buffer,
    guint stream_nr, GstClockTime timestamp, guint headerlen, guint datalen)
{
  GstMPEGStream *outstream;
  GstFlowReturn ret;

  outstream = CLASS (mpeg_demux)->get_private_stream (mpeg_demux,
      stream_nr, GST_MPEG_DEMUX_PRIVATE_UNKNOWN, NULL);
  ret = CLASS (mpeg_demux)->send_subbuffer (mpeg_demux, outstream, buffer,
      timestamp, headerlen + 4, datalen);
  return ret;
}

static void
gst_mpeg_demux_synchronise_pads (GstMPEGDemux * mpeg_demux,
    GstClockTime threshold, GstClockTime new_ts)
{
  /*
   * Send a new-segment event to any pad with cur_ts < threshold to catch it up
   */
  gint i;

  for (i = 0; i < GST_MPEG_DEMUX_NUM_VIDEO_STREAMS; i++)
    if (mpeg_demux->video_stream[i]
        && mpeg_demux->video_stream[i]->cur_ts < threshold) {
      CLASS (mpeg_demux)->sync_stream_to_time (mpeg_demux,
          mpeg_demux->video_stream[i], new_ts);
      mpeg_demux->video_stream[i]->cur_ts = new_ts;
    }

  for (i = 0; i < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS; i++)
    if (mpeg_demux->audio_stream[i]
        && mpeg_demux->audio_stream[i]->cur_ts < threshold) {
      CLASS (mpeg_demux)->sync_stream_to_time (mpeg_demux,
          mpeg_demux->audio_stream[i], new_ts);
      mpeg_demux->audio_stream[i]->cur_ts = new_ts;
    }

  for (i = 0; i < GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS; i++)
    if (mpeg_demux->private_stream[i]
        && mpeg_demux->private_stream[i]->cur_ts < threshold) {
      CLASS (mpeg_demux)->sync_stream_to_time (mpeg_demux,
          mpeg_demux->private_stream[i], new_ts);
      mpeg_demux->private_stream[i]->cur_ts = new_ts;
    }
}

/*
 * Send a new-segment event on the indicated pad to catch it up to last_ts.
 */
static void
gst_mpeg_demux_sync_stream_to_time (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * stream, GstClockTime last_ts)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (mpeg_demux);
  guint64 update_time;

  update_time =
      MIN ((guint64) last_ts, (guint64) mpeg_parse->current_segment.stop);
  gst_pad_push_event (stream->pad, gst_event_new_new_segment (TRUE,
          mpeg_parse->current_segment.rate, GST_FORMAT_TIME,
          update_time, mpeg_parse->current_segment.stop, update_time));
}

#if 0
const GstFormat *
gst_mpeg_demux_get_src_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,            /* we prefer seeking on time */
    GST_FORMAT_BYTES,
    0
  };

  return formats;
}

static gboolean
index_seek (GstPad * pad, GstEvent * event, gint64 * offset)
{
  GstIndexEntry *entry;
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));
  GstMPEGStream *stream = gst_pad_get_element_private (pad);

  entry = gst_index_get_assoc_entry (mpeg_demux->index, stream->index_id,
      GST_INDEX_LOOKUP_BEFORE, 0,
      GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event));
  if (!entry) {
    GST_CAT_WARNING (GST_CAT_SEEK, "%s:%s index %s %" G_GINT64_FORMAT
        " -> failed",
        GST_DEBUG_PAD_NAME (pad),
        gst_format_get_details (GST_EVENT_SEEK_FORMAT (event))->nick,
        GST_EVENT_SEEK_OFFSET (event));
    return FALSE;
  }

  if (gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, offset)) {
    GST_CAT_DEBUG (GST_CAT_SEEK, "%s:%s index %s %" G_GINT64_FORMAT
        " -> %" G_GINT64_FORMAT " bytes",
        GST_DEBUG_PAD_NAME (pad),
        gst_format_get_details (GST_EVENT_SEEK_FORMAT (event))->nick,
        GST_EVENT_SEEK_OFFSET (event), *offset);
    return TRUE;
  }
  return FALSE;
}

static gboolean
normal_seek (GstPad * pad, GstEvent * event, gint64 * offset)
{
  gboolean res = FALSE;
  gint64 adjust;
  GstFormat format;
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));

  format = GST_EVENT_SEEK_FORMAT (event);

  res = gst_pad_convert (pad, GST_FORMAT_BYTES, mpeg_demux->total_size_bound,
      &format, &adjust);

  if (res) {
    *offset = MAX (GST_EVENT_SEEK_OFFSET (event) - adjust, 0);

    GST_CAT_DEBUG (GST_CAT_SEEK, "%s:%s guesstimate %" G_GINT64_FORMAT
        " %s -> %" G_GINT64_FORMAT
        " (total_size_bound = %" G_GINT64_FORMAT ")",
        GST_DEBUG_PAD_NAME (pad),
        GST_EVENT_SEEK_OFFSET (event),
        gst_format_get_details (GST_EVENT_SEEK_FORMAT (event))->nick,
        *offset, mpeg_demux->total_size_bound);
  }

  return res;
}

static gboolean
gst_mpeg_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 desired_offset;

      if (mpeg_demux->index)
        res = index_seek (pad, event, &desired_offset);
      if (!res)
        res = normal_seek (pad, event, &desired_offset);

      if (res) {
        GstEvent *new_event;

        new_event =
            gst_event_new_seek (GST_EVENT_SEEK_TYPE (event), desired_offset);
        res = gst_mpeg_parse_handle_src_event (pad, new_event);
      }
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_NAVIGATION:
    {
      res = gst_pad_push_event (GST_MPEG_PARSE (mpeg_demux)->sinkpad, event);
      break;
    }
    default:
      gst_event_unref (event);
      break;
  }
  return res;
}

static gboolean
gst_mpeg_demux_handle_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res;

  res = gst_mpeg_parse_handle_src_query (pad, type, format, value);

  if (res && (type == GST_QUERY_POSITION) && (format)
      && (*format == GST_FORMAT_TIME)) {
    GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));

    *value += mpeg_demux->adjust;
  }

  return res;
}
#endif

static void
gst_mpeg_demux_reset (GstMPEGDemux * mpeg_demux)
{
  int i;

  /* Reset the element */

  GST_INFO ("Resetting the MPEG Demuxer");

  /* free the streams , remove the pads */
  /* filled in init_stream */
  /* check get_audio/video_stream because it can be derivated */
  for (i = 0; i < GST_MPEG_DEMUX_NUM_VIDEO_STREAMS; i++)
    if (mpeg_demux->video_stream[i]) {
      gst_pad_push_event (mpeg_demux->video_stream[i]->pad,
          gst_event_new_eos ());
      gst_element_remove_pad (GST_ELEMENT (mpeg_demux),
          mpeg_demux->video_stream[i]->pad);
      if (mpeg_demux->video_stream[i]->caps)
        gst_caps_unref (mpeg_demux->video_stream[i]->caps);
      g_free (mpeg_demux->video_stream[i]);
      mpeg_demux->video_stream[i] = NULL;
    }
  for (i = 0; i < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS; i++)
    if (mpeg_demux->audio_stream[i]) {
      gst_pad_push_event (mpeg_demux->audio_stream[i]->pad,
          gst_event_new_eos ());
      gst_element_remove_pad (GST_ELEMENT (mpeg_demux),
          mpeg_demux->audio_stream[i]->pad);
      if (mpeg_demux->audio_stream[i]->tags)
        gst_tag_list_free (mpeg_demux->audio_stream[i]->tags);
      if (mpeg_demux->audio_stream[i]->caps)
        gst_caps_unref (mpeg_demux->audio_stream[i]->caps);
      g_free (mpeg_demux->audio_stream[i]);
      mpeg_demux->audio_stream[i] = NULL;
    }
  for (i = 0; i < GST_MPEG_DEMUX_NUM_PRIVATE_STREAMS; i++)
    if (mpeg_demux->private_stream[i]) {
      gst_pad_push_event (mpeg_demux->private_stream[i]->pad,
          gst_event_new_eos ());
      gst_element_remove_pad (GST_ELEMENT (mpeg_demux),
          mpeg_demux->private_stream[i]->pad);
      if (mpeg_demux->private_stream[i]->caps)
        gst_caps_unref (mpeg_demux->private_stream[i]->caps);
      g_free (mpeg_demux->private_stream[i]);
      mpeg_demux->private_stream[i] = NULL;
    }

  mpeg_demux->in_flush = FALSE;
  mpeg_demux->header_length = 0;
  mpeg_demux->rate_bound = 0;
  mpeg_demux->audio_bound = 0;
  mpeg_demux->video_bound = 0;
  mpeg_demux->fixed = FALSE;
  mpeg_demux->constrained = FALSE;
  mpeg_demux->audio_lock = FALSE;
  mpeg_demux->video_lock = FALSE;

  mpeg_demux->packet_rate_restriction = FALSE;
  mpeg_demux->total_size_bound = G_GINT64_CONSTANT (0);

  mpeg_demux->index = NULL;
  mpeg_demux->last_pts = -1;
  mpeg_demux->pending_tags = FALSE;

  /*
   * Don't adjust things that are only for subclass use
   * - if they changed it, they can reset it.
   *
   * mpeg_demux->adjust = 0;
   * mpeg_demux->max_gap = GST_CLOCK_TIME_NONE;
   * mpeg_demux->max_gap_tolerance = GST_CLOCK_TIME_NONE;
   */
}

static GstStateChangeReturn
gst_mpeg_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mpeg_demux_reset (mpeg_demux);
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_mpeg_demux_set_index (GstElement * element, GstIndex * index)
{
  GstMPEGDemux *mpeg_demux;

  GST_ELEMENT_CLASS (parent_class)->set_index (element, index);

  mpeg_demux = GST_MPEG_DEMUX (element);

  mpeg_demux->index = index;
}

static GstIndex *
gst_mpeg_demux_get_index (GstElement * element)
{
  GstMPEGDemux *mpeg_demux;

  mpeg_demux = GST_MPEG_DEMUX (element);

  return mpeg_demux->index;
}

void
gst_mpeg_streams_reset_last_flow (GstMPEGStream * streams[], guint num)
{
  guint i;

  for (i = 0; i < num; ++i) {
    if (streams[i] != NULL)
      streams[i]->last_flow = GST_FLOW_OK;
  }
}

void
gst_mpeg_streams_reset_cur_ts (GstMPEGStream * streams[], guint num,
    GstClockTime cur_ts)
{
  guint i;

  for (i = 0; i < num; ++i) {
    if (streams[i] != NULL)
      streams[i]->cur_ts = cur_ts;
  }
}

gboolean
gst_mpeg_demux_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mpegdemux",
      GST_RANK_SECONDARY, GST_TYPE_MPEG_DEMUX);
}
