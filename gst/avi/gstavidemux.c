/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
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


/* #define GST_DEBUG_ENABLED */
#include <string.h>

#include "gstavidemux.h"


/* elementfactory information */
static GstElementDetails gst_avi_demux_details = {
  "Avi demuxer",
  "Codec/Demuxer",
  "Demultiplex an avi file into audio and video",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 1999",
};

static GstCaps* avi_type_find (GstBuffer *buf, gpointer private);

/* typefactory for 'avi' */
static GstTypeDefinition avidefinition = {
  "avidemux_video/avi",
  "video/avi",
  ".avi",
  avi_type_find,
};

/* AviDemux signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BITRATE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "avidemux_sink",
     "video/avi",
      "format",    GST_PROPS_STRING ("AVI")
  )
)

GST_PAD_TEMPLATE_FACTORY (src_video_templ,
  "video_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "avidemux_src_video",
    "video/avi",
      "format",  GST_PROPS_LIST (
	           GST_PROPS_STRING ("strf_vids"),
	           GST_PROPS_STRING ("strf_iavs")
      		 ),
      "width",   GST_PROPS_INT_RANGE (16, 4096),
      "height",  GST_PROPS_INT_RANGE (16, 4096)

  ),
  GST_CAPS_NEW (
    "avidemux_src_video",
    "video/raw",
      "format",  GST_PROPS_LIST (
                   GST_PROPS_FOURCC (GST_MAKE_FOURCC('Y','U','Y','2')),
                   GST_PROPS_FOURCC (GST_MAKE_FOURCC('I','4','2','0'))
                 ),
      "width",          GST_PROPS_INT_RANGE (16, 4096),
      "height",         GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "avidemux_src_video",
    "video/jpeg",
      "width",   GST_PROPS_INT_RANGE (16, 4096),
      "height",  GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "avidemux_src_video",
    "video/dv",
      "format",  GST_PROPS_LIST (
                   GST_PROPS_STRING ("NTSC"),
                   GST_PROPS_STRING ("PAL")
                 ),
      "width",   GST_PROPS_INT_RANGE (16, 4096),
      "height",  GST_PROPS_INT_RANGE (16, 4096)
  )
)

GST_PAD_TEMPLATE_FACTORY (src_audio_templ,
  "audio_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "avidemux_src_audio",
    "video/avi",
      "format",  GST_PROPS_STRING ("strf_auds")
  ),
  GST_CAPS_NEW (
    "avidemux_src_audio",
    "audio/raw",
      "format",           GST_PROPS_STRING ("int"),
      "law",              GST_PROPS_INT (0),
      "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
      "signed",           GST_PROPS_LIST (
      			    GST_PROPS_BOOLEAN (TRUE),
      			    GST_PROPS_BOOLEAN (FALSE)
			  ),
      "width",            GST_PROPS_LIST (
      			    GST_PROPS_INT (8),
      			    GST_PROPS_INT (16)
			  ),
      "depth",            GST_PROPS_LIST (
      			    GST_PROPS_INT (8),
      			    GST_PROPS_INT (16)
			  ),
      "rate",             GST_PROPS_INT_RANGE (11025, 44100),
      "channels",         GST_PROPS_INT_RANGE (1, 2)
  ),
  GST_CAPS_NEW (
    "avidemux_src_audio",
    "audio/mp3",
      NULL
  )
)

static void 	gst_avi_demux_class_init	(GstAviDemuxClass *klass);
static void 	gst_avi_demux_init		(GstAviDemux *avi_demux);

static void 	gst_avi_demux_loop 		(GstElement *element);

static gboolean gst_avi_demux_send_event 	(GstElement *element, GstEvent *event);

static gboolean gst_avi_demux_handle_src_event 	(GstPad *pad, GstEvent *event);
static gboolean gst_avi_demux_handle_src_query 	(GstPad *pad, GstPadQueryType type, 
						 GstFormat *format, gint64 *value);

static GstElementStateReturn
		gst_avi_demux_change_state 	(GstElement *element);

static void     gst_avi_demux_get_property      (GObject *object, guint prop_id, 	
						 GValue *value, GParamSpec *pspec);


static GstElementClass *parent_class = NULL;
/*static guint gst_avi_demux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avi_demux_get_type(void) 
{
  static GType avi_demux_type = 0;

  if (!avi_demux_type) {
    static const GTypeInfo avi_demux_info = {
      sizeof(GstAviDemuxClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_avi_demux_class_init,
      NULL,
      NULL,
      sizeof(GstAviDemux),
      0,
      (GInstanceInitFunc)gst_avi_demux_init,
    };
    avi_demux_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAviDemux", &avi_demux_info, 0);
  }
  return avi_demux_type;
}

static void
gst_avi_demux_class_init (GstAviDemuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property (G_OBJECT_CLASS(klass), ARG_BITRATE,
    g_param_spec_long ("bitrate","bitrate","bitrate",
                       G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE)); /* CHECKME */

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  gobject_class->get_property = gst_avi_demux_get_property;
  
  gstelement_class->change_state = gst_avi_demux_change_state;
  gstelement_class->send_event = gst_avi_demux_send_event;
}

static void 
gst_avi_demux_init (GstAviDemux *avi_demux) 
{
  GST_FLAG_SET (avi_demux, GST_ELEMENT_EVENT_AWARE);
				
  avi_demux->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (avi_demux), avi_demux->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (avi_demux), gst_avi_demux_loop);

  avi_demux->num_streams = 0;
  avi_demux->num_v_streams = 0;
  avi_demux->num_a_streams = 0;
  avi_demux->index_entries = NULL;
  avi_demux->index_size = 0;
  avi_demux->seek_pending = 0;

}

static GstCaps*
avi_type_find (GstBuffer *buf,
              gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);
  GstCaps *new;

  GST_DEBUG (0,"avi_demux: typefind");

  if (GUINT32_FROM_LE (((guint32 *)data)[0]) != GST_RIFF_TAG_RIFF)
    return NULL;
  if (GUINT32_FROM_LE (((guint32 *)data)[2]) != GST_RIFF_RIFF_AVI)
    return NULL;

  new = GST_CAPS_NEW ("avi_type_find",
		      "video/avi", 
		        "format", GST_PROPS_STRING ("AVI"));

  return new;
}

static gboolean
gst_avi_demux_avih (GstAviDemux *avi_demux)
{
  gst_riff_avih *avih;
  guint32       got_bytes;
  GstByteStream *bs = avi_demux->bs;

  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&avih, sizeof (gst_riff_avih));
  if (got_bytes == sizeof (gst_riff_avih)) {
    avi_demux->avih.us_frame 	= GUINT32_FROM_LE (avih->us_frame);
    avi_demux->avih.max_bps 	= GUINT32_FROM_LE (avih->max_bps);
    avi_demux->avih.pad_gran 	= GUINT32_FROM_LE (avih->pad_gran);
    avi_demux->avih.flags 	= GUINT32_FROM_LE (avih->flags);
    avi_demux->avih.tot_frames 	= GUINT32_FROM_LE (avih->tot_frames);
    avi_demux->avih.init_frames = GUINT32_FROM_LE (avih->init_frames);
    avi_demux->avih.streams 	= GUINT32_FROM_LE (avih->streams);
    avi_demux->avih.bufsize 	= GUINT32_FROM_LE (avih->bufsize);
    avi_demux->avih.width 	= GUINT32_FROM_LE (avih->width);
    avi_demux->avih.height 	= GUINT32_FROM_LE (avih->height);
    avi_demux->avih.scale 	= GUINT32_FROM_LE (avih->scale);
    avi_demux->avih.rate 	= GUINT32_FROM_LE (avih->rate);
    avi_demux->avih.start 	= GUINT32_FROM_LE (avih->start);
    avi_demux->avih.length 	= GUINT32_FROM_LE (avih->length);

    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: avih tag found");
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  us_frame    %d", avi_demux->avih.us_frame);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  max_bps     %d", avi_demux->avih.max_bps);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  pad_gran    %d", avi_demux->avih.pad_gran);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  flags       0x%08x", avi_demux->avih.flags);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  tot_frames  %d", avi_demux->avih.tot_frames);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  init_frames %d", avi_demux->avih.init_frames);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  streams     %d", avi_demux->avih.streams);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  bufsize     %d", avi_demux->avih.bufsize);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  width       %d", avi_demux->avih.width);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  height      %d", avi_demux->avih.height);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  scale       %d", avi_demux->avih.scale);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  rate        %d", avi_demux->avih.rate);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  start       %d", avi_demux->avih.start);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  length      %d", avi_demux->avih.length);

    return TRUE;
  }
  return FALSE;
}

static gboolean 
gst_avi_demux_strh (GstAviDemux *avi_demux)
{
  gst_riff_strh *strh;
  GstByteStream *bs = avi_demux->bs;
  guint32       got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&strh, sizeof (gst_riff_strh));
  if (got_bytes == sizeof (gst_riff_strh)) {
    avi_stream_context *target;

    avi_demux->fcc_type = GUINT32_FROM_LE (strh->type);

    target = &avi_demux->stream[avi_demux->num_streams];

    target->num = avi_demux->num_streams;

    target->strh.type 		= avi_demux->fcc_type;
    target->strh.fcc_handler 	= GUINT32_FROM_LE (strh->fcc_handler);
    target->strh.flags		= GUINT32_FROM_LE (strh->flags);
    target->strh.priority	= GUINT32_FROM_LE (strh->priority);
    target->strh.init_frames	= GUINT32_FROM_LE (strh->init_frames);
    target->strh.scale		= GUINT32_FROM_LE (strh->scale);
    target->strh.rate		= GUINT32_FROM_LE (strh->rate);
    target->strh.start		= GUINT32_FROM_LE (strh->start);
    target->strh.length		= GUINT32_FROM_LE (strh->length);
    target->strh.bufsize	= GUINT32_FROM_LE (strh->bufsize);
    target->strh.quality	= GUINT32_FROM_LE (strh->quality);
    target->strh.samplesize	= GUINT32_FROM_LE (strh->samplesize);

    if (!target->strh.scale)
      target->strh.scale = 1; /* avoid division by zero */
    if (!target->strh.rate)
      target->strh.rate = 1; /* avoid division by zero */

    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: strh tag found");
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  type        0x%08x (%s)", 
  		  target->strh.type, gst_riff_id_to_fourcc (strh->type));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  fcc_handler 0x%08x (%s)", 
		  target->strh.fcc_handler, gst_riff_id_to_fourcc (strh->fcc_handler));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  flags       0x%08x", strh->flags);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  priority    %d", target->strh.priority);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  init_frames %d", target->strh.init_frames);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  scale       %d", target->strh.scale);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  rate        %d", target->strh.rate);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  start       %d", target->strh.start);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  length      %d", target->strh.length);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  bufsize     %d", target->strh.bufsize);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  quality     %d", target->strh.quality);
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  samplesize  %d", target->strh.samplesize);

    target->delay = 0LL;
    target->total_bytes = 0LL;
    target->total_frames = 0;

    target->skip = 0;

    return TRUE;
  }
  return FALSE;
}

static void 
gst_avi_demux_strf_vids (GstAviDemux *avi_demux)
{
  gst_riff_strf_vids *strf;
  GstPad *srcpad;
  GstByteStream *bs = avi_demux->bs;
  GstCaps *newcaps = NULL, *capslist = NULL;
  avi_stream_context *stream;
  guint32       got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&strf, sizeof (gst_riff_strf_vids));

  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: strf tag found in context vids");
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  size        %d", GUINT32_FROM_LE (strf->size));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  width       %d", GUINT32_FROM_LE (strf->width));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  height      %d", GUINT32_FROM_LE (strf->height));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  planes      %d", GUINT16_FROM_LE (strf->planes));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  bit_cnt     %d", GUINT16_FROM_LE (strf->bit_cnt));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  compression 0x%08x (%s)", 
		  GUINT32_FROM_LE (strf->compression), gst_riff_id_to_fourcc (strf->compression));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  image_size  %d", GUINT32_FROM_LE (strf->image_size));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  xpels_meter %d", GUINT32_FROM_LE (strf->xpels_meter));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  ypels_meter %d", GUINT32_FROM_LE (strf->ypels_meter));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  num_colors  %d", GUINT32_FROM_LE (strf->num_colors));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  imp_colors  %d", GUINT32_FROM_LE (strf->imp_colors));

  srcpad =  gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_video_templ), g_strdup_printf ("video_%02d", 
			  avi_demux->num_v_streams));

  capslist = gst_caps_append(NULL, GST_CAPS_NEW (
			  "avidemux_video_src",
			  "video/avi",
			    "format",    	GST_PROPS_STRING ("strf_vids"),
			      "size", 		GST_PROPS_INT (GUINT32_FROM_LE (strf->size)),
			      "width", 		GST_PROPS_INT (GUINT32_FROM_LE (strf->width)),
			      "height", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->height)),
			      "planes", 	GST_PROPS_INT (GUINT16_FROM_LE (strf->planes)),
			      "bit_cnt", 	GST_PROPS_INT (GUINT16_FROM_LE (strf->bit_cnt)),
			      "compression", 	GST_PROPS_FOURCC (GUINT32_FROM_LE (strf->compression)),
			      "image_size", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->image_size)),
			      "xpels_meter", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->xpels_meter)),
			      "ypels_meter", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->ypels_meter)),
			      "num_colors", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->num_colors)),
			      "imp_colors", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->imp_colors))
			      ));

  /* let's try some gstreamer-like mime-type caps */
  switch (GUINT32_FROM_LE(strf->compression))
  {
    case GST_MAKE_FOURCC('I','4','2','0'):
    case GST_MAKE_FOURCC('Y','U','Y','2'):
      newcaps = GST_CAPS_NEW (
                  "avidemux_video_src",
                  "video/raw",
                    "format",  GST_PROPS_FOURCC(GUINT32_FROM_LE(strf->compression)),
                    "width",   GST_PROPS_INT(strf->width),
                    "height",  GST_PROPS_INT(strf->height)
                );
      break;
    case GST_MAKE_FOURCC('M','J','P','G'):
      newcaps = GST_CAPS_NEW (
                  "avidemux_video_src",
                  "video/jpeg",
                    "width",   GST_PROPS_INT(strf->width),
                    "height",  GST_PROPS_INT(strf->height)
                );
      break;
    case GST_MAKE_FOURCC('d','v','s','d'):
      newcaps = GST_CAPS_NEW (
                  "avidemux_video_src",
                  "video/dv",
                    "format",  GST_PROPS_STRING("NTSC"), /* FIXME??? */
                    "width",   GST_PROPS_INT(strf->width),
                    "height",  GST_PROPS_INT(strf->height)
                );
      break;
  }

  if (newcaps) capslist = gst_caps_append (capslist, newcaps);

  gst_pad_try_set_caps (srcpad, capslist);
  gst_pad_set_event_function (srcpad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_function (srcpad, gst_avi_demux_handle_src_query);

  stream = &avi_demux->stream[avi_demux->num_streams];
  stream->pad = srcpad;
  gst_pad_set_element_private (srcpad, stream);
  avi_demux->num_streams++;
  avi_demux->num_v_streams++;

  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static void 
gst_avi_demux_strf_auds (GstAviDemux *avi_demux)
{
  gst_riff_strf_auds *strf;
  GstPad *srcpad;
  GstByteStream *bs = avi_demux->bs;
  GstCaps *newcaps = NULL, *capslist = NULL;
  avi_stream_context *stream;
  guint32       got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&strf, sizeof (gst_riff_strf_auds));

  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: strf tag found in context auds");
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  format      %d", GUINT16_FROM_LE (strf->format));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  channels    %d", GUINT16_FROM_LE (strf->channels));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  rate        %d", GUINT32_FROM_LE (strf->rate));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  av_bps      %d", GUINT32_FROM_LE (strf->av_bps));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  blockalign  %d", GUINT16_FROM_LE (strf->blockalign));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  size        %d", GUINT16_FROM_LE (strf->size));

  srcpad =  gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_audio_templ), g_strdup_printf ("audio_%02d", 
			  avi_demux->num_a_streams));

  capslist = gst_caps_append(NULL, GST_CAPS_NEW (
			  "avidemux_audio_src",
			  "video/avi",
			    "format",    	GST_PROPS_STRING ("strf_auds"),
			      "fmt", 		GST_PROPS_INT (GUINT16_FROM_LE (strf->format)),
			      "channels", 	GST_PROPS_INT (GUINT16_FROM_LE (strf->channels)),
			      "rate", 		GST_PROPS_INT (GUINT32_FROM_LE (strf->rate)),
			      "av_bps",		GST_PROPS_INT (GUINT32_FROM_LE (strf->av_bps)),
			      "blockalign",	GST_PROPS_INT (GUINT16_FROM_LE (strf->blockalign)),
			      "size",		GST_PROPS_INT (GUINT16_FROM_LE (strf->size))
			  ));

  /* let's try some gstreamer-formatted mime types */
  switch (GUINT16_FROM_LE(strf->format))
  {
    case 0x0050:
    case 0x0055: /* mp3 */
      newcaps = gst_caps_new ("avidemux_audio_src",
                              "audio/mp3",
                                NULL);
      break;
    case 0x0001: /* PCM/wav */
      newcaps = gst_caps_new ("avidemux_audio_src",
                              "audio/raw",
                              gst_props_new (
                                "format",     GST_PROPS_STRING ("int"),
                                "law",        GST_PROPS_INT (0),
                                "endianness", GST_PROPS_INT (G_BYTE_ORDER),
                                "signed",     GST_PROPS_BOOLEAN ((GUINT16_FROM_LE (strf->size) != 8)),
                                "width",      GST_PROPS_INT ((GUINT16_FROM_LE (strf->blockalign)*8) /
                                                              GUINT16_FROM_LE (strf->channels)),
                                "depth",      GST_PROPS_INT (GUINT16_FROM_LE (strf->size)),
                                "rate",       GST_PROPS_INT (GUINT32_FROM_LE (strf->rate)),
                                "channels",   GST_PROPS_INT (GUINT16_FROM_LE (strf->channels)),
                                NULL
                              ));
      break;
  }

  if (newcaps) capslist = gst_caps_append(capslist, newcaps);


  gst_pad_try_set_caps(srcpad, capslist);
  gst_pad_set_event_function (srcpad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_function (srcpad, gst_avi_demux_handle_src_query);

  stream = &avi_demux->stream[avi_demux->num_streams];
  stream->pad = srcpad;
  gst_pad_set_element_private (srcpad, stream);
  avi_demux->num_streams++;
  avi_demux->num_a_streams++;

  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static void 
gst_avi_demux_strf_iavs (GstAviDemux *avi_demux)
{
  gst_riff_strf_iavs *strf;
  GstPad *srcpad;
  GstByteStream *bs = avi_demux->bs;
  GstCaps *newcaps = NULL, *capslist = NULL;
  avi_stream_context *stream;
  guint32       got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&strf, sizeof (gst_riff_strf_iavs));

  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: strf tag found in context iavs");
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVAAuxSrc   %08x", GUINT32_FROM_LE (strf->DVAAuxSrc));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVAAuxCtl   %08x", GUINT32_FROM_LE (strf->DVAAuxCtl));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVAAuxSrc1  %08x", GUINT32_FROM_LE (strf->DVAAuxSrc1));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVAAuxCtl1  %08x", GUINT32_FROM_LE (strf->DVAAuxCtl1));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVVAuxSrc   %08x", GUINT32_FROM_LE (strf->DVVAuxSrc));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVVAuxCtl   %08x", GUINT32_FROM_LE (strf->DVVAuxCtl));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVReserved1 %08x", GUINT32_FROM_LE (strf->DVReserved1));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  DVReserved2 %08x", GUINT32_FROM_LE (strf->DVReserved2));

  srcpad =  gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_video_templ), g_strdup_printf ("video_%02d", 
			  avi_demux->num_v_streams));

  capslist = gst_caps_append(NULL, GST_CAPS_NEW (
			  "avidemux_video_src",
			  "video/avi",
			    "format",    	GST_PROPS_STRING ("strf_iavs"),
                              "DVAAuxSrc", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVAAuxSrc)),
                              "DVAAuxCtl", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVAAuxCtl)),
                              "DVAAuxSrc1", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVAAuxSrc1)),
                              "DVAAuxCtl1", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVAAuxCtl1)),
                              "DVVAuxSrc", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVVAuxSrc)),
                              "DVVAuxCtl", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVVAuxCtl)),
                              "DVReserved1", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVReserved1)),
                              "DVReserved2", 	GST_PROPS_INT (GUINT32_FROM_LE (strf->DVReserved2))
			 ));

  newcaps = gst_caps_new ("avi_type_dv", 
                          "video/dv", 
                          gst_props_new (
                            "format",		GST_PROPS_STRING ("NTSC"), /* FIXME??? */
                            NULL));

  if (newcaps) capslist = gst_caps_append(capslist, newcaps);

  gst_pad_try_set_caps(srcpad, capslist);
  gst_pad_set_event_function (srcpad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_function (srcpad, gst_avi_demux_handle_src_query);

  stream = &avi_demux->stream[avi_demux->num_streams];
  stream->pad = srcpad;
  gst_pad_set_element_private (srcpad, stream);
  avi_demux->num_streams++;
  avi_demux->num_v_streams++;

  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static void
gst_avi_debug_entry (const gchar *prefix, gst_avi_index_entry *entry)
{
  GST_DEBUG (0, "%s: %05d %d %08llx %05d %08lld %08x %08x %08x\n", prefix, entry->index_nr, entry->stream_nr, 
		  entry->bytes_before, entry->frames_before, entry->ts, entry->flags, entry->offset, entry->size);
}

static void
gst_avi_demux_parse_index (GstAviDemux *avi_demux,
		          gulong filepos, gulong offset)
{
  GstBuffer *buf;
  gulong index_size;
  guint32 got_bytes;
  gint i;
  gst_riff_index_entry *entry;

  if (!gst_bytestream_seek (avi_demux->bs, filepos + offset, GST_SEEK_METHOD_SET)) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not seek to index");
    return;
  }
  got_bytes = gst_bytestream_read (avi_demux->bs, &buf, 8);
  while (got_bytes < 8) {
    guint32 remaining;
    GstEvent *event;
  
    gst_bytestream_get_status (avi_demux->bs, &remaining, &event);

    got_bytes = gst_bytestream_read (avi_demux->bs, &buf, 8);
  }
		  
  if (GST_BUFFER_OFFSET (buf) != filepos + offset || GST_BUFFER_SIZE (buf) != 8) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not get index");
    goto end;
  }

  if (gst_riff_fourcc_to_id (GST_BUFFER_DATA (buf)) != GST_RIFF_TAG_idx1) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: no index found");
    goto end;
  }

  index_size = GUINT32_FROM_LE(*(guint32 *)(GST_BUFFER_DATA (buf) + 4));
  gst_buffer_unref (buf);

  got_bytes = gst_bytestream_read (avi_demux->bs, &buf, index_size);
  if (got_bytes < index_size) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: error reading index");
    goto end;
  }

  avi_demux->index_size = index_size/sizeof(gst_riff_index_entry);
  GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: index size %lu", avi_demux->index_size);

  avi_demux->index_entries = g_malloc (avi_demux->index_size * sizeof (gst_avi_index_entry));

  entry = (gst_riff_index_entry *) GST_BUFFER_DATA (buf);

  for (i = 0; i < avi_demux->index_size; i++) {
    avi_stream_context *stream;
    gint stream_nr;
    gst_avi_index_entry *target = &avi_demux->index_entries[i];

    stream_nr = CHUNKID_TO_STREAMNR (entry[i].id);
    target->stream_nr = stream_nr;
    stream = &avi_demux->stream[stream_nr];

    target->index_nr = i;
    target->flags    = entry[i].flags;
    target->size     = entry[i].size;
    target->offset   = entry[i].offset;
    
    target->bytes_before = stream->total_bytes;
    target->frames_before = stream->total_frames;

    if (stream->strh.type == GST_RIFF_FCC_auds) {
      target->ts = stream->total_bytes * GST_SECOND * stream->strh.scale / stream->strh.rate;
    }
    else {
      target->ts = stream->total_frames * GST_SECOND * stream->strh.scale / stream->strh.rate;
    }

    gst_avi_debug_entry ("index", target);

    stream->total_bytes += target->size;
    stream->total_frames++;
  }
  gst_buffer_unref (buf);

end:
  avi_demux->index_offset = filepos;
  GST_DEBUG (GST_CAT_PLUGIN_INFO, "index offset at %08lx\n", filepos);

  if (!gst_bytestream_seek (avi_demux->bs, filepos, GST_SEEK_METHOD_SET)) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not seek back to movi");
    return;
  }
}

static gst_avi_index_entry*
gst_avi_demux_index_next (GstAviDemux *avi_demux, gint stream_nr, gint start, guint32 flags)
{
  gint i;
  gst_avi_index_entry *entry = NULL;

  for (i = start; i < avi_demux->index_size; i++) {
    entry = &avi_demux->index_entries[i];

    if (entry->stream_nr == stream_nr && (entry->flags & flags) == flags) {
      break;
    }
  }

  return entry;
}

static gst_avi_index_entry*
gst_avi_demux_index_entry_for_time (GstAviDemux *avi_demux, gint stream_nr, guint64 time, guint32 flags)
{
  gst_avi_index_entry *entry = NULL, *last_entry = NULL;
  gint i;

  i = -1;
  do {
    entry = gst_avi_demux_index_next (avi_demux, stream_nr, i + 1, flags);
    if (!entry)
      return NULL;

    i = entry->index_nr;

    if (entry->ts <= time) {
      last_entry = entry;
    }
  }
  while (entry->ts <= time);

  return last_entry;
}

static gboolean
gst_avi_demux_handle_src_query (GstPad *pad, GstPadQueryType type, 
				GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  //GstAviDemux *avi_demux = GST_AVI_DEMUX (gst_pad_get_parent (pad));
  avi_stream_context *stream = gst_pad_get_element_private (pad);

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          *value = GST_SECOND * stream->strh.scale *stream->strh.length / stream->strh.rate;
	  break;
        case GST_FORMAT_BYTES:
          if (stream->strh.type == GST_RIFF_FCC_auds)
            *value = stream->strh.length;
	  else
	    res = FALSE;
	  break;
        case GST_FORMAT_UNIT:
          if (stream->strh.type == GST_RIFF_FCC_auds)
            *value = stream->strh.length * stream->strh.samplesize;
	  else if (stream->strh.type == GST_RIFF_FCC_vids)
            *value = stream->strh.length;
	  else
	    res = FALSE;
	  break;
	default:
          res = FALSE;
	  break;
      }
      break;
    case GST_PAD_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          if (stream->strh.type == GST_RIFF_FCC_auds)
            *value = stream->current_byte * GST_SECOND / stream->strh.rate;
	  else
            *value = stream->next_ts;
	  break;
        case GST_FORMAT_BYTES:
          *value = stream->current_byte;
	  break;
        case GST_FORMAT_UNIT:
          if (stream->strh.type == GST_RIFF_FCC_auds)
            *value = stream->current_byte * stream->strh.samplesize;
	  else if (stream->strh.type == GST_RIFF_FCC_vids)
            *value = stream->current_frame;
	  else
	    res = FALSE;
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

static gint32
gst_avi_demux_sync_streams (GstAviDemux *avi_demux, guint64 time)
{
  gint i;
  guint64 min_index = -1;
  avi_stream_context *stream;
  gst_avi_index_entry *entry;

  for (i = 0; i < avi_demux->num_streams; i++) {
    stream = &avi_demux->stream[i];

    GST_DEBUG (0, "finding %d for time %lld\n", i, time);

    entry = gst_avi_demux_index_entry_for_time (avi_demux, stream->num, time, GST_RIFF_IF_KEYFRAME);
    if (entry) {
      gst_avi_debug_entry ("sync entry", entry);

      min_index = MIN (entry->index_nr, min_index);
    }
  }
  
  /* now we know the entry we need to sync on. calculate number of frames to
   * skip fro there on and the stream stats */
  for (i = 0; i < avi_demux->num_streams; i++) {
    gst_avi_index_entry *next_entry;
    stream = &avi_demux->stream[i];

    /* next entry */
    next_entry = gst_avi_demux_index_next (avi_demux, stream->num, min_index, 0);
    /* next entry with keyframe */
    entry = gst_avi_demux_index_next (avi_demux, stream->num, min_index, GST_RIFF_IF_KEYFRAME);
    gst_avi_debug_entry ("final sync", entry);

    stream->next_ts = next_entry->ts;
    stream->current_byte = next_entry->bytes_before;
    stream->current_frame = next_entry->frames_before;
    stream->skip = entry->frames_before - next_entry->frames_before;

    GST_DEBUG (0, "%d skip %d\n", stream->num, stream->skip);
  }

  return min_index;
}

static gboolean
gst_avi_demux_send_event (GstElement *element, GstEvent *event)
{
  GList *pads;

  pads = gst_element_get_pad_list (element);

  while (pads) { 
    GstPad *pad = GST_PAD (pads->data);

    if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
      return gst_avi_demux_handle_src_event (pad, event);
    }
    
    pads = g_list_next (pads);
  }
  
  return FALSE;
}

static gboolean
gst_avi_demux_handle_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstAviDemux *avi_demux = GST_AVI_DEMUX (gst_pad_get_parent (pad));
  avi_stream_context *stream;
  
  stream = gst_pad_get_element_private (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      switch (GST_EVENT_SEEK_FORMAT (event)) {
	case GST_FORMAT_BYTES:
	case GST_FORMAT_UNIT:
	  break;
	case GST_FORMAT_TIME:
        {
	  gst_avi_index_entry *seek_entry, *entry;
	  gint64 desired_offset = GST_EVENT_SEEK_OFFSET (event);
	  guint32 flags;
          guint64 min_index;

	  /* no seek on audio yet */
	  if (stream->strh.type == GST_RIFF_FCC_auds)
	    return FALSE;

          flags = GST_RIFF_IF_KEYFRAME;

          entry = gst_avi_demux_index_entry_for_time (avi_demux, stream->num, desired_offset, GST_RIFF_IF_KEYFRAME);
	  if (entry) {
            desired_offset = entry->ts;
	    min_index = gst_avi_demux_sync_streams (avi_demux, desired_offset);
            seek_entry = &avi_demux->index_entries[min_index];
	    
	    avi_demux->seek_offset = seek_entry->offset + avi_demux->index_offset;
            avi_demux->seek_pending = TRUE;
	    avi_demux->last_seek = seek_entry->ts;
	  }
	  else {
	    res = FALSE;
	  }
	  break;
	}
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

static gboolean
gst_avi_demux_handle_sink_event (GstAviDemux *avi_demux)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;
  
  gst_bytestream_get_status (avi_demux->bs, &remaining, &event);

  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS:
      gst_pad_event_default (avi_demux->sinkpad, event);
      break;
    case GST_EVENT_SEEK:
      g_warning ("seek event\n");
      break;
    case GST_EVENT_FLUSH:
      g_warning ("flush event\n");
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint i;

      for (i = 0; i < avi_demux->num_streams; i++) {
        avi_stream_context *stream = &avi_demux->stream[i];

        event = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
			avi_demux->last_seek + stream->delay, NULL);
	gst_pad_push (stream->pad, GST_BUFFER (event));
      }
      break;
    }
    default:
      g_warning ("unhandled event %d\n", type);
      break;
  }

  return TRUE;
}

static inline gboolean 
gst_avi_demux_read_chunk (GstAviDemux *avi_demux, guint32 *id, guint32 *size)
{
  gst_riff_chunk *chunk;
  GstByteStream *bs = avi_demux->bs;
  guint32       got_bytes;

  if (avi_demux->seek_pending) {
    GST_DEBUG (0, "avidemux: seek pending to %lld %08llx\n", avi_demux->seek_offset, avi_demux->seek_offset);
    if (!gst_bytestream_seek (avi_demux->bs, avi_demux->seek_offset, GST_SEEK_METHOD_SET)) {
      GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not seek");
    }
    avi_demux->seek_pending = FALSE;
  }

  do {
    got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&chunk, sizeof (gst_riff_chunk));
    if (got_bytes == sizeof (gst_riff_chunk)) {
      *id =   GUINT32_FROM_LE (chunk->id);
      *size = GUINT32_FROM_LE (chunk->size);

      gst_bytestream_flush (bs, sizeof (gst_riff_chunk));

      return TRUE;
    }
  } while (gst_avi_demux_handle_sink_event (avi_demux));

  return TRUE;
}

static gboolean
gst_avi_demux_process_chunk (GstAviDemux *avi_demux, guint64 *filepos,
			     guint32 desired_tag,
			     gint rec_depth, guint32 *chunksize)
{
  guint32 chunkid;	
  GstByteStream *bs = avi_demux->bs;

  if (!gst_avi_demux_read_chunk (avi_demux, &chunkid, chunksize)) {
    g_print ("  *****  Error reading chunk at filepos 0x%08llx\n", *filepos);
    return FALSE;
  }
  if (desired_tag) {		/* do we have to test identity? */
    if (desired_tag != chunkid) {
      g_print ("\n\n *** Error: Expected chunk '%s', found '%s'\n",
	      gst_riff_id_to_fourcc (desired_tag),
	      gst_riff_id_to_fourcc (chunkid));
      return FALSE;
    }
  }

  GST_INFO (GST_CAT_PLUGIN_INFO, "chunkid %s, size %08x, filepos %08llx", 
		  gst_riff_id_to_fourcc (chunkid), *chunksize, *filepos);

  *filepos += (sizeof (guint32) + sizeof (guint32));

  switch (chunkid) {
    case GST_RIFF_TAG_RIFF:
    case GST_RIFF_TAG_LIST:
    {
      guint32 datashowed;
      guint32 subchunksize = 0;	/* size of a read subchunk             */
      gchar *formtype;
      guint32       got_bytes;

      got_bytes = gst_bytestream_peek_bytes (bs, (guint8**)&formtype, sizeof (guint32));
      if (got_bytes < sizeof(guint32))
	return FALSE;

      switch (GUINT32_FROM_LE (*((guint32*)formtype))) {
	case GST_RIFF_LIST_movi:
	  gst_avi_demux_parse_index (avi_demux, *filepos, *chunksize);
          while (!gst_bytestream_flush (bs, sizeof (guint32))) {
            guint32 remaining;
            GstEvent *event;

            gst_bytestream_get_status (avi_demux->bs, &remaining, &event);
	  }
          break;
	default:
          /* flush the form type */
          gst_bytestream_flush_fast (bs, sizeof (guint32));
          break;
      }

      datashowed = sizeof (guint32);	/* we showed the form type      */
      *filepos += datashowed;	/* for the rest of the routine  */

      while (datashowed < *chunksize) {	/* while not showed all: */

        GST_INFO (GST_CAT_PLUGIN_INFO, "process chunk filepos %08llx", *filepos);
	/* recurse for subchunks of RIFF and LIST chunks: */
	if (!gst_avi_demux_process_chunk (avi_demux, filepos, 0,
			   rec_depth + 1, &subchunksize)) {

          g_print ("  *****  Error processing chunk at filepos 0x%08llxi
			  %u %u\n", *filepos, *chunksize, datashowed);
	  return FALSE;
	}

	subchunksize = ((subchunksize + 1) & ~1);

	datashowed += (sizeof (guint32) + sizeof (guint32) + subchunksize);
        GST_INFO (GST_CAT_PLUGIN_INFO, "process chunk done filepos %08llx, subchunksize %08x", 
			*filepos, subchunksize);
      }
      if (datashowed != *chunksize) {
	g_warning ("error parsing AVI %u %u", datashowed, *chunksize);
      }
      goto done;
    }
    case GST_RIFF_TAG_avih:
      gst_avi_demux_avih (avi_demux);
      break;
    case GST_RIFF_TAG_strh:
    {
      gst_avi_demux_strh (avi_demux);
      break;
    }
    case GST_RIFF_TAG_strf:
      switch (avi_demux->fcc_type) {
        case GST_RIFF_FCC_vids:
          gst_avi_demux_strf_vids (avi_demux);
	  break;
        case GST_RIFF_FCC_auds:
          gst_avi_demux_strf_auds (avi_demux);
	  break;
        case GST_RIFF_FCC_iavs:
          gst_avi_demux_strf_iavs (avi_demux);
	  break;
	case GST_RIFF_FCC_pads:
	case GST_RIFF_FCC_txts:
	default:
          GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux_chain: strh type %s not supported", 
			  gst_riff_id_to_fourcc (avi_demux->fcc_type));
	  break;
      }
      break;
    case GST_RIFF_00dc:
    case GST_RIFF_00db:
    case GST_RIFF_00__:
    case GST_RIFF_01wb:
    {
      gint stream_id;
      avi_stream_context *stream;
      
      stream_id = CHUNKID_TO_STREAMNR (chunkid);
		   
      stream = &avi_demux->stream[stream_id];

      GST_DEBUG (0,"gst_avi_demux_chain: tag found %08x size %08x",
		    chunkid, *chunksize);

      if (stream->strh.type == GST_RIFF_FCC_vids) {
        stream->next_ts = stream->current_frame * GST_SECOND * stream->strh.scale / stream->strh.rate;
      }
      else {
        stream->next_ts = stream->current_byte * GST_SECOND * stream->strh.scale / stream->strh.rate;
      }
      if (stream->strh.init_frames == stream->current_frame && stream->delay==0)
	stream->delay = stream->next_ts;

      if (stream->skip) {
	stream->skip--;
      }
      else {
        if (GST_PAD_IS_CONNECTED (stream->pad)) {
	  GstBuffer *buf;
          guint32   got_bytes;

	  if (*chunksize) {
            got_bytes = gst_bytestream_peek (bs, &buf, *chunksize);

            GST_BUFFER_TIMESTAMP (buf) = stream->next_ts;

            if (stream->need_flush) {
               /* FIXME, do some flush event here */
              stream->need_flush = FALSE;
            }
	    GST_DEBUG (0, "send stream %d: %lld %d %lld %08x\n", stream_id, stream->next_ts, stream->current_frame,
			  stream->delay, *chunksize);

            gst_pad_push(stream->pad, buf);
	  }
        }
      }
      stream->current_frame++;
      stream->current_byte += *chunksize;

      *chunksize = (*chunksize + 1) & ~1;
      break;
    }
    default:
      GST_DEBUG (0, "  *****  unknown chunkid %08x (%s)", chunkid, gst_riff_id_to_fourcc (chunkid));
      *chunksize = (*chunksize + 1) & ~1;
      break;
  }
  GST_INFO (GST_CAT_PLUGIN_INFO, "chunkid %s, flush %08x, filepos %08llx", 
		  gst_riff_id_to_fourcc (chunkid), *chunksize, *filepos);

  *filepos += *chunksize;
  if (!gst_bytestream_flush (bs, *chunksize)) {
    return gst_avi_demux_handle_sink_event (avi_demux);
  }

done:
  /* we are running in an infinite loop, we need to _yield 
   * from time to time */
  gst_element_yield (GST_ELEMENT (avi_demux));

  return TRUE;
}

static void
gst_avi_demux_loop (GstElement *element)
{
  GstAviDemux *avi_demux;
  guint32 chunksize;
  guint64 filepos = 0;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_AVI_DEMUX (element));

  avi_demux = GST_AVI_DEMUX (element);

  /* this is basically an infinite loop */
  if (!gst_avi_demux_process_chunk (avi_demux, &filepos, GST_RIFF_TAG_RIFF, 0, &chunksize)) {
    gst_element_error (element, "This doesn't appear to be an AVI file");
    return;
  }
  /* if we exit the loop we are EOS */
  gst_pad_event_default (avi_demux->sinkpad, gst_event_new (GST_EVENT_EOS));
}

static GstElementStateReturn
gst_avi_demux_change_state (GstElement *element)
{
  GstAviDemux *avi_demux = GST_AVI_DEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      avi_demux->bs = gst_bytestream_new (avi_demux->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (avi_demux->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_avi_demux_get_property (GObject *object, guint prop_id, GValue *value,
			    GParamSpec *pspec)
{
  GstAviDemux *src;

  g_return_if_fail (GST_IS_AVI_DEMUX (object));

  src = GST_AVI_DEMUX (object);

  switch (prop_id) {
    case ARG_BITRATE:
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* this filter needs the riff parser */
  if (!gst_library_load ("gstbytestream")) {
    gst_info("avidemux: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }
  if (!gst_library_load ("gstriff")) {
    gst_info("avidemux: could not load support library: 'gstriff'\n");
    return FALSE;
  }

  /* create an elementfactory for the avi_demux element */
  factory = gst_element_factory_new ("avidemux",GST_TYPE_AVI_DEMUX,
                                    &gst_avi_demux_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_audio_templ));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_video_templ));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_templ));

  type = gst_type_factory_new (&avidefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "avidemux",
  plugin_init
};

