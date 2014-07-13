/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@gmail.com>
 *               <2004-2008> Edward Hervey <bilboed@bilboed.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gnl.h"

/**
 * SECTION:element-gnlsource
 *
 * The GnlSource encapsulates a pipeline which produces data for processing
 * in a #GnlComposition.
 */

static GstStaticPadTemplate gnl_source_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gnlsource);
#define GST_CAT_DEFAULT gnlsource

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gnlsource, "gnlsource", GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Source Element");
#define gnl_source_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GnlSource, gnl_source, GNL_TYPE_OBJECT, _do_init);

struct _GnlSourcePrivate
{
  gboolean dispose_has_run;

  gboolean dynamicpads;         /* TRUE if the controlled element has dynamic pads */
  GstEvent *event;              /* queued event */

  gulong padremovedid;          /* signal handler for element pad-removed signal */
  gulong padaddedid;            /* signal handler for element pad-added signal */
  gint probeid;                 /* source pad probe id */

  gboolean pendingblock;        /* We have a pending pad_block */
  gboolean is_blocked;          /* We already got blocked */
  GstPad *ghostedpad;           /* Pad (to be) ghosted */
  GstPad *staticpad;            /* The only pad. We keep an extra ref */
  gboolean got_seeked;
};

static gboolean gnl_source_prepare (GnlObject * object);
static gboolean gnl_source_cleanup (GnlObject * object);

static gboolean gnl_source_add_element (GstBin * bin, GstElement * element);

static gboolean gnl_source_remove_element (GstBin * bin, GstElement * element);

static void gnl_source_dispose (GObject * object);

static GstPadProbeReturn
pad_blocked_cb (GstPad * pad, GstPadProbeInfo * info, GnlSource * source);

static gboolean
gnl_source_control_element_func (GnlSource * source, GstElement * element);

static void
gnl_source_class_init (GnlSourceClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  GnlObjectClass *gnlobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;
  gnlobject_class = (GnlObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GnlSourcePrivate));

  gst_element_class_set_static_metadata (gstelement_class, "GNonLin Source",
      "Filter/Editor",
      "Manages source elements",
      "Wim Taymans <wim.taymans@gmail.com>, Edward Hervey <bilboed@bilboed.com>");

  parent_class = g_type_class_ref (GNL_TYPE_OBJECT);

  klass->control_element = GST_DEBUG_FUNCPTR (gnl_source_control_element_func);

  gnlobject_class->prepare = GST_DEBUG_FUNCPTR (gnl_source_prepare);
  gnlobject_class->cleanup = GST_DEBUG_FUNCPTR (gnl_source_cleanup);

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (gnl_source_add_element);
  gstbin_class->remove_element = GST_DEBUG_FUNCPTR (gnl_source_remove_element);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gnl_source_dispose);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gnl_source_src_template));

}


static void
gnl_source_init (GnlSource * source)
{
  GST_OBJECT_FLAG_SET (source, GNL_OBJECT_SOURCE);
  source->element = NULL;
  source->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (source, GNL_TYPE_SOURCE, GnlSourcePrivate);

  GST_DEBUG_OBJECT (source, "Setting GstBin async-handling to TRUE");
  g_object_set (G_OBJECT (source), "async-handling", TRUE, NULL);
}

static void
gnl_source_dispose (GObject * object)
{
  GnlObject *gnlobject = (GnlObject *) object;
  GnlSource *source = (GnlSource *) object;
  GnlSourcePrivate *priv = source->priv;

  GST_DEBUG_OBJECT (object, "dispose");

  if (priv->dispose_has_run)
    return;

  if (source->element) {
    gst_object_unref (source->element);
    source->element = NULL;
  }

  priv->dispose_has_run = TRUE;
  if (priv->event)
    gst_event_unref (priv->event);

  if (priv->ghostedpad)
    gnl_object_ghost_pad_set_target (gnlobject, gnlobject->srcpad, NULL);

  if (priv->staticpad) {
    gst_object_unref (priv->staticpad);
    priv->staticpad = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
_remove_pad_probe (GnlSource * source, GstPad * pad)
{
  gint tmp_probe_id;

  tmp_probe_id = source->priv->probeid;
  if (g_atomic_int_compare_and_exchange (&source->priv->probeid,
          tmp_probe_id, 0)) {
    if (tmp_probe_id)
      gst_pad_remove_probe (pad, tmp_probe_id);
  }

}

static void
element_pad_added_cb (GstElement * element G_GNUC_UNUSED, GstPad * pad,
    GnlSource * source)
{
  GstCaps *srccaps;
  GnlSourcePrivate *priv = source->priv;
  GnlObject *gnlobject = (GnlObject *) source;

  GST_DEBUG_OBJECT (source, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (priv->pendingblock) {
    GST_WARNING_OBJECT (source,
        "We already have (pending) ghost-ed a valid source pad (srcpad:%s:%s, pendingblock:%d",
        GST_DEBUG_PAD_NAME (gnlobject->srcpad), priv->pendingblock);
    return;
  }

  /* FIXME: pass filter caps to query_caps directly */
  srccaps = gst_pad_query_caps (pad, NULL);
  if (gnlobject->caps && !gst_caps_can_intersect (srccaps, gnlobject->caps)) {
    gst_caps_unref (srccaps);
    GST_DEBUG_OBJECT (source, "Pad doesn't have valid caps, ignoring");
    return;
  }
  gst_caps_unref (srccaps);

  GST_DEBUG_OBJECT (pad, "valid pad, about to add event probe and pad block");

  priv->probeid = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) pad_blocked_cb, source, NULL);
  if (priv->probeid == 0)
    GST_WARNING_OBJECT (source, "Couldn't set Async pad blocking");
  else {
    priv->ghostedpad = pad;
    priv->pendingblock = TRUE;
  }

  GST_DEBUG_OBJECT (source, "Done handling pad %s:%s",
      GST_DEBUG_PAD_NAME (pad));
}

static void
element_pad_removed_cb (GstElement * element G_GNUC_UNUSED, GstPad * pad,
    GnlSource * source)
{
  GnlSourcePrivate *priv = source->priv;
  GnlObject *gnlobject = (GnlObject *) source;

  GST_DEBUG_OBJECT (source, "pad %s:%s (controlled pad %s:%s)",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (priv->ghostedpad));

  if (pad == priv->ghostedpad) {
    GST_DEBUG_OBJECT (source,
        "The removed pad is the controlled pad, clearing up");

    GST_DEBUG_OBJECT (source, "Clearing up ghostpad");

    priv->is_blocked = FALSE;
    _remove_pad_probe (source, pad);
    gnl_object_ghost_pad_set_target (GNL_OBJECT (source), gnlobject->srcpad,
        NULL);

    priv->pendingblock = FALSE;
    priv->ghostedpad = NULL;
  } else {
    GST_DEBUG_OBJECT (source, "The removed pad is NOT our controlled pad");
  }
}

static gint
compare_src_pad (GValue * item, GstCaps * caps)
{
  gint ret = 1;
  GstPad *pad = g_value_get_object (item);
  GstCaps *padcaps;

  GST_DEBUG_OBJECT (pad, "Trying pad for caps %" GST_PTR_FORMAT, caps);

  /* FIXME: can pass the filter caps right away.. */
  padcaps = gst_pad_query_caps (pad, NULL);

  if (gst_caps_can_intersect (padcaps, caps))
    ret = 0;

  gst_caps_unref (padcaps);

  return ret;
}

/*
  get_valid_src_pad

  Returns True if there's a src pad compatible with the GnlObject caps in the
  given element. Fills in pad if so. The returned pad has an incremented refcount
*/

static gboolean
get_valid_src_pad (GnlSource * source, GstElement * element, GstPad ** pad)
{
  gboolean res = FALSE;
  GstIterator *srcpads;
  GValue item = { 0, };

  g_return_val_if_fail (pad, FALSE);

  srcpads = gst_element_iterate_src_pads (element);
  if (gst_iterator_find_custom (srcpads, (GCompareFunc) compare_src_pad, &item,
          GNL_OBJECT (source)->caps)) {
    *pad = g_value_get_object (&item);
    gst_object_ref (*pad);
    g_value_reset (&item);
    res = TRUE;
  }
  gst_iterator_free (srcpads);

  return res;
}

static gpointer
ghost_seek_pad (GnlSource * source)
{
  GnlSourcePrivate *priv = source->priv;
  GstPad *pad = priv->ghostedpad;
  GnlObject *gnlobject = (GnlObject *) source;

  priv->got_seeked = TRUE;

  if (!pad)
    goto beach;

  GST_DEBUG_OBJECT (source, "ghosting %s:%s", GST_DEBUG_PAD_NAME (pad));

  gnl_object_ghost_pad_set_target (gnlobject, gnlobject->srcpad, pad);

  /*FIXME : do that when going to PAUSED */
  gst_pad_set_active (gnlobject->srcpad, TRUE);

  GST_DEBUG_OBJECT (source, "about to unblock %s:%s", GST_DEBUG_PAD_NAME (pad));
  /* FIXME: Kill that code please! */
  if (g_strcmp0 (GST_OBJECT_NAME (GST_OBJECT_PARENT (source)), "current-bin")
      && priv->event) {
    if (!(gst_pad_send_event (gnlobject->srcpad, priv->event)))
      GST_ELEMENT_ERROR (source, RESOURCE, SEEK,
          (NULL), ("Sending initial seek to upstream element failed"));
    else
      GST_ERROR_OBJECT (source, "queued seek sent");
    priv->event = NULL;
  }

  priv->is_blocked = FALSE;
  _remove_pad_probe (source, pad);
  gst_element_no_more_pads (GST_ELEMENT (source));

  priv->pendingblock = FALSE;

beach:
  return NULL;
}

static GstPadProbeReturn
pad_blocked_cb (GstPad * pad, GstPadProbeInfo * info, GnlSource * source)
{
  GST_DEBUG_OBJECT (pad, "probe callback");

  if (!source->priv->got_seeked && !source->priv->is_blocked) {
    GThread *lthread;

    source->priv->is_blocked = TRUE;
    GST_DEBUG_OBJECT (pad, "starting thread to call ghost_seek_pad");
    lthread =
        g_thread_new ("gnlsourceseek", (GThreadFunc) ghost_seek_pad, source);
    g_thread_unref (lthread);
  }

  return GST_PAD_PROBE_OK;
}


/*
 * has_dynamic_pads
 * Returns TRUE if the element has only dynamic pads.
 */

static gboolean
has_dynamic_srcpads (GstElement * element)
{
  gboolean ret = TRUE;
  GList *templates;
  GstPadTemplate *template;

  templates =
      gst_element_class_get_pad_template_list (GST_ELEMENT_GET_CLASS (element));

  while (templates) {
    template = (GstPadTemplate *) templates->data;

    if ((GST_PAD_TEMPLATE_DIRECTION (template) == GST_PAD_SRC)
        && (GST_PAD_TEMPLATE_PRESENCE (template) == GST_PAD_ALWAYS)) {
      ret = FALSE;
      break;
    }

    templates = templates->next;
  }

  return ret;
}

static gboolean
gnl_source_control_element_func (GnlSource * source, GstElement * element)
{
  GnlSourcePrivate *priv = source->priv;
  GstPad *pad = NULL;

  g_return_val_if_fail (source->element == NULL, FALSE);

  GST_DEBUG_OBJECT (source, "element:%s, source->element:%p",
      GST_ELEMENT_NAME (element), source->element);

  source->element = element;
  gst_object_ref (element);

  if (get_valid_src_pad (source, source->element, &pad)) {
    priv->staticpad = pad;
    GST_DEBUG_OBJECT (source,
        "There is a valid source pad, we consider the object as NOT having dynamic pads");
    priv->dynamicpads = FALSE;
  } else {
    priv->dynamicpads = has_dynamic_srcpads (element);
    GST_DEBUG_OBJECT (source, "No valid source pad yet, dynamicpads:%d",
        priv->dynamicpads);
    if (priv->dynamicpads) {
      /* connect to pad-added/removed signals */
      priv->padremovedid = g_signal_connect
          (G_OBJECT (element), "pad-removed",
          G_CALLBACK (element_pad_removed_cb), source);
      priv->padaddedid =
          g_signal_connect (G_OBJECT (element), "pad-added",
          G_CALLBACK (element_pad_added_cb), source);
    }
  }

  return TRUE;
}

static gboolean
gnl_source_add_element (GstBin * bin, GstElement * element)
{
  GnlSource *source = (GnlSource *) bin;
  gboolean pret;

  GST_DEBUG_OBJECT (source, "Adding element %s", GST_ELEMENT_NAME (element));

  if (source->element) {
    GST_WARNING_OBJECT (bin, "GnlSource can only handle one element at a time");
    return FALSE;
  }

  /* call parent add_element */
  pret = GST_BIN_CLASS (parent_class)->add_element (bin, element);

  if (pret) {
    gnl_source_control_element_func (source, element);
  }
  return pret;
}

static gboolean
gnl_source_remove_element (GstBin * bin, GstElement * element)
{
  GnlSource *source = (GnlSource *) bin;
  GnlObject *gnlobject = (GnlObject *) element;
  GnlSourcePrivate *priv = source->priv;
  gboolean pret;

  GST_DEBUG_OBJECT (source, "Removing element %s", GST_ELEMENT_NAME (element));

  /* try to remove it */
  pret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);

  if ((!source->element) || (source->element != element)) {
    return TRUE;
  }

  if (pret) {
    gnl_object_ghost_pad_set_target (GNL_OBJECT (source), gnlobject->srcpad,
        NULL);
    priv->got_seeked = FALSE;

    /* discard events */
    if (priv->event) {
      gst_event_unref (priv->event);
      priv->event = NULL;
    }

    /* remove signal handlers */
    if (priv->padremovedid) {
      g_signal_handler_disconnect (source->element, priv->padremovedid);
      priv->padremovedid = 0;
    }
    if (priv->padaddedid) {
      g_signal_handler_disconnect (source->element, priv->padaddedid);
      priv->padaddedid = 0;
    }

    priv->dynamicpads = FALSE;
    gst_object_unref (element);
    source->element = NULL;
  }
  return pret;
}

static gboolean
gnl_source_prepare (GnlObject * object)
{
  GnlSource *source = GNL_SOURCE (object);
  GnlSourcePrivate *priv = source->priv;
  GstElement *parent =
      (GstElement *) gst_element_get_parent ((GstElement *) object);

  if (!source->element) {
    GST_WARNING_OBJECT (source,
        "GnlSource doesn't have an element to control !");
    if (parent)
      gst_object_unref (parent);
    return FALSE;
  }

  GST_LOG_OBJECT (source, "srcpad:%p, dynamicpads:%d",
      object->srcpad, priv->dynamicpads);

  if (!(priv->got_seeked) && !priv->pendingblock) {
    GstPad *pad;

    GST_LOG_OBJECT (source, "no ghostpad and no dynamic pads");

    /* Do an async block on valid source pad */

    if (!priv->staticpad
        && !(get_valid_src_pad (source, source->element, &pad))) {
      GST_DEBUG_OBJECT (source, "Couldn't find a valid source pad");
    } else {
      if (priv->staticpad)
        pad = gst_object_ref (priv->staticpad);
      GST_LOG_OBJECT (source, "Trying to async block source pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      priv->ghostedpad = pad;
      priv->probeid = gst_pad_add_probe (pad,
          GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
          (GstPadProbeCallback) pad_blocked_cb, source, NULL);
      gst_object_unref (pad);
    }
  }

  if (!GNL_IS_COMPOSITION (parent)) {
    /* Figure out if we're in a composition */
    if (source->priv->event)
      gst_event_unref (source->priv->event);

    GST_DEBUG_OBJECT (object, "Creating initial seek");

    source->priv->event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
        GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH,
        GST_SEEK_TYPE_SET, object->start, GST_SEEK_TYPE_SET, object->stop);
  }

  gst_object_unref (parent);

  return TRUE;
}

static gboolean
gnl_source_cleanup (GnlObject * object)
{
  GnlSource *source = GNL_SOURCE (object);
  GnlSourcePrivate *priv = source->priv;

  /* FIXME : should just be ghostedpad */
  GstPad *target = gst_ghost_pad_get_target ((GstGhostPad *) object->srcpad);

  if (target) {
    _remove_pad_probe (source, target);
    gst_object_unref (target);
  }

  gnl_object_ghost_pad_set_target (GNL_OBJECT (source), object->srcpad, NULL);
  priv->got_seeked = FALSE;
  priv->ghostedpad = NULL;
  priv->is_blocked = FALSE;
  priv->pendingblock = FALSE;

  return TRUE;
}
