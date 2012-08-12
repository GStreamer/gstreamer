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

static GstStateChangeReturn
gst_mulawdec_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_mulawdec_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_mulawdec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

#define gst_mulawdec_parent_class parent_class
G_DEFINE_TYPE (GstMuLawDec, gst_mulawdec, GST_TYPE_ELEMENT);

static gboolean
mulawdec_setcaps (GstMuLawDec * mulawdec, GstCaps * caps)
{
  GstStructure *structure;
  int rate, channels;
  gboolean ret;
  GstCaps *outcaps;
  GstAudioInfo info;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &rate);
  ret = ret && gst_structure_get_int (structure, "channels", &channels);
  if (!ret)
    return FALSE;

  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, rate, channels, NULL);

  outcaps = gst_audio_info_to_caps (&info);
  ret = gst_pad_set_caps (mulawdec->srcpad, outcaps);
  gst_caps_unref (outcaps);

  if (ret) {
    GST_DEBUG_OBJECT (mulawdec, "rate=%d, channels=%d", rate, channels);
    mulawdec->info = info;
  }
  return ret;
}

static GstCaps *
mulawdec_getcaps (GstPad * pad, GstCaps * filter)
{
  GstMuLawDec *mulawdec;
  GstPad *otherpad;
  GstCaps *othercaps, *result;
  GstCaps *templ;
  const gchar *name;
  gint i;

  mulawdec = GST_MULAWDEC (GST_PAD_PARENT (pad));

  /* figure out the name of the caps we are going to return */
  if (pad == mulawdec->srcpad) {
    name = "audio/x-raw";
    otherpad = mulawdec->sinkpad;
  } else {
    name = "audio/x-mulaw";
    otherpad = mulawdec->srcpad;
  }
  /* get caps from the peer, this can return NULL when there is no peer */
  othercaps = gst_pad_peer_query_caps (otherpad, NULL);

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
        gst_structure_remove_fields (structure, "format", "layout", NULL);
      } else {
        /* add fixed fields */
        gst_structure_set (structure, "format", G_TYPE_STRING,
            GST_AUDIO_NE (S16), "layout", G_TYPE_STRING, "interleaved", NULL);
      }
    }
    /* filter against the allowed caps of the pad to return our result */
    result = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);
    gst_caps_unref (templ);
  } else {
    /* there was no peer, return the template caps */
    result = templ;
  }
  if (filter && result) {
    GstCaps *temp;

    temp = gst_caps_intersect (result, filter);
    gst_caps_unref (result);
    result = temp;
  }
  return result;
}

static gboolean
gst_mulawdec_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = mulawdec_getcaps (pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static void
gst_mulawdec_class_init (GstMuLawDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mulaw_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mulaw_dec_sink_factory));

  gst_element_class_set_static_metadata (element_class, "Mu Law audio decoder",
      "Codec/Decoder/Audio",
      "Convert 8bit mu law to 16bit PCM",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mulawdec_change_state);
}

static void
gst_mulawdec_init (GstMuLawDec * mulawdec)
{
  mulawdec->sinkpad =
      gst_pad_new_from_static_template (&mulaw_dec_sink_factory, "sink");
  gst_pad_set_query_function (mulawdec->sinkpad, gst_mulawdec_query);
  gst_pad_set_event_function (mulawdec->sinkpad, gst_mulawdec_event);
  gst_pad_set_chain_function (mulawdec->sinkpad, gst_mulawdec_chain);
  gst_element_add_pad (GST_ELEMENT (mulawdec), mulawdec->sinkpad);

  mulawdec->srcpad =
      gst_pad_new_from_static_template (&mulaw_dec_src_factory, "src");
  gst_pad_set_query_function (mulawdec->srcpad, gst_mulawdec_query);
  gst_element_add_pad (GST_ELEMENT (mulawdec), mulawdec->srcpad);
}

static gboolean
gst_mulawdec_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMuLawDec *mulawdec;
  gboolean res;

  mulawdec = GST_MULAWDEC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      mulawdec_setcaps (mulawdec, caps);
      gst_event_unref (event);

      res = TRUE;
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_mulawdec_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstMuLawDec *mulawdec;
  GstMapInfo inmap, outmap;
  gint16 *linear_data;
  guint8 *mulaw_data;
  gsize mulaw_size, linear_size;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  mulawdec = GST_MULAWDEC (parent);

  if (G_UNLIKELY (!GST_AUDIO_INFO_IS_VALID (&mulawdec->info)))
    goto not_negotiated;

  gst_buffer_map (buffer, &inmap, GST_MAP_READ);
  mulaw_data = inmap.data;
  mulaw_size = inmap.size;

  linear_size = mulaw_size * 2;

  outbuf = gst_buffer_new_allocate (NULL, linear_size, NULL);
  gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
  linear_data = (gint16 *) outmap.data;

  /* copy discont flag */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);
  } else {
    GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale_int (GST_SECOND,
        linear_size, GST_AUDIO_INFO_RATE (&mulawdec->info) *
        GST_AUDIO_INFO_BPF (&mulawdec->info));
  }

  mulaw_decode (mulaw_data, linear_data, mulaw_size);

  gst_buffer_unmap (outbuf, &outmap);
  gst_buffer_unmap (buffer, &inmap);
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
      gst_audio_info_init (&dec->info);
      break;
    default:
      break;
  }

  return ret;
}
