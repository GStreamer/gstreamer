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
/**
 * SECTION:element-mulawdec
 *
 * This element decodes mulaw audio. Mulaw coding is also known as G.711.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include "mulaw-decode.h"
#include "mulaw-conversion.h"

extern GstStaticPadTemplate mulaw_dec_src_factory;
extern GstStaticPadTemplate mulaw_dec_sink_factory;

/* Stereo signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_mulawdec_class_init (GstMuLawDecClass * klass);
static void gst_mulawdec_base_init (GstMuLawDecClass * klass);
static void gst_mulawdec_init (GstMuLawDec * mulawdec);
static GstStateChangeReturn
gst_mulawdec_change_state (GstElement * element, GstStateChange transition);

static GstFlowReturn gst_mulawdec_chain (GstPad * pad, GstBuffer * buffer);

static GstElementClass *parent_class = NULL;

static gboolean
mulawdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMuLawDec *mulawdec;
  GstStructure *structure;
  int rate, channels;
  gboolean ret;
  GstCaps *outcaps;

  mulawdec = GST_MULAWDEC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &rate);
  ret = ret && gst_structure_get_int (structure, "channels", &channels);
  if (!ret)
    return FALSE;

  outcaps = gst_caps_new_simple ("audio/x-raw-int",
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);
  ret = gst_pad_set_caps (mulawdec->srcpad, outcaps);
  gst_caps_unref (outcaps);

  if (ret) {
    GST_DEBUG_OBJECT (mulawdec, "rate=%d, channels=%d", rate, channels);
    mulawdec->rate = rate;
    mulawdec->channels = channels;
  }
  return ret;
}

static GstCaps *
mulawdec_getcaps (GstPad * pad)
{
  GstMuLawDec *mulawdec;
  GstPad *otherpad;
  GstCaps *othercaps, *result;
  const GstCaps *templ;
  const gchar *name;
  gint i;

  mulawdec = GST_MULAWDEC (GST_PAD_PARENT (pad));

  /* figure out the name of the caps we are going to return */
  if (pad == mulawdec->srcpad) {
    name = "audio/x-raw-int";
    otherpad = mulawdec->sinkpad;
  } else {
    name = "audio/x-mulaw";
    otherpad = mulawdec->srcpad;
  }
  /* get caps from the peer, this can return NULL when there is no peer */
  othercaps = gst_pad_peer_get_caps (otherpad);

  /* get the template caps to make sure we return something acceptable */
  templ = gst_pad_get_pad_template_caps (pad);

  if (othercaps) {
    /* there was a peer */
    othercaps = gst_caps_make_writable (othercaps);

    /* go through the caps and remove the fields we don't want */
    for (i = 0; i < gst_caps_get_size (othercaps); i++) {
      GstStructure *structure;

      structure = gst_caps_get_structure (othercaps, i);

      /* adjust the name */
      gst_structure_set_name (structure, name);

      if (pad == mulawdec->sinkpad) {
        /* remove the fields we don't want */
        gst_structure_remove_fields (structure, "width", "depth", "endianness",
            "signed", NULL);
      } else {
        /* add fixed fields */
        gst_structure_set (structure, "width", G_TYPE_INT, 16,
            "depth", G_TYPE_INT, 16,
            "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      }
    }
    /* filter against the allowed caps of the pad to return our result */
    result = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);
  } else {
    /* there was no peer, return the template caps */
    result = gst_caps_copy (templ);
  }
  return result;
}

GType
gst_mulawdec_get_type (void)
{
  static GType mulawdec_type = 0;

  if (!mulawdec_type) {
    static const GTypeInfo mulawdec_info = {
      sizeof (GstMuLawDecClass),
      (GBaseInitFunc) gst_mulawdec_base_init,
      NULL,
      (GClassInitFunc) gst_mulawdec_class_init,
      NULL,
      NULL,
      sizeof (GstMuLawDec),
      0,
      (GInstanceInitFunc) gst_mulawdec_init,
    };

    mulawdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMuLawDec", &mulawdec_info,
        0);
  }
  return mulawdec_type;
}

static void
gst_mulawdec_base_init (GstMuLawDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &mulaw_dec_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &mulaw_dec_sink_factory);
  gst_element_class_set_details_simple (element_class, "Mu Law audio decoder",
      "Codec/Decoder/Audio",
      "Convert 8bit mu law to 16bit PCM",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");
}

static void
gst_mulawdec_class_init (GstMuLawDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mulawdec_change_state);
}

static void
gst_mulawdec_init (GstMuLawDec * mulawdec)
{
  mulawdec->sinkpad =
      gst_pad_new_from_static_template (&mulaw_dec_sink_factory, "sink");
  gst_pad_set_setcaps_function (mulawdec->sinkpad, mulawdec_sink_setcaps);
  gst_pad_set_getcaps_function (mulawdec->sinkpad, mulawdec_getcaps);
  gst_pad_set_chain_function (mulawdec->sinkpad, gst_mulawdec_chain);
  gst_element_add_pad (GST_ELEMENT (mulawdec), mulawdec->sinkpad);

  mulawdec->srcpad =
      gst_pad_new_from_static_template (&mulaw_dec_src_factory, "src");
  gst_pad_use_fixed_caps (mulawdec->srcpad);
  gst_pad_set_getcaps_function (mulawdec->srcpad, mulawdec_getcaps);
  gst_element_add_pad (GST_ELEMENT (mulawdec), mulawdec->srcpad);
}

static GstFlowReturn
gst_mulawdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMuLawDec *mulawdec;
  gint16 *linear_data;
  guint8 *mulaw_data;
  guint mulaw_size;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  mulawdec = GST_MULAWDEC (GST_PAD_PARENT (pad));

  if (G_UNLIKELY (mulawdec->rate == 0))
    goto not_negotiated;

  mulaw_data = (guint8 *) GST_BUFFER_DATA (buffer);
  mulaw_size = GST_BUFFER_SIZE (buffer);

  ret =
      gst_pad_alloc_buffer_and_set_caps (mulawdec->srcpad,
      GST_BUFFER_OFFSET_NONE, mulaw_size * 2, GST_PAD_CAPS (mulawdec->srcpad),
      &outbuf);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  linear_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  /* copy discont flag */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_BUFFER_DURATION (outbuf) == GST_CLOCK_TIME_NONE)
    GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale_int (GST_SECOND,
        mulaw_size * 2, 2 * mulawdec->rate * mulawdec->channels);
  else
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (mulawdec->srcpad));

  mulaw_decode (mulaw_data, linear_data, mulaw_size);

  gst_buffer_unref (buffer);

  ret = gst_pad_push (mulawdec->srcpad, outbuf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_WARNING_OBJECT (mulawdec, "no input format set: not-negotiated");
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
alloc_failed:
  {
    GST_DEBUG_OBJECT (mulawdec, "pad alloc failed, flow: %s",
        gst_flow_get_name (ret));
    gst_buffer_unref (buffer);
    return ret;
  }
}

static GstStateChangeReturn
gst_mulawdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMuLawDec *dec = GST_MULAWDEC (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dec->rate = 0;
      dec->channels = 0;
      break;
    default:
      break;
  }

  return ret;
}
