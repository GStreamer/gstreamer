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
#include <gst/riff/riff-ids.h>
#include <string.h>

#include "gstasfdemux.h"
#include "asfheaders.h"

static GstStaticPadTemplate gst_asf_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

GST_DEBUG_CATEGORY_STATIC (asf_debug);
#define GST_CAT_DEFAULT asf_debug

static void gst_asf_demux_base_init (gpointer g_class);
static void gst_asf_demux_class_init (GstASFDemuxClass * klass);
static void gst_asf_demux_init (GstASFDemux * asf_demux);
static gboolean gst_asf_demux_send_event (GstElement * element,
    GstEvent * event);
static void gst_asf_demux_loop (GstElement * element);
static gboolean gst_asf_demux_handle_data (GstASFDemux * asf_demux);
static gboolean gst_asf_demux_process_object (GstASFDemux * asf_demux);
static void gst_asf_demux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static guint32 gst_asf_demux_identify_guid (GstASFDemux * asf_demux,
    ASFGuidHash * guids, ASFGuid * guid_raw);
static gboolean gst_asf_demux_process_chunk (GstASFDemux * asf_demux,
    asf_packet_info * packet_info, asf_segment_info * segment_info);
static const GstEventMask *gst_asf_demux_get_src_event_mask (GstPad * pad);
static gboolean gst_asf_demux_handle_sink_event (GstASFDemux * asf_demux,
    GstEvent * event, guint32 remaining);
static gboolean gst_asf_demux_handle_src_event (GstPad * pad, GstEvent * event);
static const GstFormat *gst_asf_demux_get_src_formats (GstPad * pad);
static const GstQueryType *gst_asf_demux_get_src_query_types (GstPad * pad);
static gboolean gst_asf_demux_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value);
static gboolean gst_asf_demux_add_video_stream (GstASFDemux * asf_demux,
    asf_stream_video_format * video_format, guint16 id);
static gboolean gst_asf_demux_add_audio_stream (GstASFDemux * asf_demux,
    asf_stream_audio * audio, guint16 id);
static gboolean gst_asf_demux_setup_pad (GstASFDemux * asf_demux,
    GstPad * src_pad, GstCaps * caps, guint16 id);

static GstElementStateReturn gst_asf_demux_change_state (GstElement * element);
static GstCaps *gst_asf_demux_video_caps (guint32 codec_fcc,
    asf_stream_video_format * video, guint8 * extradata, char **codec_name);
static GstCaps *gst_asf_demux_audio_caps (guint16 codec_id,
    asf_stream_audio * audio, guint8 * extradata, char **codec_name);

static GstPadTemplate *videosrctempl, *audiosrctempl;
static GstElementClass *parent_class = NULL;

GType
gst_asf_demux_get_type (void)
{
  static GType asf_demux_type = 0;

  if (!asf_demux_type) {
    static const GTypeInfo asf_demux_info = {
      sizeof (GstASFDemuxClass),
      gst_asf_demux_base_init,
      NULL,
      (GClassInitFunc) gst_asf_demux_class_init,
      NULL,
      NULL,
      sizeof (GstASFDemux),
      0,
      (GInstanceInitFunc) gst_asf_demux_init,
    };

    asf_demux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstASFDemux",
        &asf_demux_info, 0);

    GST_DEBUG_CATEGORY_INIT (asf_debug, "asfdemux", 0, "asf demuxer element");
  }
  return asf_demux_type;
}

static void
gst_asf_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_asf_demux_details = {
    "ASF Demuxer",
    "Codec/Demuxer",
    "Demultiplexes ASF Streams",
    "Owen Fraser-Green <owen@discobabe.net>"
  };
  int i;
  GstCaps *audcaps, *vidcaps, *temp;
  guint32 vid_list[] = {
    GST_MAKE_FOURCC ('I', '4', '2', '0'),
    GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
    GST_MAKE_FOURCC ('M', 'J', 'P', 'G'),
    GST_MAKE_FOURCC ('D', 'V', 'S', 'D'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '1'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '2'),
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'),
    GST_MAKE_FOURCC ('M', 'P', 'G', '4'),
    GST_MAKE_FOURCC ('M', 'P', '4', '2'),
    GST_MAKE_FOURCC ('M', 'P', '4', '3'),
    GST_MAKE_FOURCC ('D', 'I', 'V', '3'),
    GST_MAKE_FOURCC ('D', 'X', '5', '0'),
    0                           /* end */
  };
  gint aud_list[] = {
    GST_RIFF_WAVE_FORMAT_MPEGL3,
    GST_RIFF_WAVE_FORMAT_MPEGL12,
    GST_RIFF_WAVE_FORMAT_PCM,
    GST_RIFF_WAVE_FORMAT_VORBIS1,
    GST_RIFF_WAVE_FORMAT_A52,
    GST_RIFF_WAVE_FORMAT_WMAV1,
    GST_RIFF_WAVE_FORMAT_WMAV2,
    GST_RIFF_WAVE_FORMAT_WMAV3,
    -1                          /* end */
  };

  audcaps = gst_caps_new_empty ();
  for (i = 0; aud_list[i] != -1; i++) {
    temp = gst_asf_demux_audio_caps (aud_list[i], NULL, NULL, NULL);
    gst_caps_append (audcaps, temp);
  }

  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, audcaps);

  vidcaps = gst_caps_new_empty ();
  for (i = 0; vid_list[i] != 0; i++) {
    temp = gst_asf_demux_video_caps (vid_list[i], NULL, NULL, NULL);
    gst_caps_append (vidcaps, temp);
  }

  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, vidcaps);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asf_demux_sink_template));
  gst_element_class_set_details (element_class, &gst_asf_demux_details);
}

static void
gst_asf_demux_class_init (GstASFDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->get_property = gst_asf_demux_get_property;

  gstelement_class->change_state = gst_asf_demux_change_state;
  gstelement_class->send_event = gst_asf_demux_send_event;
}

static void
gst_asf_demux_init (GstASFDemux * asf_demux)
{
  guint i;

  asf_demux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_asf_demux_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (asf_demux), asf_demux->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (asf_demux), gst_asf_demux_loop);

  /* We should zero everything to be on the safe side */
  for (i = 0; i < GST_ASF_DEMUX_NUM_VIDEO_PADS; i++) {
    asf_demux->video_pad[i] = NULL;
    asf_demux->video_PTS[i] = 0;
  }

  for (i = 0; i < GST_ASF_DEMUX_NUM_AUDIO_PADS; i++) {
    asf_demux->audio_pad[i] = NULL;
    asf_demux->audio_PTS[i] = 0;
  }

  asf_demux->num_audio_streams = 0;
  asf_demux->num_video_streams = 0;
  asf_demux->num_streams = 0;

  asf_demux->state = GST_ASF_DEMUX_STATE_HEADER;
  asf_demux->seek_pending = GST_CLOCK_TIME_NONE;
  asf_demux->seek_discont = FALSE;

  GST_FLAG_SET (asf_demux, GST_ELEMENT_EVENT_AWARE);
}

static gboolean
gst_asf_demux_send_event (GstElement * element, GstEvent * event)
{
  const GList *pads;

  pads = gst_element_get_pad_list (element);

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
      /* we ref the event here as we might have to try again if the event
       * failed on this pad */
      gst_event_ref (event);
      if (gst_asf_demux_handle_src_event (pad, event)) {
        gst_event_unref (event);
        return TRUE;
      }
    }

    pads = g_list_next (pads);
  }

  gst_event_unref (event);
  return FALSE;
}

static void
gst_asf_demux_loop (GstElement * element)
{
  GstASFDemux *asf_demux;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ASF_DEMUX (element));

  asf_demux = GST_ASF_DEMUX (element);

  /* this is basically an infinite loop */
  switch (asf_demux->state) {
    case GST_ASF_DEMUX_STATE_HEADER:
      gst_asf_demux_process_object (asf_demux);
      break;
    case GST_ASF_DEMUX_STATE_DATA:
      gst_asf_demux_handle_data (asf_demux);
      break;
    case GST_ASF_DEMUX_STATE_EOS:
      gst_pad_event_default (asf_demux->sinkpad, gst_event_new (GST_EVENT_EOS));
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static guint32
_read_var_length (GstASFDemux * asf_demux, guint8 type, guint32 * rsize)
{
  guint32 got_bytes;
  guint8 *var;
  guint32 ret = 0;
  GstByteStream *bs = asf_demux->bs;

  if (type == 0) {
    return 0;
  }

  got_bytes = gst_bytestream_peek_bytes (bs, &var, 4);

  while (got_bytes < 4) {
    guint32 remaining;
    GstEvent *event;

    gst_bytestream_get_status (bs, &remaining, &event);
    gst_event_unref (event);

    got_bytes = gst_bytestream_peek_bytes (bs, &var, 4);
  }

  switch (type) {
    case 1:
      ret = (GST_READ_UINT32_LE (var)) & 0xff;
      gst_bytestream_flush (bs, 1);
      *rsize += 1;
      break;
    case 2:
      ret = (GST_READ_UINT32_LE (var)) & 0xffff;
      gst_bytestream_flush (bs, 2);
      *rsize += 2;
      break;
    case 3:
      ret = GST_READ_UINT32_LE (var);
      gst_bytestream_flush (bs, 4);
      *rsize += 4;
      break;
  }

  return ret;
}

#define READ_UINT_BITS_FUNCTION(bits)						\
static gboolean									\
_read_uint ## bits (GstASFDemux *asf_demux, guint ## bits *ret)	        	\
{										\
  GstEvent *event;								\
  guint32 remaining;								\
  guint8* data;									\
										\
  g_return_val_if_fail (ret != NULL, FALSE);					\
										\
  do {										\
    if (gst_bytestream_peek_bytes (asf_demux->bs, &data, bits / 8) == bits / 8) { \
      *ret = GST_READ_UINT ## bits ## _LE (data);				\
      gst_bytestream_flush (asf_demux->bs, bits / 8);				\
      return TRUE;								\
    }										\
    gst_bytestream_get_status (asf_demux->bs, &remaining, &event);		\
  } while (gst_asf_demux_handle_sink_event (asf_demux, event, remaining));	\
										\
  return FALSE;									\
}

#define GST_READ_UINT8_LE(x) GST_READ_UINT8(x)
READ_UINT_BITS_FUNCTION (8)
    READ_UINT_BITS_FUNCTION (16)
    READ_UINT_BITS_FUNCTION (32)
    READ_UINT_BITS_FUNCTION (64)
#define GET_UINT(a,b)
     static gboolean _read_guid (GstASFDemux * asf_demux, ASFGuid * guid)
{
  return (_read_uint32 (asf_demux, &guid->v1) &&
      _read_uint32 (asf_demux, &guid->v2) &&
      _read_uint32 (asf_demux, &guid->v3) &&
      _read_uint32 (asf_demux, &guid->v4));
}

static gboolean
_read_obj_file (GstASFDemux * asf_demux, asf_obj_file * object)
{
  return (_read_guid (asf_demux, &object->file_id) &&
      _read_uint64 (asf_demux, &object->file_size) &&
      _read_uint64 (asf_demux, &object->creation_time) &&
      _read_uint64 (asf_demux, &object->packets_count) &&
      _read_uint64 (asf_demux, &object->play_time) &&
      _read_uint64 (asf_demux, &object->send_time) &&
      _read_uint64 (asf_demux, &object->preroll) &&
      _read_uint32 (asf_demux, &object->flags) &&
      _read_uint32 (asf_demux, &object->min_pktsize) &&
      _read_uint32 (asf_demux, &object->max_pktsize) &&
      _read_uint32 (asf_demux, &object->min_bitrate));
}

static gboolean
_read_bitrate_record (GstASFDemux * asf_demux, asf_bitrate_record * record)
{
  return (_read_uint16 (asf_demux, &record->stream_id) &&
      _read_uint32 (asf_demux, &record->bitrate));
}

static gboolean
_read_obj_comment (GstASFDemux * asf_demux, asf_obj_comment * comment)
{
  return (_read_uint16 (asf_demux, &comment->title_length) &&
      _read_uint16 (asf_demux, &comment->author_length) &&
      _read_uint16 (asf_demux, &comment->copyright_length) &&
      _read_uint16 (asf_demux, &comment->description_length) &&
      _read_uint16 (asf_demux, &comment->rating_length));
}

static gboolean
_read_obj_header (GstASFDemux * asf_demux, asf_obj_header * header)
{
  return (_read_uint32 (asf_demux, &header->num_objects) &&
      _read_uint8 (asf_demux, &header->unknown1) &&
      _read_uint8 (asf_demux, &header->unknown2));
}

static gboolean
_read_obj_header_ext (GstASFDemux * asf_demux, asf_obj_header_ext * header_ext)
{
  return (_read_guid (asf_demux, &header_ext->reserved1) &&
      _read_uint16 (asf_demux, &header_ext->reserved2) &&
      _read_uint32 (asf_demux, &header_ext->data_size));
}

static gboolean
_read_obj_stream (GstASFDemux * asf_demux, asf_obj_stream * stream)
{
  return (_read_guid (asf_demux, &stream->type) &&
      _read_guid (asf_demux, &stream->correction) &&
      _read_uint64 (asf_demux, &stream->unknown1) &&
      _read_uint32 (asf_demux, &stream->type_specific_size) &&
      _read_uint32 (asf_demux, &stream->stream_specific_size) &&
      _read_uint16 (asf_demux, &stream->id) &&
      _read_uint32 (asf_demux, &stream->unknown2));
}

static gboolean
_read_replicated_data (GstASFDemux * asf_demux, asf_replicated_data * rep)
{
  return (_read_uint32 (asf_demux, &rep->object_size) &&
      _read_uint32 (asf_demux, &rep->frag_timestamp));
}

static gboolean
_read_obj_data (GstASFDemux * asf_demux, asf_obj_data * object)
{
  return (_read_guid (asf_demux, &object->file_id) &&
      _read_uint64 (asf_demux, &object->packets) &&
      _read_uint8 (asf_demux, &object->unknown1) &&
          /*_read_uint8 (asf_demux, &object->unknown2) && */
      _read_uint8 (asf_demux, &object->correction));
}

static gboolean
_read_obj_data_correction (GstASFDemux * asf_demux,
    asf_obj_data_correction * object)
{
  return (_read_uint8 (asf_demux, &object->type) &&
      _read_uint8 (asf_demux, &object->cycle));
}

static gboolean
_read_obj_data_packet (GstASFDemux * asf_demux, asf_obj_data_packet * object)
{
  return (_read_uint8 (asf_demux, &object->flags) &&
      _read_uint8 (asf_demux, &object->property));
}

static gboolean
_read_stream_audio (GstASFDemux * asf_demux, asf_stream_audio * audio)
{
  /* WAVEFORMATEX Structure */
  return (_read_uint16 (asf_demux, &audio->codec_tag) && _read_uint16 (asf_demux, &audio->channels) && _read_uint32 (asf_demux, &audio->sample_rate) && _read_uint32 (asf_demux, &audio->byte_rate) && _read_uint16 (asf_demux, &audio->block_align) && _read_uint16 (asf_demux, &audio->word_size) && _read_uint16 (asf_demux, &audio->size)); /* Codec specific data size */
}

static gboolean
_read_stream_correction (GstASFDemux * asf_demux,
    asf_stream_correction * object)
{
  return (_read_uint8 (asf_demux, &object->span) &&
      _read_uint16 (asf_demux, &object->packet_size) &&
      _read_uint16 (asf_demux, &object->chunk_size) &&
      _read_uint16 (asf_demux, &object->data_size) &&
      _read_uint8 (asf_demux, &object->silence_data));
}

static gboolean
_read_stream_video (GstASFDemux * asf_demux, asf_stream_video * video)
{
  return (_read_uint32 (asf_demux, &video->width) &&
      _read_uint32 (asf_demux, &video->height) &&
      _read_uint8 (asf_demux, &video->unknown) &&
      _read_uint16 (asf_demux, &video->size));
}

static gboolean
_read_stream_video_format (GstASFDemux * asf_demux,
    asf_stream_video_format * fmt)
{
  return (_read_uint32 (asf_demux, &fmt->size) &&
      _read_uint32 (asf_demux, &fmt->width) &&
      _read_uint32 (asf_demux, &fmt->height) &&
      _read_uint16 (asf_demux, &fmt->planes) &&
      _read_uint16 (asf_demux, &fmt->depth) &&
      _read_uint32 (asf_demux, &fmt->tag) &&
      _read_uint32 (asf_demux, &fmt->image_size) &&
      _read_uint32 (asf_demux, &fmt->xpels_meter) &&
      _read_uint32 (asf_demux, &fmt->ypels_meter) &&
      _read_uint32 (asf_demux, &fmt->num_colors) &&
      _read_uint32 (asf_demux, &fmt->imp_colors));
}







static gboolean
gst_asf_demux_process_file (GstASFDemux * asf_demux, guint64 * obj_size)
{
  asf_obj_file object;

  /* Get the rest of the header's header */
  _read_obj_file (asf_demux, &object);
  if (object.min_pktsize == object.max_pktsize)
    asf_demux->packet_size = object.max_pktsize;
  else {
    asf_demux->packet_size = (guint32) - 1;
    GST_WARNING_OBJECT (asf_demux, "Non-const packet size, seeking disabled");
  }
  asf_demux->play_time = (guint64) object.play_time * (GST_SECOND / 10000000);
  asf_demux->preroll = object.preroll;

  GST_INFO ("Object is a file with %" G_GUINT64_FORMAT " data packets",
      object.packets_count);

  return TRUE;
}

static gboolean
gst_asf_demux_process_bitrate_props_object (GstASFDemux * asf_demux,
    guint64 * obj_size)
{
  guint16 num_streams;
  guint8 stream_id;
  guint16 i;
  asf_bitrate_record bitrate_record;

  if (!_read_uint16 (asf_demux, &num_streams))
    return FALSE;

  GST_INFO ("Object is a bitrate properties object with %u streams.",
      num_streams);

  for (i = 0; i < num_streams; i++) {
    _read_bitrate_record (asf_demux, &bitrate_record);
    stream_id = bitrate_record.stream_id & 0x7f;
    asf_demux->bitrate[stream_id] = bitrate_record.bitrate;
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_comment (GstASFDemux * asf_demux, guint64 * obj_size)
{
  asf_obj_comment object;
  GstByteStream *bs = asf_demux->bs;
  gchar *utf8_comments[5] = { NULL, NULL, NULL, NULL, NULL };
  guchar *data;
  const gchar *tags[5] = { GST_TAG_TITLE, GST_TAG_ARTIST, GST_TAG_COPYRIGHT,
    GST_TAG_COMMENT, NULL       /* ? */
  };
  guint16 *lengths = (guint16 *) & object;
  gint i;
  gsize in, out;
  GstTagList *taglist;
  GValue value = { 0 };
  gboolean have_tags = FALSE;

  GST_INFO ("Object is a comment.");

  /* Get the rest of the comment's header */
  _read_obj_comment (asf_demux, &object);
  GST_DEBUG
      ("Comment lengths: title=%d author=%d copyright=%d description=%d rating=%d",
      object.title_length, object.author_length, object.copyright_length,
      object.description_length, object.rating_length);
  for (i = 0; i < 5; i++) {
    /* might be just '/0', '/0'... */
    if (lengths[i] > 2 && lengths[i] % 2 == 0) {
      if (gst_bytestream_peek_bytes (bs, &data, lengths[i]) != lengths[i])
        goto fail;

      /* convert to UTF-8 */
      utf8_comments[i] = g_convert (data, lengths[i],
          "UTF-8", "UTF-16LE", &in, &out, NULL);

      gst_bytestream_flush_fast (bs, lengths[i]);
    } else {
      if (lengths[i] > 0 && !gst_bytestream_flush (bs, lengths[i]))
        goto fail;
    }
  }

  /* parse metadata into taglist */
  taglist = gst_tag_list_new ();
  g_value_init (&value, G_TYPE_STRING);
  for (i = 0; i < 5; i++) {
    if (utf8_comments[i] && tags[i]) {
      have_tags = TRUE;
      g_value_set_string (&value, utf8_comments[i]);
      gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND,
          tags[i], &value, NULL);
      g_free (utf8_comments[i]);
    }
  }
  g_value_unset (&value);

  if (have_tags) {
    GstElement *element = GST_ELEMENT (asf_demux);
    GstEvent *event;
    const GList *padlist;

    gst_element_found_tags (element, taglist);
    event = gst_event_new_tag (taglist);

    for (padlist = gst_element_get_pad_list (element);
        padlist != NULL; padlist = padlist->next) {
      if (GST_PAD_IS_SRC (padlist->data) && GST_PAD_IS_USABLE (padlist->data)) {
        gst_event_ref (event);
        gst_pad_push (GST_PAD (padlist->data), GST_DATA (event));
      }
    }

    gst_event_unref (event);
  } else {
    gst_tag_list_free (taglist);
  }

  return TRUE;

fail:
  for (i = 0; i < 5; i++)
    g_free (utf8_comments[i]);
  return FALSE;
}

static gboolean
gst_asf_demux_process_header (GstASFDemux * asf_demux, guint64 * obj_size)
{
  asf_obj_header object;
  guint32 i;

  /* Get the rest of the header's header */
  _read_obj_header (asf_demux, &object);

  GST_INFO ("Object is a header with %u parts", object.num_objects);

  /* Loop through the header's objects, processing those */
  for (i = 0; i < object.num_objects; i++) {
    if (!gst_asf_demux_process_object (asf_demux)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_header_ext (GstASFDemux * asf_demux, guint64 * obj_size)
{
  asf_obj_header_ext object;
  guint64 original_offset;

  /* Get the rest of the header's header */
  _read_obj_header_ext (asf_demux, &object);

  GST_INFO ("Object is an extended header with a size of %u bytes",
      object.data_size);

  original_offset = asf_demux->bs->offset;

  while ((asf_demux->bs->offset - original_offset) < object.data_size) {
    if (!gst_asf_demux_process_object (asf_demux))
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_segment (GstASFDemux * asf_demux,
    asf_packet_info * packet_info)
{
  guint8 byte;
  gboolean key_frame;
  guint32 replic_size;
  guint8 time_delta;
  guint32 time_start;
  guint32 frag_size;
  guint32 rsize;
  asf_segment_info segment_info;

  _read_uint8 (asf_demux, &byte);
  rsize = 1;
  segment_info.stream_number = byte & 0x7f;
  key_frame = (byte & 0x80) >> 7;

  GST_INFO ("Processing segment for stream %u", segment_info.stream_number);
  segment_info.sequence =
      _read_var_length (asf_demux, packet_info->seqtype, &rsize);
  segment_info.frag_offset =
      _read_var_length (asf_demux, packet_info->fragoffsettype, &rsize);
  replic_size =
      _read_var_length (asf_demux, packet_info->replicsizetype, &rsize);
  GST_DEBUG ("sequence = %x, frag_offset = %x, replic_size = %x",
      segment_info.sequence, segment_info.frag_offset, replic_size);

  if (replic_size > 1) {
    asf_replicated_data replicated_data_header;

    segment_info.compressed = FALSE;

    /* It's uncompressed with replic data */
    if (replic_size < 8) {
      GST_ELEMENT_ERROR (asf_demux, STREAM, DEMUX, (NULL),
          ("The payload has replicated data but the size is less than 8"));
      return FALSE;
    }
    _read_replicated_data (asf_demux, &replicated_data_header);
    segment_info.frag_timestamp = replicated_data_header.frag_timestamp;
    segment_info.segment_size = replicated_data_header.object_size;

    if (replic_size > 8) {
      gst_bytestream_flush (asf_demux->bs, replic_size - 8);
    }

    rsize += replic_size;
  } else {
    if (replic_size == 1) {
      /* It's compressed */
      segment_info.compressed = TRUE;
      _read_uint8 (asf_demux, &time_delta);
      rsize++;
      GST_DEBUG ("time_delta %u", time_delta);
    } else {
      segment_info.compressed = FALSE;
    }

    time_start = segment_info.frag_offset;
    segment_info.frag_offset = 0;
    segment_info.frag_timestamp = asf_demux->timestamp;
  }

  GST_DEBUG ("multiple = %u compressed = %u", packet_info->multiple,
      segment_info.compressed);

  if (packet_info->multiple) {
    frag_size = _read_var_length (asf_demux, packet_info->segsizetype, &rsize);
  } else {
    frag_size = packet_info->size_left - rsize;
  }

  packet_info->size_left -= rsize;

  GST_DEBUG ("size left = %u frag size = %u rsize = %u", packet_info->size_left,
      frag_size, rsize);

  if (segment_info.compressed) {
    while (frag_size > 0) {
      _read_uint8 (asf_demux, &byte);
      packet_info->size_left--;
      segment_info.chunk_size = byte;
      segment_info.segment_size = segment_info.chunk_size;

      if (segment_info.chunk_size > packet_info->size_left) {
        GST_ELEMENT_ERROR (asf_demux, STREAM, DEMUX, (NULL),
            ("Payload chunk overruns packet size."));
        return FALSE;
      }

      if (!gst_asf_demux_process_chunk (asf_demux, packet_info, &segment_info))
        return FALSE;

      frag_size -= segment_info.chunk_size + 1;
    }
  } else {
    segment_info.chunk_size = frag_size;
    if (!gst_asf_demux_process_chunk (asf_demux, packet_info, &segment_info))
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_data (GstASFDemux * asf_demux, guint64 * obj_size)
{
  asf_obj_data object;

  /* Get the rest of the header */
  _read_obj_data (asf_demux, &object);

  GST_INFO ("Object is data with %" G_GUINT64_FORMAT " packets",
      object.packets);

  gst_element_no_more_pads (GST_ELEMENT (asf_demux));
  asf_demux->state = GST_ASF_DEMUX_STATE_DATA;
  asf_demux->packet = 0;
  asf_demux->num_packets = object.packets;
  asf_demux->data_size = *obj_size;
  asf_demux->data_offset = gst_bytestream_tell (asf_demux->bs);

  return TRUE;
}

static gboolean
gst_asf_demux_handle_data (GstASFDemux * asf_demux)
{
  asf_obj_data_packet packet_properties_object;
  gboolean correction;
  guint8 buf;
  guint32 sequence;
  guint32 packet_length;
  guint16 duration;
  guint8 segment;
  guint8 segments;
  guint8 flags;
  guint8 property;
  asf_packet_info packet_info;
  guint32 rsize;
  gint n;

  /* handle seek, if any */
  if (GST_CLOCK_TIME_IS_VALID (asf_demux->seek_pending) &&
      asf_demux->packet_size != 0) {
    guint64 packet_seek = asf_demux->num_packets *
        asf_demux->seek_pending / asf_demux->play_time;

    if (packet_seek > asf_demux->num_packets)
      packet_seek = asf_demux->num_packets;

    if (gst_bytestream_seek (asf_demux->bs, packet_seek *
            asf_demux->packet_size + asf_demux->data_offset,
            GST_SEEK_METHOD_SET)) {
      asf_demux->packet = packet_seek;

      for (n = 0; n < asf_demux->num_streams; n++) {
        if (asf_demux->stream[n].frag_offset > 0) {
          gst_buffer_unref (asf_demux->stream[n].payload);
          asf_demux->stream[n].frag_offset = 0;
        }
      }

      asf_demux->seek_discont = TRUE;
    }

    asf_demux->seek_pending = GST_CLOCK_TIME_NONE;
  }

  GST_INFO ("Process packet");

  if (asf_demux->packet++ >= asf_demux->num_packets) {
    GstEvent *event;
    guint32 remaining;

    gst_bytestream_flush (asf_demux->bs, 0xFFFFFF);
    gst_bytestream_get_status (asf_demux->bs, &remaining, &event);
    if (!event || GST_EVENT_TYPE (event) != GST_EVENT_EOS)
      g_warning ("No EOS");
    if (event)
      gst_event_unref (event);

    return gst_asf_demux_handle_sink_event (asf_demux,
        gst_event_new (GST_EVENT_EOS), 0);
  }

  _read_uint8 (asf_demux, &buf);
  rsize = 1;
  if (buf & 0x80) {
    asf_obj_data_correction correction_object;

    /* Uses error correction */
    correction = TRUE;
    GST_DEBUG ("Data has error correction (%x)", buf);
    _read_obj_data_correction (asf_demux, &correction_object);
    rsize += 2;
  }

  /* Read the packet flags */
  _read_obj_data_packet (asf_demux, &packet_properties_object);
  rsize += 2;
  flags = packet_properties_object.flags;
  property = packet_properties_object.property;

  packet_info.multiple = flags & 0x01;
  sequence = _read_var_length (asf_demux, (flags >> 1) & 0x03, &rsize);
  packet_info.padsize =
      _read_var_length (asf_demux, (flags >> 3) & 0x03, &rsize);
  packet_length = _read_var_length (asf_demux, (flags >> 5) & 0x03, &rsize);
  if (packet_length == 0)
    packet_length = asf_demux->packet_size;

  GST_DEBUG ("Multiple = %u, Sequence = %u, Padsize = %u, Packet length = %u",
      packet_info.multiple, sequence, packet_info.padsize, packet_length);

  /* Read the property flags */
  packet_info.replicsizetype = property & 0x03;
  packet_info.fragoffsettype = (property >> 2) & 0x03;
  packet_info.seqtype = (property >> 4) & 0x03;

  _read_uint32 (asf_demux, &asf_demux->timestamp);
  _read_uint16 (asf_demux, &duration);

  rsize += 6;

  GST_DEBUG ("Timestamp = %x, Duration = %x", asf_demux->timestamp, duration);

  if (packet_info.multiple) {
    /* There are multiple payloads */
    _read_uint8 (asf_demux, &buf);
    rsize++;
    packet_info.segsizetype = (buf >> 6) & 0x03;
    segments = buf & 0x3f;
  } else {
    packet_info.segsizetype = 2;
    segments = 1;
  }

  packet_info.size_left = packet_length - packet_info.padsize - rsize;

  GST_DEBUG ("rsize: %u size left: %u", rsize, packet_info.size_left);

  for (segment = 0; segment < segments; segment++) {
    if (!gst_asf_demux_process_segment (asf_demux, &packet_info))
      return FALSE;
  }

  /* Skip the padding */
  if (packet_info.padsize > 0)
    gst_bytestream_flush (asf_demux->bs, packet_info.padsize);


  GST_DEBUG ("Remaining size left: %u", packet_info.size_left);

  if (packet_info.size_left > 0)
    gst_bytestream_flush (asf_demux->bs, packet_info.size_left);

  return TRUE;
}

static gboolean
gst_asf_demux_process_stream (GstASFDemux * asf_demux, guint64 * obj_size)
{
  asf_obj_stream object;
  guint32 stream_id;
  guint32 correction;
  asf_stream_audio audio_object;
  asf_stream_correction correction_object;
  asf_stream_video video_object;
  asf_stream_video_format video_format_object;
  guint16 size;
  guint16 id;

  /* Get the rest of the header's header */
  _read_obj_stream (asf_demux, &object);

  /* Identify the stream type */
  stream_id =
      gst_asf_demux_identify_guid (asf_demux, asf_stream_guids, &(object.type));
  correction =
      gst_asf_demux_identify_guid (asf_demux, asf_correction_guids,
      &(object.correction));
  id = object.id;

  switch (stream_id) {
    case ASF_STREAM_AUDIO:
      _read_stream_audio (asf_demux, &audio_object);
      size = audio_object.size;

      GST_INFO ("Object is an audio stream with %u bytes of additional data.",
          size);

      if (!gst_asf_demux_add_audio_stream (asf_demux, &audio_object, id))
        return FALSE;

      switch (correction) {
        case ASF_CORRECTION_ON:
          GST_INFO ("Using error correction");

          _read_stream_correction (asf_demux, &correction_object);
          asf_demux->span = correction_object.span;

          GST_DEBUG ("Descrambling: ps:%d cs:%d ds:%d s:%d sd:%d",
              correction_object.packet_size, correction_object.chunk_size,
              correction_object.data_size, (guint) correction_object.span,
              (guint) correction_object.silence_data);

          if (asf_demux->span > 1) {
            if (!correction_object.chunk_size
                || ((correction_object.packet_size /
                        correction_object.chunk_size) <= 1))
              /* Disable descrambling */
              asf_demux->span = 0;
          } else {
            /* Descambling is enabled */
            asf_demux->ds_packet_size = correction_object.packet_size;
            asf_demux->ds_chunk_size = correction_object.chunk_size;
          }

          /* Now skip the rest of the silence data */
          if (correction_object.data_size > 1)
            gst_bytestream_flush (asf_demux->bs,
                correction_object.data_size - 1);

          break;
        case ASF_CORRECTION_OFF:
          GST_INFO ("Error correction off");
          gst_bytestream_flush (asf_demux->bs, object.stream_specific_size);
          break;
        default:
          GST_ELEMENT_ERROR (asf_demux, STREAM, DEMUX, (NULL),
              ("Audio stream using unknown error correction"));
          return FALSE;
      }

      break;
    case ASF_STREAM_VIDEO:
      _read_stream_video (asf_demux, &video_object);
      size = video_object.size - 40;    /* Byte order gets offset by single byte */
      GST_INFO ("Object is a video stream with %u bytes of additional data.",
          size);
      _read_stream_video_format (asf_demux, &video_format_object);

      if (!gst_asf_demux_add_video_stream (asf_demux, &video_format_object, id))
        return FALSE;
      break;
    default:
      GST_ELEMENT_ERROR (asf_demux, STREAM, WRONG_TYPE, (NULL),
          ("unknown asf stream (id %08x)", (guint) stream_id));
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_asf_demux_skip_object (GstASFDemux * asf_demux, guint64 * obj_size)
{
  GstByteStream *bs = asf_demux->bs;

  GST_INFO ("Skipping object...");

  gst_bytestream_flush (bs, *obj_size - 24);

  return TRUE;
}

static inline gboolean
_read_object_header (GstASFDemux * asf_demux, guint32 * obj_id,
    guint64 * obj_size)
{
  ASFGuid guid;

  /* First get the GUID */
  if (!_read_guid (asf_demux, &guid))
    return FALSE;
  *obj_id = gst_asf_demux_identify_guid (asf_demux, asf_object_guids, &guid);

  if (!_read_uint64 (asf_demux, obj_size))
    return FALSE;

  if (*obj_id == ASF_OBJ_UNDEFINED) {
    GST_WARNING_OBJECT (asf_demux,
        "Could not identify object (0x%08x/0x%08x/0x%08x/0x%08x) with size=%llu",
        guid.v1, guid.v2, guid.v3, guid.v4, *obj_size);
    return TRUE;
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_object (GstASFDemux * asf_demux)
{

  guint32 obj_id;
  guint64 obj_size;

  if (!_read_object_header (asf_demux, &obj_id, &obj_size)) {
    GST_DEBUG ("  *****  Error reading object at filepos %" G_GUINT64_FORMAT
        " (EOS?)\n", /**filepos*/ gst_bytestream_tell (asf_demux->bs));
    gst_asf_demux_handle_sink_event (asf_demux, gst_event_new (GST_EVENT_EOS),
        0);
    return FALSE;
  }

  GST_INFO ("Found object %u with size %" G_GUINT64_FORMAT, obj_id, obj_size);

  switch (obj_id) {
    case ASF_OBJ_STREAM:
      return gst_asf_demux_process_stream (asf_demux, &obj_size);
    case ASF_OBJ_DATA:
      gst_asf_demux_process_data (asf_demux, &obj_size);
      /* This is the last object */
      return FALSE;
    case ASF_OBJ_FILE:
      return gst_asf_demux_process_file (asf_demux, &obj_size);
    case ASF_OBJ_HEADER:
      return gst_asf_demux_process_header (asf_demux, &obj_size);
    case ASF_OBJ_COMMENT:
      return gst_asf_demux_process_comment (asf_demux, &obj_size);
    case ASF_OBJ_HEAD1:
      return gst_asf_demux_process_header_ext (asf_demux, &obj_size);
    case ASF_OBJ_BITRATE_PROPS:
      return gst_asf_demux_process_bitrate_props_object (asf_demux, &obj_size);
    case ASF_OBJ_CONCEAL_NONE:
    case ASF_OBJ_HEAD2:
    case ASF_OBJ_UNDEFINED:
    case ASF_OBJ_CODEC_COMMENT:
    case ASF_OBJ_INDEX:
    case ASF_OBJ_PADDING:
    case ASF_OBJ_EXT_CONTENT_DESC:
    case ASF_OBJ_BITRATE_MUTEX:
    case ASF_OBJ_LANGUAGE_LIST:
    case ASF_OBJ_METADATA_OBJECT:
    case ASF_OBJ_EXTENDED_STREAM_PROPS:
    default:
      /* unknown/unhandled object read, just ignore it, we hate fatal errors */
      return gst_asf_demux_skip_object (asf_demux, &obj_size);
  }

  return TRUE;
}


static asf_stream_context *
gst_asf_demux_get_stream (GstASFDemux * asf_demux, guint16 id)
{
  guint8 i;
  asf_stream_context *stream;

  for (i = 0; i < asf_demux->num_streams; i++) {
    stream = &asf_demux->stream[i];
    if (stream->id == id) {
      /* We've found the one with the matching id */
      return &asf_demux->stream[i];
    }
  }

  /* Base case if we haven't found one at all */
  GST_WARNING_OBJECT (asf_demux,
      "Segment found for undefined stream: (%d)", id);

  return NULL;
}

static inline void
gst_asf_demux_descramble_segment (GstASFDemux * asf_demux,
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

  offset = 0;

  for (offset = 0; offset < segment_info->segment_size;
      offset += asf_demux->ds_chunk_size) {
    off = offset / asf_demux->ds_chunk_size;
    row = off / asf_demux->span;
    col = off % asf_demux->span;
    idx = row + col * asf_demux->ds_packet_size / asf_demux->ds_chunk_size;
    sub_buffer =
        gst_buffer_create_sub (scrambled_buffer, idx * asf_demux->ds_chunk_size,
        asf_demux->ds_chunk_size);
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
gst_asf_demux_process_chunk (GstASFDemux * asf_demux,
    asf_packet_info * packet_info, asf_segment_info * segment_info)
{
  asf_stream_context *stream;
  guint32 got_bytes;
  GstByteStream *bs = asf_demux->bs;
  GstBuffer *buffer;

  if (!(stream =
          gst_asf_demux_get_stream (asf_demux, segment_info->stream_number)))
    goto done;

  GST_DEBUG ("Processing chunk of size %u (fo = %d)", segment_info->chunk_size,
      stream->frag_offset);

  if (segment_info->frag_offset == 0) {
    /* new packet */
    stream->sequence = segment_info->sequence;
    asf_demux->pts = segment_info->frag_timestamp - asf_demux->preroll;
    got_bytes = gst_bytestream_peek (bs, &buffer, segment_info->chunk_size);
    GST_DEBUG ("BUFFER: Copied stream to buffer (%p - %d)", buffer,
        GST_BUFFER_REFCOUNT_VALUE (buffer));
    stream->payload = buffer;
  } else {
    GST_DEBUG
        ("segment_info->sequence = %d, stream->sequence = %d, segment_info->frag_offset = %d, stream->frag_offset = %d",
        segment_info->sequence, stream->sequence, segment_info->frag_offset,
        stream->frag_offset);
    if (segment_info->sequence == stream->sequence &&
        segment_info->frag_offset == stream->frag_offset) {
      GstBuffer *new_buffer;

      /* continuing packet */
      GST_INFO ("A continuation packet");
      got_bytes = gst_bytestream_peek (bs, &buffer, segment_info->chunk_size);
      GST_DEBUG ("Copied stream to buffer (%p - %d)", buffer,
          GST_BUFFER_REFCOUNT_VALUE (buffer));
      new_buffer = gst_buffer_merge (stream->payload, buffer);
      GST_DEBUG
          ("BUFFER: Merged new_buffer (%p - %d) from stream->payload(%p - %d) and buffer (%p - %d)",
          new_buffer, GST_BUFFER_REFCOUNT_VALUE (new_buffer), stream->payload,
          GST_BUFFER_REFCOUNT_VALUE (stream->payload), buffer,
          GST_BUFFER_REFCOUNT_VALUE (buffer));
      gst_buffer_unref (stream->payload);
      gst_buffer_unref (buffer);
      stream->payload = new_buffer;
    } else {
      /* cannot continue current packet: free it */
      if (stream->frag_offset != 0) {
        /* cannot create new packet */
        GST_DEBUG ("BUFFER: Freeing stream->payload (%p)", stream->payload);
        gst_buffer_unref (stream->payload);
        gst_bytestream_flush (bs, segment_info->chunk_size);
        packet_info->size_left -= segment_info->chunk_size;
        stream->frag_offset = 0;
      }
      asf_demux->pts = segment_info->frag_timestamp - asf_demux->preroll;
      goto done;
      //return TRUE;
      //} else {
      /* create new packet */
      //stream->sequence = segment_info->sequence;
      //}
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
    if (asf_demux->span > 1) {
      gst_asf_demux_descramble_segment (asf_demux, segment_info, stream);
    }

    if (GST_PAD_IS_USABLE (stream->pad)) {
      GST_DEBUG ("New buffer is at: %p size: %u",
          GST_BUFFER_DATA (stream->payload), GST_BUFFER_SIZE (stream->payload));

      GST_BUFFER_TIMESTAMP (stream->payload) =
          (GST_SECOND / 1000) * asf_demux->pts;

      /*!!! Should handle flush events here? */
      GST_DEBUG ("Sending stream %d of size %d", stream->id,
          segment_info->chunk_size);

      GST_INFO ("Pushing pad");

      if (asf_demux->seek_discont) {
        if (asf_demux->seek_flush) {
          gst_pad_event_default (asf_demux->sinkpad,
              gst_event_new (GST_EVENT_FLUSH));
        }

        gst_pad_event_default (asf_demux->sinkpad,
            gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
                GST_BUFFER_TIMESTAMP (stream->payload), GST_FORMAT_UNDEFINED));

        asf_demux->seek_discont = FALSE;
      }

      gst_pad_push (stream->pad, GST_DATA (stream->payload));
    }

    stream->frag_offset = 0;
  }
done:
  gst_bytestream_flush (bs, segment_info->chunk_size);
  packet_info->size_left -= segment_info->chunk_size;

  return TRUE;
}

/*
 * Event stuff
 */

static gboolean
gst_asf_demux_handle_sink_event (GstASFDemux * asf_demux,
    GstEvent * event, guint32 remaining)
{
  gboolean ret = TRUE;

  if (!event) {
    GST_ELEMENT_ERROR (asf_demux, RESOURCE, READ, (NULL), (NULL));
    return FALSE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_pad_event_default (asf_demux->sinkpad, event);
      return FALSE;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint i;
      GstEvent *discont;

      for (i = 0; i < asf_demux->num_streams; i++) {
        asf_stream_context *stream = &asf_demux->stream[i];

        if (GST_PAD_IS_USABLE (stream->pad)) {
          GST_DEBUG ("sending discont on stream %d with %" GST_TIME_FORMAT
              " + %" GST_TIME_FORMAT " = %" GST_TIME_FORMAT,
              i, GST_TIME_ARGS (asf_demux->last_seek),
              GST_TIME_ARGS (stream->delay),
              GST_TIME_ARGS (asf_demux->last_seek + stream->delay));
          discont =
              gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
              asf_demux->last_seek + stream->delay, NULL);
          gst_pad_push (stream->pad, GST_DATA (discont));
        }
      }
      break;
    }
    case GST_EVENT_FLUSH:
      GST_WARNING_OBJECT (asf_demux, "flush event");
      break;
    default:
      GST_WARNING_OBJECT (asf_demux, "unhandled event %d",
          GST_EVENT_TYPE (event));
      break;
  }

  gst_event_unref (event);

  return ret;
}

static gboolean
gst_asf_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstASFDemux *asf_demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG ("asfdemux: handle_src_event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_BYTES:
        case GST_FORMAT_DEFAULT:
          res = FALSE;
          break;
        case GST_FORMAT_TIME:
          asf_demux->seek_pending = GST_EVENT_SEEK_OFFSET (event);
          asf_demux->seek_flush = (GST_EVENT_SEEK_FLAGS (event) &
              GST_SEEK_FLAG_FLUSH) ? TRUE : FALSE;
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstEventMask *
gst_asf_demux_get_src_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT},
    {0,}
  };

  return masks;
}

static const GstFormat *
gst_asf_demux_get_src_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    0
  };

  return formats;
}

static const GstQueryType *
gst_asf_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_asf_demux_handle_src_query (GstPad * pad,
    GstQueryType type, GstFormat * format, gint64 * value)
{
  GstASFDemux *asf_demux;
  gboolean res = TRUE;

  asf_demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          *value = asf_demux->play_time;
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          *value = (GST_SECOND / 1000) * asf_demux->timestamp;
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    default:
      res = FALSE;
      break;
  }

  return res;
}


static void
gst_asf_demux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstASFDemux *src;

  g_return_if_fail (GST_IS_ASF_DEMUX (object));

  src = GST_ASF_DEMUX (object);

  switch (prop_id) {
    default:
      break;
  }
}

static GstElementStateReturn
gst_asf_demux_change_state (GstElement * element)
{
  GstASFDemux *asf_demux = GST_ASF_DEMUX (element);
  gint i;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      asf_demux->bs = gst_bytestream_new (asf_demux->sinkpad);
      asf_demux->last_seek = 0;
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (asf_demux->bs);
      for (i = 0; i < GST_ASF_DEMUX_NUM_VIDEO_PADS; i++) {
        asf_demux->video_PTS[i] = 0;
      }
      for (i = 0; i < GST_ASF_DEMUX_NUM_AUDIO_PADS; i++) {
        asf_demux->audio_PTS[i] = 0;
      }
      asf_demux->state = GST_ASF_DEMUX_STATE_HEADER;
      asf_demux->seek_pending = GST_CLOCK_TIME_NONE;
      asf_demux->seek_discont = FALSE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static guint32
gst_asf_demux_identify_guid (GstASFDemux * asf_demux,
    ASFGuidHash * guids, ASFGuid * guid)
{
  guint32 i;

  GST_LOG_OBJECT (asf_demux, "identifying 0x%08x/0x%08x/0x%08x/0x%08x",
      guid->v1, guid->v2, guid->v3, guid->v4);
  i = 0;
  while (guids[i].obj_id != ASF_OBJ_UNDEFINED) {
    if (guids[i].guid.v1 == guid->v1 &&
        guids[i].guid.v2 == guid->v2 &&
        guids[i].guid.v3 == guid->v3 && guids[i].guid.v4 == guid->v4) {
      return guids[i].obj_id;
    }
    i++;
  }

  /* The base case if none is found */
  return ASF_OBJ_UNDEFINED;
}


static GstCaps *
gst_asf_demux_audio_caps (guint16 codec_id,
    asf_stream_audio * audio, guint8 * extradata, char **codec_name)
{
  GstCaps *caps;
  gint flags1, flags2;

  flags1 = 0;
  flags2 = 0;

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3:  /* mp3 */
      caps = gst_caps_from_string ("audio/mpeg, mpegversion = (int) 1, "
          "layer = (int) 3");
      if (codec_name)
        *codec_name = g_strdup ("MPEG 1 layer 3");
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = gst_caps_from_string ("audio/mpeg, mpegversion = (int) 1, "
          "layer = (int) 2");
      if (codec_name)
        *codec_name = g_strdup ("MPEG 1 layer 2");
      break;

    case GST_RIFF_WAVE_FORMAT_PCM:     /* PCM/wav */  {
      caps = gst_caps_from_string ("audio/x-raw-int, "
          "endianness = (int) LITTLE_ENDIAN,"
          "signed = (boolean) { true, false }, "
          "width = (int) { 8, 16 }, " "depth = (int) { 8, 16 }");

      if (audio != NULL) {
        gint ba = audio->block_align;
        gint ch = audio->channels;
        gint ws = audio->word_size;

        gst_caps_set_simple (caps,
            "width", G_TYPE_INT, (int) (ba * 8 / ch),
            "depth", G_TYPE_INT, ws, "signed", G_TYPE_BOOLEAN, (ws != 8), NULL);
      }
      if (codec_name)
        *codec_name = g_strdup ("PCM WAV");
    }
      break;

    case GST_RIFF_WAVE_FORMAT_VORBIS1: /* vorbis mode 1 */
    case GST_RIFF_WAVE_FORMAT_VORBIS2: /* vorbis mode 2 */
    case GST_RIFF_WAVE_FORMAT_VORBIS3: /* vorbis mode 3 */
    case GST_RIFF_WAVE_FORMAT_VORBIS1PLUS:     /* vorbis mode 1+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS2PLUS:     /* vorbis mode 2+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS3PLUS:     /* vorbis mode 3+ */
      caps = gst_caps_from_string ("audio/x-vorbis");
      if (codec_name)
        *codec_name = g_strdup ("Vorbis");
      break;

    case GST_RIFF_WAVE_FORMAT_A52:
      caps = gst_caps_from_string ("audio/x-ac3");
      if (codec_name)
        *codec_name = g_strdup ("AC3");
      break;

    case GST_RIFF_WAVE_FORMAT_WMAV1:
      caps = gst_caps_from_string ("audio/x-wma, "
          "wmaversion = (int) 1, "
          "block_align = (int) [ 0, MAX ], " "bitrate = (int) [ 0, MAX ]");
      if (audio != NULL) {
        GstBuffer *buffer;

        buffer = gst_buffer_new_and_alloc (audio->size);
        memcpy (GST_BUFFER_DATA (buffer), extradata, audio->size);
        /* gst_util_dump_mem (GST_BUFFER_DATA (buffer), audio->size); */

        gst_caps_set_simple (caps,
            "codec_data", GST_TYPE_BUFFER, buffer,
            "block_align", G_TYPE_INT, audio->block_align,
            "bitrate", G_TYPE_INT, audio->byte_rate * 8, NULL);
      }
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 7 (WMA1)");
      break;

    case GST_RIFF_WAVE_FORMAT_WMAV2:
      caps = gst_caps_from_string ("audio/x-wma, "
          "wmaversion = (int) 2, "
          "block_align = (int) [ 0, MAX ], " "bitrate = (int) [ 0, MAX ]");
      if (audio != NULL) {
        GstBuffer *buffer;

        buffer = gst_buffer_new_and_alloc (audio->size);
        memcpy (GST_BUFFER_DATA (buffer), extradata, audio->size);
        /* gst_util_dump_mem (GST_BUFFER_DATA (buffer), audio->size); */

        gst_caps_set_simple (caps,
            "codec_data", GST_TYPE_BUFFER, buffer,
            "block_align", G_TYPE_INT, audio->block_align,
            "bitrate", G_TYPE_INT, audio->byte_rate * 8, NULL);
        gst_data_unref (GST_DATA (buffer));
      }
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 8 (WMA2)");
      break;

    case GST_RIFF_WAVE_FORMAT_WMAV3:
      caps = gst_caps_from_string ("audio/x-wma, " "wmaversion = (int) 3");
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 9 (WMA3)");
      break;

    default:
      GST_WARNING ("asfdemux: unknown audio format 0x%04x", codec_id);
      return GST_CAPS_ANY;
      break;
  }

  if (audio != NULL) {
    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, audio->sample_rate,
        "channels", G_TYPE_INT, audio->channels, NULL);
  } else {
    gst_caps_set_simple (caps,
        "rate", GST_TYPE_INT_RANGE, 8000, 96000,
        "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
  }

  return caps;
}

static gboolean
gst_asf_demux_add_audio_stream (GstASFDemux * asf_demux,
    asf_stream_audio * audio, guint16 id)
{
  GstPad *src_pad;
  GstCaps *caps;
  gchar *name = NULL;
  guint16 size_left = 0;
  char *codec_name = NULL;
  GstTagList *list = gst_tag_list_new ();

  size_left = audio->size;

  /* Create the audio pad */
  name = g_strdup_printf ("audio_%02d", asf_demux->num_audio_streams);

  src_pad = gst_pad_new_from_template (audiosrctempl, name);
  g_free (name);

  gst_pad_use_explicit_caps (src_pad);

  /* Swallow up any left over data and set up the standard properties from the header info */
  if (size_left) {
    guint8 *extradata;

    GST_WARNING_OBJECT (asf_demux,
        "asfdemux: Audio header contains %d bytes of codec specific data",
        size_left);
    gst_bytestream_peek_bytes (asf_demux->bs, &extradata, size_left);
    caps = gst_asf_demux_audio_caps (audio->codec_tag, audio, extradata,
        &codec_name);
    gst_bytestream_flush (asf_demux->bs, size_left);
  } else {
    caps = gst_asf_demux_audio_caps (audio->codec_tag, audio, NULL,
        &codec_name);
  }

  /* Informing about that audio format we just added */
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, GST_TAG_AUDIO_CODEC,
      codec_name, NULL);
  gst_element_found_tags (GST_ELEMENT (asf_demux), list);
  gst_tag_list_free (list);
  if (codec_name)
    g_free (codec_name);

  GST_INFO ("Adding audio stream %u codec %u (0x%x)",
      asf_demux->num_video_streams, audio->codec_tag, audio->codec_tag);

  asf_demux->num_audio_streams++;

  return gst_asf_demux_setup_pad (asf_demux, src_pad, caps, id);
}

static GstCaps *
gst_asf_demux_video_caps (guint32 codec_fcc,
    asf_stream_video_format * video, guint8 * extradata, char **codec_name)
{
  GstCaps *caps = NULL;
  gint width = 0, height = 0;

  if (video != NULL) {
    width = video->width;
    height = video->height;
  }

  switch (codec_fcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, codec_fcc, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Raw, uncompressed I420");
      break;
    case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, codec_fcc, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Raw, uncompressed YUV 4:2:2");
      break;

    case GST_MAKE_FOURCC ('M', 'J', 'P', 'G'):
      caps = gst_caps_new_simple ("video/x-jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Motion JPEG");
      break;
    case GST_MAKE_FOURCC ('J', 'P', 'E', 'G'):
      caps = gst_caps_new_simple ("video/x-jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("JPEG Still Image");
      break;
    case GST_MAKE_FOURCC ('P', 'I', 'X', 'L'):
    case GST_MAKE_FOURCC ('V', 'I', 'X', 'L'):
      caps = gst_caps_new_simple ("video/x-jpeg", NULL);
      if (codec_name)
        *codec_name = g_strdup ("Miro/Pinnacle Video XL");
      break;

    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
      caps = gst_caps_new_simple ("video/x-dv",
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Generic DV");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '1'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 1, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 7 (WMV1)");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '2'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 2, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 8 (WMV2)");
      break;

    case GST_MAKE_FOURCC ('W', 'M', 'V', '3'):
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft Windows Media 9 (WMV3)");
      break;

    case GST_MAKE_FOURCC ('M', 'P', 'G', '4'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 41, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.1");
      break;

    case GST_MAKE_FOURCC ('M', 'P', '4', '2'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 42, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.2");
      break;

    case GST_MAKE_FOURCC ('M', 'P', '4', '3'):
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 43, NULL);
      if (codec_name)
        *codec_name = g_strdup ("Microsoft MPEG-4 4.3");
      break;

    case GST_MAKE_FOURCC ('D', 'I', 'V', '3'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '4'):
    case GST_MAKE_FOURCC ('D', 'I', 'V', '5'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 3, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 3");
      break;

    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', 'x'):
    case GST_MAKE_FOURCC ('D', 'X', '5', '0'):
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 5, NULL);
      if (codec_name)
        *codec_name = g_strdup ("DivX MPEG-4 Version 5");
      break;

    default:
      GST_WARNING ("asfdemux: unknown video format " GST_FOURCC_FORMAT
          "(0x%08x)", GST_FOURCC_ARGS (codec_fcc), codec_fcc);
      return NULL;
      break;
  }

  if (video != NULL) {
    gst_caps_set_simple (caps,
        "width", G_TYPE_INT, video->width,
        "height", G_TYPE_INT, video->height,
        "framerate", G_TYPE_DOUBLE, (double) 25, NULL);

    if (extradata) {
      GstBuffer *buffer;

      buffer = gst_buffer_new_and_alloc (video->size);
      memcpy (GST_BUFFER_DATA (buffer), extradata, video->size);
      /* gst_util_dump_mem (GST_BUFFER_DATA (buffer), video->size); */

      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buffer, NULL);
      gst_data_unref (GST_DATA (buffer));
    }
  } else {
    gst_caps_set_simple (caps,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_DOUBLE_RANGE, 25.0, G_MAXDOUBLE, NULL);
  }

  return caps;
}

static gboolean
gst_asf_demux_add_video_stream (GstASFDemux * asf_demux,
    asf_stream_video_format * video, guint16 id)
{
  GstPad *src_pad;
  GstCaps *caps;
  gchar *name = NULL;
  char *codec_name = NULL;
  GstTagList *list = gst_tag_list_new ();
  gint size_left = video->size - 40;

  /* Create the audio pad */
  name = g_strdup_printf ("video_%02d", asf_demux->num_video_streams);
  src_pad = gst_pad_new_from_template (videosrctempl, name);
  g_free (name);

  /* Now try some gstreamer formatted MIME types (from gst_avi_demux_strf_vids) */
  if (size_left) {
    guint8 *extradata;

    GST_WARNING_OBJECT (asf_demux,
        "asfdemux: Video header contains %d bytes of codec specific data",
        size_left);
    gst_bytestream_peek_bytes (asf_demux->bs, &extradata, size_left);
    caps = gst_asf_demux_video_caps (video->tag, video, extradata, &codec_name);
    gst_bytestream_flush (asf_demux->bs, size_left);
  } else {
    caps = gst_asf_demux_video_caps (video->tag, video, NULL, &codec_name);
  }
  gst_tag_list_add (list, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
      codec_name, NULL);
  gst_element_found_tags (GST_ELEMENT (asf_demux), list);
  gst_tag_list_free (list);
  if (codec_name)
    g_free (codec_name);
  GST_INFO ("Adding video stream %u codec " GST_FOURCC_FORMAT " (0x%08x)",
      asf_demux->num_video_streams, GST_FOURCC_ARGS (video->tag), video->tag);

  asf_demux->num_video_streams++;

  return gst_asf_demux_setup_pad (asf_demux, src_pad, caps, id);
}


static gboolean
gst_asf_demux_setup_pad (GstASFDemux * asf_demux,
    GstPad * src_pad, GstCaps * caps, guint16 id)
{
  asf_stream_context *stream;

  gst_pad_use_explicit_caps (src_pad);
  gst_pad_set_explicit_caps (src_pad, caps);
  gst_pad_set_formats_function (src_pad, gst_asf_demux_get_src_formats);
  gst_pad_set_event_mask_function (src_pad, gst_asf_demux_get_src_event_mask);
  gst_pad_set_event_function (src_pad, gst_asf_demux_handle_src_event);
  gst_pad_set_query_type_function (src_pad, gst_asf_demux_get_src_query_types);
  gst_pad_set_query_function (src_pad, gst_asf_demux_handle_src_query);

  stream = &asf_demux->stream[asf_demux->num_streams];
  stream->pad = src_pad;
  stream->id = id;
  stream->frag_offset = 0;
  stream->sequence = 0;
  stream->delay = 0LL;

  gst_pad_set_element_private (src_pad, stream);

  GST_INFO ("Adding pad for stream %u", asf_demux->num_streams);

  asf_demux->num_streams++;

  gst_element_add_pad (GST_ELEMENT (asf_demux), src_pad);

  return TRUE;
}
