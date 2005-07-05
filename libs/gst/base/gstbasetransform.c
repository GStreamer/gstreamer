/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbasetransform.c: 
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


#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../gst-i18n-lib.h"
#include "gstbasetransform.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_STATIC (gst_base_transform_debug);
#define GST_CAT_DEFAULT gst_base_transform_debug

/* BaseTransform signals and args */
enum
{
  SIGNAL_HANDOFF,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

static GstElementClass *parent_class = NULL;

static void gst_base_transform_base_init (gpointer g_class);
static void gst_base_transform_class_init (GstBaseTransformClass * klass);
static void gst_base_transform_init (GstBaseTransform * trans,
    gpointer g_class);

GType
gst_base_transform_get_type (void)
{
  static GType base_transform_type = 0;

  if (!base_transform_type) {
    static const GTypeInfo base_transform_info = {
      sizeof (GstBaseTransformClass),
      (GBaseInitFunc) gst_base_transform_base_init,
      NULL,
      (GClassInitFunc) gst_base_transform_class_init,
      NULL,
      NULL,
      sizeof (GstBaseTransform),
      0,
      (GInstanceInitFunc) gst_base_transform_init,
    };

    base_transform_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseTransform", &base_transform_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_transform_type;
}

static void gst_base_transform_finalize (GObject * object);
static void gst_base_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_base_transform_src_activate_pull (GstPad * pad,
    gboolean active);
static gboolean gst_base_transform_sink_activate_push (GstPad * pad,
    gboolean active);
static GstElementStateReturn gst_base_transform_change_state (GstElement *
    element);

static gboolean gst_base_transform_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_base_transform_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer);
static GstFlowReturn gst_base_transform_chain (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_base_transform_handle_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static GstCaps *gst_base_transform_getcaps (GstPad * pad);
static gboolean gst_base_transform_setcaps (GstPad * pad, GstCaps * caps);

/* static guint gst_base_transform_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_base_transform_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_base_transform_debug, "basetransform", 0,
      "basetransform element");
}

static void
gst_base_transform_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_transform_class_init (GstBaseTransformClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_transform_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_transform_get_property);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_transform_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_transform_change_state);
}

static void
gst_base_transform_init (GstBaseTransform * trans, gpointer g_class)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_base_transform_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);
  trans->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_getcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getcaps));
  gst_pad_set_setcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_setcaps));
  gst_pad_set_event_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_event));
  gst_pad_set_chain_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_chain));
  gst_pad_set_activatepush_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_sink_activate_push));
  gst_element_add_pad (GST_ELEMENT (trans), trans->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);
  trans->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_getcaps_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getcaps));
  gst_pad_set_setcaps_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_setcaps));
  gst_pad_set_getrange_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getrange));
  gst_pad_set_activatepull_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_src_activate_pull));
  gst_element_add_pad (GST_ELEMENT (trans), trans->srcpad);
}

static GstCaps *
gst_base_transform_transform_caps (GstBaseTransform * trans, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *ret;
  GstBaseTransformClass *klass;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_DEBUG_OBJECT (trans, "from: %" GST_PTR_FORMAT, caps);

  if (klass->transform_caps)
    ret = klass->transform_caps (trans, pad, caps);
  else
    ret = gst_caps_ref (caps);

  GST_DEBUG_OBJECT (trans, "to:   %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_base_transform_getcaps (GstPad * pad)
{
  GstBaseTransform *trans;
  GstPad *otherpad;
  GstCaps *caps;

  trans = GST_BASE_TRANSFORM (GST_PAD_PARENT (pad));

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);

  if (caps) {
    GstCaps *temp;

    temp = gst_caps_intersect (caps, gst_pad_get_pad_template_caps (otherpad));
    gst_caps_unref (caps);
    caps = gst_base_transform_transform_caps (trans, otherpad, temp);
    gst_caps_unref (temp);
    temp = gst_caps_intersect (caps, gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (caps);
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  return caps;
}

static gboolean
gst_base_transform_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *klass;
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps = NULL;
  gboolean ret = FALSE;

  trans = GST_BASE_TRANSFORM (GST_PAD_PARENT (pad));
  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  if (GST_PAD_IS_IN_SETCAPS (otherpad)) {
    ret = TRUE;
    goto done;
  }

  /* see how we can transform the input caps */
  othercaps = gst_base_transform_transform_caps (trans, pad, caps);

  if (!othercaps) {
    ret = FALSE;
    goto done;
  }

  if (!gst_caps_is_fixed (othercaps)) {
    GstCaps *temp;

    temp = gst_caps_intersect (othercaps, caps);
    if (temp) {
      /* try passthrough. we know it's fixed, because caps is fixed */
      if (gst_pad_accept_caps (otherpeer, caps)) {
        gst_caps_unref (othercaps);
        othercaps = gst_caps_ref (caps);
        /* will fall though. calls accept_caps again, should fix that. */
      }
      gst_caps_unref (temp);
    }
  }

  if (!gst_caps_is_fixed (othercaps) && otherpeer) {
    /* intersect against what the peer can do */
    if (otherpeer) {
      GstCaps *peercaps;
      GstCaps *intersect;

      peercaps = gst_pad_get_caps (otherpeer);
      intersect = gst_caps_intersect (peercaps, othercaps);
      gst_caps_unref (peercaps);
      gst_caps_unref (othercaps);
      othercaps = intersect;

      GST_DEBUG ("filtering against peer yields %" GST_PTR_FORMAT, othercaps);
    }
  }

  if (!gst_caps_is_fixed (othercaps)) {
    GstCaps *temp;

    /* take first possibility and fixate if necessary */
    temp = gst_caps_copy_nth (othercaps, 0);
    gst_caps_unref (othercaps);
    othercaps = temp;
    gst_pad_fixate_caps (otherpad, othercaps);
  }

  g_return_val_if_fail (gst_caps_is_fixed (othercaps), FALSE);

  if (otherpeer && !gst_pad_accept_caps (otherpeer, othercaps)) {
    GST_DEBUG ("FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    ret = FALSE;
    goto done;
  }

  /* we know this will work, we implement the setcaps */
  gst_pad_set_caps (otherpad, othercaps);

  /* success, let the element know */
  if (klass->set_caps) {
    if (pad == trans->sinkpad)
      ret = klass->set_caps (trans, caps, othercaps);
    else
      ret = klass->set_caps (trans, othercaps, caps);
  }

done:

  if (otherpeer)
    gst_object_unref (otherpeer);

  if (othercaps)
    gst_caps_unref (othercaps);

  return ret;
}

static gboolean
gst_base_transform_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = FALSE;
  gboolean unlock;

  trans = GST_BASE_TRANSFORM (GST_PAD_PARENT (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->event)
    bclass->event (trans, event);

  unlock = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      if (GST_EVENT_FLUSH_DONE (event)) {
        GST_STREAM_LOCK (pad);
        unlock = TRUE;
      }
      break;
    case GST_EVENT_EOS:
      GST_STREAM_LOCK (pad);
      unlock = TRUE;
      break;
    default:
      break;
  }
  ret = gst_pad_event_default (pad, event);
  if (unlock)
    GST_STREAM_UNLOCK (pad);

  return ret;
}

static GstFlowReturn
gst_base_transform_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstBaseTransform *trans;
  GstFlowReturn ret;
  GstBuffer *inbuf;

  trans = GST_BASE_TRANSFORM (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  ret = gst_pad_pull_range (trans->sinkpad, offset, length, &inbuf);
  if (ret == GST_FLOW_OK) {
    ret = gst_base_transform_handle_buffer (trans, inbuf, buffer);
  }

  GST_STREAM_UNLOCK (pad);

  return ret;
}

static GstFlowReturn
gst_base_transform_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseTransform *trans;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;

  trans = GST_BASE_TRANSFORM (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  ret = gst_base_transform_handle_buffer (trans, buffer, &outbuf);
  if (ret == GST_FLOW_OK) {
    ret = gst_pad_push (trans->srcpad, outbuf);
  }

  GST_STREAM_UNLOCK (pad);

  return ret;
}

static GstFlowReturn
gst_base_transform_handle_buffer (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseTransformClass *bclass;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  if (bclass->transform)
    ret = bclass->transform (trans, inbuf, outbuf);

  return ret;
}

static void
gst_base_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_base_transform_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;

  trans = GST_BASE_TRANSFORM (GST_OBJECT_PARENT (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (active) {
    if (bclass->start)
      result = bclass->start (trans);
  }

  return result;
}

static gboolean
gst_base_transform_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;

  trans = GST_BASE_TRANSFORM (GST_OBJECT_PARENT (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  result = gst_pad_activate_pull (trans->sinkpad, active);

  if (active) {
    if (result && bclass->start)
      result &= bclass->start (trans);
  }

  return result;
}

static GstElementStateReturn
gst_base_transform_change_state (GstElement * element)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  GstElementState transition;
  GstElementStateReturn result;

  trans = GST_BASE_TRANSFORM (element);
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (bclass->stop)
        result = bclass->stop (trans);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return result;
}
