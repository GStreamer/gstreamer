/* GStreamer
 * Copyright (C) <2009> Jan Schmidt <thaytan@noraisin.net>
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

#include "rsnaudiodec.h"

GST_DEBUG_CATEGORY_STATIC (rsn_audiodec_debug);
#define GST_CAT_DEFAULT rsn_audiodec_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg,mpegversion=(int)1;"
        "audio/x-private1-lpcm;"
        "audio/x-private1-ac3;" "audio/ac3;" "audio/x-ac3;"
        "audio/x-private1-dts;")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 32, 64 }; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 24, "
        "depth = (int) 24, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) true; "
        "audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) true")
    );

G_DEFINE_TYPE (RsnAudioDec, rsn_audiodec, GST_TYPE_BIN);

static gboolean rsn_audiodec_set_sink_caps (GstPad * pad, GstCaps * caps);
static GstCaps *rsn_audiodec_get_sink_caps (GstPad * pad);
static GstFlowReturn rsn_audiodec_chain (GstPad * pad, GstBuffer * buf);
static gboolean rsn_audiodec_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn rsn_audiodec_change_state (GstElement * element,
    GstStateChange transition);

static GstCaps *rsn_audiodec_get_proxy_sink_caps (GstPad * pad);
static GstCaps *rsn_audiodec_get_proxy_src_caps (GstPad * pad);

static GstFlowReturn rsn_audiodec_proxy_src_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean rsn_audiodec_proxy_src_event (GstPad * pad, GstEvent * event);

static void rsn_audiodec_dispose (GObject * gobj);
static void cleanup_child (RsnAudioDec * self);

static void
rsn_audiodec_class_init (RsnAudioDecClass * klass)
{
  static GstElementDetails element_details = {
    "RsnAudioDec",
    "Audio/Decoder",
    "Resin DVD audio stream decoder",
    "Jan Schmidt <thaytan@noraisin.net>"
  };
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (rsn_audiodec_debug, "rsnaudiodec",
      0, "Resin DVD audio stream decoder");

  object_class->dispose = rsn_audiodec_dispose;

  element_class->change_state = GST_DEBUG_FUNCPTR (rsn_audiodec_change_state);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &element_details);
}

static void
rsn_audiodec_init (RsnAudioDec * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_set_sink_caps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_get_sink_caps));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiodec_sink_event));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static void
rsn_audiodec_dispose (GObject * object)
{
  RsnAudioDec *self = (RsnAudioDec *) object;
  cleanup_child (self);

  G_OBJECT_CLASS (rsn_audiodec_parent_class)->dispose (object);
}

static gboolean
rsn_audiodec_set_sink_caps (GstPad * pad, GstCaps * caps)
{
  RsnAudioDec *self = (RsnAudioDec *) gst_pad_get_parent (pad);
  gboolean res;
  if (self == NULL)
    goto error;

  res = gst_pad_set_caps (self->child_sink, caps);

  gst_object_unref (self);
  return res;
error:
  if (self)
    gst_object_unref (self);
  return FALSE;
}

static GstCaps *
rsn_audiodec_get_sink_caps (GstPad * sinkpad)
{
  /* FIXME: Calculate set of allowed caps for all discovered decoders */
  RsnAudioDec *self = (RsnAudioDec *) gst_pad_get_parent (sinkpad);
  GstCaps *res;
  if (self == NULL || self->child_sink == NULL)
    goto error;

  res = gst_pad_get_caps (self->child_sink);
  GST_INFO_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, res);

  gst_object_unref (self);
  return res;
error:
  if (self)
    gst_object_unref (self);
  return gst_caps_copy (gst_pad_get_pad_template_caps (sinkpad));
}

static GstFlowReturn
rsn_audiodec_chain (GstPad * pad, GstBuffer * buf)
{
  RsnAudioDec *self = (RsnAudioDec *) gst_pad_get_parent (pad);
  GstFlowReturn res;
  if (self == NULL)
    goto error;

  GST_INFO_OBJECT (self, "Pushing buffer %" GST_PTR_FORMAT " into decoder",
      buf);
  res = gst_pad_chain (self->child_sink, buf);

  gst_object_unref (self);
  return res;
error:
  if (self)
    gst_object_unref (self);
  return GST_FLOW_ERROR;
}

static gboolean
rsn_audiodec_sink_event (GstPad * pad, GstEvent * event)
{
  GstPad *sinkpad = GST_PAD (gst_pad_get_parent (pad));
  RsnAudioDec *self = (RsnAudioDec *) gst_pad_get_parent (sinkpad);
  gboolean res;

  gst_object_unref (sinkpad);

  if (self == NULL)
    goto error;

  GST_INFO_OBJECT (self, "Sending event %" GST_PTR_FORMAT " into decoder",
      event);
  res = gst_pad_send_event (self->child_sink, event);

  gst_object_unref (self);
  return res;
error:
  if (self)
    gst_object_unref (self);
  return FALSE;
}

static GstCaps *
rsn_audiodec_get_proxy_sink_caps (GstPad * pad)
{
  GstPad *sinkpad = GST_PAD (gst_pad_get_parent (pad));
  GstCaps *ret;

  if (sinkpad != NULL) {
    ret = gst_pad_get_caps (sinkpad);
  } else
    ret = gst_caps_new_any ();

  gst_object_unref (sinkpad);

  return ret;
}

static GstCaps *
rsn_audiodec_get_proxy_src_caps (GstPad * pad)
{
  GstPad *srcpad = GST_PAD (gst_pad_get_parent (pad));
  GstCaps *ret;

  if (srcpad != NULL) {
    ret = gst_pad_get_caps (srcpad);
  } else
    ret = gst_caps_new_any ();

  gst_object_unref (srcpad);

  return ret;
}

static GstFlowReturn
rsn_audiodec_proxy_src_chain (GstPad * pad, GstBuffer * buf)
{
  GstPad *srcpad = GST_PAD (gst_pad_get_parent (pad));
  RsnAudioDec *self = (RsnAudioDec *) gst_pad_get_parent (srcpad);
  GstFlowReturn ret;

  gst_object_unref (srcpad);

  if (self == NULL)
    return GST_FLOW_ERROR;

  GST_DEBUG_OBJECT (self, "Data from decoder, pushing to pad %"
      GST_PTR_FORMAT, self->srcpad);
  ret = gst_pad_push (self->srcpad, buf);

  gst_object_unref (self);

  return ret;
}

static gboolean
rsn_audiodec_proxy_src_event (GstPad * pad, GstEvent * event)
{
  GstPad *srcpad = GST_PAD (gst_pad_get_parent (pad));
  RsnAudioDec *self = (RsnAudioDec *) gst_pad_get_parent (srcpad);
  gboolean ret;

  gst_object_unref (srcpad);

  if (self == NULL)
    return FALSE;

  ret = gst_pad_push_event (self->srcpad, event);

  gst_object_unref (self);
  return ret;
}

static void
create_proxy_pads (RsnAudioDec * self)
{
  if (self->child_sink_proxy == NULL) {
    /* A src pad the child can query/send events to */
    self->child_sink_proxy = gst_pad_new ("sink_proxy", GST_PAD_SRC);
    gst_object_set_parent ((GstObject *) self->child_sink_proxy,
        (GstObject *) self->sinkpad);
    gst_pad_set_getcaps_function (self->child_sink_proxy,
        GST_DEBUG_FUNCPTR (rsn_audiodec_get_proxy_sink_caps));
  }

  if (self->child_src_proxy == NULL) {
    /* A sink pad the child can push to */
    self->child_src_proxy = gst_pad_new ("src_proxy", GST_PAD_SINK);
    gst_object_set_parent ((GstObject *) self->child_src_proxy,
        (GstObject *) self->srcpad);
    gst_pad_set_getcaps_function (self->child_src_proxy,
        GST_DEBUG_FUNCPTR (rsn_audiodec_get_proxy_src_caps));
    gst_pad_set_chain_function (self->child_src_proxy,
        GST_DEBUG_FUNCPTR (rsn_audiodec_proxy_src_chain));
    gst_pad_set_event_function (self->child_src_proxy,
        GST_DEBUG_FUNCPTR (rsn_audiodec_proxy_src_event));
  }
}

static gboolean
rsn_audiodec_set_child (RsnAudioDec * self, GstElement * new_child)
{
  if (self->current_decoder) {
    gst_bin_remove ((GstBin *) self, self->current_decoder);
    self->current_decoder = NULL;
  }
  if (self->child_sink) {
    (void) gst_pad_unlink (self->child_sink_proxy, self->child_sink);
    gst_object_unref (self->child_sink);
    self->child_sink = NULL;
  }
  if (self->child_src) {
    (void) gst_pad_unlink (self->child_src, self->child_src_proxy);
    gst_object_unref (self->child_src);
    self->child_src = NULL;
  }

  if (new_child == NULL)
    return TRUE;

  self->child_sink = gst_element_get_static_pad (new_child, "sink");
  if (self->child_sink == NULL) {
    return FALSE;
  }
  self->child_src = gst_element_get_static_pad (new_child, "src");
  if (self->child_src == NULL) {
    return FALSE;
  }
  if (!gst_bin_add ((GstBin *) self, new_child)) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Add child %" GST_PTR_FORMAT, new_child);
  self->current_decoder = new_child;
  if (gst_pad_link (self->child_sink_proxy,
          self->child_sink) != GST_PAD_LINK_OK)
    return FALSE;
  GST_DEBUG_OBJECT (self, "linked proxy sink pad %" GST_PTR_FORMAT
      " to child sink %" GST_PTR_FORMAT, self->child_sink_proxy,
      self->child_sink);
  if (gst_pad_link (self->child_src, self->child_src_proxy) != GST_PAD_LINK_OK)
    return FALSE;
  GST_DEBUG_OBJECT (self, "linked child src pad %" GST_PTR_FORMAT
      " to proxy pad %" GST_PTR_FORMAT, self->child_src, self->child_src_proxy);

  return TRUE;
}

static void
cleanup_child (RsnAudioDec * self)
{
  GST_DEBUG_OBJECT (self, "Removing child element");
  (void) rsn_audiodec_set_child (self, NULL);
  GST_DEBUG_OBJECT (self, "Destroying proxy pads");
  if (self->child_sink_proxy != NULL) {
    gst_object_unparent ((GstObject *) self->child_sink_proxy);
    self->child_sink_proxy = NULL;
  }
  if (self->child_src_proxy != NULL) {
    gst_object_unparent ((GstObject *) self->child_src_proxy);
    self->child_src_proxy = NULL;
  }
}

static GstStateChangeReturn
rsn_audiodec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  RsnAudioDec *self = RSN_AUDIODEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstElement *new_child;
      create_proxy_pads (self);
      GST_DEBUG_OBJECT (self, "Created proxy pads");
      new_child = gst_element_factory_make ("a52dec", NULL);
      if (new_child == NULL || !rsn_audiodec_set_child (self, new_child))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (self, "Activating proxy pads");
      if (self->child_sink_proxy)
        gst_pad_set_active (self->child_sink_proxy, TRUE);
      if (self->child_src_proxy)
        gst_pad_set_active (self->child_src_proxy, TRUE);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (rsn_audiodec_parent_class)->change_state (element,
      transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (self, "Deactivating proxy pads");
      if (self->child_sink_proxy)
        gst_pad_set_active (self->child_sink_proxy, FALSE);
      if (self->child_src_proxy)
        gst_pad_set_active (self->child_src_proxy, FALSE);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      cleanup_child (self);
      break;
    default:
      break;
  }

  return ret;

}
