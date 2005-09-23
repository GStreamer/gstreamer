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
  ARG_0
      /* FILL ME */
};

static void gst_gsmdec_base_init (gpointer g_class);
static void gst_gsmdec_class_init (GstGSMDec * klass);
static void gst_gsmdec_init (GstGSMDec * gsmdec);

static GstFlowReturn gst_gsmdec_chain (GstPad * pad, GstBuffer * buffer);

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
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate gsmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 8000, " "channels = (int) 1")
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
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->sinkpad);

  gsmdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gsmdec_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (gsmdec), gsmdec->srcpad);

  gsmdec->state = gsm_create ();
  gsmdec->bufsize = 0;
  gsmdec->next_ts = 0;
  gsmdec->next_of = 0;
}

static GstFlowReturn
gst_gsmdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstGSMDec *gsmdec;

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));

  gsm_byte *data = (gsm_byte *) GST_BUFFER_DATA (buf);
  guint size = GST_BUFFER_SIZE (buf);

  if (gsmdec->bufsize && (gsmdec->bufsize + size >= 33)) {
    GstBuffer *outbuf;

    memcpy (gsmdec->buffer + gsmdec->bufsize, data,
        (33 - gsmdec->bufsize) * sizeof (gsm_byte));

    outbuf = gst_buffer_new_and_alloc (160 * sizeof (gsm_signal));
    GST_BUFFER_TIMESTAMP (outbuf) = gsmdec->next_ts;
    GST_BUFFER_DURATION (outbuf) = 20 * GST_MSECOND;
    GST_BUFFER_OFFSET (outbuf) = gsmdec->next_of;
    GST_BUFFER_OFFSET_END (outbuf) = gsmdec->next_of + 160 - 1;
    gst_buffer_set_caps (outbuf, gst_pad_get_caps (gsmdec->srcpad));

    gsmdec->next_ts += 20 * GST_MSECOND;
    gsmdec->next_of += 160;

    gsm_decode (gsmdec->state, gsmdec->buffer,
        (gsm_signal *) GST_BUFFER_DATA (outbuf));

    gst_pad_push (gsmdec->srcpad, outbuf);

    size -= (33 - gsmdec->bufsize);
    data += (33 - gsmdec->bufsize);
    gsmdec->bufsize = 0;
  }

  while (size >= 33) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (160 * sizeof (gsm_signal));
    GST_BUFFER_TIMESTAMP (outbuf) = gsmdec->next_ts;
    GST_BUFFER_DURATION (outbuf) = 20 * GST_MSECOND;
    GST_BUFFER_OFFSET (outbuf) = gsmdec->next_of;
    GST_BUFFER_OFFSET_END (outbuf) = gsmdec->next_of + 160 - 1;
    gst_buffer_set_caps (outbuf, gst_pad_get_caps (gsmdec->srcpad));

    gsmdec->next_ts += 20 * GST_MSECOND;
    gsmdec->next_of += 160;

    gsm_decode (gsmdec->state, data, (gsm_signal *) GST_BUFFER_DATA (outbuf));
    gst_pad_push (gsmdec->srcpad, outbuf);

    size -= 33;
    data += 33;
  }

  if (size) {
    memcpy (gsmdec->buffer + gsmdec->bufsize, data, size * sizeof (gsm_byte));
    gsmdec->bufsize += size;
  }

  return GST_FLOW_OK;

}
