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
#include <stdlib.h>
#include <string.h>

#include "gsttarkindec.h"

static GstPadTemplate *dec_src_template, *dec_sink_template;

/* elementfactory information */
GstElementDetails tarkindec_details = {
  "Ogg Tarkin decoder",
  "Filter/Video/Decoder",
  "Decodes video in OGG Tarkin format",
  "Monty <monty@xiph.org>, " 
  "Wim Taymans <wim.taymans@chello.be>",
};

/* TarkinDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BITRATE,
};

static void     gst_tarkindec_base_init         (gpointer g_class);
static void 	gst_tarkindec_class_init 	(TarkinDecClass *klass);
static void 	gst_tarkindec_init 		(TarkinDec *arkindec);

static void 	gst_tarkindec_chain 		(GstPad *pad, GstData *_data);
static void 	gst_tarkindec_setup 		(TarkinDec *tarkindec);
static GstElementStateReturn
		gst_tarkindec_change_state 	(GstElement *element);

static void 	gst_tarkindec_get_property 	(GObject *object, guint prop_id, GValue *value,
						 GParamSpec *pspec);
static void 	gst_tarkindec_set_property 	(GObject *object, guint prop_id, const GValue *value,
						 GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_tarkindec_signals[LAST_SIGNAL] = { 0 }; */

GType
tarkindec_get_type (void)
{
  static GType tarkindec_type = 0;

  if (!tarkindec_type) {
    static const GTypeInfo tarkindec_info = {
      sizeof (TarkinDecClass), 
      gst_tarkindec_base_init,
      NULL,
      (GClassInitFunc) gst_tarkindec_class_init,
      NULL,
      NULL,
      sizeof (TarkinDec),
      0,
      (GInstanceInitFunc) gst_tarkindec_init,
    };

    tarkindec_type = g_type_register_static (GST_TYPE_ELEMENT, "TarkinDec", &tarkindec_info, 0);
  }
  return tarkindec_type;
}

static GstCaps*
tarkin_caps_factory (void)
{
  return
   gst_caps_new (
  	"tarkin_tarkin",
  	"application/ogg",
	  NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return
   GST_CAPS_NEW (
    "tarkin_raw",
    "video/x-raw-rgb",
      "bpp",        GST_PROPS_INT (24),
      "depth",      GST_PROPS_INT (24),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "red_mask",   GST_PROPS_INT (0xff0000),
      "green_mask", GST_PROPS_INT (0xff00),
      "blue_mask",  GST_PROPS_INT (0xff),
      "width",      GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",     GST_PROPS_INT_RANGE (0, G_MAXINT),
      "framerate",  GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)
   );
}

static void
gst_tarkindec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *tarkin_caps;

  raw_caps = raw_caps_factory ();
  tarkin_caps = tarkin_caps_factory ();

  dec_sink_template = gst_pad_template_new ("sink", 
					    GST_PAD_SINK, 
					    GST_PAD_ALWAYS, 
					    tarkin_caps, 
					    NULL);
  dec_src_template = gst_pad_template_new ("src", 
					   GST_PAD_SRC, 
					   GST_PAD_ALWAYS, 
					   raw_caps, 
					   NULL);
  gst_element_class_add_pad_template (element_class, dec_sink_template);
  gst_element_class_add_pad_template (element_class, dec_src_template);

  gst_element_class_set_details (element_class, &tarkindec_details);
}

static void
gst_tarkindec_class_init (TarkinDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE, 
    g_param_spec_int ("bitrate", "bitrate", "bitrate", 
	    G_MININT, G_MAXINT, 3000, G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_tarkindec_set_property;
  gobject_class->get_property = gst_tarkindec_get_property;

  gstelement_class->change_state = gst_tarkindec_change_state;
}

static void
gst_tarkindec_init (TarkinDec * tarkindec)
{
  tarkindec->sinkpad = gst_pad_new_from_template (dec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (tarkindec), tarkindec->sinkpad);
  gst_pad_set_chain_function (tarkindec->sinkpad, gst_tarkindec_chain);

  tarkindec->srcpad = gst_pad_new_from_template (dec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (tarkindec), tarkindec->srcpad);

  tarkindec->bitrate = 3000;
  tarkindec->setup = FALSE;
  tarkindec->nheader = 0;

  /* we're chained and we can deal with events */
  GST_FLAG_SET (tarkindec, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_tarkindec_setup (TarkinDec *tarkindec)
{
  tarkindec->tarkin_stream = tarkin_stream_new ();
  
  ogg_sync_init (&tarkindec->oy);
  ogg_stream_init (&tarkindec->os, 1);
  tarkin_info_init (&tarkindec->ti);
  tarkin_comment_init (&tarkindec->tc);

  tarkindec->setup = TRUE;
}

static void
gst_tarkindec_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  TarkinDec *tarkindec;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  tarkindec = GST_TARKINDEC (gst_pad_get_parent (pad));

  if (!tarkindec->setup) {
    gst_element_error (tarkindec, CORE, NEGOTATION, NULL, ("decoder not initialized (input is not tarkin?)"));
    if (GST_IS_BUFFER (buf))
      gst_buffer_unref (buf);
    else
      gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  if (GST_IS_EVENT (buf)) {
    switch (GST_EVENT_TYPE (buf)) {
      case GST_EVENT_EOS:
      default:
	gst_pad_event_default (pad, GST_EVENT (buf));
	break;
    }
  }
  else {
    gchar *data;
    gulong size;
    gchar *buffer;
    guchar *rgb;
    TarkinTime date;
    TarkinVideoLayerDesc *layer;
  
    /* data to decode */
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    buffer = ogg_sync_buffer(&tarkindec->oy, size);
    memcpy (buffer, data, size);
    ogg_sync_wrote(&tarkindec->oy, size);

    if (ogg_sync_pageout (&tarkindec->oy, &tarkindec->og)) {
      ogg_stream_pagein (&tarkindec->os, &tarkindec->og);

      while (ogg_stream_packetout (&tarkindec->os, &tarkindec->op)) {
        if (tarkindec->op.e_o_s)
          break;
	if (tarkindec->nheader < 3) { /* 3 first packets to headerin */
          tarkin_synthesis_headerin (&tarkindec->ti, &tarkindec->tc, &tarkindec->op);

	  if (tarkindec->nheader == 2) {
	    tarkin_synthesis_init (tarkindec->tarkin_stream, &tarkindec->ti);
          }
          tarkindec->nheader++;
        } else {
	  tarkin_synthesis_packetin (tarkindec->tarkin_stream, &tarkindec->op);
	  
	  while (tarkin_synthesis_frameout (tarkindec->tarkin_stream, &rgb, 0, &date) == 0) {
            GstBuffer *outbuf;

	    layer = &tarkindec->tarkin_stream->layer->desc;

	    if (!GST_PAD_CAPS (tarkindec->srcpad)) {
	      if (gst_pad_try_set_caps (tarkindec->srcpad,
				      GST_CAPS_NEW (
				        "tarkin_raw",
				        "video/x-raw-rgb",
				        "bpp",        GST_PROPS_INT (24),
				        "depth",      GST_PROPS_INT (24),
				        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
				        "red_mask",   GST_PROPS_INT (0xff0000),
				        "green_mask", GST_PROPS_INT (0xff00),
				        "blue_mask",  GST_PROPS_INT (0xff),
				        "width",      GST_PROPS_INT (layer->width),
				        "height",     GST_PROPS_INT (layer->height),
					"framerate",  GST_PROPS_FLOAT (0.) /* FIXME!!! */
				       )) <= 0)
	      {
		gst_element_error (tarkindec, CORE, NEGOTATION, NULL,  ("could not output format"));
		gst_buffer_unref (buf);
		return;
	      }
	    }
	    outbuf = gst_buffer_new ();
	    GST_BUFFER_DATA (outbuf) = rgb;
	    GST_BUFFER_SIZE (outbuf) = layer->width * layer->height * 3;
	    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
	    gst_pad_push (tarkindec->srcpad, GST_DATA (outbuf));
	    
	    tarkin_synthesis_freeframe (tarkindec->tarkin_stream, rgb);
          }
        }
      }
    }
    gst_buffer_unref (buf);
  }
}

static GstElementStateReturn
gst_tarkindec_change_state (GstElement *element)
{
  TarkinDec *tarkindec;

  tarkindec = GST_TARKINDEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      gst_tarkindec_setup (tarkindec);
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }
  
  return parent_class->change_state (element);
}

static void
gst_tarkindec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  TarkinDec *tarkindec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TARKINDEC (object));

  tarkindec = GST_TARKINDEC (object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_int (value, tarkindec->bitrate);
      break;
    default:
      break;
  }
}

static void
gst_tarkindec_set_property (GObject *object, guint prop_id, const GValue *value,
			    GParamSpec *pspec)
{
  TarkinDec *tarkindec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TARKINDEC (object));

  tarkindec = GST_TARKINDEC (object);

  switch (prop_id) {
    case ARG_BITRATE:
      tarkindec->bitrate = g_value_get_int (value);
      break;
    default:
      break;
  }
}
