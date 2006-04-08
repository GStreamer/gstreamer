/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
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

/*
 * !!!!!!!!!!!!!!!!! Big phat warning. !!!!!!!!!!!!!!!!!!!!!!
 *
 * This is not a generic switch element. This is not to be used for
 * any such purpose. Patches to make it do that will be rejected.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gststreamselector.h"

GST_DEBUG_CATEGORY_STATIC (stream_selector_debug);
#define GST_CAT_DEFAULT stream_selector_debug

static GstElementDetails gst_stream_selector_details =
GST_ELEMENT_DETAILS ("StreamSelector",
    "Generic",
    "N-to-1 input stream_selectoring",
    "Julien Moutte <julien@moutte.net>\n"
    "Ronald S. Bultje <rbultje@ronald.bitfreak.net>\n"
    "Jan Schmidt <thaytan@mad.scientist.com>");

static GstStaticPadTemplate gst_stream_selector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_stream_selector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_ACTIVE_PAD = 1
};

static void gst_stream_selector_dispose (GObject * object);
static void gst_stream_selector_init (GstStreamSelector * sel);
static void gst_stream_selector_base_init (GstStreamSelectorClass * klass);
static void gst_stream_selector_class_init (GstStreamSelectorClass * klass);

static GstCaps *gst_stream_selector_getcaps (GstPad * pad);
static GList *gst_stream_selector_get_linked_pads (GstPad * pad);
static GstPad *gst_stream_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static GstFlowReturn gst_stream_selector_chain (GstPad * pad, GstBuffer * buf);
static void gst_stream_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_stream_selector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_stream_selector_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

static GstElementClass *parent_class = NULL;

GType
gst_stream_selector_get_type (void)
{
  static GType stream_selector_type = 0;

  if (!stream_selector_type) {
    static const GTypeInfo stream_selector_info = {
      sizeof (GstStreamSelectorClass),
      (GBaseInitFunc) gst_stream_selector_base_init,
      NULL,
      (GClassInitFunc) gst_stream_selector_class_init,
      NULL,
      NULL,
      sizeof (GstStreamSelector),
      0,
      (GInstanceInitFunc) gst_stream_selector_init,
    };

    stream_selector_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstStreamSelector", &stream_selector_info, 0);

    GST_DEBUG_CATEGORY_INIT (stream_selector_debug,
        "streamselector", 0, "A stream-selector element");
  }

  return stream_selector_type;
}

static void
gst_stream_selector_base_init (GstStreamSelectorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &gst_stream_selector_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_stream_selector_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_stream_selector_src_factory));
}

static void
gst_stream_selector_class_init (GstStreamSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_stream_selector_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_stream_selector_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ACTIVE_PAD,
      g_param_spec_string ("active-pad", "Active pad", "Name of the currently"
          " active sink pad", NULL, G_PARAM_READWRITE));

  gobject_class->dispose = gst_stream_selector_dispose;

  gstelement_class->request_new_pad = gst_stream_selector_request_new_pad;
}

static void
gst_stream_selector_init (GstStreamSelector * sel)
{
  sel->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_internal_link_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_get_linked_pads));
  gst_pad_set_getcaps_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_getcaps));
  gst_element_add_pad (GST_ELEMENT (sel), sel->srcpad);

  /* sinkpad management */
  sel->active_sinkpad = NULL;
  sel->nb_sinkpads = 0;

  //GST_OBJECT_FLAG_SET (sel, GST_ELEMENT_WORK_IN_PLACE);
}

static void
gst_stream_selector_dispose (GObject * object)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (object);

  gst_object_unref (sel->active_sinkpad);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_stream_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:{
      const gchar *pad_name = g_value_get_string (value);
      GstPad *pad = NULL;
      GstPad **active_pad_p;

      if (strcmp (pad_name, "") != 0) {
        pad = gst_element_get_pad (GST_ELEMENT (object), pad_name);
      }

      GST_OBJECT_LOCK (object);
      if (pad == sel->active_sinkpad) {
        GST_OBJECT_UNLOCK (object);
        if (pad)
          gst_object_unref (pad);
        break;
      }
#if 0
      if (sel->active_sinkpad && (GST_STATE (sel) >= GST_STATE_PAUSED)) {
        gst_pad_set_active (sel->active_sinkpad, FALSE);
        GST_DEBUG_OBJECT (sel, "Deactivating pad %" GST_PTR_FORMAT,
            sel->active_sinkpad);
      }
#endif

      active_pad_p = &sel->active_sinkpad;
      gst_object_replace ((GstObject **) active_pad_p, GST_OBJECT_CAST (pad));
      if (pad)
        gst_object_unref (pad);

#if 0
      if (sel->active_sinkpad && (GST_STATE (sel) >= GST_STATE_PAUSED)) {
        gst_pad_set_active (sel->active_sinkpad, TRUE);
        GST_DEBUG_OBJECT (sel, "Activating pad %" GST_PTR_FORMAT,
            sel->active_sinkpad);
      }
#endif
      GST_DEBUG_OBJECT (sel, "New active pad is %" GST_PTR_FORMAT,
          sel->active_sinkpad);
      GST_OBJECT_UNLOCK (object);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_stream_selector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:{
      GST_OBJECT_LOCK (object);
      if (sel->active_sinkpad != NULL) {
        g_value_set_string (value, gst_pad_get_name (sel->active_sinkpad));
      } else
        g_value_set_string (value, "");
      GST_OBJECT_UNLOCK (object);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_stream_selector_get_linked_pad (GstPad * pad, gboolean strict)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (gst_pad_get_parent (pad));
  GstPad *otherpad = NULL;

  if (pad == sel->srcpad)
    otherpad = sel->active_sinkpad;
  else if (pad == sel->active_sinkpad || !strict)
    otherpad = sel->srcpad;

  gst_object_unref (sel);

  return otherpad;
}

static GstCaps *
gst_stream_selector_getcaps (GstPad * pad)
{
  GstPad *otherpad = gst_stream_selector_get_linked_pad (pad, FALSE);
  GstObject *parent;

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

  return gst_pad_peer_get_caps (otherpad);
}

static GstFlowReturn
gst_stream_selector_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (gst_pad_get_parent (pad));
  GstFlowReturn result;
  GstPad *active_sinkpad;

  GST_OBJECT_LOCK (sel);
  active_sinkpad = sel->active_sinkpad;
  GST_OBJECT_UNLOCK (sel);

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad) {
    GST_DEBUG_OBJECT (sel,
        "Returning not-linked for buffer alloc from pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    result = GST_FLOW_NOT_LINKED;
  } else {
    result = gst_pad_alloc_buffer (sel->srcpad, offset, size, caps, buf);

    /* FIXME: HACK. If buffer alloc returns not-linked, perform a fallback
     * allocation.  This should NOT be necessary, because playbin should
     * properly block the source pad from running until it's finished hooking 
     * everything up, but playbin needs refactoring first. */
    if (result == GST_FLOW_NOT_LINKED) {
      GST_DEBUG_OBJECT (sel,
          "No peer pad yet - performing fallback allocation for pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));

      *buf = NULL;
      result = GST_FLOW_OK;
    }
  }

  gst_object_unref (sel);

  return result;
}

static GList *
gst_stream_selector_get_linked_pads (GstPad * pad)
{
  GstPad *otherpad = gst_stream_selector_get_linked_pad (pad, TRUE);

  if (!otherpad)
    return NULL;

  return g_list_append (NULL, otherpad);
}

static GstPad *
gst_stream_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (element);
  gchar *name = NULL;
  GstPad *sinkpad = NULL;

  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);

  GST_LOG_OBJECT (sel, "Creating new pad %d", sel->nb_sinkpads);

  name = g_strdup_printf ("sink%d", sel->nb_sinkpads++);
  sinkpad = gst_pad_new_from_template (templ, name);
  g_free (name);

  GST_OBJECT_LOCK (sel);
  if (sel->active_sinkpad == NULL)
    sel->active_sinkpad = gst_object_ref (sinkpad);
  GST_OBJECT_UNLOCK (sel);

  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_getcaps));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_chain));
  gst_pad_set_internal_link_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_get_linked_pads));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_selector_bufferalloc));

  gst_element_add_pad (GST_ELEMENT (sel), sinkpad);

  return sinkpad;
}

static GstFlowReturn
gst_stream_selector_chain (GstPad * pad, GstBuffer * buf)
{
  GstStreamSelector *sel = GST_STREAM_SELECTOR (gst_pad_get_parent (pad));
  GstFlowReturn res;
  GstPad *active_sinkpad;

  GST_OBJECT_LOCK (sel);
  active_sinkpad = sel->active_sinkpad;
  GST_OBJECT_UNLOCK (sel);

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad) {
    GST_DEBUG_OBJECT (sel, "Ignoring buffer %p from pad %s:%s",
        buf, GST_DEBUG_PAD_NAME (pad));

    gst_object_unref (sel);
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_LINKED;
  }

  /* forward */
  GST_DEBUG_OBJECT (sel, "Forwarding buffer %p from pad %s:%s",
      buf, GST_DEBUG_PAD_NAME (pad));
  res = gst_pad_push (sel->srcpad, buf);

  gst_object_unref (sel);

  return res;
}
