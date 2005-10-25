/*
 * Farsight
 * GStreamer GSM decoder
 * Copyright (C) 2005 Philippe Khalaf <burger@speedy.org>
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

GST_DEBUG_CATEGORY (gsmdec_debug);
#define GST_CAT_DEFAULT (gsmdec_debug)

/* elementfactory information */
GstElementDetails gst_gsmdec_details = {
  "GSM audio decoder",
  "Codec/Decoder/Audio",
  "Decodes GSM encoded audio",
  "Philippe Khalaf <burger@speedy.org>",
};

/* GSMDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  /* FILL ME */
  ARG_0
};

static void gst_gsmdec_base_init (gpointer g_class);
static void gst_gsmdec_class_init (GstGSMDec * klass);
static void gst_gsmdec_init (GstGSMDec * gsmdec);

static GstFlowReturn gst_gsmdec_chain (GstPad * pad, GstBuffer * buf);

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

  GST_DEBUG_CATEGORY_INIT (gsmdec_debug, "gsmdec", 0, "GSM Decoder");
}

static void
gst_gsmdec_init (GstGSMDec * gsmdec)
{
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
  // turn on WAN49 handling
  gint use_wav49 = 0;

  gsm_option (gsmdec->state, GSM_OPT_WAV49, &use_wav49);

  gsmdec->next_of = 0;
  gsmdec->next_ts = 0;
}

static GstFlowReturn
gst_gsmdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstGSMDec *gsmdec;
  gsm_byte *data;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);

  gsmdec = GST_GSMDEC (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_IS_GSMDEC (gsmdec), GST_FLOW_ERROR);

  g_return_val_if_fail (GST_PAD_IS_LINKED (gsmdec->srcpad), GST_FLOW_ERROR);

  // do we have enough bytes to read a header
  if (GST_BUFFER_SIZE (buf) >= 33) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (160 * sizeof (gsm_signal));
    // TODO take new segment in consideration, if not given restart
    // timestamps at 0
    if (GST_BUFFER_TIMESTAMP (buf) == GST_CLOCK_TIME_NONE) {
      /* If we are not given any timestamp */
      GST_BUFFER_TIMESTAMP (outbuf) = gsmdec->next_ts;
      gsmdec->next_ts += 20 * GST_MSECOND;
    }

    else {
      /* But if you insist on giving us a timestamp, you are welcome. */
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    }

    GST_BUFFER_DURATION (outbuf) = 20 * GST_MSECOND;
    GST_BUFFER_OFFSET (outbuf) = gsmdec->next_of;
    GST_BUFFER_OFFSET_END (outbuf) = gsmdec->next_of + 160 - 1;
    gst_buffer_set_caps (outbuf, gst_pad_get_caps (gsmdec->srcpad));
    gsmdec->next_of += 160;

    data = (gsm_byte *) GST_BUFFER_DATA (buf);
    gsm_decode (gsmdec->state, data, (gsm_signal *) GST_BUFFER_DATA (outbuf));

    GST_DEBUG ("Pushing buffer of size %d ts %" GST_TIME_FORMAT,
        GST_BUFFER_SIZE (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
    //gst_util_dump_mem (GST_BUFFER_DATA(outbuf), GST_BUFFER_SIZE (outbuf));
    gst_pad_push (gsmdec->srcpad, outbuf);
  }

  gst_buffer_unref (buf);

  return GST_FLOW_OK;
}
