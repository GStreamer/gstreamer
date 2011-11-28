/*
 * Farsight
 * GStreamer GSM encoder
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

#include "gstgsmenc.h"

GST_DEBUG_CATEGORY_STATIC (gsmenc_debug);
#define GST_CAT_DEFAULT (gsmenc_debug)

/* GSMEnc signals and args */
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

static void gst_gsmenc_base_init (gpointer g_class);
static void gst_gsmenc_class_init (GstGSMEnc * klass);
static void gst_gsmenc_init (GstGSMEnc * gsmenc);
static void gst_gsmenc_finalize (GObject * object);

static gboolean gst_gsmenc_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_gsmenc_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

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

    gsmenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstGSMEnc", &gsmenc_info, 0);
  }
  return gsmenc_type;
}

static GstStaticPadTemplate gsmenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate gsmenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 8000, " "channels = (int) 1")
    );

static void
gst_gsmenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gsmenc_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gsmenc_src_template);
  gst_element_class_set_details_simple (element_class, "GSM audio encoder",
      "Codec/Encoder/Audio",
      "Encodes GSM audio", "Philippe Khalaf <burger@speedy.org>");
}

static void
gst_gsmenc_class_init (GstGSMEnc * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_gsmenc_finalize;

  GST_DEBUG_CATEGORY_INIT (gsmenc_debug, "gsmenc", 0, "GSM Encoder");
}

static void
gst_gsmenc_init (GstGSMEnc * gsmenc)
{
  gint use_wav49;

  /* create the sink and src pads */
  gsmenc->sinkpad =
      gst_pad_new_from_static_template (&gsmenc_sink_template, "sink");
  gst_pad_set_chain_function (gsmenc->sinkpad, gst_gsmenc_chain);
  gst_pad_set_setcaps_function (gsmenc->sinkpad, gst_gsmenc_setcaps);
  gst_element_add_pad (GST_ELEMENT (gsmenc), gsmenc->sinkpad);

  gsmenc->srcpad =
      gst_pad_new_from_static_template (&gsmenc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (gsmenc), gsmenc->srcpad);

  gsmenc->state = gsm_create ();

  /* turn off WAV49 handling */
  use_wav49 = 0;
  gsm_option (gsmenc->state, GSM_OPT_WAV49, &use_wav49);

  gsmenc->adapter = gst_adapter_new ();
  gsmenc->next_ts = 0;
}

static void
gst_gsmenc_finalize (GObject * object)
{
  GstGSMEnc *gsmenc;

  gsmenc = GST_GSMENC (object);

  g_object_unref (gsmenc->adapter);
  gsm_destroy (gsmenc->state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gsmenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstGSMEnc *gsmenc;
  GstCaps *srccaps;

  gsmenc = GST_GSMENC (gst_pad_get_parent (pad));

  srccaps = gst_static_pad_template_get_caps (&gsmenc_src_template);

  gst_pad_set_caps (gsmenc->srcpad, srccaps);

  gst_object_unref (gsmenc);

  return TRUE;
}


static GstFlowReturn
gst_gsmenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstGSMEnc *gsmenc;
  gsm_signal *data;
  GstFlowReturn ret = GST_FLOW_OK;

  gsmenc = GST_GSMENC (gst_pad_get_parent (pad));

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (gsmenc->adapter);
  }
  gst_adapter_push (gsmenc->adapter, buf);

  while (gst_adapter_available (gsmenc->adapter) >= 320) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (33 * sizeof (gsm_byte));

    GST_BUFFER_TIMESTAMP (outbuf) = gsmenc->next_ts;
    GST_BUFFER_DURATION (outbuf) = 20 * GST_MSECOND;
    gsmenc->next_ts += 20 * GST_MSECOND;

    /* encode 160 16-bit samples into 33 bytes */
    data = (gsm_signal *) gst_adapter_peek (gsmenc->adapter, 320);
    gsm_encode (gsmenc->state, data, (gsm_byte *) GST_BUFFER_DATA (outbuf));
    gst_adapter_flush (gsmenc->adapter, 320);

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (gsmenc->srcpad));

    GST_DEBUG_OBJECT (gsmenc, "Pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    ret = gst_pad_push (gsmenc->srcpad, outbuf);
  }

  gst_object_unref (gsmenc);

  return ret;
}
