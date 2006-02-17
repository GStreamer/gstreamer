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

#include <gst/gstutils.h>
#include <gst/riff/riff-media.h>
#include <string.h>

#include "gstasfdemux.h"
#include "asfheaders.h"

static GstStaticPadTemplate gst_asf_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );


/* abuse this GstFlowReturn enum for internal usage */
#define ASF_FLOW_NEED_MORE_DATA  99

#define gst_asf_get_flow_name(flow)    \
  (flow == ASF_FLOW_NEED_MORE_DATA) ?  \
  "need-more-data" : gst_flow_get_name (flow)

GST_DEBUG_CATEGORY_STATIC (asf_debug);
#define GST_CAT_DEFAULT asf_debug

static GstStateChangeReturn gst_asf_demux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_asf_demux_element_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_asf_demux_send_event_unlocked (GstASFDemux * demux,
    GstEvent * event);
static gboolean gst_asf_demux_handle_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_asf_demux_get_src_query_types (GstPad * pad);
static GstFlowReturn gst_asf_demux_parse_data (GstASFDemux * demux);
static GstFlowReturn gst_asf_demux_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_asf_demux_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_asf_demux_process_object (GstASFDemux * demux,
    guint8 ** p_data, guint64 * p_size);

GST_BOILERPLATE (GstASFDemux, gst_asf_demux, GstElement, GST_TYPE_ELEMENT)

     static GstPadTemplate *videosrctempl;
     static GstPadTemplate *audiosrctempl;

     static void gst_asf_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_asf_demux_details = {
    "ASF Demuxer",
    "Codec/Demuxer",
    "Demultiplexes ASF Streams",
    "Owen Fraser-Green <owen@discobabe.net>"
  };
  GstCaps *audcaps = gst_riff_create_audio_template_caps (),
      *vidcaps = gst_riff_create_video_template_caps ();

  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, audcaps);
  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, vidcaps);

  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asf_demux_sink_template));

  gst_element_class_set_details (element_class, &gst_asf_demux_details);

  GST_DEBUG_CATEGORY_INIT (asf_debug, "asfdemux", 0, "asf demuxer element");
}

static void
gst_asf_demux_class_init (GstASFDemuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_asf_demux_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_asf_demux_element_send_event);
}

static void
gst_asf_demux_init (GstASFDemux * demux, GstASFDemuxClass * klass)
{
  demux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_asf_demux_sink_template), "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* We should zero everything to be on the safe side */
  demux->num_audio_streams = 0;
  demux->num_video_streams = 0;
  demux->num_streams = 0;

  demux->taglist = NULL;
  demux->state = GST_ASF_DEMUX_STATE_HEADER;
}

static gboolean
gst_asf_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstASFDemux *demux;
  gboolean ret = TRUE;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat newsegment_format;
      gint64 newsegment_start;
      guint n;

      gst_event_parse_new_segment (event, NULL, NULL, &newsegment_format,
          &newsegment_start, NULL, NULL);

      g_assert (newsegment_format == GST_FORMAT_BYTES);
      g_assert (newsegment_start >= 0);

      GST_OBJECT_LOCK (demux);
      demux->pts = 0;
      demux->bytes_needed = 0;
      demux->next_byte_offset = newsegment_start;
      gst_adapter_clear (demux->adapter);

      for (n = 0; n < demux->num_streams; n++) {
        if (demux->stream[n].frag_offset > 0) {
          gst_buffer_unref (demux->stream[n].payload);
          demux->stream[n].frag_offset = 0;
        }
        if (demux->stream[n].cache) {
          gst_buffer_unref (demux->stream[n].cache);
        }
        demux->stream[n].need_newsegment = TRUE;
        demux->stream[n].last_pts = GST_CLOCK_TIME_NONE;
        demux->stream[n].sequence = 0;
      }

      GST_OBJECT_UNLOCK (demux);
      break;
    }

    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:{
#if 0
      /* just drop these events, we
       * send our own when seeking */
      gst_event_unref (event);
#endif
      ret = gst_pad_event_default (pad, event);
      break;
    }

    case GST_EVENT_EOS:{
      GST_OBJECT_LOCK (demux);
      gst_adapter_clear (demux->adapter);
      demux->bytes_needed = 0;
      gst_asf_demux_send_event_unlocked (demux, event);
      GST_OBJECT_UNLOCK (demux);
      break;
    }

    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (demux);
  return ret;
}

static gboolean
gst_asf_demux_handle_seek_event (GstASFDemux * demux, GstEvent * event)
{
  GstSegment segment;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  gboolean only_need_update;
  gboolean keyunit_sync;
  gboolean accurate;
  gboolean flush;
  gboolean ret = FALSE;
  gdouble rate;
  gint64 cur, stop;
  gint64 seek_offset;
  guint64 seek_packet;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    GST_LOG ("seeking is only supported in TIME format");
    return FALSE;
  }

  if (rate <= 0.0) {
    GST_LOG ("backward playback is not supported yet");
    return FALSE;
  }

  /* FIXME: this seeking code is very very broken. Do not copy
   * it under any circumstances, unless you want to make Wim cry */

  flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);
  accurate = ((flags & GST_SEEK_FLAG_ACCURATE) == GST_SEEK_FLAG_ACCURATE);
  keyunit_sync = ((flags & GST_SEEK_FLAG_KEY_UNIT) == GST_SEEK_FLAG_KEY_UNIT);

  /* operating on copy of segment until we know the seek worked */
  GST_OBJECT_LOCK (demux);
  segment = demux->segment;
  GST_OBJECT_UNLOCK (demux);

  gst_segment_set_seek (&segment, rate, format, flags, cur_type,
      cur, stop_type, stop, &only_need_update);

  GST_DEBUG ("trying to seek to time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (segment.start));

  if (demux->packet_size > 0) {
    gint64 seek_time = segment.start;

    /* Hackety hack, this sucks. We just seek to an earlier position
     *  and let the sinks throw away the stuff before the segment start */
    if (flush && (accurate || keyunit_sync)) {
      seek_time -= 5 * GST_SECOND;
      if (seek_time < 0)
        seek_time = 0;
    }

    seek_packet = demux->num_packets * seek_time / demux->play_time;

    if (seek_packet > demux->num_packets)
      seek_packet = demux->num_packets;

    seek_offset = seek_packet * demux->packet_size + demux->data_offset;
    /* demux->next_byte_offset will be set via newsegment event */
  } else {
    /* FIXME */
    g_message ("IMPLEMENT ME: seeking for packet_size == 0 (asfdemux)");
    ret = FALSE;
    goto done;
  }

  GST_LOG ("seeking to byte offset %" G_GINT64_FORMAT, seek_offset);

  ret = gst_pad_push_event (demux->sinkpad,
      gst_event_new_seek (1.0, GST_FORMAT_BYTES,
          flags | GST_SEEK_FLAG_ACCURATE,
          GST_SEEK_TYPE_SET, seek_offset, GST_SEEK_TYPE_NONE, -1));

  if (ret == FALSE) {
    GST_WARNING ("upstream element failed to seek!");
    goto done;
  }

  GST_OBJECT_LOCK (demux);
  demux->segment = segment;
  demux->packet = seek_packet;
  GST_OBJECT_UNLOCK (demux);

done:

  return ret;
}

static gboolean
gst_asf_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstASFDemux *demux;
  gboolean ret;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG ("handling %s event on source pad %s",
      GST_EVENT_TYPE_NAME (event), GST_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      ret = gst_asf_demux_handle_seek_event (demux, event);
      gst_event_unref (event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (demux);
  return ret;
}

static gint64
gst_asf_demux_get_current_offset (GstASFDemux * demux, guint8 * cur_data)
{
  guint64 ret;

  if (demux->next_byte_offset == GST_BUFFER_OFFSET_NONE)
    return GST_BUFFER_OFFSET_NONE;

  ret = demux->next_byte_offset - gst_adapter_available (demux->adapter);

  if (cur_data) {
    guint8 *start = (guint8 *) gst_adapter_peek (demux->adapter, 1);

    g_assert (cur_data > start);
    ret += cur_data - start;
  }

  return ret;
}

static GstFlowReturn
gst_asf_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstASFDemux *demux;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  /* GST_DEBUG ("====================== chain ================="); */

  GST_DEBUG ("received buffer: size=%u, offset=%" G_GINT64_FORMAT,
      GST_BUFFER_SIZE (buf), GST_BUFFER_OFFSET (buf));

  /* So we can always calculate the current byte offset ... */
  if (GST_BUFFER_OFFSET (buf) != GST_BUFFER_OFFSET_NONE)
    demux->next_byte_offset = GST_BUFFER_OFFSET (buf) + GST_BUFFER_SIZE (buf);
  else
    demux->next_byte_offset = GST_BUFFER_OFFSET_NONE;

  gst_adapter_push (demux->adapter, buf);
  buf = NULL;                   /* adapter took ownership */

  /* If we know the minimum number of bytes required
   * to do further processing from last time, check here
   * and save us some unnecessary repeated parsing */
  if (demux->bytes_needed > 0) {
    guint avail;

    avail = gst_adapter_available (demux->adapter);

    GST_DEBUG ("bytes_needed=%u, available=%u", demux->bytes_needed, avail);

    if (avail < demux->bytes_needed)
      goto done;
  }

  demux->bytes_needed = 0;

  /* Parse until we need more data, get an error, or are done */
  do {
    GST_DEBUG ("current offset = %" G_GINT64_FORMAT,
        gst_asf_demux_get_current_offset (demux, NULL));

    ret = gst_asf_demux_parse_data (demux);
  } while (ret == GST_FLOW_OK);

  if (ret == ASF_FLOW_NEED_MORE_DATA) {
    GST_DEBUG ("waiting for more data, %u bytes needed and only %u available",
        demux->bytes_needed, gst_adapter_available (demux->adapter));
    ret = GST_FLOW_OK;
    goto done;
  }

  GST_DEBUG ("parse_data returned %s", gst_flow_get_name (ret));

done:
  gst_object_unref (demux);
  g_assert (ret != ASF_FLOW_NEED_MORE_DATA);    /* internal only */
  return ret;
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

static inline guint32
gst_asf_demux_identify_guid (GstASFDemux * demux,
    const ASFGuidHash * guids, ASFGuid * guid)
{
  guint32 ret;

  GST_LOG ("identifying 0x%08x-0x%08x-0x%08x-0x%08x",
      guid->v1, guid->v2, guid->v3, guid->v4);

  ret = gst_asf_identify_guid (guids, guid);

  GST_LOG ("identified as %s", gst_asf_get_guid_nick (guids, ret));

  return ret;
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

static inline guint32
gst_asf_demux_get_var_length (guint8 type, guint8 ** p_data, guint64 * p_size)
{
  switch (type) {
    case 0:
      return 0;

    case 1:
      g_assert (*p_size >= 1);
      return gst_asf_demux_get_uint8 (p_data, p_size);

    case 2:
      g_assert (*p_size >= 2);
      return gst_asf_demux_get_uint16 (p_data, p_size);

    case 3:
      g_assert (*p_size >= 4);
      return gst_asf_demux_get_uint32 (p_data, p_size);

    default:
      break;
  }

  g_assert_not_reached ();
}

static gboolean
gst_asf_demux_get_buffer (GstBuffer ** p_buf, guint num_bytes_to_read,
    guint8 ** p_data, guint64 * p_size)
{
  *p_buf = NULL;

  if (*p_size < num_bytes_to_read)
    return FALSE;

  *p_buf = gst_buffer_new_and_alloc (num_bytes_to_read);
  memcpy (GST_BUFFER_DATA (*p_buf), *p_data, num_bytes_to_read);
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

  if (*p_strlen)
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


static gboolean
gst_asf_demux_get_guid (ASFGuid * guid, guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < 4 * sizeof (guint32))
    return FALSE;

  guid->v1 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v2 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v3 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v4 = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_file (asf_obj_file * object, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (16 + 8 + 8 + 8 + 8 + 8 + 8 + 4 + 4 + 4 + 4))
    return FALSE;

  gst_asf_demux_get_guid (&object->file_id, p_data, p_size);
  object->file_size = gst_asf_demux_get_uint64 (p_data, p_size);
  object->creation_time = gst_asf_demux_get_uint64 (p_data, p_size);
  object->packets_count = gst_asf_demux_get_uint64 (p_data, p_size);
  object->play_time = gst_asf_demux_get_uint64 (p_data, p_size);
  object->send_time = gst_asf_demux_get_uint64 (p_data, p_size);
  object->preroll = gst_asf_demux_get_uint64 (p_data, p_size);
  object->flags = gst_asf_demux_get_uint32 (p_data, p_size);
  object->min_pktsize = gst_asf_demux_get_uint32 (p_data, p_size);
  object->max_pktsize = gst_asf_demux_get_uint32 (p_data, p_size);
  object->min_bitrate = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_bitrate_record (asf_bitrate_record * record,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (2 + 4))
    return FALSE;

  record->stream_id = gst_asf_demux_get_uint16 (p_data, p_size);
  record->bitrate = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_comment (asf_obj_comment * comment, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (2 + 2 + 2 + 2 + 2))
    return FALSE;

  comment->title_length = gst_asf_demux_get_uint16 (p_data, p_size);
  comment->author_length = gst_asf_demux_get_uint16 (p_data, p_size);
  comment->copyright_length = gst_asf_demux_get_uint16 (p_data, p_size);
  comment->description_length = gst_asf_demux_get_uint16 (p_data, p_size);
  comment->rating_length = gst_asf_demux_get_uint16 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_header (asf_obj_header * header, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (4 + 1 + 1))
    return FALSE;

  header->num_objects = gst_asf_demux_get_uint32 (p_data, p_size);
  header->unknown1 = gst_asf_demux_get_uint8 (p_data, p_size);
  header->unknown2 = gst_asf_demux_get_uint8 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_header_ext (asf_obj_header_ext * hdr_ext,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (16 + 2 + 4))
    return FALSE;

  gst_asf_demux_get_guid (&hdr_ext->reserved1, p_data, p_size);
  hdr_ext->reserved2 = gst_asf_demux_get_uint16 (p_data, p_size);
  hdr_ext->data_size = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_stream (asf_obj_stream * stream, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (16 + 16 + 8 + 4 + 4 + 2 + 4))
    return FALSE;

  gst_asf_demux_get_guid (&stream->type, p_data, p_size);
  gst_asf_demux_get_guid (&stream->correction, p_data, p_size);

  stream->unknown1 = gst_asf_demux_get_uint64 (p_data, p_size);
  stream->type_specific_size = gst_asf_demux_get_uint32 (p_data, p_size);
  stream->stream_specific_size = gst_asf_demux_get_uint32 (p_data, p_size);
  stream->id = gst_asf_demux_get_uint16 (p_data, p_size);
  stream->unknown2 = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_replicated_data (asf_replicated_data * rep, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (4 + 4))
    return FALSE;

  rep->object_size = gst_asf_demux_get_uint32 (p_data, p_size);
  rep->frag_timestamp = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_data (asf_obj_data * object, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (16 + 8 + 1 + 1))
    return FALSE;

  gst_asf_demux_get_guid (&object->file_id, p_data, p_size);
  object->packets = gst_asf_demux_get_uint64 (p_data, p_size);
  object->unknown1 = gst_asf_demux_get_uint8 (p_data, p_size);
  /* object->unknown2 = gst_asf_demux_get_uint8 (p_data, p_size); */
  object->correction = gst_asf_demux_get_uint8 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_obj_data_correction (asf_obj_data_correction * object,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (1 + 1))
    return FALSE;

  object->type = gst_asf_demux_get_uint8 (p_data, p_size);
  object->cycle = gst_asf_demux_get_uint8 (p_data, p_size);
  return TRUE;
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
gst_asf_demux_get_stream_correction (asf_stream_correction * object,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (1 + 2 + 2 + 2 + 1))
    return FALSE;

  object->span = gst_asf_demux_get_uint8 (p_data, p_size);
  object->packet_size = gst_asf_demux_get_uint16 (p_data, p_size);
  object->chunk_size = gst_asf_demux_get_uint16 (p_data, p_size);
  object->data_size = gst_asf_demux_get_uint16 (p_data, p_size);
  object->silence_data = gst_asf_demux_get_uint8 (p_data, p_size);
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

static asf_stream_context *
gst_asf_demux_get_stream (GstASFDemux * demux, guint16 id)
{
  guint8 i;
  asf_stream_context *stream;

  for (i = 0; i < demux->num_streams; i++) {
    stream = &demux->stream[i];
    if (stream->id == id) {
      /* We've found the one with the matching id */
      return &demux->stream[i];
    }
  }

  /* Base case if we haven't found one at all */
  GST_WARNING ("Segment found for undefined stream: (%d)", id);

  return NULL;
}

static void
gst_asf_demux_setup_pad (GstASFDemux * demux, GstPad * src_pad,
    GstCaps * caps, guint16 id, gboolean is_video)
{
  asf_stream_context *stream;

  gst_pad_use_fixed_caps (src_pad);
  gst_pad_set_caps (src_pad, caps);

  gst_pad_set_event_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_handle_src_event));
  gst_pad_set_query_type_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_get_src_query_types));
  gst_pad_set_query_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_handle_src_query));

  stream = &demux->stream[demux->num_streams];
  stream->caps = caps;
  stream->pad = src_pad;
  stream->id = id;
  stream->frag_offset = 0;
  stream->sequence = 0;
  stream->delay = 0;
  stream->last_pts = GST_CLOCK_TIME_NONE;
  stream->fps_known = !is_video;        /* bit hacky for audio */
  stream->is_video = is_video;
  stream->need_newsegment = TRUE;

  gst_pad_set_element_private (src_pad, stream);

  GST_INFO ("Adding pad %s for stream %u with caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (src_pad), demux->num_streams, caps);

  ++demux->num_streams;

  gst_element_add_pad (GST_ELEMENT (demux), src_pad);

  /* FIXME */
#if 0
  if (demux->taglist) {
    /* ... push tag event downstream ... */
  }
#endif
}

static void
gst_asf_demux_add_audio_stream (GstASFDemux * demux,
    asf_stream_audio * audio, guint16 id, guint8 ** p_data, guint64 * p_size)
{
  GstTagList *list = gst_tag_list_new ();
  GstBuffer *extradata = NULL;
  GstPad *src_pad;
  GstCaps *caps;
  guint16 size_left = 0;
  gchar *codec_name = NULL;
  gchar *name = NULL;

  size_left = audio->size;

  /* Create the audio pad */
  name = g_strdup_printf ("audio_%02d", demux->num_audio_streams);

  src_pad = gst_pad_new_from_template (audiosrctempl, name);
  g_free (name);

  /* Swallow up any left over data and set up the 
   * standard properties from the header info */
  if (size_left) {
    GST_WARNING ("Audio header contains %d bytes of "
        "codec specific data", size_left);

    gst_asf_demux_get_buffer (&extradata, size_left, p_data, p_size);
  }

  /* asf_stream_audio is the same as gst_riff_strf_auds, but with an
   * additional two bytes indicating extradata. */
  caps = gst_riff_create_audio_caps (audio->codec_tag, NULL,
      (gst_riff_strf_auds *) audio, extradata, NULL, &codec_name);

  /* Informing about that audio format we just added */
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, GST_TAG_AUDIO_CODEC,
      codec_name, NULL);
  g_free (codec_name);

  if (extradata)
    gst_buffer_unref (extradata);

  GST_INFO ("Adding audio stream %u codec %u (0x%x)",
      demux->num_video_streams, audio->codec_tag, audio->codec_tag);

  ++demux->num_audio_streams;

  gst_asf_demux_setup_pad (demux, src_pad, caps, id, FALSE);

  gst_element_found_tags_for_pad (GST_ELEMENT (demux), src_pad, list);
}

static void
gst_asf_demux_add_video_stream (GstASFDemux * demux,
    asf_stream_video_format * video, guint16 id,
    guint8 ** p_data, guint64 * p_size)
{
  GstTagList *list = gst_tag_list_new ();
  GstBuffer *extradata = NULL;
  GstPad *src_pad;
  GstCaps *caps;
  gchar *name = NULL;
  gchar *codec_name = NULL;
  gint size_left = video->size - 40;

  /* Create the audio pad */
  name = g_strdup_printf ("video_%02d", demux->num_video_streams);
  src_pad = gst_pad_new_from_template (videosrctempl, name);
  g_free (name);

  /* Now try some gstreamer formatted MIME types (from gst_avi_demux_strf_vids) */
  if (size_left) {
    GST_LOG ("Video header has %d bytes of codec specific data", size_left);
    gst_asf_demux_get_buffer (&extradata, size_left, p_data, p_size);
  }

  /* yes, asf_stream_video_format and gst_riff_strf_vids are the same */
  caps = gst_riff_create_video_caps (video->tag, NULL,
      (gst_riff_strf_vids *) video, extradata, NULL, &codec_name);

  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
      codec_name, NULL);
  g_free (codec_name);

  if (extradata)
    gst_buffer_unref (extradata);

  GST_INFO ("Adding video stream %u codec " GST_FOURCC_FORMAT " (0x%08x)",
      demux->num_video_streams, GST_FOURCC_ARGS (video->tag), video->tag);

  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

  ++demux->num_video_streams;

  gst_asf_demux_setup_pad (demux, src_pad, caps, id, TRUE);

  gst_element_found_tags_for_pad (GST_ELEMENT (demux), src_pad, list);
}

static GstFlowReturn
gst_asf_demux_process_stream (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  asf_obj_stream object;
  guint32 stream_id;
  guint32 correction;
  guint8 *obj_data_start = *p_data;

  /* Get the rest of the header's header */
  if (!gst_asf_demux_get_obj_stream (&object, p_data, p_size))
    goto not_enough_data;

  /* Identify the stream type */
  stream_id = gst_asf_demux_identify_guid (demux, asf_stream_guids,
      &object.type);
  correction = gst_asf_demux_identify_guid (demux, asf_correction_guids,
      &object.correction);

  switch (stream_id) {
    case ASF_STREAM_AUDIO:{
      asf_stream_correction correction_object;
      asf_stream_audio audio_object;

      if (!gst_asf_demux_get_stream_audio (&audio_object, p_data, p_size))
        goto not_enough_data;

      GST_INFO ("Object is an audio stream with %u bytes of additional data",
          audio_object.size);

      gst_asf_demux_add_audio_stream (demux, &audio_object, object.id,
          p_data, p_size);

      switch (correction) {
        case ASF_CORRECTION_ON:
          GST_INFO ("Using error correction");

          if (!gst_asf_demux_get_stream_correction (&correction_object,
                  p_data, p_size)) {
            goto not_enough_data;
          }

          demux->span = correction_object.span;

          GST_DEBUG ("Descrambling: ps:%d cs:%d ds:%d s:%d sd:%d",
              correction_object.packet_size, correction_object.chunk_size,
              correction_object.data_size, (guint) correction_object.span,
              (guint) correction_object.silence_data);

          if (demux->span > 1) {
            if (!correction_object.chunk_size
                || ((correction_object.packet_size /
                        correction_object.chunk_size) <= 1)) {
              /* Disable descrambling */
              demux->span = 0;
            } else {
              /* FIXME: this else branch was added for
               * weird_al_yankovic - the saga begins.asf */
              demux->ds_packet_size = correction_object.packet_size;
              demux->ds_chunk_size = correction_object.chunk_size;
            }
          } else {
            /* Descambling is enabled */
            demux->ds_packet_size = correction_object.packet_size;
            demux->ds_chunk_size = correction_object.chunk_size;
          }
#if 0
          /* Now skip the rest of the silence data */
          if (correction_object.data_size > 1)
            gst_bytestream_flush (demux->bs, correction_object.data_size - 1);
#else
          /* FIXME: CHECKME. And why -1? */
          if (correction_object.data_size > 1) {
            if (!gst_asf_demux_skip_bytes (correction_object.data_size - 1,
                    p_data, p_size)) {
              goto not_enough_data;
            }
          }
#endif
          break;
        case ASF_CORRECTION_OFF:
          GST_INFO ("Error correction off");
#if 0
          /* gst_bytestream_flush (demux->bs, object.stream_specific_size); */
#else
          /* FIXME: CHECKME */
          if (!gst_asf_demux_skip_bytes (object.stream_specific_size,
                  p_data, p_size)) {
            goto not_enough_data;
          }
#endif
          break;
        default:
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
              ("Audio stream using unknown error correction"));
          return GST_FLOW_ERROR;
      }

      break;
    }

    case ASF_STREAM_VIDEO:{
      asf_stream_video_format video_format_object;
      asf_stream_video video_object;
      guint16 size;

      if (!gst_asf_demux_get_stream_video (&video_object, p_data, p_size))
        goto not_enough_data;

      size = video_object.size - 40;    /* Byte order gets offset by single byte */

      GST_INFO ("object is a video stream with %u bytes of "
          "additional data", size);

      if (!gst_asf_demux_get_stream_video_format (&video_format_object,
              p_data, p_size)) {
        goto not_enough_data;
      }

      gst_asf_demux_add_video_stream (demux, &video_format_object, object.id,
          p_data, p_size);

      break;
    }

    default:
      GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
          ("Unknown asf stream (id %08x)", (guint) stream_id));
      return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    /* avoid compiler warning when disabling logging at compile time */
    obj_data_start = NULL;

    GST_WARNING ("Unexpected end of data parsing stream object");
    GST_DEBUG ("object data offset: %u, bytes left to parse: %u",
        (guint) (*p_data - obj_data_start), (guint) * p_size);

    return ASF_FLOW_NEED_MORE_DATA;
  }
}

static const gchar *
gst_asf_demux_get_gst_tag_from_tag_name (const gchar * name_utf16le,
    gsize name_len)
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
    "WM/TrackNumber", GST_TAG_TRACK_NUMBER}, {
    "WM/Year", GST_TAG_DATE}
    /* { "WM/Composer", GST_TAG_COMPOSER } */
  };
  gchar *name_utf8;
  gsize in, out;
  guint i;

  /* convert name to UTF-8 */
  name_utf8 = g_convert (name_utf16le, name_len, "UTF-8", "UTF-16LE", &in,
      &out, NULL);

  if (name_utf8 == NULL) {
    GST_WARNING ("Failed to convert name to UTF8, skipping");
    return NULL;
  }

  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (strncmp (tags[i].asf_name, name_utf8, out) == 0) {
      g_free (name_utf8);
      return tags[i].gst_name;
    }
  }

  g_free (name_utf8);
  return NULL;
}

/* gst_asf_demux_commit_taglist() takes ownership of taglist! */
static void
gst_asf_demux_commit_taglist (GstASFDemux * demux, GstTagList * taglist)
{
  GST_DEBUG ("Committing tags: %" GST_PTR_FORMAT, taglist);

  gst_element_found_tags (GST_ELEMENT (demux), gst_tag_list_copy (taglist));

  /* save internally */
  if (!demux->taglist)
    demux->taglist = taglist;
  else {
    GstTagList *t;

    t = gst_tag_list_merge (demux->taglist, taglist, GST_TAG_MERGE_APPEND);
    gst_tag_list_free (demux->taglist);
    gst_tag_list_free (taglist);
    demux->taglist = t;
  }
}

#define ASF_DEMUX_DATA_TYPE_UTF16LE_STRING  0
#define ASF_DEMUX_DATA_TYPE_DWORD           3

/* Extended Content Description Object */
static GstFlowReturn
gst_asf_demux_process_ext_content_desc (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
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
   *
   * Other known (and unused) 'non-text' metadata available :
   *
   *   WM/Track (same as WM/TrackNumber but starts at 0)
   *   WM/EncodingTime
   *   WM/MCDI
   *   IsVBR
   */

  GstTagList *taglist;
  guint16 blockcount, i;
  guint8 *obj_data_start = *p_data;

  GST_INFO ("object is an extended content description");

  taglist = gst_tag_list_new ();

  /* Content Descriptor Count */
  if (*p_size < 2)
    goto not_enough_data;

  blockcount = gst_asf_demux_get_uint16 (p_data, p_size);

  for (i = 1; i <= blockcount; ++i) {
    const gchar *gst_tag_name;
    guint16 datatype;
    guint16 value_len;
    guint16 name_len;
    GValue tag_value = { 0, };
    gsize in, out;
    gchar *name;
    gchar *value;

    /* Descriptor */
    if (!gst_asf_demux_get_string (&name, &name_len, p_data, p_size))
      goto not_enough_data;

    if (*p_size < 2)
      goto not_enough_data;

    /* Descriptor Value Data Type */
    datatype = gst_asf_demux_get_uint16 (p_data, p_size);

    /* Descriptor Value (not really a string, but same thing reading-wise) */
    if (!gst_asf_demux_get_string (&value, &value_len, p_data, p_size))
      goto not_enough_data;

    gst_tag_name = gst_asf_demux_get_gst_tag_from_tag_name (name, name_len);
    if (gst_tag_name != NULL) {
      switch (datatype) {
        case ASF_DEMUX_DATA_TYPE_UTF16LE_STRING:{
          gchar *value_utf8;

          value_utf8 = g_convert (value, value_len, "UTF-8", "UTF-16LE",
              &in, &out, NULL);

          /* get rid of tags with empty value */
          if (value_utf8 != NULL && *value_utf8 != '\0') {
            value_utf8[out] = '\0';

            if (strcmp (gst_tag_name, GST_TAG_DATE) == 0) {
              guint year = atoi (value_utf8);

              if (year > 0) {
                GDate *date = g_date_new_dmy (1, 1, year);

                g_value_init (&tag_value, GST_TYPE_DATE);
                gst_value_set_date (&tag_value, date);
                g_date_free (date);
              }
            } else {
              g_value_init (&tag_value, G_TYPE_STRING);
              g_value_set_string (&tag_value, value_utf8);
            }
          } else if (value_utf8 == NULL) {
            GST_WARNING ("Failed to convert string value to UTF8, skipping");
          } else {
            GST_DEBUG ("Skipping empty string value for %s", gst_tag_name);
          }
          g_free (value_utf8);
          break;
        }
        case ASF_DEMUX_DATA_TYPE_DWORD:{
          g_value_init (&tag_value, G_TYPE_INT);
          g_value_set_int (&tag_value, (gint) GST_READ_UINT32_LE (value));
          break;
        }
        default:{
          GST_DEBUG ("Skipping tag %s of type %d", gst_tag_name, datatype);
          break;
        }
      }

      if (G_IS_VALUE (&tag_value)) {
        gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND,
            gst_tag_name, &tag_value, NULL);

        g_value_unset (&tag_value);
      }
    }

    g_free (name);
    g_free (value);
  }

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    gst_asf_demux_commit_taglist (demux, taglist);
  } else {
    gst_tag_list_free (taglist);
  }

  return GST_FLOW_OK;


not_enough_data:
  {
    /* avoid compiler warning when disabling logging at compile time */
    obj_data_start = NULL;

    GST_WARNING ("Unexpected end of data parsing stream object");
    GST_DEBUG ("object data offset: %u, bytes left to parse: %u",
        (guint) (*p_data - obj_data_start), (guint) * p_size);

    gst_tag_list_free (taglist);
    return ASF_FLOW_NEED_MORE_DATA;
  }
}


#define ASF_DEMUX_OBJECT_HEADER_SIZE  (16+8)

static gboolean
gst_asf_demux_get_object_header (GstASFDemux * demux, guint32 * obj_id,
    guint64 * obj_size, guint8 ** p_data, guint64 * p_size)
{
  ASFGuid guid;

  if (*p_size < ASF_DEMUX_OBJECT_HEADER_SIZE)
    return FALSE;

  gst_asf_demux_get_guid (&guid, p_data, p_size);

  *obj_id = gst_asf_demux_identify_guid (demux, asf_object_guids, &guid);

  *obj_size = gst_asf_demux_get_uint64 (p_data, p_size);

  if (*obj_id == ASF_OBJ_UNDEFINED) {
    GST_WARNING ("Unknown object %08x-%08x-%08x-%08x",
        guid.v1, guid.v2, guid.v3, guid.v4);
  }

  return TRUE;
}

static GstFlowReturn
gst_asf_demux_process_data (GstASFDemux * demux, guint64 object_size,
    guint8 ** p_data, guint64 * p_size)
{
  asf_obj_data data_object;

  /* Get the rest of the header */
  if (!gst_asf_demux_get_obj_data (&data_object, p_data, p_size))
    return ASF_FLOW_NEED_MORE_DATA;

  GST_INFO ("object is data with %" G_GUINT64_FORMAT " packets",
      data_object.packets);

  gst_element_no_more_pads (GST_ELEMENT (demux));

  demux->state = GST_ASF_DEMUX_STATE_DATA;
  demux->packet = 0;
  demux->num_packets = data_object.packets;

  /* minus object header and data object header */
  demux->data_size =
      object_size - ASF_DEMUX_OBJECT_HEADER_SIZE - (16 + 8 + 1 + 1);
  demux->data_offset = gst_asf_demux_get_current_offset (demux, *p_data);

  GST_LOG ("data_offset=%" G_GINT64_FORMAT ", data_size=%" G_GINT64_FORMAT,
      demux->data_offset, demux->data_size);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_asf_demux_process_header (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  asf_obj_header object;
  guint32 i;

  /* Get the rest of the header's header */
  if (!gst_asf_demux_get_obj_header (&object, p_data, p_size))
    return ASF_FLOW_NEED_MORE_DATA;

  GST_INFO ("object is a header with %u parts", object.num_objects);

  /* Loop through the header's objects, processing those */
  for (i = 0; i < object.num_objects; ++i) {
    GST_DEBUG ("reading header part %u: data=%p", i, *p_data);
    ret = gst_asf_demux_process_object (demux, p_data, p_size);
    if (ret != GST_FLOW_OK) {
      GST_WARNING ("process_object returned %s", gst_asf_get_flow_name (ret));
      break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_asf_demux_process_file (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  asf_obj_file object;

  /* Get the rest of the header's header */
  if (!gst_asf_demux_get_obj_file (&object, p_data, p_size))
    return ASF_FLOW_NEED_MORE_DATA;

  if (object.min_pktsize == object.max_pktsize) {
    demux->packet_size = object.max_pktsize;
  } else {
    demux->packet_size = (guint32) - 1;
    GST_WARNING ("Non-const packet size, seeking disabled");
  }

  /* FIXME: do we need object.send_time as well? what is it? */

  demux->play_time = (guint64) object.play_time * (GST_SECOND / 10000000);
  demux->preroll = object.preroll;
  GST_DEBUG_OBJECT (demux,
      "play_time %" GST_TIME_FORMAT " preroll %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->play_time), GST_TIME_ARGS (demux->preroll));

  gst_segment_set_duration (&demux->segment, GST_FORMAT_TIME, demux->play_time);

  GST_INFO ("object is a file with %" G_GUINT64_FORMAT " data packets",
      object.packets_count);
  GST_INFO ("preroll = %" G_GUINT64_FORMAT, demux->preroll);

  return GST_FLOW_OK;
}

/* Content Description Object */
static GstFlowReturn
gst_asf_demux_process_comment (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
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
    GST_TAG_COMMENT, 0, NULL}, {
    NULL, 0, NULL}              /* what GST_TAG to use here? */
  };

  asf_obj_comment object;
  GstTagList *taglist;
  GValue value = { 0 };
  gsize in, out;
  gint i;

  GST_INFO ("object is a comment");

  /* Get the rest of the comment's header */
  if (!gst_asf_demux_get_obj_comment (&object, p_data, p_size))
    return ASF_FLOW_NEED_MORE_DATA;

  GST_DEBUG ("Comment lengths: title=%d author=%d copyright=%d "
      "description=%d rating=%d", object.title_length, object.author_length,
      object.copyright_length, object.description_length, object.rating_length);


  tags[0].val_length = object.title_length;
  tags[1].val_length = object.author_length,
      tags[2].val_length = object.copyright_length;
  tags[3].val_length = object.description_length;
  tags[4].val_length = object.rating_length;

  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (*p_size < tags[i].val_length)
      goto not_enough_data;

    /* might be just '/0', '/0'... */
    if (tags[i].val_length > 2 && tags[i].val_length % 2 == 0) {
      /* convert to UTF-8 */
      tags[i].val_utf8 = g_convert ((gchar *) * p_data, tags[i].val_length,
          "UTF-8", "UTF-16LE", &in, &out, NULL);
    }
    *p_data += tags[i].val_length;
    *p_size -= tags[i].val_length;
  }

  /* parse metadata into taglist */
  taglist = gst_tag_list_new ();
  g_value_init (&value, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (tags[i].val_utf8 && strlen (tags[i].val_utf8) > 0 && tags[i].gst_tag) {
      g_value_set_string (&value, tags[i].val_utf8);
      gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND,
          tags[i].gst_tag, &value, NULL);
    }
  }
  g_value_unset (&value);

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    gst_asf_demux_commit_taglist (demux, taglist);
  } else {
    gst_tag_list_free (taglist);
  }

  for (i = 0; i < G_N_ELEMENTS (tags); ++i)
    g_free (tags[i].val_utf8);

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING ("unexpectedly short of data while processing "
        "comment tag section %s, skipping comment tag",
        (i < G_N_ELEMENTS (tags)) ? tags[i].gst_tag : "NONE");
    for (i = 0; i < G_N_ELEMENTS (tags); i++)
      g_free (tags[i].val_utf8);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_asf_demux_process_bitrate_props_object (GstASFDemux * demux,
    guint8 ** p_data, guint64 * p_size)
{
  guint16 num_streams, i;
  guint8 stream_id;

  if (*p_size < 2)
    return ASF_FLOW_NEED_MORE_DATA;

  num_streams = gst_asf_demux_get_uint16 (p_data, p_size);

  GST_INFO ("object is a bitrate properties object with %u streams",
      num_streams);

  for (i = 0; i < num_streams; ++i) {
    asf_bitrate_record bitrate_record;

    if (!gst_asf_demux_get_bitrate_record (&bitrate_record, p_data, p_size))
      return ASF_FLOW_NEED_MORE_DATA;

    stream_id = bitrate_record.stream_id;
    if (bitrate_record.stream_id < GST_ASF_DEMUX_NUM_STREAM_IDS) {
      demux->bitrate[stream_id] = bitrate_record.bitrate;
      GST_DEBUG ("bitrate[%u] = %u", stream_id, bitrate_record.bitrate);
    } else {
      GST_WARNING ("stream id %u is too large", stream_id);
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_asf_demux_process_header_ext (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  asf_obj_header_ext object;
  guint64 target_size;

  /* Get the rest of the header's header */
  if (!gst_asf_demux_get_obj_header_ext (&object, p_data, p_size))
    return ASF_FLOW_NEED_MORE_DATA;

  GST_INFO ("object is an extended header with a size of %u bytes",
      object.data_size);

  /* FIXME: does data_size include the rest of the header that we have read? */
  if (*p_size < object.data_size)
    return ASF_FLOW_NEED_MORE_DATA;

  target_size = *p_size - object.data_size;
  while (*p_size > target_size && ret == GST_FLOW_OK) {
    ret = gst_asf_demux_process_object (demux, p_data, p_size);
    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;
}

static GstFlowReturn
gst_asf_demux_process_object (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  GstFlowReturn ret;
  guint32 obj_id;
  guint64 obj_size;

  if (!gst_asf_demux_get_object_header (demux, &obj_id, &obj_size, p_data,
          p_size)) {
    demux->bytes_needed = ASF_DEMUX_OBJECT_HEADER_SIZE;
    return ASF_FLOW_NEED_MORE_DATA;
  }

  obj_size -= ASF_DEMUX_OBJECT_HEADER_SIZE;

  if (obj_id != ASF_OBJ_DATA && *p_size < obj_size) {
    demux->bytes_needed = obj_size + ASF_DEMUX_OBJECT_HEADER_SIZE;
    return ASF_FLOW_NEED_MORE_DATA;
  }

  GST_INFO ("processing object %s with size %" G_GUINT64_FORMAT,
      gst_asf_get_guid_nick (asf_object_guids, obj_id),
      obj_size + ASF_DEMUX_OBJECT_HEADER_SIZE);

  switch (obj_id) {
    case ASF_OBJ_STREAM:
      ret = gst_asf_demux_process_stream (demux, p_data, p_size);
      break;
    case ASF_OBJ_DATA:
      ret = gst_asf_demux_process_data (demux, obj_size, p_data, p_size);
      break;
    case ASF_OBJ_FILE:
      ret = gst_asf_demux_process_file (demux, p_data, p_size);
      break;
    case ASF_OBJ_HEADER:
      ret = gst_asf_demux_process_header (demux, p_data, p_size);
      break;
    case ASF_OBJ_COMMENT:
      ret = gst_asf_demux_process_comment (demux, p_data, p_size);
      break;
    case ASF_OBJ_HEAD1:
      ret = gst_asf_demux_process_header_ext (demux, p_data, p_size);
      break;
    case ASF_OBJ_BITRATE_PROPS:
      ret = gst_asf_demux_process_bitrate_props_object (demux, p_data, p_size);
      break;
    case ASF_OBJ_EXT_CONTENT_DESC:
      ret = gst_asf_demux_process_ext_content_desc (demux, p_data, p_size);
      break;
    case ASF_OBJ_CONCEAL_NONE:
    case ASF_OBJ_HEAD2:
    case ASF_OBJ_UNDEFINED:
    case ASF_OBJ_CODEC_COMMENT:
    case ASF_OBJ_INDEX:
    case ASF_OBJ_PADDING:
    case ASF_OBJ_BITRATE_MUTEX:
    case ASF_OBJ_LANGUAGE_LIST:
    case ASF_OBJ_METADATA_OBJECT:
    case ASF_OBJ_EXTENDED_STREAM_PROPS:
    default:
      /* Unknown/unhandled object read. Just ignore
       * it, people don't like fatal errors much */
      GST_INFO ("Skipping object (size %" G_GUINT64_FORMAT ") ...", obj_size);

      if (!gst_asf_demux_skip_bytes (obj_size, p_data, p_size))
        ret = ASF_FLOW_NEED_MORE_DATA;
      else
        ret = GST_FLOW_OK;
      break;
  }

  GST_DEBUG ("ret = %s", gst_asf_get_flow_name (ret));

  return ret;
}

static void
gst_asf_demux_descramble_segment (GstASFDemux * demux,
    asf_segment_info * segment_info, asf_stream_context * stream)
{
  GstBuffer *scrambled_buffer;
  GstBuffer *descrambled_buffer;
  GstBuffer *sub_buffer;
  guint offset;
  guint off;
  guint row;
  guint col;
  guint idx;

  /* descrambled_buffer is initialised in the first iteration */
  descrambled_buffer = NULL;
  scrambled_buffer = stream->payload;

  if (segment_info->segment_size < demux->ds_packet_size * demux->span)
    return;

  for (offset = 0; offset < segment_info->segment_size;
      offset += demux->ds_chunk_size) {
    off = offset / demux->ds_chunk_size;
    row = off / demux->span;
    col = off % demux->span;
    idx = row + col * demux->ds_packet_size / demux->ds_chunk_size;
    GST_DEBUG ("idx=%u, row=%u, col=%u, off=%u, ds_chunk_size=%u", idx, row,
        col, off, demux->ds_chunk_size);
    GST_DEBUG ("segment_info->segment_size=%u, span=%u, packet_size=%u",
        segment_info->segment_size, demux->span, demux->ds_packet_size);
    GST_DEBUG ("GST_BUFFER_SIZE (scrambled_buffer) = %u",
        GST_BUFFER_SIZE (scrambled_buffer));
    sub_buffer =
        gst_buffer_create_sub (scrambled_buffer, idx * demux->ds_chunk_size,
        demux->ds_chunk_size);
    if (!offset) {
      descrambled_buffer = sub_buffer;
    } else {
      GstBuffer *newbuf;

      newbuf = gst_buffer_merge (descrambled_buffer, sub_buffer);
      gst_buffer_unref (sub_buffer);
      gst_buffer_unref (descrambled_buffer);
      descrambled_buffer = newbuf;
    }
  }

  stream->payload = descrambled_buffer;
  gst_buffer_unref (scrambled_buffer);
}

static gboolean
gst_asf_demux_element_send_event (GstElement * element, GstEvent * event)
{
  GstASFDemux *demux = GST_ASF_DEMUX (element);
  gint i;

  GST_DEBUG ("handling element event of type %s", GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; ++i) {
    gst_event_ref (event);
    if (gst_asf_demux_handle_src_event (demux->stream[i].pad, event)) {
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

  GST_DEBUG ("sending event of type %s to all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; ++i) {
    gst_event_ref (event);
    gst_pad_push_event (demux->stream[i].pad, event);
  }
  gst_event_unref (event);
  return ret;
}

static GstFlowReturn
gst_asf_demux_push_buffer (GstASFDemux * demux, asf_stream_context * stream,
    GstBuffer * buf)
{
  GstFlowReturn ret;

  /* do we need to send a newsegment event? */
  if (stream->need_newsegment) {
    GST_DEBUG ("sending new-segment event on pad %s",
        GST_PAD_NAME (stream->pad));

    /* FIXME: if we need to send a newsegment event on this pad and
     * the buffer doesn't have a timestamp, should we just drop the buffer
     * and wait for one with a timestamp before sending it? */
    gst_asf_demux_send_event_unlocked (demux, gst_event_new_new_segment (FALSE, demux->segment.rate, GST_FORMAT_TIME, demux->segment.start, demux->segment.stop, demux->segment.start));        /* last parameter isn't right */

    stream->need_newsegment = FALSE;
  }

  /* don't set the same time stamp on multiple consecutive outgoing
   * video buffers, set it on the first one and set NONE on the others,
   * it's the decoder's job to fill the missing bits properly */
  if (stream->is_video && GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
      GST_BUFFER_TIMESTAMP (buf) == stream->last_buffer_timestamp) {
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  }

  /* make sure segment.last_stop is continually increasing */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
      demux->segment.last_stop < (gint64) GST_BUFFER_TIMESTAMP (buf)) {
    gst_segment_set_last_stop (&demux->segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf));
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
    stream->last_buffer_timestamp = GST_BUFFER_TIMESTAMP (buf);

  gst_buffer_set_caps (buf, stream->caps);

  GST_INFO ("pushing buffer on pad %s, ts=%" GST_TIME_FORMAT,
      GST_PAD_NAME (stream->pad), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  ret = gst_pad_push (stream->pad, buf);

  if (ret == GST_FLOW_NOT_LINKED)
    ret = GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_asf_demux_process_chunk (GstASFDemux * demux,
    asf_packet_info * packet_info, asf_segment_info * segment_info,
    guint8 ** p_data, guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  asf_stream_context *stream;
  GstBuffer *buffer;

  stream = gst_asf_demux_get_stream (demux, segment_info->stream_number);
  if (stream == NULL) {
    GST_WARNING ("invalid stream number %d", segment_info->stream_number);
    goto done;
  }

  GST_DEBUG ("Processing %s chunk of size %u (frag_offset=%d)",
      GST_PAD_NAME (stream->pad), segment_info->chunk_size,
      stream->frag_offset);

  if (segment_info->frag_offset == 0) {
    /* new packet */
    stream->sequence = segment_info->sequence;
    demux->pts = segment_info->frag_timestamp - demux->preroll;

    /*
       if (stream->is_video) {
       GST_DEBUG ("%s: demux->pts=%lld (frag_timestamp=%ld, preroll=%lld)",
       GST_PAD_NAME (stream->pad), demux->pts,
       segment_info->frag_timestamp, demux->preroll);
       }
     */

    if (!gst_asf_demux_get_buffer (&buffer, segment_info->chunk_size,
            p_data, p_size)) {
      return ASF_FLOW_NEED_MORE_DATA;
    }

    GST_DEBUG ("BUFFER: Copied stream to buffer %p", buffer);
    stream->payload = buffer;
  } else {
    GST_DEBUG ("segment_info->sequence=%d, stream->sequence=%d,"
        " segment_info->frag_offset=%d, stream->frag_offset=%d",
        segment_info->sequence, stream->sequence, segment_info->frag_offset,
        stream->frag_offset);

    if (segment_info->sequence == stream->sequence &&
        segment_info->frag_offset == stream->frag_offset) {
      GstBuffer *new_buffer;

      /* continuing packet */
      GST_INFO ("continuation packet");

      if (!gst_asf_demux_get_buffer (&buffer, segment_info->chunk_size,
              p_data, p_size)) {
        return ASF_FLOW_NEED_MORE_DATA;
      }

      GST_DEBUG ("copied stream to buffer %p", buffer);

      new_buffer = gst_buffer_merge (stream->payload, buffer);
      GST_DEBUG_OBJECT (demux,
          "BUFFER: Merged new_buffer (%p - %d) from stream->payload (%p - %d)"
          " and buffer (%p - %d)", new_buffer,
          GST_MINI_OBJECT_REFCOUNT_VALUE (new_buffer), stream->payload,
          GST_MINI_OBJECT_REFCOUNT_VALUE (stream->payload), buffer,
          GST_MINI_OBJECT_REFCOUNT_VALUE (buffer));
      gst_buffer_unref (stream->payload);
      gst_buffer_unref (buffer);
      stream->payload = new_buffer;
    } else {
      /* cannot continue current packet: free it */
      if (stream->frag_offset != 0) {
        /* cannot create new packet */
        GST_DEBUG ("BUFFER: Freeing stream->payload (%p)", stream->payload);
        gst_buffer_unref (stream->payload);
#if 0
        /* FIXME: is this right/needed? we already do that below, no? */
        packet_info->size_left -= segment_info->chunk_size;
#endif
        stream->frag_offset = 0;
      }
      demux->pts = segment_info->frag_timestamp - demux->preroll;

      /*
         if (stream->is_video) {
         GST_DEBUG ("%s: demux->pts=%lld (frag_timestamp=%ld, preroll=%lld)",
         GST_PAD_NAME (stream->pad), demux->pts,
         segment_info->frag_timestamp, demux->preroll);
         }
       */

      goto done;
#if 0
      /* FIXME: where did this come from / fit in ? */
      return TRUE;
      else {
        /* create new packet */
        stream->sequence = segment_info->sequence;
      }
#endif
    }
  }

  stream->frag_offset += segment_info->chunk_size;

  GST_DEBUG ("frag_offset = %d  segment_size = %d ", stream->frag_offset,
      segment_info->segment_size);

  if (stream->frag_offset < segment_info->segment_size) {
    /* We don't have the whole packet yet */
  } else {
    /* We have the whole packet now so we should push the packet to
       the src pad now. First though we should check if we need to do
       descrambling */
    if (demux->span > 1) {
      gst_asf_demux_descramble_segment (demux, segment_info, stream);
    }

    if (stream->is_video) {
      GST_DEBUG ("%s: demux->pts=%lld=%" GST_TIME_FORMAT
          ", stream->last_pts=%lld=%" GST_TIME_FORMAT,
          GST_PAD_NAME (stream->pad), demux->pts,
          GST_TIME_ARGS ((GST_SECOND / 1000) * demux->pts), stream->last_pts,
          GST_TIME_ARGS ((GST_SECOND / 1000) * stream->last_pts));
    }

    /* FIXME: last_pts is not a GstClockTime and not in nanoseconds, so
     * this is not really 100% right ... */
    if (demux->pts >= stream->last_pts ||
        !GST_CLOCK_TIME_IS_VALID (stream->last_pts)) {
      stream->last_pts = demux->pts;
    }

    GST_BUFFER_TIMESTAMP (stream->payload) =
        (GST_SECOND / 1000) * stream->last_pts;

    GST_DEBUG ("sending stream %d of size %d", stream->id,
        segment_info->chunk_size);

    if (!stream->fps_known) {
      if (!stream->cache) {
        stream->cache = stream->payload;
      } else {
        gdouble fps;
        gint64 diff;
        gint num, denom;

        /* why is all this needed anyway? (tpm) */
        diff = GST_BUFFER_TIMESTAMP (stream->payload) -
            GST_BUFFER_TIMESTAMP (stream->cache);

        fps = (gdouble) GST_SECOND / diff;

        /* artificial cap */
        if (fps >= 50.0) {
          num = 50;
          denom = 1;
        } else if (fps <= 5.0) {
          num = 5;
          denom = 1;
        } else {
          /* crack alert */
          num = (gint) GST_SECOND;
          while (diff > G_MAXINT) {
            num = num >> 1;
            diff = diff >> 1;
          }
          denom = (gint) diff;
        }
        stream->fps_known = TRUE;
        stream->caps = gst_caps_make_writable (stream->caps);
        gst_caps_set_simple (stream->caps,
            "framerate", GST_TYPE_FRACTION, num, denom, NULL);
        GST_DEBUG ("set up stream with fps %d/%d", num, denom);
        gst_pad_use_fixed_caps (stream->pad);
        gst_pad_set_caps (stream->pad, stream->caps);

        ret = gst_asf_demux_push_buffer (demux, stream, stream->cache);
        stream->cache = NULL;

        ret = gst_asf_demux_push_buffer (demux, stream, stream->payload);
        stream->payload = NULL;
      }
    } else {
      ret = gst_asf_demux_push_buffer (demux, stream, stream->payload);
      stream->payload = NULL;
    }

    stream->frag_offset = 0;
  }

done:

  packet_info->size_left -= segment_info->chunk_size;

  return ret;
}

static GstFlowReturn
gst_asf_demux_process_segment (GstASFDemux * demux,
    asf_packet_info * packet_info, guint8 ** p_data, guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  asf_segment_info segment_info;
  gboolean has_key_frame;
  guint64 start_size;
  guint32 replic_size;
  guint32 time_start;
  guint32 frag_size;
  guint32 rsize;
  guint8 time_delta;
  guint8 byte;

  start_size = *p_size;

  if (*p_size < 1)
    return ASF_FLOW_NEED_MORE_DATA;

  byte = gst_asf_demux_get_uint8 (p_data, p_size);
  segment_info.stream_number = byte & 0x7f;
  has_key_frame = ((byte & 0x80) == 0x80);      /* FIXME: use this somewhere? */

  GST_INFO ("processing segment for stream %u%s", segment_info.stream_number,
      (has_key_frame) ? " (has keyframe)" : "");

  /* FIXME: check (doesn't work) */
#if 0
  {
    asf_stream_context *stream;

    stream = gst_asf_demux_get_stream (demux, segment_info.stream_number);
    if (stream && stream->last_pts == GST_CLOCK_TIME_NONE &&
        stream->is_video && !has_key_frame) {
      g_print ("skipping segment, waiting for a key unit\n");
      if (!gst_asf_demux_skip_bytes (segment_info.segment_size - 1, p_data,
              p_size))
        return ASF_FLOW_NEED_MORE_DATA;
      packet_info->size_left -= segment_info.segment_size;
      return GST_FLOW_OK;
    }
  }
#endif

  segment_info.sequence =
      gst_asf_demux_get_var_length (packet_info->seqtype, p_data, p_size);
  segment_info.frag_offset =
      gst_asf_demux_get_var_length (packet_info->fragoffsettype, p_data,
      p_size);
  replic_size =
      gst_asf_demux_get_var_length (packet_info->replicsizetype, p_data,
      p_size);

  GST_DEBUG ("sequence=%u, frag_offset=%u, replic_size=%u",
      segment_info.sequence, segment_info.frag_offset, replic_size);

  if (replic_size > 1) {
    asf_replicated_data replicated_data_header;

    segment_info.compressed = FALSE;

    /* It's uncompressed with replic data */
    if (!gst_asf_demux_get_replicated_data (&replicated_data_header, p_data,
            p_size))
      return ASF_FLOW_NEED_MORE_DATA;
/*    {
      GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
          ("The payload has replicated data but the size is less than 8"));
      return GST_FLOW_ERROR;
    }
*/
    segment_info.frag_timestamp = replicated_data_header.frag_timestamp;
    segment_info.segment_size = replicated_data_header.object_size;

    if (replic_size > 8) {
      if (!gst_asf_demux_skip_bytes ((replic_size - 8), p_data, p_size))
        return ASF_FLOW_NEED_MORE_DATA;
    }
  } else {
    if (replic_size == 1) {
      /* It's compressed */
      segment_info.compressed = TRUE;
      time_delta = gst_asf_demux_get_uint8 (p_data, p_size);
      GST_DEBUG ("time_delta = %u", time_delta);
    } else {
      segment_info.compressed = FALSE;
    }

    time_start = segment_info.frag_offset;
    segment_info.frag_offset = 0;
    segment_info.frag_timestamp = demux->timestamp;
  }

  GST_DEBUG ("multiple = %u, compressed = %u",
      packet_info->multiple, segment_info.compressed);

  if (packet_info->multiple) {
    frag_size = gst_asf_demux_get_var_length (packet_info->segsizetype,
        p_data, p_size);
  } else {
    frag_size = packet_info->size_left - (start_size - *p_size);
  }

  rsize = start_size - *p_size;

  packet_info->size_left -= rsize;

  GST_DEBUG ("size left = %u, frag size = %u, rsize = %u",
      packet_info->size_left, frag_size, rsize);

  if (segment_info.compressed) {
    while (frag_size > 0) {
      byte = gst_asf_demux_get_uint8 (p_data, p_size);
      packet_info->size_left--;
      segment_info.chunk_size = byte;
      segment_info.segment_size = segment_info.chunk_size;

      if (segment_info.chunk_size > packet_info->size_left) {
        return ASF_FLOW_NEED_MORE_DATA;
        /* or is this an error?
         *   GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
         *        ("Payload chunk overruns packet size."));
         *    return GST_FLOW_ERROR; */
      }

      ret = gst_asf_demux_process_chunk (demux, packet_info, &segment_info,
          p_data, p_size);

      if (ret != GST_FLOW_OK)
        break;

      if (segment_info.chunk_size < frag_size)
        frag_size -= segment_info.chunk_size + 1;
      else {
/*
        GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
            ("Invalid data in stream"),
            ("Invalid fragment size indicator in segment"));
        ret = GST_FLOW_ERROR;
*/
        return ASF_FLOW_NEED_MORE_DATA;
        break;
      }
    }
  } else {
    segment_info.chunk_size = frag_size;
    ret = gst_asf_demux_process_chunk (demux, packet_info, &segment_info,
        p_data, p_size);
  }

  return ret;
}

static GstFlowReturn
gst_asf_demux_handle_data (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  asf_packet_info packet_info;
  gboolean correction;
  guint64 start_size;
  guint32 sequence;
  guint32 packet_length;
  guint32 rsize;
  guint16 duration;
  guint8 num_segments;
  guint8 segment;
  guint8 flags;
  guint8 property;

  start_size = *p_size;

  GST_INFO ("processing packet %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT,
      demux->packet, demux->num_packets);

  if (demux->num_packets > 0 && demux->packet++ >= demux->num_packets) {

    GST_LOG ("reached EOS");
#if 0
    have a gst_asf_demux_reset (demux) maybe ?
        gst_adapter_clear (demux->adapter);
#endif

    gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  }

  if (*p_size < 1) {
    GST_WARNING ("unexpected end of data");
    return ASF_FLOW_NEED_MORE_DATA;
  }

  correction = ((gst_asf_demux_get_uint8 (p_data, p_size) & 0x80) == 0x80);

  /* Uses error correction? */
  if (correction) {
    asf_obj_data_correction corr_obj;

    GST_DEBUG ("data has error correction");
    if (!gst_asf_demux_get_obj_data_correction (&corr_obj, p_data, p_size)) {
      GST_WARNING ("unexpected end of data");
      return ASF_FLOW_NEED_MORE_DATA;
    }
  }

  /* Read the packet flags */
  if (*p_size < (1 + 1)) {
    GST_WARNING ("unexpected end of data");
    return ASF_FLOW_NEED_MORE_DATA;
  }
  flags = gst_asf_demux_get_uint8 (p_data, p_size);
  property = gst_asf_demux_get_uint8 (p_data, p_size);

  packet_info.multiple = ((flags & 0x01) == 0x01);

  sequence = gst_asf_demux_get_var_length ((flags >> 1) & 0x03, p_data, p_size);

  packet_info.padsize =
      gst_asf_demux_get_var_length ((flags >> 3) & 0x03, p_data, p_size);

  packet_length =
      gst_asf_demux_get_var_length ((flags >> 5) & 0x03, p_data, p_size);

  if (packet_length == 0)
    packet_length = demux->packet_size;

  GST_DEBUG ("multiple = %u, sequence = %u, padsize = %u, "
      "packet length = %u", packet_info.multiple, sequence,
      packet_info.padsize, packet_length);

  /* Read the property flags */
  packet_info.replicsizetype = property & 0x03;
  packet_info.fragoffsettype = (property >> 2) & 0x03;
  packet_info.seqtype = (property >> 4) & 0x03;

  if (*p_size < (4 + 2)) {
    GST_WARNING ("unexpected end of data");
    return ASF_FLOW_NEED_MORE_DATA;
  }

  demux->timestamp = gst_asf_demux_get_uint32 (p_data, p_size);
  duration = gst_asf_demux_get_uint16 (p_data, p_size);

  GST_DEBUG ("timestamp = %" GST_TIME_FORMAT ", duration = %" GST_TIME_FORMAT,
      GST_TIME_ARGS ((gint64) demux->timestamp * GST_MSECOND),
      GST_TIME_ARGS ((gint64) duration * GST_MSECOND));

  /* Are there multiple payloads? */
  if (packet_info.multiple) {
    guint8 multi_flags = gst_asf_demux_get_uint8 (p_data, p_size);

    packet_info.segsizetype = (multi_flags >> 6) & 0x03;
    num_segments = multi_flags & 0x3f;
  } else {
    packet_info.segsizetype = 2;
    num_segments = 1;
  }

  rsize = start_size - *p_size;

  packet_info.size_left = packet_length - packet_info.padsize - rsize;

  GST_DEBUG ("rsize: %u, size left: %u", rsize, packet_info.size_left);

  for (segment = 0; segment < num_segments; ++segment) {
    GstFlowReturn ret;

    ret = gst_asf_demux_process_segment (demux, &packet_info, p_data, p_size);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG ("process_segment %u returned %s", segment,
          gst_asf_get_flow_name (ret));
      return ret;
    }
  }

  /* Skip the padding */
  if (packet_info.padsize > 0) {
    if (*p_size < packet_info.padsize) {
      GST_WARNING ("unexpected end of data");
      return ASF_FLOW_NEED_MORE_DATA;
    }

    if (!gst_asf_demux_skip_bytes (packet_info.padsize, p_data, p_size))
      return ASF_FLOW_NEED_MORE_DATA;
  }

  GST_DEBUG ("remaining size left: %u", packet_info.size_left);

  /* FIXME: this doesn't really make sense, does it? if we don't have enough
   * bytes left to skip the stuff at the end and we've already sent out
   * buffers, just returning NEED_MORE_DATA isn't really right. Should we
   * just throw an error in that case (can it happen with a non-broken
   * stream?) */
  if (packet_info.size_left > 0) {
    if (!gst_asf_demux_skip_bytes (packet_info.size_left, p_data, p_size)) {
      GST_WARNING
          ("unexpected end of data, *p_size=%lld,packet_info.size_left=%u",
          *p_size, packet_info.size_left);
      return ASF_FLOW_NEED_MORE_DATA;
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_asf_demux_parse_data (GstASFDemux * demux)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* this is basically an infinite loop */
  switch (demux->state) {
    case GST_ASF_DEMUX_STATE_HEADER:{
      guint64 data_left;
      guint8 *data;

      data_left = (guint64) gst_adapter_available (demux->adapter);

      GST_DEBUG ("STATE_HEADER, avail=%u:", data_left);

      data = (guint8 *) gst_adapter_peek (demux->adapter, data_left);

      ret = gst_asf_demux_process_object (demux, &data, &data_left);

      if (ret != ASF_FLOW_NEED_MORE_DATA) {
        guint bytes_used = gst_adapter_available (demux->adapter) - data_left;

        GST_DEBUG ("flushing %u bytes", bytes_used);
        gst_adapter_flush (demux->adapter, bytes_used);
      } else {
        GST_DEBUG ("not flushing, process_object returned %s",
            gst_asf_get_flow_name (ret));
      }

      break;
    }
    case GST_ASF_DEMUX_STATE_DATA:{
      guint64 data_size, start_data_size;
      guint8 *data;
      guint avail;

      avail = gst_adapter_available (demux->adapter);

      GST_DEBUG ("STATE_DATA, avail=%u:", avail);

      /* make sure a full packet is actually available */
      if (demux->packet_size != (guint32) - 1 && avail < demux->packet_size) {
        demux->bytes_needed = demux->packet_size;
        return ASF_FLOW_NEED_MORE_DATA;
      }

      if (demux->packet_size == (guint32) - 1)
        data_size = avail;
      else
        data_size = demux->packet_size;

      start_data_size = data_size;

      data = (guint8 *) gst_adapter_peek (demux->adapter, data_size);

      ret = gst_asf_demux_handle_data (demux, &data, &data_size);

      if (ret != ASF_FLOW_NEED_MORE_DATA) {
        if (demux->packet_size == (guint32) - 1) {
          guint bytes_used = start_data_size - data_size;

          GST_DEBUG ("flushing %u bytes", bytes_used);
          gst_adapter_flush (demux->adapter, bytes_used);
        } else {
          GST_DEBUG ("flushing %u bytes", demux->packet_size);
          gst_adapter_flush (demux->adapter, demux->packet_size);
        }
      } else {
        GST_DEBUG ("not flushing, handle_data returned %s",
            gst_asf_get_flow_name (ret));

        /* if we know the packet size and still do a
         * short read, then something is fishy */
        if (demux->packet_size != (guint32) - 1) {
/*
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
              ("Error parsing packet"),
              ("Unexpected short read in packet at offset %" G_GINT64_FORMAT,
                  gst_asf_demux_get_current_offset (demux, NULL)));
          
          ret = GST_FLOW_ERROR;
*/
          gst_adapter_flush (demux->adapter, demux->packet_size);
          ret = GST_FLOW_OK;
        }
      }
      break;
    }
    case GST_ASF_DEMUX_STATE_EOS:{
      GST_DEBUG ("STATE_EOS:");
      gst_pad_event_default (demux->sinkpad, gst_event_new_eos ());
      break;
    }
    default:
      g_return_val_if_reached (GST_FLOW_UNEXPECTED);
  }

  return ret;
}

static const GstQueryType *
gst_asf_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return types;
}

static gboolean
gst_asf_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstASFDemux *demux;
  gboolean res = FALSE;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

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

      if (demux->segment.last_stop != GST_CLOCK_TIME_NONE) {
        GST_LOG ("returning position: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->segment.last_stop));

        gst_query_set_position (query, GST_FORMAT_TIME,
            demux->segment.last_stop);

        res = TRUE;
      } else {
        GST_LOG ("position not known yet");
      }

      GST_OBJECT_UNLOCK (demux);
      break;
    }

    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (demux);
  return res;
}

static GstStateChangeReturn
gst_asf_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstASFDemux *demux = GST_ASF_DEMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      gst_segment_init (&demux->segment, GST_FORMAT_TIME);
      demux->adapter = gst_adapter_new ();
      demux->next_byte_offset = GST_BUFFER_OFFSET_NONE;
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
      gst_adapter_clear (demux->adapter);
      g_object_unref (demux->adapter);
      demux->adapter = NULL;
      if (demux->taglist) {
        gst_tag_list_free (demux->taglist);
        demux->taglist = NULL;
      }
      demux->state = GST_ASF_DEMUX_STATE_HEADER;
      break;
    }
    default:
      break;
  }

  return ret;
}
