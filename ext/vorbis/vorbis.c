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


#include <vorbisenc.h>
#include <vorbisdec.h>

extern GstElementDetails vorbisenc_details;
extern GstElementDetails vorbisdec_details;

static GstCaps* 	vorbis_type_find 	(GstBuffer *buf, gpointer private);

GstPadTemplate *dec_src_template, *dec_sink_template; 
GstPadTemplate *enc_src_template, *enc_sink_template;

static GstCaps*
vorbis_caps_factory (void)
{
  return
   gst_caps_new (
  	"vorbis_vorbis",
  	"audio/x-ogg",
  	NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   gst_caps_new (
  	"vorbis_raw",
  	"audio/raw",
	gst_props_new (
  	  "format",   		GST_PROPS_STRING ("int"),
    	    "law",   		GST_PROPS_INT (0),
    	    "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
    	    "signed", 		GST_PROPS_BOOLEAN (TRUE),
    	    "width", 		GST_PROPS_INT (16),
    	    "depth",    	GST_PROPS_INT (16),
    	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT_RANGE (1, 2),
	    NULL));
}

static GstCaps*
raw_caps2_factory (void)
{
  return
   gst_caps_new (
  	"vorbis_raw_float",
  	"audio/raw",
	gst_props_new (
  	  "format",   		GST_PROPS_STRING ("float"),
    	    "layout",		GST_PROPS_STRING ("IEEE"),
    	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT (2),
	    NULL));
}

static GstTypeDefinition vorbisdefinition = {
  "vorbis_audio/x-ogg",
  "audio/x-ogg",
  ".ogg",
  vorbis_type_find,
};

static GstCaps* 
vorbis_type_find (GstBuffer *buf, gpointer private) 
{
  gulong head = GULONG_FROM_BE (*((gulong *)GST_BUFFER_DATA (buf)));

  if (head  != 0x4F676753)
    return NULL;

  return gst_caps_new ("vorbis_type_find", "audio/x-ogg", NULL);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *dec;
  GstTypeFactory *type;
  GstCaps *raw_caps, *vorbis_caps, *raw_caps2;

  gst_plugin_set_longname (plugin, "The OGG Vorbis Codec");

  /* create an elementfactory for the vorbisenc element */
  enc = gst_element_factory_new ("vorbisenc", GST_TYPE_VORBISENC,
                                &vorbisenc_details);
  g_return_val_if_fail (enc != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  raw_caps2 = raw_caps2_factory ();
  vorbis_caps = vorbis_caps_factory ();

  /* register sink pads */
  enc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      raw_caps, NULL);
  gst_element_factory_add_pad_template (enc, enc_sink_template);

  /* register src pads */
  enc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     vorbis_caps, NULL);
  gst_element_factory_add_pad_template (enc, enc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the vorbisdec element */
  dec = gst_element_factory_new("vorbisdec",GST_TYPE_VORBISDEC,
                               &vorbisdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
 
  /* register sink pads */
  dec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      vorbis_caps, NULL);
  gst_element_factory_add_pad_template (dec, dec_sink_template);

  raw_caps = gst_caps_prepend (raw_caps, raw_caps2);
  /* register src pads */
  dec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_element_factory_add_pad_template (dec, dec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  type = gst_type_factory_new (&vorbisdefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "vorbis",
  plugin_init
};
