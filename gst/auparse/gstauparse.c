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

/* 2001/04/03 - Updated parseau to use caps nego
 *              Zaheer Merali <zaheer@grid9.net
 */

#include <stdlib.h>
#include <string.h>

#include <gstauparse.h>


/* elementfactory information */
static GstElementDetails gst_auparse_details = {
  ".au parser",
  "Parser/Audio",
  "Parse an .au file into raw audio",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

static GstCaps*
au_typefind (GstBuffer *buf, gpointer private)
{
  GstCaps *new = NULL;
  gulong *head = (gulong *) GST_BUFFER_DATA (buf);

  if (*head == 0x2e736e64 || *head == 0x646e732e)
    new = gst_caps_new ("au_typefind", "audio/au", NULL);

  return new;
}

/* typefactory for 'au' */
static GstTypeDefinition audefinition = {
  "auparse_audio/au",
  "audio/au",
  ".au",
  au_typefind,
};

GST_PADTEMPLATE_FACTORY (sink_factory_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "auparse_sink",
    "audio/au",
    NULL
  )
)


GST_PADTEMPLATE_FACTORY (src_factory_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "auparse_src",
    "audio/raw",
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT_RANGE (0, 1),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_LIST(
		      GST_PROPS_BOOLEAN (FALSE),
		      GST_PROPS_BOOLEAN (TRUE)
	  	    ),
      "width",      GST_PROPS_LIST(
		      GST_PROPS_INT (8),
		      GST_PROPS_INT (16)
		    ),
      "depth",      GST_PROPS_LIST(
		      GST_PROPS_INT (8),
		      GST_PROPS_INT (16)
		    ),
      "rate",       GST_PROPS_INT_RANGE (8000,48000),
      "channels",   GST_PROPS_INT_RANGE (1, 2)
  )
)

/* AuParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void 	gst_auparse_class_init		(GstAuParseClass *klass);
static void 	gst_auparse_init		(GstAuParse *auparse);

static void 	gst_auparse_chain		(GstPad *pad,GstBuffer *buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_auparse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_auparse_get_type (void) 
{
  static GType auparse_type = 0;

  if (!auparse_type) {
    static const GTypeInfo auparse_info = {
      sizeof(GstAuParseClass),      NULL,
      NULL,
      (GClassInitFunc) gst_auparse_class_init,
      NULL,
      NULL,
      sizeof(GstAuParse),
      0,
      (GInstanceInitFunc) gst_auparse_init,
    };
    auparse_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAuParse", &auparse_info, 0);
  }
  return auparse_type;
}

static void
gst_auparse_class_init (GstAuParseClass *klass) 
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void 
gst_auparse_init (GstAuParse *auparse) 
{
  auparse->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_factory_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->sinkpad);
  gst_pad_set_chain_function (auparse->sinkpad, gst_auparse_chain);

  auparse->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_factory_templ), "src");
  gst_element_add_pad (GST_ELEMENT (auparse), auparse->srcpad);

  auparse->offset = 0;
  auparse->size = 0;
  auparse->encoding = 0;
  auparse->frequency = 0;
  auparse->channels = 0;
}

static void 
gst_auparse_chain (GstPad *pad, GstBuffer *buf) 
{
  GstAuParse *auparse;
  gchar *data;
  glong size;
  GstCaps* tempcaps;
  gint law, depth;
  gboolean sign;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  auparse = GST_AUPARSE (gst_pad_get_parent (pad));
  
  GST_DEBUG (0, "gst_auparse_chain: got buffer in '%s'\n",
          gst_element_get_name (GST_ELEMENT (auparse)));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* if we haven't seen any data yet... */
  if (auparse->size == 0) {
    GstBuffer *newbuf;
    gulong *head = (gulong *)data;

    /* normal format is big endian (au is a Sparc format) */
    if (GULONG_FROM_BE (*(head++)) == 0x2e736e64) {
      auparse->le = 0;
      auparse->offset 		= GULONG_FROM_BE (*(head++));
      auparse->size 		= GULONG_FROM_BE (*(head++));
      auparse->encoding 	= GULONG_FROM_BE (*(head++));
      auparse->frequency 	= GULONG_FROM_BE (*(head++));
      auparse->channels 	= GULONG_FROM_BE (*(head++));

    /* but I wouldn't be surprised by a little endian version */
    } else if (GULONG_FROM_LE (*(head++)) == 0x2e736e64) {
      auparse->le = 1;
      auparse->offset 		= GULONG_FROM_LE(*(head++));
      auparse->size 		= GULONG_FROM_LE(*(head++));
      auparse->encoding 	= GULONG_FROM_LE(*(head++));
      auparse->frequency 	= GULONG_FROM_LE(*(head++));
      auparse->channels 	= GULONG_FROM_LE(*(head++));

    } else {
      g_warning ("help, dunno what I'm looking at!\n");
      gst_buffer_unref(buf);
      return;
    }

    g_print ("offset %ld, size %ld, encoding %ld, frequency %ld, channels %ld\n",
             auparse->offset,auparse->size,auparse->encoding,
             auparse->frequency,auparse->channels);
    GST_DEBUG (0, "offset %ld, size %ld, encoding %ld, frequency %ld, channels %ld\n",
             auparse->offset,auparse->size,auparse->encoding,
             auparse->frequency,auparse->channels);
    
    switch (auparse->encoding) {
      case 1:
	law = 1;
	depth = 8;
	sign = FALSE;
	break;
      case 2:
	law = 0;
	depth = 8;
	sign = TRUE;
	break;
      case 3:
	law = 0;
	depth = 16;
	sign = TRUE;
	break;
      default:
	g_warning ("help!, dont know how to deal with this format yet\n");
	return;
    }

    tempcaps = GST_CAPS_NEW ("auparse_src",
		             "audio/raw",
			       "format",  	GST_PROPS_STRING ("int"),
      			       "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
			       "rate",  	GST_PROPS_INT (auparse->frequency),
			       "channels",  	GST_PROPS_INT (auparse->channels),
			       "law",  		GST_PROPS_INT (law),
			       "depth", 	GST_PROPS_INT (depth),
			       "width", 	GST_PROPS_INT (depth),
			       "signed", 	GST_PROPS_BOOLEAN (sign));

    if (!gst_pad_try_set_caps (auparse->srcpad, tempcaps)) {
      gst_buffer_unref (buf);
      gst_element_error (GST_ELEMENT (auparse), "could not set audio caps");
      return;
    }

    newbuf = gst_buffer_new ();
    GST_BUFFER_DATA (newbuf) = (gpointer) malloc (size-(auparse->offset));
    memcpy (GST_BUFFER_DATA (newbuf), data+24, size-(auparse->offset));
    GST_BUFFER_SIZE (newbuf) = size-(auparse->offset);

    gst_buffer_unref (buf);

    gst_pad_push (auparse->srcpad, newbuf);
    return;
  }

  gst_pad_push (auparse->srcpad, buf);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create the plugin structure */
  /* create an elementfactory for the auparse element and list it */
  factory = gst_elementfactory_new ("auparse", GST_TYPE_AUPARSE,
                                    &gst_auparse_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_factory_templ));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_factory_templ));

  type = gst_typefactory_new (&audefinition);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "auparse",
  plugin_init
};

