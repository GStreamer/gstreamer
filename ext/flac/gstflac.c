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


#include "gstflacenc.h"
#include "gstflacdec.h"

extern GstElementDetails flacenc_details;
extern GstElementDetails flacdec_details;

static GstCaps* 	flac_type_find 	(GstBuffer *buf, gpointer private);

GstPadTemplate *dec_src_template, *dec_sink_template; 
GstPadTemplate *enc_src_template, *enc_sink_template;

static GstCaps*
flac_caps_factory (void)
{
  return
   gst_caps_new (
  	"flac_flac",
  	"audio/x-flac",
  	NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   gst_caps_new (
  	"flac_raw",
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

static GstTypeDefinition flacdefinition = {
  "flac_audio/x-flac",
  "audio/x-flac",
  ".flac",
  flac_type_find,
};

static GstCaps* 
flac_type_find (GstBuffer *buf, gpointer private) 
{
  gulong head = GULONG_FROM_BE (*((gulong *)GST_BUFFER_DATA (buf)));

  if (head  != 0x664C6143)
    return NULL;

  return gst_caps_new ("flac_type_find", "audio/x-flac", NULL);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *dec;
  GstTypeFactory *type;
  GstCaps *raw_caps, *flac_caps;

  gst_plugin_set_longname (plugin, "The FLAC Lossless compressor Codec");

  /* create an elementfactory for the flacenc element */
  enc = gst_element_factory_new ("flacenc", GST_TYPE_FLACENC,
                                &flacenc_details);
  g_return_val_if_fail (enc != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  flac_caps = flac_caps_factory ();

  /* register sink pads */
  enc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      raw_caps, NULL);
  gst_element_factory_add_pad_template (enc, enc_sink_template);

  /* register src pads */
  enc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     flac_caps, NULL);
  gst_element_factory_add_pad_template (enc, enc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the flacdec element */
  dec = gst_element_factory_new("flacdec",GST_TYPE_FLACDEC,
                               &flacdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
 
  /* register sink pads */
  dec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      flac_caps, NULL);
  gst_element_factory_add_pad_template (dec, dec_sink_template);

  /* register src pads */
  dec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_element_factory_add_pad_template (dec, dec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  type = gst_type_factory_new (&flacdefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "flac",
  plugin_init
};
