/* GStreamer Adaptive Multi-Rate Wide-Band (AMR-WB) plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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

#include "gstamrwbenc.h"

static GstElementDetails gst_amrwbenc_details =
GST_ELEMENT_DETAILS ("AMR-WB audio encoder",
    "Codec/Encoder/Audio",
    "Adaptive Multi-Rate Wideband audio encoder",
    "Renato Araujo <renato.filho@indt.org.br>");


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR-WB, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

static void gst_amrwbenc_base_init (gpointer klass);
static void gst_amrwbenc_class_init (GstAmrwbEncClass * klass);
static void gst_amrwbenc_init (GstAmrwbEnc * amrwbenc,
    GstAmrwbEncClass * klass);
static void gst_amrwbenc_finalize (GObject * object);

static GstFlowReturn gst_amrwbenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrwbenc_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_amrwbenc_state_change (GstElement * element,
    GstStateChange transition);


GST_BOILERPLATE (GstAmrwbEnc, gst_amrwbenc, GstElement, GST_TYPE_ELEMENT)

     static void gst_amrwbenc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_amrwbenc_details);
}

static void
gst_amrwbenc_class_init (GstAmrwbEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->finalize = gst_amrwbenc_finalize;

  element_class->change_state = gst_amrwbenc_state_change;
}

static void
gst_amrwbenc_init (GstAmrwbEnc * amrwbenc, GstAmrwbEncClass * klass)
{
  /* create the sink pad */
  amrwbenc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_setcaps_function (amrwbenc->sinkpad, gst_amrwbenc_setcaps);
  gst_pad_set_chain_function (amrwbenc->sinkpad, gst_amrwbenc_chain);
  gst_element_add_pad (GST_ELEMENT (amrwbenc), amrwbenc->sinkpad);

  /* create the src pad */
  amrwbenc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_use_fixed_caps (amrwbenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrwbenc), amrwbenc->srcpad);

  amrwbenc->adapter = gst_adapter_new ();

  /* init rest */
  amrwbenc->handle = NULL;
  amrwbenc->channels = 0;
  amrwbenc->rate = 0;
  amrwbenc->ts = 0;
}

static void
gst_amrwbenc_finalize (GObject * object)
{
  GstAmrwbEnc *amrwbenc;

  amrwbenc = GST_AMRWBENC (object);

  g_object_unref (G_OBJECT (amrwbenc->adapter));
  amrwbenc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_amrwbenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrwbEnc *amrwbenc;
  GstCaps *copy;

  amrwbenc = GST_AMRWBENC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrwbenc->channels);
  gst_structure_get_int (structure, "rate", &amrwbenc->rate);

  /* this is not wrong but will sound bad */
  if (amrwbenc->channels != 1) {
    GST_WARNING ("amrwbdec is only optimized for mono channels");
  }
  if (amrwbenc->rate != 16000) {
    GST_WARNING ("amrwbdec is only optimized for 16000 Hz samplerate");
  }

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/AMR-WB",
      "channels", G_TYPE_INT, amrwbenc->channels,
      "rate", G_TYPE_INT, amrwbenc->rate, NULL);

  gst_pad_set_caps (amrwbenc->srcpad, copy);
  gst_caps_unref (copy);

  return TRUE;
}

static GstFlowReturn
gst_amrwbenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrwbEnc *amrwbenc;
  GstFlowReturn ret = GST_FLOW_OK;
  const int buffer_size = sizeof (Word16) * L_FRAME16k;

  amrwbenc = GST_AMRWBENC (gst_pad_get_parent (pad));

  g_return_val_if_fail (amrwbenc->handle, GST_FLOW_WRONG_STATE);

  if (amrwbenc->rate == 0 || amrwbenc->channels == 0) {
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrwbenc->ts = GST_BUFFER_TIMESTAMP (buffer);

  ret = GST_FLOW_OK;
  gst_adapter_push (amrwbenc->adapter, buffer);

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (amrwbenc->adapter) >= buffer_size) {
    GstBuffer *out;
    guint8 *data;
    gint outsize;

    out = gst_buffer_new_and_alloc (buffer_size);
    GST_BUFFER_DURATION (out) = GST_SECOND * L_FRAME16k /
        (amrwbenc->rate * amrwbenc->channels);
    GST_BUFFER_TIMESTAMP (out) = amrwbenc->ts;
    amrwbenc->ts += GST_BUFFER_DURATION (out);
    gst_buffer_set_caps (out, gst_pad_get_caps (amrwbenc->srcpad));

    data = (guint8 *) gst_adapter_peek (amrwbenc->adapter, buffer_size);

    /* encode */
    outsize = E_IF_encode (amrwbenc->handle, 0, (Word16 *) data,
        (UWord8 *) GST_BUFFER_DATA (out), 0);

    gst_adapter_flush (amrwbenc->adapter, buffer_size);
    GST_BUFFER_SIZE (out) = outsize;

    ret = gst_pad_push (amrwbenc->srcpad, out);
  }

done:

  gst_object_unref (amrwbenc);
  return ret;

}

static GstStateChangeReturn
gst_amrwbenc_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrwbEnc *amrwbenc;
  GstStateChangeReturn ret;

  amrwbenc = GST_AMRWBENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrwbenc->handle = E_IF_init ()))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      amrwbenc->ts = 0;
      gst_adapter_clear (amrwbenc->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      E_IF_exit (amrwbenc->handle);
      break;
    default:
      break;
  }

  return ret;
}
