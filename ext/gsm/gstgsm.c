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


#include "gstgsmdec.h"
#include "gstgsmenc.h"

/* elementfactory information */
extern GstElementDetails gst_gsmdec_details;
extern GstElementDetails gst_gsmenc_details;

GstPadTemplate *gsmdec_src_template, *gsmdec_sink_template; 
GstPadTemplate *gsmenc_src_template, *gsmenc_sink_template;

GST_CAPS_FACTORY (gsm_caps_factory,
  GST_CAPS_NEW (
    "gsm_gsm",
    "audio/x-gsm",
      "rate",       GST_PROPS_INT_RANGE (1000, 48000)
  )
)

GST_CAPS_FACTORY (raw_caps_factory,
  GST_CAPS_NEW (
    "gsm_raw",
    "audio/raw",
    "format",       GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (TRUE),
      "width",      GST_PROPS_INT (16),
      "depth",      GST_PROPS_INT (16),
      "rate",       GST_PROPS_INT_RANGE (1000, 48000),
      "channels",   GST_PROPS_INT (1)
  )
)

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *dec, *enc;
  GstCaps *raw_caps, *gsm_caps;

  /* create an elementfactory for the gsmdec element */
  enc = gst_elementfactory_new("gsmenc",GST_TYPE_GSMENC,
                                   &gst_gsmenc_details);
  g_return_val_if_fail(enc != NULL, FALSE);

  raw_caps = GST_CAPS_GET (raw_caps_factory);
  gsm_caps = GST_CAPS_GET (gsm_caps_factory);

  /* register sink pads */
  gsmenc_sink_template = gst_padtemplate_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      raw_caps, NULL);
  gst_elementfactory_add_padtemplate (enc, gsmenc_sink_template);

  /* register src pads */
  gsmenc_src_template = gst_padtemplate_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     gsm_caps, NULL);
  gst_elementfactory_add_padtemplate (enc, gsmenc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the gsmdec element */
  dec = gst_elementfactory_new("gsmdec",GST_TYPE_GSMDEC,
                                   &gst_gsmdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
 
  /* register sink pads */
  gsmdec_sink_template = gst_padtemplate_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      gsm_caps, NULL);
  gst_elementfactory_add_padtemplate (dec, gsmdec_sink_template);

  /* register src pads */
  gsmdec_src_template = gst_padtemplate_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_elementfactory_add_padtemplate (dec, gsmdec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gsm",
  plugin_init
};
