/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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

/* Object header */
#include "gstswitch.h"
#include <gst/gst.h>

#include <string.h>

/**
 * This element allows runtime switching between many sources. It outputs a
 * new segment every time it switches. The input sources are expected to be
 * rate controlled/live or synced to the clock using identity sync=true upstream
 * of this element. If they are not, your cpu usage will hike up.
 *
 * Example pipelines:
 * videotestsrc pattern=0 ! identity sync=true \
 *                                              switch ! ximagesink
 * videotestsrc pattern=1 ! identity sync=true /
 *
 * videotestsrc pattern=0 ! identity sync=true \
 *                                              switch ! 
 *                                               identity single-segment=true !
 *                                               theoraenc ! oggmux ! filesink
 * videotestsrc pattern=1 ! identity sync=true /
 */
enum
{
  ARG_0,
  ARG_NB_SOURCES,
  ARG_ACTIVE_SOURCE
};

GST_DEBUG_CATEGORY_STATIC (switch_debug);
#define GST_CAT_DEFAULT switch_debug
/* ElementFactory information */
static const GstElementDetails gst_switch_details =
GST_ELEMENT_DETAILS ("Switch",
    "Generic",
    "N-to-1 input switching",
    "Julien Moutte <julien@moutte.net>\n"
    "Zaheer Merali <zaheerabbas at merali dot org>");

static GstStaticPadTemplate gst_switch_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_switch_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElementClass *parent_class = NULL;
static GstCaps *gst_switch_getcaps (GstPad * pad);
static GList *gst_switch_get_linked_pads (GstPad * pad);
static GstFlowReturn gst_switch_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static GstFlowReturn gst_switch_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_switch_event (GstPad * pad, GstEvent * event);

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */


static void
gst_switch_release_pad (GstElement * element, GstPad * pad)
{
  GstSwitch *gstswitch = NULL;

  g_return_if_fail (GST_IS_SWITCH (element));

  gstswitch = GST_SWITCH (element);

  GST_LOG_OBJECT (gstswitch, "releasing requested pad %p", pad);

  gst_element_remove_pad (element, pad);
  GST_OBJECT_LOCK (gstswitch);
  gstswitch->nb_sinkpads--;
  if (gstswitch->active_sinkpad == pad) {
    gst_object_unref (gstswitch->active_sinkpad);
    gstswitch->active_sinkpad = NULL;
    if (gstswitch->nb_sinkpads == 0) {
      GstIterator *iter =
          gst_element_iterate_sink_pads (GST_ELEMENT (gstswitch));
      gpointer active_sinkpad_store = (gpointer) gstswitch->active_sinkpad;

      if (gst_iterator_next (iter, &active_sinkpad_store) == GST_ITERATOR_DONE) {
        GST_LOG_OBJECT (gstswitch, "active pad now %p",
            gstswitch->active_sinkpad);
      } else {
        GST_LOG_OBJECT (gstswitch, "could not get first sinkpad");
      }
      gst_iterator_free (iter);
    }
  }
  GST_OBJECT_UNLOCK (gstswitch);
}

static GstPad *
gst_switch_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused)
{
  gchar *name = NULL;
  GstPad *sinkpad = NULL;
  GstSwitch *gstswitch = NULL;

  g_return_val_if_fail (GST_IS_SWITCH (element), NULL);

  gstswitch = GST_SWITCH (element);

  /* We only provide requested sink pads */
  if (templ->direction != GST_PAD_SINK) {
    GST_LOG_OBJECT (gstswitch, "requested a non sink pad");
    return NULL;
  }

  GST_OBJECT_LOCK (gstswitch);
  name = g_strdup_printf ("sink%d", gstswitch->nb_sinkpads);

  sinkpad = gst_pad_new_from_template (templ, name);

  if (name)
    g_free (name);

  if (gstswitch->active_sinkpad == NULL)
    gstswitch->active_sinkpad = gst_object_ref (sinkpad);
  GST_OBJECT_UNLOCK (gstswitch);

  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_switch_getcaps));
  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR (gst_switch_chain));
  gst_pad_set_internal_link_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_switch_get_linked_pads));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_switch_bufferalloc));
  gst_pad_set_event_function (sinkpad, GST_DEBUG_FUNCPTR (gst_switch_event));
  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (gstswitch), sinkpad);

  gstswitch->nb_sinkpads++;

  return sinkpad;
}

static GstFlowReturn
gst_switch_chain (GstPad * pad, GstBuffer * buf)
{
  GstSwitch *gstswitch = GST_SWITCH (gst_pad_get_parent (pad));
  GstFlowReturn res;
  GstPad *active_sinkpad;

  GST_OBJECT_LOCK (gstswitch);
  active_sinkpad = gstswitch->active_sinkpad;
  GST_OBJECT_UNLOCK (gstswitch);

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad) {
    GST_DEBUG_OBJECT (gstswitch, "Ignoring buffer %p from pad %s:%s",
        buf, GST_DEBUG_PAD_NAME (pad));

    gst_object_unref (gstswitch);
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  /* check if we need to send a new segment event */
  GST_OBJECT_LOCK (gstswitch);
  if (gstswitch->need_to_send_newsegment) {
    /* retrieve event from hash table */
    GstEvent *event =
        (GstEvent *) g_hash_table_lookup (gstswitch->newsegment_events, pad);
    if (event) {
      /* create a copy of this event so we can change start to match
       * the start time of this buffer */
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);
      gst_pad_push_event (gstswitch->srcpad,
          gst_event_new_new_segment_full (update, rate, applied_rate, format,
              GST_BUFFER_TIMESTAMP (buf), stop, position));
      gstswitch->need_to_send_newsegment = FALSE;
      GST_DEBUG_OBJECT (gstswitch,
          "Sending new segment with start of %" G_GINT64_FORMAT,
          GST_BUFFER_TIMESTAMP (buf));
    }
  }
  GST_OBJECT_UNLOCK (gstswitch);
  /* forward */
  GST_DEBUG_OBJECT (gstswitch, "Forwarding buffer %p from pad %s:%s",
      buf, GST_DEBUG_PAD_NAME (pad));
  res = gst_pad_push (gstswitch->srcpad, buf);

  gst_object_unref (gstswitch);

  return res;
}

static gboolean
gst_switch_event (GstPad * pad, GstEvent * event)
{
  GstSwitch *gstswitch = GST_SWITCH (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      GST_OBJECT_LOCK (gstswitch);
      /* need to put in or replace what's in hash table */
      g_hash_table_replace (gstswitch->newsegment_events, pad, event);
      if (pad == gstswitch->active_sinkpad) {
        /* want to ref event because we have kept it */
        gst_event_ref (event);
        /* need to send it across if we are active pad */
        ret = gst_pad_push_event (gstswitch->srcpad, event);
      }
      GST_OBJECT_UNLOCK (gstswitch);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (gstswitch);
  return ret;
}

/* =========================================== */
/*                                             */
/*                 Properties                  */
/*                                             */
/* =========================================== */

static void
gst_switch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSwitch *gstswitch = NULL;
  const gchar *pad_name;
  GstPad *pad = NULL;
  GstPad **active_pad_p;

  g_return_if_fail (GST_IS_SWITCH (object));

  gstswitch = GST_SWITCH (object);

  switch (prop_id) {
    case ARG_ACTIVE_SOURCE:
      pad_name = g_value_get_string (value);
      if (strcmp (pad_name, "") != 0) {
        pad = gst_element_get_pad (GST_ELEMENT (object), pad_name);
      }

      GST_OBJECT_LOCK (object);
      if (pad == gstswitch->active_sinkpad) {
        GST_OBJECT_UNLOCK (object);
        if (pad)
          gst_object_unref (pad);
        break;
      }
      active_pad_p = &gstswitch->active_sinkpad;
      gst_object_replace ((GstObject **) active_pad_p, GST_OBJECT_CAST (pad));
      if (pad)
        gst_object_unref (pad);

      GST_DEBUG_OBJECT (gstswitch, "New active pad is %" GST_PTR_FORMAT,
          gstswitch->active_sinkpad);
      gstswitch->need_to_send_newsegment = TRUE;
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_switch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSwitch *gstswitch = NULL;

  g_return_if_fail (GST_IS_SWITCH (object));

  gstswitch = GST_SWITCH (object);

  switch (prop_id) {
    case ARG_ACTIVE_SOURCE:
      GST_OBJECT_LOCK (object);
      if (gstswitch->active_sinkpad != NULL) {
        g_value_take_string (value,
            gst_pad_get_name (gstswitch->active_sinkpad));
      } else {
        g_value_set_string (value, "");
      }
      GST_OBJECT_UNLOCK (object);
      break;
    case ARG_NB_SOURCES:
      g_value_set_uint (value, gstswitch->nb_sinkpads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_switch_get_linked_pad (GstPad * pad, gboolean strict)
{
  GstSwitch *gstswitch = GST_SWITCH (gst_pad_get_parent (pad));
  GstPad *otherpad = NULL;

  if (pad == gstswitch->srcpad)
    otherpad = gstswitch->active_sinkpad;
  else if (pad == gstswitch->active_sinkpad || !strict)
    otherpad = gstswitch->srcpad;

  gst_object_unref (gstswitch);

  return otherpad;
}

static GstCaps *
gst_switch_getcaps (GstPad * pad)
{
  GstPad *otherpad = gst_switch_get_linked_pad (pad, FALSE);
  GstObject *parent;
  GstCaps *caps;

  parent = gst_object_get_parent (GST_OBJECT (pad));
  if (!otherpad) {
    GST_DEBUG_OBJECT (parent,
        "Pad %s:%s not linked, returning ANY", GST_DEBUG_PAD_NAME (pad));

    gst_object_unref (parent);
    return gst_caps_new_any ();
  }

  GST_DEBUG_OBJECT (parent,
      "Pad %s:%s is linked (to %s:%s), returning allowed-caps",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (otherpad));

  gst_object_unref (parent);

  caps = gst_pad_peer_get_caps (otherpad);
  if (caps == NULL) {
    caps = gst_caps_new_any ();
  }
  return caps;
}

static GstFlowReturn
gst_switch_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstSwitch *gstswitch = GST_SWITCH (gst_pad_get_parent (pad));
  GstFlowReturn result;
  GstPad *active_sinkpad;

  GST_OBJECT_LOCK (gstswitch);
  active_sinkpad = gstswitch->active_sinkpad;
  GST_OBJECT_UNLOCK (gstswitch);

  /* Fallback allocation for buffers from pads except the selected one */
  if (pad != active_sinkpad) {
    GST_DEBUG_OBJECT (gstswitch,
        "Pad %s:%s is not selected. Performing fallback allocation",
        GST_DEBUG_PAD_NAME (pad));

    *buf = NULL;
    result = GST_FLOW_OK;
  } else {
    result = gst_pad_alloc_buffer (gstswitch->srcpad, offset, size, caps, buf);

    /* FIXME: HACK. If buffer alloc returns not-linked, perform a fallback
     * allocation.  This should NOT be necessary, because playbin should
     * properly block the source pad from running until it's finished hooking 
     * everything up, but playbin needs refactoring first. */
    if (result == GST_FLOW_NOT_LINKED) {
      GST_DEBUG_OBJECT (gstswitch,
          "No peer pad yet - performing fallback allocation for pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));

      *buf = NULL;
      result = GST_FLOW_OK;
    }
  }

  gst_object_unref (gstswitch);

  return result;
}

static GList *
gst_switch_get_linked_pads (GstPad * pad)
{
  GstPad *otherpad = gst_switch_get_linked_pad (pad, TRUE);

  if (!otherpad)
    return NULL;

  return g_list_append (NULL, otherpad);
}


/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_switch_dispose (GObject * object)
{
  GstSwitch *gstswitch = NULL;

  gstswitch = GST_SWITCH (object);

  if (gstswitch->active_sinkpad) {
    gst_object_unref (gstswitch->active_sinkpad);
    gstswitch->active_sinkpad = NULL;
  }
  if (gstswitch->newsegment_events) {
    g_hash_table_destroy (gstswitch->newsegment_events);
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_switch_init (GstSwitch * gstswitch)
{
  gstswitch->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_internal_link_function (gstswitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_switch_get_linked_pads));
  gst_pad_set_getcaps_function (gstswitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_switch_getcaps));
  gst_element_add_pad (GST_ELEMENT (gstswitch), gstswitch->srcpad);

  gstswitch->active_sinkpad = NULL;
  gstswitch->nb_sinkpads = 0;
  gstswitch->newsegment_events = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) gst_mini_object_unref);
  gstswitch->need_to_send_newsegment = FALSE;
}

static void
gst_switch_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_switch_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_switch_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_switch_src_factory));

}

static void
gst_switch_class_init (GstSwitchClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_switch_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_switch_get_property);

  g_object_class_install_property (gobject_class,
      ARG_NB_SOURCES,
      g_param_spec_uint ("num-sources",
          "number of sources",
          "number of sources", 0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_ACTIVE_SOURCE,
      g_param_spec_string ("active-pad",
          "Active Pad",
          "Name of the currently active sink pad", NULL, G_PARAM_READWRITE));

  gobject_class->dispose = gst_switch_dispose;

  gstelement_class->request_new_pad = gst_switch_request_new_pad;
  gstelement_class->release_pad = gst_switch_release_pad;
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

GType
gst_switch_get_type (void)
{
  static GType switch_type = 0;

  if (!switch_type) {
    static const GTypeInfo switch_info = {
      sizeof (GstSwitchClass),
      gst_switch_base_init,
      NULL,
      (GClassInitFunc) gst_switch_class_init,
      NULL,
      NULL,
      sizeof (GstSwitch),
      0,
      (GInstanceInitFunc) gst_switch_init,
    };

    switch_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSwitch", &switch_info, 0);

    GST_DEBUG_CATEGORY_INIT (switch_debug, "switch", 0, "the switch element");
  }

  return switch_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "switch", GST_RANK_NONE,
      GST_TYPE_SWITCH);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "switch",
    "N-to-1 input switching",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
