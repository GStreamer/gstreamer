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


/*#define GST_DEBUG_ENABLED */
#include <string.h>

#include "gstavitypes.h"



/* elementfactory information */
static GstElementDetails gst_avi_types_details = {
  "avi type converter",
  "Decoder/Video",
  "Converts avi types into gstreamer types",
  VERSION,
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 1999",
};

/* AviTypes signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_TYPE_FOUND,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "avitypes_sink",
     "video/avi",
      "format",         GST_PROPS_LIST (
      		          GST_PROPS_STRING ("strf_vids"),
      		          GST_PROPS_STRING ("strf_auds"),
      		          GST_PROPS_STRING ("strf_iavs")
			)
  )
)

GST_PADTEMPLATE_FACTORY (src_templ,
  "video_src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "avitypes_src",
    "video/raw",
      "format",         GST_PROPS_LIST (
                          GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')),
                          GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' '))
                        ),
      "width",          GST_PROPS_INT_RANGE (16, 4096),
      "height",         GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "avitypes_src",
    "video/avi",
      "format",         GST_PROPS_STRING ("strf_vids")
  ),
  GST_CAPS_NEW (
    "src_audio",
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
    "src_audio",
    "audio/mp3",
    NULL
  ),
  GST_CAPS_NEW (
    "src_video",
    "video/jpeg",
    NULL
  ),
  GST_CAPS_NEW (
    "src_dv",
    "video/dv",
    NULL
  )
)

static void 	gst_avi_types_class_init	(GstAviTypesClass *klass);
static void 	gst_avi_types_init		(GstAviTypes *avi_types);

static void 	gst_avi_types_chain 		(GstPad *pad, GstBuffer *buffer); 

static void     gst_avi_types_get_property      (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);


static GstElementClass *parent_class = NULL;
/*static guint gst_avi_types_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avi_types_get_type(void) 
{
  static GType avi_types_type = 0;

  if (!avi_types_type) {
    static const GTypeInfo avi_types_info = {
      sizeof(GstAviTypesClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_avi_types_class_init,
      NULL,
      NULL,
      sizeof(GstAviTypes),
      0,
      (GInstanceInitFunc)gst_avi_types_init,
    };
    avi_types_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAviTypes", &avi_types_info, 0);
  }
  return avi_types_type;
}

static void
gst_avi_types_class_init (GstAviTypesClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TYPE_FOUND,
    g_param_spec_boolean ("type_found","type_found","type_found",
                     FALSE, G_PARAM_READABLE));

  gobject_class->get_property = gst_avi_types_get_property;
}

static GstPadConnectReturn 
gst_avi_types_sinkconnect (GstPad *pad, GstCaps *caps) 
{
  GstAviTypes *avi_types;
  const gchar *format;
  GstCaps *newcaps = NULL;

  avi_types = GST_AVI_TYPES (gst_pad_get_parent (pad));

  format = gst_caps_get_string (caps, "format");

  if (!strcmp (format, "strf_vids")) {
    gulong video_format = gst_caps_get_fourcc_int (caps, "compression");

    switch (video_format) {
      case GST_MAKE_FOURCC ('M','J','P','G'):
        newcaps = gst_caps_new ("avi_type_mjpg",
		      "video/jpeg", NULL);
        break;
      case GST_MAKE_FOURCC ('d','v','s','d'):
        newcaps = gst_caps_new ("avi_type_dv", 
		    	    "video/dv", 
			    gst_props_new (
			      "format",		GST_PROPS_STRING ("NTSC"),
			      NULL));
      default:
	/* else we simply don't convert, and hope there is a native decoder
	 * available */
	newcaps = caps;
        break;
    }
  }
  else if (!strcmp (format, "strf_auds")) {
    gint16 audio_format 	= gst_caps_get_int (caps, "fmt");
    gint blockalign 		= gst_caps_get_int (caps, "blockalign");
    gint size 			= gst_caps_get_int (caps, "size");
    gint channels 		= gst_caps_get_int (caps, "channels");
    gint rate	 		= gst_caps_get_int (caps, "rate");
    gboolean sign		= (size == 8 ? FALSE : TRUE);

    GST_DEBUG (GST_CAT_PLUGIN_INFO, "avitypes: new caps with audio format:%04x\n", audio_format);

    switch (audio_format) {
      case 0x0001:
        newcaps = gst_caps_new ("avi_type_pcm",
		    "audio/raw",
		    gst_props_new (
		      "format",           GST_PROPS_STRING ("int"),
		      "law",              GST_PROPS_INT (0),
		      "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
		      "signed",           GST_PROPS_BOOLEAN (sign),
		      "width",            GST_PROPS_INT ((blockalign*8)/channels),
		      "depth",            GST_PROPS_INT (size),
		      "rate",             GST_PROPS_INT (rate),
		      "channels",         GST_PROPS_INT (channels),
		      NULL
		    ));
        break;
      case 0x0050:
      case 0x0055:
        newcaps = gst_caps_new ("avi_type_mp3",
		      "audio/mp3", NULL);
        break;
      default:
        break;
    }
  }
  else if (!strcmp (format, "strf_iavs")) {
    newcaps = gst_caps_new ("avi_type_dv", 
		    	    "video/dv", 
			    gst_props_new (
			      "format",		GST_PROPS_STRING ("NTSC"),
			      NULL));
  }
  
  if (newcaps) {
    gst_pad_try_set_caps (avi_types->srcpad, newcaps);
    avi_types->type_found = TRUE;
    return GST_PAD_CONNECT_OK;
  }
  return GST_PAD_CONNECT_REFUSED;
}

static void 
gst_avi_types_init (GstAviTypes *avi_types) 
{
  avi_types->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (avi_types), avi_types->sinkpad);
  gst_pad_set_connect_function (avi_types->sinkpad, gst_avi_types_sinkconnect);
  gst_pad_set_chain_function (avi_types->sinkpad, gst_avi_types_chain);

  avi_types->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_templ), "src");
  gst_element_add_pad (GST_ELEMENT (avi_types), avi_types->srcpad);

  avi_types->type_found = FALSE;
}

static void 
gst_avi_types_chain (GstPad *pad, GstBuffer *buffer) 
{
  GstAviTypes *avi_types;

  avi_types = GST_AVI_TYPES (gst_pad_get_parent (pad));

  if (GST_PAD_IS_CONNECTED (avi_types->srcpad))
    gst_pad_push (avi_types->srcpad, buffer);
  else
    gst_buffer_unref (buffer);
}

static void
gst_avi_types_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAviTypes *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AVI_TYPES (object));

  src = GST_AVI_TYPES (object);

  switch (prop_id) {
    case ARG_TYPE_FOUND:
      g_value_set_boolean (value, src->type_found);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the avi_types element */
  factory = gst_elementfactory_new ("avitypes",GST_TYPE_AVI_TYPES,
                                    &gst_avi_types_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  /*gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_templ)); */
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_templ));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "avitypes",
  plugin_init
};

