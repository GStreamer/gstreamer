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


/*#define GST_DEBUG_ENABLED */
#include <string.h>

#include "gstaviaudiodecoder.h"



/* elementfactory information */
static GstElementDetails gst_avi_audio_decoder_details = {
  ".avi parser",
  "Parser/Video",
  "Parse a .avi file into audio and video",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 1999",
};

/* AviAudioDecoder signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "avidecoder_sink",
     "video/avi",
      "format", GST_PROPS_STRING ("strf_auds")
  )
)

GST_PADTEMPLATE_FACTORY (src_audio_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "src_audio",
    "audio/raw",
      "format",           GST_PROPS_STRING ("int"),
       "law",              GST_PROPS_INT (0),
       "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
       "signed",           GST_PROPS_BOOLEAN (TRUE),
       "width",            GST_PROPS_INT (16),
       "depth",            GST_PROPS_INT (16),
       "rate",             GST_PROPS_INT_RANGE (11025, 44100),
       "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
)

static void 	gst_avi_audio_decoder_class_init	(GstAviAudioDecoderClass *klass);
static void 	gst_avi_audio_decoder_init		(GstAviAudioDecoder *avi_audio_decoder);

static void 	gst_avi_audio_decoder_chain 		(GstPad *pad, GstBuffer *buf);

static void     gst_avi_audio_decoder_get_property      (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);



static GstElementClass *parent_class = NULL;
/*static guint gst_avi_audio_decoder_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avi_audio_decoder_get_type(void) 
{
  static GType avi_audio_decoder_type = 0;

  if (!avi_audio_decoder_type) {
    static const GTypeInfo avi_audio_decoder_info = {
      sizeof(GstAviAudioDecoderClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_avi_audio_decoder_class_init,
      NULL,
      NULL,
      sizeof(GstAviAudioDecoder),
      0,
      (GInstanceInitFunc)gst_avi_audio_decoder_init,
    };
    avi_audio_decoder_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAviAudioDecoder", &avi_audio_decoder_info, 0);
  }
  return avi_audio_decoder_type;
}

static void
gst_avi_audio_decoder_class_init (GstAviAudioDecoderClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);
  
  gobject_class->get_property = gst_avi_audio_decoder_get_property;
}

static void 
gst_avi_audio_decoder_init (GstAviAudioDecoder *avi_audio_decoder) 
{
}

static void
gst_avi_audio_decoder_chain (GstPad *pad,
		       GstBuffer *buf)
{
  GstAviAudioDecoder *avi_audio_decoder;
  guchar *data;
  gulong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD(pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_BUFFER_DATA(buf) != NULL);

  avi_audio_decoder = GST_AVI_AUDIO_DECODER (gst_pad_get_parent (pad));
  GST_DEBUG (0,"gst_avi_audio_decoder_chain: got buffer in %u\n", GST_BUFFER_OFFSET (buf));
  g_print ("gst_avi_audio_decoder_chain: got buffer in %u\n", GST_BUFFER_OFFSET (buf));
  data = (guchar *)GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  gst_buffer_unref (buf);
}

static void     
gst_avi_audio_decoder_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAviAudioDecoder *src;

  g_return_if_fail (GST_IS_AVI_AUDIO_DECODER (object));

  src = GST_AVI_AUDIO_DECODER (object);

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

  /* create an elementfactory for the avi_audio_decoder element */
  factory = gst_elementfactory_new ("aviaudiodecoder",GST_TYPE_AVI_AUDIO_DECODER,
                                    &gst_avi_audio_decoder_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_templ));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_audio_templ));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "aviaudiodecoder",
  plugin_init
};

