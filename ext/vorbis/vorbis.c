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

extern GType vorbisfile_get_type(void);

extern GstElementDetails vorbisfile_details;
extern GstElementDetails vorbisenc_details;

static GstCaps* 	vorbis_type_find 	(GstBuffer *buf, gpointer private);

GstPadTemplate *gst_vorbisdec_src_template, *gst_vorbisdec_sink_template; 
GstPadTemplate *gst_vorbisenc_src_template, *gst_vorbisenc_sink_template;

static GstCaps*
vorbis_caps_factory (void)
{
  return
   gst_caps_new (
  	"vorbis_vorbis",
  	"application/ogg",
  	NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   gst_caps_new (
  	"vorbis_raw",
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

static GstCaps*
raw_caps2_factory (void)
{
  return
   gst_caps_new (
  	"vorbis_raw_float",
  	"audio/x-raw-float",
	gst_props_new (
	    "width",		GST_PROPS_INT (32),
	    "endianness",	GST_PROPS_INT (G_BYTE_ORDER),
    	    "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
    	    "channels", 	GST_PROPS_INT_RANGE (1, 2),
            "buffer-frames",    GST_PROPS_INT_RANGE (1, G_MAXINT),
	    NULL));
}

static GstTypeDefinition vorbisdefinition = {
  "vorbis_audio/x-ogg",
  "application/ogg",
  ".ogg",
  vorbis_type_find,
};

static GstCaps*
vorbis_type_find (GstBuffer *buf, gpointer private)
{
  guint32 head;
  gint offset;
  guint8 *data;
  gint size;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (size < sizeof(guint32))
    return NULL;

  head = GUINT32_FROM_BE (*((guint32 *)data));

  if (head  == 0x4F676753) {
    return gst_caps_new ("vorbis_type_find", "application/ogg", NULL);
  } else {
    /* checks for existance of vorbis identification header in case
     * there's an ID3 tag */
    for (offset = 0; offset < size-7; offset++) {
      if (data[offset] == 0x01 && 
          data[offset+1] == 'v' && 
          data[offset+2] == 'o' &&
          data[offset+3] == 'r' && 
          data[offset+4] == 'b' &&
          data[offset+5] == 'i' && 
          data[offset+6] == 's' ) {
        return gst_caps_new ("vorbis_type_find", "application/ogg", NULL);
      }
    }
  }

  return NULL;
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *file;
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
  gst_vorbisenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                                      GST_PAD_ALWAYS, 
					              raw_caps, NULL);
  gst_element_factory_add_pad_template (enc, gst_vorbisenc_sink_template);

  /* register src pads */
  gst_vorbisenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                                     GST_PAD_ALWAYS, 
					             vorbis_caps, NULL);
  gst_element_factory_add_pad_template (enc, gst_vorbisenc_src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* register sink pads */
  gst_vorbisdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                                      GST_PAD_ALWAYS, 
					              vorbis_caps, NULL);
  raw_caps = gst_caps_prepend (raw_caps, raw_caps2);
  /* register src pads */
  gst_vorbisdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                                     GST_PAD_ALWAYS, 
					             raw_caps, NULL);
  /* create an elementfactory for the vorbisfile element */
  file = gst_element_factory_new ("vorbisfile", vorbisfile_get_type(),
                                  &vorbisfile_details);
  g_return_val_if_fail(file != NULL, FALSE);
  gst_element_factory_set_rank (file, GST_ELEMENT_RANK_PRIMARY);
 
  /* register sink pads */
  gst_element_factory_add_pad_template (file, gst_vorbisdec_sink_template);
  /* register src pads */
  gst_element_factory_add_pad_template (file, gst_vorbisdec_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (file));

  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

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
