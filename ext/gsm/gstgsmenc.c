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

#include "gstgsmenc.h"

/* elementfactory information */
GstElementDetails gst_gsmenc_details = {
  "GSM audio encoder",
  "Codec/Encoder/Audio",
  "Encodes audio using GSM",
  "Wim Taymans <wim.taymans@chello.be>",
};

/* GSMEnc signals and args */
enum {
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void                     gst_gsmenc_base_init (gpointer g_class);
static void			gst_gsmenc_class_init	(GstGSMEnc *klass);
static void			gst_gsmenc_init		(GstGSMEnc *gsmenc);

static void			gst_gsmenc_chain	(GstPad *pad,GstData *_data);
static GstPadLinkReturn	gst_gsmenc_sinkconnect 	(GstPad *pad, const GstCaps *caps);

static GstElementClass *parent_class = NULL;
static guint gst_gsmenc_signals[LAST_SIGNAL] = { 0 };

GType
gst_gsmenc_get_type (void)
{
  static GType gsmenc_type = 0;

  if (!gsmenc_type) {
    static const GTypeInfo gsmenc_info = {
      sizeof (GstGSMEncClass),
      gst_gsmenc_base_init, 
      NULL,
      (GClassInitFunc) gst_gsmenc_class_init,
      NULL,
      NULL,
      sizeof (GstGSMEnc),
      0,
      (GInstanceInitFunc) gst_gsmenc_init,
    };
    gsmenc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstGSMEnc", &gsmenc_info, 0);
  }
  return gsmenc_type;
}

static GstStaticPadTemplate gsmenc_src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-gsm, "
      "rate = (int) [ 1000, 48000 ], "
      "channels = (int) 1"
  )
);

static GstStaticPadTemplate gsmenc_sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) true, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = (int) [ 1000, 48000 ], "
      "channels = (int) 1"
  )
);

static void
gst_gsmenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gsmenc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gsmenc_src_template));
  gst_element_class_set_details (element_class, &gst_gsmenc_details);
}
  
static void
gst_gsmenc_class_init (GstGSMEnc *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_gsmenc_signals[FRAME_ENCODED] =
    g_signal_new ("frame_encoded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstGSMEncClass, frame_encoded), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
gst_gsmenc_init (GstGSMEnc *gsmenc)
{
  /* create the sink and src pads */
  gsmenc->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gsmenc_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (gsmenc), gsmenc->sinkpad);
  gst_pad_set_chain_function (gsmenc->sinkpad, gst_gsmenc_chain);
  gst_pad_set_link_function (gsmenc->sinkpad, gst_gsmenc_sinkconnect);

  gsmenc->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gsmenc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (gsmenc), gsmenc->srcpad);

  gsmenc->state = gsm_create ();
  gsmenc->bufsize = 0;
  gsmenc->next_ts = 0;
  gsmenc->rate = 8000;
}

static GstPadLinkReturn
gst_gsmenc_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstGSMEnc *gsmenc;
  GstStructure *structure;

  gsmenc = GST_GSMENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int  (structure, "rate", &gsmenc->rate);
  if (gst_pad_try_set_caps (gsmenc->srcpad,
        gst_caps_new_simple("audio/x-gsm",
          "rate",     G_TYPE_INT, gsmenc->rate,
          "channels", G_TYPE_INT, 1,
          NULL)) > 0)
  {
    return GST_PAD_LINK_OK;
  }
  return GST_PAD_LINK_REFUSED;

}

static void
gst_gsmenc_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstGSMEnc *gsmenc;
  gsm_signal *data;
  guint size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  gsmenc = GST_GSMENC (GST_OBJECT_PARENT (pad));
	      
  data = (gsm_signal*) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf) / sizeof (gsm_signal);

  if (gsmenc->bufsize && (gsmenc->bufsize + size >= 160)) {
    GstBuffer *outbuf;

    memcpy (gsmenc->buffer + gsmenc->bufsize, data, (160 - gsmenc->bufsize) * sizeof (gsm_signal));

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = g_malloc (33 * sizeof (gsm_byte));
    GST_BUFFER_SIZE (outbuf) = 33 * sizeof (gsm_byte);

    gsm_encode (gsmenc->state, gsmenc->buffer, (gsm_byte *) GST_BUFFER_DATA (outbuf));

    GST_BUFFER_TIMESTAMP (outbuf) = gsmenc->next_ts;
    gst_pad_push (gsmenc->srcpad, GST_DATA (outbuf));
    gsmenc->next_ts += (160.0 / gsmenc->rate) * 1000000;

    size -= (160 - gsmenc->bufsize); 
    data += (160 - gsmenc->bufsize);
    gsmenc->bufsize = 0;
  }

  while (size >= 160) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = g_malloc (33 * sizeof (gsm_byte));
    GST_BUFFER_SIZE (outbuf) = 33 * sizeof (gsm_byte);

    gsm_encode (gsmenc->state, data, (gsm_byte *) GST_BUFFER_DATA (outbuf));

    GST_BUFFER_TIMESTAMP (outbuf) = gsmenc->next_ts;
    gst_pad_push (gsmenc->srcpad, GST_DATA (outbuf));
    gsmenc->next_ts += (160 / gsmenc->rate) * GST_SECOND;

    size -= 160;
    data += 160;
  }

  if (size) {
    memcpy (gsmenc->buffer + gsmenc->bufsize, data, size * sizeof (gsm_signal));
    gsmenc->bufsize += size;
  }

  gst_buffer_unref(buf);
}
