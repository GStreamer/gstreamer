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


#include "gsttarkinenc.h"
#include "gsttarkindec.h"

extern GstElementDetails tarkinenc_details;
extern GstElementDetails tarkindec_details;

static GstCaps* 	tarkin_typefind 	(GstBuffer *buf, gpointer private);

GstPadTemplate *enc_src_template, *enc_sink_template;
GstPadTemplate *dec_src_template, *dec_sink_template;

static GstCaps*
tarkin_caps_factory (void)
{
  return
   gst_caps_new (
  	"tarkin_tarkin",
  	"video/x-ogg",
  	NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   GST_CAPS_NEW (
    "tarkin_raw",
    "video/raw",
      "format",     GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
      "bpp",        GST_PROPS_INT (24),
      "depth",      GST_PROPS_INT (24),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "red_mask",   GST_PROPS_INT (0xff0000),
      "green_mask", GST_PROPS_INT (0xff00),
      "blue_mask",  GST_PROPS_INT (0xff),
      "width",      GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",     GST_PROPS_INT_RANGE (0, G_MAXINT)
   );
}

static GstTypeDefinition tarkindefinition = 
{
  "tarkin_video/x-ogg",
  "video/x-ogg",
  ".ogg",
  tarkin_typefind,
};

static GstCaps* 
tarkin_typefind (GstBuffer *buf, gpointer private) 
{
  gulong head = GULONG_FROM_BE (*((gulong *)GST_BUFFER_DATA (buf)));

  /* FIXME */
  return NULL;
  
  if (head  != 0x4F676753)
    return NULL;

  return gst_caps_new ("tarkin_typefind", "video/x-ogg", NULL);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *dec;
  GstTypeFactory *type;
  GstCaps *raw_caps, *tarkin_caps;

  gst_plugin_set_longname (plugin, "The OGG Vorbis Codec");

  /* create an elementfactory for the tarkinenc element */
  enc = gst_elementfactory_new ("tarkinenc", GST_TYPE_TARKINENC,
                                &tarkinenc_details);
  g_return_val_if_fail (enc != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  tarkin_caps = tarkin_caps_factory ();

  /* register sink pads */
  enc_sink_template = gst_padtemplate_new ("sink", 
		  			   GST_PAD_SINK, 
		                           GST_PAD_ALWAYS, 
					   raw_caps, 
					   NULL);
  gst_elementfactory_add_padtemplate (enc, enc_sink_template);

  /* register src pads */
  enc_src_template = gst_padtemplate_new ("src", 
		                          GST_PAD_SRC, 
		                          GST_PAD_ALWAYS, 
					  tarkin_caps, 
					  NULL);
  gst_elementfactory_add_padtemplate (enc, enc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the tarkindec element */
  dec = gst_elementfactory_new ("tarkindec", GST_TYPE_TARKINDEC,
                                &tarkindec_details);
  g_return_val_if_fail (dec != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  tarkin_caps = tarkin_caps_factory ();

  /* register sink pads */
  dec_sink_template = gst_padtemplate_new ("sink", 
		  			   GST_PAD_SINK, 
		                           GST_PAD_ALWAYS, 
					   tarkin_caps, 
					   NULL);
  gst_elementfactory_add_padtemplate (dec, dec_sink_template);

  /* register src pads */
  dec_src_template = gst_padtemplate_new ("src", 
		                          GST_PAD_SRC, 
		                          GST_PAD_ALWAYS, 
					  raw_caps, 
					  NULL);
  gst_elementfactory_add_padtemplate (dec, dec_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  type = gst_typefactory_new (&tarkindefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "tarkin",
  plugin_init
};
