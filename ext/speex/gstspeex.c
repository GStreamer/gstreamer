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


#include "gstspeexdec.h"
#include "gstspeexenc.h"

/* elementfactory information */
extern GstElementDetails gst_speexdec_details;
extern GstElementDetails gst_speexenc_details;

GstPadTemplate *speexdec_src_template, *speexdec_sink_template; 
GstPadTemplate *speexenc_src_template, *speexenc_sink_template;

GST_CAPS_FACTORY (speex_caps_factory,
  GST_CAPS_NEW (
    "speex_speex",
    "audio/x-speex",
      "rate",       GST_PROPS_INT_RANGE (1000, 48000),
      "channels",   GST_PROPS_INT (1)
  )
)

GST_CAPS_FACTORY (raw_caps_factory,
  GST_CAPS_NEW (
    "speex_raw",
    "audio/x-raw-int",
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
  GstCaps *raw_caps, *speex_caps;

  /* create an elementfactory for the speexdec element */
  enc = gst_element_factory_new("speexenc",GST_TYPE_SPEEXENC,
                                   &gst_speexenc_details);
  g_return_val_if_fail(enc != NULL, FALSE);

  raw_caps = GST_CAPS_GET (raw_caps_factory);
  speex_caps = GST_CAPS_GET (speex_caps_factory);

  /* register sink pads */
  speexenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      raw_caps, NULL);
  gst_element_factory_add_pad_template (enc, speexenc_sink_template);

  /* register src pads */
  speexenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     speex_caps, NULL);
  gst_element_factory_add_pad_template (enc, speexenc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the speexdec element */
  dec = gst_element_factory_new("speexdec",GST_TYPE_SPEEXDEC,
                                   &gst_speexdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
  gst_element_factory_set_rank (dec, GST_ELEMENT_RANK_PRIMARY);
 
  /* register sink pads */
  speexdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      speex_caps, NULL);
  gst_element_factory_add_pad_template (dec, speexdec_sink_template);

  /* register src pads */
  speexdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_element_factory_add_pad_template (dec, speexdec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "speex",
  plugin_init
};
