/* GStreamer Adaptive Multi-Rate Narrow-Band (AMR-NB) plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include "gstamrwbdec.h"

static GstElementDetails gst_amrwbdec_details =
GST_ELEMENT_DETAILS ("AMR-WB audio decoder",
    "Codec/Decoder/Audio",
    "Adaptive Multi-Rate Wideband audio decoder",
    "Renato Araujo <renato.filho@indt.org.br>");


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR-WB, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) 16000, " "channels = (int) 1")
    );

extern const UWord8 block_size[];

static void gst_amrwbdec_base_init (gpointer klass);
static void gst_amrwbdec_class_init (GstAmrwbDecClass * klass);
static void gst_amrwbdec_init (GstAmrwbDec * amrwbdec,
    GstAmrwbDecClass * klass);

static gboolean gst_amrwbdec_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_amrwbdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_amrwbdec_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_amrwbdec_state_change (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstAmrwbDec, gst_amrwbdec, GstElement, GST_TYPE_ELEMENT)

     static void gst_amrwbdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_amrwbdec_details);
}

static void
gst_amrwbdec_class_init (GstAmrwbDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  element_class->change_state = gst_amrwbdec_state_change;
}

static void
gst_amrwbdec_init (GstAmrwbDec * amrwbdec, GstAmrwbDecClass * klass)
{
  /* create the sink pad */
  amrwbdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_pad_set_setcaps_function (amrwbdec->sinkpad, gst_amrwbdec_setcaps);
  gst_pad_set_event_function (amrwbdec->sinkpad, gst_amrwbdec_event);
  gst_pad_set_chain_function (amrwbdec->sinkpad, gst_amrwbdec_chain);
  gst_element_add_pad (GST_ELEMENT (amrwbdec), amrwbdec->sinkpad);

  /* create the src pad */
  amrwbdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_use_fixed_caps (amrwbdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (amrwbdec), amrwbdec->srcpad);

  amrwbdec->adapter = gst_adapter_new ();

  /* init rest */
  amrwbdec->handle = NULL;
  amrwbdec->channels = 0;
  amrwbdec->rate = 0;
  amrwbdec->duration = 0;
  amrwbdec->ts = -1;
}

static gboolean
gst_amrwbdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstAmrwbDec *amrwbdec;
  GstCaps *copy;

  amrwbdec = GST_AMRWBDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &amrwbdec->channels);
  gst_structure_get_int (structure, "rate", &amrwbdec->rate);

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-raw-int",
      "channels", G_TYPE_INT, amrwbdec->channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", G_TYPE_INT, amrwbdec->rate, "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  amrwbdec->duration = gst_util_uint64_scale_int (GST_SECOND, L_FRAME16k,
      amrwbdec->rate * amrwbdec->channels);

  gst_pad_set_caps (amrwbdec->srcpad, copy);
  gst_caps_unref (copy);

  gst_object_unref (amrwbdec);

  return TRUE;
}

static gboolean
gst_amrwbdec_event (GstPad * pad, GstEvent * event)
{
  GstAmrwbDec *amrwbdec;
  gboolean ret = TRUE;

  amrwbdec = GST_AMRWBDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      gst_adapter_clear (amrwbdec->adapter);
      amrwbdec->ts = -1;
      break;
    case GST_EVENT_EOS:
      gst_adapter_clear (amrwbdec->adapter);
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      break;
    default:
      ret = gst_pad_push_event (amrwbdec->srcpad, event);
      break;
  }
  gst_object_unref (amrwbdec);

  return ret;
}

static GstFlowReturn
gst_amrwbdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAmrwbDec *amrwbdec;
  GstFlowReturn ret = GST_FLOW_OK;

  amrwbdec = GST_AMRWBDEC (gst_pad_get_parent (pad));

  if (amrwbdec->rate == 0 || amrwbdec->channels == 0) {
    GST_ELEMENT_ERROR (amrwbdec, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Decoder is not initialized"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }


  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    amrwbdec->ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (amrwbdec->adapter, buffer);

  while (TRUE) {
    GstBuffer *out;
    UWord8 *data;
    Word16 block, mode;


    if (gst_adapter_available (amrwbdec->adapter) < 1)
      break;

    data = (UWord8 *) gst_adapter_peek (amrwbdec->adapter, 1);

    /* get size */
    mode = (Word16) (data[0] >> 3) & 0x0F;
    block = block_size[mode];

    if (gst_adapter_available (amrwbdec->adapter) < block) {
      break;
    }

    /* the library seems to write into the source data, hence
     * the copy. */
    data = (UWord8 *) gst_adapter_take (amrwbdec->adapter, block);

    /* get output */
    out = gst_buffer_new_and_alloc (sizeof (Word16) * L_FRAME16k);

    GST_BUFFER_DURATION (out) = amrwbdec->duration;
    GST_BUFFER_TIMESTAMP (out) = amrwbdec->ts;

    if (amrwbdec->ts != -1)
      amrwbdec->ts += GST_BUFFER_DURATION (out);

    gst_buffer_set_caps (out, GST_PAD_CAPS (amrwbdec->srcpad));

    /* decode */
    D_IF_decode (amrwbdec->handle, data,
        (Word16 *) GST_BUFFER_DATA (out), _good_frame);

    g_free (data);

    /* play */
    ret = gst_pad_push (amrwbdec->srcpad, out);
  }

done:

  gst_object_unref (amrwbdec);
  return ret;

}

static GstStateChangeReturn
gst_amrwbdec_state_change (GstElement * element, GstStateChange transition)
{
  GstAmrwbDec *amrwbdec;
  GstStateChangeReturn ret;

  amrwbdec = GST_AMRWBDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(amrwbdec->handle = D_IF_init ()))
        goto init_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (amrwbdec->adapter);
      amrwbdec->rate = 0;
      amrwbdec->channels = 0;
      amrwbdec->ts = -1;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      D_IF_exit (amrwbdec->handle);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (amrwbdec, LIBRARY, INIT, (NULL),
        ("Failed to open AMR Decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }
}
