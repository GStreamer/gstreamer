/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
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

#include "nle.h"

/**
 * SECTION:element-nleoperation
 *
 * <refsect2>
 * <para>
 * A NleOperation performs a transformation or mixing operation on the
 * data from one or more #NleSources, which is used to implement filters or 
 * effects.
 * </para>
 * </refsect2>
 */

static GstStaticPadTemplate nle_operation_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate nle_operation_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (nleoperation);
#define GST_CAT_DEFAULT nleoperation

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (nleoperation, "nleoperation", GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Operation element");
#define nle_operation_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (NleOperation, nle_operation, NLE_TYPE_OBJECT,
    _do_init);

enum
{
  ARG_0,
  ARG_SINKS,
};

enum
{
  INPUT_PRIORITY_CHANGED,
  LAST_SIGNAL
};

static guint nle_operation_signals[LAST_SIGNAL] = { 0 };

static void nle_operation_dispose (GObject * object);

static void nle_operation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void nle_operation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean nle_operation_prepare (NleObject * object);
static gboolean nle_operation_cleanup (NleObject * object);

static gboolean nle_operation_add_element (GstBin * bin, GstElement * element);
static gboolean nle_operation_remove_element (GstBin * bin,
    GstElement * element);

static GstPad *nle_operation_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void nle_operation_release_pad (GstElement * element, GstPad * pad);

static void synchronize_sinks (NleOperation * operation);
static gboolean remove_sink_pad (NleOperation * operation, GstPad * sinkpad);

static void
nle_operation_class_init (NleOperationClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBinClass *gstbin_class = (GstBinClass *) klass;

  GstElementClass *gstelement_class = (GstElementClass *) klass;
  NleObjectClass *nleobject_class = (NleObjectClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class, "GNonLin Operation",
      "Filter/Editor",
      "Encapsulates filters/effects for use with NLE Objects",
      "Wim Taymans <wim.taymans@gmail.com>, Edward Hervey <bilboed@bilboed.com>");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (nle_operation_dispose);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (nle_operation_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (nle_operation_get_property);

  /**
   * NleOperation:sinks:
   *
   * Specifies the number of sink pads the operation should provide.
   * If the sinks property is -1 (the default) pads are only created as
   * demanded via get_request_pad() calls on the element.
   */
  g_object_class_install_property (gobject_class, ARG_SINKS,
      g_param_spec_int ("sinks", "Sinks",
          "Number of input sinks (-1 for automatic handling)", -1, G_MAXINT, -1,
          G_PARAM_READWRITE));

  /**
   * NleOperation:input-priority-changed:
   * @pad: The operation's input pad whose priority changed.
   * @priority: The new priority
   *
   * Signals that the @priority of the stream being fed to the given @pad
   * might have changed.
   */
  nle_operation_signals[INPUT_PRIORITY_CHANGED] =
      g_signal_new ("input-priority-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (NleOperationClass,
          input_priority_changed), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, GST_TYPE_PAD, G_TYPE_UINT);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (nle_operation_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (nle_operation_release_pad);

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (nle_operation_add_element);
  gstbin_class->remove_element =
      GST_DEBUG_FUNCPTR (nle_operation_remove_element);

  nleobject_class->prepare = GST_DEBUG_FUNCPTR (nle_operation_prepare);
  nleobject_class->cleanup = GST_DEBUG_FUNCPTR (nle_operation_cleanup);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&nle_operation_src_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&nle_operation_sink_template));

}

static void
nle_operation_dispose (GObject * object)
{
  NleOperation *oper = (NleOperation *) object;

  GST_DEBUG_OBJECT (object, "Disposing of source pad");

  nle_object_ghost_pad_set_target (NLE_OBJECT (object),
      NLE_OBJECT (object)->srcpad, NULL);

  GST_DEBUG_OBJECT (object, "Disposing of sink pad(s)");
  while (oper->sinks) {
    GstPad *ghost = (GstPad *) oper->sinks->data;
    remove_sink_pad (oper, ghost);
  }

  GST_DEBUG_OBJECT (object, "Done, calling parent class ::dispose()");
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
nle_operation_reset (NleOperation * operation)
{
  operation->num_sinks = 1;
  operation->realsinks = 0;
  operation->next_base_time = 0;
}

static void
nle_operation_init (NleOperation * operation)
{
  nle_operation_reset (operation);
  operation->element = NULL;
}

static gboolean
element_is_valid_filter (GstElement * element, gboolean * isdynamic)
{
  gboolean havesink = FALSE;
  gboolean havesrc = FALSE;
  gboolean done = FALSE;
  GstIterator *pads;
  GValue item = { 0, };

  if (isdynamic)
    *isdynamic = FALSE;

  pads = gst_element_iterate_pads (element);

  while (!done) {
    switch (gst_iterator_next (pads, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);

        if (gst_pad_get_direction (pad) == GST_PAD_SRC)
          havesrc = TRUE;
        else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
          havesink = TRUE;

        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        havesrc = FALSE;
        havesink = FALSE;
        break;
      default:
        /* ERROR and DONE */
        done = TRUE;
        break;
    }
  }

  g_value_unset (&item);
  gst_iterator_free (pads);

  /* just look at the element's class, not the factory, since there might
   * not be a factory (in case of python elements) or the factory is the
   * wrong one (in case of a GstBin sub-class) and doesn't have complete
   * information. */
  {
    GList *tmp =
        gst_element_class_get_pad_template_list (GST_ELEMENT_GET_CLASS
        (element));

    while (tmp) {
      GstPadTemplate *template = (GstPadTemplate *) tmp->data;

      if (template->direction == GST_PAD_SRC)
        havesrc = TRUE;
      else if (template->direction == GST_PAD_SINK) {
        if (!havesink && (template->presence == GST_PAD_REQUEST) && isdynamic)
          *isdynamic = TRUE;
        havesink = TRUE;
      }
      tmp = tmp->next;
    }
  }
  return (havesink && havesrc);
}

/*
 * get_src_pad:
 * element: a #GstElement
 *
 * Returns: The src pad for the given element. A reference was added to the
 * returned pad, remove it when you don't need that pad anymore.
 * Returns NULL if there's no source pad.
 */

static GstPad *
get_src_pad (GstElement * element)
{
  GstIterator *it;
  GstIteratorResult itres;
  GValue item = { 0, };
  GstPad *srcpad = NULL;

  it = gst_element_iterate_src_pads (element);
  itres = gst_iterator_next (it, &item);
  if (itres != GST_ITERATOR_OK) {
    GST_DEBUG ("%s doesn't have a src pad !", GST_ELEMENT_NAME (element));
  } else {
    srcpad = g_value_get_object (&item);
    gst_object_ref (srcpad);
  }
  g_value_reset (&item);
  gst_iterator_free (it);

  return srcpad;
}

/* get_nb_static_sinks:
 * 
 * Returns : The number of static sink pads of the controlled element.
 */
static guint
get_nb_static_sinks (NleOperation * oper)
{
  GstIterator *sinkpads;
  gboolean done = FALSE;
  guint nbsinks = 0;
  GValue item = { 0, };

  sinkpads = gst_element_iterate_sink_pads (oper->element);

  while (!done) {
    switch (gst_iterator_next (sinkpads, &item)) {
      case GST_ITERATOR_OK:{
        nbsinks++;
        g_value_unset (&item);
      }
        break;
      case GST_ITERATOR_RESYNC:
        nbsinks = 0;
        gst_iterator_resync (sinkpads);
        break;
      default:
        /* ERROR and DONE */
        done = TRUE;
        break;
    }
  }

  g_value_reset (&item);
  gst_iterator_free (sinkpads);

  GST_DEBUG ("We found %d static sinks", nbsinks);

  return nbsinks;
}

static gboolean
nle_operation_add_element (GstBin * bin, GstElement * element)
{
  NleOperation *operation = (NleOperation *) bin;
  gboolean res = FALSE;
  gboolean isdynamic;

  GST_DEBUG_OBJECT (bin, "element:%s", GST_ELEMENT_NAME (element));

  if (operation->element) {
    GST_WARNING_OBJECT (operation,
        "We already control an element : %s , remove it first",
        GST_OBJECT_NAME (operation->element));
  } else {
    if (!element_is_valid_filter (element, &isdynamic)) {
      GST_WARNING_OBJECT (operation,
          "Element %s is not a valid filter element",
          GST_ELEMENT_NAME (element));
    } else {
      if ((res = GST_BIN_CLASS (parent_class)->add_element (bin, element))) {
        GstPad *srcpad;

        srcpad = get_src_pad (element);
        if (!srcpad)
          return FALSE;

        operation->element = element;
        operation->dynamicsinks = isdynamic;

        nle_object_ghost_pad_set_target (NLE_OBJECT (operation),
            NLE_OBJECT (operation)->srcpad, srcpad);

        /* Remove the reference get_src_pad gave us */
        gst_object_unref (srcpad);

        /* Figure out number of static sink pads */
        operation->num_sinks = get_nb_static_sinks (operation);

        /* Finally sync the ghostpads with the real pads */
        synchronize_sinks (operation);
      }
    }
  }

  return res;
}

static gboolean
nle_operation_remove_element (GstBin * bin, GstElement * element)
{
  NleOperation *operation = (NleOperation *) bin;
  gboolean res = FALSE;

  if (operation->element) {
    if ((res = GST_BIN_CLASS (parent_class)->remove_element (bin, element)))
      operation->element = NULL;
  } else {
    GST_WARNING_OBJECT (bin,
        "Element %s is not the one controlled by this operation",
        GST_ELEMENT_NAME (element));
  }
  return res;
}

static void
nle_operation_set_sinks (NleOperation * operation, guint sinks)
{
  /* FIXME : Check if sinkpad of element is on-demand .... */

  operation->num_sinks = sinks;
  synchronize_sinks (operation);
}

static void
nle_operation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  NleOperation *operation = (NleOperation *) object;

  switch (prop_id) {
    case ARG_SINKS:
      nle_operation_set_sinks (operation, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nle_operation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  NleOperation *operation = (NleOperation *) object;

  switch (prop_id) {
    case ARG_SINKS:
      g_value_set_int (value, operation->num_sinks);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*
 * Returns the first unused sink pad of the controlled element.
 * Only use with static element. Unref after usage.
 * Returns NULL if there's no more unused sink pads.
 */
static GstPad *
get_unused_static_sink_pad (NleOperation * operation)
{
  GstIterator *pads;
  gboolean done = FALSE;
  GValue item = { 0, };
  GstPad *ret = NULL;

  if (!operation->element)
    return NULL;

  pads = gst_element_iterate_pads (operation->element);

  while (!done) {
    switch (gst_iterator_next (pads, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);

        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GList *tmp;
          gboolean istaken = FALSE;

          /* 1. figure out if one of our sink ghostpads has this pad as target */
          for (tmp = operation->sinks; tmp; tmp = tmp->next) {
            GstGhostPad *gpad = (GstGhostPad *) tmp->data;
            GstPad *target = gst_ghost_pad_get_target (gpad);

            GST_LOG ("found ghostpad with target %s:%s",
                GST_DEBUG_PAD_NAME (target));

            if (target) {
              if (target == pad)
                istaken = TRUE;
              gst_object_unref (target);
            }
          }

          /* 2. if not taken, return that pad */
          if (!istaken) {
            gst_object_ref (pad);
            ret = pad;
            done = TRUE;
          }
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        break;
      default:
        /* ERROR and DONE */
        done = TRUE;
        break;
    }
  }

  g_value_unset (&item);
  gst_iterator_free (pads);

  if (ret)
    GST_DEBUG_OBJECT (operation, "found free sink pad %s:%s",
        GST_DEBUG_PAD_NAME (ret));
  else
    GST_DEBUG_OBJECT (operation, "Couldn't find an unused sink pad");

  return ret;
}

GstPad *
get_unlinked_sink_ghost_pad (NleOperation * operation)
{
  GstIterator *pads;
  gboolean done = FALSE;
  GValue item = { 0, };
  GstPad *ret = NULL;

  if (!operation->element)
    return NULL;

  pads = gst_element_iterate_sink_pads ((GstElement *) operation);

  while (!done) {
    switch (gst_iterator_next (pads, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);
        GstPad *peer = gst_pad_get_peer (pad);

        if (peer == NULL) {
          ret = pad;
          gst_object_ref (ret);
          done = TRUE;
        } else {
          gst_object_unref (peer);
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        break;
      default:
        /* ERROR and DONE */
        done = TRUE;
        break;
    }
  }

  g_value_unset (&item);
  gst_iterator_free (pads);

  if (ret)
    GST_DEBUG_OBJECT (operation, "found unlinked ghost sink pad %s:%s",
        GST_DEBUG_PAD_NAME (ret));
  else
    GST_DEBUG_OBJECT (operation, "Couldn't find an unlinked ghost sink pad");

  return ret;

}

static GstPad *
get_request_sink_pad (NleOperation * operation)
{
  GstPad *pad = NULL;
  GList *templates;

  if (!operation->element)
    return NULL;

  templates = gst_element_class_get_pad_template_list
      (GST_ELEMENT_GET_CLASS (operation->element));

  for (; templates; templates = templates->next) {
    GstPadTemplate *templ = (GstPadTemplate *) templates->data;

    GST_LOG_OBJECT (operation->element, "Trying template %s",
        GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));

    if ((GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SINK) &&
        (GST_PAD_TEMPLATE_PRESENCE (templ) == GST_PAD_REQUEST)) {
      pad =
          gst_element_get_request_pad (operation->element,
          GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));
      if (pad)
        break;
    }
  }

  return pad;
}

static GstPad *
add_sink_pad (NleOperation * operation)
{
  GstPad *gpad = NULL;
  GstPad *ret = NULL;

  if (!operation->element)
    return NULL;

  /* FIXME : implement */
  GST_LOG_OBJECT (operation, "element:%s , dynamicsinks:%d",
      GST_ELEMENT_NAME (operation->element), operation->dynamicsinks);


  if (!operation->dynamicsinks) {
    /* static sink pads */
    ret = get_unused_static_sink_pad (operation);
    if (ret) {
      gpad = nle_object_ghost_pad ((NleObject *) operation, GST_PAD_NAME (ret),
          ret);
      gst_object_unref (ret);
    }
  }

  if (!gpad) {
    /* request sink pads */
    ret = get_request_sink_pad (operation);
    if (ret) {
      gpad = nle_object_ghost_pad ((NleObject *) operation, GST_PAD_NAME (ret),
          ret);
      gst_object_unref (ret);
    }
  }

  if (gpad) {
    operation->sinks = g_list_append (operation->sinks, gpad);
    operation->realsinks++;
    GST_DEBUG ("Created new pad %s:%s ghosting %s:%s",
        GST_DEBUG_PAD_NAME (gpad), GST_DEBUG_PAD_NAME (ret));
  } else {
    GST_WARNING ("Couldn't find a usable sink pad!");
  }

  return gpad;
}

static gboolean
remove_sink_pad (NleOperation * operation, GstPad * sinkpad)
{
  gboolean ret = TRUE;

  GST_DEBUG ("sinkpad %s:%s", GST_DEBUG_PAD_NAME (sinkpad));

  /*
     We can't remove any random pad.
     We should remove an unused pad ... which is hard to figure out in a
     thread-safe way.
   */

  if ((sinkpad == NULL) && operation->dynamicsinks) {
    /* Find an unlinked sinkpad */
    if ((sinkpad = get_unlinked_sink_ghost_pad (operation)) == NULL) {
      ret = FALSE;
      goto beach;
    }
  }

  if (sinkpad) {
    GstPad *target = gst_ghost_pad_get_target ((GstGhostPad *) sinkpad);

    if (target) {
      /* release the target pad */
      nle_object_ghost_pad_set_target ((NleObject *) operation, sinkpad, NULL);
      if (operation->dynamicsinks)
        gst_element_release_request_pad (operation->element, target);
      gst_object_unref (target);
    }
    operation->sinks = g_list_remove (operation->sinks, sinkpad);
    nle_object_remove_ghost_pad ((NleObject *) operation, sinkpad);
    operation->realsinks--;
  }

beach:
  return ret;
}

static void
synchronize_sinks (NleOperation * operation)
{

  GST_DEBUG_OBJECT (operation, "num_sinks:%d , realsinks:%d, dynamicsinks:%d",
      operation->num_sinks, operation->realsinks, operation->dynamicsinks);

  if (operation->num_sinks == operation->realsinks)
    return;

  if (operation->num_sinks > operation->realsinks) {
    while (operation->num_sinks > operation->realsinks) /* Add pad */
      if (!(add_sink_pad (operation))) {
        break;
      }
  } else {
    /* Remove pad */
    /* FIXME, which one do we remove ? :) */
    while (operation->num_sinks < operation->realsinks)
      if (!remove_sink_pad (operation, NULL))
        break;
  }
}

static gboolean
nle_operation_prepare (NleObject * object)
{
  /* Prepare the pads */
  synchronize_sinks ((NleOperation *) object);

  return TRUE;
}

static gboolean
nle_operation_cleanup (NleObject * object)
{
  NleOperation *oper = (NleOperation *) object;

  if (oper->dynamicsinks) {
    GST_DEBUG ("Resetting dynamic sinks");
    nle_operation_set_sinks (oper, 0);
  }

  return TRUE;
}

void
nle_operation_hard_cleanup (NleOperation * operation)
{
  gboolean done = FALSE;

  GValue item = { 0, };
  GstIterator *pads;

  GST_INFO_OBJECT (operation, "Hard reset of the operation");

  pads = gst_element_iterate_sink_pads (GST_ELEMENT (operation));
  while (!done) {
    switch (gst_iterator_next (pads, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *sinkpad = g_value_get_object (&item);
        GstPad *srcpad = gst_pad_get_peer (sinkpad);

        if (srcpad) {
          GST_ERROR ("Unlinking %" GST_PTR_FORMAT " and  %"
              GST_PTR_FORMAT, srcpad, sinkpad);
          gst_pad_unlink (srcpad, sinkpad);
        }

        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        break;
      default:
        /* ERROR and DONE */
        done = TRUE;
        break;
    }
  }
  nle_object_cleanup (NLE_OBJECT (operation));
}


static GstPad *
nle_operation_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  NleOperation *operation = (NleOperation *) element;
  GstPad *ret;

  GST_DEBUG ("template:%s name:%s", templ->name_template, name);

  if (operation->num_sinks == operation->realsinks) {
    GST_WARNING_OBJECT (element,
        "We already have the maximum number of pads : %d",
        operation->num_sinks);
    return NULL;
  }

  ret = add_sink_pad ((NleOperation *) element);

  return ret;
}

static void
nle_operation_release_pad (GstElement * element, GstPad * pad)
{
  GST_DEBUG ("pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  remove_sink_pad ((NleOperation *) element, pad);
}

void
nle_operation_signal_input_priority_changed (NleOperation * operation,
    GstPad * pad, guint32 priority)
{
  GST_DEBUG_OBJECT (operation, "pad:%s:%s, priority:%d",
      GST_DEBUG_PAD_NAME (pad), priority);
  g_signal_emit (operation, nle_operation_signals[INPUT_PRIORITY_CHANGED],
      0, pad, priority);
}

void
nle_operation_update_base_time (NleOperation * operation,
    GstClockTime timestamp)
{
  if (!nle_object_to_media_time (NLE_OBJECT (operation),
          timestamp, &operation->next_base_time)) {
    GST_WARNING_OBJECT (operation, "Trying to set a basetime outside of "
        "ourself");

    return;
  }

  GST_INFO_OBJECT (operation, "Setting next_basetime to %"
      GST_TIME_FORMAT, GST_TIME_ARGS (operation->next_base_time));
}
