/* Gnome-Streamer
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



#include <stdlib.h>
#include <string.h>

#include "gstavimux.h"



/* elementfactory information */
static GstElementDetails 
gst_avimux_details = 
{
  ".avi mux",
  "Mux/Video",
  "Encodes audio and video into an avi stream",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* AviMux signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "sink_video",
    "video/avi",
    NULL
  )
)
    
GST_PADTEMPLATE_FACTORY (video_sink_factory,
  "video_%02d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sink_video",
    "video/avi",
      "format",   GST_PROPS_STRING ("strf_vids")
  )
)
    
GST_PADTEMPLATE_FACTORY (audio_sink_factory,
  "audio_%02d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sink_audio",
    "video/avi",
      "format",   GST_PROPS_STRING ("strf_auds")
  )
)
    

static void 	gst_avimux_class_init		(GstAviMuxClass *klass);
static void 	gst_avimux_init			(GstAviMux *avimux);

static void 	gst_avimux_chain 		(GstPad *pad, GstBuffer *buf);
static GstPad* 	gst_avimux_request_new_pad 	(GstElement *element, GstPadTemplate *templ, const gchar *name);
	
static void     gst_avimux_set_property         (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void     gst_avimux_get_property         (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);


static GstElementClass *parent_class = NULL;
//static guint gst_avimux_signals[LAST_SIGNAL] = { 0 };

GType
gst_avimux_get_type (void) 
{
  static GType avimux_type = 0;

  if (!avimux_type) {
    static const GTypeInfo avimux_info = {
      sizeof(GstAviMuxClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_avimux_class_init,
      NULL,
      NULL,
      sizeof(GstAviMux),
      0,
      (GInstanceInitFunc)gst_avimux_init,
    };
    avimux_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAviMux", &avimux_info, 0);
  }
  return avimux_type;
}

static void
gst_avimux_class_init (GstAviMuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_avimux_set_property;
  gobject_class->get_property = gst_avimux_get_property;

  gstelement_class->request_new_pad = gst_avimux_request_new_pad;
}

static void 
gst_avimux_init (GstAviMux *avimux) 
{
  avimux->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (avimux), avimux->srcpad);

  avimux->state = GST_AVIMUX_INITIAL;
  avimux->riff = NULL;
  avimux->num_audio_pads = 0;
  avimux->num_video_pads = 0;
  avimux->next_time = 0;

  avimux->riff = gst_riff_encoder_new (GST_RIFF_RIFF_AVI);
  avimux->aviheader = g_malloc0 (sizeof (gst_riff_avih));
}

static GstPadConnectReturn
gst_avimux_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstAviMux *avimux;
  const gchar* format = gst_caps_get_string (caps, "format");
  gint padnum = GPOINTER_TO_INT (gst_pad_get_element_private (pad));

  avimux = GST_AVIMUX (gst_pad_get_parent (pad));

  GST_DEBUG (0, "avimux: sinkconnect triggered on %s (%d), %s\n", gst_pad_get_name (pad), 
		  padnum, format);

  if (!strncmp (format, "strf_vids", 9)) {
    gst_riff_strf_vids *strf_vids = g_malloc(sizeof(gst_riff_strf_vids));

    strf_vids->size        = sizeof(gst_riff_strf_vids);
    strf_vids->width       = gst_caps_get_int (caps, "width");
    strf_vids->height      = gst_caps_get_int (caps, "height");;
    strf_vids->planes      = gst_caps_get_int (caps, "planes");;
    strf_vids->bit_cnt     = gst_caps_get_int (caps, "bit_cnt");;
    strf_vids->compression = gst_caps_get_fourcc_int (caps, "compression");;
    strf_vids->image_size  = gst_caps_get_int (caps, "image_size");;
    strf_vids->xpels_meter = gst_caps_get_int (caps, "xpels_meter");;
    strf_vids->ypels_meter = gst_caps_get_int (caps, "ypels_meter");;
    strf_vids->num_colors  = gst_caps_get_int (caps, "num_colors");;
    strf_vids->imp_colors  = gst_caps_get_int (caps, "imp_colors");;

    avimux->video_header[padnum] = strf_vids;
  }
  else if (!strncmp (format, "strf_auds", 9)) {

  }
  return GST_PAD_CONNECT_OK;
}

static GstPad*
gst_avimux_request_new_pad (GstElement     *element,
			    GstPadTemplate *templ,
			    const gchar    *req_name)
{
  GstAviMux *avimux;
  gchar *name = NULL;
  GstPad *newpad;
  
  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("avimux: request pad that is not a SINK pad\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_AVIMUX (element), NULL);

  avimux = GST_AVIMUX (element);

  if (templ == GST_PADTEMPLATE_GET (audio_sink_factory)) {
    name = g_strdup_printf ("audio_%02d", avimux->num_audio_pads);
    newpad = gst_pad_new_from_template (templ, name);
    gst_pad_set_element_private (newpad, GINT_TO_POINTER (avimux->num_audio_pads));

    avimux->audio_pad[avimux->num_audio_pads] = newpad;
    avimux->num_audio_pads++;
  }
  else if (templ == GST_PADTEMPLATE_GET (video_sink_factory)) {
    name = g_strdup_printf ("video_%02d", avimux->num_video_pads);
    newpad = gst_pad_new_from_template (templ, name);
    gst_pad_set_element_private (newpad, GINT_TO_POINTER (avimux->num_video_pads));

    avimux->video_pad[avimux->num_video_pads] = newpad;
    avimux->num_video_pads++;
  }
  else {
    g_warning ("avimux: this is not our template!\n");
    return NULL;
  }

  gst_pad_set_chain_function (newpad, gst_avimux_chain);
  gst_pad_set_connect_function (newpad, gst_avimux_sinkconnect);
  gst_element_add_pad (element, newpad);
  
  return newpad;
}

static void
gst_avimux_make_header (GstAviMux *avimux)
{
  gint i;

  gst_riff_strh strh;

  avimux->aviheader->us_frame = 40000;
  avimux->aviheader->streams  = avimux->num_video_pads + avimux->num_audio_pads;
  avimux->aviheader->width    = -1;
  avimux->aviheader->height   = -1;
  gst_riff_encoder_avih(avimux->riff, avimux->aviheader, sizeof(gst_riff_avih));

  memset(&strh, 0, sizeof(gst_riff_strh));
  strh.scale = 40000;

  gst_riff_encoder_strh(avimux->riff, GST_RIFF_FCC_vids, &strh, sizeof(gst_riff_strh));

  for (i=0; i<avimux->num_video_pads; i++) {
    gst_riff_encoder_strf(avimux->riff, avimux->video_header[i], sizeof(gst_riff_strf_vids));
  }
}

static void
gst_avimux_chain (GstPad *pad, GstBuffer *buf)
{
  GstAviMux *avimux;
  guchar *data;
  gulong size;
  const gchar *padname;
  gint channel;
  GstBuffer *newbuf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_BUFFER_DATA (buf) != NULL);

  avimux = GST_AVIMUX (gst_pad_get_parent (pad));

  data = (guchar *)GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  switch(avimux->state) {
    case GST_AVIMUX_INITIAL:
      GST_DEBUG (0,"gst_avimux_chain: writing header\n");
      gst_avimux_make_header(avimux);
      newbuf = gst_riff_encoder_get_and_reset_buffer(avimux->riff);
      gst_pad_push(avimux->srcpad, newbuf);
      avimux->state = GST_AVIMUX_MOVI;
    case GST_AVIMUX_MOVI:
      padname = gst_pad_get_name (pad);
      channel = GPOINTER_TO_INT (gst_pad_get_element_private (pad));

      if (strncmp(padname, "audio_", 6) == 0) {
        GST_DEBUG (0,"gst_avimux_chain: got audio buffer in from channel %02d %lu\n", channel, size);
        gst_riff_encoder_chunk(avimux->riff, GST_RIFF_01wb, NULL, size); 
        newbuf = gst_riff_encoder_get_and_reset_buffer(avimux->riff);
        gst_pad_push(avimux->srcpad, newbuf);
      }
      else if (strncmp(padname, "video_", 6) == 0) {
        GST_DEBUG (0,"gst_avimux_chain: got video buffer in from channel %02d %lu\n", channel, size);
        gst_riff_encoder_chunk(avimux->riff, GST_RIFF_00db, NULL, size); 
        newbuf = gst_riff_encoder_get_and_reset_buffer(avimux->riff);
        GST_DEBUG (0,"gst_avimux_chain: encoded %u\n", GST_BUFFER_SIZE(newbuf));
        gst_pad_push(avimux->srcpad, newbuf);
      }
      GST_BUFFER_SIZE(buf) = (GST_BUFFER_SIZE(buf)+1)&~1;
      gst_pad_push(avimux->srcpad, buf);
      break;
    default:
      break;
  }

  //gst_buffer_unref(buf);
}

static void 
gst_avimux_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAviMux *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AVIMUX(object));
  src = GST_AVIMUX(object);

  switch(prop_id) {
    default:
      break;
  }
}

static void 
gst_avimux_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAviMux *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AVIMUX(object));
  src = GST_AVIMUX(object);

  switch(prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* this filter needs the riff parser */
  if (!gst_library_load ("gstriff")) {
    gst_info ("avimux: could not load support library: 'gstriff'\n");
    return FALSE;
  }

  /* create an elementfactory for the avimux element */
  factory = gst_elementfactory_new ("avimux", GST_TYPE_AVIMUX,
                                    &gst_avimux_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (audio_sink_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (video_sink_factory));
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "avimux",
  plugin_init
};

