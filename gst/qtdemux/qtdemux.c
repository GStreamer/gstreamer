/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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
#include "qtdemux.h"

#include <string.h>
#include <zlib.h>

#define QTDEMUX_GUINT32_GET(a) GUINT32_FROM_BE(*(guint32 *)(a))
#define QTDEMUX_GUINT16_GET(a) GUINT16_FROM_BE(*(guint16 *)(a))
#define QTDEMUX_GUINT8_GET(a) (*(guint8 *)(a))
#define QTDEMUX_FP32_GET(a) (GUINT32_FROM_BE(*(guint16 *)(a))/65536.0)
#define QTDEMUX_FP16_GET(a) (GUINT16_FROM_BE(*(guint16 *)(a))/256.0)
#define QTDEMUX_FOURCC_GET(a) GUINT32_FROM_LE(*(guint32 *)(a))

#define QTDEMUX_GUINT64_GET(a) ((((guint64)QTDEMUX_GUINT32_GET(a))<<32)|QTDEMUX_GUINT32_GET(((void *)a)+4))

typedef struct _QtNode QtNode;
typedef struct _QtNodeType QtNodeType;
typedef struct _QtDemuxSample QtDemuxSample;
//typedef struct _QtDemuxStream QtDemuxStream;

struct _QtNode {
  guint32 type;
  gpointer data;
  int len;
};

struct _QtNodeType {
  guint32 fourcc;
  char *name;
  int flags;
  void (*dump)(GstQTDemux *qtdemux, void *buffer, int depth);
};

struct _QtDemuxSample {
  int sample_index;
  int chunk;
  int size;
  guint32 offset;
  guint64 timestamp;
  guint64 duration;
};

struct _QtDemuxStream {
  guint32 subtype;
  GstCaps *caps;
  GstPad *pad;
  int n_samples;
  QtDemuxSample *samples;
  int timescale;

  int sample_index;

  int width;
  int height;
  float fps;
  double rate;
  int n_channels;
};

enum QtDemuxState {
  QTDEMUX_STATE_NULL,
  QTDEMUX_STATE_HEADER,
  QTDEMUX_STATE_HEADER_SEEKING,
  QTDEMUX_STATE_SEEKING,
  QTDEMUX_STATE_MOVIE,
  QTDEMUX_STATE_SEEKING_EOS,
  QTDEMUX_STATE_EOS,
};

static GNode *qtdemux_tree_get_child_by_type(GNode *node, guint32 fourcc);
static GNode *qtdemux_tree_get_sibling_by_type(GNode *node, guint32 fourcc);

static GstElementDetails 
gst_qtdemux_details = 
{
  "QuickTime Demuxer",
  "Codec/Demuxer",
  "Demultiplex a QuickTime file into audio and video streams",
  "David Schleef <ds@schleef.org>"
};

enum {
  LAST_SIGNAL
};

enum {
  ARG_0
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "qtdemux_sink",
    "video/quicktime",
      NULL
  )
)

static GstPadTemplate *videosrctempl, *audiosrctempl;
static GstElementClass *parent_class = NULL;

static void gst_qtdemux_class_init (GstQTDemuxClass *klass);
static void gst_qtdemux_base_init (GstQTDemuxClass *klass);
static void gst_qtdemux_init (GstQTDemux *quicktime_demux);
static GstElementStateReturn gst_qtdemux_change_state(GstElement *element);
static void gst_qtdemux_loop_header (GstElement *element);
static gboolean gst_qtdemux_handle_sink_event (GstQTDemux *qtdemux);

static void qtdemux_parse_moov(GstQTDemux *qtdemux, void *buffer, int length);
static void qtdemux_parse(GstQTDemux *qtdemux, GNode *node, void *buffer, int length);
static QtNodeType *qtdemux_type_get(guint32 fourcc);
static void qtdemux_node_dump(GstQTDemux *qtdemux, GNode *node);
static void qtdemux_parse_tree(GstQTDemux *qtdemux);
static GstCaps *qtdemux_video_caps(GstQTDemux *qtdemux, guint32 fourcc);
static GstCaps *qtdemux_audio_caps(GstQTDemux *qtdemux, guint32 fourcc);

static GType gst_qtdemux_get_type (void) 
{
  static GType qtdemux_type = 0;

  if (!qtdemux_type) {
    static const GTypeInfo qtdemux_info = {
      sizeof(GstQTDemuxClass),
      (GBaseInitFunc)gst_qtdemux_base_init, NULL,
      (GClassInitFunc)gst_qtdemux_class_init,
      NULL, NULL, sizeof(GstQTDemux), 0,
      (GInstanceInitFunc)gst_qtdemux_init,
    };
    qtdemux_type = g_type_register_static (GST_TYPE_ELEMENT, "GstQTDemux", &qtdemux_info, 0);
  }
  return qtdemux_type;
}

static void gst_qtdemux_base_init (GstQTDemuxClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (sink_templ));
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_set_details (element_class, &gst_qtdemux_details);
}

static void gst_qtdemux_class_init (GstQTDemuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_qtdemux_change_state;
}

static void 
gst_qtdemux_init (GstQTDemux *qtdemux) 
{
  qtdemux->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_set_loop_function (GST_ELEMENT (qtdemux), gst_qtdemux_loop_header);
  gst_element_add_pad (GST_ELEMENT (qtdemux), qtdemux->sinkpad);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  GstCaps *audiocaps = NULL, *videocaps = NULL, *temp;
  const guint32 audio_fcc[] = {
    /* FILLME */
    0,
  }, video_fcc[] = {
    /* FILLME */
    0,
  };
  gint i;

  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  for (i = 0; audio_fcc[i] != 0; i++) {
    temp = qtdemux_audio_caps (NULL, audio_fcc[i]);
    audiocaps = gst_caps_append (audiocaps, temp);
  }
  audiosrctempl = gst_pad_template_new ("audio_%02d",
					GST_PAD_SRC,
					GST_PAD_SOMETIMES,
					audiocaps, NULL);

  for (i = 0; video_fcc[i] != 0; i++) {
    temp = qtdemux_video_caps (NULL, video_fcc[i]);
    videocaps = gst_caps_append (videocaps, temp);
  }
  videosrctempl = gst_pad_template_new ("video_%02d",
					GST_PAD_SRC,
					GST_PAD_SOMETIMES,
					videocaps, NULL);

  return gst_element_register (plugin, "qtdemux",
			       GST_RANK_PRIMARY, GST_TYPE_QTDEMUX);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "qtdemux",
  "Quicktime stream demuxer",
  plugin_init,
  VERSION,
  "LGPL",
  GST_COPYRIGHT,
  GST_PACKAGE,
  GST_ORIGIN
)

static gboolean gst_qtdemux_handle_sink_event (GstQTDemux *qtdemux)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;

  gst_bytestream_get_status(qtdemux->bs, &remaining, &event);

  type = event ? GST_EVENT_TYPE(event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("qtdemux: event %p %d", event, type);

  switch(type){
    case GST_EVENT_EOS:
      gst_bytestream_flush(qtdemux->bs, remaining);
      gst_pad_event_default(qtdemux->sinkpad, event);
      return FALSE;
    case GST_EVENT_FLUSH:
      g_warning("flush event");
      break;
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG ("discontinuous event\n");
      //gst_bytestream_flush_fast(qtdemux->bs, remaining);
      break;
    default:
      g_warning("unhandled event %d",type);
      break;
  }

  gst_event_unref(event);
  return TRUE;
}

static GstElementStateReturn gst_qtdemux_change_state(GstElement *element)
{
  GstQTDemux *qtdemux = GST_QTDEMUX(element);

  switch(GST_STATE_TRANSITION(element)){
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      qtdemux->bs = gst_bytestream_new(qtdemux->sinkpad);
      qtdemux->state = QTDEMUX_STATE_HEADER;
      /* FIXME */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy(qtdemux->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS(parent_class)->change_state(element);
}

static void gst_qtdemux_loop_header (GstElement *element)
{
  GstQTDemux *qtdemux = GST_QTDEMUX(element);
  guint8 *data;
  guint32 length;
  guint32 fourcc;
  GstBuffer *buf;
  int offset;
  int cur_offset;
  int size;
  int ret;

  /* FIXME _tell gets the offset wrong */
  //cur_offset = gst_bytestream_tell(qtdemux->bs);
  
  cur_offset = qtdemux->offset;
  GST_DEBUG ("loop at position %d",cur_offset);

  switch(qtdemux->state){
  case QTDEMUX_STATE_HEADER:
  {
    do{
      ret = gst_bytestream_peek_bytes(qtdemux->bs, &data, 16);
      if(ret<16){
        if(!gst_qtdemux_handle_sink_event(qtdemux)){
          return;
	}
      }else{
	break;
      }
    }while(1);

    length = GUINT32_FROM_BE(*(guint32 *)data);
    GST_DEBUG ("length %08x",length);
    fourcc = GUINT32_FROM_LE(*(guint32 *)(data+4));
    GST_DEBUG ("fourcc " GST_FOURCC_FORMAT, GST_FOURCC_ARGS(fourcc));

    if(length==0){
      length = gst_bytestream_length(qtdemux->bs) - cur_offset;
    }
    if(length==1){
      guint32 length1, length2;
  
      length1 = GUINT32_FROM_BE(*(guint32 *)(data+8));
      GST_DEBUG ("length1 %08x",length1);
      length2 = GUINT32_FROM_BE(*(guint32 *)(data+12));
      GST_DEBUG ("length2 %08x",length2);
  
      length=length2;
    }
  
    switch(fourcc){
      case GST_MAKE_FOURCC('m','d','a','t'):
      case GST_MAKE_FOURCC('f','r','e','e'):
      case GST_MAKE_FOURCC('w','i','d','e'):
      case GST_MAKE_FOURCC('P','I','C','T'):
      case GST_MAKE_FOURCC('p','n','o','t'):
        break;
      case GST_MAKE_FOURCC('m','o','o','v'):
      {
        GstBuffer *moov;
  
	do{
          ret = gst_bytestream_read(qtdemux->bs, &moov, length);
          if(ret < length){
            GST_DEBUG ("read failed (%d < %d)",ret,length);
            if(!gst_qtdemux_handle_sink_event(qtdemux)){
	      return;
	    }
          }else{
	    break;
	  }
	}while(1);

        qtdemux_parse_moov(qtdemux, GST_BUFFER_DATA(moov), length);
        if(0)qtdemux_node_dump(qtdemux, qtdemux->moov_node);
        qtdemux_parse_tree(qtdemux);
        qtdemux->state = QTDEMUX_STATE_MOVIE;
        break;
      }
      default:
      {
        g_print("unknown %08x '" GST_FOURCC_FORMAT "' at %d\n",
	    fourcc, GST_FOURCC_ARGS(fourcc), cur_offset);
        break;
      }
    }
    ret = gst_bytestream_seek(qtdemux->bs, cur_offset + length,
        GST_SEEK_METHOD_SET);
    qtdemux->offset = cur_offset + length;
    GST_DEBUG ("seek returned %d\n",ret);
    break;
  }
  case QTDEMUX_STATE_SEEKING_EOS:
  {
    guint8 *data;

    do{
      ret = gst_bytestream_peek_bytes(qtdemux->bs, &data, 1);
      if(ret<1){
        if(!gst_qtdemux_handle_sink_event(qtdemux)){
	  return;
        }
      }else{
	break;
      }
    }while(TRUE);
    gst_element_set_eos(element);

    qtdemux->state = QTDEMUX_STATE_EOS;
    return;
  }
  case QTDEMUX_STATE_EOS:
    g_warning("spinning in EOS\n");
    return;
  case QTDEMUX_STATE_MOVIE:
  {
    QtDemuxStream *stream;
    guint64 min_time;
    int index = -1;
    int i;

    min_time = G_MAXUINT64;
    for(i=0;i<qtdemux->n_streams;i++){
      stream = qtdemux->streams[i];

      if(stream->sample_index < stream->n_samples &&
	  stream->samples[stream->sample_index].timestamp < min_time){
	min_time = stream->samples[stream->sample_index].timestamp;
	index = i;
      }
    }

    if(index==-1){
      for(i=0;i<qtdemux->n_streams;i++){
        gst_pad_push(qtdemux->streams[i]->pad,
	    GST_DATA(gst_event_new (GST_EVENT_EOS)));
      }
      ret = gst_bytestream_seek(qtdemux->bs, 0, GST_SEEK_METHOD_END);
      GST_DEBUG ("seek returned %d",ret);

      qtdemux->state = QTDEMUX_STATE_SEEKING_EOS;
      return;
    }

    stream = qtdemux->streams[index];

    offset = stream->samples[stream->sample_index].offset;
    size = stream->samples[stream->sample_index].size;

    GST_DEBUG ("pushing from stream %d, sample_index=%d offset=%d size=%d",
	index, stream->sample_index, offset, size);

    cur_offset = gst_bytestream_tell(qtdemux->bs);
    if(offset != cur_offset){
      GST_DEBUG ("seeking to offset %d",offset);
      ret = gst_bytestream_seek(qtdemux->bs, offset, GST_SEEK_METHOD_SET);
      GST_DEBUG ("seek returned %d",ret);
      return;
    }

    GST_DEBUG ("reading %d bytes\n",size);
    buf = NULL;
    do{
      ret = gst_bytestream_read(qtdemux->bs, &buf, size);
      if(ret < size){
        GST_DEBUG ("read failed (%d < %d)",ret,size);
        if(!gst_qtdemux_handle_sink_event(qtdemux)){
	  return;
	}
      }else{
	break;
      }
    }while(TRUE);

    if(buf){
      /* hum... */
      if(stream->subtype == GST_MAKE_FOURCC('v','i','d','e')){
        float fps = 1. * GST_SECOND / stream->samples[stream->sample_index].duration;
        if (fps != stream->fps) {
          gst_props_remove_entry_by_name(stream->caps->properties, "framerate");
          gst_props_add_entry(stream->caps->properties,
                              gst_props_entry_new("framerate", GST_PROPS_FLOAT(fps)));
          stream->fps = fps;
          gst_pad_try_set_caps(stream->pad, stream->caps);
        }
      }

      GST_BUFFER_TIMESTAMP(buf) = stream->samples[stream->sample_index].timestamp;
      GST_BUFFER_DURATION(buf) = stream->samples[stream->sample_index].duration;
      gst_pad_push(stream->pad, GST_DATA (buf));
    }
    stream->sample_index++;
    break;
  }
  default:
    /* unreached */
    g_assert(0);
  }

}

static GstCaps *gst_qtdemux_src_getcaps(GstPad *pad, GstCaps *caps)
{
  GstQTDemux *qtdemux;
  QtDemuxStream *stream;
  int i;

  GST_DEBUG ("gst_qtdemux_src_getcaps");

  qtdemux = GST_QTDEMUX(gst_pad_get_parent(pad));

  g_return_val_if_fail(GST_IS_QTDEMUX(qtdemux), NULL);

  GST_DEBUG ("looking for pad %p in qtdemux %p", pad, qtdemux);
  GST_DEBUG ("n_streams is %d", qtdemux->n_streams);
  for(i=0;i<qtdemux->n_streams;i++){
    stream = qtdemux->streams[i];
    if(stream->pad == pad){
      return stream->caps;
    }
  }

  GST_DEBUG ("Couldn't find stream cooresponding to pad\n");

  return NULL;
}

static GstPadLinkReturn
gst_qtdemux_src_link(GstPad *pad, GstCaps *caps)
{
  GstQTDemux *qtdemux;
  QtDemuxStream *stream;
  int i;

  GST_DEBUG ("gst_qtdemux_src_link");

  qtdemux = GST_QTDEMUX(gst_pad_get_parent(pad));

  GST_DEBUG ("looking for pad %p in qtdemux %p", pad, qtdemux);
  g_return_val_if_fail(GST_IS_QTDEMUX(qtdemux), GST_PAD_LINK_REFUSED);

  GST_DEBUG ("n_streams is %d", qtdemux->n_streams);
  for(i=0;i<qtdemux->n_streams;i++){
    stream = qtdemux->streams[i];
    GST_DEBUG ("pad[%d] is %p", i, stream->pad);
    if(stream->pad == pad){
      return GST_PAD_LINK_OK;
    }
  }

  GST_DEBUG ("Couldn't find stream cooresponding to pad\n");

  return GST_PAD_LINK_REFUSED;
}

void gst_qtdemux_add_stream(GstQTDemux *qtdemux, QtDemuxStream *stream)
{
  if(stream->subtype == GST_MAKE_FOURCC('v','i','d','e')){
    stream->pad = gst_pad_new_from_template (videosrctempl,
        g_strdup_printf ("video_%02d", qtdemux->n_video_streams));
    stream->fps = 1. * GST_SECOND / stream->samples[0].duration;
    if(stream->caps){
      GstProps *properties = gst_props_intersect(
	  stream->caps->properties,
	  gst_props_new("width",GST_PROPS_INT(stream->width),
              "height",GST_PROPS_INT(stream->height),
              "framerate", GST_PROPS_FLOAT(stream->fps), NULL));
      if (stream->caps->properties != NULL)
        gst_props_unref (stream->caps->properties);
      stream->caps->properties = properties;
    }
    qtdemux->n_video_streams++;
  }else{
    stream->pad = gst_pad_new_from_template (audiosrctempl,
        g_strdup_printf ("audio_%02d", qtdemux->n_audio_streams));
    if(stream->caps){
      GstProps *properties = gst_props_intersect(
	  stream->caps->properties,
	  gst_props_new("rate",GST_PROPS_INT((int)stream->rate),
              "channels",GST_PROPS_INT(stream->n_channels), NULL));
      if (stream->caps->properties != NULL)
        gst_props_unref (stream->caps->properties);
      stream->caps->properties = properties;
    }
    qtdemux->n_audio_streams++;
  }

  gst_pad_set_getcaps_function(stream->pad, gst_qtdemux_src_getcaps);
  gst_pad_set_link_function(stream->pad, gst_qtdemux_src_link);

  qtdemux->streams[qtdemux->n_streams] = stream;
  qtdemux->n_streams++;
  GST_DEBUG ("n_streams is now %d", qtdemux->n_streams);

  GST_DEBUG ("adding pad %p to qtdemux %p", stream->pad, qtdemux);
  gst_element_add_pad(GST_ELEMENT (qtdemux), stream->pad);

  /* Note: we need to have everything set up before calling try_set_caps */
  if(stream->caps){
    g_print("setting caps to %s\n",gst_caps_to_string(stream->caps));

    gst_pad_try_set_caps(stream->pad, stream->caps);
  }
}


#define QT_CONTAINER 1

#define FOURCC_moov	GST_MAKE_FOURCC('m','o','o','v')
#define FOURCC_mvhd	GST_MAKE_FOURCC('m','v','h','d')
#define FOURCC_clip	GST_MAKE_FOURCC('c','l','i','p')
#define FOURCC_trak	GST_MAKE_FOURCC('t','r','a','k')
#define FOURCC_udta	GST_MAKE_FOURCC('u','d','t','a')
#define FOURCC_ctab	GST_MAKE_FOURCC('c','t','a','b')
#define FOURCC_tkhd	GST_MAKE_FOURCC('t','k','h','d')
#define FOURCC_crgn	GST_MAKE_FOURCC('c','r','g','n')
#define FOURCC_matt	GST_MAKE_FOURCC('m','a','t','t')
#define FOURCC_kmat	GST_MAKE_FOURCC('k','m','a','t')
#define FOURCC_edts	GST_MAKE_FOURCC('e','d','t','s')
#define FOURCC_elst	GST_MAKE_FOURCC('e','l','s','t')
#define FOURCC_load	GST_MAKE_FOURCC('l','o','a','d')
#define FOURCC_tref	GST_MAKE_FOURCC('t','r','e','f')
#define FOURCC_imap	GST_MAKE_FOURCC('i','m','a','p')
#define FOURCC___in	GST_MAKE_FOURCC(' ',' ','i','n')
#define FOURCC___ty	GST_MAKE_FOURCC(' ',' ','t','y')
#define FOURCC_mdia	GST_MAKE_FOURCC('m','d','i','a')
#define FOURCC_mdhd	GST_MAKE_FOURCC('m','d','h','d')
#define FOURCC_hdlr	GST_MAKE_FOURCC('h','d','l','r')
#define FOURCC_minf	GST_MAKE_FOURCC('m','i','n','f')
#define FOURCC_vmhd	GST_MAKE_FOURCC('v','m','h','d')
#define FOURCC_smhd	GST_MAKE_FOURCC('s','m','h','d')
#define FOURCC_gmhd	GST_MAKE_FOURCC('g','m','h','d')
#define FOURCC_gmin	GST_MAKE_FOURCC('g','m','i','n')
#define FOURCC_dinf	GST_MAKE_FOURCC('d','i','n','f')
#define FOURCC_dref	GST_MAKE_FOURCC('d','r','e','f')
#define FOURCC_stbl	GST_MAKE_FOURCC('s','t','b','l')
#define FOURCC_stsd	GST_MAKE_FOURCC('s','t','s','d')
#define FOURCC_stts	GST_MAKE_FOURCC('s','t','t','s')
#define FOURCC_stss	GST_MAKE_FOURCC('s','t','s','s')
#define FOURCC_stsc	GST_MAKE_FOURCC('s','t','s','c')
#define FOURCC_stsz	GST_MAKE_FOURCC('s','t','s','z')
#define FOURCC_stco	GST_MAKE_FOURCC('s','t','c','o')
#define FOURCC_vide	GST_MAKE_FOURCC('v','i','d','e')
#define FOURCC_soun	GST_MAKE_FOURCC('s','o','u','n')
#define FOURCC_co64	GST_MAKE_FOURCC('c','o','6','4')
#define FOURCC_cmov	GST_MAKE_FOURCC('c','m','o','v')
#define FOURCC_dcom	GST_MAKE_FOURCC('d','c','o','m')
#define FOURCC_cmvd	GST_MAKE_FOURCC('c','m','v','d')


static void qtdemux_dump_mvhd(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_tkhd(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_elst(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_mdhd(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_hdlr(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_vmhd(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_dref(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_stsd(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_stts(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_stss(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_stsc(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_stsz(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_stco(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_co64(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_dcom(GstQTDemux *qtdemux, void *buffer, int depth);
static void qtdemux_dump_cmvd(GstQTDemux *qtdemux, void *buffer, int depth);

QtNodeType qt_node_types[] = {
  { FOURCC_moov, "movie",		QT_CONTAINER, },
  { FOURCC_mvhd, "movie header",	0,
  	qtdemux_dump_mvhd },
  { FOURCC_clip, "clipping",		QT_CONTAINER, },
  { FOURCC_trak, "track",		QT_CONTAINER, },
  { FOURCC_udta, "user data",		0, }, /* special container */
  { FOURCC_ctab, "color table",		0, },
  { FOURCC_tkhd, "track header",	0,
  	qtdemux_dump_tkhd },
  { FOURCC_crgn, "clipping region",	0, },
  { FOURCC_matt, "track matte",		QT_CONTAINER, },
  { FOURCC_kmat, "compressed matte",	0, },
  { FOURCC_edts, "edit",		QT_CONTAINER, },
  { FOURCC_elst, "edit list",		0,
  	qtdemux_dump_elst },
  { FOURCC_load, "track load settings",	0, },
  { FOURCC_tref, "track reference",	QT_CONTAINER, },
  { FOURCC_imap, "track input map",	QT_CONTAINER, },
  { FOURCC___in, "track input",		0, }, /* special container */
  { FOURCC___ty, "input type",		0, },
  { FOURCC_mdia, "media",		QT_CONTAINER },
  { FOURCC_mdhd, "media header",	0,
  	qtdemux_dump_mdhd },
  { FOURCC_hdlr, "handler reference",	0,
  	qtdemux_dump_hdlr },
  { FOURCC_minf, "media information",	QT_CONTAINER },
  { FOURCC_vmhd, "video media information", 0,
  	qtdemux_dump_vmhd },
  { FOURCC_smhd, "sound media information", 0 },
  { FOURCC_gmhd, "base media information header", 0 },
  { FOURCC_gmin, "base media info",	0 },
  { FOURCC_dinf, "data information",	QT_CONTAINER },
  { FOURCC_dref, "data reference",	0,
  	qtdemux_dump_dref },
  { FOURCC_stbl, "sample table",	QT_CONTAINER },
  { FOURCC_stsd, "sample description",	0,
  	qtdemux_dump_stsd },
  { FOURCC_stts, "time-to-sample",	0,
  	qtdemux_dump_stts },
  { FOURCC_stss, "sync sample",		0,
  	qtdemux_dump_stss },
  { FOURCC_stsc, "sample-to-chunk",	0,
  	qtdemux_dump_stsc },
  { FOURCC_stsz, "sample size",		0,
  	qtdemux_dump_stsz },
  { FOURCC_stco, "chunk offset",	0,
  	qtdemux_dump_stco },
  { FOURCC_co64, "64-bit chunk offset",	0,
  	qtdemux_dump_co64 },
  { FOURCC_vide, "video media",		0 },
  { FOURCC_cmov, "compressed movie",	QT_CONTAINER },
  { FOURCC_dcom, "compressed data",	0,
  	qtdemux_dump_dcom },
  { FOURCC_cmvd, "compressed movie data", 0,
  	qtdemux_dump_cmvd },
  { 0, "unknown", 0 },
};
static int n_qt_node_types = sizeof(qt_node_types)/sizeof(qt_node_types[0]);


static void *qtdemux_zalloc(void *opaque, unsigned int items, unsigned int size)
{
  return g_malloc(items*size);
}

static void qtdemux_zfree(void *opaque, void *addr)
{
  g_free(addr);
}

static void *qtdemux_inflate(void *z_buffer, int z_length, int length)
{
  void *buffer;
  z_stream *z;
  int ret;

  z = g_new0(z_stream, 1);
  z->zalloc = qtdemux_zalloc;
  z->zfree = qtdemux_zfree;
  z->opaque = NULL;

  z->next_in = z_buffer;
  z->avail_in = z_length;

  buffer = g_malloc(length);
  ret = inflateInit(z);
  while(z->avail_in > 0){
    if(z->avail_out == 0){
      length += 1024;
      buffer = realloc(buffer, length);
      z->next_out = buffer + z->total_out;
      z->avail_out = 1024;
    }
    ret = inflate(z,Z_SYNC_FLUSH);
    if(ret != Z_OK)break;
  }
  if(ret != Z_STREAM_END){
    g_warning("inflate() returned %d\n",ret);
  }

  g_free(z);
  return buffer;
}

static void qtdemux_parse_moov(GstQTDemux *qtdemux, void *buffer, int length)
{
  GNode *cmov;

  qtdemux->moov_node = g_node_new(buffer);

  qtdemux_parse(qtdemux, qtdemux->moov_node, buffer, length);

  cmov = qtdemux_tree_get_child_by_type(qtdemux->moov_node, FOURCC_cmov);
  if(cmov){
    GNode *dcom;
    GNode *cmvd;

    dcom = qtdemux_tree_get_child_by_type(cmov, FOURCC_dcom);
    cmvd = qtdemux_tree_get_child_by_type(cmov, FOURCC_cmvd);

    if(QTDEMUX_FOURCC_GET(dcom->data+8) == GST_MAKE_FOURCC('z','l','i','b')){
      int uncompressed_length;
      int compressed_length;
      void *buf;
      
      uncompressed_length = QTDEMUX_GUINT32_GET(cmvd->data+8);
      compressed_length = QTDEMUX_GUINT32_GET(cmvd->data+4) - 12;
      g_print("length = %d\n",uncompressed_length);

      buf = qtdemux_inflate(cmvd->data + 12, compressed_length,
	  uncompressed_length);

      qtdemux->moov_node_compressed = qtdemux->moov_node;
      qtdemux->moov_node = g_node_new(buf);

      qtdemux_parse(qtdemux, qtdemux->moov_node, buf, uncompressed_length);
    }else{
      g_print("unknown header compression type\n");
    }
  }
}

static void qtdemux_parse(GstQTDemux *qtdemux, GNode *node, void *buffer, int length)
{
  guint32 fourcc;
  guint32 node_length;
  QtNodeType *type;
  void *end;

  //g_print("qtdemux_parse %p %d\n",buffer, length);

  node_length = QTDEMUX_GUINT32_GET(buffer);
  fourcc = QTDEMUX_FOURCC_GET(buffer+4);

  type = qtdemux_type_get(fourcc);
  
  /*g_print("parsing '" GST_FOURCC_FORMAT "', length=%d\n",
      GST_FOURCC_ARGS(fourcc), node_length);*/

  if(type->flags & QT_CONTAINER){
    void *buf;
    guint32 len;

    buf = buffer + 8;
    end = buffer + length;
    while(buf < end){
      GNode *child;

      if(buf + 8 >= end){
	/* FIXME: get annoyed */
	g_print("buffer overrun\n");
      }
      len = QTDEMUX_GUINT32_GET(buf);

      child = g_node_new(buf);
      g_node_append(node, child);
      qtdemux_parse(qtdemux, child, buf, len);

      buf += len;
    }
  }else{
#if 0
    if(fourcc == FOURCC_cmvd){
      int uncompressed_length;
      void *buf;
      
      uncompressed_length = QTDEMUX_GUINT32_GET(buffer+8);
      g_print("length = %d\n",uncompressed_length);

      buf = qtdemux_inflate(buffer + 12, node_length-12, uncompressed_length);

      end = buf + uncompressed_length;
      while(buf < end){
        GNode *child;
	guint32 len;

        if(buf + 8 >= end){
	  /* FIXME: get annoyed */
	  g_print("buffer overrun\n");
        }
        len = QTDEMUX_GUINT32_GET(buf);

        child = g_node_new(buf);
        g_node_append(node, child);
        qtdemux_parse(qtdemux, child, buf, len);

        buf += len;
      }
    }
#endif
  }
}

static QtNodeType *qtdemux_type_get(guint32 fourcc)
{
  int i;

  for(i=0;i<n_qt_node_types;i++){
    if(qt_node_types[i].fourcc == fourcc)
      return qt_node_types+i;
  }
  return qt_node_types+n_qt_node_types-1;
}

static gboolean qtdemux_node_dump_foreach(GNode *node, gpointer data)
{
  void *buffer = node->data;
  guint32 node_length;
  guint32 fourcc;
  QtNodeType *type;
  int depth;

  node_length = GUINT32_FROM_BE(*(guint32 *)buffer);
  fourcc = GUINT32_FROM_LE(*(guint32 *)(buffer+4));

  type = qtdemux_type_get(fourcc);

  depth = (g_node_depth(node)-1)*2;
  g_print("%*s'" GST_FOURCC_FORMAT "', [%d], %s\n",
      depth, "",
      GST_FOURCC_ARGS(fourcc),
      node_length,
      type->name);

  if(type->dump)type->dump(data, buffer, depth);

  return FALSE;
}

static void qtdemux_node_dump(GstQTDemux *qtdemux, GNode *node)
{
  g_node_traverse(qtdemux->moov_node, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
      qtdemux_node_dump_foreach, qtdemux);
}

static void qtdemux_dump_mvhd(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  creation time: %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  g_print("%*s  modify time:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+16));
  g_print("%*s  time scale:    1/%u sec\n", depth, "", QTDEMUX_GUINT32_GET(buffer+20));
  g_print("%*s  duration:      %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+24));
  g_print("%*s  pref. rate:    %g\n", depth, "", QTDEMUX_FP32_GET(buffer+28));
  g_print("%*s  pref. volume:  %g\n", depth, "", QTDEMUX_FP16_GET(buffer+32));
  g_print("%*s  preview time:  %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+80));
  g_print("%*s  preview dur.:  %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+84));
  g_print("%*s  poster time:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+88));
  g_print("%*s  select time:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+92));
  g_print("%*s  select dur.:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+96));
  g_print("%*s  current time:  %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+100));
  g_print("%*s  next track ID: %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+104));
}

static void qtdemux_dump_tkhd(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  creation time: %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  g_print("%*s  modify time:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+16));
  g_print("%*s  track ID:      %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+20));
  g_print("%*s  duration:      %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+28));
  g_print("%*s  layer:         %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+36));
  g_print("%*s  alt group:     %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+38));
  g_print("%*s  volume:        %g\n", depth, "", QTDEMUX_FP16_GET(buffer+44));
  g_print("%*s  track width:   %g\n", depth, "", QTDEMUX_FP32_GET(buffer+84));
  g_print("%*s  track height:  %g\n", depth, "", QTDEMUX_FP32_GET(buffer+88));

}

static void qtdemux_dump_elst(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  for(i=0;i<n;i++){
    g_print("%*s    track dur:     %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+16+i*12));
    g_print("%*s    media time:    %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+20+i*12));
    g_print("%*s    media rate:    %g\n", depth, "", QTDEMUX_FP32_GET(buffer+24+i*12));
  }
}

static void qtdemux_dump_mdhd(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  creation time: %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  g_print("%*s  modify time:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+16));
  g_print("%*s  time scale:    1/%u sec\n", depth, "", QTDEMUX_GUINT32_GET(buffer+20));
  g_print("%*s  duration:      %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+24));
  g_print("%*s  language:      %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+28));
  g_print("%*s  quality:       %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+30));

}

static void qtdemux_dump_hdlr(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  type:          " GST_FOURCC_FORMAT "\n", depth, "",
      GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+12)));
  g_print("%*s  subtype:       " GST_FOURCC_FORMAT "\n", depth, "",
      GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+16)));
  g_print("%*s  manufacturer:  " GST_FOURCC_FORMAT "\n", depth, "",
      GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+20)));
  g_print("%*s  flags:         %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+24));
  g_print("%*s  flags mask:    %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+28));
  g_print("%*s  name:          %*s\n", depth, "",
      QTDEMUX_GUINT8_GET(buffer+32), (char *)(buffer+33));

}

static void qtdemux_dump_vmhd(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  mode/color:    %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+16));
}

static void qtdemux_dump_dref(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int n;
  int i;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    size:          %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));
    g_print("%*s    type:          " GST_FOURCC_FORMAT "\n", depth, "",
	GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+offset+4)));
    offset += QTDEMUX_GUINT32_GET(buffer+offset);
  }
}

static void qtdemux_dump_stsd(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    size:          %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));
    g_print("%*s    type:          " GST_FOURCC_FORMAT "\n", depth, "",
	GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+offset+4)));
    g_print("%*s    data reference:%d\n", depth, "", QTDEMUX_GUINT16_GET(buffer+offset+14));

    g_print("%*s    version/rev.:  %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset+16));
    g_print("%*s    vendor:        " GST_FOURCC_FORMAT "\n", depth, "",
	GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+offset+20)));
    g_print("%*s    temporal qual: %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset+24));
    g_print("%*s    spatial qual:  %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset+28));
    g_print("%*s    width:         %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+offset+32));
    g_print("%*s    height:        %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+offset+34));
    g_print("%*s    horiz. resol:  %g\n", depth, "", QTDEMUX_FP32_GET(buffer+offset+36));
    g_print("%*s    vert. resol.:  %g\n", depth, "", QTDEMUX_FP32_GET(buffer+offset+40));
    g_print("%*s    data size:     %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset+44));
    g_print("%*s    frame count:   %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+offset+48));
    g_print("%*s    compressor:    %*s\n", depth, "",
	QTDEMUX_GUINT8_GET(buffer+offset+49), (char *)(buffer+offset+51));
    g_print("%*s    depth:         %u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+offset+82));
    g_print("%*s    color table ID:%u\n", depth, "", QTDEMUX_GUINT16_GET(buffer+offset+84));

    offset += QTDEMUX_GUINT32_GET(buffer+offset);
  }
}

static void qtdemux_dump_stts(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    count:         %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));
    g_print("%*s    duration:      %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset + 4));

    offset += 8;
  }
}

static void qtdemux_dump_stss(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    sample:        %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));

    offset += 4;
  }
}

static void qtdemux_dump_stsc(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    first chunk:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));
    g_print("%*s    sample per ch: %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset+4));
    g_print("%*s    sample desc id:%08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset+8));

    offset += 12;
  }
}

static void qtdemux_dump_stsz(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;
  int sample_size;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  sample size:   %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  sample_size = QTDEMUX_GUINT32_GET(buffer+12);
  if(sample_size == 0){
    g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+16));
    n = QTDEMUX_GUINT32_GET(buffer+16);
    offset = 20;
    for(i=0;i<n;i++){
      g_print("%*s    sample size:   %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));

      offset += 4;
    }
  }
}

static void qtdemux_dump_stco(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    chunk offset:  %u\n", depth, "", QTDEMUX_GUINT32_GET(buffer+offset));

    offset += 4;
  }
}

static void qtdemux_dump_co64(GstQTDemux *qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  g_print("%*s  version/flags: %08x\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
  g_print("%*s  n entries:     %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+12));
  n = QTDEMUX_GUINT32_GET(buffer+12);
  offset = 16;
  for(i=0;i<n;i++){
    g_print("%*s    chunk offset:  %" G_GUINT64_FORMAT "\n", depth, "", QTDEMUX_GUINT64_GET(buffer+offset));

    offset += 8;
  }
}

static void qtdemux_dump_dcom(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  compression type: " GST_FOURCC_FORMAT "\n", depth, "",
      GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(buffer+8)));
}

static void qtdemux_dump_cmvd(GstQTDemux *qtdemux, void *buffer, int depth)
{
  g_print("%*s  length: %d\n", depth, "", QTDEMUX_GUINT32_GET(buffer+8));
}


static GNode *qtdemux_tree_get_child_by_type(GNode *node, guint32 fourcc)
{
  GNode *child;
  void *buffer;
  guint32 child_fourcc;

  for(child = g_node_first_child(node); child; child = g_node_next_sibling(child)){
    buffer = child->data;

    child_fourcc = GUINT32_FROM_LE(*(guint32 *)(buffer+4));

    if(child_fourcc == fourcc){
      return child;
    }
  }
  return NULL;
}

static GNode *qtdemux_tree_get_sibling_by_type(GNode *node, guint32 fourcc)
{
  GNode *child;
  void *buffer;
  guint32 child_fourcc;

  for(child = g_node_next_sibling(node); child; child = g_node_next_sibling(child)){
    buffer = child->data;

    child_fourcc = GUINT32_FROM_LE(*(guint32 *)(buffer+4));

    if(child_fourcc == fourcc){
      return child;
    }
  }
  return NULL;
}

static void qtdemux_parse_trak(GstQTDemux *qtdemux, GNode *trak);

static void qtdemux_parse_tree(GstQTDemux *qtdemux)
{
  GNode *mvhd;
  GNode *trak;

  mvhd = qtdemux_tree_get_child_by_type(qtdemux->moov_node, FOURCC_mvhd);
  if(mvhd==NULL){
    g_print("No mvhd node found.\n");
    return;
  }

  qtdemux->timescale = QTDEMUX_GUINT32_GET(mvhd->data + 20);
  qtdemux->duration = QTDEMUX_GUINT32_GET(mvhd->data + 24);

  g_print("timescale: %d\n", qtdemux->timescale);
  g_print("duration: %d\n", qtdemux->duration);

  trak = qtdemux_tree_get_child_by_type(qtdemux->moov_node, FOURCC_trak);
  qtdemux_parse_trak(qtdemux, trak);

/*  trak = qtdemux_tree_get_sibling_by_type(trak, FOURCC_trak);
  if(trak)qtdemux_parse_trak(qtdemux, trak);*/

  while ((trak = qtdemux_tree_get_sibling_by_type(trak, FOURCC_trak)) != NULL)
    qtdemux_parse_trak(qtdemux, trak);
}

static void qtdemux_parse_trak(GstQTDemux *qtdemux, GNode *trak)
{
  int offset;
  GNode *tkhd;
  GNode *mdia;
  GNode *mdhd;
  GNode *hdlr;
  GNode *minf;
  GNode *stbl;
  GNode *stsd;
  GNode *stsc;
  GNode *stsz;
  GNode *stco;
  GNode *co64;
  GNode *stts;
  int n_samples;
  QtDemuxSample *samples;
  int n_samples_per_chunk;
  int index;
  int i,j,k;
  QtDemuxStream *stream;
  int n_sample_times;
  guint64 timestamp;
  int sample_size;
  int sample_index;

  stream = g_new0(QtDemuxStream,1);

  tkhd = qtdemux_tree_get_child_by_type(trak, FOURCC_tkhd);
  g_assert(tkhd);

  /* track duration? */

  mdia = qtdemux_tree_get_child_by_type(trak, FOURCC_mdia);
  g_assert(mdia);

  mdhd = qtdemux_tree_get_child_by_type(mdia, FOURCC_mdhd);
  g_assert(mdhd);

  stream->timescale = QTDEMUX_GUINT32_GET(mdhd->data+20);
  
  hdlr = qtdemux_tree_get_child_by_type(mdia, FOURCC_hdlr);
  g_assert(hdlr);
  
  g_print("track type: " GST_FOURCC_FORMAT "\n",
      GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(hdlr->data+12)));
  g_print("track subtype: " GST_FOURCC_FORMAT "\n",
      GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(hdlr->data+16)));

  stream->subtype = QTDEMUX_FOURCC_GET(hdlr->data+16);

  minf = qtdemux_tree_get_child_by_type(mdia, FOURCC_minf);
  g_assert(minf);

  stbl = qtdemux_tree_get_child_by_type(minf, FOURCC_stbl);
  g_assert(stbl);

  stsd = qtdemux_tree_get_child_by_type(stbl, FOURCC_stsd);
  g_assert(stsd);

  if(stream->subtype == FOURCC_vide){
    offset = 16;
    g_print("st type:          " GST_FOURCC_FORMAT "\n",
	  GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(stsd->data+offset+4)));

    stream->width = QTDEMUX_GUINT16_GET(stsd->data+offset+32);
    stream->height = QTDEMUX_GUINT16_GET(stsd->data+offset+34);
    stream->fps = 0.; /* this is filled in later */

    g_print("frame count:   %u\n", QTDEMUX_GUINT16_GET(stsd->data+offset+48));
    
    stream->caps = qtdemux_video_caps(qtdemux,
        QTDEMUX_FOURCC_GET(stsd->data+offset+4));
    g_print("caps %s\n",gst_caps_to_string(stream->caps));
  }else if(stream->subtype == FOURCC_soun){
    int version;

    g_print("st type:          " GST_FOURCC_FORMAT "\n",
	  GST_FOURCC_ARGS(QTDEMUX_FOURCC_GET(stsd->data+16+4)));

    offset = 32;
    g_print("version/rev:      %08x\n", QTDEMUX_GUINT32_GET(stsd->data+offset));
    version = QTDEMUX_GUINT32_GET(stsd->data+offset);
    g_print("vendor:           %08x\n", QTDEMUX_GUINT32_GET(stsd->data+offset + 4));
    g_print("n_channels:       %d\n", QTDEMUX_GUINT16_GET(stsd->data+offset + 8));
    stream->n_channels = QTDEMUX_GUINT16_GET(stsd->data+offset + 8);
    g_print("sample_size:      %d\n", QTDEMUX_GUINT16_GET(stsd->data+offset + 10));
    g_print("compression_id:   %d\n", QTDEMUX_GUINT16_GET(stsd->data+offset + 12));
    g_print("packet size:      %d\n", QTDEMUX_GUINT16_GET(stsd->data+offset + 14));
    g_print("sample rate:      %g\n", QTDEMUX_FP32_GET(stsd->data+offset + 16));
    stream->rate = QTDEMUX_FP32_GET(stsd->data+offset + 16);

    if(version == 0x00010000){
      g_print("samples/packet:   %d\n", QTDEMUX_GUINT32_GET(stsd->data+offset + 20));
      g_print("bytes/packet:     %d\n", QTDEMUX_GUINT32_GET(stsd->data+offset + 24));
      g_print("bytes/frame:      %d\n", QTDEMUX_GUINT32_GET(stsd->data+offset + 28));
      g_print("bytes/sample:     %d\n", QTDEMUX_GUINT32_GET(stsd->data+offset + 32));
    }

    stream->caps = qtdemux_audio_caps(qtdemux,
        QTDEMUX_FOURCC_GET(stsd->data+16+4));
    g_print("caps %s\n",gst_caps_to_string(stream->caps));
  }else{
    g_print("unknown subtype\n");
    return;
  }

  if(stream->caps){
    gst_caps_ref(stream->caps);
    gst_caps_sink(stream->caps);
  }

  /* sample to chunk */
  stsc = qtdemux_tree_get_child_by_type(stbl, FOURCC_stsc);
  g_assert(stsc);
  /* sample size */
  stsz = qtdemux_tree_get_child_by_type(stbl, FOURCC_stsz);
  g_assert(stsz);
  /* chunk offsets */
  stco = qtdemux_tree_get_child_by_type(stbl, FOURCC_stco);
  co64 = qtdemux_tree_get_child_by_type(stbl, FOURCC_co64);
  g_assert(stco || co64);
  /* sample time */
  stts = qtdemux_tree_get_child_by_type(stbl, FOURCC_stts);
  g_assert(stts);

  sample_size = QTDEMUX_GUINT32_GET(stsz->data+12);
  if(sample_size == 0){
    n_samples = QTDEMUX_GUINT32_GET(stsz->data+16);
    stream->n_samples = n_samples;
    samples = g_malloc(sizeof(QtDemuxSample)*n_samples);
    stream->samples = samples;

    for(i=0;i<n_samples;i++){
      samples[i].size = QTDEMUX_GUINT32_GET(stsz->data + i*4 + 20);
    }
    n_samples_per_chunk = QTDEMUX_GUINT32_GET(stsc->data+12);
    index = 0;
    offset = 16;
    for(i=0;i<n_samples_per_chunk;i++){
      int first_chunk, last_chunk;
      int samples_per_chunk;
  
      first_chunk = QTDEMUX_GUINT32_GET(stsc->data + 16 + i*12 + 0) - 1;
      if(i==n_samples_per_chunk-1){
        last_chunk = INT_MAX;
      }else{
        last_chunk = QTDEMUX_GUINT32_GET(stsc->data +16 + i*12 + 12) - 1;
      }
      samples_per_chunk = QTDEMUX_GUINT32_GET(stsc->data + 16 + i*12 + 4);

      for(j=first_chunk;j<last_chunk;j++){
        int chunk_offset;
        if(stco){
          chunk_offset = QTDEMUX_GUINT32_GET(stco->data + 16 + j*4);
        }else{
          chunk_offset = QTDEMUX_GUINT64_GET(co64->data + 16 + j*8);
        }
        for(k=0;k<samples_per_chunk;k++){
	  samples[index].chunk = j;
	  samples[index].offset = chunk_offset;
	  chunk_offset += samples[index].size;
	  index++;
	  if(index>=n_samples)goto done;
        }
      }
    }
done:
    
    n_sample_times = QTDEMUX_GUINT32_GET(stts->data + 12);
    timestamp = 0;
    index = 0;
    for(i=0;i<n_sample_times;i++){
      int n;
      int duration;
      guint64 time;
  
      n = QTDEMUX_GUINT32_GET(stts->data + 16 + 8*i);
      duration = QTDEMUX_GUINT32_GET(stts->data + 16 + 8*i + 4);
      time = (GST_SECOND * duration)/stream->timescale;
      for(j=0;j<n;j++){
        samples[index].timestamp = timestamp;
        samples[index].duration = time;
        timestamp += time;
        index++;
      }
    }
  }else{
    int sample_width;

    g_print("treating chunks as samples\n");

    /* treat chunks as samples */
    if(stco){
      n_samples = QTDEMUX_GUINT32_GET(stco->data+12);
    }else{
      n_samples = QTDEMUX_GUINT32_GET(co64->data+12);
    }
    stream->n_samples = n_samples;
    samples = g_malloc(sizeof(QtDemuxSample)*n_samples);
    stream->samples = samples;

    sample_width = QTDEMUX_GUINT16_GET(stsd->data+offset + 10) / 8;

    n_samples_per_chunk = QTDEMUX_GUINT32_GET(stsc->data+12);
    offset = 16;
    sample_index = 0;
    for(i=0;i<n_samples_per_chunk;i++){
      int first_chunk, last_chunk;
      int samples_per_chunk;
  
      first_chunk = QTDEMUX_GUINT32_GET(stsc->data + 16 + i*12 + 0) - 1;
      if(i==n_samples-1){
        last_chunk = INT_MAX;
      }else{
        last_chunk = QTDEMUX_GUINT32_GET(stsc->data +16 + i*12 + 12) - 1;
      }
      samples_per_chunk = QTDEMUX_GUINT32_GET(stsc->data + 16 + i*12 + 4);

      for(j=first_chunk;j<last_chunk;j++){
        int chunk_offset;
        if(stco){
          chunk_offset = QTDEMUX_GUINT32_GET(stco->data + 16 + j*4);
        }else{
          chunk_offset = QTDEMUX_GUINT64_GET(co64->data + 16 + j*8);
        }
	samples[j].chunk = j;
	samples[j].offset = chunk_offset;
	samples[j].size = samples_per_chunk * stream->n_channels * sample_width;
	samples[j].sample_index = sample_index;
	sample_index += samples_per_chunk;
	if(j>=n_samples)goto done2;
      }
    }
done2:

    n_sample_times = QTDEMUX_GUINT32_GET(stts->data + 12);
    g_print("n_sample_times = %d\n",n_sample_times);
    timestamp = 0;
    index = 0;
    sample_index = 0;
    for(i=0;i<n_sample_times;i++){
      int duration;
      guint64 time;
  
      sample_index += QTDEMUX_GUINT32_GET(stts->data + 16 + 8*i);
      duration = QTDEMUX_GUINT32_GET(stts->data + 16 + 8*i + 4);
      for(;index < n_samples && samples[index].sample_index < sample_index;index++){
	int size;

        samples[index].timestamp = timestamp;
	size = samples[index+1].sample_index - samples[index].sample_index;
	time = (GST_SECOND * duration * samples[index].size)/stream->timescale ;
        timestamp += time;
        samples[index].duration = time;
      }
    }
  }

#if 0
  for(i=0;i<n_samples;i++){
    g_print("%d: %d %d %d %d %" G_GUINT64_FORMAT "\n",i,
	samples[i].sample_index,samples[i].chunk,
	samples[i].offset, samples[i].size, samples[i].timestamp);
    if(i>10)break;
  }
#endif

  gst_qtdemux_add_stream(qtdemux,stream);
}


static GstCaps *qtdemux_video_caps(GstQTDemux *qtdemux, guint32 fourcc)
{
  switch(fourcc){
    case GST_MAKE_FOURCC('j','p','e','g'):
      /* JPEG */
      return GST_CAPS_NEW("jpeg_caps","image/jpeg",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('m','j','p','a'):
      /* Motion-JPEG (format A) */
      return GST_CAPS_NEW("mjpa_caps","image/jpeg",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('m','j','p','b'):
      /* Motion-JPEG (format B) */
      return GST_CAPS_NEW("mjpb_caps","image/jpeg",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('S','V','Q','3'):
      return GST_CAPS_NEW("SVQ3_caps","video/x-svq",
	  "svqversion", GST_PROPS_INT(3),
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('s','v','q','i'):
    case GST_MAKE_FOURCC('S','V','Q','1'):
      return GST_CAPS_NEW("SVQ1_caps","video/x-svq",
	  "svqversion", GST_PROPS_INT(1),
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('r','a','w',' '):
      /* uncompressed RGB */
      return GST_CAPS_NEW("raw__caps","video/x-raw-rgb",
	  "endianness",GST_PROPS_INT(G_BIG_ENDIAN),
          /*"bpp", GST_PROPS_INT(x),
          "depth", GST_PROPS_INT(x),
          "red_mask", GST_PROPS_INT(x),
          "green_mask", GST_PROPS_INT(x),
          "blue_mask", GST_PROPS_INT(x), FIXME! */
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('Y','u','v','2'):
      /* uncompressed YUV2 */
      return GST_CAPS_NEW("Yuv2_caps","video/x-raw-yuv",
	  "format",GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','U','V','2')),
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('m','p','e','g'):
      /* MPEG */
      return GST_CAPS_NEW("mpeg_caps","video/mpeg",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096),
          "systemstream", GST_PROPS_BOOLEAN(FALSE),
          "mpegversion", GST_PROPS_INT(1));
    case GST_MAKE_FOURCC('g','i','f',' '):
      return GST_CAPS_NEW("gif__caps","image/gif",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('h','2','6','3'):
      /* H.263 */
      /* ffmpeg uses the height/width props, don't know why */
      return GST_CAPS_NEW("h263_caps","video/x-h263",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('m','p','4','v'):
      /* MPEG-4 */
      return GST_CAPS_NEW("mp4v_caps", "video/mpeg",
	  "mpegversion",GST_PROPS_INT(4),
          "systemstream", GST_PROPS_BOOLEAN(FALSE),
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('3','I','V','1'):
      return GST_CAPS_NEW("3IV1_caps", "video/x-3ivx",
          "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
          "width", GST_PROPS_INT_RANGE (16, 4096),
          "height", GST_PROPS_INT_RANGE (16, 4096));
    case GST_MAKE_FOURCC('r','p','z','a'):
    case GST_MAKE_FOURCC('c','v','i','d'):
      /* Cinepak */
    case GST_MAKE_FOURCC('r','l','e',' '):
      /* Run-length encoding */
    case GST_MAKE_FOURCC('s','m','c',' '):
    case GST_MAKE_FOURCC('k','p','c','d'):
    default:
      g_print("Don't know how to convert fourcc '" GST_FOURCC_FORMAT
	  "' to caps\n", GST_FOURCC_ARGS(fourcc));
      return NULL;
  }
}

static GstCaps *qtdemux_audio_caps(GstQTDemux *qtdemux, guint32 fourcc)
{
  switch(fourcc){
    case GST_MAKE_FOURCC('N','O','N','E'):
      return NULL; /*GST_CAPS_NEW("NONE_caps","audio/raw",NULL);*/
    case GST_MAKE_FOURCC('r','a','w',' '):
      /* FIXME */
      return GST_CAPS_NEW("raw__caps","audio/x-raw-int",
	  "width",GST_PROPS_INT(8),
	  "depth",GST_PROPS_INT(8),
	  "signed",GST_PROPS_BOOLEAN(FALSE),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('t','w','o','s'):
      /* FIXME */
      return GST_CAPS_NEW("twos_caps","audio/x-raw-int",
	  "width",GST_PROPS_INT(16),
	  "depth",GST_PROPS_INT(16),
	  "endianness",GST_PROPS_INT(G_BIG_ENDIAN),
	  "signed",GST_PROPS_BOOLEAN(TRUE),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('s','o','w','t'):
      /* FIXME */
      return GST_CAPS_NEW("sowt_caps","audio/x-raw-int",
	  "width",GST_PROPS_INT(16),
	  "depth",GST_PROPS_INT(16),
	  "endianness",GST_PROPS_INT(G_LITTLE_ENDIAN),
	  "signed",GST_PROPS_BOOLEAN(TRUE),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('f','l','6','4'):
      return GST_CAPS_NEW("fl64_caps","audio/x-raw-float",
          "width",GST_PROPS_INT (64),
          "endianness",GST_PROPS_INT (G_BIG_ENDIAN),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('f','l','3','2'):
      return GST_CAPS_NEW("fl32_caps","audio/x-raw-float",
          "width",GST_PROPS_INT (32),
          "endianness",GST_PROPS_INT (G_BIG_ENDIAN),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('i','n','2','4'):
      /* FIXME */
      return GST_CAPS_NEW("in24_caps","audio/x-raw-int",
	  "width",GST_PROPS_INT(24),
	  "depth",GST_PROPS_INT(32),
	  "endianness",GST_PROPS_INT(G_BIG_ENDIAN),
	  "signed",GST_PROPS_BOOLEAN(TRUE),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('i','n','3','2'):
      /* FIXME */
      return GST_CAPS_NEW("in32_caps","audio/x-raw-int",
	  "width",GST_PROPS_INT(24),
	  "depth",GST_PROPS_INT(32),
	  "endianness",GST_PROPS_INT(G_BIG_ENDIAN),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('u','l','a','w'):
      /* FIXME */
      return GST_CAPS_NEW("ulaw_caps","audio/x-mulaw",
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case GST_MAKE_FOURCC('a','l','a','w'):
      /* FIXME */
      return GST_CAPS_NEW("alaw_caps","audio/x-alaw",
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT));
    case 0x6d730002:
      /* Microsoft ADPCM-ACM code 2 */
      return GST_CAPS_NEW("msxx_caps","audio/x-adpcm",
	  "layout", GST_PROPS_STRING("microsoft"),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case 0x6d730011:
      /* FIXME DVI/Intel IMA ADPCM/ACM code 17 */
      return GST_CAPS_NEW("msxx_caps","audio/x-adpcm",
	  "layout", GST_PROPS_STRING("quicktime"),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case 0x6d730055:
      /* MPEG layer 3, CBR only (pre QT4.1) */
    case 0x5500736d:
    case GST_MAKE_FOURCC('.','m','p','3'):
      /* MPEG layer 3, CBR & VBR (QT4.1 and later) */
      return GST_CAPS_NEW("_mp3_caps","audio/mpeg",
	  "layer", GST_PROPS_INT(3),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case GST_MAKE_FOURCC('M','A','C','3'):
      /* MACE 3:1 */
      return GST_CAPS_NEW("MAC3_caps","audio/x-mace",
	  "maceversion",GST_PROPS_INT(3),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case GST_MAKE_FOURCC('M','A','C','6'):
      /* MACE 6:1 */
      return GST_CAPS_NEW("MAC3_caps","audio/x-mace",
	  "maceversion",GST_PROPS_INT(6),
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case GST_MAKE_FOURCC('O','g','g','V'):
      /* Ogg Vorbis */
      return GST_CAPS_NEW("OggV_caps","application/ogg",
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case GST_MAKE_FOURCC('d','v','c','a'):
      /* DV audio */
      return GST_CAPS_NEW("dvca_caps","audio/x-dv",
	  "rate",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  "channels",GST_PROPS_INT_RANGE(1,G_MAXINT),
	  NULL);
    case GST_MAKE_FOURCC('m','p','4','a'):
      /* MPEG-4 AAC */
      return GST_CAPS_NEW("mp4a_caps", "audio/mpeg",
	   "mpegversion", GST_PROPS_INT(4),
	   "rate", GST_PROPS_INT_RANGE(1, G_MAXINT),
	   "channels", GST_PROPS_INT_RANGE(1, G_MAXINT),
	   "systemstream", GST_PROPS_BOOLEAN(FALSE), NULL);
    case GST_MAKE_FOURCC('q','t','v','r'):
      /* ? */
    case GST_MAKE_FOURCC('Q','D','M','2'):
      /* QDesign music version 2 (no constant) */
    case GST_MAKE_FOURCC('Q','D','M','C'):
      /* QDesign music */
    case GST_MAKE_FOURCC('i','m','a','4'):
      /* IMA 4:1 */
    case GST_MAKE_FOURCC('Q','c','l','p'):
      /* QUALCOMM PureVoice */
    case GST_MAKE_FOURCC('a','g','s','m'):
      /* ? */
    default:
      g_print("Don't know how to convert fourcc '" GST_FOURCC_FORMAT
	  "' to caps\n", GST_FOURCC_ARGS(fourcc));
      return NULL;
  }
}

