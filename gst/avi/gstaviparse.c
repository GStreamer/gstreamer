/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
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


/* #define GST_DEBUG_ENABLED */
#include <string.h>
#include <config.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include <gst/riff/riff.h>

#define GST_TYPE_AVI_PARSE \
  (gst_avi_parse_get_type())
#define GST_AVI_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVI_PARSE,GstAviParse))
#define GST_AVI_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVI_PARSE,GstAviParse))
#define GST_IS_AVI_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVI_PARSE))
#define GST_IS_AVI_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVI_PARSE))

typedef struct _GstAviParse GstAviParse;
typedef struct _GstAviParseClass GstAviParseClass;

struct _GstAviParse {
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad, 
  		*srcpad;

  GstRiffParse  *rp;
};

struct _GstAviParseClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_avi_parse_details = {
  "Avi parseer",
  "Codec/Parseer",
  "LGPL",
  "Demultiplex an avi file into audio and video",
  VERSION,
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 2003",
};

static GstCaps* avi_type_find (GstBuffer *buf, gpointer private);

/* typefactory for 'avi' */
static GstTypeDefinition avidefinition = {
  "aviparse_video/avi",
  "video/avi",
  ".avi",
  avi_type_find,
};

/* AviParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BITRATE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "aviparse_sink",
     "video/avi",
      "format",    GST_PROPS_STRING ("AVI")
  )
)

GST_PAD_TEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "aviparse_sink",
     "video/avi",
      "format",    GST_PROPS_STRING ("AVI")
  )
)

static void 		gst_avi_parse_class_init		(GstAviParseClass *klass);
static void 		gst_avi_parse_init			(GstAviParse *avi_parse);

static void 		gst_avi_parse_loop 			(GstElement *element);

static GstElementStateReturn
			gst_avi_parse_change_state 		(GstElement *element);

static void     	gst_avi_parse_get_property      	(GObject *object, guint prop_id, 	
								 GValue *value, GParamSpec *pspec);


static GstElementClass *parent_class = NULL;
/*static guint gst_avi_parse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avi_parse_get_type(void) 
{
  static GType avi_parse_type = 0;

  if (!avi_parse_type) {
    static const GTypeInfo avi_parse_info = {
      sizeof(GstAviParseClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_avi_parse_class_init,
      NULL,
      NULL,
      sizeof(GstAviParse),
      0,
      (GInstanceInitFunc)gst_avi_parse_init,
    };
    avi_parse_type = g_type_register_static(GST_TYPE_ELEMENT, "GstAviParse", &avi_parse_info, 0);
  }
  return avi_parse_type;
}

static void
gst_avi_parse_class_init (GstAviParseClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property (G_OBJECT_CLASS(klass), ARG_BITRATE,
    g_param_spec_long ("bitrate","bitrate","bitrate",
                       G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE)); /* CHECKME */

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  gobject_class->get_property = gst_avi_parse_get_property;
  
  gstelement_class->change_state = gst_avi_parse_change_state;
}

static void 
gst_avi_parse_init (GstAviParse *avi_parse) 
{
  GST_FLAG_SET (avi_parse, GST_ELEMENT_EVENT_AWARE);
				
  avi_parse->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (avi_parse), avi_parse->sinkpad);

  avi_parse->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_templ), "src");
  gst_element_add_pad (GST_ELEMENT (avi_parse), avi_parse->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (avi_parse), gst_avi_parse_loop);
}

static GstCaps*
avi_type_find (GstBuffer *buf,
              gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);
  GstCaps *new;

  GST_DEBUG (0,"avi_parse: typefind");

  if (GUINT32_FROM_LE (((guint32 *)data)[0]) != GST_RIFF_TAG_RIFF)
    return NULL;
  if (GUINT32_FROM_LE (((guint32 *)data)[2]) != GST_RIFF_RIFF_AVI)
    return NULL;

  new = GST_CAPS_NEW ("avi_type_find",
		      "video/avi", 
		        "format", GST_PROPS_STRING ("AVI"));
  return new;
}

static void
gst_avi_parse_loop (GstElement *element)
{
  GstAviParse *avi_parse;
  GstRiffParse *rp;
  GstRiffReturn res;
  gst_riff_chunk chunk;
  guint32 data_size;
  guint64 pos;
  
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_AVI_PARSE (element));

  avi_parse = GST_AVI_PARSE (element);

  rp = avi_parse->rp;

  pos = gst_bytestream_tell (rp->bs);

  res = gst_riff_parse_next_chunk (rp, &chunk);
  if (res == GST_RIFF_EOS) { 
    gst_element_set_eos (element);
    return;
  }

  switch (chunk.id) {
    case GST_RIFF_TAG_RIFF:
    case GST_RIFF_TAG_LIST:
      g_print ("%08llx: %4.4s %08x %4.4s\n", pos, (gchar *)&chunk.id, chunk.size, (gchar *)&chunk.type);
      data_size = 0;
      break;
    default:
      g_print ("%08llx: %4.4s %08x\n", pos, (gchar *)&chunk.id, chunk.size);
      data_size = chunk.size;
      break;
  }

  if (GST_PAD_IS_USABLE (avi_parse->srcpad) && data_size) {
    GstBuffer *buf;

    gst_riff_parse_peek (rp, &buf, data_size);
    gst_pad_push (avi_parse->srcpad, buf);
  }
  data_size = (data_size + 1) & ~1;

  gst_riff_parse_flush (rp, data_size);
}

static GstElementStateReturn
gst_avi_parse_change_state (GstElement *element)
{
  GstAviParse *avi_parse = GST_AVI_PARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      avi_parse->rp = gst_riff_parse_new (avi_parse->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_riff_parse_free (avi_parse->rp);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_avi_parse_get_property (GObject *object, guint prop_id, GValue *value,
			    GParamSpec *pspec)
{
  GstAviParse *src;

  g_return_if_fail (GST_IS_AVI_PARSE (object));

  src = GST_AVI_PARSE (object);

  switch (prop_id) {
    case ARG_BITRATE:
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* this filter needs the riff parser */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  /* create an elementfactory for the avi_parse element */
  factory = gst_element_factory_new ("aviparse", GST_TYPE_AVI_PARSE,
                                     &gst_avi_parse_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_templ));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_templ));

  type = gst_type_factory_new (&avidefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "aviparse",
  plugin_init
};

