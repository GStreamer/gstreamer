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
 * SECTION:element-mulawenc
 *
 * This element encode mulaw audio. Mulaw coding is also known as G.711.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include "mulaw-encode.h"
#include "mulaw-conversion.h"

extern GstStaticPadTemplate mulaw_enc_src_factory;
extern GstStaticPadTemplate mulaw_enc_sink_factory;

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

static gboolean gst_mulawenc_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mulawenc_chain (GstPad * pad, GstBuffer * buffer);

#define gst_mulawenc_parent_class parent_class
G_DEFINE_TYPE (GstMuLawEnc, gst_mulawenc, GST_TYPE_ELEMENT);

/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

static GstCaps *
mulawenc_getcaps (GstPad * pad, GstCaps * filter)
{
  GstMuLawEnc *mulawenc;
  GstPad *otherpad;
  GstCaps *othercaps, *result;
  const GstCaps *templ;
  const gchar *name;
  gint i;

  mulawenc = GST_MULAWENC (GST_PAD_PARENT (pad));

  /* figure out the name of the caps we are going to return */
  if (pad == mulawenc->srcpad) {
    name = "audio/x-mulaw";
    otherpad = mulawenc->sinkpad;
  } else {
    name = "audio/x-raw-int";
    otherpad = mulawenc->srcpad;
  }
  /* get caps from the peer, this can return NULL when there is no peer */
  othercaps = gst_pad_peer_get_caps (otherpad, filter);

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

      if (pad == mulawenc->srcpad) {
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

static gboolean
mulawenc_setcaps (GstMuLawEnc * mulawenc, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *base_caps;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &mulawenc->channels);
  gst_structure_get_int (structure, "rate", &mulawenc->rate);

  base_caps = gst_caps_copy (gst_pad_get_pad_template_caps (mulawenc->srcpad));

  structure = gst_caps_get_structure (base_caps, 0);
  gst_structure_set (structure, "rate", G_TYPE_INT, mulawenc->rate, NULL);
  gst_structure_set (structure, "channels", G_TYPE_INT, mulawenc->channels,
      NULL);

  gst_pad_set_caps (mulawenc->srcpad, base_caps);

  gst_caps_unref (base_caps);

  return TRUE;
}

static void
gst_mulawenc_class_init (GstMuLawEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mulaw_enc_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mulaw_enc_sink_factory));

  gst_element_class_set_details_simple (element_class, "Mu Law audio encoder",
      "Codec/Encoder/Audio",
      "Convert 16bit PCM to 8bit mu law",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");
}

static void
gst_mulawenc_init (GstMuLawEnc * mulawenc)
{
  mulawenc->sinkpad =
      gst_pad_new_from_static_template (&mulaw_enc_sink_factory, "sink");
  gst_pad_set_getcaps_function (mulawenc->sinkpad, mulawenc_getcaps);
  gst_pad_set_event_function (mulawenc->sinkpad, gst_mulawenc_event);
  gst_pad_set_chain_function (mulawenc->sinkpad, gst_mulawenc_chain);
  gst_element_add_pad (GST_ELEMENT (mulawenc), mulawenc->sinkpad);

  mulawenc->srcpad =
      gst_pad_new_from_static_template (&mulaw_enc_src_factory, "src");
  gst_pad_set_getcaps_function (mulawenc->srcpad, mulawenc_getcaps);
  gst_pad_use_fixed_caps (mulawenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (mulawenc), mulawenc->srcpad);

  /* init rest */
  mulawenc->channels = 0;
  mulawenc->rate = 0;
}

static gboolean
gst_mulawenc_event (GstPad * pad, GstEvent * event)
{
  GstMuLawEnc *mulawenc;
  gboolean res;

  mulawenc = GST_MULAWENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      mulawenc_setcaps (mulawenc, caps);
      gst_event_unref (event);

      res = TRUE;
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_mulawenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMuLawEnc *mulawenc;
  gint16 *linear_data;
  gsize linear_size;
  guint8 *mulaw_data;
  guint mulaw_size;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  GstClockTime timestamp, duration;

  mulawenc = GST_MULAWENC (gst_pad_get_parent (pad));

  if (!mulawenc->rate || !mulawenc->channels)
    goto not_negotiated;

  linear_data = gst_buffer_map (buffer, &linear_size, NULL, GST_MAP_READ);

  mulaw_size = linear_size / 2;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  outbuf = gst_buffer_new_allocate (NULL, mulaw_size, 0);

  if (duration == -1) {
    duration = gst_util_uint64_scale_int (mulaw_size,
        GST_SECOND, mulawenc->rate * mulawenc->channels);
  }

  mulaw_data = gst_buffer_map (outbuf, NULL, NULL, GST_MAP_WRITE);

  /* copy discont flag */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;

  mulaw_encode (linear_data, mulaw_data, mulaw_size);

  gst_buffer_unmap (outbuf, mulaw_data, -1);
  gst_buffer_unmap (buffer, linear_data, -1);
  gst_buffer_unref (buffer);

  ret = gst_pad_push (mulawenc->srcpad, outbuf);

done:
  gst_object_unref (mulawenc);

  return ret;

not_negotiated:
  {
    GST_DEBUG_OBJECT (mulawenc, "no format negotiated");
    ret = GST_FLOW_NOT_NEGOTIATED;
    gst_buffer_unref (buffer);
    goto done;
  }
}
