/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Andy Wingo <wingo@pobox.com>
 *		      2006 Edward Hervey <bilboed@bilboed.com>
 *
 * gstghostpad.c: Proxy pads
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
 * SECTION:gstghostpad
 * @short_description: Pseudo link pads
 * @see_also: #GstPad
 *
 * GhostPads are useful when organizing pipelines with #GstBin like elements.
 * The idea here is to create hierarchical element graphs. The bin element
 * contains a sub-graph. Now one would like to treat the bin-element like any
 * other #GstElement. This is where GhostPads come into play. A GhostPad acts as
 * a proxy for another pad. Thus the bin can have sink and source ghost-pads
 * that are associated with sink and source pads of the child elements.
 *
 * If the target pad is known at creation time, gst_ghost_pad_new() is the
 * function to use to get a ghost-pad. Otherwise one can use gst_ghost_pad_new_no_target()
 * to create the ghost-pad and use gst_ghost_pad_set_target() to establish the
 * association later on.
 *
 * Note that GhostPads add overhead to the data processing of a pipeline.
 *
 * Last reviewed on 2005-11-18 (0.9.5)
 */

#include "gst_private.h"
#include "gstinfo.h"

#include "gstghostpad.h"
#include "gst.h"

#define GST_CAT_DEFAULT GST_CAT_PADS

#define GST_PROXY_PAD_CAST(obj)         ((GstProxyPad *)obj)
#define GST_PROXY_PAD_PRIVATE(obj)      (GST_PROXY_PAD_CAST (obj)->priv)
#define GST_PROXY_PAD_TARGET(pad)       (GST_PAD_PEER (GST_PROXY_PAD_INTERNAL (pad)))
#define GST_PROXY_PAD_INTERNAL(pad)     (GST_PROXY_PAD_PRIVATE (pad)->internal)

struct _GstProxyPadPrivate
{
  GstPad *internal;
};

G_DEFINE_TYPE (GstProxyPad, gst_proxy_pad, GST_TYPE_PAD);

static GstPad *gst_proxy_pad_get_target (GstPad * pad);

#if !defined(GST_DISABLE_LOADSAVE) && !defined(GST_REMOVE_DEPRECATED)
#ifdef GST_DISABLE_DEPRECATED
#include <libxml/parser.h>
#endif
static xmlNodePtr gst_proxy_pad_save_thyself (GstObject * object,
    xmlNodePtr parent);
#endif

static void on_src_target_notify (GstPad * target,
    GParamSpec * unused, gpointer user_data);

static GParamSpec *pspec_caps = NULL;

/**
 * gst_proxy_pad_query_type_default:
 * @pad: a #GstPad.
 *
 * Invoke the default query type handler of the proxy pad.
 *
 * Returns: (transfer none) (array zero-terminated=1): a zero-terminated array
 *     of #GstQueryType.
 *
 * Since: 0.10.36
 */
const GstQueryType *
gst_proxy_pad_query_type_default (GstPad * pad)
{
  GstPad *target;
  const GstQueryType *res = NULL;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), NULL);

  target = gst_proxy_pad_get_target (pad);
  if (target) {
    res = gst_pad_get_query_types (target);
    gst_object_unref (target);
  }
  return res;
}

/**
 * gst_proxy_pad_event_default:
 * @pad: a #GstPad to push the event to.
 * @event: (transfer full): the #GstEvent to send to the pad.
 *
 * Invoke the default event of the proxy pad.
 *
 * Returns: TRUE if the event was handled.
 *
 * Since: 0.10.36
 */
gboolean
gst_proxy_pad_event_default (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);
  g_return_val_if_fail (GST_IS_EVENT (event), FALSE);

  internal =
      GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD_CAST (pad)));
  if (internal) {
    res = gst_pad_push_event (internal, event);
    gst_object_unref (internal);
  }

  return res;
}

/**
 * gst_proxy_pad_query_default:
 * @pad: a #GstPad to invoke the default query on.
 * @query: (transfer none): the #GstQuery to perform.
 *
 * Invoke the default query function of the proxy pad.
 *
 * Returns: TRUE if the query could be performed.
 *
 * Since: 0.10.36
 */
gboolean
gst_proxy_pad_query_default (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstPad *target;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);
  g_return_val_if_fail (GST_IS_QUERY (query), FALSE);

  target = gst_proxy_pad_get_target (pad);
  if (target) {
    res = gst_pad_query (target, query);
    gst_object_unref (target);
  }

  return res;
}

/**
 * gst_proyx_pad_iterate_internal_links_default:
 * @pad: the #GstPad to get the internal links of.
 *
 * Invoke the default iterate internal links function of the proxy pad.
 *
 * Returns: a #GstIterator of #GstPad, or NULL if @pad has no parent. Unref each
 * returned pad with gst_object_unref().
 *
 * Since: 0.10.36
 */
GstIterator *
gst_proxy_pad_iterate_internal_links_default (GstPad * pad)
{
  GstIterator *res = NULL;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), NULL);

  internal =
      GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD_CAST (pad)));

  if (internal) {
    res =
        gst_iterator_new_single (GST_TYPE_PAD, internal,
        (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);
    gst_object_unref (internal);
  }

  return res;
}

/**
 * gst_proxy_pad_bufferalloc_default:
 * @pad: a source #GstPad
 * @offset: the offset of the new buffer in the stream
 * @size: the size of the new buffer
 * @caps: the caps of the new buffer
 * @buf: a newly allocated buffer
 *
 * Invoke the default bufferalloc function of the proxy pad.
 *
 * Returns: a result code indicating success of the operation. Any
 * result code other than #GST_FLOW_OK is an error and @buf should
 * not be used.
 * An error can occur if the pad is not connected or when the downstream
 * peer elements cannot provide an acceptable buffer.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_proxy_pad_bufferalloc_default (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstFlowReturn result = GST_FLOW_WRONG_STATE;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (caps == NULL || GST_IS_CAPS (caps), GST_FLOW_ERROR);
  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  internal =
      GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD_CAST (pad)));
  if (internal) {
    result = gst_pad_alloc_buffer (internal, offset, size, caps, buf);
    gst_object_unref (internal);
  }

  return result;
}

/**
 * gst_proxy_pad_chain_default:
 * @pad: a sink #GstPad, returns GST_FLOW_ERROR if not.
 * @buffer: (transfer full): the #GstBuffer to send, return GST_FLOW_ERROR
 *     if not.
 *
 * Invoke the default chain function of the proxy pad.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_proxy_pad_chain_default (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  internal = GST_PROXY_PAD_INTERNAL (pad);
  res = gst_pad_push (internal, buffer);

  return res;
}

/**
 * gst_proxy_pad_chain_list_default:
 * @pad: a sink #GstPad, returns GST_FLOW_ERROR if not.
 * @list: (transfer full): the #GstBufferList to send, return GST_FLOW_ERROR
 *     if not.
 *
 * Invoke the default chain list function of the proxy pad.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_proxy_pad_chain_list_default (GstPad * pad, GstBufferList * list)
{
  GstFlowReturn res;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER_LIST (list), GST_FLOW_ERROR);

  internal = GST_PROXY_PAD_INTERNAL (pad);
  res = gst_pad_push_list (internal, list);

  return res;
}

/**
 * gst_proxy_pad_get_range_default:
 * @pad: a src #GstPad, returns #GST_FLOW_ERROR if not.
 * @offset: The start offset of the buffer
 * @size: The length of the buffer
 * @buffer: (out callee-allocates): a pointer to hold the #GstBuffer,
 *     returns #GST_FLOW_ERROR if %NULL.
 *
 * Invoke the default getrange function of the proxy pad.
 *
 * Returns: a #GstFlowReturn from the pad.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_proxy_pad_getrange_default (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstFlowReturn res;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  internal = GST_PROXY_PAD_INTERNAL (pad);
  res = gst_pad_pull_range (internal, offset, size, buffer);

  return res;
}

/**
 * gst_proxy_pad_checkgetrange_default:
 * @pad: a src #GstPad, returns #GST_FLOW_ERROR if not.
 *
 * Invoke the default checkgetrange function of the proxy pad.
 *
 * Returns: a #gboolean from the pad.
 *
 * Since: 0.10.36
 */
gboolean
gst_proxy_pad_checkgetrange_default (GstPad * pad)
{
  gboolean result;
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);

  internal = GST_PROXY_PAD_INTERNAL (pad);
  result = gst_pad_check_pull_range (internal);

  return result;
}

/**
 * gst_proxy_pad_getcaps_default:
 * @pad: a  #GstPad to get the capabilities of.
 *
 * Invoke the default getcaps function of the proxy pad.
 *
 * Returns: (transfer full): the caps of the pad with incremented ref-count
 *
 * Since: 0.10.36
 */
GstCaps *
gst_proxy_pad_getcaps_default (GstPad * pad)
{
  GstPad *target;
  GstCaps *res;
  GstPadTemplate *templ;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), NULL);

  templ = GST_PAD_PAD_TEMPLATE (pad);
  target = gst_proxy_pad_get_target (pad);
  if (target) {
    /* if we have a real target, proxy the call */
    res = gst_pad_get_caps_reffed (target);

    GST_DEBUG_OBJECT (pad, "get caps of target %s:%s : %" GST_PTR_FORMAT,
        GST_DEBUG_PAD_NAME (target), res);

    gst_object_unref (target);

    /* filter against the template */
    if (templ && res) {
      GstCaps *filt, *tmp;

      filt = GST_PAD_TEMPLATE_CAPS (templ);
      if (filt) {
        tmp = gst_caps_intersect (filt, res);
        gst_caps_unref (res);
        res = tmp;
        GST_DEBUG_OBJECT (pad,
            "filtered against template gives %" GST_PTR_FORMAT, res);
      }
    }
  } else {
    /* else, if we have a template, use its caps. */
    if (templ) {
      res = GST_PAD_TEMPLATE_CAPS (templ);
      GST_DEBUG_OBJECT (pad,
          "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, res,
          res);
      res = gst_caps_ref (res);
      goto done;
    }

    /* last resort, any caps */
    GST_DEBUG_OBJECT (pad, "pad has no template, returning ANY");
    res = gst_caps_new_any ();
  }

done:
  return res;
}

/**
 * gst_proxy_pad_acceptcaps_default:
 * @pad: a #GstPad to check
 * @caps: a #GstCaps to check on the pad
 *
 * Invoke the default acceptcaps function of the proxy pad.
 *
 * Returns: TRUE if the pad can accept the caps.
 *
 * Since: 0.10.36
 */
gboolean
gst_proxy_pad_acceptcaps_default (GstPad * pad, GstCaps * caps)
{
  GstPad *target;
  gboolean res;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);
  g_return_val_if_fail (caps == NULL || GST_IS_CAPS (caps), FALSE);

  target = gst_proxy_pad_get_target (pad);
  if (target) {
    res = gst_pad_accept_caps (target, caps);
    gst_object_unref (target);
  } else {
    /* We don't have a target, we return TRUE and we assume that any future
     * target will be able to deal with any configured caps. */
    res = TRUE;
  }

  return res;
}

/**
 * gst_proxy_pad_fixatecaps_default:
 * @pad: a  #GstPad to fixate
 * @caps: the  #GstCaps to fixate
 *
 * Invoke the default fixatecaps function of the proxy pad.
 *
 * Since: 0.10.36
 */
void
gst_proxy_pad_fixatecaps_default (GstPad * pad, GstCaps * caps)
{
  GstPad *target;

  g_return_if_fail (GST_IS_PROXY_PAD (pad));
  g_return_if_fail (GST_IS_CAPS (caps));

  target = gst_proxy_pad_get_target (pad);
  if (target) {
    gst_pad_fixate_caps (target, caps);
    gst_object_unref (target);
  }
}

/**
 * gst_proxy_pad_setcaps_default:
 * @pad: a  #GstPad to set the capabilities of.
 * @caps: (transfer none): a #GstCaps to set.
 *
 * Invoke the default setcaps function of the proxy pad.
 *
 * Returns: TRUE if the caps could be set. FALSE if the caps were not fixed
 * or bad parameters were provided to this function.
 *
 * Since: 0.10.36
 */
gboolean
gst_proxy_pad_setcaps_default (GstPad * pad, GstCaps * caps)
{
  GstPad *target;
  gboolean res;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);
  g_return_val_if_fail (caps == NULL || GST_IS_CAPS (caps), FALSE);

  target = gst_proxy_pad_get_target (pad);
  if (target) {
    res = gst_pad_set_caps (target, caps);
    gst_object_unref (target);
  } else {
    /* We don't have any target, but we shouldn't return FALSE since this
     * would stop the actual push of a buffer (which might trigger a pad block
     * or probe, or properly return GST_FLOW_NOT_LINKED.
     */
    res = TRUE;
  }
  return res;
}

static GstPad *
gst_proxy_pad_get_target (GstPad * pad)
{
  GstPad *target;

  GST_OBJECT_LOCK (pad);
  target = GST_PROXY_PAD_TARGET (pad);
  if (target)
    gst_object_ref (target);
  GST_OBJECT_UNLOCK (pad);

  return target;
}

/**
 * gst_proxy_pad_get_internal:
 * @pad: the #GstProxyPad
 *
 * Get the internal pad of @pad. Unref target pad after usage.
 *
 * The internal pad of a #GstGhostPad is the internally used
 * pad of opposite direction, which is used to link to the target.
 *
 * Returns: (transfer full): the target #GstProxyPad, can be NULL.
 * Unref target pad after usage.
 *
 * Since: 0.10.36
 */
GstProxyPad *
gst_proxy_pad_get_internal (GstProxyPad * pad)
{
  GstPad *internal;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), NULL);

  GST_OBJECT_LOCK (pad);
  internal = GST_PROXY_PAD_INTERNAL (pad);
  if (internal)
    gst_object_ref (internal);
  GST_OBJECT_UNLOCK (pad);

  return GST_PROXY_PAD_CAST (internal);
}

/**
 * gst_proxy_pad_unlink_default:
 * @pad: a #GstPad to unlink
 *
 * Invoke the default unlink function of the proxy pad.
 *
 * Since: 0.10.36
 */
void
gst_proxy_pad_unlink_default (GstPad * pad)
{
  /* nothing to do anymore */
  GST_DEBUG_OBJECT (pad, "pad is unlinked");
}

static void
gst_proxy_pad_class_init (GstProxyPadClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstProxyPadPrivate));

#if !defined(GST_DISABLE_LOADSAVE) && !defined(GST_REMOVE_DEPRECATED)
  {
    GstObjectClass *gstobject_class = (GstObjectClass *) klass;

    gstobject_class->save_thyself =
        ((gpointer (*)(GstObject * object,
                gpointer self)) *
        GST_DEBUG_FUNCPTR (gst_proxy_pad_save_thyself));
  }
#endif
  /* Register common function pointer descriptions */
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_query_type_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_event_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_query_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_iterate_internal_links_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_getcaps_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_acceptcaps_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_fixatecaps_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_setcaps_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_unlink_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_bufferalloc_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_chain_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_chain_list_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_getrange_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_proxy_pad_checkgetrange_default);
}

static void
gst_proxy_pad_init (GstProxyPad * ppad)
{
  GstPad *pad = (GstPad *) ppad;

  GST_PROXY_PAD_PRIVATE (ppad) = G_TYPE_INSTANCE_GET_PRIVATE (ppad,
      GST_TYPE_PROXY_PAD, GstProxyPadPrivate);

  gst_pad_set_query_type_function (pad, gst_proxy_pad_query_type_default);
  gst_pad_set_event_function (pad, gst_proxy_pad_event_default);
  gst_pad_set_query_function (pad, gst_proxy_pad_query_default);
  gst_pad_set_iterate_internal_links_function (pad,
      gst_proxy_pad_iterate_internal_links_default);

  gst_pad_set_getcaps_function (pad, gst_proxy_pad_getcaps_default);
  gst_pad_set_acceptcaps_function (pad, gst_proxy_pad_acceptcaps_default);
  gst_pad_set_fixatecaps_function (pad, gst_proxy_pad_fixatecaps_default);
  gst_pad_set_setcaps_function (pad, gst_proxy_pad_setcaps_default);
  gst_pad_set_unlink_function (pad, gst_proxy_pad_unlink_default);
}

#if !defined(GST_DISABLE_LOADSAVE) && !defined(GST_REMOVE_DEPRECATED)
/**
 * gst_proxy_pad_save_thyself:
 * @pad: a ghost #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the ghost pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
static xmlNodePtr
gst_proxy_pad_save_thyself (GstObject * object, xmlNodePtr parent)
{
  xmlNodePtr self;
  GstProxyPad *proxypad;
  GstPad *pad;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_PROXY_PAD (object), NULL);

  self = xmlNewChild (parent, NULL, (xmlChar *) "ghostpad", NULL);
  xmlNewChild (self, NULL, (xmlChar *) "name",
      (xmlChar *) GST_OBJECT_NAME (object));
  xmlNewChild (self, NULL, (xmlChar *) "parent",
      (xmlChar *) GST_OBJECT_NAME (GST_OBJECT_PARENT (object)));

  proxypad = GST_PROXY_PAD_CAST (object);
  pad = GST_PAD_CAST (proxypad);
  peer = GST_PAD_CAST (pad->peer);

  if (GST_IS_PAD (pad)) {
    if (GST_PAD_IS_SRC (pad))
      xmlNewChild (self, NULL, (xmlChar *) "direction", (xmlChar *) "source");
    else if (GST_PAD_IS_SINK (pad))
      xmlNewChild (self, NULL, (xmlChar *) "direction", (xmlChar *) "sink");
    else
      xmlNewChild (self, NULL, (xmlChar *) "direction", (xmlChar *) "unknown");
  } else {
    xmlNewChild (self, NULL, (xmlChar *) "direction", (xmlChar *) "unknown");
  }
  if (GST_IS_PAD (peer)) {
    gchar *content = g_strdup_printf ("%s.%s",
        GST_OBJECT_NAME (GST_PAD_PARENT (peer)), GST_PAD_NAME (peer));

    xmlNewChild (self, NULL, (xmlChar *) "peer", (xmlChar *) content);
    g_free (content);
  } else {
    xmlNewChild (self, NULL, (xmlChar *) "peer", NULL);
  }

  return self;
}
#endif /* GST_DISABLE_LOADSAVE */


/***********************************************************************
 * Ghost pads, implemented as a pair of proxy pads (sort of)
 */


#define GST_GHOST_PAD_PRIVATE(obj)	(GST_GHOST_PAD_CAST (obj)->priv)

struct _GstGhostPadPrivate
{
  /* with PROXY_LOCK */
  gulong notify_id;

  gboolean constructed;
};

G_DEFINE_TYPE (GstGhostPad, gst_ghost_pad, GST_TYPE_PROXY_PAD);

static void gst_ghost_pad_dispose (GObject * object);

/**
 * gst_ghost_pad_internal_activate_push_default:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether the pad should be active or not.
 *
 * Invoke the default activate push function of a proxy pad that is
 * owned by a ghost pad.
 *
 * Returns: %TRUE if the operation was successful.
 *
 * Since: 0.10.36
 */
gboolean
gst_ghost_pad_internal_activate_push_default (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);

  GST_LOG_OBJECT (pad, "%sactivate push on %s:%s, we're ok",
      (active ? "" : "de"), GST_DEBUG_PAD_NAME (pad));

  /* in both cases (SRC and SINK) we activate just the internal pad. The targets
   * will be activated later (or already in case of a ghost sinkpad). */
  other = GST_PROXY_PAD_INTERNAL (pad);
  ret = gst_pad_activate_push (other, active);

  return ret;
}

/**
 * gst_ghost_pad_internal_activate_pull_default:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether the pad should be active or not.
 *
 * Invoke the default activate pull function of a proxy pad that is
 * owned by a ghost pad.
 *
 * Returns: %TRUE if the operation was successful.
 *
 * Since: 0.10.36
 */
gboolean
gst_ghost_pad_internal_activate_pull_default (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  g_return_val_if_fail (GST_IS_PROXY_PAD (pad), FALSE);

  GST_LOG_OBJECT (pad, "%sactivate pull on %s:%s", (active ? "" : "de"),
      GST_DEBUG_PAD_NAME (pad));

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    /* we are activated in pull mode by our peer element, which is a sinkpad
     * that wants to operate in pull mode. This activation has to propagate
     * upstream through the pipeline. We call the internal activation function,
     * which will trigger gst_ghost_pad_activate_pull_default, which propagates even
     * further upstream */
    GST_LOG_OBJECT (pad, "pad is src, activate internal");
    other = GST_PROXY_PAD_INTERNAL (pad);
    ret = gst_pad_activate_pull (other, active);
  } else if (G_LIKELY ((other = gst_pad_get_peer (pad)))) {
    /* We are SINK, the ghostpad is SRC, we propagate the activation upstream
     * since we hold a pointer to the upstream peer. */
    GST_LOG_OBJECT (pad, "activating peer");
    ret = gst_pad_activate_pull (other, active);
    gst_object_unref (other);
  } else {
    /* this is failure, we can't activate pull if there is no peer */
    GST_LOG_OBJECT (pad, "not src and no peer, failing");
    ret = FALSE;
  }

  return ret;
}

/**
 * gst_ghost_pad_activate_push_default:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether the pad should be active or not.
 *
 * Invoke the default activate push function of a ghost pad.
 *
 * Returns: %TRUE if the operation was successful.
 *
 * Since: 0.10.36
 */
gboolean
gst_ghost_pad_activate_push_default (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  g_return_val_if_fail (GST_IS_GHOST_PAD (pad), FALSE);

  GST_LOG_OBJECT (pad, "%sactivate push on %s:%s, proxy internal",
      (active ? "" : "de"), GST_DEBUG_PAD_NAME (pad));

  /* just activate the internal pad */
  other = GST_PROXY_PAD_INTERNAL (pad);
  ret = gst_pad_activate_push (other, active);

  return ret;
}

/**
 * gst_ghost_pad_activate_pull_default:
 * @pad: the #GstPad to activate or deactivate.
 * @active: whether the pad should be active or not.
 *
 * Invoke the default activate pull function of a ghost pad.
 *
 * Returns: %TRUE if the operation was successful.
 *
 * Since: 0.10.36
 */
gboolean
gst_ghost_pad_activate_pull_default (GstPad * pad, gboolean active)
{
  gboolean ret;
  GstPad *other;

  g_return_val_if_fail (GST_IS_GHOST_PAD (pad), FALSE);

  GST_LOG_OBJECT (pad, "%sactivate pull on %s:%s", (active ? "" : "de"),
      GST_DEBUG_PAD_NAME (pad));

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    /* the ghostpad is SRC and activated in pull mode by its peer, call the
     * activation function of the internal pad to propagate the activation
     * upstream */
    GST_LOG_OBJECT (pad, "pad is src, activate internal");
    other = GST_PROXY_PAD_INTERNAL (pad);
    ret = gst_pad_activate_pull (other, active);
  } else if (G_LIKELY ((other = gst_pad_get_peer (pad)))) {
    /* We are SINK and activated by the internal pad, propagate activation
     * upstream because we hold a ref to the upstream peer */
    GST_LOG_OBJECT (pad, "activating peer");
    ret = gst_pad_activate_pull (other, active);
    gst_object_unref (other);
  } else {
    /* no peer, we fail */
    GST_LOG_OBJECT (pad, "pad not src and no peer, failing");
    ret = FALSE;
  }

  return ret;
}

/**
 * gst_ghost_pad_link_default:
 * @pad: the #GstPad to link.
 * @peer: the #GstPad peer
 *
 * Invoke the default link function of a ghost pad.
 *
 * Returns: #GstPadLinkReturn of the operation
 *
 * Since: 0.10.36
 */
GstPadLinkReturn
gst_ghost_pad_link_default (GstPad * pad, GstPad * peer)
{
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_GHOST_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_PAD (peer), GST_PAD_LINK_REFUSED);

  GST_DEBUG_OBJECT (pad, "linking ghostpad");

  ret = GST_PAD_LINK_OK;
  /* if we are a source pad, we should call the peer link function
   * if the peer has one, see design docs. */
  if (GST_PAD_IS_SRC (pad)) {
    if (GST_PAD_LINKFUNC (peer)) {
      ret = GST_PAD_LINKFUNC (peer) (peer, pad);
      if (ret != GST_PAD_LINK_OK)
        GST_DEBUG_OBJECT (pad, "linking failed");
    }
  }
  return ret;
}

/**
 * gst_ghost_pad_unlink_default:
 * @pad: the #GstPad to link.
 *
 * Invoke the default unlink function of a ghost pad.
 *
 * Since: 0.10.36
 */
void
gst_ghost_pad_unlink_default (GstPad * pad)
{
  g_return_if_fail (GST_IS_GHOST_PAD (pad));

  GST_DEBUG_OBJECT (pad, "unlinking ghostpad");
}

static void
on_int_notify (GstPad * internal, GParamSpec * unused, GstGhostPad * pad)
{
  GstCaps *caps;

  g_object_get (internal, "caps", &caps, NULL);

  GST_DEBUG_OBJECT (pad, "notified %p %" GST_PTR_FORMAT, caps, caps);
  gst_pad_set_caps (GST_PAD_CAST (pad), caps);

  if (caps)
    gst_caps_unref (caps);
}

static void
on_src_target_notify (GstPad * target, GParamSpec * unused, gpointer user_data)
{
  GstProxyPad *proxypad;
  GstGhostPad *gpad;
  GstCaps *caps;

  g_object_get (target, "caps", &caps, NULL);

  GST_OBJECT_LOCK (target);
  /* First check if the peer is still available and our proxy pad */
  if (!GST_PAD_PEER (target) || !GST_IS_PROXY_PAD (GST_PAD_PEER (target))) {
    GST_OBJECT_UNLOCK (target);
    goto done;
  }

  proxypad = GST_PROXY_PAD (GST_PAD_PEER (target));
  GST_OBJECT_LOCK (proxypad);
  /* Now check if the proxypad's internal pad is still there and
   * a ghostpad */
  if (!GST_PROXY_PAD_INTERNAL (proxypad) ||
      !GST_IS_GHOST_PAD (GST_PROXY_PAD_INTERNAL (proxypad))) {
    GST_OBJECT_UNLOCK (proxypad);
    GST_OBJECT_UNLOCK (target);
    goto done;
  }
  gpad = GST_GHOST_PAD (GST_PROXY_PAD_INTERNAL (proxypad));
  g_object_ref (gpad);
  GST_OBJECT_UNLOCK (proxypad);
  GST_OBJECT_UNLOCK (target);

  gst_pad_set_caps (GST_PAD_CAST (gpad), caps);

  g_object_unref (gpad);

done:
  if (caps)
    gst_caps_unref (caps);
}

static void
on_src_target_unlinked (GstPad * pad, GstPad * peer, gpointer user_data)
{
  g_signal_handlers_disconnect_by_func (pad,
      (gpointer) on_src_target_notify, NULL);
}

/**
 * gst_ghost_pad_setcaps_default:
 * @pad: the #GstPad to link.
 * @caps: (transfer none): the #GstCaps to set
 *
 * Invoke the default setcaps function of a ghost pad.
 *
 * Returns: %TRUE if the operation was successful
 *
 * Since: 0.10.36
 */
gboolean
gst_ghost_pad_setcaps_default (GstPad * pad, GstCaps * caps)
{
  g_return_val_if_fail (GST_IS_GHOST_PAD (pad), FALSE);
  g_return_val_if_fail (caps == NULL || GST_IS_CAPS (caps), FALSE);

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
    return TRUE;

  return gst_proxy_pad_setcaps_default (pad, caps);
}

static void
gst_ghost_pad_class_init (GstGhostPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGhostPadPrivate));

  pspec_caps = g_object_class_find_property (gobject_class, "caps");

  gobject_class->dispose = gst_ghost_pad_dispose;

  GST_DEBUG_REGISTER_FUNCPTR (gst_ghost_pad_setcaps_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_ghost_pad_activate_pull_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_ghost_pad_activate_push_default);
  GST_DEBUG_REGISTER_FUNCPTR (gst_ghost_pad_link_default);
}

static void
gst_ghost_pad_init (GstGhostPad * pad)
{
  GST_GHOST_PAD_PRIVATE (pad) = G_TYPE_INSTANCE_GET_PRIVATE (pad,
      GST_TYPE_GHOST_PAD, GstGhostPadPrivate);

  gst_pad_set_setcaps_function (GST_PAD_CAST (pad),
      gst_ghost_pad_setcaps_default);
  gst_pad_set_activatepull_function (GST_PAD_CAST (pad),
      gst_ghost_pad_activate_pull_default);
  gst_pad_set_activatepush_function (GST_PAD_CAST (pad),
      gst_ghost_pad_activate_push_default);
}

static void
gst_ghost_pad_dispose (GObject * object)
{
  GstPad *pad;
  GstPad *internal;
  GstPad *peer;

  pad = GST_PAD (object);

  GST_DEBUG_OBJECT (pad, "dispose");

  gst_ghost_pad_set_target (GST_GHOST_PAD (pad), NULL);

  /* Unlink here so that gst_pad_dispose doesn't. That would lead to a call to
   * gst_ghost_pad_unlink_default when the ghost pad is in an inconsistent state */
  peer = gst_pad_get_peer (pad);
  if (peer) {
    if (GST_PAD_IS_SRC (pad))
      gst_pad_unlink (pad, peer);
    else
      gst_pad_unlink (peer, pad);

    gst_object_unref (peer);
  }

  GST_OBJECT_LOCK (pad);
  internal = GST_PROXY_PAD_INTERNAL (pad);

  gst_pad_set_activatepull_function (internal, NULL);
  gst_pad_set_activatepush_function (internal, NULL);

  g_signal_handler_disconnect (internal,
      GST_GHOST_PAD_PRIVATE (pad)->notify_id);

  /* disposes of the internal pad, since the ghostpad is the only possible object
   * that has a refcount on the internal pad. */
  gst_object_unparent (GST_OBJECT_CAST (internal));
  GST_PROXY_PAD_INTERNAL (pad) = NULL;

  GST_OBJECT_UNLOCK (pad);

  G_OBJECT_CLASS (gst_ghost_pad_parent_class)->dispose (object);
}

/**
 * gst_ghost_pad_construct:
 * @gpad: the newly allocated ghost pad
 *
 * Finish initialization of a newly allocated ghost pad.
 *
 * This function is most useful in language bindings and when subclassing
 * #GstGhostPad; plugin and application developers normally will not call this
 * function. Call this function directly after a call to g_object_new
 * (GST_TYPE_GHOST_PAD, "direction", @dir, ..., NULL).
 *
 * Returns: %TRUE if the construction succeeds, %FALSE otherwise.
 *
 * Since: 0.10.22
 */
gboolean
gst_ghost_pad_construct (GstGhostPad * gpad)
{
  GstPadDirection dir, otherdir;
  GstPadTemplate *templ;
  GstPad *pad, *internal;

  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), FALSE);
  g_return_val_if_fail (GST_GHOST_PAD_PRIVATE (gpad)->constructed == FALSE,
      FALSE);

  g_object_get (gpad, "direction", &dir, "template", &templ, NULL);

  g_return_val_if_fail (dir != GST_PAD_UNKNOWN, FALSE);

  pad = GST_PAD (gpad);

  /* Set directional padfunctions for ghostpad */
  if (dir == GST_PAD_SINK) {
    gst_pad_set_bufferalloc_function (pad, gst_proxy_pad_bufferalloc_default);
    gst_pad_set_chain_function (pad, gst_proxy_pad_chain_default);
    gst_pad_set_chain_list_function (pad, gst_proxy_pad_chain_list_default);
  } else {
    gst_pad_set_getrange_function (pad, gst_proxy_pad_getrange_default);
    gst_pad_set_checkgetrange_function (pad,
        gst_proxy_pad_checkgetrange_default);
  }

  /* link/unlink functions */
  gst_pad_set_link_function (pad, gst_ghost_pad_link_default);
  gst_pad_set_unlink_function (pad, gst_ghost_pad_unlink_default);

  /* INTERNAL PAD, it always exists and is child of the ghostpad */
  otherdir = (dir == GST_PAD_SRC) ? GST_PAD_SINK : GST_PAD_SRC;
  if (templ) {
    internal =
        g_object_new (GST_TYPE_PROXY_PAD, "name", NULL,
        "direction", otherdir, "template", templ, NULL);
    /* release ref obtained via g_object_get */
    gst_object_unref (templ);
  } else {
    internal =
        g_object_new (GST_TYPE_PROXY_PAD, "name", NULL,
        "direction", otherdir, NULL);
  }
  GST_PAD_UNSET_FLUSHING (internal);

  /* Set directional padfunctions for internal pad */
  if (dir == GST_PAD_SRC) {
    gst_pad_set_bufferalloc_function (internal,
        gst_proxy_pad_bufferalloc_default);
    gst_pad_set_chain_function (internal, gst_proxy_pad_chain_default);
    gst_pad_set_chain_list_function (internal,
        gst_proxy_pad_chain_list_default);
  } else {
    gst_pad_set_getrange_function (internal, gst_proxy_pad_getrange_default);
    gst_pad_set_checkgetrange_function (internal,
        gst_proxy_pad_checkgetrange_default);
  }

  GST_OBJECT_LOCK (pad);

  /* now make the ghostpad a parent of the internal pad */
  if (!gst_object_set_parent (GST_OBJECT_CAST (internal),
          GST_OBJECT_CAST (pad)))
    goto parent_failed;

  /* The ghostpad is the parent of the internal pad and is the only object that
   * can have a refcount on the internal pad.
   * At this point, the GstGhostPad has a refcount of 1, and the internal pad has
   * a refcount of 1.
   * When the refcount of the GstGhostPad drops to 0, the ghostpad will dispose
   * its refcount on the internal pad in the dispose method by un-parenting it.
   * This is why we don't take extra refcounts in the assignments below
   */
  GST_PROXY_PAD_INTERNAL (pad) = internal;
  GST_PROXY_PAD_INTERNAL (internal) = pad;

  /* could be more general here, iterating over all writable properties...
   * taking the short road for now tho */
  GST_GHOST_PAD_PRIVATE (pad)->notify_id =
      g_signal_connect (internal, "notify::caps", G_CALLBACK (on_int_notify),
      pad);

  /* special activation functions for the internal pad */
  gst_pad_set_activatepull_function (internal,
      gst_ghost_pad_internal_activate_pull_default);
  gst_pad_set_activatepush_function (internal,
      gst_ghost_pad_internal_activate_push_default);

  GST_OBJECT_UNLOCK (pad);

  /* call function to init values of the pad caps */
  on_int_notify (internal, NULL, GST_GHOST_PAD_CAST (pad));

  GST_GHOST_PAD_PRIVATE (gpad)->constructed = TRUE;
  return TRUE;

  /* ERRORS */
parent_failed:
  {
    GST_WARNING_OBJECT (gpad, "Could not set internal pad %s:%s",
        GST_DEBUG_PAD_NAME (internal));
    g_critical ("Could not set internal pad %s:%s",
        GST_DEBUG_PAD_NAME (internal));
    GST_OBJECT_UNLOCK (pad);
    gst_object_unref (internal);
    return FALSE;
  }
}

static GstPad *
gst_ghost_pad_new_full (const gchar * name, GstPadDirection dir,
    GstPadTemplate * templ)
{
  GstGhostPad *ret;

  g_return_val_if_fail (dir != GST_PAD_UNKNOWN, NULL);

  /* OBJECT CREATION */
  if (templ) {
    ret = g_object_new (GST_TYPE_GHOST_PAD, "name", name,
        "direction", dir, "template", templ, NULL);
  } else {
    ret = g_object_new (GST_TYPE_GHOST_PAD, "name", name,
        "direction", dir, NULL);
  }

  if (!gst_ghost_pad_construct (ret))
    goto construct_failed;

  return GST_PAD_CAST (ret);

construct_failed:
  /* already logged */
  gst_object_unref (ret);
  return NULL;
}

/**
 * gst_ghost_pad_new_no_target:
 * @name: (allow-none): the name of the new pad, or NULL to assign a default name.
 * @dir: the direction of the ghostpad
 *
 * Create a new ghostpad without a target with the given direction.
 * A target can be set on the ghostpad later with the
 * gst_ghost_pad_set_target() function.
 *
 * The created ghostpad will not have a padtemplate.
 *
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new_no_target (const gchar * name, GstPadDirection dir)
{
  GstPad *ret;

  g_return_val_if_fail (dir != GST_PAD_UNKNOWN, NULL);

  GST_LOG ("name:%s, direction:%d", GST_STR_NULL (name), dir);

  ret = gst_ghost_pad_new_full (name, dir, NULL);

  return ret;
}

/**
 * gst_ghost_pad_new:
 * @name: (allow-none): the name of the new pad, or NULL to assign a default name
 * @target: (transfer none): the pad to ghost.
 *
 * Create a new ghostpad with @target as the target. The direction will be taken
 * from the target pad. @target must be unlinked.
 *
 * Will ref the target.
 *
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new (const gchar * name, GstPad * target)
{
  GstPad *ret;

  g_return_val_if_fail (GST_IS_PAD (target), NULL);
  g_return_val_if_fail (!gst_pad_is_linked (target), NULL);

  GST_LOG ("name:%s, target:%s:%s", GST_STR_NULL (name),
      GST_DEBUG_PAD_NAME (target));

  if ((ret = gst_ghost_pad_new_no_target (name, GST_PAD_DIRECTION (target))))
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ret), target))
      goto set_target_failed;

  return ret;

  /* ERRORS */
set_target_failed:
  {
    GST_WARNING_OBJECT (ret, "failed to set target %s:%s",
        GST_DEBUG_PAD_NAME (target));
    gst_object_unref (ret);
    return NULL;
  }
}

/**
 * gst_ghost_pad_new_from_template:
 * @name: (allow-none): the name of the new pad, or NULL to assign a default name.
 * @target: (transfer none): the pad to ghost.
 * @templ: (transfer none): the #GstPadTemplate to use on the ghostpad.
 *
 * Create a new ghostpad with @target as the target. The direction will be taken
 * from the target pad. The template used on the ghostpad will be @template.
 *
 * Will ref the target.
 *
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
 *
 * Since: 0.10.10
 */

GstPad *
gst_ghost_pad_new_from_template (const gchar * name, GstPad * target,
    GstPadTemplate * templ)
{
  GstPad *ret;

  g_return_val_if_fail (GST_IS_PAD (target), NULL);
  g_return_val_if_fail (!gst_pad_is_linked (target), NULL);
  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_PAD_TEMPLATE_DIRECTION (templ) ==
      GST_PAD_DIRECTION (target), NULL);

  GST_LOG ("name:%s, target:%s:%s, templ:%p", GST_STR_NULL (name),
      GST_DEBUG_PAD_NAME (target), templ);

  if ((ret = gst_ghost_pad_new_full (name, GST_PAD_DIRECTION (target), templ)))
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ret), target))
      goto set_target_failed;

  return ret;

  /* ERRORS */
set_target_failed:
  {
    GST_WARNING_OBJECT (ret, "failed to set target %s:%s",
        GST_DEBUG_PAD_NAME (target));
    gst_object_unref (ret);
    return NULL;
  }
}

/**
 * gst_ghost_pad_new_no_target_from_template:
 * @name: (allow-none): the name of the new pad, or NULL to assign a default name
 * @templ: (transfer none): the #GstPadTemplate to create the ghostpad from.
 *
 * Create a new ghostpad based on @templ, without setting a target. The
 * direction will be taken from the @templ.
 *
 * Returns: (transfer full): a new #GstPad, or NULL in case of an error.
 *
 * Since: 0.10.10
 */
GstPad *
gst_ghost_pad_new_no_target_from_template (const gchar * name,
    GstPadTemplate * templ)
{
  GstPad *ret;

  g_return_val_if_fail (templ != NULL, NULL);

  ret =
      gst_ghost_pad_new_full (name, GST_PAD_TEMPLATE_DIRECTION (templ), templ);

  return ret;
}

/**
 * gst_ghost_pad_get_target:
 * @gpad: the #GstGhostPad
 *
 * Get the target pad of @gpad. Unref target pad after usage.
 *
 * Returns: (transfer full): the target #GstPad, can be NULL if the ghostpad
 * has no target set. Unref target pad after usage.
 */
GstPad *
gst_ghost_pad_get_target (GstGhostPad * gpad)
{
  GstPad *ret;

  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), NULL);

  ret = gst_proxy_pad_get_target (GST_PAD_CAST (gpad));

  GST_DEBUG_OBJECT (gpad, "get target %s:%s", GST_DEBUG_PAD_NAME (ret));

  return ret;
}

/**
 * gst_ghost_pad_set_target:
 * @gpad: the #GstGhostPad
 * @newtarget: (transfer none) (allow-none): the new pad target
 *
 * Set the new target of the ghostpad @gpad. Any existing target
 * is unlinked and links to the new target are established. if @newtarget is
 * NULL the target will be cleared.
 *
 * Returns: (transfer full): TRUE if the new target could be set. This function
 *     can return FALSE when the internal pads could not be linked.
 */
gboolean
gst_ghost_pad_set_target (GstGhostPad * gpad, GstPad * newtarget)
{
  GstPad *internal;
  GstPad *oldtarget;
  GstPadLinkReturn lret;

  g_return_val_if_fail (GST_IS_GHOST_PAD (gpad), FALSE);
  g_return_val_if_fail (GST_PAD_CAST (gpad) != newtarget, FALSE);
  g_return_val_if_fail (newtarget != GST_PROXY_PAD_INTERNAL (gpad), FALSE);

  /* no need for locking, the internal pad's lifecycle is directly linked to the
   * ghostpad's */
  internal = GST_PROXY_PAD_INTERNAL (gpad);

  if (newtarget)
    GST_DEBUG_OBJECT (gpad, "set target %s:%s", GST_DEBUG_PAD_NAME (newtarget));
  else
    GST_DEBUG_OBJECT (gpad, "clearing target");

  /* clear old target */
  GST_OBJECT_LOCK (gpad);
  if ((oldtarget = GST_PROXY_PAD_TARGET (gpad))) {
    GST_OBJECT_UNLOCK (gpad);

    /* unlink internal pad */
    if (GST_PAD_IS_SRC (internal))
      gst_pad_unlink (internal, oldtarget);
    else
      gst_pad_unlink (oldtarget, internal);
  } else {
    GST_OBJECT_UNLOCK (gpad);
  }

  if (newtarget) {
    if (GST_PAD_IS_SRC (newtarget)) {
      g_signal_connect (newtarget, "notify::caps",
          G_CALLBACK (on_src_target_notify), NULL);
      g_signal_connect (newtarget, "unlinked",
          G_CALLBACK (on_src_target_unlinked), NULL);
    }

    /* and link to internal pad without any checks */
    GST_DEBUG_OBJECT (gpad, "connecting internal pad to target");

    if (GST_PAD_IS_SRC (internal))
      lret =
          gst_pad_link_full (internal, newtarget, GST_PAD_LINK_CHECK_NOTHING);
    else
      lret =
          gst_pad_link_full (newtarget, internal, GST_PAD_LINK_CHECK_NOTHING);

    if (lret != GST_PAD_LINK_OK)
      goto link_failed;
  }

  return TRUE;

  /* ERRORS */
link_failed:
  {
    GST_WARNING_OBJECT (gpad, "could not link internal and target, reason:%d",
        lret);
    return FALSE;
  }
}
