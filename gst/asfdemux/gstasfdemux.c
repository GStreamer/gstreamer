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


//#define GST_DEBUG_ENABLED
#include "gstasfdemux.h"

enum {
  ASF_OBJ_UNDEFINED = 0,
  ASF_OBJ_STREAM,
  ASF_OBJ_DATA,
  ASF_OBJ_FILE,
  ASF_OBJ_HEADER,
  ASF_OBJ_CONCEAL_NONE,
  ASF_OBJ_COMMENT,
  ASF_OBJ_CODEC_COMMENT,
  ASF_OBJ_INDEX,
  ASF_OBJ_HEAD1,
  ASF_OBJ_HEAD2,
  ASF_OBJ_PADDING,
};

enum {
  ASF_STREAM_UNDEFINED = 0,
  ASF_STREAM_VIDEO,
  ASF_STREAM_AUDIO,
};

enum {
  ASF_CORRECTION_UNDEFINED = 0,
  ASF_CORRECTION_ON,
  ASF_CORRECTION_OFF,
};

static GstASFGuidHash asf_correction_guids[] = {
  { ASF_CORRECTION_ON,     { 0xBFC3CD50, 0x11CF618F, 0xAA00B28B, 0x20E2B400 }},
  { ASF_CORRECTION_OFF,    { 0x20FB5700, 0x11CF5B55, 0x8000FDA8, 0x2B445C5F }},
  { ASF_CORRECTION_UNDEFINED,  { 0, 0, 0, 0 }},
};

static GstASFGuidHash asf_stream_guids[] = {
  { ASF_STREAM_VIDEO,      { 0xBC19EFC0, 0x11CF5B4D, 0x8000FDA8, 0x2B445C5F }},
  { ASF_STREAM_AUDIO,      { 0xF8699E40, 0x11CF5B4D, 0x8000FDA8, 0x2B445C5F }},
  { ASF_STREAM_UNDEFINED,  { 0, 0, 0, 0 }},
};

static GstASFGuidHash asf_object_guids[] = {
  { ASF_OBJ_STREAM,        { 0xB7DC0791, 0x11CFA9B7, 0xC000E68E, 0x6553200C }},
  { ASF_OBJ_DATA,          { 0x75b22636, 0x11cf668e, 0xAA00D9a6, 0x6Cce6200 }},
  { ASF_OBJ_FILE,          { 0x8CABDCA1, 0x11CFA947, 0xC000E48E, 0x6553200C }},
  { ASF_OBJ_HEADER,        { 0x75B22630, 0x11CF668E, 0xAA00D9A6, 0x6CCE6200 }},
  { ASF_OBJ_CONCEAL_NONE,  { 0x20fb5700, 0x11cf5b55, 0x8000FDa8, 0x2B445C5f }},
  { ASF_OBJ_COMMENT,       { 0x75b22633, 0x11cf668e, 0xAA00D9a6, 0x6Cce6200 }},
  { ASF_OBJ_CODEC_COMMENT, { 0x86D15240, 0x11D0311D, 0xA000A4A3, 0xF64803C9 }},
  { ASF_OBJ_CODEC_COMMENT, { 0x86d15241, 0x11d0311d, 0xA000A4a3, 0xF64803c9 }},
  { ASF_OBJ_INDEX,         { 0x33000890, 0x11cfe5b1, 0xA000F489, 0xCB4903c9 }},
  { ASF_OBJ_HEAD1,         { 0x5fbf03b5, 0x11cfa92e, 0xC000E38e, 0x6553200c }},
  { ASF_OBJ_HEAD2,         { 0xabd3d211, 0x11cfa9ba, 0xC000E68e, 0x6553200c }},
  { ASF_OBJ_PADDING,       { 0x1806D474, 0x4509CADF, 0xAB9ABAA4, 0xE8AA96CD }},
  { ASF_OBJ_UNDEFINED,     { 0, 0, 0, 0 }},
};

struct _asf_obj_header {
  guint32 num_objects;
  guint8  unknown1;
  guint8  unknown2;
};

typedef struct _asf_obj_header asf_obj_header;

struct _asf_obj_file {
  GstASFGuid file_id;
  guint64    file_size;
  guint64    creation_time;
  guint64    packets_count;
  guint64    play_time;
  guint64    send_time;
  guint64    preroll;
  guint32    flags;
  guint32    min_pktsize;
  guint32    max_pktsize;
  guint32    min_bitrate;
};

typedef struct _asf_obj_file asf_obj_file;

struct _asf_obj_stream {
  GstASFGuid type;
  GstASFGuid correction;
  guint64    offset;
  guint32    unknown1;
  guint32    unknown2;
  guint16    flags;
  guint32    unknown3;
};

typedef struct _asf_obj_stream asf_obj_stream;

struct _asf_stream_audio {
  guint16   codec_tag;
  guint16   channels;
  guint32   sample_rate;
  guint32   byte_rate;
  guint16   block_align;
  guint16   word_size;
  guint16   size;
};

typedef struct _asf_stream_audio asf_stream_audio;

struct _asf_stream_correction {
  guint8  span;
  guint16 packet_size;
  guint16 chunk_size;
  guint16 data_size;
  guint8  silence_data;
};

typedef struct _asf_stream_correction asf_stream_correction;

struct _asf_stream_video {
  guint32 width;
  guint32 height;
  guint8  unknown;
  guint16 size;
};

typedef struct _asf_stream_video asf_stream_video;

struct _asf_stream_video_format {
  guint32   size;
  guint32   width;
  guint32   height;
  guint16   panes;
  guint16   depth;
  guint32   tag;
  guint32   unknown1;
  guint32   unknown2;
  guint32   unknown3;
  guint32   unknown4;
  guint32   unknown5;
};

typedef struct _asf_stream_video_format asf_stream_video_format;

struct _asf_obj_data {
  GstASFGuid file_id;
  guint64    packets;
  guint8     unknown1;
  guint8     unknown2;
  guint8     correction;
};

typedef struct _asf_obj_data asf_obj_data;

struct _asf_obj_data_correction {
  guint8 type;
  guint8 cycle;
};

typedef struct _asf_obj_data_correction asf_obj_data_correction;

struct _asf_obj_data_packet {
  guint8  flags;
  guint8  property;
};

typedef struct _asf_obj_data_packet asf_obj_data_packet;

struct _asf_packet_info {
  guint32  padsize;
  guint8   replicsizetype;
  guint8   fragoffsettype;
  guint8   seqtype;
  guint8   segsizetype;
  gboolean multiple;
  guint32  size_left;
};

typedef struct _asf_packet_info asf_packet_info;

struct _asf_segment_info {
  guint8   stream_number;
  guint32  chunk_size;
  guint32  frag_offset;
  guint32  sequence;
  gboolean compressed;
};

typedef struct _asf_segment_info asf_segment_info;


struct _asf_replicated_data {
  guint32 object_size;
  guint32 frag_timestamp;
};

typedef struct _asf_replicated_data asf_replicated_data;


/* elementfactory information */
static GstElementDetails gst_asf_demux_details = {
  "ASF Demuxer",
  "Codec/Demuxer",
  "LGPL",
  "Demultiplexes ASF Streams",
  VERSION,
  "Owen Fraser-Green <owen@discobabe.net>",
  "(C) 2002",
};

static GstCaps* asf_asf_type_find (GstBuffer *buf, gpointer private);
static GstCaps* asf_wma_type_find (GstBuffer *buf, gpointer private);
static GstCaps* asf_wax_type_find (GstBuffer *buf, gpointer private);
static GstCaps* asf_wmv_type_find (GstBuffer *buf, gpointer private);
static GstCaps* asf_wvx_type_find (GstBuffer *buf, gpointer private);
static GstCaps* asf_wm_type_find (GstBuffer *buf, gpointer private);

/* typefactory for 'asf' */
static GstTypeDefinition asf_type_definitions[] = {
  { "asfdemux_video/asf",
    "video/x-ms-asf",
    ".asf .asx",
    asf_asf_type_find },
  { "asfdemux_video/wma",
    "video/x-ms-wma",
    ".wma",
    asf_wma_type_find },
  { "asfdemux_video/wax",
    "video/x-ms-wax",
    ".wax",
    asf_wax_type_find },
  { "asfdemux_video/wmv",
    "video/x-ms-wmv",
    ".wmv",
    asf_wmv_type_find },
  { "asfdemux_video/wvx",
    "video/x-ms-wvx",
    ".wvx",
    asf_wvx_type_find },
  { "asfdemux_video/wm",
    "video/x-ms-wm",
    ".wm",
    asf_wm_type_find },
  { NULL, NULL, NULL, NULL }
};

static GstCaps*
asf_asf_type_find (GstBuffer *buf, gpointer private)
{
  GstCaps *new;

  /*!!! Check here if the header is a valid ASF 1.0. return NULL otherwise */

  new = gst_caps_new (
                  "asf_type_find",
                  "video/x-ms-asf",
                  gst_props_new ("asfversion",
				 GST_PROPS_INT (1),
				 NULL));
  return new;
}

static GstCaps*
asf_wma_type_find (GstBuffer *buf, gpointer private)
{
  GstCaps *new;

  /*!!! Check here if the header is a valid ASF 1.0. return NULL otherwise */

  new = gst_caps_new (
                  "asf_type_find",
                  "video/x-ms-asf",
                  gst_props_new ("asfversion",
				 GST_PROPS_INT (1),
				 NULL));
  return new;
}

static GstCaps*
asf_wax_type_find (GstBuffer *buf, gpointer private)
{
  GstCaps *new;

  /*!!! Check here if the header is a valid ASF 1.0. return NULL otherwise */

  new = gst_caps_new (
                  "asf_type_find",
                  "video/x-ms-asf",
                  gst_props_new ("asfversion",
				 GST_PROPS_INT (1),
				 NULL));
  return new;
}

static GstCaps*
asf_wmv_type_find (GstBuffer *buf, gpointer private)
{
  GstCaps *new;

  /*!!! Check here if the header is a valid ASF 1.0. return NULL otherwise */

  new = gst_caps_new (
                  "asf_type_find",
                  "video/x-ms-asf",
                  gst_props_new ("asfversion",
				 GST_PROPS_INT (1),
				 NULL));
  return new;
}

static GstCaps*
asf_wvx_type_find (GstBuffer *buf, gpointer private)
{
  GstCaps *new;

  /*!!! Check here if the header is a valid ASF 1.0. return NULL otherwise */

  new = gst_caps_new (
                  "asf_type_find",
                  "video/x-ms-asf",
                  gst_props_new ("asfversion",
				 GST_PROPS_INT (1),
				 NULL));
  return new;
}

static GstCaps*
asf_wm_type_find (GstBuffer *buf, gpointer private)
{
  GstCaps *new;

  /*!!! Check here if the header is a valid ASF 1.0. return NULL otherwise */

  new = gst_caps_new (
                  "asf_type_find",
                  "video/x-ms-asf",
                  gst_props_new ("asfversion",
				 GST_PROPS_INT (1),
				 NULL));
  return new;
}

GST_PAD_TEMPLATE_FACTORY (sink_factory,
			  "sink",
			  GST_PAD_SINK,
			  GST_PAD_ALWAYS,
			  GST_CAPS_NEW ("asf_asf_demux_sink",
					"video/x-ms-asf",
					"asfversion", GST_PROPS_INT_RANGE (1, 1)
					),
                          GST_CAPS_NEW ("asf_wma_demux_sink",
					"video/x-ms-wma",
					"asfversion", GST_PROPS_INT_RANGE (1, 1)
					),
			  GST_CAPS_NEW ("asf_wax_demux_sink",
					"video/x-ms-wax",
					"asfversion", GST_PROPS_INT_RANGE (1, 1)
					),
			  GST_CAPS_NEW ("asf_wmv_demux_sink",
					"video/x-ms-wmv",
					"asfversion", GST_PROPS_INT_RANGE (1, 1)
					),
			  GST_CAPS_NEW ("asf_wvx_demux_sink",
					"video/x-ms-wvx",
					"asfversion", GST_PROPS_INT_RANGE (1, 1)
					),
			  GST_CAPS_NEW ("asf_wm_demux_sink",
					"video/x-ms-wm",
					"asfversion", GST_PROPS_INT_RANGE (1, 1)
					)
			  );

GST_PAD_TEMPLATE_FACTORY (audio_factory,
			  "audio_%02d",
			  GST_PAD_SRC,
			  GST_PAD_SOMETIMES,
			  GST_CAPS_NEW ("asf_demux_audio",
					"audio/x-wav",
					NULL
					)
			  );

GST_PAD_TEMPLATE_FACTORY (video_factory,
			  "video_%02d",
			  GST_PAD_SRC,
			  GST_PAD_SOMETIMES,
			  GST_CAPS_NEW ("asf_demux_video_mpeg4",
					"video/mpeg",
					"mpegversion",  GST_PROPS_INT (4),
					"systemstream",  GST_PROPS_BOOLEAN (FALSE),
					NULL
					)
			  );

static void 	gst_asf_demux_class_init	(GstASFDemuxClass *klass);
static void 	gst_asf_demux_init		(GstASFDemux *asf_demux);
static gboolean gst_asf_demux_send_event 	(GstElement *element, 
						 GstEvent *event);
static void 	gst_asf_demux_loop 		(GstElement *element);
static gboolean gst_asf_demux_process_object    (GstASFDemux *asf_demux,
						 guint64 *filepos);
static void     gst_asf_demux_get_property      (GObject *object, 
						 guint prop_id, 	
						 GValue *value, 
						 GParamSpec *pspec);
static guint32  gst_asf_demux_identify_guid (GstASFDemux *asf_demux,
				       GstASFGuidHash *guids,
				       GstASFGuid *guid_raw);

static gboolean gst_asf_demux_process_chunk (GstASFDemux *asf_demux,
					     asf_packet_info *packet_info,
					     asf_segment_info *segment_info);

static const GstEventMask* gst_asf_demux_get_src_event_mask (GstPad *pad);
static gboolean 	   gst_asf_demux_handle_src_event (GstPad *pad,
							   GstEvent *event);
static const GstFormat*    gst_asf_demux_get_src_formats  (GstPad *pad); 
static const GstPadQueryType* gst_asf_demux_get_src_query_types (GstPad *pad);
static gboolean 	   gst_asf_demux_handle_src_query (GstPad *pad,
							   GstPadQueryType type,
							   GstFormat *format, gint64 *value);

static GstElementStateReturn
		gst_asf_demux_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;

GType
asf_demux_get_type (void)
{
  static GType asf_demux_type = 0;

  if (!asf_demux_type) {
    static const GTypeInfo asf_demux_info = {
      sizeof(GstASFDemuxClass),      
      NULL,
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
  gint i;

  asf_demux->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (asf_demux), asf_demux->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (asf_demux), gst_asf_demux_loop);

  /* i think everything is already zero'd, but oh well*/
  for (i=0; i < GST_ASF_DEMUX_NUM_VIDEO_PADS; i++) {
    asf_demux->video_pad[i] = NULL;
    asf_demux->video_PTS[i] = 0;
  }
  for (i=0; i < GST_ASF_DEMUX_NUM_AUDIO_PADS; i++) {
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
  guint64 filepos = 0;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ASF_DEMUX (element));

  asf_demux = GST_ASF_DEMUX (element);

  asf_demux->restart = FALSE;

  g_print ("asf_demux_loop called\n");
  /* this is basically an infinite loop */
  if (!gst_asf_demux_process_object (asf_demux, &filepos)) {
    gst_element_error (element, "This doesn't appear to be an ASF stream");
    return;
  }
  if (!asf_demux->restart)
    /* if we exit the loop we are EOS */
    gst_pad_event_default (asf_demux->sinkpad, gst_event_new (GST_EVENT_EOS));
}

static inline gboolean
gst_asf_demux_read_object_header (GstASFDemux *asf_demux, guint32 *obj_id, guint64 *obj_size)
{
  guint32       got_bytes;
  GstASFGuid    *guid;
  guint64       *size;
  GstByteStream *bs = asf_demux->bs;


  /* First get the GUID */
  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&guid, sizeof(GstASFGuid));
  if (got_bytes < sizeof (GstASFGuid)) {
    guint32 remaining;
    GstEvent *event;
    
    gst_bytestream_get_status (bs, &remaining, &event);
    gst_event_unref (event);
    
    return FALSE;
  }

  *obj_id = gst_asf_demux_identify_guid (asf_demux, asf_object_guids, guid);

  gst_bytestream_flush (bs, sizeof (GstASFGuid));

  if (*obj_id == ASF_OBJ_UNDEFINED) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "Object found with unknown GUID %08x %08x %08x %08x", guid->v1, guid->v2, guid->v3, guid->v4);
    gst_element_error (GST_ELEMENT (asf_demux), "Could not identify object");
    return FALSE;
  }

  /* Now get the object size */
  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&size, sizeof (guint64));
  while (got_bytes < sizeof (guint64)) {
    guint32 remaining;
    GstEvent *event;
  
    gst_bytestream_get_status (bs, &remaining, &event);
    gst_event_unref (event);

    got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&size, sizeof (guint64));
  }

  *obj_size = GUINT64_FROM_LE (*size);
  gst_bytestream_flush (bs, sizeof (guint64));

  return TRUE;
}

static guint32 gst_asf_demux_read_var_length (GstASFDemux *asf_demux, guint8 type, guint32 *rsize)
{
  guint32 got_bytes;
  guint32 *var;
  guint32 ret = 0;
  GstByteStream *bs = asf_demux->bs;

  if (type == 0) {
    return 0;
  }
  
  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&var, 4);

  while (got_bytes < 4) {
    guint32 remaining;
    GstEvent *event;
  
    gst_bytestream_get_status (bs, &remaining, &event);
    gst_event_unref (event);

    got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&var, 4);
  }
    
  switch (type) {
  case 1:
    ret = GUINT32_FROM_LE(*var) & 0xff;
    gst_bytestream_flush (bs, 1);
    *rsize += 1;
    break;
  case 2:
    ret = GUINT32_FROM_LE(*var) & 0xffff;
    gst_bytestream_flush (bs, 2);
    *rsize += 2;
    break;
  case 3:
    ret = GUINT32_FROM_LE(*var);
    gst_bytestream_flush (bs, 4);
    *rsize += 4;
    break;
  }

  return ret;
}

static void gst_asf_demux_read_object_header_rest (GstASFDemux *asf_demux, guint8 **buf, guint32 size) {
  guint32       got_bytes;
  GstByteStream *bs = asf_demux->bs;

  got_bytes = gst_bytestream_peek_bytes (bs, buf, size);
  while (got_bytes < size) {
    guint32 remaining;
    GstEvent *event;
  
    gst_bytestream_get_status (bs, &remaining, &event);
    gst_event_unref (event);

    got_bytes = gst_bytestream_peek_bytes (bs, buf, size);
  }

  gst_bytestream_flush (bs, size);
}

static inline gboolean
gst_asf_demux_process_file (GstASFDemux *asf_demux, guint64 *filepos, guint64 *obj_size)
{
  asf_obj_file *object;
  guint64 packets;

  /* Get the rest of the header's header */
  gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&object, 80);
  packets = GUINT64_FROM_LE (object->packets_count);
  asf_demux->packet_size = GUINT32_FROM_LE (object->max_pktsize);
  asf_demux->play_time = (guint32) GUINT64_FROM_LE (object->play_time) / 10;

  GST_INFO (GST_CAT_PLUGIN_INFO, "Object is a file with %llu data packets", packets);

  return TRUE;
}


static inline gboolean
gst_asf_demux_process_header (GstASFDemux *asf_demux, guint64 *filepos, guint64 *obj_size)
{
  guint32 num_objects;
  asf_obj_header *object;
  guint32 i;
  

  /* Get the rest of the header's header */
  gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&object, 6);
  num_objects = GUINT32_FROM_LE (object->num_objects);

  GST_INFO (GST_CAT_PLUGIN_INFO, "Object is a header with %u parts", num_objects);  

  /* Loop through the header's objects, processing those */  
  for (i = 0; i < num_objects; i++) {
    if (!gst_asf_demux_process_object (asf_demux, filepos)) {
      return FALSE;
    }
  }

  return TRUE;
}

static inline gboolean
gst_asf_demux_process_segment (GstASFDemux       *asf_demux, 
			       asf_packet_info   *packet_info)
{
  guint8   *byte;
  gboolean key_frame;
  guint32  replic_size;
  guint8   time_delta;
  guint32  time_start;
  guint32  frag_timestamp;
  guint32  frag_size;
  guint32  rsize;
  asf_segment_info segment_info;

  gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&byte, 1); rsize = 1;
  segment_info.stream_number = *byte & 0x7f;
  key_frame = (*byte & 0x80) >> 7;
  
  GST_INFO (GST_CAT_PLUGIN_INFO, "Processing segment for stream %u", segment_info.stream_number);
  segment_info.sequence = gst_asf_demux_read_var_length (asf_demux, packet_info->seqtype, &rsize);
  segment_info.frag_offset = gst_asf_demux_read_var_length (asf_demux, packet_info->fragoffsettype, &rsize);
  replic_size = gst_asf_demux_read_var_length (asf_demux, packet_info->replicsizetype, &rsize);
  GST_DEBUG (GST_CAT_PLUGIN_INFO, "sequence = %x, frag_offset = %x, replic_size = %x", segment_info.sequence, segment_info.frag_offset, replic_size);

  if (replic_size > 1) {
    asf_replicated_data *replicated_data_header;
    guint8              **replicated_data = NULL;

    segment_info.compressed = FALSE;
    
    /* It's uncompressed with replic data*/
    if (replic_size < 8) {
      gst_element_error (GST_ELEMENT (asf_demux), "The payload has replicated data but the size is less than 8");
      return FALSE;
    }
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&replicated_data_header, 8);
    frag_timestamp = replicated_data_header->frag_timestamp;

    if (replic_size > 8) {
      gst_asf_demux_read_object_header_rest (asf_demux, replicated_data, replic_size - 8);
    }

    rsize += replic_size;
  } else {
    if (replic_size == 1) {
      /* It's compressed */
      segment_info.compressed = TRUE;
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&byte, 1); rsize++;
      time_delta = *byte;
    } else {
      segment_info.compressed = FALSE;
    }
    
    time_start = segment_info.frag_offset;
    segment_info.frag_offset = 0;
    frag_timestamp = asf_demux->timestamp;
  }

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "multiple = %u compressed = %u", packet_info->multiple, segment_info.compressed);

  if (packet_info->multiple) {
    frag_size = gst_asf_demux_read_var_length (asf_demux, packet_info->segsizetype, &rsize);
  } else {
    frag_size = packet_info->size_left - rsize;
  }

  packet_info->size_left -= rsize;
  
  GST_DEBUG (GST_CAT_PLUGIN_INFO, "size left = %u frag size = %u rsize = %u", packet_info->size_left, frag_size, rsize);

  if (segment_info.compressed) {
    while (frag_size > 0) {
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&byte, 1); 
      packet_info->size_left--;
      segment_info.chunk_size = *byte;

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

static inline gboolean
gst_asf_demux_process_data (GstASFDemux *asf_demux, guint64 *filepos, guint64 *obj_size)
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
  
  /* Get the rest of the header */
  gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&object, 26);
  packets = GUINT64_FROM_LE (object->packets);

  GST_INFO (GST_CAT_PLUGIN_INFO, "Object is data with %llu packets", packets); 

  for (packet = 0; packet < packets; packet++) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "Process packet %llu", packet);
    
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&buf, 1); rsize=1;
    if (*buf & 0x80) {
      asf_obj_data_correction *correction_object;
      
      /* Uses error correction */
      correction = TRUE;
      GST_DEBUG (GST_CAT_PLUGIN_INFO, "Data has error correction (%x)", *buf);
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&correction_object, 2); rsize += 2;
    }
    
    /* Read the packet flags */
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&packet_properties_object, 2); rsize += 2;
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
    
    GST_DEBUG (GST_CAT_PLUGIN_INFO, "Multiple = %u, Sequence = %u, Padsize = %u, Packet length = %u", packet_info.multiple, sequence, packet_info.padsize, packet_length);
    
    /* Read the property flags */
    packet_info.replicsizetype = property & 0x03;
    packet_info.fragoffsettype = (property >> 2) & 0x03;
    packet_info.seqtype = (property >> 4) & 0x03;
    
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&timestamp, 4);
    asf_demux->timestamp = *timestamp;
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&duration, 2);
    
    rsize += 6;
    
    GST_DEBUG (GST_CAT_PLUGIN_INFO, "Timestamp = %x, Duration = %x", asf_demux->timestamp, *duration);
    
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

    GST_DEBUG (GST_CAT_PLUGIN_INFO, "rsize: %u size left: %u", rsize, packet_info.size_left);
    
    for (segment = 0; segment < segments; segment++) {
      if (!gst_asf_demux_process_segment (asf_demux, &packet_info))
	return FALSE;
    }

    /* Skip the padding */
    if (packet_info.padsize > 0)
      gst_bytestream_flush (asf_demux->bs, packet_info.padsize);
      

    GST_DEBUG (GST_CAT_PLUGIN_INFO, "Remaining size left: %u", packet_info.size_left);
    
    if (packet_info.size_left > 0) {
      gst_element_error (GST_ELEMENT (asf_demux), "The packet's header indicated a length longer than its contents");
      return FALSE;
    }
  }
  
  return TRUE;
}

static inline gboolean
gst_asf_demux_process_stream (GstASFDemux *asf_demux, guint64 *filepos, guint64 *obj_size)
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
  GstPad                  **outpad = NULL;
  GstPadTemplate          *new_template = NULL;
  gchar                   *name = NULL;
  asf_stream_context      *stream = NULL;

  /* Get the rest of the header's header */
  gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&object, 54);

  /* Identify the stream type */
  stream_id = gst_asf_demux_identify_guid (asf_demux, asf_stream_guids, &(object->type));
  correction = gst_asf_demux_identify_guid (asf_demux, asf_correction_guids, &(object->correction));


  switch (stream_id) {
  case ASF_STREAM_AUDIO:
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&audio_object, 18);
    size = GUINT16_FROM_LE (audio_object->size);

    name = g_strdup_printf ("audio_%02d", asf_demux->num_audio_streams);
    outpad = &asf_demux->audio_pad[asf_demux->num_audio_streams++];
    new_template = GST_PAD_TEMPLATE_GET (audio_factory);

    GST_INFO (GST_CAT_PLUGIN_INFO, "Object is an audio stream with %u bytes of additional data. Assigned to pad '%s'", size, name);
    switch (correction) {
    case ASF_CORRECTION_ON:
      GST_INFO (GST_CAT_PLUGIN_INFO, "Using error correction");
      gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&correction_object, 8);
      break;
    case ASF_CORRECTION_OFF:
      break;
    default:
      gst_element_error (GST_ELEMENT (asf_demux), "Audio stream using unknown error correction");
      return FALSE;
    }
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
  case ASF_STREAM_VIDEO:
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&video_object, 11);
    size = GUINT16_FROM_BE(video_object->size) - 40; /* Byte order gets 
						      * offset by single 
						      * byte */
    name = g_strdup_printf ("video_%02d", asf_demux->num_video_streams);
    outpad = &asf_demux->video_pad[asf_demux->num_video_streams++];
    new_template = GST_PAD_TEMPLATE_GET (video_factory);

    /*!!! Set for MJPG too */

    GST_INFO (GST_CAT_PLUGIN_INFO, "Object is a video stream with %u bytes of additional data. Assigned to pad '%s'", size, name);
    gst_asf_demux_read_object_header_rest (asf_demux, (guint8**)&video_format_object, 40);
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
  
  *outpad = gst_pad_new_from_template (new_template, name);

  /* Set up the pad */
  gst_pad_try_set_caps (*outpad, gst_pad_get_pad_template_caps (*outpad));
  gst_pad_set_formats_function (*outpad, gst_asf_demux_get_src_formats);
  gst_pad_set_event_mask_function (*outpad, gst_asf_demux_get_src_event_mask);
  gst_pad_set_event_function (*outpad, gst_asf_demux_handle_src_event);
  gst_pad_set_query_type_function (*outpad, gst_asf_demux_get_src_query_types);
  gst_pad_set_query_function (*outpad, gst_asf_demux_handle_src_query);

  /* Initialise the stream context */
  stream = &asf_demux->stream[asf_demux->num_streams];
  stream->pad = *outpad;
  stream->frag_offset = 0;
  stream->sequence = 0;
  gst_pad_set_element_private (*outpad, stream);
  asf_demux->num_streams++;

  /* Add the pad to the element */
  gst_element_add_pad (GST_ELEMENT (asf_demux), *outpad);
  
  return TRUE;
}

static gboolean
gst_asf_demux_skip_object (GstASFDemux *asf_demux, guint64 *filepos, guint64 *obj_size)
{
  GstByteStream *bs = asf_demux->bs;

  GST_INFO (GST_CAT_PLUGIN_INFO, "Skipping object...");

  gst_bytestream_flush (bs, *obj_size - 24);

  return TRUE;
}

static gboolean
gst_asf_demux_process_object    (GstASFDemux *asf_demux,
				 guint64 *filepos) {

  guint32 obj_id;
  guint64 obj_size;

  if (!gst_asf_demux_read_object_header (asf_demux, &obj_id, &obj_size)) {
    g_print ("  *****  Error reading object at filepos 0x%08llx\n", *filepos);
    return FALSE;
  }

  GST_INFO (GST_CAT_PLUGIN_INFO, "Found object %u with size %llx", obj_id, obj_size);

  switch (obj_id) {
  case ASF_OBJ_STREAM:
    gst_asf_demux_process_stream (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_DATA:
    gst_asf_demux_process_data (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_FILE:
    gst_asf_demux_process_file (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_HEADER:
    gst_asf_demux_process_header (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_CONCEAL_NONE:
    break;
  case ASF_OBJ_COMMENT:
    gst_asf_demux_skip_object (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_CODEC_COMMENT:
    gst_asf_demux_skip_object (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_INDEX:
    gst_asf_demux_skip_object (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_HEAD1:
    gst_asf_demux_skip_object (asf_demux, filepos, &obj_size);
    break;
  case ASF_OBJ_HEAD2:
    break;
  case ASF_OBJ_PADDING:
    gst_asf_demux_skip_object (asf_demux, filepos, &obj_size);
    break;
  default:
    gst_element_error (GST_ELEMENT (asf_demux), "Unknown ASF object");
  }
    

  return TRUE;
}

static gboolean gst_asf_demux_process_chunk (GstASFDemux *asf_demux,
					     asf_packet_info *packet_info,
					     asf_segment_info *segment_info)
{
  asf_stream_context *stream;
  GstFormat          format;
  guint64            next_ts;
  guint32            got_bytes;
  GstByteStream      *bs = asf_demux->bs;

  if (segment_info->stream_number > asf_demux->num_streams) {
    gst_element_error (GST_ELEMENT (asf_demux), "Segment found for stream out of range");
    return FALSE;
  }
  stream = &asf_demux->stream[segment_info->stream_number - 1];
  
  GST_DEBUG (GST_CAT_PLUGIN_INFO, "Processing chunk of size %u", segment_info->chunk_size);
  
  if (stream->frag_offset == 0) {
    /* new packet */
    stream->sequence = segment_info->sequence;
  } else {
    if (segment_info->sequence == stream->sequence && 
	segment_info->frag_offset == stream->frag_offset) {
      /* continuing packet */
    } else {
      /* cannot continue current packet: free it */
      stream->frag_offset = 0;
      if (segment_info->frag_offset != 0) {
	/* cannot create new packet */
	gst_bytestream_flush (bs, segment_info->chunk_size);
	packet_info->size_left -= segment_info->chunk_size;
	return TRUE;
      } else {
	/* create new packet */
	stream->sequence = segment_info->sequence;
      }
    }
  }
  
  format = GST_FORMAT_TIME;
  gst_pad_query (stream->pad, GST_PAD_QUERY_POSITION, &format, &next_ts);

  if (GST_PAD_IS_CONNECTED (stream->pad)) {
    GstBuffer *buf;
    
    got_bytes = gst_bytestream_peek (bs, &buf, segment_info->chunk_size);

    GST_BUFFER_TIMESTAMP (buf) = next_ts;

    /*!!! Should handle flush events here? */
    
    gst_pad_push (stream->pad, buf);
  }

  gst_bytestream_flush (bs, segment_info->chunk_size);
  packet_info->size_left -= segment_info->chunk_size;

  return TRUE;
}

static gboolean
gst_asf_demux_handle_src_event (GstPad *pad, GstEvent *event)
{
  GST_DEBUG (0,"asfdemux: handle_src_event");
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

static const GstPadQueryType* 
gst_asf_demux_get_src_query_types (GstPad *pad) {
  static const GstPadQueryType types[] = {
    GST_PAD_QUERY_TOTAL,
    GST_PAD_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_asf_demux_handle_src_query (GstPad *pad,
				GstPadQueryType type,
				GstFormat *format, gint64 *value)
{
  GstASFDemux *asf_demux;
  gboolean res = TRUE;

  asf_demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  switch (type) {
  case GST_PAD_QUERY_TOTAL:
    switch (*format) {
    case GST_FORMAT_DEFAULT:
      *format = GST_FORMAT_TIME;
      /* fall through */
    case GST_FORMAT_TIME:
      *value = (GST_SECOND / 1000) * asf_demux->play_time;
      break;
    default:
      res = FALSE;
    }
    break;
  case GST_PAD_QUERY_POSITION:
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
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (asf_demux->bs);
      asf_demux->restart = TRUE;
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
		       GstASFGuidHash *guids,
		       GstASFGuid *guid_raw)
{
  guint32 i;
  GstASFGuid guid;

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

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;
  gint i = 0;

  /* this filter needs bytestream */
  if (!gst_library_load ("gstbytestream")) {
    gst_info("asfdemux: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }
  
  /* create an elementfactory for the asf_demux element */
  factory = gst_element_factory_new ("asfdemux",GST_TYPE_ASF_DEMUX,
                                    &gst_asf_demux_details);

  while (asf_type_definitions[i].name) {
    type = gst_type_factory_new (&asf_type_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }

  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_NONE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (audio_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (video_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "asfdemux",
  plugin_init
};
