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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstspeexdec.h"

static GstPadTemplate *speexdec_src_template, *speexdec_sink_template;

/* elementfactory information */
GstElementDetails gst_speexdec_details = {
  "speex audio decoder",
  "Codec/Audio/Decoder",
  ".speex",
  "Wim Taymans <wim.taymans@chello.be>",
};

/* SpeexDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void                     gst_speexdec_base_init (gpointer g_class);
static void			gst_speexdec_class_init	(GstSpeexDec *klass);
static void			gst_speexdec_init		(GstSpeexDec *speexdec);

static void			gst_speexdec_chain	(GstPad *pad, GstData *_data);
static GstPadLinkReturn	gst_speexdec_sinkconnect 	(GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;
/*static guint gst_speexdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_speexdec_get_type(void) {
  static GType speexdec_type = 0;

  if (!speexdec_type) {
    static const GTypeInfo speexdec_info = {
      sizeof(GstSpeexDecClass),
      gst_speexdec_base_init,
      NULL,
      (GClassInitFunc)gst_speexdec_class_init,
      NULL,
      NULL,
      sizeof(GstSpeexDec),
      0,
      (GInstanceInitFunc)gst_speexdec_init,
    };
    speexdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSpeexDec", &speexdec_info, 0);
  }
  return speexdec_type;
}

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

static void
gst_speexdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *speex_caps;

  raw_caps = GST_CAPS_GET (raw_caps_factory);
  speex_caps = GST_CAPS_GET (speex_caps_factory);

  speexdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
		                              GST_PAD_ALWAYS, 
					      speex_caps, NULL);
  speexdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
		                             GST_PAD_ALWAYS, 
					     raw_caps, NULL);
  gst_element_class_add_pad_template (element_class, speexdec_sink_template);
  gst_element_class_add_pad_template (element_class, speexdec_src_template);

  gst_element_class_set_details (element_class, &gst_speexdec_details);
}

static void
gst_speexdec_class_init (GstSpeexDec *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

static void
gst_speexdec_init (GstSpeexDec *speexdec)
{
  GST_DEBUG ("gst_speexdec_init: initializing");

  /* create the sink and src pads */
  speexdec->sinkpad = gst_pad_new_from_template (speexdec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (speexdec), speexdec->sinkpad);
  gst_pad_set_chain_function (speexdec->sinkpad, gst_speexdec_chain);
  gst_pad_set_link_function (speexdec->sinkpad, gst_speexdec_sinkconnect);

  speexdec->srcpad = gst_pad_new_from_template (speexdec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (speexdec), speexdec->srcpad);

}

static GstPadLinkReturn
gst_speexdec_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstSpeexDec *speexdec;
  gint rate;
  
  speexdec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;
  
  gst_caps_get_int (caps, "rate", &rate);

  if (gst_pad_try_set_caps (speexdec->srcpad, 
		      GST_CAPS_NEW (
	  		"speex_raw",
			"audio/x-raw-int",
			    "endianness", GST_PROPS_INT (G_BYTE_ORDER),
			    "signed",     GST_PROPS_BOOLEAN (TRUE),
			    "width",      GST_PROPS_INT (16),
			    "depth",      GST_PROPS_INT (16),
			    "rate",       GST_PROPS_INT (rate),
			    "channels",   GST_PROPS_INT (1)
			   )))
  {
    return GST_PAD_LINK_OK;
  }
  return GST_PAD_LINK_REFUSED;
}

static void
gst_speexdec_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSpeexDec *speexdec;
  gchar *data;
  guint size;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  /*g_return_if_fail(GST_IS_BUFFER(buf)); */

  speexdec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  gst_buffer_unref(buf);
}

