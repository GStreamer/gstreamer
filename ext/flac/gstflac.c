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

#include "flac_compat.h"

extern GstElementDetails flacenc_details;
extern GstElementDetails flacdec_details;

GstPadTemplate *gst_flacdec_src_template, *gst_flacdec_sink_template; 
GstPadTemplate *gst_flacenc_src_template, *gst_flacenc_sink_template;

static GstCaps*
flac_caps_factory (void)
{
  return
   gst_caps_new (
  	"flac_flac",
  	"application/x-flac",
	/*gst_props_new (
 	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT_RANGE (1, 2),
	    NULL)*/ NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   gst_caps_new (
  	"flac_raw",
  	"audio/x-raw-int",
	gst_props_new (
    	    "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
    	    "signed", 		GST_PROPS_BOOLEAN (TRUE),
    	    "width", 		GST_PROPS_INT (16),
    	    "depth",    	GST_PROPS_INT (16),
    	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT_RANGE (1, 2),
	    NULL));
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *dec;
  GstCaps *raw_caps, *flac_caps;

  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  gst_plugin_set_longname (plugin, "The FLAC Lossless compressor Codec");

  /* create an elementfactory for the flacenc element */
  enc = gst_element_factory_new ("flacenc", GST_TYPE_FLACENC,
                                &flacenc_details);
  g_return_val_if_fail (enc != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  flac_caps = flac_caps_factory ();

  /* register sink pads */
  gst_flacenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      raw_caps, NULL);
  gst_element_factory_add_pad_template (enc, gst_flacenc_sink_template);

  /* register src pads */
  gst_flacenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     flac_caps, NULL);
  gst_element_factory_add_pad_template (enc, gst_flacenc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the flacdec element */
  dec = gst_element_factory_new("flacdec",GST_TYPE_FLACDEC,
                               &flacdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
  gst_element_factory_set_rank (dec, GST_ELEMENT_RANK_PRIMARY);
 
  /* register sink pads */
  gst_flacdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      flac_caps, NULL);
  gst_element_factory_add_pad_template (dec, gst_flacdec_sink_template);

  /* register src pads */
  gst_flacdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_element_factory_add_pad_template (dec, gst_flacdec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "flac",
  plugin_init
};
