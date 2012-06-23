/* GStreamer Smart Video Encoder element
 * Copyright (C) <2010> Edward Hervey <bilboed@gmail.com>
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

/* TODO:
 * * Implement get_caps/set_caps (store/forward caps)
 * * Adjust template caps to the formats we can support
 **/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstsmartencoder.h"

GST_DEBUG_CATEGORY_STATIC (smart_encoder_debug);
#define GST_CAT_DEFAULT smart_encoder_debug

/* FIXME : Update this with new caps */
/* WARNING : We can only allow formats with closed-GOP */
#define ALLOWED_CAPS "video/x-h263;video/x-intel-h263;"\
  "video/mpeg,mpegversion=(int)1,systemstream=(boolean)false;"\
  "video/mpeg,mpegversion=(int)2,systemstream=(boolean)false;"

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (ALLOWED_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (ALLOWED_CAPS)
    );

static GQuark INTERNAL_ELEMENT;

/* GstSmartEncoder signals and args */
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

static void
_do_init (void)
{
  INTERNAL_ELEMENT = g_quark_from_static_string ("internal-element");
};

G_DEFINE_TYPE_EXTENDED (GstSmartEncoder, gst_smart_encoder, GST_TYPE_ELEMENT, 0,
    _do_init ());

static void gst_smart_encoder_dispose (GObject * object);

static gboolean setup_recoder_pipeline (GstSmartEncoder * smart_encoder);

static GstFlowReturn gst_smart_encoder_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean smart_encoder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean smart_encoder_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstCaps *smart_encoder_sink_getcaps (GstPad * pad, GstCaps * filter);
static GstStateChangeReturn
gst_smart_encoder_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_smart_encoder_class_init (GstSmartEncoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  element_class = (GstElementClass *) klass;
  gobject_class = G_OBJECT_CLASS (klass);

  gst_smart_encoder_parent_class = g_type_class_peek_parent (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (element_class, "Smart Video Encoder",
      "Codec/Recoder/Video",
      "Re-encodes portions of Video that lay on segment boundaries",
      "Edward Hervey <bilboed@gmail.com>");

  gobject_class->dispose = (GObjectFinalizeFunc) (gst_smart_encoder_dispose);
  element_class->change_state = gst_smart_encoder_change_state;

  GST_DEBUG_CATEGORY_INIT (smart_encoder_debug, "smartencoder", 0,
      "Smart Encoder");
}

static void
smart_encoder_reset (GstSmartEncoder * smart_encoder)
{
  gst_segment_init (smart_encoder->segment, GST_FORMAT_UNDEFINED);

  if (smart_encoder->encoder) {
    /* Clean up/remove elements */
    gst_element_set_state (smart_encoder->encoder, GST_STATE_NULL);
    gst_element_set_state (smart_encoder->decoder, GST_STATE_NULL);
    gst_element_set_bus (smart_encoder->encoder, NULL);
    gst_element_set_bus (smart_encoder->decoder, NULL);
    gst_pad_set_active (smart_encoder->internal_srcpad, FALSE);
    gst_pad_set_active (smart_encoder->internal_sinkpad, FALSE);
    gst_object_unref (smart_encoder->encoder);
    gst_object_unref (smart_encoder->decoder);
    gst_object_unref (smart_encoder->internal_srcpad);
    gst_object_unref (smart_encoder->internal_sinkpad);

    smart_encoder->encoder = NULL;
    smart_encoder->decoder = NULL;
    smart_encoder->internal_sinkpad = NULL;
    smart_encoder->internal_srcpad = NULL;
  }

  if (smart_encoder->newsegment) {
    gst_event_unref (smart_encoder->newsegment);
    smart_encoder->newsegment = NULL;
  }
}


static void
gst_smart_encoder_init (GstSmartEncoder * smart_encoder)
{
  smart_encoder->sinkpad =
      gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (smart_encoder->sinkpad, gst_smart_encoder_chain);
  gst_pad_set_event_function (smart_encoder->sinkpad, smart_encoder_sink_event);
  gst_pad_set_query_function (smart_encoder->sinkpad, smart_encoder_sink_query);
  gst_element_add_pad (GST_ELEMENT (smart_encoder), smart_encoder->sinkpad);

  smart_encoder->srcpad =
      gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (smart_encoder->srcpad);
  gst_element_add_pad (GST_ELEMENT (smart_encoder), smart_encoder->srcpad);

  smart_encoder->segment = gst_segment_new ();

  smart_encoder_reset (smart_encoder);
}

void
gst_smart_encoder_dispose (GObject * object)
{
  GstSmartEncoder *smart_encoder = (GstSmartEncoder *) object;

  if (smart_encoder->segment)
    gst_segment_free (smart_encoder->segment);
  smart_encoder->segment = NULL;
  if (smart_encoder->available_caps)
    gst_caps_unref (smart_encoder->available_caps);
  smart_encoder->available_caps = NULL;
  G_OBJECT_CLASS (gst_smart_encoder_parent_class)->dispose (object);
}

static GstFlowReturn
gst_smart_encoder_reencode_gop (GstSmartEncoder * smart_encoder)
{
  GstFlowReturn res = GST_FLOW_OK;
  GList *tmp;

  if (smart_encoder->encoder == NULL) {
    if (!setup_recoder_pipeline (smart_encoder))
      return GST_FLOW_ERROR;
  }

  /* Activate elements */
  /* Set elements to PAUSED */
  gst_element_set_state (smart_encoder->encoder, GST_STATE_PAUSED);
  gst_element_set_state (smart_encoder->decoder, GST_STATE_PAUSED);

  GST_INFO ("Pushing Flush start/stop to clean decoder/encoder");
  gst_pad_push_event (smart_encoder->internal_srcpad,
      gst_event_new_flush_start ());
  gst_pad_push_event (smart_encoder->internal_srcpad,
      gst_event_new_flush_stop (TRUE));

  /* push newsegment */
  GST_INFO ("Pushing newsegment %" GST_PTR_FORMAT, smart_encoder->newsegment);
  gst_pad_push_event (smart_encoder->internal_srcpad,
      gst_event_ref (smart_encoder->newsegment));

  /* Push buffers through our pads */
  GST_DEBUG ("Pushing pending buffers");

  for (tmp = smart_encoder->pending_gop; tmp; tmp = tmp->next) {
    GstBuffer *buf = (GstBuffer *) tmp->data;

    res = gst_pad_push (smart_encoder->internal_srcpad, buf);
    if (G_UNLIKELY (res != GST_FLOW_OK))
      break;
  }

  if (G_UNLIKELY (res != GST_FLOW_OK)) {
    GST_WARNING ("Error pushing pending buffers : %s", gst_flow_get_name (res));
    /* Remove pending bfufers */
    for (tmp = smart_encoder->pending_gop; tmp; tmp = tmp->next) {
      gst_buffer_unref ((GstBuffer *) tmp->data);
    }
  } else {
    GST_INFO ("Pushing out EOS to flush out decoder/encoder");
    gst_pad_push_event (smart_encoder->internal_srcpad, gst_event_new_eos ());
  }

  /* Activate elements */
  /* Set elements to PAUSED */
  gst_element_set_state (smart_encoder->encoder, GST_STATE_NULL);
  gst_element_set_state (smart_encoder->decoder, GST_STATE_NULL);

  g_list_free (smart_encoder->pending_gop);
  smart_encoder->pending_gop = NULL;

  return res;
}

static GstFlowReturn
gst_smart_encoder_push_pending_gop (GstSmartEncoder * smart_encoder)
{
  guint64 cstart, cstop;
  GList *tmp;
  GstFlowReturn res = GST_FLOW_OK;

  GST_DEBUG ("Pushing pending GOP (%" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT
      ")", GST_TIME_ARGS (smart_encoder->gop_start),
      GST_TIME_ARGS (smart_encoder->gop_stop));

  /* If GOP is entirely within segment, just push downstream */
  if (gst_segment_clip (smart_encoder->segment, GST_FORMAT_TIME,
          smart_encoder->gop_start, smart_encoder->gop_stop, &cstart, &cstop)) {
    if ((cstart != smart_encoder->gop_start)
        || (cstop != smart_encoder->gop_stop)) {
      GST_DEBUG ("GOP needs to be re-encoded from %" GST_TIME_FORMAT " to %"
          GST_TIME_FORMAT, GST_TIME_ARGS (cstart), GST_TIME_ARGS (cstop));
      res = gst_smart_encoder_reencode_gop (smart_encoder);
    } else {
      /* The whole GOP is within the segment, push all pending buffers downstream */
      GST_DEBUG ("GOP doesn't need to be modified, pushing downstream");
      for (tmp = smart_encoder->pending_gop; tmp; tmp = tmp->next) {
        GstBuffer *buf = (GstBuffer *) tmp->data;
        res = gst_pad_push (smart_encoder->srcpad, buf);
        if (G_UNLIKELY (res != GST_FLOW_OK))
          break;
      }
    }
  } else {
    /* The whole GOP is outside the segment, there's most likely
     * a bug somewhere. */
    GST_WARNING
        ("GOP is entirely outside of the segment, upstream gave us too much data");
    for (tmp = smart_encoder->pending_gop; tmp; tmp = tmp->next) {
      gst_buffer_unref ((GstBuffer *) tmp->data);
    }
  }

  if (smart_encoder->pending_gop) {
    g_list_free (smart_encoder->pending_gop);
    smart_encoder->pending_gop = NULL;
  }
  smart_encoder->gop_start = GST_CLOCK_TIME_NONE;
  smart_encoder->gop_stop = GST_CLOCK_TIME_NONE;

  return res;
}

static GstFlowReturn
gst_smart_encoder_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstSmartEncoder *smart_encoder;
  GstFlowReturn res = GST_FLOW_OK;
  gboolean discont, keyframe;

  smart_encoder = GST_SMART_ENCODER (parent);

  discont = GST_BUFFER_IS_DISCONT (buf);
  keyframe = !GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  GST_DEBUG ("New buffer %s %s %" GST_TIME_FORMAT,
      discont ? "discont" : "",
      keyframe ? "keyframe" : "", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  if (keyframe) {
    GST_DEBUG ("Got a keyframe");

    /* If there's a pending GOP, flush it out */
    if (smart_encoder->pending_gop) {
      /* Mark gop_stop */
      smart_encoder->gop_stop = GST_BUFFER_TIMESTAMP (buf);

      /* flush pending */
      res = gst_smart_encoder_push_pending_gop (smart_encoder);
      if (G_UNLIKELY (res != GST_FLOW_OK))
        goto beach;
    }

    /* Mark gop_start for new gop */
    smart_encoder->gop_start = GST_BUFFER_TIMESTAMP (buf);
  }

  /* Store buffer */
  smart_encoder->pending_gop = g_list_append (smart_encoder->pending_gop, buf);
  /* Update GOP stop position */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    smart_encoder->gop_stop = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      smart_encoder->gop_stop += GST_BUFFER_DURATION (buf);
  }

  GST_DEBUG ("Buffer stored , Current GOP : %" GST_TIME_FORMAT " -- %"
      GST_TIME_FORMAT, GST_TIME_ARGS (smart_encoder->gop_start),
      GST_TIME_ARGS (smart_encoder->gop_stop));

beach:
  return res;
}

static gboolean
smart_encoder_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  GstSmartEncoder *smart_encoder = GST_SMART_ENCODER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      smart_encoder_reset (smart_encoder);
      break;
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, smart_encoder->segment);

      GST_DEBUG_OBJECT (smart_encoder, "segment: %" GST_SEGMENT_FORMAT,
          smart_encoder->segment);
      if (smart_encoder->segment->format != GST_FORMAT_TIME)
        GST_ERROR
            ("smart_encoder can not handle streams not specified in GST_FORMAT_TIME");

      /* And keep a copy for further usage */
      if (smart_encoder->newsegment)
        gst_event_unref (smart_encoder->newsegment);
      smart_encoder->newsegment = gst_event_ref (event);
    }
      break;
    case GST_EVENT_EOS:
      GST_DEBUG ("Eos, flushing remaining data");
      gst_smart_encoder_push_pending_gop (smart_encoder);
      break;
    default:
      break;
  }

  res = gst_pad_push_event (smart_encoder->srcpad, event);

  return res;
}

static GstCaps *
smart_encoder_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *peer, *tmpl, *res;
  GstSmartEncoder *smart_encoder = GST_SMART_ENCODER (gst_pad_get_parent (pad));

  /* Use computed caps */
  if (smart_encoder->available_caps)
    tmpl = gst_caps_ref (smart_encoder->available_caps);
  else
    tmpl = gst_static_pad_template_get_caps (&src_template);

  /* Try getting it from downstream */
  peer = gst_pad_peer_query_caps (smart_encoder->srcpad, tmpl);

  if (peer == NULL) {
    res = tmpl;
  } else {
    res = peer;
    gst_caps_unref (tmpl);
  }

  gst_object_unref (smart_encoder);
  return res;
}

static gboolean
smart_encoder_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = smart_encoder_sink_getcaps (pad, filter);
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

/*****************************************
 *    Internal encoder/decoder pipeline  *
 ******************************************/

static GstElementFactory *
get_decoder_factory (GstCaps * caps)
{
  GstElementFactory *fact = NULL;
  GList *decoders, *tmp;

  tmp =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_DECODER,
      GST_RANK_MARGINAL);
  decoders = gst_element_factory_list_filter (tmp, caps, GST_PAD_SINK, FALSE);
  gst_plugin_feature_list_free (tmp);

  for (tmp = decoders; tmp; tmp = tmp->next) {
    /* We just pick the first one */
    fact = (GstElementFactory *) tmp->data;
    gst_object_ref (fact);
    break;
  }

  gst_plugin_feature_list_free (decoders);

  return fact;
}

static GstElementFactory *
get_encoder_factory (GstCaps * caps)
{
  GstElementFactory *fact = NULL;
  GList *encoders, *tmp;

  tmp =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_ENCODER,
      GST_RANK_MARGINAL);
  encoders = gst_element_factory_list_filter (tmp, caps, GST_PAD_SRC, FALSE);
  gst_plugin_feature_list_free (tmp);

  for (tmp = encoders; tmp; tmp = tmp->next) {
    /* We just pick the first one */
    fact = (GstElementFactory *) tmp->data;
    gst_object_ref (fact);
    break;
  }

  gst_plugin_feature_list_free (encoders);

  return fact;
}

static GstElement *
get_decoder (GstCaps * caps)
{
  GstElementFactory *fact = get_decoder_factory (caps);
  GstElement *res = NULL;

  if (fact) {
    res = gst_element_factory_create (fact, "internal-decoder");
    gst_object_unref (fact);
  }
  return res;
}

static GstElement *
get_encoder (GstCaps * caps)
{
  GstElementFactory *fact = get_encoder_factory (caps);
  GstElement *res = NULL;

  if (fact) {
    res = gst_element_factory_create (fact, "internal-encoder");
    gst_object_unref (fact);
  }
  return res;
}

static GstFlowReturn
internal_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstSmartEncoder *smart_encoder =
      g_object_get_qdata ((GObject *) pad, INTERNAL_ELEMENT);

  return gst_pad_push (smart_encoder->srcpad, buf);
}

static gboolean
setup_recoder_pipeline (GstSmartEncoder * smart_encoder)
{
  GstPad *tmppad;
  GstCaps *caps;

  /* Fast path */
  if (G_UNLIKELY (smart_encoder->encoder))
    return TRUE;

  GST_DEBUG ("Creating internal decoder and encoder");

  /* Create decoder/encoder */
  caps = gst_pad_get_current_caps (smart_encoder->sinkpad);
  smart_encoder->decoder = get_decoder (caps);
  if (G_UNLIKELY (smart_encoder->decoder == NULL))
    goto no_decoder;
  gst_caps_unref (caps);
  gst_element_set_bus (smart_encoder->decoder, GST_ELEMENT_BUS (smart_encoder));

  caps = gst_pad_get_current_caps (smart_encoder->sinkpad);
  smart_encoder->encoder = get_encoder (caps);
  if (G_UNLIKELY (smart_encoder->encoder == NULL))
    goto no_encoder;
  gst_caps_unref (caps);
  gst_element_set_bus (smart_encoder->encoder, GST_ELEMENT_BUS (smart_encoder));

  GST_DEBUG ("Creating internal pads");

  /* Create internal pads */

  /* Source pad which we'll use to feed data to decoders */
  smart_encoder->internal_srcpad = gst_pad_new ("internal_src", GST_PAD_SRC);
  g_object_set_qdata ((GObject *) smart_encoder->internal_srcpad,
      INTERNAL_ELEMENT, smart_encoder);
  gst_pad_set_active (smart_encoder->internal_srcpad, TRUE);

  /* Sink pad which will get the buffers from the encoder.
   * Note: We don't need an event function since we'll be discarding all
   * of them. */
  smart_encoder->internal_sinkpad = gst_pad_new ("internal_sink", GST_PAD_SINK);
  g_object_set_qdata ((GObject *) smart_encoder->internal_sinkpad,
      INTERNAL_ELEMENT, smart_encoder);
  gst_pad_set_chain_function (smart_encoder->internal_sinkpad, internal_chain);
  gst_pad_set_active (smart_encoder->internal_sinkpad, TRUE);

  GST_DEBUG ("Linking pads to elements");

  /* Link everything */
  tmppad = gst_element_get_static_pad (smart_encoder->encoder, "src");
  if (GST_PAD_LINK_FAILED (gst_pad_link (tmppad,
              smart_encoder->internal_sinkpad)))
    goto sinkpad_link_fail;
  gst_object_unref (tmppad);

  if (!gst_element_link (smart_encoder->decoder, smart_encoder->encoder))
    goto encoder_decoder_link_fail;

  tmppad = gst_element_get_static_pad (smart_encoder->decoder, "sink");
  if (GST_PAD_LINK_FAILED (gst_pad_link (smart_encoder->internal_srcpad,
              tmppad)))
    goto srcpad_link_fail;
  gst_object_unref (tmppad);

  GST_DEBUG ("Done creating internal elements/pads");

  return TRUE;

no_decoder:
  {
    GST_WARNING ("Couldn't find a decoder for %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

no_encoder:
  {
    GST_WARNING ("Couldn't find an encoder for %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

srcpad_link_fail:
  {
    gst_object_unref (tmppad);
    GST_WARNING ("Couldn't link internal srcpad to decoder");
    return FALSE;
  }

sinkpad_link_fail:
  {
    gst_object_unref (tmppad);
    GST_WARNING ("Couldn't link encoder to internal sinkpad");
    return FALSE;
  }

encoder_decoder_link_fail:
  {
    GST_WARNING ("Couldn't link decoder to encoder");
    return FALSE;
  }
}

static GstStateChangeReturn
gst_smart_encoder_find_elements (GstSmartEncoder * smart_encoder)
{
  guint i, n;
  GstCaps *tmpl, *st, *res;
  GstElementFactory *dec, *enc;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  if (G_UNLIKELY (smart_encoder->available_caps))
    goto beach;

  /* Iterate over all pad template caps and see if we have both an
   * encoder and a decoder for those media types */
  tmpl = gst_static_pad_template_get_caps (&src_template);
  res = gst_caps_new_empty ();
  n = gst_caps_get_size (tmpl);

  for (i = 0; i < n; i++) {
    st = gst_caps_copy_nth (tmpl, i);
    GST_DEBUG_OBJECT (smart_encoder,
        "Checking for available decoder and encoder for %" GST_PTR_FORMAT, st);
    if (!(dec = get_decoder_factory (st))) {
      gst_caps_unref (st);
      continue;
    }
    gst_object_unref (dec);
    if (!(enc = get_encoder_factory (st))) {
      gst_caps_unref (st);
      continue;
    }
    gst_object_unref (enc);
    GST_DEBUG_OBJECT (smart_encoder, "OK");
    gst_caps_append (res, st);
  }

  gst_caps_unref (tmpl);

  if (gst_caps_is_empty (res)) {
    gst_caps_unref (res);
    ret = GST_STATE_CHANGE_FAILURE;
  } else
    smart_encoder->available_caps = res;

  GST_DEBUG_OBJECT (smart_encoder, "Done, available_caps:%" GST_PTR_FORMAT,
      smart_encoder->available_caps);

beach:
  return ret;
}

/******************************************
 *    GstElement vmethod implementations  *
 ******************************************/

static GstStateChangeReturn
gst_smart_encoder_change_state (GstElement * element, GstStateChange transition)
{
  GstSmartEncoder *smart_encoder;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_SMART_ENCODER (element),
      GST_STATE_CHANGE_FAILURE);

  smart_encoder = GST_SMART_ENCODER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* Figure out which elements are available  */
      if ((ret =
              gst_smart_encoder_find_elements (smart_encoder)) ==
          GST_STATE_CHANGE_FAILURE)
        goto beach;
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_smart_encoder_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      smart_encoder_reset (smart_encoder);
      break;
    default:
      break;
  }

beach:
  return ret;
}
