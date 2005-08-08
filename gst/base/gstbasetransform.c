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

/**
 * SECTION:gstbasetransform
 * @short_description: Base class for simple tranform filters
 * @see_also: #GstBaseSrc, #GstBaseSink
 *
 * This base class is for filter elements that process data.
 *
 * <itemizedlist>
 *   <listitem><para>one sinkpad and one srcpad</para></listitem>
 *   <listitem><para>
 *      possible formats on sink and source pad implemented
 *      with custom transform_caps function. By default uses
 *      same format on sink and source.
 *   </para></listitem>
 *   <listitem><para>handles state changes</para></listitem>
 *   <listitem><para>does flushing</para></listitem>
 *   <listitem><para>push mode</para></listitem>
 *   <listitem><para>
 *       pull mode if transform can operate on arbitrary data
 *    </para></listitem>
 * </itemizedlist>
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
static guint gst_base_transform_get_size (GstBaseTransform * trans,
    GstCaps * caps);

static GstElementStateReturn gst_base_transform_change_state (GstElement *
    element);

static gboolean gst_base_transform_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_base_transform_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer);
static GstFlowReturn gst_base_transform_chain (GstPad * pad,
    GstBuffer * buffer);
static GstCaps *gst_base_transform_getcaps (GstPad * pad);
static gboolean gst_base_transform_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_base_transform_buffer_alloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

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
  gst_pad_set_bufferalloc_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_buffer_alloc));
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

  trans->passthrough = FALSE;
  trans->out_size = -1;
}

static GstCaps *
gst_base_transform_transform_caps (GstBaseTransform * trans, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *ret;
  GstBaseTransformClass *klass;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_DEBUG_OBJECT (trans, "from: %" GST_PTR_FORMAT, caps);

  /* if there is a custom transform function, use this */
  if (klass->transform_caps) {
    GstCaps *temp;
    gint i;

    ret = gst_caps_new_empty ();

    /* we send caps with just one structure to the transform 
     * function as this is easier for the element */
    for (i = 0; i < gst_caps_get_size (caps); i++) {
      GstCaps *nth;

      nth = gst_caps_copy_nth (caps, i);
      GST_DEBUG_OBJECT (trans, "  from: %" GST_PTR_FORMAT, nth);
      temp = klass->transform_caps (trans, pad, nth);
      gst_caps_unref (nth);
      GST_DEBUG_OBJECT (trans, "  to  : %" GST_PTR_FORMAT, temp);

      gst_caps_append (ret, temp);
    }
    gst_caps_do_simplify (ret);
  } else {
    /* else use the identity transform */
    ret = gst_caps_ref (caps);
  }

  GST_DEBUG_OBJECT (trans, "to:   %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_base_transform_getcaps (GstPad * pad)
{
  GstBaseTransform *trans;
  GstPad *otherpad;
  GstCaps *caps;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *temp;
    const GstCaps *templ;

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, caps);

    /* filtered against our padtemplate */
    templ = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect (caps, templ);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    /* then see what we can tranform this to */
    caps = gst_base_transform_transform_caps (trans, otherpad, temp);
    GST_DEBUG_OBJECT (pad, "transformed  %" GST_PTR_FORMAT, caps);
    gst_caps_unref (temp);
    if (caps == NULL)
      goto done;

    /* and filter against the template again */
    templ = gst_pad_get_pad_template_caps (pad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect (caps, templ);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

done:
  GST_DEBUG_OBJECT (trans, "returning  %" GST_PTR_FORMAT, caps);

  gst_object_unref (trans);

  return caps;
}

static gboolean
gst_base_transform_configure_caps (GstBaseTransform * trans, GstCaps * in,
    GstCaps * out)
{
  gboolean ret = TRUE;
  GstBaseTransformClass *klass;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  /* no configure the element with the caps */
  if (klass->set_caps) {
    ret = klass->set_caps (trans, in, out);
  }

  return ret;
}

static gboolean
gst_base_transform_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *klass;
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps = NULL;
  gboolean ret = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  if (GST_PAD_IS_IN_SETCAPS (otherpad)) {
    ret = TRUE;
    goto done;
  }

  /* see how we can transform the input caps */
  othercaps = gst_base_transform_transform_caps (trans, pad, caps);

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  if (!gst_caps_is_fixed (othercaps)) {
    GstCaps *temp;

    GST_DEBUG_OBJECT (trans,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    temp = gst_caps_intersect (othercaps, caps);
    GST_DEBUG_OBJECT (trans, "intersect returned %" GST_PTR_FORMAT, temp);
    if (temp) {
      if (!gst_caps_is_empty (temp) && otherpeer) {
        GST_DEBUG_OBJECT (trans, "try passthrough with %" GST_PTR_FORMAT, caps);
        /* try passthrough. we know it's fixed, because caps is fixed */
        if (gst_pad_accept_caps (otherpeer, caps)) {
          GST_DEBUG_OBJECT (trans, "peer accepted %" GST_PTR_FORMAT, caps);
          gst_caps_unref (othercaps);
          othercaps = gst_caps_ref (caps);
          /* will fall though. calls accept_caps again, should fix that. */
        } else {
          GST_DEBUG_OBJECT (trans,
              "peer did not accept %" GST_PTR_FORMAT, caps);
        }
      }
      gst_caps_unref (temp);
    }
  }

  if (!gst_caps_is_fixed (othercaps) && otherpeer) {
    /* intersect against what the peer can do */
    GstCaps *peercaps;
    GstCaps *intersect;

    GST_DEBUG_OBJECT (trans, "othercaps now %" GST_PTR_FORMAT, othercaps);

    peercaps = gst_pad_get_caps (otherpeer);
    intersect = gst_caps_intersect (peercaps, othercaps);
    gst_caps_unref (peercaps);
    gst_caps_unref (othercaps);
    othercaps = intersect;

    GST_DEBUG_OBJECT (trans,
        "filtering against peer yields %" GST_PTR_FORMAT, othercaps);
  }

  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  if (!gst_caps_is_fixed (othercaps)) {
    GstCaps *temp;

    GST_DEBUG_OBJECT (trans,
        "othercaps now, trying to fixate %" GST_PTR_FORMAT, othercaps);

    /* take first possibility and fixate if necessary */
    temp = gst_caps_copy_nth (othercaps, 0);
    gst_caps_unref (othercaps);
    othercaps = temp;
    gst_pad_fixate_caps (otherpad, othercaps);

    GST_DEBUG_OBJECT (trans, "after fixating %" GST_PTR_FORMAT, othercaps);
  }

  /* caps should be fixed now */
  if (!gst_caps_is_fixed (othercaps))
    goto could_not_fixate;

  /* and peer should accept */
  if (otherpeer && !gst_pad_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (trans, "got final caps %" GST_PTR_FORMAT, othercaps);

  /* we know this will work, we implement the setcaps */
  gst_pad_set_caps (otherpad, othercaps);

  trans->in_place = gst_caps_is_equal (caps, othercaps);
  GST_DEBUG_OBJECT (trans, "in_place: %d", trans->in_place);

  /* see if we have to configure the element now */
  if (!trans->delay_configure) {
    if (pad == trans->sinkpad)
      ret = gst_base_transform_configure_caps (trans, caps, othercaps);
    else
      ret = gst_base_transform_configure_caps (trans, othercaps, caps);
  }

done:
  if (otherpeer)
    gst_object_unref (otherpeer);
  if (othercaps)
    gst_caps_unref (othercaps);

  gst_object_unref (trans);

  return ret;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (trans,
        "transform returned useless  %" GST_PTR_FORMAT, othercaps);
    ret = FALSE;
    goto done;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (trans,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    ret = FALSE;
    goto done;
  }
could_not_fixate:
  {
    GST_ERROR_OBJECT (trans, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    ret = FALSE;
    goto done;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (trans, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    ret = FALSE;
    goto done;
  }
}

static guint
gst_base_transform_get_size (GstBaseTransform * trans, GstCaps * caps)
{
  guint res = -1;
  GstBaseTransformClass *bclass;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  if (bclass->get_size) {
    res = bclass->get_size (trans, caps);
    GST_DEBUG_OBJECT (trans, "get size(%p) returned %d", caps, res);
  }

  return res;
}

static GstFlowReturn
gst_base_transform_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstBaseTransform *trans;
  GstFlowReturn res;
  guint got_size;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  *buf = NULL;

  if (trans->in_place) {
    /* request a buffer with the same caps */
    res = gst_pad_alloc_buffer (trans->srcpad, offset, size, caps, buf);
  } else {
    /* if we are configured, request a buffer with the src caps */
    GstCaps *srccaps = gst_pad_get_negotiated_caps (trans->srcpad);

    if (!srccaps)
      goto not_configured;

    got_size = gst_base_transform_get_size (trans, srccaps);
    if (got_size == -1) {
      gst_caps_unref (srccaps);
      goto unknown_size;
    }

    res = gst_pad_alloc_buffer (trans->srcpad, offset, got_size, srccaps, buf);
    gst_caps_unref (srccaps);
  }

  if (res == GST_FLOW_OK && !trans->in_place) {
    /* note that we might have been in place before, but calling the
       alloc_buffer caused setcaps to switch us out of in_place -- in any case
       the alloc_buffer served to transmit caps information but we can't use the
       buffer. fall through and allocate a buffer corresponding to our sink
       caps, if any */
    GstCaps *sinkcaps = gst_pad_get_negotiated_caps (trans->sinkpad);

    if (!sinkcaps)
      goto not_configured;

    got_size = gst_base_transform_get_size (trans, sinkcaps);
    if (got_size == -1) {
      gst_caps_unref (sinkcaps);
      goto unknown_size;
    }

    if (*buf) {
      gst_buffer_unref (*buf);
    }
    *buf = gst_buffer_new_and_alloc (got_size);
    gst_buffer_set_caps (*buf, sinkcaps);
    GST_BUFFER_OFFSET (*buf) = offset;
    res = GST_FLOW_OK;

    gst_caps_unref (sinkcaps);
  }

  gst_object_unref (trans);
  return res;

not_configured:
  {
    /* let the default allocator handle it */
    if (*buf) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }
    gst_object_unref (trans);
    return GST_FLOW_OK;
  }
unknown_size:
  {
    /* let the default allocator handle it */
    if (*buf) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }
    gst_object_unref (trans);
    return GST_FLOW_OK;
  }
}


static gboolean
gst_base_transform_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = FALSE;
  gboolean unlock;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->event)
    bclass->event (trans, event);

  unlock = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_STREAM_LOCK (pad);
      unlock = TRUE;
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

  gst_object_unref (trans);

  return ret;
}

static GstFlowReturn
gst_base_transform_handle_buffer (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstBaseTransformClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (trans->in_place) {
    if (bclass->transform_ip) {
      gst_buffer_ref (inbuf);

      /* in place transform and subclass supports method */
      ret = bclass->transform_ip (trans, inbuf);

      *outbuf = inbuf;
    } else {
      /* in place transform and subclass does not support method */
      if (bclass->transform) {
        /* make a copy of the buffer */
        *outbuf = inbuf;
        inbuf = gst_buffer_copy (inbuf);

        ret = bclass->transform (trans, inbuf, *outbuf);
      } else {
        ret = GST_FLOW_NOT_SUPPORTED;
      }
    }
  } else {
    /* figure out the output size */
    if (trans->out_size == -1) {
      /* ask subclass */
      trans->out_size = gst_base_transform_get_size (trans,
          GST_PAD_CAPS (trans->srcpad));
      if (trans->out_size == -1)
        /* we have an error */
        goto no_size;
    }

    /* we cannot reconfigure the element yet as we are still processing
     * the old buffer. We will therefore delay the reconfiguration of the
     * element until we have processed this last buffer. */
    trans->delay_configure = TRUE;

    /* no in place transform, get buffer, this might renegotiate. */
    ret = gst_pad_alloc_buffer (trans->srcpad,
        GST_BUFFER_OFFSET (inbuf), trans->out_size,
        GST_PAD_CAPS (trans->srcpad), outbuf);

    trans->delay_configure = FALSE;

    if (ret != GST_FLOW_OK)
      goto no_buffer;

    gst_buffer_stamp (*outbuf, inbuf);

    if (bclass->transform)
      ret = bclass->transform (trans, inbuf, *outbuf);
    else
      ret = GST_FLOW_NOT_SUPPORTED;

    if (ret != GST_FLOW_OK)
      ret =
          gst_base_transform_configure_caps (trans,
          GST_PAD_CAPS (trans->sinkpad), GST_PAD_CAPS (trans->srcpad));
  }
  gst_buffer_unref (inbuf);

  return ret;

  /* ERRORS */
no_size:
  {
    gst_buffer_unref (inbuf);
    GST_ELEMENT_ERROR (trans, STREAM, NOT_IMPLEMENTED,
        ("subclass did not specify output size"),
        ("subclass did not specify output size"));
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    gst_buffer_unref (inbuf);
    GST_DEBUG_OBJECT (trans, "could not get buffer from pool");
    return ret;
  }
}

static GstFlowReturn
gst_base_transform_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstBaseTransform *trans;
  GstFlowReturn ret;
  GstBuffer *inbuf;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  ret = gst_pad_pull_range (trans->sinkpad, offset, length, &inbuf);
  if (ret == GST_FLOW_OK) {
    ret = gst_base_transform_handle_buffer (trans, inbuf, buffer);
  }

  gst_object_unref (trans);

  return ret;
}

static GstFlowReturn
gst_base_transform_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseTransform *trans;
  GstFlowReturn ret;
  GstBuffer *outbuf;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  ret = gst_base_transform_handle_buffer (trans, buffer, &outbuf);
  if (ret == GST_FLOW_OK) {
    ret = gst_pad_push (trans->srcpad, outbuf);
  }

  gst_object_unref (trans);

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

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (active) {
    if (bclass->start)
      result = bclass->start (trans);
  }
  gst_object_unref (trans);

  return result;
}

static gboolean
gst_base_transform_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  result = gst_pad_activate_pull (trans->sinkpad, active);

  if (active) {
    if (result && bclass->start)
      result &= bclass->start (trans);
  }
  gst_object_unref (trans);

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
      GST_LOCK (trans);
      if (GST_PAD_CAPS (trans->sinkpad) && GST_PAD_CAPS (trans->srcpad))
        trans->in_place = gst_caps_is_equal (GST_PAD_CAPS (trans->sinkpad),
            GST_PAD_CAPS (trans->srcpad)) || trans->passthrough;
      else
        trans->in_place = trans->passthrough;
      trans->out_size = -1;
      GST_UNLOCK (trans);
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

/**
 * gst_base_transform_set_passthrough:
 * @trans: the #GstBaseTransform to set
 * @passthrough: boolean indicating passthrough mode.
 *
 * Set passthrough mode for this filter by default. This is mostly
 * usefull for filters that do not care about negotiation.
 *
 * MT safe.
 */
void
gst_base_transform_set_passthrough (GstBaseTransform * trans,
    gboolean passthrough)
{
  g_return_if_fail (trans != NULL);

  GST_LOCK (trans);
  trans->passthrough = passthrough;
  GST_UNLOCK (trans);
}

/**
 * gst_base_transform_is_passthrough:
 * @trans: the #GstBaseTransform to query
 *
 * See if @trans is configured as a passthrough transform.
 *
 * Returns: TRUE is the transform is configured in passthrough mode.
 *
 * MT safe.
 */
gboolean
gst_base_transform_is_passthrough (GstBaseTransform * trans)
{
  gboolean result;

  g_return_val_if_fail (trans != NULL, FALSE);

  GST_LOCK (trans);
  result = trans->passthrough;
  GST_UNLOCK (trans);

  return result;
}
