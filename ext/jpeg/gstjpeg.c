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


#include "gstjpegdec.h"
#include "gstjpegenc.h"

/* elementfactory information */
extern GstElementDetails gst_jpegdec_details;
extern GstElementDetails gst_jpegenc_details;

GstPadTemplate *jpegdec_src_template, *jpegdec_sink_template; 
GstPadTemplate *jpegenc_src_template, *jpegenc_sink_template;

static GstCaps*
jpeg_caps_factory (void) 
{
  return
    gst_caps_new (
  	"jpeg_jpeg",
  	"video/jpeg",
  	NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
    gst_caps_new (
  	"jpeg_raw",
  	"video/raw",
	gst_props_new (
  	  "format",    GST_PROPS_LIST (
                 	 GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0'))
               	       ),
  	  "width",     GST_PROPS_INT_RANGE (16, 4096),
  	  "height",    GST_PROPS_INT_RANGE (16, 4096),
  	  NULL));
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *dec, *enc;
  GstCaps *raw_caps, *jpeg_caps;

  /* create an elementfactory for the jpegdec element */
  enc = gst_elementfactory_new("jpegenc",GST_TYPE_JPEGENC,
                                   &gst_jpegenc_details);
  g_return_val_if_fail(enc != NULL, FALSE);

  raw_caps = raw_caps_factory ();
  jpeg_caps = jpeg_caps_factory ();

  /* register sink pads */
  jpegenc_sink_template = gst_padtemplate_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      raw_caps, NULL);
  gst_elementfactory_add_padtemplate (enc, jpegenc_sink_template);

  /* register src pads */
  jpegenc_src_template = gst_padtemplate_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     jpeg_caps, NULL);
  gst_elementfactory_add_padtemplate (enc, jpegenc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the jpegdec element */
  dec = gst_elementfactory_new("jpegdec",GST_TYPE_JPEGDEC,
                                   &gst_jpegdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
 
  /* register sink pads */
  jpegdec_sink_template = gst_padtemplate_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      jpeg_caps, NULL);
  gst_elementfactory_add_padtemplate (dec, jpegdec_sink_template);

  /* register src pads */
  jpegdec_src_template = gst_padtemplate_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_elementfactory_add_padtemplate (dec, jpegdec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "jpeg",
  plugin_init
};
