/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2002> Wim Taymans <wim.taymans@chello.be>
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


#include <string.h>

#include "gstcdxaparse.h"

#define MAKE_FOUR_CC(a,b,c,d) ( ((guint32)a)     | (((guint32)b)<< 8) | \
	                        ((guint32)c)<<16 | (((guint32)d)<<24) )


/* RIFF types */
#define GST_RIFF_TAG_RIFF  MAKE_FOUR_CC('R','I','F','F')
#define GST_RIFF_RIFF_CDXA MAKE_FOUR_CC('C','D','X','A')


#define GST_RIFF_TAG_fmt  MAKE_FOUR_CC('f','m','t',' ')
#define GST_RIFF_TAG_data MAKE_FOUR_CC('d','a','t','a')


/* elementfactory information */
static GstElementDetails gst_cdxa_parse_details = {
  ".dat parser",
  "Codec/Parser",
  "Parse a .dat file (VCD) into raw mpeg1",
  VERSION,
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 2002",
};

static GstCaps* cdxa_type_find (GstBuffer *buf, gpointer private);

/* typefactory for 'cdxa' */
static GstTypeDefinition cdxadefinition = {
  "cdxaparse_video/avi",
  "video/avi",
  ".dat",
  cdxa_type_find,
};

/* CDXAParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "cdxaparse_sink",
     "video/avi",
      "format", GST_PROPS_STRING ("CDXA")
  )
)

GST_PAD_TEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "cdxaparse_src",
    "video/mpeg",
      "mpegversion",   GST_PROPS_INT (1),
      "systemstream",  GST_PROPS_BOOLEAN (TRUE)
  )
)

static void 	gst_cdxa_parse_class_init	(GstCDXAParseClass *klass);
static void 	gst_cdxa_parse_init		(GstCDXAParse *cdxa_parse);

static void 	gst_cdxa_parse_loop 		(GstElement *element);

static GstElementStateReturn
		gst_cdxa_parse_change_state 	(GstElement *element);


static GstElementClass *parent_class = NULL;
/*static guint gst_cdxa_parse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_cdxa_parse_get_type(void) 
{
  static GType cdxa_parse_type = 0;

  if (!cdxa_parse_type) {
    static const GTypeInfo cdxa_parse_info = {
      sizeof(GstCDXAParseClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_cdxa_parse_class_init,
      NULL,
      NULL,
      sizeof(GstCDXAParse),
      0,
      (GInstanceInitFunc)gst_cdxa_parse_init,
    };
    cdxa_parse_type = g_type_register_static(GST_TYPE_ELEMENT, "GstCDXAParse", &cdxa_parse_info, 0);
  }
  return cdxa_parse_type;
}

static void
gst_cdxa_parse_class_init (GstCDXAParseClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
  
  gstelement_class->change_state = gst_cdxa_parse_change_state;
}

static void 
gst_cdxa_parse_init (GstCDXAParse *cdxa_parse) 
{
  GST_FLAG_SET (cdxa_parse, GST_ELEMENT_EVENT_AWARE);
				
  cdxa_parse->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (cdxa_parse), cdxa_parse->sinkpad);

  cdxa_parse->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_templ), "src");
  gst_element_add_pad (GST_ELEMENT (cdxa_parse), cdxa_parse->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (cdxa_parse), gst_cdxa_parse_loop);

}

static GstCaps*
cdxa_type_find (GstBuffer *buf,
              gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);
  GstCaps *new;

  GST_DEBUG (0,"cdxa_parse: typefind");

  if (GUINT32_FROM_LE (((guint32 *)data)[0]) != GST_RIFF_TAG_RIFF)
    return NULL;
  if (GUINT32_FROM_LE (((guint32 *)data)[2]) != GST_RIFF_RIFF_CDXA)
    return NULL;

  new = GST_CAPS_NEW ("cdxa_type_find",
		      "video/avi", 
		        "RIFF", GST_PROPS_STRING ("CDXA"));

  return new;
}

static gboolean
gst_cdxa_parse_handle_event (GstCDXAParse *cdxa_parse)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;
  
  gst_bytestream_get_status (cdxa_parse->bs, &remaining, &event);

  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS:
      gst_pad_event_default (cdxa_parse->sinkpad, event);
      break;
    case GST_EVENT_SEEK:
      g_warning ("seek event\n");
      break;
    case GST_EVENT_FLUSH:
      g_warning ("flush event\n");
      break;
    case GST_EVENT_DISCONTINUOUS:
      g_warning ("discont event\n");
      break;
    default:
      g_warning ("unhandled event %d\n", type);
      break;
  }

  return TRUE;
}

/*

CDXA starts with the following header:
 
! RIFF:4 ! size:4 ! "CDXA" ! "fmt " ! size:4 ! (size+1)&~1 bytes of crap ! 
! "data" ! data_size:4 ! (data_size/2352) sectors...

*/

typedef struct 
{
  gchar   RIFF_tag[4];
  guint32 riff_size;
  gchar   CDXA_tag[4];
  gchar   fmt_tag[4];
  guint32 fmt_size;
} CDXAParseHeader;
   
/*
A sectors is 2352 bytes long and is composed of:

!  sync    !  header ! subheader ! data ...   ! edc     !
! 12 bytes ! 4 bytes ! 8 bytes   ! 2324 bytes ! 4 bytes !
!-------------------------------------------------------!

We parse the data out of it and send it to the srcpad.
*/

static void
gst_cdxa_parse_loop (GstElement *element)
{
  GstCDXAParse *cdxa_parse;
  CDXAParseHeader *header;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_CDXA_PARSE (element));

  cdxa_parse = GST_CDXA_PARSE (element);

  if (cdxa_parse->state == CDXA_PARSE_HEADER) {
    guint32 fmt_size;
    guint8 *buf;

    header = (CDXAParseHeader *) gst_bytestream_peek_bytes (cdxa_parse->bs, 20);
    if (!header)
      return;

    cdxa_parse->riff_size = GUINT32_FROM_LE (header->riff_size);
    fmt_size = (GUINT32_FROM_LE (header->fmt_size) + 1)&~1;

    /* flush the header + fmt_size bytes + 4 bytes "data" */
    if (!gst_bytestream_flush (cdxa_parse->bs, 20 + fmt_size + 4))
      return;
    
    /* get the data size */
    buf = gst_bytestream_peek_bytes (cdxa_parse->bs, 4);
    if (!buf)
      return;
    cdxa_parse->data_size = GUINT32_FROM_LE (*((guint32 *)buf));

    /* flush the data size */
    if (!gst_bytestream_flush (cdxa_parse->bs, 4))
      return;

    if (cdxa_parse->data_size % CDXA_SECTOR_SIZE)
      g_warning ("cdxa_parse: size not multiple of %d bytes", CDXA_SECTOR_SIZE);

    cdxa_parse->sectors = cdxa_parse->data_size / CDXA_SECTOR_SIZE;
    
    cdxa_parse->state = CDXA_PARSE_DATA;
  }
  else {
    GstBuffer *buf;
    GstBuffer *outbuf;

    buf = gst_bytestream_read (cdxa_parse->bs, CDXA_SECTOR_SIZE);
    if (!buf) {
      gst_cdxa_parse_handle_event (cdxa_parse);
      return;
    }

    outbuf = gst_buffer_create_sub (buf, 24, CDXA_DATA_SIZE);
    gst_buffer_unref (buf);

    gst_pad_push (cdxa_parse->srcpad, outbuf);
  }
}

static GstElementStateReturn
gst_cdxa_parse_change_state (GstElement *element)
{
  GstCDXAParse *cdxa_parse = GST_CDXA_PARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      cdxa_parse->state = CDXA_PARSE_HEADER;
      cdxa_parse->bs = gst_bytestream_new (cdxa_parse->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (cdxa_parse->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* this filter needs the riff parser */
  if (!gst_library_load ("gstbytestream")) {
    gst_info("cdxaparse: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }

  /* create an elementfactory for the cdxa_parse element */
  factory = gst_element_factory_new ("cdxaparse", GST_TYPE_CDXA_PARSE,
                                    &gst_cdxa_parse_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_templ));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_templ));

  type = gst_type_factory_new (&cdxadefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "cdxaparse",
  plugin_init
};

