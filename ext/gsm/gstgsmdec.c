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

#include "gstgsmdec.h"

/* elementfactory information */
GstElementDetails gst_gsmdec_details = {
  "GSM audio decoder",
  "Codec/Decoder/Audio",
  "Decodes GSM encoded audio",
  "Wim Taymans <wim.taymans@chello.be>",
};

/* GSMDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static void gst_gsmdec_base_init (gpointer g_class);
static void gst_gsmdec_class_init (GstGSMDec * klass);
static void gst_gsmdec_init (GstGSMDec * gsmdec);

static void gst_gsmdec_chain (GstPad * pad, GstData * _data);
static GstCaps *gst_gsmdec_getcaps (GstPad * pad);
static GstPadLinkReturn gst_gsmdec_link (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

/*static guint gst_gsmdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_gsmdec_get_type (void)
{
  static GType gsmdec_type = 0;

  if (!gsmdec_type) {
    static const GTypeInfo gsmdec_info = {
      sizeof (GstGSMDecClass),
      gst_gsmdec_base_init,
      NULL,
      (GClassInitFunc) gst_gsmdec_class_init,
      NULL,
      NULL,
      sizeof (GstGSMDec),
      0,
      (GInstanceInitFunc) gst_gsmdec_init,
    };

    gsmdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstGSMDec", &gsmdec_info, 0);
  }
  return gsmdec_type;
}

static GstStaticPadTemplate gsmdec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, "
        "rate = (int) [ 1000, 48000 ], " "channels = (int) 1")
    );

static GstStaticPadTemplate gsmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1000, 48000 ], " "channels = (int) 1")
    );

static void
gst_gsmdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gsmdec_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gsmdec_src_template));
  gst_element_class_set_details (element_class, &gst_gsmdec_details);
}

static void
gst_gsmdec_class_init (GstGSMDec * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void
gst_gsmdec_init (GstGSMDec * gsmdec)
{
  GST_DEBUG ("gst_gsmdec_init: initializing");

  /* create the sink and src pads */
  gsmdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gsmdec_sink_template), "sink");
  gst_pad_set_chain_function (gsmdec->sinkpad, gst_gsmdec_chain);
  gst_pad_set_getcaps_function (gsmdec->sinkpad, gst_gsmdec_getcaps);
  gst_pad_set_link_function (gsmdec->sinkpad, gst_gsmdec_link);
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->sinkpad);

  gsmdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gsmdec_src_template), "src");
  gst_pad_set_getcaps_function (gsmdec->srcpad, gst_gsmdec_getcaps);
  gst_pad_set_link_function (gsmdec->srcpad, gst_gsmdec_link);
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->srcpad);

  gsmdec->state = gsm_create ();
  gsmdec->bufsize = 0;
}

static GstCaps *
gst_gsmdec_getcaps (GstPad * pad)
{
  GstGSMDec *gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));
  const GValue *rate_value;
  GstPad *otherpad;
  GstCaps *othercaps, *basecaps;

  if (pad == gsmdec->srcpad) {
    otherpad = gsmdec->sinkpad;
    basecaps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
  } else {
    otherpad = gsmdec->srcpad;
    basecaps = gst_caps_new_simple ("audio/x-gsm", NULL);
  }

  othercaps = gst_pad_get_allowed_caps (otherpad);
  rate_value = gst_structure_get_value (gst_caps_get_structure (othercaps,
          0), "rate");
  gst_structure_set_value (gst_caps_get_structure (basecaps, 0), "rate",
      rate_value);

  return basecaps;
}

static GstPadLinkReturn
gst_gsmdec_link (GstPad * pad, const GstCaps * caps)
{
  GstGSMDec *gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *othercaps;
  gint rate;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "rate", &rate);

  if (pad == gsmdec->sinkpad) {
    otherpad = gsmdec->srcpad;
    othercaps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
  } else {
    otherpad = gsmdec->sinkpad;
    othercaps = gst_caps_new_simple ("audio/x-gsm", NULL);
  }

  gst_caps_set_simple (othercaps,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, 1, NULL);

  return gst_pad_try_set_caps (otherpad, othercaps);
}

static void
gst_gsmdec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstGSMDec *gsmdec;
  gsm_byte *data;
  guint size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  /*g_return_if_fail(GST_IS_BUFFER(buf)); */

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  data = (gsm_byte *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (gsmdec->bufsize && (gsmdec->bufsize + size >= 33)) {
    GstBuffer *outbuf;

    memcpy (gsmdec->buffer + gsmdec->bufsize, data,
        (33 - gsmdec->bufsize) * sizeof (gsm_byte));

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = g_malloc (160 * sizeof (gsm_signal));
    GST_BUFFER_SIZE (outbuf) = 160 * sizeof (gsm_signal);

    gsm_decode (gsmdec->state, gsmdec->buffer,
        (gsm_signal *) GST_BUFFER_DATA (outbuf));

    gst_pad_push (gsmdec->srcpad, GST_DATA (outbuf));

    size -= (33 - gsmdec->bufsize);
    data += (33 - gsmdec->bufsize);
    gsmdec->bufsize = 0;
  }

  while (size >= 33) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (160 * sizeof (gsm_signal));
    gsm_decode (gsmdec->state, data, (gsm_signal *) GST_BUFFER_DATA (outbuf));
    gst_pad_push (gsmdec->srcpad, GST_DATA (outbuf));

    size -= 33;
    data += 33;
  }

  if (size) {
    memcpy (gsmdec->buffer + gsmdec->bufsize, data, size * sizeof (gsm_byte));
    gsmdec->bufsize += size;
  }

  gst_buffer_unref (buf);
}
