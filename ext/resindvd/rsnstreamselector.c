/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2007 Wim Taymans <wim.taymans@gmail.com>
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

#include <string.h>

#include "rsnstreamselector.h"

GST_DEBUG_CATEGORY_STATIC (stream_selector_debug);
#define GST_CAT_DEFAULT stream_selector_debug

static GstStaticPadTemplate rsn_stream_selector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate rsn_stream_selector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_N_PADS,
  PROP_ACTIVE_PAD,
  PROP_LAST
};

static gboolean rsn_stream_selector_is_active_sinkpad (RsnStreamSelector * sel,
    GstPad * pad);
static GstPad *rsn_stream_selector_get_active (RsnStreamSelector * sel,
    GstPad * pad);
static void rsn_stream_selector_set_active (RsnStreamSelector * sel,
    GstPad * pad);
static GstPad *rsn_stream_selector_get_linked_pad (GstPad * pad,
    gboolean strict);

#define RSN_TYPE_SELECTOR_PAD \
  (gst_selector_pad_get_type())
#define GST_SELECTOR_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), RSN_TYPE_SELECTOR_PAD, RsnSelectorPad))
#define GST_SELECTOR_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), RSN_TYPE_SELECTOR_PAD, RsnSelectorPadClass))
#define RSN_IS_SELECTOR_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RSN_TYPE_SELECTOR_PAD))
#define RSN_IS_SELECTOR_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), RSN_TYPE_SELECTOR_PAD))
#define GST_SELECTOR_PAD_CAST(obj) \
  ((RsnSelectorPad *)(obj))

typedef struct _RsnSelectorPad RsnSelectorPad;
typedef struct _RsnSelectorPadClass RsnSelectorPadClass;

struct _RsnSelectorPad
{
  GstPad parent;

  gboolean active;
  gboolean eos;
  GstSegment segment;
  GstTagList *tags;
};

struct _RsnSelectorPadClass
{
  GstPadClass parent;
};

static void gst_selector_pad_class_init (RsnSelectorPadClass * klass);
static void gst_selector_pad_init (RsnSelectorPad * pad);
static void gst_selector_pad_finalize (GObject * object);

static void gst_selector_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPadClass *selector_pad_parent_class = NULL;

static void gst_selector_pad_reset (RsnSelectorPad * pad);
static gboolean gst_selector_pad_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_selector_pad_getcaps (GstPad * pad);
static GstFlowReturn gst_selector_pad_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_selector_pad_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

enum
{
  PROP_PAD_0,
  PROP_PAD_TAGS,
  PROP_PAD_ACTIVE,
  PROP_PAD_LAST
};

static GType
gst_selector_pad_get_type (void)
{
  static GType selector_pad_type = 0;

  if (!selector_pad_type) {
    static const GTypeInfo selector_pad_info = {
      sizeof (RsnSelectorPadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_selector_pad_class_init,
      NULL,
      NULL,
      sizeof (RsnSelectorPad),
      0,
      (GInstanceInitFunc) gst_selector_pad_init,
    };

    selector_pad_type =
        g_type_register_static (GST_TYPE_PAD, "RsnSelectorPad",
        &selector_pad_info, 0);
  }
  return selector_pad_type;
}

static void
gst_selector_pad_class_init (RsnSelectorPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  selector_pad_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_selector_pad_finalize;
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_selector_pad_get_property);

  g_object_class_install_property (gobject_class, PROP_PAD_TAGS,
      g_param_spec_boxed ("tags", "Tags",
          "The currently active tags on the pad", GST_TYPE_TAG_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PAD_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "If the pad is currently active", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_selector_pad_init (RsnSelectorPad * pad)
{
  gst_selector_pad_reset (pad);
}

static void
gst_selector_pad_finalize (GObject * object)
{
  RsnSelectorPad *pad;

  pad = GST_SELECTOR_PAD_CAST (object);

  if (pad->tags)
    gst_tag_list_free (pad->tags);

  G_OBJECT_CLASS (selector_pad_parent_class)->finalize (object);
}

static void
gst_selector_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  RsnSelectorPad *pad;

  pad = GST_SELECTOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_TAGS:
      GST_OBJECT_LOCK (object);
      g_value_set_boxed (value, pad->tags);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_PAD_ACTIVE:
    {
      RsnStreamSelector *sel;

      sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));
      g_value_set_boolean (value, rsn_stream_selector_is_active_sinkpad (sel,
              GST_PAD_CAST (pad)));
      gst_object_unref (sel);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_selector_pad_reset (RsnSelectorPad * pad)
{
  pad->active = FALSE;
  pad->eos = FALSE;
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
}

static gboolean
gst_selector_pad_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  gboolean forward = TRUE;
  RsnStreamSelector *sel;
  RsnSelectorPad *selpad;
  GstPad *active_sinkpad;

  sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);

  /* only forward if we are dealing with the active sinkpad */
  active_sinkpad = rsn_stream_selector_get_active (sel, pad);
  forward = (active_sinkpad == pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_selector_pad_reset (selpad);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (selpad,
          "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format %d, "
          "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
          G_GINT64_FORMAT, update, rate, arate, format, start, stop, time);

      gst_segment_set_newsegment_full (&selpad->segment, update,
          rate, arate, format, start, stop, time);
      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *tags;

      GST_OBJECT_LOCK (selpad);
      if (selpad->tags)
        gst_tag_list_free (selpad->tags);
      gst_event_parse_tag (event, &tags);
      if (tags)
        tags = gst_tag_list_copy (tags);
      selpad->tags = tags;
      GST_DEBUG_OBJECT (sel, "received tags %" GST_PTR_FORMAT, selpad->tags);
      GST_OBJECT_UNLOCK (selpad);
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      if (structure != NULL &&
          gst_structure_has_name (structure, "application/x-gst-dvd")) {
        const char *type = gst_structure_get_string (structure, "event");
        if (strcmp (type, "select-pad") == 0) {
          rsn_stream_selector_set_active (sel, pad);
          forward = FALSE;
        }
      }
    }
    case GST_EVENT_EOS:
      selpad->eos = TRUE;
      break;
    default:
      break;
  }
  if (forward)
    res = gst_pad_push_event (sel->srcpad, event);
  else
    gst_event_unref (event);

  gst_object_unref (sel);

  return res;
}

static GstCaps *
gst_selector_pad_getcaps (GstPad * pad)
{
  RsnStreamSelector *sel;
  GstCaps *caps;

  sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sel, "Getting caps of srcpad peer");
  caps = gst_pad_peer_get_caps (sel->srcpad);
  if (caps == NULL)
    caps = gst_caps_new_any ();

  gst_object_unref (sel);

  return caps;
}

static GstFlowReturn
gst_selector_pad_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  RsnStreamSelector *sel;
  GstFlowReturn result;
  GstPad *active_sinkpad;

  sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));

  active_sinkpad = rsn_stream_selector_get_active (sel, pad);

  /* Fallback allocation for buffers from pads except the selected one */
  if (pad != active_sinkpad) {
    GST_DEBUG_OBJECT (sel,
        "Pad %s:%s is not selected. Performing fallback allocation",
        GST_DEBUG_PAD_NAME (pad));

    *buf = NULL;
    result = GST_FLOW_OK;
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

static GstFlowReturn
gst_selector_pad_chain (GstPad * pad, GstBuffer * buf)
{
  RsnStreamSelector *sel;
  GstFlowReturn res;
  GstPad *active_sinkpad;
  RsnSelectorPad *selpad;
  GstClockTime timestamp;
  GstSegment *seg;
  gboolean discont;

  sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);
  seg = &selpad->segment;

  active_sinkpad = rsn_stream_selector_get_active (sel, pad);

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_DEBUG_OBJECT (sel, "received timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    gst_segment_set_last_stop (seg, seg->format, timestamp);
  }

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad)
    goto ignore;

  /* If we just switched pads, mark a discont buffer */
  GST_OBJECT_LOCK (sel);
  discont = sel->mark_discont;
  sel->mark_discont = FALSE;
  GST_OBJECT_UNLOCK (sel);

  if (discont) {
    GST_DEBUG_OBJECT (sel, "Marking buffer discont due to pad switch");
    buf = gst_buffer_make_metadata_writable (buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  }

  /* forward */
  GST_LOG_OBJECT (sel, "Forwarding buffer %p from pad %s:%s", buf,
      GST_DEBUG_PAD_NAME (pad));
  res = gst_pad_push (sel->srcpad, buf);
done:
  gst_object_unref (sel);
  return res;
  /* dropped buffers */
ignore:
  {
    GST_DEBUG_OBJECT (sel, "Ignoring buffer %p from pad %s:%s",
        buf, GST_DEBUG_PAD_NAME (pad));
    gst_buffer_unref (buf);
    res = GST_FLOW_NOT_LINKED;
    goto done;
  }
}

static void rsn_stream_selector_dispose (GObject * object);

static void rsn_stream_selector_init (RsnStreamSelector * sel);
static void rsn_stream_selector_base_init (RsnStreamSelectorClass * klass);
static void rsn_stream_selector_class_init (RsnStreamSelectorClass * klass);

static void rsn_stream_selector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void rsn_stream_selector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *rsn_stream_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void rsn_stream_selector_release_pad (GstElement * element,
    GstPad * pad);
static GstIterator *rsn_stream_selector_iterate_linked_pads (GstPad * pad);
static GstCaps *rsn_stream_selector_getcaps (GstPad * pad);

static GstElementClass *parent_class = NULL;

GType
rsn_stream_selector_get_type (void)
{
  static GType stream_selector_type = 0;

  if (!stream_selector_type) {
    static const GTypeInfo stream_selector_info = {
      sizeof (RsnStreamSelectorClass),
      (GBaseInitFunc) rsn_stream_selector_base_init,
      NULL,
      (GClassInitFunc) rsn_stream_selector_class_init,
      NULL,
      NULL,
      sizeof (RsnStreamSelector),
      0,
      (GInstanceInitFunc) rsn_stream_selector_init,
    };
    stream_selector_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "RsnStreamSelector", &stream_selector_info, 0);
    GST_DEBUG_CATEGORY_INIT (stream_selector_debug,
        "streamselector", 0, "A stream-selector element");
  }

  return stream_selector_type;
}

static void
rsn_stream_selector_base_init (RsnStreamSelectorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class, "StreamSelector",
      "Generic", "N-to-1 input stream_selectoring",
      "Julien Moutte <julien@moutte.net>, "
      "Ronald S. Bultje <rbultje@ronald.bitfreak.net>, "
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Wim Taymans <wim.taymans@gmail.com>");
  gst_element_class_add_static_pad_template (element_class,
      &rsn_stream_selector_sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &rsn_stream_selector_src_factory);
}

static void
rsn_stream_selector_class_init (RsnStreamSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = rsn_stream_selector_dispose;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (rsn_stream_selector_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (rsn_stream_selector_get_property);

  g_object_class_install_property (gobject_class, PROP_N_PADS,
      g_param_spec_uint ("n-pads", "Number of Pads",
          "The number of sink pads", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active Pad",
          "The currently active sink pad", GST_TYPE_PAD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad = rsn_stream_selector_request_new_pad;
  gstelement_class->release_pad = rsn_stream_selector_release_pad;
}

static void
rsn_stream_selector_init (RsnStreamSelector * sel)
{
  sel->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_iterate_internal_links_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (rsn_stream_selector_iterate_linked_pads));
  gst_pad_set_getcaps_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (rsn_stream_selector_getcaps));
  gst_element_add_pad (GST_ELEMENT (sel), sel->srcpad);
  /* sinkpad management */
  sel->padcount = 0;
  sel->active_sinkpad = NULL;
  gst_segment_init (&sel->segment, GST_FORMAT_UNDEFINED);
}

static void
rsn_stream_selector_dispose (GObject * object)
{
  RsnStreamSelector *sel = RSN_STREAM_SELECTOR (object);

  if (sel->active_sinkpad) {
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
rsn_stream_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  RsnStreamSelector *sel = RSN_STREAM_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
    {
      GstPad *pad = NULL;

      pad = g_value_get_object (value);
      rsn_stream_selector_set_active (sel, pad);

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rsn_stream_selector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  RsnStreamSelector *sel = RSN_STREAM_SELECTOR (object);

  switch (prop_id) {
    case PROP_N_PADS:
      GST_OBJECT_LOCK (object);
      g_value_set_uint (value, sel->n_pads);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_ACTIVE_PAD:{
      GST_OBJECT_LOCK (object);
      g_value_set_object (value, sel->active_sinkpad);
      GST_OBJECT_UNLOCK (object);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
rsn_stream_selector_get_linked_pad (GstPad * pad, gboolean strict)
{
  RsnStreamSelector *sel;
  GstPad *otherpad = NULL;

  sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));

  GST_OBJECT_LOCK (sel);
  if (pad == sel->srcpad)
    otherpad = sel->active_sinkpad;
  else if (pad == sel->active_sinkpad || !strict)
    otherpad = sel->srcpad;
  if (otherpad)
    gst_object_ref (otherpad);
  GST_OBJECT_UNLOCK (sel);
  gst_object_unref (sel);
  return otherpad;
}

static GstCaps *
rsn_stream_selector_getcaps (GstPad * pad)
{
  GstPad *otherpad;
  GstObject *parent;
  GstCaps *caps;

  otherpad = rsn_stream_selector_get_linked_pad (pad, FALSE);
  parent = gst_object_get_parent (GST_OBJECT (pad));
  if (!otherpad) {
    GST_DEBUG_OBJECT (parent,
        "Pad %s:%s not linked, returning ANY", GST_DEBUG_PAD_NAME (pad));
    caps = gst_caps_new_any ();
  } else {
    GST_DEBUG_OBJECT (parent,
        "Pad %s:%s is linked (to %s:%s), returning peer caps",
        GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (otherpad));
    /* if the peer has caps, use those. If the pad is not linked, this function
     * returns NULL and we return ANY */
    if (!(caps = gst_pad_peer_get_caps (otherpad)))
      caps = gst_caps_new_any ();
    gst_object_unref (otherpad);
  }

  gst_object_unref (parent);
  return caps;
}

/* check if the pad is the active sinkpad */
static gboolean
rsn_stream_selector_is_active_sinkpad (RsnStreamSelector * sel, GstPad * pad)
{
  gboolean res;

  GST_OBJECT_LOCK (sel);
  res = (pad == sel->active_sinkpad);
  GST_OBJECT_UNLOCK (sel);

  return res;
}

/* Get or create the active sinkpad */
static GstPad *
rsn_stream_selector_get_active (RsnStreamSelector * sel, GstPad * pad)
{
  GstPad *active_sinkpad;
  RsnSelectorPad *selpad;

  selpad = GST_SELECTOR_PAD_CAST (pad);

  GST_OBJECT_LOCK (sel);
  selpad->active = TRUE;
  active_sinkpad = sel->active_sinkpad;
  if (active_sinkpad == NULL) {
    /* first pad we get an alloc on becomes the activated pad by default */
    active_sinkpad = sel->active_sinkpad = gst_object_ref (pad);
    GST_DEBUG_OBJECT (sel, "Activating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }
  GST_OBJECT_UNLOCK (sel);

  return active_sinkpad;
}

static void
rsn_stream_selector_set_active (RsnStreamSelector * sel, GstPad * pad)
{
  GstPad **active_pad_p;

  GST_OBJECT_LOCK (GST_OBJECT_CAST (sel));
  if (pad != sel->active_sinkpad) {
    RsnSelectorPad *selpad;

    selpad = GST_SELECTOR_PAD_CAST (pad);
    /* we can only activate pads that have data received */
    if (selpad && !selpad->active) {
      GST_DEBUG_OBJECT (sel, "No data received on pad %" GST_PTR_FORMAT, pad);
    } else {
      active_pad_p = &sel->active_sinkpad;
      gst_object_replace ((GstObject **) active_pad_p, GST_OBJECT_CAST (pad));
      GST_DEBUG_OBJECT (sel, "New active pad is %" GST_PTR_FORMAT,
          sel->active_sinkpad);
    }
    /* Mark the next buffer as discontinuous */
    sel->mark_discont = TRUE;
  }
  GST_OBJECT_UNLOCK (GST_OBJECT_CAST (sel));
}

static GstIterator *
rsn_stream_selector_iterate_linked_pads (GstPad * pad)
{
  RsnStreamSelector *sel = RSN_STREAM_SELECTOR (gst_pad_get_parent (pad));
  GstPad *otherpad = rsn_stream_selector_get_linked_pad (pad, TRUE);
  GstIterator *it = gst_iterator_new_single (GST_TYPE_PAD, otherpad,
      (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);

  if (otherpad)
    gst_object_unref (otherpad);
  gst_object_unref (sel);

  return it;
}

static GstPad *
rsn_stream_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused)
{
  RsnStreamSelector *sel;
  gchar *name = NULL;
  GstPad *sinkpad = NULL;

  sel = RSN_STREAM_SELECTOR (element);
  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);
  GST_LOG_OBJECT (sel, "Creating new pad %d", sel->padcount);
  GST_OBJECT_LOCK (sel);
  name = g_strdup_printf ("sink%d", sel->padcount++);
  sinkpad = g_object_new (RSN_TYPE_SELECTOR_PAD,
      "name", name, "direction", templ->direction, "template", templ, NULL);
  g_free (name);
  sel->n_pads++;
  GST_OBJECT_UNLOCK (sel);

  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_event));
  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_getcaps));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_chain));
  gst_pad_set_iterate_internal_links_function (sinkpad,
      GST_DEBUG_FUNCPTR (rsn_stream_selector_iterate_linked_pads));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_bufferalloc));

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (sel), sinkpad);
  return sinkpad;
}

static void
rsn_stream_selector_release_pad (GstElement * element, GstPad * pad)
{
  RsnStreamSelector *sel;

  sel = RSN_STREAM_SELECTOR (element);
  GST_LOG_OBJECT (sel, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_OBJECT_LOCK (sel);
  /* if the pad was the active pad, makes us select a new one */
  if (sel->active_sinkpad == pad) {
    GST_DEBUG_OBJECT (sel, "Deactivating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    sel->active_sinkpad = NULL;
  }
  sel->n_pads--;
  GST_OBJECT_UNLOCK (sel);

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT (sel), pad);
}
