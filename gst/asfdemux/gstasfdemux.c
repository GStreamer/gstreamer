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
#include "gstasfdemux.h"
#include "asfheaders.h"

/* elementfactory information */
static GstElementDetails gst_asf_demux_details = {
  "ASF Demuxer",
  "Codec/Demuxer",
  "Demultiplexes ASF Streams",
  "Owen Fraser-Green <owen@discobabe.net>",
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW ("asf_asf_demux_sink",
		"video/x-ms-asf",
		  NULL)
);
			  
static void	gst_asf_demux_base_init		(gpointer g_class);
static void 	gst_asf_demux_class_init	(GstASFDemuxClass *klass);
static void 	gst_asf_demux_init		(GstASFDemux *asf_demux);
static gboolean gst_asf_demux_send_event 	(GstElement *element, 
						 GstEvent *event);
static void 	gst_asf_demux_loop 		(GstElement *element);
static gboolean gst_asf_demux_process_object    (GstASFDemux *asf_demux);
static void     gst_asf_demux_get_property      (GObject *object, 
						 guint prop_id, 	
						 GValue *value, 
						 GParamSpec *pspec);
static guint32  gst_asf_demux_identify_guid     (GstASFDemux *asf_demux,
						 ASFGuidHash *guids,
						 ASFGuid *guid_raw);
static gboolean gst_asf_demux_process_chunk      (GstASFDemux *asf_demux,
						  asf_packet_info *packet_info,
						  asf_segment_info *segment_info);
static const GstEventMask* gst_asf_demux_get_src_event_mask  (GstPad *pad);
static gboolean 	   gst_asf_demux_handle_sink_event   (GstASFDemux *asf_demux,
							      GstEvent *event,
							      guint32 remaining);
static gboolean 	   gst_asf_demux_handle_src_event    (GstPad *pad,
							      GstEvent *event);
static const GstFormat*    gst_asf_demux_get_src_formats     (GstPad *pad); 
static const GstQueryType* gst_asf_demux_get_src_query_types (GstPad *pad);
static gboolean 	   gst_asf_demux_handle_src_query    (GstPad *pad,
							      GstQueryType type,
							      GstFormat *format, gint64 *value);
static gboolean            gst_asf_demux_add_video_stream (GstASFDemux *asf_demux,
							   asf_stream_video_format *video_format,
							   guint16 id);
static gboolean            gst_asf_demux_add_audio_stream (GstASFDemux *asf_demux,
							   asf_stream_audio *audio,
							   guint16 id);
static gboolean            gst_asf_demux_setup_pad        (GstASFDemux *asf_demux,
							   GstPad *src_pad,
							   GstCaps *caps_list,
							   guint16 id);

static GstElementStateReturn gst_asf_demux_change_state   (GstElement *element);
static GstCaps * gst_asf_demux_video_caps (guint32 codec_fcc,
					   asf_stream_video_format *video);
static GstCaps * gst_asf_demux_audio_caps (guint16 codec_id,
					   asf_stream_audio *audio, guint8 *extradata);

static GstPadTemplate *videosrctempl, *audiosrctempl;
static GstElementClass *parent_class = NULL;

GType
asf_demux_get_type (void)
{
  static GType asf_demux_type = 0;

  if (!asf_demux_type) {
    static const GTypeInfo asf_demux_info = {
      sizeof(GstASFDemuxClass),      
      gst_asf_demux_base_init,
      NULL,
      (GClassInitFunc)gst_asf_demux_class_init,
      NULL,
      NULL,
      sizeof(GstASFDemux),
      0,
      (GInstanceInitFunc)gst_asf_demux_init,
    };
    asf_demux_type = g_type_register_static(GST_TYPE_ELEMENT, "GstASFDemux", &asf_demux_info, 0);
  }
  return asf_demux_type;
}

static void
gst_asf_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  int i;
  GstCaps *audcaps = NULL, *vidcaps = NULL, *temp;
  guint32 vid_list[] = {
    GST_MAKE_FOURCC('I','4','2','0'),
    GST_MAKE_FOURCC('Y','U','Y','2'),
    GST_MAKE_FOURCC('M','J','P','G'),
    GST_MAKE_FOURCC('D','V','S','D'),
    GST_MAKE_FOURCC('W','M','V','1'),
    GST_MAKE_FOURCC('W','M','V','2'),
    GST_MAKE_FOURCC('M','P','G','4'),
    GST_MAKE_FOURCC('M','P','4','2'),
    GST_MAKE_FOURCC('M','P','4','3'),
    GST_MAKE_FOURCC('D','I','V','3'),
    GST_MAKE_FOURCC('D','X','5','0'),
    0 /* end */
  };
  gint aud_list[] = {
    GST_RIFF_WAVE_FORMAT_MPEGL3,
    GST_RIFF_WAVE_FORMAT_MPEGL12,
    GST_RIFF_WAVE_FORMAT_PCM,
    GST_RIFF_WAVE_FORMAT_VORBIS1,
    GST_RIFF_WAVE_FORMAT_A52,
    GST_RIFF_WAVE_FORMAT_DIVX_WMAV1,
    GST_RIFF_WAVE_FORMAT_DIVX_WMAV2,
    GST_RIFF_WAVE_FORMAT_WMAV9,
    -1 /* end */
  };

  for (i = 0; aud_list[i] != -1; i++) {
    temp = gst_asf_demux_audio_caps (aud_list[i], NULL, NULL);
    audcaps = gst_caps_append (audcaps, temp);
  }

  audiosrctempl = gst_pad_template_new ("audio_%02d",
					GST_PAD_SRC,
				        GST_PAD_SOMETIMES,
					audcaps, NULL);
  for (i = 0; vid_list[i] != 0; i++) {
    temp = gst_asf_demux_video_caps (vid_list[i], NULL);
    vidcaps = gst_caps_append (vidcaps, temp);
  }

  videosrctempl = gst_pad_template_new ("video_%02d",
					GST_PAD_SRC,
				      	GST_PAD_SOMETIMES,
					vidcaps, NULL);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, 
		  GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_class_set_details (element_class, &gst_asf_demux_details);
}

static void
gst_asf_demux_class_init (GstASFDemuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  gobject_class->get_property = gst_asf_demux_get_property;
  
  gstelement_class->change_state = gst_asf_demux_change_state;
  gstelement_class->send_event = gst_asf_demux_send_event;
}

static void
gst_asf_demux_init (GstASFDemux *asf_demux)
{
  guint i;

  asf_demux->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
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
  
  GST_FLAG_SET (asf_demux, GST_ELEMENT_EVENT_AWARE);
}

static gboolean
gst_asf_demux_send_event (GstElement *element, GstEvent *event)
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
gst_asf_demux_loop (GstElement *element)
{
  GstASFDemux *asf_demux;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ASF_DEMUX (element));

  asf_demux = GST_ASF_DEMUX (element);

  /* this is basically an infinite loop */
  gst_asf_demux_process_object (asf_demux);
}

static guint32 gst_asf_demux_read_var_length (GstASFDemux *asf_demux, guint8 type, guint32 *rsize)
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
    ret = GUINT32_FROM_LE(*(guint32 *)var) & 0xff;
    gst_bytestream_flush (bs, 1);
    *rsize += 1;
    break;
  case 2:
    ret = GUINT32_FROM_LE(*(guint32 *)var) & 0xffff;
    gst_bytestream_flush (bs, 2);
    *rsize += 2;
    break;
  case 3:
    ret = GUINT32_FROM_LE(*(guint32 *)var);
    gst_bytestream_flush (bs, 4);
    *rsize += 4;
    break;
  }

  return ret;
}

static void gst_asf_demux_read_object_header_rest (GstASFDemux *asf_demux, guint8 **buf, guint32 size) {
  guint32       got_bytes;
  GstByteStream *bs = asf_demux->bs;
  gboolean ret;
  GstEvent *event;
  guint32 remaining;

  do {
    got_bytes = gst_bytestream_peek_bytes (bs, buf, size);
    if (got_bytes == size) {
      gst_bytestream_flush (bs, size);
      return;
    }
    gst_bytestream_get_status (bs, &remaining, &event);
    ret = gst_asf_demux_handle_sink_event (asf_demux, event, remaining);
  } while (ret);
}

static gboolean
gst_asf_demux_process_file (GstASFDemux *asf_demux, guint64 *obj_size)
{
  asf_obj_file *object;
  guint8 *ptr;
  guint64 packets;
  
  /* Get the rest of the header's header */
  gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 80);
  object = (asf_obj_file *)ptr;
  packets = GUINT64_FROM_LE (object->packets_count);
  asf_demux->packet_size = GUINT32_FROM_LE (object->max_pktsize);
  asf_demux->play_time = (guint32) GUINT64_FROM_LE (object->play_time) / 10;
  asf_demux->preroll = GUINT64_FROM_LE (object->preroll);
  
  GST_INFO ( "Object is a file with %" G_GUINT64_FORMAT " data packets", packets);

  return TRUE;
}

static gboolean
gst_asf_demux_process_bitrate_props_object (GstASFDemux *asf_demux, guint64 *obj_size)
{
  guint32            got_bytes;
  GstBuffer          *buf;
  guint16            num_streams;
  guint8             stream_id;
  guint16            i;
  guint8             *ptr;
  asf_bitrate_record *bitrate_record;

  got_bytes = gst_bytestream_read (asf_demux->bs, &buf, 2);
  num_streams = GUINT16_FROM_LE (*GST_BUFFER_DATA (buf));

  GST_INFO ( "Object is a bitrate properties object with %u streams.", num_streams);

  for (i = 0; i < num_streams; i++) {
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 6);
    bitrate_record = (asf_bitrate_record *)ptr;
    stream_id = GUINT16_FROM_LE (bitrate_record->stream_id) & 0x7f;
    asf_demux->bitrate[stream_id] = GUINT32_FROM_LE (bitrate_record->bitrate);
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_comment (GstASFDemux *asf_demux, guint64 *obj_size)
{
  asf_obj_comment *object;
  guint16 title_length;
  guint16 author_length;
  guint16 copyright_length;
  guint16 description_length;
  guint16 rating_length;
  guint8 *ptr;
  GstByteStream *bs = asf_demux->bs;

  GST_INFO ( "Object is a comment.");  

  /* Get the rest of the comment's header */
  gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 10);
  object = (asf_obj_comment *)ptr;
  title_length = GUINT16_FROM_LE (object->title_length);
  author_length = GUINT16_FROM_LE (object->author_length);
  copyright_length = GUINT16_FROM_LE (object->copyright_length);
  description_length = GUINT16_FROM_LE (object->description_length);
  rating_length = GUINT16_FROM_LE (object->rating_length);
  GST_DEBUG ("Comment lengths: title=%d author=%d copyright=%d description=%d rating=%d", title_length, author_length, copyright_length, description_length, rating_length); 

  /* We don't do anything with them at the moment so just skip them */
  gst_bytestream_flush (bs, title_length);
  gst_bytestream_flush (bs, author_length);
  gst_bytestream_flush (bs, copyright_length);
  gst_bytestream_flush (bs, description_length);
  gst_bytestream_flush (bs, rating_length);

  return TRUE;
}


static gboolean
gst_asf_demux_process_header (GstASFDemux *asf_demux, guint64 *obj_size)
{
  guint32 num_objects;
  asf_obj_header *object;
  guint32 i;
  guint8 *ptr;

  /* Get the rest of the header's header */
  gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 6);
  object = (asf_obj_header *)ptr;
  num_objects = GUINT32_FROM_LE (object->num_objects);

  GST_INFO ( "Object is a header with %u parts", num_objects);  

  /* Loop through the header's objects, processing those */  
  for (i = 0; i < num_objects; i++) {
    if (!gst_asf_demux_process_object (asf_demux)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_segment (GstASFDemux       *asf_demux, 
			       asf_packet_info   *packet_info)
{
  guint8   *byte;
  gboolean key_frame;
  guint32  replic_size;
  guint8   time_delta;
  guint32  time_start;
  guint32  frag_size;
  guint32  rsize;
  asf_segment_info segment_info;

  gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&byte, 1); rsize = 1;
  segment_info.stream_number = *byte & 0x7f;
  key_frame = (*byte & 0x80) >> 7;
  
  GST_INFO ( "Processing segment for stream %u", segment_info.stream_number);
  segment_info.sequence = gst_asf_demux_read_var_length (asf_demux, packet_info->seqtype, &rsize);
  segment_info.frag_offset = gst_asf_demux_read_var_length (asf_demux, packet_info->fragoffsettype, &rsize);
  replic_size = gst_asf_demux_read_var_length (asf_demux, packet_info->replicsizetype, &rsize);
  GST_DEBUG ("sequence = %x, frag_offset = %x, replic_size = %x", segment_info.sequence, segment_info.frag_offset, replic_size);
  
  if (replic_size > 1) {
    asf_replicated_data *replicated_data_header;
    guint8              *replicated_data = NULL;
    guint8              *ptr;

    segment_info.compressed = FALSE;
    
    /* It's uncompressed with replic data*/
    if (replic_size < 8) {
      gst_element_error (GST_ELEMENT (asf_demux), "The payload has replicated data but the size is less than 8");
      return FALSE;
    }
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 8);
    replicated_data_header = (asf_replicated_data *)ptr;
    segment_info.frag_timestamp = GUINT32_FROM_LE (replicated_data_header->frag_timestamp);
    segment_info.segment_size = GUINT32_FROM_LE (replicated_data_header->object_size);

    if (replic_size > 8) {
      gst_asf_demux_read_object_header_rest (asf_demux, &replicated_data, replic_size - 8);
    }

    rsize += replic_size;
  } else {
    if (replic_size == 1) {
      /* It's compressed */
      segment_info.compressed = TRUE;
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&byte, 1); rsize++;
      time_delta = *byte;
      GST_DEBUG ("time_delta %u", time_delta);
    } else {
      segment_info.compressed = FALSE;
    }
    
    time_start = segment_info.frag_offset;
    segment_info.frag_offset = 0;
    segment_info.frag_timestamp = asf_demux->timestamp;
  }

  GST_DEBUG ("multiple = %u compressed = %u", packet_info->multiple, segment_info.compressed);

  if (packet_info->multiple) {
    frag_size = gst_asf_demux_read_var_length (asf_demux, packet_info->segsizetype, &rsize);
  } else {
    frag_size = packet_info->size_left - rsize;
  }

  packet_info->size_left -= rsize;
  
  GST_DEBUG ("size left = %u frag size = %u rsize = %u", packet_info->size_left, frag_size, rsize);

  if (segment_info.compressed) {
    while (frag_size > 0) {
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&byte, 1); 
      packet_info->size_left--;
      segment_info.chunk_size = *byte;
      segment_info.segment_size = segment_info.chunk_size;

      if (segment_info.chunk_size > packet_info->size_left) {
	gst_element_error (GST_ELEMENT (asf_demux), "Payload chunk overruns packet size.");
	return FALSE;
      }

      gst_asf_demux_process_chunk (asf_demux, packet_info, &segment_info);

      frag_size -= segment_info.chunk_size + 1;
    }
  } else {
    segment_info.chunk_size = frag_size;
    gst_asf_demux_process_chunk (asf_demux, packet_info, &segment_info);
  }

  return TRUE;
}

static gboolean
gst_asf_demux_process_data (GstASFDemux *asf_demux, guint64 *obj_size)
{
  asf_obj_data        *object;
  asf_obj_data_packet *packet_properties_object;
  gboolean            correction;
  guint64             packets;
  guint64             packet;
  guint8              *buf;
  guint32             sequence;
  guint32             packet_length;
  guint32             *timestamp;
  guint16             *duration;
  guint8              segment;
  guint8              segments;
  guint8              flags;
  guint8              property;
  asf_packet_info     packet_info;
  guint32             rsize;
  guint8              *ptr;
  
  /* Get the rest of the header */
  gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 26);
  object = (asf_obj_data *)ptr;
  packets = GUINT64_FROM_LE (object->packets);

  GST_INFO ( "Object is data with %" G_GUINT64_FORMAT " packets", packets); 

  for (packet = 0; packet < packets; packet++) {
    GST_INFO ( "\n\nProcess packet %" G_GUINT64_FORMAT, packet);
    
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&buf, 1); rsize=1;
    if (*buf & 0x80) {
      asf_obj_data_correction *correction_object;
      guint8 *ptr;
      
      /* Uses error correction */
      correction = TRUE;
      GST_DEBUG ("Data has error correction (%x)", *buf);
      gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 2); rsize += 2;
      correction_object = (asf_obj_data_correction *)ptr;
    }
    
    /* Read the packet flags */
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 2); rsize += 2;
    packet_properties_object = (asf_obj_data_packet *)ptr;
    flags = packet_properties_object->flags;
    property = packet_properties_object->property;
    
    packet_info.multiple = flags & 0x01;
    sequence = gst_asf_demux_read_var_length (asf_demux, (flags >> 1) & 0x03, &rsize);
    packet_info.padsize = 
      gst_asf_demux_read_var_length (asf_demux, (flags >> 3) & 0x03, &rsize);
    packet_length = 
      gst_asf_demux_read_var_length (asf_demux, (flags >> 5) & 0x03, &rsize);
    if (packet_length == 0)
      packet_length = asf_demux->packet_size;
    
    GST_DEBUG ("Multiple = %u, Sequence = %u, Padsize = %u, Packet length = %u", packet_info.multiple, sequence, packet_info.padsize, packet_length);
    
    /* Read the property flags */
    packet_info.replicsizetype = property & 0x03;
    packet_info.fragoffsettype = (property >> 2) & 0x03;
    packet_info.seqtype = (property >> 4) & 0x03;
    
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 4);
    timestamp = (guint32 *)ptr;
    asf_demux->timestamp = *timestamp;
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 2);
    duration = (guint16 *)ptr;
    
    rsize += 6;
    
    GST_DEBUG ("Timestamp = %x, Duration = %x", asf_demux->timestamp, *duration);
    
    if (packet_info.multiple) {
      /* There are multiple payloads */
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&buf, 1);
      rsize++;
      packet_info.segsizetype = (*buf >> 6) & 0x03;
      segments = *buf & 0x3f;
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
  }
  
  return TRUE;
}

static gboolean
gst_asf_demux_process_stream (GstASFDemux *asf_demux, guint64 *obj_size)
{
  asf_obj_stream          *object;
  guint32                 stream_id;
  guint32                 correction;
  asf_stream_audio        *audio_object;
  asf_stream_correction   *correction_object;
  asf_stream_video        *video_object;
  asf_stream_video_format *video_format_object;
  guint16                 size;
  GstBuffer               *buf;
  guint32                 got_bytes;
  guint16                 id;
  guint8                  *ptr;

  /* Get the rest of the header's header */
  gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 54);
  object = (asf_obj_stream *)ptr;

  /* Identify the stream type */
  stream_id = gst_asf_demux_identify_guid (asf_demux, asf_stream_guids, &(object->type));
  correction = gst_asf_demux_identify_guid (asf_demux, asf_correction_guids, &(object->correction));
  id = GUINT16_FROM_LE (object->id);

  switch (stream_id) {
  case ASF_STREAM_AUDIO:
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 18);
    audio_object = (asf_stream_audio *)ptr;
    size = GUINT16_FROM_LE (audio_object->size);

    GST_INFO ("Object is an audio stream with %u bytes of additional data.", size);

    if (!gst_asf_demux_add_audio_stream (asf_demux, audio_object, id))
      return FALSE;

    switch (correction) {
    case ASF_CORRECTION_ON:
      GST_INFO ( "Using error correction");

      /* Have to read the first byte seperately to avoid endian problems */
      got_bytes = gst_bytestream_read (asf_demux->bs, &buf, 1);
      asf_demux->span = *GST_BUFFER_DATA (buf);

      gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 7);
      correction_object = (asf_stream_correction *)ptr;
      GST_DEBUG ("Descrambling: ps:%d cs:%d ds:%d s:%d sd:%d", GUINT16_FROM_LE(correction_object->packet_size), GUINT16_FROM_LE(correction_object->chunk_size), GUINT16_FROM_LE(correction_object->data_size), asf_demux->span, correction_object->silence_data);

      if (asf_demux->span > 1) {
	if (!correction_object->chunk_size || ((correction_object->packet_size / correction_object->chunk_size) <= 1))
	  /* Disable descrambling */
	  asf_demux->span = 0;
      } else {
	/* Descambling is enabled */
	asf_demux->ds_packet_size = correction_object->packet_size;
	asf_demux->ds_chunk_size = correction_object->chunk_size;
      }

      /* Now skip the rest of the silence data */
      if (correction_object->data_size > 1)
	gst_bytestream_flush (asf_demux->bs, correction_object->data_size - 1);

      break;
    case ASF_CORRECTION_OFF:
      GST_INFO ( "Error correction off");
      gst_bytestream_flush (asf_demux->bs, object->stream_specific_size);
      break;
    default:
      gst_element_error (GST_ELEMENT (asf_demux), "Audio stream using unknown error correction");
      return FALSE;
    }

    break;
  case ASF_STREAM_VIDEO:
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 11);
    video_object = (asf_stream_video *)ptr;
    size = GUINT16_FROM_LE(video_object->size) - 40; /* Byte order gets 
						      * offset by single 
						      * byte */
    GST_INFO ( "Object is a video stream with %u bytes of additional data.", size);
    gst_asf_demux_read_object_header_rest (asf_demux, &ptr, 40);
    video_format_object = (asf_stream_video_format *)ptr;

    if (!gst_asf_demux_add_video_stream (asf_demux, video_format_object, id))
      return FALSE;
    
    /* Read any additional information */
    if (size) {
      got_bytes = gst_bytestream_read (asf_demux->bs, &buf, size);
      /* There is additional data */
      while (got_bytes < size) {
	guint32 remaining;
	GstEvent *event;
	
	gst_bytestream_get_status (asf_demux->bs, &remaining, &event);
	gst_event_unref (event);

	got_bytes = gst_bytestream_read (asf_demux->bs, &buf, size);
      }
    }
    break;
  default:
    gst_element_error (GST_ELEMENT (asf_demux), "Object is a stream of unrecognised type");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_asf_demux_skip_object (GstASFDemux *asf_demux, guint64 *obj_size)
{
  GstByteStream *bs = asf_demux->bs;

  GST_INFO ( "Skipping object...");

  gst_bytestream_flush (bs, *obj_size - 24);

  return TRUE;
}

static inline gboolean
gst_asf_demux_read_object_header (GstASFDemux *asf_demux, guint32 *obj_id, guint64 *obj_size)
{
  guint32       got_bytes;
  ASFGuid    *guid;
  GstByteStream *bs = asf_demux->bs;
  guint8	*ptr;
  
  
  /* First get the GUID */
  if ((got_bytes = gst_bytestream_peek_bytes (bs, &ptr,
			sizeof(ASFGuid))) < sizeof(ASFGuid))
    return FALSE;
  guid = (ASFGuid *) ptr;
  *obj_id = gst_asf_demux_identify_guid (asf_demux, asf_object_guids, guid);
  gst_bytestream_flush (bs, sizeof (ASFGuid));

  if ((got_bytes = gst_bytestream_peek_bytes (bs, &ptr,
			sizeof(guint64))) < sizeof(guint64))
    return FALSE;
  *obj_size = GUINT64_FROM_LE(* (guint64 *) ptr);
  gst_bytestream_flush (bs, sizeof (guint64));

  if (*obj_id == ASF_OBJ_UNDEFINED) {
    g_warning ("Could not identify object (0x%08x/0x%08x/0x%08x/0x%08x) with size=%llu",
	       guid->v1, guid->v2, guid->v3, guid->v4, *obj_size);
    return TRUE;
  }
  
  return TRUE;
}

static gboolean
gst_asf_demux_process_object    (GstASFDemux *asf_demux) {

  guint32 obj_id;
  guint64 obj_size;

  if (!gst_asf_demux_read_object_header (asf_demux, &obj_id, &obj_size)) {
    GST_DEBUG ("  *****  Error reading object at filepos %" G_GUINT64_FORMAT " (EOS?)\n", /**filepos*/ gst_bytestream_tell (asf_demux->bs));
    gst_asf_demux_handle_sink_event (asf_demux, gst_event_new (GST_EVENT_EOS), 0);
    return FALSE;
  }

  GST_INFO ( "Found object %u with size %" G_GUINT64_FORMAT, obj_id, obj_size);

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
  case ASF_OBJ_CONCEAL_NONE:
    break;
  case ASF_OBJ_COMMENT:
    return gst_asf_demux_process_comment (asf_demux, &obj_size);
  case ASF_OBJ_CODEC_COMMENT:
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  case ASF_OBJ_INDEX:
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  case ASF_OBJ_HEAD1:
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  case ASF_OBJ_HEAD2:
    break;
  case ASF_OBJ_PADDING:
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  case ASF_OBJ_EXT_CONTENT_DESC:
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  case ASF_OBJ_BITRATE_PROPS:
    return gst_asf_demux_process_bitrate_props_object (asf_demux, &obj_size);
  case ASF_OBJ_BITRATE_MUTEX:
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  case ASF_OBJ_UNDEFINED:
  default:
    /* unknown object read, just ignore it, we hate fatal errors */
    return gst_asf_demux_skip_object (asf_demux, &obj_size);
  }

  return TRUE;
}


static asf_stream_context *
gst_asf_demux_get_stream (GstASFDemux *asf_demux,
		    guint16 id)
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
  gst_element_error (GST_ELEMENT (asf_demux), "Segment found for undefined stream: (%d)", id);

  return NULL;
}

static inline void
gst_asf_demux_descramble_segment (GstASFDemux        *asf_demux,
				  asf_segment_info   *segment_info,
				  asf_stream_context *stream) {
  GstBuffer *scrambled_buffer;
  GstBuffer *descrambled_buffer;
  GstBuffer *sub_buffer;
  guint     offset;
  guint     off;
  guint     row;
  guint     col;
  guint     idx;
  
  /* descrambled_buffer is initialised in the first iteration */
  descrambled_buffer = NULL;
  scrambled_buffer = stream->payload;

  offset = 0;

  for (offset = 0; offset < segment_info->segment_size; offset += asf_demux->ds_chunk_size) {
    off = offset / asf_demux->ds_chunk_size;
    row = off / asf_demux->span;
    col = off % asf_demux->span;
    idx = row + col * asf_demux->ds_packet_size / asf_demux->ds_chunk_size;
    sub_buffer = gst_buffer_create_sub (scrambled_buffer, idx * asf_demux->ds_chunk_size, asf_demux->ds_chunk_size);
    if (!offset) {
      descrambled_buffer = sub_buffer;
    } else {
      gst_buffer_merge (descrambled_buffer, sub_buffer);
      gst_buffer_unref (sub_buffer);
    }
  }

  stream->payload = descrambled_buffer;
  gst_buffer_unref (scrambled_buffer);
}

static gboolean 
gst_asf_demux_process_chunk (GstASFDemux *asf_demux,
			     asf_packet_info *packet_info,
			     asf_segment_info *segment_info)
{
  asf_stream_context *stream;
  guint32            got_bytes;
  GstByteStream      *bs = asf_demux->bs;
  GstBuffer          *buffer;

  if (!(stream = gst_asf_demux_get_stream (asf_demux, segment_info->stream_number)))
    return FALSE;
  
  GST_DEBUG ("Processing chunk of size %u (fo = %d)", segment_info->chunk_size, stream->frag_offset);
  
  if (stream->frag_offset == 0) {
    /* new packet */
    stream->sequence = segment_info->sequence;
    asf_demux->pts = segment_info->frag_timestamp - asf_demux->preroll;
    got_bytes = gst_bytestream_peek (bs, &buffer, segment_info->chunk_size);
    GST_DEBUG ("BUFFER: Copied stream to buffer (%p - %d)", buffer, GST_BUFFER_REFCOUNT_VALUE(buffer));
    stream->payload = buffer;
  } else {
    GST_DEBUG ("segment_info->sequence = %d, stream->sequence = %d, segment_info->frag_offset = %d, stream->frag_offset = %d", segment_info->sequence, stream->sequence, segment_info->frag_offset, stream->frag_offset);
    if (segment_info->sequence == stream->sequence && 
	segment_info->frag_offset == stream->frag_offset) {
      GstBuffer *new_buffer;
      /* continuing packet */
      GST_INFO ( "A continuation packet");
      got_bytes = gst_bytestream_peek (bs, &buffer, segment_info->chunk_size);
      GST_DEBUG ("Copied stream to buffer (%p - %d)", buffer, GST_BUFFER_REFCOUNT_VALUE(buffer));
      new_buffer = gst_buffer_merge (stream->payload, buffer);
      GST_DEBUG ("BUFFER: Merged new_buffer (%p - %d) from stream->payload(%p - %d) and buffer (%p - %d)", new_buffer, GST_BUFFER_REFCOUNT_VALUE(new_buffer), stream->payload, GST_BUFFER_REFCOUNT_VALUE(stream->payload), buffer, GST_BUFFER_REFCOUNT_VALUE(buffer));
      gst_buffer_unref (stream->payload);
      gst_buffer_unref (buffer);
      stream->payload = new_buffer;
    } else {
      /* cannot continue current packet: free it */
      stream->frag_offset = 0;
      if (segment_info->frag_offset != 0) {
	/* cannot create new packet */
	GST_DEBUG ("BUFFER: Freeing stream->payload (%p)", stream->payload);
	gst_buffer_free(stream->payload);
	gst_bytestream_flush (bs, segment_info->chunk_size);
	packet_info->size_left -= segment_info->chunk_size;
	return TRUE;
      } else {
	/* create new packet */
	stream->sequence = segment_info->sequence;
      }
    }
  }

  stream->frag_offset +=segment_info->chunk_size;

  GST_DEBUG ("frag_offset = %d  segment_size = %d ", stream->frag_offset, segment_info->segment_size);

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
      GST_DEBUG ("New buffer is at: %p size: %u", GST_BUFFER_DATA(stream->payload), GST_BUFFER_SIZE(stream->payload));
      
      GST_BUFFER_TIMESTAMP (stream->payload) = (GST_SECOND / 1000) * asf_demux->pts;

      /*!!! Should handle flush events here? */
      GST_DEBUG ("Sending strem %d of size %d", stream->id , segment_info->chunk_size);
      
      GST_INFO ( "Pushing pad");
      gst_pad_push (stream->pad, GST_DATA (stream->payload));
    }

    stream->frag_offset = 0;
  }

  gst_bytestream_flush (bs, segment_info->chunk_size);  
  packet_info->size_left -= segment_info->chunk_size;

  GST_DEBUG (" ");

  return TRUE;
}

/*
 * Event stuff
 */

static gboolean
gst_asf_demux_handle_sink_event (GstASFDemux *asf_demux,
				 GstEvent    *event,
				 guint32      remaining)
{
  GstEventType type;

  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS: {
      asf_stream_context *stream;
      GstEvent *event = gst_event_new (GST_EVENT_EOS);
      gint n;
      for (n = 0; n < asf_demux->num_streams; n++) {
        stream = &asf_demux->stream[n];
        gst_pad_push (stream->pad, GST_DATA (gst_event_ref (event)));
      }
      gst_event_unref (event);
      gst_bytestream_flush (asf_demux->bs, remaining);
      gst_element_set_eos (GST_ELEMENT (asf_demux));
      break;
    }
    case GST_EVENT_FLUSH:
      g_warning ("flush event");
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint i;
      GstEvent *discont;

      for (i = 0; i < asf_demux->num_streams; i++) {
        asf_stream_context *stream = &asf_demux->stream[i];

	if (GST_PAD_IS_USABLE (stream->pad)) {
	  GST_DEBUG ("sending discont on %d %" G_GINT64_FORMAT " + %" G_GINT64_FORMAT " = %" G_GINT64_FORMAT, i, 
			asf_demux->last_seek, stream->delay, asf_demux->last_seek + stream->delay);
         discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
			asf_demux->last_seek + stream->delay , NULL);
	  gst_pad_push (stream->pad, GST_DATA (discont));
	}
      }
      break;
    }
    default:
      g_warning ("unhandled event %d", type);
      break;
  }

  gst_event_unref (event);

  return TRUE;
}

static gboolean
gst_asf_demux_handle_src_event (GstPad *pad, GstEvent *event)
{
  GST_DEBUG ("asfdemux: handle_src_event");
  return FALSE;
}

static const GstEventMask*
gst_asf_demux_get_src_event_mask (GstPad *pad) {
  static const GstEventMask masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT },
    { 0, }
  };

  return masks;
}

static const GstFormat*
gst_asf_demux_get_src_formats  (GstPad *pad) {
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    0
  };
  return formats;
}

static const GstQueryType* 
gst_asf_demux_get_src_query_types (GstPad *pad) {
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_asf_demux_handle_src_query (GstPad *pad,
				GstQueryType type,
				GstFormat *format, gint64 *value)
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
      *value = (GST_SECOND / 1000) * asf_demux->pts;
      break;
    default:
      res = FALSE;
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
    }
  default:
    res = FALSE;
    break;
  }

  return res;
}


static void
gst_asf_demux_get_property (GObject *object, 
			    guint prop_id, 
			    GValue *value,
			    GParamSpec *pspec)
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
gst_asf_demux_change_state (GstElement *element)
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
      for (i = 0 ; i < GST_ASF_DEMUX_NUM_VIDEO_PADS; i++) {
        asf_demux->video_PTS[i] = 0;
      }
      for (i = 0 ; i < GST_ASF_DEMUX_NUM_AUDIO_PADS;i++) {
        asf_demux->audio_PTS[i] = 0;
      }
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static guint32
gst_asf_demux_identify_guid (GstASFDemux *asf_demux,
		       ASFGuidHash *guids,
		       ASFGuid *guid_raw)
{
  guint32 i;
  ASFGuid guid;

  /* guid_raw is passed in 'as-is' from the file
   * so we must convert it's endianess */
  guid.v1 = GUINT32_FROM_LE (guid_raw->v1);
  guid.v2 = GUINT32_FROM_LE (guid_raw->v2);
  guid.v3 = GUINT32_FROM_LE (guid_raw->v3);
  guid.v4 = GUINT32_FROM_LE (guid_raw->v4);

  i = 0;
  while (guids[i].obj_id != ASF_OBJ_UNDEFINED) {
    if (guids[i].guid.v1 == guid.v1 &&
	guids[i].guid.v2 == guid.v2 &&
	guids[i].guid.v3 == guid.v3 &&
	guids[i].guid.v4 == guid.v4) {
      return guids[i].obj_id;
    }
    i++;
  }
  
  /* The base case if none is found */
  return ASF_OBJ_UNDEFINED;
}


/*
 * Stream and pad setup code
 */

#ifdef G_HAVE_ISO_VARARGS

#define GST_ASF_AUD_CAPS_NEW(name, mimetype, ...)			\
	(audio != NULL) ?						\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT (			\
				    GUINT32_FROM_LE (audio->sample_rate)), \
		      "channels", GST_PROPS_INT (			\
				    GUINT16_FROM_LE (audio->channels)),	\
		      __VA_ARGS__)					\
	:								\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT_RANGE (8000, 96000),	\
		      "channels", GST_PROPS_INT_RANGE (1, 2),		\
		      __VA_ARGS__)

#elif defined(G_HAVE_GNUC_VARARGS)

#define GST_ASF_AUD_CAPS_NEW(name, mimetype, props...)			\
	(audio != NULL) ?						\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT (			\
				    GUINT32_FROM_LE (audio->sample_rate)), \
		      "channels", GST_PROPS_INT (			\
				    GUINT16_FROM_LE (audio->channels)),	\
		      ##props)						\
	:								\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT_RANGE (8000, 96000),	\
		      "channels", GST_PROPS_INT_RANGE (1, 2),		\
		      ##props)
#endif

static GstCaps *
gst_asf_demux_audio_caps (guint16 codec_id,
			  asf_stream_audio *audio, guint8 *extradata)
{
  GstCaps *caps = NULL;
  gint flags1, flags2;

  flags1 = 0;
  flags2 = 0;

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3: /* mp3 */
      caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_mp3",
				   "audio/mpeg",
				     "layer", GST_PROPS_INT (3));
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_mp12",
				   "audio/mpeg",
				     "layer", GST_PROPS_INT (2));
      break;

    case GST_RIFF_WAVE_FORMAT_PCM: /* PCM/wav */ {
      GstPropsEntry *width, *depth, *signedness;

      if (audio != NULL) {
        gint ba = GUINT16_FROM_LE (audio->block_align);
        gint ch = GUINT16_FROM_LE (audio->channels);
        gint ws = GUINT16_FROM_LE (audio->word_size);
        width = gst_props_entry_new ("width",
				     GST_PROPS_INT (ba * 8 / ch));
        depth = gst_props_entry_new ("depth",
				     GST_PROPS_INT (ws));
        signedness = gst_props_entry_new ("signedness",
					  GST_PROPS_BOOLEAN (ws != 8));
      } else {
        signedness = gst_props_entry_new ("signed",
					  GST_PROPS_LIST (
					    GST_PROPS_BOOLEAN (TRUE),
					    GST_PROPS_BOOLEAN (FALSE)));
        width = gst_props_entry_new ("width",
				     GST_PROPS_LIST (
				       GST_PROPS_INT (8),
				       GST_PROPS_INT (16)));
        depth = gst_props_entry_new ("depth",
				     GST_PROPS_LIST (
				       GST_PROPS_INT (8),
				       GST_PROPS_INT (16)));
      }

      caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_pcm",
				   "audio/x-raw-int",
				     "endianness",
				       GST_PROPS_INT (G_LITTLE_ENDIAN));
      gst_props_add_entry (caps->properties, width);
      gst_props_add_entry (caps->properties, depth);
      gst_props_add_entry (caps->properties, signedness);
    }
      break;

    case GST_RIFF_WAVE_FORMAT_VORBIS1: /* vorbis mode 1 */
    case GST_RIFF_WAVE_FORMAT_VORBIS2: /* vorbis mode 2 */
    case GST_RIFF_WAVE_FORMAT_VORBIS3: /* vorbis mode 3 */
    case GST_RIFF_WAVE_FORMAT_VORBIS1PLUS: /* vorbis mode 1+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS2PLUS: /* vorbis mode 2+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS3PLUS: /* vorbis mode 3+ */
      caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_vorbis",
				   "audio/x-vorbis",
				     NULL);
      break;

    case GST_RIFF_WAVE_FORMAT_A52:
      caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_ac3",
				   "audio/x-ac3",
				     NULL);
      break;

    case GST_RIFF_WAVE_FORMAT_DIVX_WMAV1:
      /* get flags1 and flags2 ripped from ffmpeg (wmadec.c) */
      if (audio && audio->size >= 4) {
        flags1 = extradata[0] | (extradata[1] << 8);
        flags2 = extradata[2] | (extradata[3] << 8);
      }
      if (audio != NULL)
        caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_wmav1",
                                     "audio/x-wma",
                                       "wmaversion",  GST_PROPS_INT (1),                        
                                       "flags1",      GST_PROPS_INT (flags1),
                                       "flags2",      GST_PROPS_INT (flags2),
                                       "block_align", GST_PROPS_INT (audio->block_align),
                                       "bitrate",     GST_PROPS_INT (audio->byte_rate * 8));
      else
        caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_wmav1",
                                     "audio/x-wma",
	 			       "wmaversion",  GST_PROPS_INT (1),
                                       "flags1",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                       "flags2",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                       "block_align", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                       "bitrate",     GST_PROPS_INT_RANGE (0, G_MAXINT)
                                    );
      break;

    case GST_RIFF_WAVE_FORMAT_DIVX_WMAV2:
      /* get flags1 and flags2 ripped from ffmpeg (wmadec.c) */
      if (audio && audio->size >= 6) {
        flags1 = extradata[0] | (extradata[1] << 8) | 
                (extradata[2] << 16) | (extradata[3] << 24);
        flags2 = extradata[4] | (extradata[5] << 8);
      }
      if (audio != NULL)
        caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_wmav2",
				     "audio/x-wma",
				       "wmaversion",  GST_PROPS_INT (2),
                                       "flags1",      GST_PROPS_INT (flags1),
                                       "flags2",      GST_PROPS_INT (flags2),
                                       "block_align", GST_PROPS_INT (audio->block_align),
                                       "bitrate",     GST_PROPS_INT (audio->byte_rate * 8));
     else
       caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_wmav2",
	 			    "audio/x-wma",
				      "wmaversion",  GST_PROPS_INT (2),
                                      "flags1",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                      "flags2",      GST_PROPS_INT_RANGE (G_MININT, G_MAXINT),
                                      "block_align", GST_PROPS_INT_RANGE (0, G_MAXINT),
                                      "bitrate",     GST_PROPS_INT_RANGE (0, G_MAXINT)
                                   );
      break;

    case GST_RIFF_WAVE_FORMAT_WMAV9:
      caps = GST_ASF_AUD_CAPS_NEW ("asf_demux_audio_src_wmav9",
				   "audio/x-wma",
				     "wmaversion", GST_PROPS_INT (9));
      break;

    default:
      g_warning ("asfdemux: unkown audio format 0x%04x",
		 codec_id);
      break;
  }

  return caps;
}

static gboolean 
gst_asf_demux_add_audio_stream (GstASFDemux *asf_demux,
				asf_stream_audio *audio,
				guint16 id) 
{
  GstPad         *src_pad;  
  GstCaps        *caps;
  gchar          *name = NULL;
  guint16         size_left = 0;
  guint8         *extradata=NULL;

  size_left = GUINT16_FROM_LE(audio->size);

  /* Create the audio pad */
  name = g_strdup_printf ("audio_%02d", asf_demux->num_audio_streams);

  src_pad = gst_pad_new_from_template (audiosrctempl, name);
  g_free (name);

  /* Swallow up any left over data */
  if (size_left) {
    g_warning ("asfdemux: Audio header contains %d bytes of surplus data", size_left);
    gst_asf_demux_read_object_header_rest (asf_demux, &extradata, size_left);
//    gst_bytestream_flush (asf_demux->bs, size_left);
  }

  /* Now set up the standard propertis from the header info */
  caps = gst_asf_demux_audio_caps (GUINT16_FROM_LE(audio->codec_tag),
				   audio, extradata);

  GST_INFO ("Adding audio stream %u codec %u (0x%x)",
	    asf_demux->num_video_streams,
	    GUINT16_FROM_LE(audio->codec_tag),
	    GUINT16_FROM_LE(audio->codec_tag));

  asf_demux->num_audio_streams++;

  return gst_asf_demux_setup_pad (asf_demux, src_pad, caps, id);
}

#ifdef G_HAVE_ISO_VARARGS

#define GST_ASF_VID_CAPS_NEW(name, mimetype, ...)		\
	(video != NULL) ?					\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT (width),		\
		      "height", GST_PROPS_INT (height),		\
		      "framerate", GST_PROPS_FLOAT (0),/* FIXME */ \
		      __VA_ARGS__)				\
	:							\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT_RANGE (16, 4096),	\
		      "height", GST_PROPS_INT_RANGE (16, 4096),	\
		      "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT), \
		      __VA_ARGS__)

#elif defined(G_HAVE_GNUC_VARARGS)

#define GST_ASF_VID_CAPS_NEW(name, mimetype, props...)		\
	(video != NULL) ?					\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT (width),		\
		      "height", GST_PROPS_INT (height),		\
		      "framerate", GST_PROPS_FLOAT (0),/* FIXME */ \
		      ##props)					\
	:							\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT_RANGE (16, 4096),	\
		      "height", GST_PROPS_INT_RANGE (16, 4096),	\
		      "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT), \
		      ##props)
#endif

static GstCaps *
gst_asf_demux_video_caps (guint32 codec_fcc,
			  asf_stream_video_format *video)
{
  GstCaps *caps = NULL;
  gint width = 0, height = 0;
  
  if (video != NULL) {
    width = GUINT32_FROM_LE (video->width);
    height = GUINT32_FROM_LE (video->height);
  }

  switch (codec_fcc) {
    case GST_MAKE_FOURCC('I','4','2','0'):
    case GST_MAKE_FOURCC('Y','U','Y','2'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_raw",
				   "video/x-raw-yuv",
				     "format", GST_PROPS_FOURCC (codec_fcc));
      break;

    case GST_MAKE_FOURCC('M','J','P','G'):
    case GST_MAKE_FOURCC('J','P','E','G'):
    case GST_MAKE_FOURCC('P','I','X','L'):
    case GST_MAKE_FOURCC('V','I','X','L'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_jpeg",
				   "video/x-jpeg",
				     NULL);
      break;

    case GST_MAKE_FOURCC('D','V','S','D'):
    case GST_MAKE_FOURCC('d','v','s','d'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_dv",
				   "video/x-dv",
				     "systemstream", GST_PROPS_BOOLEAN (FALSE));
      break;

    case GST_MAKE_FOURCC('W','M','V','1'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_wmv1",
				   "video/x-wmv",
				     "wmvversion", GST_PROPS_INT (1));
      break;

    case GST_MAKE_FOURCC('W','M','V','2'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_wmv2",
				   "video/x-wmv",
				     "wmvversion", GST_PROPS_INT (2));
      break;

    case GST_MAKE_FOURCC('M','P','G','4'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_msmpeg41",
				   "video/x-msmpeg",
				     "msmpegversion", GST_PROPS_INT (41));
      break;

    case GST_MAKE_FOURCC('M','P','4','2'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_msmpeg42",
				   "video/x-msmpeg",
				     "msmpegversion", GST_PROPS_INT (42));
      break;

    case GST_MAKE_FOURCC('M','P','4','3'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_msmpeg43",
				   "video/x-msmpeg",
				     "msmpegversion", GST_PROPS_INT (43));
      break;

    case GST_MAKE_FOURCC('D','I','V','3'):
    case GST_MAKE_FOURCC('D','I','V','4'):
    case GST_MAKE_FOURCC('D','I','V','5'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_div3",
				   "video/x-divx",
				     "divxversion", GST_PROPS_INT (3));
      break;

    case GST_MAKE_FOURCC('D','I','V','X'):
    case GST_MAKE_FOURCC('d','i','v','x'):
    case GST_MAKE_FOURCC('D','X','5','0'):
      caps = GST_ASF_VID_CAPS_NEW ("asf_demux_video_src_div5",
				   "video/x-divx",
				     "divxversion", GST_PROPS_INT (5));
      break;

    default:
      g_warning ("asfdemux: unkown video format " GST_FOURCC_FORMAT "(0x%08x)",
		 GST_FOURCC_ARGS(codec_fcc), codec_fcc);
      break;
  }

  return caps;
}

static gboolean
gst_asf_demux_add_video_stream (GstASFDemux *asf_demux,
				asf_stream_video_format *video,
				guint16 id) {
  GstPad         *src_pad;  
  GstCaps        *caps;
  gchar          *name = NULL;
  
  /* Create the audio pad */
  name = g_strdup_printf ("video_%02d", asf_demux->num_video_streams);
  src_pad = gst_pad_new_from_template (videosrctempl, name);
  g_free (name);

  /* Now try some gstreamer formatted MIME types (from gst_avi_demux_strf_vids) */
  caps = gst_asf_demux_video_caps (GUINT32_FROM_LE(video->tag),
				   video);

  GST_INFO ("Adding video stream %u codec " GST_FOURCC_FORMAT " (0x%08x)",
	    asf_demux->num_video_streams,
	    GST_FOURCC_ARGS(GUINT32_FROM_LE(video->tag)),
	    GUINT32_FROM_LE(video->tag));
  
  asf_demux->num_video_streams++;

  return gst_asf_demux_setup_pad (asf_demux, src_pad, caps, id);
}


static gboolean
gst_asf_demux_setup_pad (GstASFDemux *asf_demux,
			 GstPad *src_pad,
			 GstCaps *caps_list,
			 guint16 id)
{
  asf_stream_context *stream;

  gst_pad_try_set_caps (src_pad, caps_list);
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

  GST_INFO ("Adding pad for stream %u",
	    asf_demux->num_streams);


  asf_demux->num_streams++;
  
  gst_element_add_pad (GST_ELEMENT (asf_demux), src_pad);
  
  return TRUE;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  /* create an elementfactory for the asf_demux element */
  if (!gst_element_register (plugin, "asfdemux", GST_RANK_PRIMARY, GST_TYPE_ASF_DEMUX))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "asfdemux",
  "Demuxes ASF streams",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
