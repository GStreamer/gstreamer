/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 *
 */

#include <string.h>
#include <gst/gst.h>

#include "gstmonkeydec.h"
#include "gstmonkeyenc.h"

extern GstElementDetails gst_monkeydec_details;
extern GstElementDetails gst_monkeyenc_details;

static GstCaps* monkey_type_find (GstBuffer *buf, gpointer priv);

GstPadTemplate *monkeydec_sink_template, *monkeydec_src_template;
GstPadTemplate *monkeyenc_sink_template, *monkeyenc_src_template;

static GstCaps*
monkey_caps_factory (void)
{
  return gst_caps_new ("monkey_application", 
                       "application/x-ape", 
                       NULL
		      );
}


static GstCaps*
raw_caps_factory (void)
{ 
  return gst_caps_new ("monkey_raw", 
                       "audio/x-raw-int",
                       gst_props_new (
                         "endianness",  GST_PROPS_INT (G_LITTLE_ENDIAN),
                         "signed",      GST_PROPS_BOOLEAN (TRUE),
                         "width",       GST_PROPS_INT (16),
                         "depth",       GST_PROPS_INT (16), 
                         "rate",        GST_PROPS_INT_RANGE (11025, 44100),
                         "channels",    GST_PROPS_INT_RANGE (1, 2),
                         NULL                      
                       ));
}

static GstCaps*
wav_caps_factory (void)
{ 
  return gst_caps_new ("monkey_wav", 
                       "audio/x-wav", 
                       NULL );
}


static GstTypeDefinition monkeydefinition = {
  "monkey_application/x-ape", "application/x-ape", ".ape", monkey_type_find, 
};


static GstCaps* 
monkey_type_find (GstBuffer *buf, gpointer priv) 
{
  if (strncmp ((gchar *)GST_BUFFER_DATA (buf), "MAC ", 4) != 0)
    return NULL;

  return gst_caps_new ("monkey_type_find", "application/x-ape", NULL);
}


static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *monkey_dec, *monkey_enc;
  GstTypeFactory *type;
  GstCaps *raw_caps, *wav_caps, *monkey_caps;

  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  raw_caps = raw_caps_factory ();
  monkey_caps = monkey_caps_factory ();
  wav_caps = wav_caps_factory ();

  /* create an elementfactory for the monkeydec element */
  monkey_dec = gst_element_factory_new ("monkeydec", GST_TYPE_MONKEYDEC, &gst_monkeydec_details);
  g_return_val_if_fail(monkey_dec != NULL, FALSE); 

  /* register sink pads */
  monkeydec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
						                                      GST_PAD_ALWAYS,
                                    						  monkey_caps, NULL);
  gst_element_factory_add_pad_template (monkey_dec, monkeydec_sink_template);
  

  /* register src pads */
  monkeydec_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
                              					         GST_PAD_ALWAYS,
                              					         raw_caps, NULL);
  gst_element_factory_add_pad_template (monkey_dec, monkeydec_src_template);
  
  gst_element_factory_set_rank (monkey_dec, GST_ELEMENT_RANK_PRIMARY);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (monkey_dec));  


  /* create an elementfactory for the monkeydec element */
  monkey_enc = gst_element_factory_new ("monkeyenc", GST_TYPE_MONKEYENC, &gst_monkeyenc_details);
  g_return_val_if_fail(monkey_enc != NULL, FALSE); 

  /* register sink pads */
  monkeyenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
						                                      GST_PAD_ALWAYS,
                                    						  raw_caps, NULL);
  gst_element_factory_add_pad_template (monkey_enc, monkeyenc_sink_template);
  

  /* register src pads */
  monkeyenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
                                  			         GST_PAD_ALWAYS,
                              					         monkey_caps, NULL);
  gst_element_factory_add_pad_template (monkey_enc, monkeyenc_src_template);
  
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (monkey_enc)); 

  
  type = gst_type_factory_new (&monkeydefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "monkey audio",
  plugin_init
};
