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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstavidemux.h"
#include "gstavimux.h"

GST_DEBUG_CATEGORY_STATIC (avidemux_debug);
#define GST_CAT_DEFAULT avidemux_debug

/* AviDemux signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BITRATE,
  ARG_METADATA,
  ARG_STREAMINFO,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "avidemux_sink",
    "video/x-msvideo",
      NULL
  )
);

static void 	gst_avi_demux_base_init	        (gpointer g_class);
static void 	gst_avi_demux_class_init	(GstAviDemuxClass *klass);
static void 	gst_avi_demux_init		(GstAviDemux *avi_demux);

static void 	gst_avi_demux_loop 		(GstElement  *element);

static gboolean gst_avi_demux_send_event 	(GstElement  *element,
						 GstEvent    *event);

static const GstEventMask *
		gst_avi_demux_get_event_mask 	(GstPad      *pad);
static gboolean gst_avi_demux_handle_src_event 	(GstPad      *pad,
						 GstEvent    *event);
static const GstFormat *
		gst_avi_demux_get_src_formats 	(GstPad      *pad); 
static const GstQueryType *
		gst_avi_demux_get_src_query_types (GstPad    *pad);
static gboolean gst_avi_demux_handle_src_query 	(GstPad      *pad,
						 GstQueryType type, 
						 GstFormat   *format,
						 gint64      *value);
static gboolean gst_avi_demux_src_convert 	(GstPad      *pad,
						 GstFormat    src_format,
						 gint64       src_value,
	        	          		 GstFormat   *dest_format,
						 gint64      *dest_value);

static GstElementStateReturn
		gst_avi_demux_change_state 	(GstElement  *element);

static void     gst_avi_demux_get_property      (GObject     *object,
						 guint        prop_id, 	
						 GValue      *value,
						 GParamSpec  *pspec);

static GstCaps * gst_avi_demux_audio_caps (guint16 codec_id,
    gst_riff_strf_auds *strf, GstAviDemux *avi_demux);
static GstCaps * gst_avi_demux_video_caps (guint32 codec_fcc,
    gst_riff_strh *strh, gst_riff_strf_vids *strf,
    GstAviDemux *avi_demux);
static GstCaps * gst_avi_demux_iavs_caps (void);

static GstPadTemplate *videosrctempl, *audiosrctempl;
static GstElementClass *parent_class = NULL;
/*static guint gst_avi_demux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avi_demux_get_type(void) 
{
  static GType avi_demux_type = 0;

  if (!avi_demux_type) {
    static const GTypeInfo avi_demux_info = {
      sizeof(GstAviDemuxClass),      
      gst_avi_demux_base_init,
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
gst_avi_demux_base_init (gpointer g_class)
{
  static GstElementDetails gst_avi_demux_details = GST_ELEMENT_DETAILS (
    "Avi demuxer",
    "Codec/Demuxer",
    "Demultiplex an avi file into audio and video",
    "Erik Walthinsen <omega@cse.ogi.edu>\n"
    "Wim Taymans <wim.taymans@chello.be>"
  );
  static guint32 vid_list[] = {
    GST_MAKE_FOURCC('I','4','2','0'),
    GST_MAKE_FOURCC('Y','U','Y','2'),
    GST_MAKE_FOURCC('M','J','P','G'),
    GST_MAKE_FOURCC('D','V','S','D'),
    GST_MAKE_FOURCC('W','M','V','1'),
    GST_MAKE_FOURCC('W','M','V','2'),
    GST_MAKE_FOURCC('M','P','G','4'),
    GST_MAKE_FOURCC('M','P','4','2'),
    GST_MAKE_FOURCC('M','P','4','3'),
    GST_MAKE_FOURCC('H','F','Y','U'),
    GST_MAKE_FOURCC('D','I','V','3'),
    GST_MAKE_FOURCC('M','P','E','G'),
    GST_MAKE_FOURCC('H','2','6','3'),
    GST_MAKE_FOURCC('D','I','V','X'),
    GST_MAKE_FOURCC('X','V','I','D'),
    GST_MAKE_FOURCC('3','I','V','1'),
    0 /* end */
  };
  static gint aud_list[] = {
    GST_RIFF_WAVE_FORMAT_MPEGL3,
    GST_RIFF_WAVE_FORMAT_MPEGL12,
    GST_RIFF_WAVE_FORMAT_PCM,
    GST_RIFF_WAVE_FORMAT_VORBIS1,
    GST_RIFF_WAVE_FORMAT_A52,
    GST_RIFF_WAVE_FORMAT_ALAW,
    GST_RIFF_WAVE_FORMAT_MULAW,
    -1 /* end */
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  gint i = 0;
  GstCaps *audcaps = NULL, *vidcaps = NULL, *temp;

  for (i = 0; aud_list[i] != -1; i++) {
    temp = gst_avi_demux_audio_caps (aud_list[i], NULL, NULL);
    audcaps = gst_caps_append (audcaps, temp);
  }
  audiosrctempl = gst_pad_template_new ("audio_%02d",
					GST_PAD_SRC,
					GST_PAD_SOMETIMES,
					audcaps, NULL);
  for (i = 0; vid_list[i] != 0; i++) {
    temp = gst_avi_demux_video_caps (vid_list[i], NULL, NULL, NULL);
    vidcaps = gst_caps_append (vidcaps, temp);
  }
  vidcaps = gst_caps_append (vidcaps,
			     gst_avi_demux_iavs_caps ());
  videosrctempl = gst_pad_template_new ("video_%02d",
					GST_PAD_SRC,
					GST_PAD_SOMETIMES,
					vidcaps, NULL);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (sink_templ));
  gst_element_class_set_details (element_class, &gst_avi_demux_details);

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
  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                        GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
    g_param_spec_boxed ("streaminfo", "Streaminfo", "Streaminfo",
                        GST_TYPE_CAPS, G_PARAM_READABLE));

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
}

static gboolean
gst_avi_demux_avih (GstAviDemux *avi_demux)
{
  gst_riff_avih *avih;
  guint8 *avihdata;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, &avihdata, sizeof (gst_riff_avih));
  avih = (gst_riff_avih *) avihdata;

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

    GST_INFO ( "gst_avi_demux: avih tag found");
    GST_INFO ( "gst_avi_demux:  us_frame    %d", avi_demux->avih.us_frame);
    GST_INFO ( "gst_avi_demux:  max_bps     %d", avi_demux->avih.max_bps);
    GST_INFO ( "gst_avi_demux:  pad_gran    %d", avi_demux->avih.pad_gran);
    GST_INFO ( "gst_avi_demux:  flags       0x%08x", avi_demux->avih.flags);
    GST_INFO ( "gst_avi_demux:  tot_frames  %d", avi_demux->avih.tot_frames);
    GST_INFO ( "gst_avi_demux:  init_frames %d", avi_demux->avih.init_frames);
    GST_INFO ( "gst_avi_demux:  streams     %d", avi_demux->avih.streams);
    GST_INFO ( "gst_avi_demux:  bufsize     %d", avi_demux->avih.bufsize);
    GST_INFO ( "gst_avi_demux:  width       %d", avi_demux->avih.width);
    GST_INFO ( "gst_avi_demux:  height      %d", avi_demux->avih.height);
    GST_INFO ( "gst_avi_demux:  scale       %d", avi_demux->avih.scale);
    GST_INFO ( "gst_avi_demux:  rate        %d", avi_demux->avih.rate);
    GST_INFO ( "gst_avi_demux:  start       %d", avi_demux->avih.start);
    GST_INFO ( "gst_avi_demux:  length      %d", avi_demux->avih.length);

    return TRUE;
  }
  return FALSE;
}

static gboolean 
gst_avi_demux_strh (GstAviDemux *avi_demux)
{
  gst_riff_strh *strh;
  guint8 *strhdata;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, &strhdata, sizeof (gst_riff_strh));
  strh = (gst_riff_strh *) strhdata;

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

    GST_INFO ( "gst_avi_demux: strh tag found");
    GST_INFO ( "gst_avi_demux:  type        0x%08x (%s)", 
  		  target->strh.type, gst_riff_id_to_fourcc (strh->type));
    GST_INFO ( "gst_avi_demux:  fcc_handler 0x%08x (%s)", 
		  target->strh.fcc_handler, gst_riff_id_to_fourcc (strh->fcc_handler));
    GST_INFO ( "gst_avi_demux:  flags       0x%08x", strh->flags);
    GST_INFO ( "gst_avi_demux:  priority    %d", target->strh.priority);
    GST_INFO ( "gst_avi_demux:  init_frames %d", target->strh.init_frames);
    GST_INFO ( "gst_avi_demux:  scale       %d", target->strh.scale);
    GST_INFO ( "gst_avi_demux:  rate        %d", target->strh.rate);
    GST_INFO ( "gst_avi_demux:  start       %d", target->strh.start);
    GST_INFO ( "gst_avi_demux:  length      %d", target->strh.length);
    GST_INFO ( "gst_avi_demux:  bufsize     %d", target->strh.bufsize);
    GST_INFO ( "gst_avi_demux:  quality     %d", target->strh.quality);
    GST_INFO ( "gst_avi_demux:  samplesize  %d", target->strh.samplesize);

    target->delay = 0LL;
    target->total_bytes = 0LL;
    target->total_frames = 0;
    target->end_pos = -1;
    target->current_frame = 0;
    target->current_byte = 0;
    target->need_flush = FALSE;
    target->skip = 0;

    avi_demux->avih.bufsize = MAX (avi_demux->avih.bufsize, target->strh.bufsize);

    return TRUE;
  }
  return FALSE;
}

static void
gst_avi_demux_dmlh (GstAviDemux *avi_demux)
{
  gst_riff_dmlh *dmlh;
  guint8 *dmlhdata;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, &dmlhdata, sizeof (gst_riff_dmlh));
  dmlh = (gst_riff_dmlh *) dmlhdata;
}

static void
gst_avi_demux_strn (GstAviDemux *avi_demux, gint len)
{
  gchar *name;
  guint8 *namedata;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;

  got_bytes = gst_bytestream_peek_bytes (bs, &namedata, len);
  name = (gchar *) namedata;
  if (got_bytes != len)
    return;

  GST_DEBUG ("Stream name: \"%s\"", name);
}

static void
gst_avi_demux_metadata (GstAviDemux *avi_demux, gint len)
{
  guint32 got_bytes;
  GstByteStream  *bs = avi_demux->bs;
  gst_riff_chunk *temp_chunk, chunk;
  guint8 *tempdata;
  gchar *name, *type;
  GstProps *props;
  GstPropsEntry *entry;

  props = gst_props_empty_new ();

  while (len > 0) {
    got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_chunk));
    temp_chunk = (gst_riff_chunk *) tempdata;
    
    /* fixup for our big endian friends */
    chunk.id = GUINT32_FROM_LE (temp_chunk->id);
    chunk.size = GUINT32_FROM_LE (temp_chunk->size);

    gst_bytestream_flush (bs, sizeof (gst_riff_chunk));
    if (got_bytes != sizeof (gst_riff_chunk))
      return;
    len -= sizeof (gst_riff_chunk);

    /* don't care about empty entries - move on */
    if (chunk.size == 0)
      continue;

    got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, chunk.size);
    name = (gchar *) tempdata;
    gst_bytestream_flush (bs, (chunk.size + 1) & ~1);
    if (got_bytes != chunk.size)
      return;
    len -= ((chunk.size + 1) & ~1);

    /* we now have an info string in 'name' of type 'chunk.id' - find 'type' */
    switch (chunk.id) {
      case GST_RIFF_INFO_IARL:
        type = "Location";
        break;
      case GST_RIFF_INFO_IART:
        type = "Artist";
        break;
      case GST_RIFF_INFO_ICMS:
        type = "Commissioner";
        break;
      case GST_RIFF_INFO_ICMT:
        type = "Comment";
        break;
      case GST_RIFF_INFO_ICOP:
        type = "Copyright";
        break;
      case GST_RIFF_INFO_ICRD:
        type = "Creation Date";
        break;
      case GST_RIFF_INFO_ICRP:
        type = "Cropped";
        break;
      case GST_RIFF_INFO_IDIM:
        type = "Dimensions";
        break;
      case GST_RIFF_INFO_IDPI:
        type = "Dots per Inch";
        break;
      case GST_RIFF_INFO_IENG:
        type = "Engineer";
        break;
      case GST_RIFF_INFO_IGNR:
        type = "Genre";
        break;
      case GST_RIFF_INFO_IKEY:
        type = "Keywords";
        break;
      case GST_RIFF_INFO_ILGT:
        type = "Lightness";
        break;
      case GST_RIFF_INFO_IMED:
        type = "Medium";
        break;
      case GST_RIFF_INFO_INAM:
        type = "Title"; /* "Name" */
        break;
      case GST_RIFF_INFO_IPLT:
        type = "Palette";
        break;
      case GST_RIFF_INFO_IPRD:
        type = "Product";
        break;
      case GST_RIFF_INFO_ISBJ:
        type = "Subject";
        break;
      case GST_RIFF_INFO_ISFT:
        type = "Encoder"; /* "Software" */
        break;
      case GST_RIFF_INFO_ISHP:
        type = "Sharpness";
        break;
      case GST_RIFF_INFO_ISRC:
        type = "Source";
        break;
      case GST_RIFF_INFO_ISRF:
        type = "Source Form";
        break;
      case GST_RIFF_INFO_ITCH:
        type = "Technician";
        break;
      default:
	type = NULL;
	break;
    }

    if (type) {
      /* create props entry */
      entry = gst_props_entry_new (type, GST_PROPS_STRING (name));
      gst_props_add_entry (props, entry);
    }
  }

  gst_props_debug(props);

  gst_caps_replace_sink (&avi_demux->metadata,
		         gst_caps_new("avi_metadata",
                                      "application/x-gst-metadata",
                                        props));

  g_object_notify(G_OBJECT(avi_demux), "metadata");
}

static void
gst_avi_demux_streaminfo (GstAviDemux *avi_demux)
{
  GstProps *props;

  props = gst_props_empty_new ();

  /* compression formats are added later - a bit hacky */

  gst_caps_replace_sink (&avi_demux->streaminfo,
		  	 gst_caps_new("avi_streaminfo",
                                      "application/x-gst-streaminfo",
                                      props));

  /*g_object_notify(G_OBJECT(avi_demux), "streaminfo");*/
}

/* video/audio pad/caps stuff */

#ifdef G_HAVE_ISO_VARARGS

#define GST_AVI_VID_CAPS_NEW(name, mimetype, ...)		\
	(strf != NULL) ?					\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT (width),		\
		      "height", GST_PROPS_INT (height),		\
		      "framerate", GST_PROPS_FLOAT (framerate), \
		      __VA_ARGS__)				\
	:							\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT_RANGE (16, 4096),	\
		      "height", GST_PROPS_INT_RANGE (16, 4096),	\
		      "framerate", GST_PROPS_FLOAT_RANGE (0., G_MAXFLOAT), \
		      __VA_ARGS__)

#elif defined(G_HAVE_GNUC_VARARGS)

#define GST_AVI_VID_CAPS_NEW(name, mimetype, props...)		\
	(strf != NULL) ?					\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT (width),		\
		      "height", GST_PROPS_INT (height),		\
		      "framerate", GST_PROPS_FLOAT (framerate), \
		      ##props)					\
	:							\
	GST_CAPS_NEW (name,					\
		      mimetype,					\
		      "width",  GST_PROPS_INT_RANGE (16, 4096),	\
		      "height", GST_PROPS_INT_RANGE (16, 4096),	\
		      "framerate", GST_PROPS_FLOAT_RANGE (0., G_MAXFLOAT), \
		      ##props)
#endif

static GstCaps *
gst_avi_demux_video_caps (guint32 codec_fcc,
			  gst_riff_strh *strh,
			  gst_riff_strf_vids *strf,
			  GstAviDemux *avi_demux)
{
  GstCaps *caps = NULL;
  gchar *codecname = NULL;
  gint width = -1, height = -1;
  gdouble framerate = 0.;

  if (strf != NULL) {
    width = GUINT32_FROM_LE (strf->width);
    height = GUINT32_FROM_LE (strf->height);
  }
  if (strh != NULL) {
    framerate = 1. * GUINT32_FROM_LE (strh->rate) /
		GUINT32_FROM_LE (strh->scale); /* fps */
  }

  switch (codec_fcc) {
    case GST_MAKE_FOURCC('I','4','2','0'):
    case GST_MAKE_FOURCC('Y','U','Y','2'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_raw",
                  "video/x-raw-yuv",
                    "format",  GST_PROPS_FOURCC (codec_fcc)
                );
      codecname = g_strdup_printf("Raw Video (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('M','J','P','G'): /* YUY2 MJPEG */
    case GST_MAKE_FOURCC('J','P','E','G'): /* generic (mostly RGB) MJPEG */
    case GST_MAKE_FOURCC('P','I','X','L'): /* Miro/Pinnacle fourccs */
    case GST_MAKE_FOURCC('V','I','X','L'): /* Miro/Pinnacle fourccs */
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_jpeg",
                  "video/x-jpeg",
                    NULL
                );
      codecname = g_strdup_printf("Motion-JPEG (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('H','F','Y','U'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_hfyu",
                  "video/x-huffyuv",
                    NULL
                );
      codecname = g_strdup_printf("HuffYUV (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('M','P','E','G'):
    case GST_MAKE_FOURCC('M','P','G','I'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_mpeg",
                  "video/mpeg",
                    "systemstream", GST_PROPS_BOOLEAN (FALSE),
		    "mpegversion", GST_PROPS_BOOLEAN (1)
                );
      codecname = g_strdup_printf("MPEG-1 (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('H','2','6','3'):
    case GST_MAKE_FOURCC('i','2','6','3'):
    case GST_MAKE_FOURCC('L','2','6','3'):
    case GST_MAKE_FOURCC('M','2','6','3'):
    case GST_MAKE_FOURCC('V','D','O','W'):
    case GST_MAKE_FOURCC('V','I','V','O'):
    case GST_MAKE_FOURCC('x','2','6','3'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_263",
                  "video/x-h263",
                    NULL
                );
      codecname = g_strdup_printf("H263-compatible (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('D','I','V','3'):
    case GST_MAKE_FOURCC('D','I','V','4'):
    case GST_MAKE_FOURCC('D','I','V','5'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_divx3",
                  "video/x-divx",
		    "divxversion", GST_PROPS_INT(3)
                );
      codecname = g_strdup_printf("DivX-3.x (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('d','i','v','x'):
    case GST_MAKE_FOURCC('D','I','V','X'):
    case GST_MAKE_FOURCC('D','X','5','0'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_divx5",
                  "video/x-divx",
		    "divxversion", GST_PROPS_INT(5)
                );
      codecname = g_strdup_printf("DivX 4.x/5.x (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('X','V','I','D'):
    case GST_MAKE_FOURCC('x','v','i','d'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src",
                  "video/x-xvid",
                    NULL
                );
      codecname = g_strdup_printf("XviD (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('M','P','G','4'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src",
                  "video/x-msmpeg",
		    "msmpegversion", GST_PROPS_INT (41)
                );
      codecname = g_strdup_printf("MS MPEG-4.1 (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('M','P','4','2'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src",
                  "video/x-msmpeg",
		    "msmpegversion", GST_PROPS_INT (42)
                );
      codecname = g_strdup_printf("MS MPEG-4.2 (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('M','P','4','3'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src",
                  "video/x-msmpeg",
		    "msmpegversion", GST_PROPS_INT (43)
                );
      codecname = g_strdup_printf("MS MPEG-4.3 (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('3','I','V','1'):
    case GST_MAKE_FOURCC('3','I','V','2'):
      caps = GST_AVI_VID_CAPS_NEW (
		  "avidemux_video_src_3ivx",
		  "video/x-3ivx",
		    NULL
		);
      codecname = g_strdup_printf("3ivX (" GST_FOURCC_FORMAT ")",
				  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('D','V','S','D'):
    case GST_MAKE_FOURCC('d','v','s','d'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src",
                  "video/x-dv",
                    "systemstream", GST_PROPS_BOOLEAN (FALSE)
                );
      codecname = g_strdup_printf("Digital Video type 2 (" GST_FOURCC_FORMAT ")",
                                  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('W','M','V','1'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_wmv1",
                  "video/x-wmv",
                    "wmvversion", GST_PROPS_INT (1)
                );
      codecname = g_strdup_printf("Windows Media Format 1 ("
				  GST_FOURCC_FORMAT ")",
				  GST_FOURCC_ARGS(codec_fcc));
      break;

    case GST_MAKE_FOURCC('W','M','V','2'):
      caps = GST_AVI_VID_CAPS_NEW (
                  "avidemux_video_src_wmv2",
                  "video/x-wmv",
                    "wmvversion", GST_PROPS_INT (2)
                );
      codecname = g_strdup_printf("Windows Media Format 2 ("
				  GST_FOURCC_FORMAT ")",
				  GST_FOURCC_ARGS(codec_fcc));
      break;

    default:
      g_warning ("avidemux: unkown video format " GST_FOURCC_FORMAT,
		 GST_FOURCC_ARGS(codec_fcc));
      break;
  }

  /* set video codec info on streaminfo caps */
  if (avi_demux != NULL && codecname != NULL) {
    GstPropsEntry *entry;
    entry = gst_props_entry_new("videocodec",
				GST_PROPS_STRING(codecname));
    gst_props_add_entry(avi_demux->streaminfo->properties, entry);
  }
  if (codecname != NULL) {
    g_free(codecname);
  }

  return caps;
}

static void 
gst_avi_demux_strf_vids (GstAviDemux *avi_demux)
{
  gst_riff_strf_vids *strf;
  gst_riff_strh *strh;
  guint8 *strfdata;
  GstPad *srcpad;
  GstCaps *caps = NULL;
  avi_stream_context *stream;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;
  gchar *padname;

  got_bytes = gst_bytestream_peek_bytes (bs, &strfdata, sizeof (gst_riff_strf_vids));
  strf = (gst_riff_strf_vids *) strfdata;
  if (got_bytes != sizeof (gst_riff_strf_vids))
    return;

  padname = g_strdup_printf ("video_%02d", avi_demux->num_v_streams);
  srcpad =  gst_pad_new_from_template (videosrctempl, padname);
  g_free (padname);

  /* let's try some gstreamer-like mime-type caps */
  strh = &avi_demux->stream[avi_demux->num_streams].strh;
  caps = gst_avi_demux_video_caps (GUINT32_FROM_LE(strf->compression),
				   strh, strf, avi_demux);

  if (caps != NULL) {
    gst_pad_try_set_caps (srcpad, caps);
  }
  gst_pad_set_formats_function (srcpad, gst_avi_demux_get_src_formats);
  gst_pad_set_event_mask_function (srcpad, gst_avi_demux_get_event_mask);
  gst_pad_set_event_function (srcpad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_type_function (srcpad, gst_avi_demux_get_src_query_types);
  gst_pad_set_query_function (srcpad, gst_avi_demux_handle_src_query);
  gst_pad_set_convert_function (srcpad, gst_avi_demux_src_convert);

  stream = &avi_demux->stream[avi_demux->num_streams];
  stream->pad = srcpad;
  gst_pad_set_element_private (srcpad, stream);
  avi_demux->num_streams++;
  avi_demux->num_v_streams++;

  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

#ifdef G_HAVE_ISO_VARARGS

#define GST_AVI_AUD_CAPS_NEW(name, mimetype, ...)			\
	(strf != NULL) ?						\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT (rate),			\
		      "channels", GST_PROPS_INT (channels),		\
		      __VA_ARGS__)					\
	:								\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT_RANGE (8000, 96000),	\
		      "channels", GST_PROPS_INT_RANGE (1, 2),		\
		      __VA_ARGS__)


#elif defined(G_HAVE_GNUC_VARARGS)

#define GST_AVI_AUD_CAPS_NEW(name, mimetype, props...)			\
	(strf != NULL) ?						\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT (rate),			\
		      "channels", GST_PROPS_INT (channels),		\
		      ##props)						\
	:								\
	GST_CAPS_NEW (name,						\
		      mimetype,						\
		      "rate",     GST_PROPS_INT_RANGE (8000, 96000),	\
		      "channels", GST_PROPS_INT_RANGE (1, 2),		\
		      ##props)
#endif

static GstCaps *
gst_avi_demux_audio_caps (guint16 codec_id,
			  gst_riff_strf_auds *strf,
			  GstAviDemux *avi_demux)
{
  GstCaps *caps = NULL;
  gchar *codecname = NULL;
  gint rate = -1, channels = -1;

  if (strf != NULL) {
    rate = GUINT32_FROM_LE (strf->rate);
    channels = GUINT16_FROM_LE (strf->channels);
  }

  switch (codec_id) {
    case GST_RIFF_WAVE_FORMAT_MPEGL3: /* mp3 */
      caps = GST_AVI_AUD_CAPS_NEW ("avi_demux_audio_src_mp3",
				   "audio/mpeg",
				     "layer", GST_PROPS_INT (3));
      codecname = g_strdup_printf("MPEG-1 layer 3 audio (0x%04x)",
                                  codec_id);
      break;

    case GST_RIFF_WAVE_FORMAT_MPEGL12: /* mp1 or mp2 */
      caps = GST_AVI_AUD_CAPS_NEW ("avi_demux_audio_src_mp12",
				   "audio/mpeg",
				     "layer", GST_PROPS_INT (2));
      codecname = g_strdup_printf("MPEG-1 layer 1/2 audio (0x%04x)",
                                  codec_id);
      break;

    case GST_RIFF_WAVE_FORMAT_PCM: /* PCM/wav */ {
      GstPropsEntry *width = NULL, *depth = NULL, *signedness = NULL;

      if (strf != NULL) {
        gint ba = GUINT16_FROM_LE (strf->blockalign);
        gint ch = GUINT16_FROM_LE (strf->channels);
        gint ws = GUINT16_FROM_LE (strf->size);

        width = gst_props_entry_new ("width",
				     GST_PROPS_INT (ba * 8 / ch));
        depth = gst_props_entry_new ("depth",
				     GST_PROPS_INT (ws));
        signedness = gst_props_entry_new ("signed",
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

      caps = GST_AVI_AUD_CAPS_NEW ("avi_demux_audio_src_pcm",
				   "audio/x-raw-int",
				     "endianness",
				       GST_PROPS_INT (G_LITTLE_ENDIAN));
      gst_props_add_entry (caps->properties, width);
      gst_props_add_entry (caps->properties, depth);
      gst_props_add_entry (caps->properties, signedness);

      codecname = g_strdup_printf("Raw PCM/WAV (0x%04x)",
                                  codec_id);
    }
      break;

    case GST_RIFF_WAVE_FORMAT_MULAW:
      if (strf != NULL && strf->size != 8) {
        g_warning ("invalid depth (%d) of mulaw audio, overwriting.",
		   strf->size);
      }
      caps = GST_AVI_AUD_CAPS_NEW ("avidemux_audio_src",
				   "audio/x-mulaw",
				     NULL);
      codecname = g_strdup_printf("A-law encoded (0x%04x)",
                                  codec_id);
      break;

    case GST_RIFF_WAVE_FORMAT_ALAW:
      if (strf != NULL && strf->size != 8) {
        g_warning ("invalid depth (%d) of alaw audio, overwriting.",
		   strf->size);
      }
      caps = GST_AVI_AUD_CAPS_NEW ("avidemux_audio_src",
				   "audio/x-alaw",
				     NULL);
      codecname = g_strdup_printf("A-law encoded (0x%04x)",
                                  codec_id);
      break;

    case GST_RIFF_WAVE_FORMAT_VORBIS1: /* ogg/vorbis mode 1 */
    case GST_RIFF_WAVE_FORMAT_VORBIS2: /* ogg/vorbis mode 2 */
    case GST_RIFF_WAVE_FORMAT_VORBIS3: /* ogg/vorbis mode 3 */
    case GST_RIFF_WAVE_FORMAT_VORBIS1PLUS: /* ogg/vorbis mode 1+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS2PLUS: /* ogg/vorbis mode 2+ */
    case GST_RIFF_WAVE_FORMAT_VORBIS3PLUS: /* ogg/vorbis mode 3+ */
      caps = GST_AVI_AUD_CAPS_NEW ("asf_demux_audio_src_vorbis",
				   "audio/x-vorbis",
				     NULL);
      codecname = g_strdup_printf("Vorbis (0x%04x)",
                                  codec_id);
      break;

    case GST_RIFF_WAVE_FORMAT_A52:
      caps = GST_AVI_AUD_CAPS_NEW ("asf_demux_audio_src_ac3",
				   "audio/x-ac3",
				     NULL);
      codecname = g_strdup_printf("AC-3 (0x%04x)",
                                  codec_id);
      break;

    default:
      g_warning ("avidemux: unkown audio format 0x%04x",
		 codec_id);
      break;
  }

  if (avi_demux != NULL && codecname != NULL) {
    /* set audio codec in streaminfo */
    GstPropsEntry *entry;
    entry = gst_props_entry_new("audiocodec",
				GST_PROPS_STRING(codecname));
    gst_props_add_entry(avi_demux->streaminfo->properties, entry);
  }
  if (codecname != NULL) {
    g_free (codecname);
  }

  return caps;
}

static void 
gst_avi_demux_strf_auds (GstAviDemux *avi_demux)
{
  gst_riff_strf_auds *strf;
  guint8 *strfdata;
  GstPad *srcpad;
  GstCaps *caps = NULL;
  avi_stream_context *stream;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;
  gchar *padname;

  got_bytes = gst_bytestream_peek_bytes (bs, &strfdata, sizeof (gst_riff_strf_auds));
  strf = (gst_riff_strf_auds *) strfdata;
  if (got_bytes != sizeof (gst_riff_strf_auds))
    return;

  GST_INFO ( "gst_avi_demux: strf tag found in context auds");
  GST_INFO ( "gst_avi_demux:  format      %d", GUINT16_FROM_LE (strf->format));
  GST_INFO ( "gst_avi_demux:  channels    %d", GUINT16_FROM_LE (strf->channels));
  GST_INFO ( "gst_avi_demux:  rate        %d", GUINT32_FROM_LE (strf->rate));
  GST_INFO ( "gst_avi_demux:  av_bps      %d", GUINT32_FROM_LE (strf->av_bps));
  GST_INFO ( "gst_avi_demux:  blockalign  %d", GUINT16_FROM_LE (strf->blockalign));
  GST_INFO ( "gst_avi_demux:  size        %d", GUINT16_FROM_LE (strf->size));

  padname = g_strdup_printf ("audio_%02d", 
			     avi_demux->num_a_streams);
  srcpad =  gst_pad_new_from_template (audiosrctempl, padname);
  g_free (padname);

  caps = gst_avi_demux_audio_caps (GUINT16_FROM_LE (strf->format),
				   strf, avi_demux);

  if (caps != NULL) {
    gst_pad_try_set_caps(srcpad, caps);
  }
  gst_pad_set_formats_function (srcpad, gst_avi_demux_get_src_formats);
  gst_pad_set_event_mask_function (srcpad, gst_avi_demux_get_event_mask);
  gst_pad_set_event_function (srcpad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_type_function (srcpad, gst_avi_demux_get_src_query_types);
  gst_pad_set_query_function (srcpad, gst_avi_demux_handle_src_query);
  gst_pad_set_convert_function (srcpad, gst_avi_demux_src_convert);

  stream = &avi_demux->stream[avi_demux->num_streams];
  stream->pad = srcpad;
  gst_pad_set_element_private (srcpad, stream);
  avi_demux->num_streams++;
  avi_demux->num_a_streams++;

  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static GstCaps *
gst_avi_demux_iavs_caps (void)
{
  return GST_CAPS_NEW ("avi_type_dv", 
                       "video/x-dv", 
                         "systemstream", GST_PROPS_BOOLEAN (TRUE));
}

static void 
gst_avi_demux_strf_iavs (GstAviDemux *avi_demux)
{
  gst_riff_strf_iavs *strf;
  guint8 *strfdata;
  GstPad *srcpad;
  GstCaps *caps = NULL;
  avi_stream_context *stream;
  GstByteStream  *bs = avi_demux->bs;
  guint32 got_bytes;
  gchar *padname;
  GstPropsEntry *entry;

  got_bytes = gst_bytestream_peek_bytes (bs, &strfdata, sizeof (gst_riff_strf_iavs));
  strf = (gst_riff_strf_iavs *) strfdata;
  if (got_bytes != sizeof (gst_riff_strf_iavs))
    return;

  GST_INFO ( "gst_avi_demux: strf tag found in context iavs");
  GST_INFO ( "gst_avi_demux:  DVAAuxSrc   %08x", GUINT32_FROM_LE (strf->DVAAuxSrc));
  GST_INFO ( "gst_avi_demux:  DVAAuxCtl   %08x", GUINT32_FROM_LE (strf->DVAAuxCtl));
  GST_INFO ( "gst_avi_demux:  DVAAuxSrc1  %08x", GUINT32_FROM_LE (strf->DVAAuxSrc1));
  GST_INFO ( "gst_avi_demux:  DVAAuxCtl1  %08x", GUINT32_FROM_LE (strf->DVAAuxCtl1));
  GST_INFO ( "gst_avi_demux:  DVVAuxSrc   %08x", GUINT32_FROM_LE (strf->DVVAuxSrc));
  GST_INFO ( "gst_avi_demux:  DVVAuxCtl   %08x", GUINT32_FROM_LE (strf->DVVAuxCtl));
  GST_INFO ( "gst_avi_demux:  DVReserved1 %08x", GUINT32_FROM_LE (strf->DVReserved1));
  GST_INFO ( "gst_avi_demux:  DVReserved2 %08x", GUINT32_FROM_LE (strf->DVReserved2));

  padname = g_strdup_printf ("video_%02d", 
			     avi_demux->num_v_streams);
  srcpad =  gst_pad_new_from_template (videosrctempl, padname);
  g_free (padname);

  caps = gst_avi_demux_iavs_caps ();
  entry = gst_props_entry_new("videocodec",
                              GST_PROPS_STRING("Digital Video type 1"));
  gst_props_add_entry(avi_demux->streaminfo->properties, entry);

  if (caps != NULL) {
    gst_pad_try_set_caps(srcpad, caps);
  }
  gst_pad_set_formats_function (srcpad, gst_avi_demux_get_src_formats);
  gst_pad_set_event_mask_function (srcpad, gst_avi_demux_get_event_mask);
  gst_pad_set_event_function (srcpad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_type_function (srcpad, gst_avi_demux_get_src_query_types);
  gst_pad_set_query_function (srcpad, gst_avi_demux_handle_src_query);
  gst_pad_set_convert_function (srcpad, gst_avi_demux_src_convert);

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
  GST_DEBUG ("%s: %05d %d %08llx %05d %14" G_GINT64_FORMAT " %08x %08x (%d) %08x", 
		  prefix, entry->index_nr, entry->stream_nr, 
		  (unsigned long long)entry->bytes_before, 
		  entry->frames_before, entry->ts, entry->flags, entry->offset, 
		  entry->offset, entry->size);
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
  guint32 id;

  if (!gst_bytestream_seek (avi_demux->bs, filepos + offset, GST_SEEK_METHOD_SET)) {
    GST_INFO ( "avidemux: could not seek to index");
    return;
  }
  do {
    guint32 remaining;
    GstEvent *event;
  
    got_bytes = gst_bytestream_read (avi_demux->bs, &buf, 8);
    if (got_bytes == 8)
      break;

    gst_bytestream_get_status (avi_demux->bs, &remaining, &event);
    gst_event_unref (event);
  } while (TRUE);

  if (GST_BUFFER_OFFSET (buf) != filepos + offset || GST_BUFFER_SIZE (buf) != 8) {
    GST_INFO ( "avidemux: could not get index, got %" G_GINT64_FORMAT " %d, expected %ld", 
		    GST_BUFFER_OFFSET (buf), GST_BUFFER_SIZE (buf), filepos + offset);
    goto end;
  }

  id = GUINT32_FROM_LE (*(guint32 *)GST_BUFFER_DATA (buf));

  if (id != GST_RIFF_TAG_idx1) {
    GST_INFO ( "avidemux: no index found");
    goto end;
  }

  index_size = GUINT32_FROM_LE(*(guint32 *)(GST_BUFFER_DATA (buf) + 4));
  gst_buffer_unref (buf);

  gst_bytestream_size_hint (avi_demux->bs, index_size);

  got_bytes = gst_bytestream_read (avi_demux->bs, &buf, index_size);
  if (got_bytes < index_size) {
    GST_INFO ( "avidemux: error reading index");
    goto end;
  }

  avi_demux->index_size = index_size/sizeof(gst_riff_index_entry);
  GST_INFO ( "avidemux: index size %lu", avi_demux->index_size);

  avi_demux->index_entries = g_malloc (avi_demux->index_size * sizeof (gst_avi_index_entry));

  entry = (gst_riff_index_entry *) GST_BUFFER_DATA (buf);

  for (i = 0; i < avi_demux->index_size; i++) {
    avi_stream_context *stream;
    gint stream_nr;
    gst_avi_index_entry *target = &avi_demux->index_entries[i];
    GstFormat format;
    guint32 id;

    id = GUINT32_FROM_LE (entry[i].id);
    stream_nr = CHUNKID_TO_STREAMNR (id);
    if (stream_nr > avi_demux->num_streams || stream_nr < 0) {
      avi_demux->index_entries[i].stream_nr = -1;
      continue;
    }

    target->stream_nr = stream_nr;
    stream = &avi_demux->stream[stream_nr];

    target->index_nr = i;
    target->flags    = GUINT32_FROM_LE (entry[i].flags);
    target->size     = GUINT32_FROM_LE (entry[i].size);
    target->offset   = GUINT32_FROM_LE (entry[i].offset);

    /* figure out if the index is 0 based or relative to the MOVI start */
    if (i == 0) {
      if (target->offset < filepos)
	avi_demux->index_offset = filepos - 4;
      else
	avi_demux->index_offset = 0;
    }

    target->bytes_before = stream->total_bytes;
    target->frames_before = stream->total_frames;

    format = GST_FORMAT_TIME;
    if (stream->strh.type == GST_RIFF_FCC_auds) {
      /* all audio frames are keyframes */
      target->flags |= GST_RIFF_IF_KEYFRAME;
    }
      
    /* constant rate stream */
    if (stream->strh.samplesize && stream->strh.type == GST_RIFF_FCC_auds) {
      gst_pad_convert (stream->pad, GST_FORMAT_BYTES, stream->total_bytes,
		                 &format, &target->ts);
    }
    /* VBR stream */
    else {
      gst_pad_convert (stream->pad, GST_FORMAT_DEFAULT, stream->total_frames,
		                 &format, &target->ts);
    }
    gst_avi_debug_entry ("index", target);

    stream->total_bytes += target->size;
    stream->total_frames++;
  }
  for (i = 0; i < avi_demux->num_streams; i++) {
    avi_stream_context *stream;

    stream = &avi_demux->stream[i];
    GST_DEBUG ("stream %i: %d frames, %" G_GINT64_FORMAT " bytes", 
	       i, stream->total_frames, stream->total_bytes);
  }
  gst_buffer_unref (buf);

end:
  GST_DEBUG ("index offset at %08lx", filepos);

  if (!gst_bytestream_seek (avi_demux->bs, filepos, GST_SEEK_METHOD_SET)) {
    GST_INFO ( "avidemux: could not seek back to movi");
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

static const GstFormat*
gst_avi_demux_get_src_formats (GstPad *pad) 
{
  avi_stream_context *stream = gst_pad_get_element_private (pad);

  static const GstFormat src_a_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0
  };
  static const GstFormat src_v_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    0
  };

  return (stream->strh.type == GST_RIFF_FCC_auds ? src_a_formats : src_v_formats);
}

static gboolean
gst_avi_demux_src_convert (GstPad *pad, GstFormat src_format, gint64 src_value,
	                   GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  avi_stream_context *stream = gst_pad_get_element_private (pad);

  if (stream->strh.type != GST_RIFF_FCC_auds && 
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
	case GST_FORMAT_BYTES:
          *dest_value = src_value * stream->strh.rate / (stream->strh.scale * GST_SECOND);
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * stream->strh.rate / (stream->strh.scale * GST_SECOND);
          break;
	default:
	  res = FALSE;
	  break;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
	case GST_FORMAT_TIME:
          *dest_value = ((gfloat)src_value) * GST_SECOND / stream->strh.rate;
	  break;
	default:
	  res = FALSE;
	  break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
	case GST_FORMAT_TIME:
          *dest_value = ((((gfloat)src_value) * stream->strh.scale)  / stream->strh.rate) * GST_SECOND;
	  break;
	default:
	  res = FALSE;
	  break;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static const GstQueryType*
gst_avi_demux_get_src_query_types (GstPad *pad) 
{
  static const GstQueryType src_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return src_types;
}

static gboolean
gst_avi_demux_handle_src_query (GstPad *pad, GstQueryType type, 
				GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  //GstAviDemux *avi_demux = GST_AVI_DEMUX (gst_pad_get_parent (pad));
  avi_stream_context *stream = gst_pad_get_element_private (pad);

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = (((gfloat)stream->strh.scale) * stream->strh.length / stream->strh.rate) * GST_SECOND;
	  break;
        case GST_FORMAT_BYTES:
          if (stream->strh.type == GST_RIFF_FCC_auds) {
            *value = stream->total_bytes;
	  }
	  else
	    res = FALSE;
	  break;
        case GST_FORMAT_DEFAULT:
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
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          if (stream->strh.samplesize && stream->strh.type == GST_RIFF_FCC_auds) {
            //*value = (((gfloat)stream->current_byte) * stream->strh.scale / stream->strh.rate) * GST_SECOND;
            *value = ((gfloat)stream->current_byte) * GST_SECOND / stream->strh.rate;
	  }
	  else {
            *value = (((gfloat)stream->current_frame) * stream->strh.scale / stream->strh.rate) * GST_SECOND;
	  }
	  break;
        case GST_FORMAT_BYTES:
          *value = stream->current_byte;
	  break;
        case GST_FORMAT_DEFAULT:
          if (stream->strh.samplesize && stream->strh.type == GST_RIFF_FCC_auds) 
            *value = stream->current_byte * stream->strh.samplesize;
	  else 
            *value = stream->current_frame;
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
  guint32 min_index = G_MAXUINT;
  avi_stream_context *stream;
  gst_avi_index_entry *entry;

  for (i = 0; i < avi_demux->num_streams; i++) {
    stream = &avi_demux->stream[i];

    GST_DEBUG ("finding %d for time %" G_GINT64_FORMAT, i, time);

    entry = gst_avi_demux_index_entry_for_time (avi_demux, stream->num, time, GST_RIFF_IF_KEYFRAME);
    if (entry) {
      gst_avi_debug_entry ("sync entry", entry);

      min_index = MIN (entry->index_nr, min_index);
    }
  }
  GST_DEBUG ("first index at %d", min_index);
  
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

    stream->current_byte = next_entry->bytes_before;
    stream->current_frame = next_entry->frames_before;
    stream->skip = entry->frames_before - next_entry->frames_before;

    GST_DEBUG ("%d skip %d", stream->num, stream->skip);
  }
  GST_DEBUG ("final index at %d", min_index);

  return min_index;
}

static gboolean
gst_avi_demux_send_event (GstElement *element, GstEvent *event)
{
  const GList *pads;

  pads = gst_element_get_pad_list (element);

  while (pads) { 
    GstPad *pad = GST_PAD (pads->data);

    if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
      /* we ref the event here as we might have to try again if the event
       * failed on this pad */
      gst_event_ref (event);
      if (gst_avi_demux_handle_src_event (pad, event)) {
	gst_event_unref (event);
	return TRUE;
      }
    }
    
    pads = g_list_next (pads);
  }
  
  gst_event_unref (event);
  return FALSE;
}

static const GstEventMask*
gst_avi_demux_get_event_mask (GstPad *pad)
{
  static const GstEventMask masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT },
    { GST_EVENT_SEEK_SEGMENT, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT },
    { 0, }
  };

  return masks;
}
	
static gboolean
gst_avi_demux_handle_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstAviDemux *avi_demux = GST_AVI_DEMUX (gst_pad_get_parent (pad));
  avi_stream_context *stream;
  
  stream = gst_pad_get_element_private (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK_SEGMENT:
      stream->end_pos = GST_EVENT_SEEK_ENDOFFSET (event);
    case GST_EVENT_SEEK:
      GST_DEBUG ("seek format %d, %08x", GST_EVENT_SEEK_FORMAT (event), stream->strh.type);
      switch (GST_EVENT_SEEK_FORMAT (event)) {
	case GST_FORMAT_BYTES:
	case GST_FORMAT_DEFAULT:
	  break;
	case GST_FORMAT_TIME:
        {
	  gst_avi_index_entry *seek_entry, *entry;
	  gint64 desired_offset = GST_EVENT_SEEK_OFFSET (event);
	  guint32 flags;
          guint64 min_index;
	  

	  /* no seek on audio yet */
	  if (stream->strh.type == GST_RIFF_FCC_auds) {
	    res = FALSE;
	    goto done;
	  }
          GST_DEBUG ("seeking to %" G_GINT64_FORMAT, desired_offset);

          flags = GST_RIFF_IF_KEYFRAME;

          entry = gst_avi_demux_index_entry_for_time (avi_demux, stream->num, desired_offset, GST_RIFF_IF_KEYFRAME);
	  if (entry) {
            desired_offset = entry->ts;
	    min_index = gst_avi_demux_sync_streams (avi_demux, desired_offset);
            seek_entry = &avi_demux->index_entries[min_index];
	    
            gst_avi_debug_entry ("syncing to entry", seek_entry);
	    
	    avi_demux->seek_offset = seek_entry->offset + avi_demux->index_offset;
            avi_demux->seek_pending = TRUE;
	    avi_demux->last_seek = seek_entry->ts;
	  }
	  else {
            GST_DEBUG ("no index entry found for time %" G_GINT64_FORMAT, desired_offset);
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

done:
  gst_event_unref (event);

  return res;
}

static gboolean
gst_avi_demux_handle_sink_event (GstAviDemux *avi_demux)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;
  gboolean res = TRUE;
  
  gst_bytestream_get_status (avi_demux->bs, &remaining, &event);

  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("avidemux: event %p %d", event, type); 

  switch (type) {
    case GST_EVENT_EOS:
      gst_bytestream_flush (avi_demux->bs, remaining);
      gst_pad_event_default (avi_demux->sinkpad, event);
      res = FALSE;
      goto done;
    case GST_EVENT_FLUSH:
      g_warning ("flush event");
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint i;
      GstEvent *discont;

      for (i = 0; i < avi_demux->num_streams; i++) {
        avi_stream_context *stream = &avi_demux->stream[i];

	if (GST_PAD_IS_USABLE (stream->pad)) {
	  GST_DEBUG ("sending discont on %d %" G_GINT64_FORMAT " + %" G_GINT64_FORMAT " = %" G_GINT64_FORMAT, 
			i, avi_demux->last_seek, stream->delay, avi_demux->last_seek + stream->delay);

          discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
			avi_demux->last_seek + stream->delay , NULL);

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

done:

  return res;
}


static void
gst_avi_demux_loop (GstElement *element)
{
  GstAviDemux *avi_demux;
  gst_riff_riff chunk;
  guint32 flush = 0;
  guint32 got_bytes;
  GstByteStream *bs;
  guint64 pos;

  avi_demux = GST_AVI_DEMUX (element);

  bs = avi_demux->bs;

  if (avi_demux->seek_pending) {
    GST_DEBUG ("avidemux: seek pending to %" G_GINT64_FORMAT " %08llx", 
		  avi_demux->seek_offset, (unsigned long long)avi_demux->seek_offset);

    if (!gst_bytestream_seek (avi_demux->bs, 
			      avi_demux->seek_offset, 
			      GST_SEEK_METHOD_SET)) 
    {
      GST_INFO ( "avidemux: could not seek");
    }
    avi_demux->seek_pending = FALSE;
  }

  pos = gst_bytestream_tell (bs);
  do {
    gst_riff_riff *temp_chunk;
    guint8 *tempdata;
    guint32 skipsize;

    /* read first two dwords to get chunktype and size */
    while (TRUE) {
      got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_chunk));
      temp_chunk = (gst_riff_riff *) tempdata;
      if (got_bytes < sizeof (gst_riff_chunk)) {
        if (!gst_avi_demux_handle_sink_event (avi_demux))
          return;
      }
      else break;
    }

    chunk.id = GUINT32_FROM_LE (temp_chunk->id);
    chunk.size = GUINT32_FROM_LE (temp_chunk->size);

    switch (chunk.id) {
      case GST_RIFF_TAG_RIFF:
      case GST_RIFF_TAG_LIST:
        /* read complete list chunk */
        while (TRUE) {
          got_bytes = gst_bytestream_peek_bytes (bs, &tempdata, sizeof (gst_riff_list));
	  temp_chunk = (gst_riff_riff *) tempdata;
          if (got_bytes < sizeof (gst_riff_list)) {
            if (!gst_avi_demux_handle_sink_event (avi_demux))
              return;
          }
          else break;
        }
        chunk.type = GUINT32_FROM_LE (temp_chunk->type);
        skipsize = sizeof (gst_riff_list);
        break;
      default:
        skipsize = sizeof (gst_riff_chunk);
        break;
    }
    gst_bytestream_flush_fast (bs, skipsize);
  } 
  while (FALSE);

  /* need to flush an even number of bytes at the end */
  flush = (chunk.size + 1) & ~1;

  switch (avi_demux->state) {
    case GST_AVI_DEMUX_START:
      if (chunk.id != GST_RIFF_TAG_RIFF && 
          chunk.type != GST_RIFF_RIFF_AVI) {
        gst_element_error (element, "This doesn't appear to be an AVI file %08x %08x", chunk.id, chunk.type);
	return;
      }
      avi_demux->state = GST_AVI_DEMUX_HEADER;
      /* we are not going to flush lists */
      flush = 0;
      break;
    case GST_AVI_DEMUX_HEADER:
      GST_DEBUG ("riff tag: %4.4s %08x", (gchar *)&chunk.id, chunk.size);
      switch (chunk.id) {
	case GST_RIFF_TAG_LIST:
          GST_DEBUG ("list type: %4.4s", (gchar *)&chunk.type);
          switch (chunk.type) {
            case GST_RIFF_LIST_movi:
	    {
	      guint64 filepos;

	      filepos = gst_bytestream_tell (bs);

              gst_avi_demux_parse_index (avi_demux, filepos , chunk.size - 4);
	      
	      if (avi_demux->avih.bufsize) {
	        gst_bytestream_size_hint (avi_demux->bs, avi_demux->avih.bufsize);
	      }

              avi_demux->state = GST_AVI_DEMUX_MOVI;
	      /* and tell the bastards that we have stream info too */
	      gst_props_debug(avi_demux->streaminfo->properties);
              g_object_notify(G_OBJECT(avi_demux), "streaminfo");
	      break;
	    }
            case GST_RIFF_LIST_INFO:
              gst_avi_demux_metadata (avi_demux, chunk.size);
              break;
	    default:
	      break;
	  }
          flush = 0;
	  break;
        case GST_RIFF_TAG_avih:
          gst_avi_demux_avih (avi_demux);
          break;
        case GST_RIFF_TAG_strh:
          gst_avi_demux_strh (avi_demux);
          break;
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
              GST_INFO ( "gst_avi_demux_chain: strh type %s not supported", 
			  gst_riff_id_to_fourcc (avi_demux->fcc_type));
	      break;
          }
          break;
        case GST_RIFF_TAG_strn:
	  gst_avi_demux_strn (avi_demux, chunk.size);
          break;
        case GST_RIFF_TAG_dmlh:
          gst_avi_demux_dmlh (avi_demux);
          break;
        case GST_RIFF_TAG_JUNK:
        case GST_RIFF_ISFT:
          break;
	default:
          GST_DEBUG ("  *****  unknown chunkid %08x", chunk.id);
	  break;
      }
      break;
    case GST_AVI_DEMUX_MOVI:
      switch (chunk.id) {
        case GST_RIFF_00dc:
        case GST_RIFF_00db:
        case GST_RIFF_00__:
        case GST_RIFF_01wb:
        {
          gint stream_id;
          avi_stream_context *stream;
          gint64 next_ts;
          GstFormat format;

          stream_id = CHUNKID_TO_STREAMNR (chunk.id);
		   
          stream = &avi_demux->stream[stream_id];

          GST_LOG_OBJECT (avi_demux, "gst_avi_demux_chain: tag found %08x size %08x stream_id %d",
		    chunk.id, chunk.size, stream_id);

          format = GST_FORMAT_TIME;
          gst_pad_query (stream->pad, GST_QUERY_POSITION, &format, &next_ts);

          if (stream->strh.init_frames == stream->current_frame && stream->delay == 0)
            stream->delay = next_ts;

          stream->current_frame++;
          stream->current_byte += chunk.size;

          if (stream->skip) {
            stream->skip--;
          }
          else {
            if (GST_PAD_IS_USABLE (stream->pad)) {
              if (next_ts >= stream->end_pos) {
                gst_pad_push (stream->pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
	        GST_DEBUG ("end stream %d: %" G_GINT64_FORMAT " %d %" G_GINT64_FORMAT, 
		           stream_id, next_ts, stream->current_frame - 1,
			   stream->end_pos);
	      }
	      else {
	        GstBuffer *buf;
                guint32   got_bytes;

	        if (chunk.size) {
	          GstClockTime dur_ts;
                  got_bytes = gst_bytestream_peek (avi_demux->bs, &buf, chunk.size);
		  
                  GST_BUFFER_TIMESTAMP (buf) = next_ts;

                  gst_pad_query (stream->pad, GST_QUERY_POSITION, &format, &dur_ts);
                  GST_BUFFER_DURATION (buf) = dur_ts - next_ts;

                  if (stream->need_flush) {
                    /* FIXME, do some flush event here */
                    stream->need_flush = FALSE;
                  }
	          GST_LOG_OBJECT (avi_demux, "send stream %d: %" 
			     G_GINT64_FORMAT " %d %" G_GINT64_FORMAT " %08x", 
			     stream_id, next_ts, stream->current_frame - 1,
			     stream->delay, chunk.size);

                  gst_pad_push(stream->pad, GST_DATA (buf));
	        }
              }
            }
          }
          break;
	}
	default:
          GST_DEBUG ("  *****  unknown chunkid %08x", chunk.id);
          break;
      }
      break;
  }

  while (flush) {
    gboolean res;
    
    res = gst_bytestream_flush (avi_demux->bs, flush);
    if (!res) {
      guint32 remaining;
      GstEvent *event;

      gst_bytestream_get_status (avi_demux->bs, &remaining, &event);
      gst_event_unref (event);
    }
    else
      break;
  }
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
      avi_demux->last_seek = 0;
      avi_demux->state = GST_AVI_DEMUX_START;
      avi_demux->num_streams = 0;
      avi_demux->num_v_streams = 0;
      avi_demux->num_a_streams = 0;
      avi_demux->index_entries = NULL;
      avi_demux->index_size = 0;
      avi_demux->seek_pending = 0;
      avi_demux->metadata = NULL;
      gst_avi_demux_streaminfo(avi_demux);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (avi_demux->bs);
      gst_caps_replace (&avi_demux->metadata, NULL);
      gst_caps_replace (&avi_demux->streaminfo, NULL);
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
    case ARG_METADATA:
      g_value_set_boxed(value, src->metadata);
      break;
    case ARG_STREAMINFO:
      g_value_set_boxed(value, src->streaminfo);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;
  if (!gst_library_load ("gstriff"))
    return FALSE;
  if (!gst_library_load("gstvideo"))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (avidemux_debug, "avidemux", 0, "Demuxer for AVI video");
  
  if (!gst_element_register (plugin, "avidemux", GST_RANK_PRIMARY,
        GST_TYPE_AVI_DEMUX)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "avimux", GST_RANK_PRIMARY,
        GST_TYPE_AVIMUX)) {
    return FALSE;
  }

  return TRUE;
}


GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "avimux",
  "AVI stream handling",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)

