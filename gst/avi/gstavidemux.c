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
  ".avi parser",
  "Parser/Video",
  "Parse a .avi file into audio and video",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 1999",
};

static GstCaps* avi_typefind (GstBuffer *buf, gpointer private);

/* typefactory for 'avi' */
static GstTypeDefinition avidefinition = {
  "avidemux_video/avi",
  "video/avi",
  ".avi",
  avi_typefind,
};

/* AviDemux signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BITRATE,
  ARG_MEDIA_TIME,
  ARG_CURRENT_TIME,
  ARG_FRAME_RATE,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "avidemux_sink",
     "video/avi",
      "format",    GST_PROPS_STRING ("AVI")
  )
)

GST_PADTEMPLATE_FACTORY (src_video_templ,
  "video_[00-32]",
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

GST_PADTEMPLATE_FACTORY (src_audio_templ,
  "audio_[00-32]",
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
  g_object_class_install_property (G_OBJECT_CLASS(klass), ARG_MEDIA_TIME,
    g_param_spec_long ("media_time","media_time","media_time",
                       G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS(klass), ARG_CURRENT_TIME,
    g_param_spec_long ("current_time","current_time","current_time",
                       G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE)); /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS(klass), ARG_FRAME_RATE,
    g_param_spec_int ("frame-rate","frame rate","Current (non-averaged) frame rate",
                      0, G_MAXINT, 0, G_PARAM_READABLE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  gobject_class->get_property = gst_avi_demux_get_property;
  
  gstelement_class->change_state = gst_avi_demux_change_state;
}

static void 
gst_avi_demux_init (GstAviDemux *avi_demux) 
{
  guint i;

  GST_FLAG_SET (avi_demux, GST_ELEMENT_EVENT_AWARE);
				
  avi_demux->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (avi_demux), avi_demux->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (avi_demux), gst_avi_demux_loop);

  avi_demux->state = GST_AVI_DEMUX_UNKNOWN;
  avi_demux->num_audio_pads = 0;
  avi_demux->num_video_pads = 0;
  /*avi_demux->next_time = 500000; */
  avi_demux->next_time = 0;
  avi_demux->init_audio = 0;
  avi_demux->flags = 0;
  avi_demux->index_entries = NULL;
  avi_demux->index_size = 0;
  avi_demux->resync_offset = 0;

  /*GST_FLAG_SET( GST_OBJECT (avi_demux), GST_ELEMENT_NO_SEEK); */

  for(i=0; i<GST_AVI_DEMUX_MAX_AUDIO_PADS; i++) 
    avi_demux->audio_pad[i] = NULL;

  for(i=0; i<GST_AVI_DEMUX_MAX_VIDEO_PADS; i++) 
    avi_demux->video_pad[i] = NULL;

}

static GstCaps*
avi_typefind (GstBuffer *buf,
              gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);
  GstCaps *new;

  GST_DEBUG (0,"avi_demux: typefind\n");

  if (GUINT32_FROM_LE (((guint32 *)data)[0]) != GST_RIFF_TAG_RIFF)
    return NULL;
  if (GUINT32_FROM_LE (((guint32 *)data)[2]) != GST_RIFF_RIFF_AVI)
    return NULL;

  new = GST_CAPS_NEW ("avi_typefind",
		      "video/avi", 
		        "format", GST_PROPS_STRING ("AVI"));

  return new;
}

static gboolean
gst_avi_demux_avih (GstAviDemux *avi_demux)
{
  gst_riff_avih *avih;
  GstByteStream *bs = avi_demux->bs;

  avih = (gst_riff_avih *) gst_bytestream_peek_bytes (bs, sizeof (gst_riff_avih));
  if (avih) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: avih tag found");
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  us_frame    %d", GUINT32_FROM_LE (avih->us_frame));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  max_bps     %d", GUINT32_FROM_LE (avih->max_bps));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  pad_gran    %d", GUINT32_FROM_LE (avih->pad_gran));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  flags       0x%08x", GUINT32_FROM_LE (avih->flags));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  tot_frames  %d", GUINT32_FROM_LE (avih->tot_frames));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  init_frames %d", GUINT32_FROM_LE (avih->init_frames));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  streams     %d", GUINT32_FROM_LE (avih->streams));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  bufsize     %d", GUINT32_FROM_LE (avih->bufsize));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  width       %d", GUINT32_FROM_LE (avih->width));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  height      %d", GUINT32_FROM_LE (avih->height));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  scale       %d", GUINT32_FROM_LE (avih->scale));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  rate        %d", GUINT32_FROM_LE (avih->rate));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  start       %d", GUINT32_FROM_LE (avih->start));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  length      %d", GUINT32_FROM_LE (avih->length));

    avi_demux->time_interval = GUINT32_FROM_LE (avih->us_frame);
    avi_demux->tot_frames = GUINT32_FROM_LE (avih->tot_frames);
    avi_demux->flags = GUINT32_FROM_LE (avih->flags);

    return TRUE;
  }
  return FALSE;
}

static gboolean 
gst_avi_demux_strh (GstAviDemux *avi_demux)
{
  gst_riff_strh *strh;
  GstByteStream *bs = avi_demux->bs;

  strh = (gst_riff_strh *) gst_bytestream_peek_bytes (bs, sizeof (gst_riff_strh));
  if (strh) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: strh tag found");
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  type        0x%08x (%s)", 
  		  GUINT32_FROM_LE (strh->type), gst_riff_id_to_fourcc (strh->type));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  fcc_handler 0x%08x (%s)", 
		  GUINT32_FROM_LE (strh->fcc_handler), gst_riff_id_to_fourcc (strh->fcc_handler));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  flags       0x%08x", GUINT32_FROM_LE (strh->flags));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  priority    %d", GUINT32_FROM_LE (strh->priority));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  init_frames %d", GUINT32_FROM_LE (strh->init_frames));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  scale       %d", GUINT32_FROM_LE (strh->scale));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  rate        %d", GUINT32_FROM_LE (strh->rate));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  start       %d", GUINT32_FROM_LE (strh->start));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  length      %d", GUINT32_FROM_LE (strh->length));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  bufsize     %d", GUINT32_FROM_LE (strh->bufsize));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  quality     %d", GUINT32_FROM_LE (strh->quality));
    GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  samplesize  %d", GUINT32_FROM_LE (strh->samplesize));

    avi_demux->fcc_type = GUINT32_FROM_LE (strh->type);
    if (strh->type == GST_RIFF_FCC_auds) {
      guint32 scale;
      
      scale = GUINT32_FROM_LE (strh->scale);
      avi_demux->init_audio = GUINT32_FROM_LE (strh->init_frames);
      if (!scale)
        scale = 1;
      avi_demux->audio_rate = GUINT32_FROM_LE (strh->rate) / scale;
    }
    else if (strh->type == GST_RIFF_FCC_vids) {
      guint32 scale;
      
      scale = GUINT32_FROM_LE (strh->scale);
      if (!scale)
        scale = 1;
      avi_demux->frame_rate = (gint) GUINT32_FROM_LE (strh->rate) / scale;
      g_object_notify (G_OBJECT (avi_demux), "frame-rate");
    }

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

  strf = (gst_riff_strf_vids *) gst_bytestream_peek_bytes (bs, sizeof (gst_riff_strf_vids));

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
		  GST_PADTEMPLATE_GET (src_video_templ), g_strdup_printf ("video_%02d", 
			  avi_demux->num_video_pads));

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

  if (newcaps) capslist = gst_caps_append(capslist, newcaps);

  gst_pad_try_set_caps(srcpad, capslist);

  avi_demux->video_pad[avi_demux->num_video_pads++] = srcpad;
  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static void 
gst_avi_demux_strf_auds (GstAviDemux *avi_demux)
{
  gst_riff_strf_auds *strf;
  GstPad *srcpad;
  GstByteStream *bs = avi_demux->bs;
  GstCaps *newcaps = NULL, *capslist = NULL;

  strf = (gst_riff_strf_auds *) gst_bytestream_peek_bytes (bs, sizeof (gst_riff_strf_auds));

  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux: strf tag found in context auds");
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  format      %d", GUINT16_FROM_LE (strf->format));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  channels    %d", GUINT16_FROM_LE (strf->channels));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  rate        %d", GUINT32_FROM_LE (strf->rate));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  av_bps      %d", GUINT32_FROM_LE (strf->av_bps));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  blockalign  %d", GUINT16_FROM_LE (strf->blockalign));
  GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux:  size        %d", GUINT16_FROM_LE (strf->size));

  srcpad =  gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_audio_templ), g_strdup_printf ("audio_%02d", 
			  avi_demux->num_audio_pads));

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

  avi_demux->audio_pad[avi_demux->num_audio_pads++] = srcpad;
  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static void 
gst_avi_demux_strf_iavs (GstAviDemux *avi_demux)
{
  gst_riff_strf_iavs *strf;
  GstPad *srcpad;
  GstByteStream *bs = avi_demux->bs;
  GstCaps *newcaps = NULL, *capslist = NULL;

  strf = (gst_riff_strf_iavs *) gst_bytestream_peek_bytes (bs, sizeof (gst_riff_strf_iavs));

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
		  GST_PADTEMPLATE_GET (src_video_templ), g_strdup_printf ("video_%02d", 
			  avi_demux->num_video_pads));

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

  avi_demux->video_pad[avi_demux->num_video_pads++] = srcpad;
  gst_element_add_pad (GST_ELEMENT (avi_demux), srcpad);
}

static void
gst_avidemux_parse_index (GstAviDemux *avi_demux,
		          gulong filepos, gulong offset)
{
  GstBuffer *buf;
  gulong index_size;

  if (!gst_bytestream_seek (avi_demux->bs, GST_SEEK_BYTEOFFSET_SET, filepos + offset)) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not seek to index");
    return;
  }
  buf = gst_bytestream_read (avi_demux->bs, 8);
  while (!buf) {
    guint32 remaining;
    GstEvent *event;
  
    gst_bytestream_get_status (avi_demux->bs, &remaining, &event);

    buf = gst_bytestream_read (avi_demux->bs, 8);
  }
		  
  if (GST_BUFFER_OFFSET (buf) != filepos + offset || GST_BUFFER_SIZE (buf) != 8) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not get index");
    return;
  }

  if (gst_riff_fourcc_to_id (GST_BUFFER_DATA (buf)) != GST_RIFF_TAG_idx1) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: no index found");
    return;
  }

  index_size = GUINT32_FROM_LE(*(guint32 *)(GST_BUFFER_DATA (buf) + 4));
  gst_buffer_unref (buf);

  buf = gst_bytestream_read (avi_demux->bs, index_size);

  avi_demux->index_size = index_size/sizeof(gst_riff_index_entry);

  GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: index size %lu", avi_demux->index_size);

  avi_demux->index_entries = g_malloc (GST_BUFFER_SIZE (buf));
  memcpy (avi_demux->index_entries, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  gst_buffer_unref (buf);

  if (!gst_bytestream_seek (avi_demux->bs, GST_SEEK_BYTEOFFSET_SET, filepos)) {
    GST_INFO (GST_CAT_PLUGIN_INFO, "avidemux: could not seek back to movi");
    return;
  }
}

static gboolean
gst_avidemux_handle_event (GstAviDemux *avi_demux)
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
      g_warning ("discont event\n");
      break;
    default:
      g_warning ("unhandled event %d\n", type);
      break;
  }

  return TRUE;
}

static inline gboolean 
gst_avidemux_read_chunk (GstAviDemux *avi_demux, guint32 *id, guint32 *size)
{
  gst_riff_chunk *chunk;
  GstByteStream *bs = avi_demux->bs;

  do {
    chunk = (gst_riff_chunk *) gst_bytestream_peek_bytes (bs, sizeof (gst_riff_chunk));
    if (chunk) {
      *id =   GUINT32_FROM_LE (chunk->id);
      *size = GUINT32_FROM_LE (chunk->size);

      gst_bytestream_flush (bs, sizeof (gst_riff_chunk));

      return TRUE;
    }
  } while (gst_avidemux_handle_event (avi_demux));

  return TRUE;
}

static gboolean
gst_avidemux_process_chunk (GstAviDemux *avi_demux, guint64 *filepos,
			     guint32 desired_tag,
			     gint rec_depth, guint32 *chunksize)
{
  guint32 chunkid;	
  GstByteStream *bs = avi_demux->bs;

  if (!gst_avidemux_read_chunk (avi_demux, &chunkid, chunksize)) {
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

      formtype = gst_bytestream_peek_bytes (bs, sizeof (guint32));
      if (!formtype)
	return FALSE;

      switch (GUINT32_FROM_LE (*((guint32*)formtype))) {
	case GST_RIFF_LIST_movi:
	  gst_avidemux_parse_index (avi_demux, *filepos, *chunksize);
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
	if (!gst_avidemux_process_chunk (avi_demux, filepos, 0,
			   rec_depth + 1, &subchunksize))
	  return FALSE;

	subchunksize = ((subchunksize + 1) & ~1);

	datashowed += (sizeof (guint32) + sizeof (guint32) + subchunksize);
        GST_INFO (GST_CAT_PLUGIN_INFO, "process chunk done filepos %08llx, subchunksize %08x", 
			*filepos, subchunksize);
      }
      if (datashowed != *chunksize) {
	g_warning ("error parsing AVI");
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
          GST_INFO (GST_CAT_PLUGIN_INFO, "gst_avi_demux_chain: strh type %s not supported", gst_riff_id_to_fourcc (avi_demux->fcc_type));
	  break;
      }
      break;
    case GST_RIFF_00dc:
    case GST_RIFF_00db:
    case GST_RIFF_00__:
    {
      GST_DEBUG (0,"gst_avi_demux_chain: tag found %08x size %08x\n",
		    chunkid, *chunksize);

      if (GST_PAD_IS_CONNECTED (avi_demux->video_pad[0])) {
	GstBuffer *buf;

	if (*chunksize) {
          buf = gst_bytestream_peek (bs, *chunksize);

          GST_BUFFER_TIMESTAMP (buf) = avi_demux->next_time;

          avi_demux->next_time += avi_demux->time_interval;

          if (avi_demux->video_need_flush[0]) {
             /* FIXME, do some flush event here */
            avi_demux->video_need_flush[0] = FALSE;
          }

          GST_DEBUG (0,"gst_avi_demux_chain: send video buffer %08x\n", *chunksize);
          gst_pad_push(avi_demux->video_pad[0], buf);
          GST_DEBUG (0,"gst_avi_demux_chain: sent video buffer %08x %p\n",
	    	      *chunksize, &avi_demux->video_pad[0]);
          avi_demux->current_frame++;
	}
      }
      *chunksize = (*chunksize + 1) & ~1;
      break;
    }
    case GST_RIFF_01wb:
    {
      GST_DEBUG (0,"gst_avi_demux_chain: tag found %08x size %08x\n",
		    chunkid, *chunksize);

      if (avi_demux->init_audio) {
	/*avi_demux->next_time += (*chunksize) * 1000000LL / avi_demux->audio_rate; */
	avi_demux->init_audio--;
      }

      if (GST_PAD_IS_CONNECTED (avi_demux->audio_pad[0])) {
	GstBuffer *buf;

	if (*chunksize) {
          buf = gst_bytestream_peek (bs, *chunksize);

          GST_BUFFER_TIMESTAMP (buf) = -1LL;

          if (avi_demux->audio_need_flush[0]) {
  	    GST_DEBUG (0,"audio flush\n");
            avi_demux->audio_need_flush[0] = FALSE;
            /* FIXME, do some flush event here */
          }

          GST_DEBUG (0,"gst_avi_demux_chain: send audio buffer %08x\n", *chunksize);
          gst_pad_push (avi_demux->audio_pad[0], buf);
          GST_DEBUG (0,"gst_avi_demux_chain: sent audio buffer %08x\n", *chunksize);
	}
      }
      *chunksize = (*chunksize + 1) & ~1;
      break;
    }
    default:
      GST_DEBUG (0, "  *****  unknown chunkid %08x (%s)\n", chunkid, gst_riff_id_to_fourcc (chunkid));
      *chunksize = (*chunksize + 1) & ~1;
      break;
  }
  GST_INFO (GST_CAT_PLUGIN_INFO, "chunkid %s, flush %08x, filepos %08llx", 
		  gst_riff_id_to_fourcc (chunkid), *chunksize, *filepos);

  *filepos += *chunksize;
  if (!gst_bytestream_flush (bs, *chunksize)) {
    return gst_avidemux_handle_event (avi_demux);
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
  if (!gst_avidemux_process_chunk (avi_demux, &filepos, GST_RIFF_TAG_RIFF, 0, &chunksize)) {
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
    case ARG_MEDIA_TIME:
      g_value_set_long (value, (src->tot_frames * src->time_interval) / 1000000);
      break;
    case ARG_CURRENT_TIME:
      g_value_set_long (value, (src->current_frame * src->time_interval) / 1000000);
      break;
    case ARG_FRAME_RATE:
      g_value_set_int (value, (src->frame_rate));
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
  factory = gst_elementfactory_new ("avidemux",GST_TYPE_AVI_DEMUX,
                                    &gst_avi_demux_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_audio_templ));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_video_templ));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_templ));

  type = gst_typefactory_new (&avidefinition);
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

