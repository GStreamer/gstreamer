/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2007 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2007 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
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
 * SECTION:element-input-selector
 * @see_also: #GstOutputSelector
 *
 * Direct one out of N input streams to the output pad.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstinputselector.h"
#include "gstplay-marshal.h"

GST_DEBUG_CATEGORY_STATIC (input_selector_debug);
#define GST_CAT_DEFAULT input_selector_debug

static GstStaticPadTemplate gst_input_selector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_input_selector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_N_PADS,
  PROP_ACTIVE_PAD,
  PROP_SELECT_ALL,
  PROP_LAST
};

#define DEFAULT_PAD_ALWAYS_OK	TRUE

enum
{
  PROP_PAD_0,
  PROP_PAD_RUNNING_TIME,
  PROP_PAD_TAGS,
  PROP_PAD_ACTIVE,
  PROP_PAD_ALWAYS_OK,
  PROP_PAD_LAST
};

enum
{
  /* methods */
  SIGNAL_BLOCK,
  SIGNAL_SWITCH,
  LAST_SIGNAL
};
static guint gst_input_selector_signals[LAST_SIGNAL] = { 0 };

static inline gboolean gst_input_selector_is_active_sinkpad (GstInputSelector *
    sel, GstPad * pad);
static GstPad *gst_input_selector_activate_sinkpad (GstInputSelector * sel,
    GstPad * pad);
static GstPad *gst_input_selector_get_linked_pad (GstPad * pad,
    gboolean strict);
static gboolean gst_input_selector_check_eos (GstElement * selector);

#define GST_TYPE_SELECTOR_PAD \
  (gst_selector_pad_get_type())
#define GST_SELECTOR_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SELECTOR_PAD, GstSelectorPad))
#define GST_SELECTOR_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SELECTOR_PAD, GstSelectorPadClass))
#define GST_IS_SELECTOR_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SELECTOR_PAD))
#define GST_IS_SELECTOR_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SELECTOR_PAD))
#define GST_SELECTOR_PAD_CAST(obj) \
  ((GstSelectorPad *)(obj))

typedef struct _GstSelectorPad GstSelectorPad;
typedef struct _GstSelectorPadClass GstSelectorPadClass;

struct _GstSelectorPad
{
  GstPad parent;

  gboolean active;              /* when buffer have passed the pad */
  gboolean eos;                 /* when EOS has been received */
  gboolean discont;             /* after switching we create a discont */
  gboolean always_ok;
  GstSegment segment;           /* the current segment on the pad */
  GstTagList *tags;             /* last tags received on the pad */

  gboolean segment_pending;
};

struct _GstSelectorPadClass
{
  GstPadClass parent;
};

static void gst_selector_pad_class_init (GstSelectorPadClass * klass);
static void gst_selector_pad_init (GstSelectorPad * pad);
static void gst_selector_pad_finalize (GObject * object);
static void gst_selector_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_selector_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstPadClass *selector_pad_parent_class = NULL;

static gint64 gst_selector_pad_get_running_time (GstSelectorPad * pad);
static void gst_selector_pad_reset (GstSelectorPad * pad);
static gboolean gst_selector_pad_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_selector_pad_getcaps (GstPad * pad);
static gboolean gst_selector_pad_acceptcaps (GstPad * pad, GstCaps * caps);
static GstIterator *gst_selector_pad_iterate_linked_pads (GstPad * pad);
static GstFlowReturn gst_selector_pad_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_selector_pad_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

GType
gst_selector_pad_get_type (void)
{
  static GType selector_pad_type = 0;

  if (!selector_pad_type) {
    static const GTypeInfo selector_pad_info = {
      sizeof (GstSelectorPadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_selector_pad_class_init,
      NULL,
      NULL,
      sizeof (GstSelectorPad),
      0,
      (GInstanceInitFunc) gst_selector_pad_init,
    };

    selector_pad_type =
        g_type_register_static (GST_TYPE_PAD, "GstPlaybin2SelectorPad",
        &selector_pad_info, 0);
  }
  return selector_pad_type;
}

static void
gst_selector_pad_class_init (GstSelectorPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  selector_pad_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_selector_pad_finalize;

  gobject_class->get_property = gst_selector_pad_get_property;
  gobject_class->set_property = gst_selector_pad_set_property;

  g_object_class_install_property (gobject_class, PROP_PAD_RUNNING_TIME,
      g_param_spec_int64 ("running-time", "Running time",
          "Running time of stream on pad", 0, G_MAXINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_PAD_TAGS,
      g_param_spec_boxed ("tags", "Tags",
          "The currently active tags on the pad", GST_TYPE_TAG_LIST,
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_PAD_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "If the pad is currently active", FALSE, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, PROP_PAD_ALWAYS_OK,
      g_param_spec_boolean ("always-ok", "Always OK",
          "Make an inactive pad return OK instead of NOT_LINKED",
          DEFAULT_PAD_ALWAYS_OK, G_PARAM_READWRITE));
}

static void
gst_selector_pad_init (GstSelectorPad * pad)
{
  pad->always_ok = DEFAULT_PAD_ALWAYS_OK;
  gst_selector_pad_reset (pad);
}

static void
gst_selector_pad_finalize (GObject * object)
{
  GstSelectorPad *pad;

  pad = GST_SELECTOR_PAD_CAST (object);

  if (pad->tags)
    gst_tag_list_free (pad->tags);

  G_OBJECT_CLASS (selector_pad_parent_class)->finalize (object);
}

static void
gst_selector_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSelectorPad *spad = GST_SELECTOR_PAD_CAST (object);

  switch (prop_id) {
    case PROP_PAD_ALWAYS_OK:
      GST_OBJECT_LOCK (object);
      spad->always_ok = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_selector_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSelectorPad *spad = GST_SELECTOR_PAD_CAST (object);

  switch (prop_id) {
    case PROP_PAD_RUNNING_TIME:
      g_value_set_int64 (value, gst_selector_pad_get_running_time (spad));
      break;
    case PROP_PAD_TAGS:
      GST_OBJECT_LOCK (object);
      g_value_set_boxed (value, spad->tags);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_PAD_ACTIVE:
    {
      GstInputSelector *sel;

      sel = GST_INPUT_SELECTOR (gst_pad_get_parent (spad));
      g_value_set_boolean (value, gst_input_selector_is_active_sinkpad (sel,
              GST_PAD_CAST (spad)));
      gst_object_unref (sel);
      break;
    }
    case PROP_PAD_ALWAYS_OK:
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, spad->always_ok);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint64
gst_selector_pad_get_running_time (GstSelectorPad * pad)
{
  gint64 ret = 0;

  GST_OBJECT_LOCK (pad);
  if (pad->active) {
    gint64 last_stop = pad->segment.last_stop;

    if (last_stop >= 0)
      ret = gst_segment_to_running_time (&pad->segment, GST_FORMAT_TIME,
          last_stop);
  }
  GST_OBJECT_UNLOCK (pad);

  GST_DEBUG_OBJECT (pad, "running time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ret));

  return ret;
}

static void
gst_selector_pad_reset (GstSelectorPad * pad)
{
  GST_OBJECT_LOCK (pad);
  pad->active = FALSE;
  pad->eos = FALSE;
  pad->segment_pending = FALSE;
  pad->discont = FALSE;
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  GST_OBJECT_UNLOCK (pad);
}

/* strictly get the linked pad from the sinkpad. If the pad is active we return
 * the srcpad else we return NULL */
static GstIterator *
gst_selector_pad_iterate_linked_pads (GstPad * pad)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstIterator *it;

  otherpad = gst_input_selector_get_linked_pad (pad, TRUE);
  it = gst_iterator_new_single (GST_TYPE_PAD, otherpad,
      (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);

  if (otherpad)
    gst_object_unref (otherpad);
  gst_object_unref (sel);

  return it;
}

static gboolean
gst_selector_pad_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  gboolean forward = TRUE;
  GstInputSelector *sel;
  GstSelectorPad *selpad;
  GstPad *prev_active_sinkpad;
  GstPad *active_sinkpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);

  GST_INPUT_SELECTOR_LOCK (sel);
  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* only forward if we are dealing with the active sinkpad or if select_all
   * is enabled */
  if (pad != active_sinkpad && !sel->select_all)
    forward = FALSE;
  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad)
    g_object_notify (G_OBJECT (sel), "active-pad");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* FIXME, flush out the waiter */
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_INPUT_SELECTOR_LOCK (sel);
      gst_selector_pad_reset (selpad);
      sel->pending_close = FALSE;
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (pad,
          "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format %d, "
          "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
          G_GINT64_FORMAT, update, rate, arate, format, start, stop, time);

      GST_INPUT_SELECTOR_LOCK (sel);
      GST_OBJECT_LOCK (selpad);
      gst_segment_set_newsegment_full (&selpad->segment, update,
          rate, arate, format, start, stop, time);
      GST_OBJECT_UNLOCK (selpad);

      /* If we aren't forwarding the event (because the pad is not the
       * active_sinkpad, and select_all is not set, then set the flag on the
       * that says a segment needs sending if/when that pad is activated.
       * For all other cases, we send the event immediately, which makes
       * sparse streams and other segment updates work correctly downstream.
       */
      if (!forward)
        selpad->segment_pending = TRUE;

      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *tags, *oldtags, *newtags;

      gst_event_parse_tag (event, &tags);

      GST_OBJECT_LOCK (selpad);
      oldtags = selpad->tags;

      newtags = gst_tag_list_merge (oldtags, tags, GST_TAG_MERGE_REPLACE);
      selpad->tags = newtags;
      if (oldtags)
        gst_tag_list_free (oldtags);
      GST_DEBUG_OBJECT (pad, "received tags %" GST_PTR_FORMAT, newtags);
      GST_OBJECT_UNLOCK (selpad);

      g_object_notify (G_OBJECT (selpad), "tags");
      break;
    }
    case GST_EVENT_EOS:
      selpad->eos = TRUE;
      GST_DEBUG_OBJECT (pad, "received EOS");
      /* don't forward eos in select_all mode until all sink pads have eos */
      if (sel->select_all && !gst_input_selector_check_eos (GST_ELEMENT (sel))) {
        forward = FALSE;
      }
      break;
    default:
      break;
  }
  if (forward) {
    GST_DEBUG_OBJECT (pad, "forwarding event");
    res = gst_pad_push_event (sel->srcpad, event);
  } else
    gst_event_unref (event);

  gst_object_unref (sel);

  return res;
}

static GstCaps *
gst_selector_pad_getcaps (GstPad * pad)
{
  GstInputSelector *sel;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sel, "Getting caps of srcpad peer");
  caps = gst_pad_peer_get_caps_reffed (sel->srcpad);
  if (caps == NULL)
    caps = gst_caps_new_any ();

  gst_object_unref (sel);

  return caps;
}

static gboolean
gst_selector_pad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstInputSelector *sel;
  gboolean res;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (sel, "Checking acceptcaps of srcpad peer");
  res = gst_pad_peer_accept_caps (sel->srcpad, caps);
  gst_object_unref (sel);

  return res;
}

static GstFlowReturn
gst_selector_pad_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstInputSelector *sel;
  GstFlowReturn result;
  GstPad *active_sinkpad;
  GstPad *prev_active_sinkpad;
  GstSelectorPad *selpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);

  GST_DEBUG_OBJECT (pad, "received alloc");

  GST_INPUT_SELECTOR_LOCK (sel);
  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  if (pad != active_sinkpad)
    goto not_active;

  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad)
    g_object_notify (G_OBJECT (sel), "active-pad");

  result = gst_pad_alloc_buffer (sel->srcpad, offset, size, caps, buf);

done:
  gst_object_unref (sel);

  return result;

  /* ERRORS */
not_active:
  {
    GST_INPUT_SELECTOR_UNLOCK (sel);

    /* unselected pad, perform fallback alloc or return unlinked when
     * asked */
    GST_OBJECT_LOCK (selpad);
    if (selpad->always_ok) {
      GST_DEBUG_OBJECT (pad, "Not selected, performing fallback allocation");
      *buf = NULL;
      result = GST_FLOW_OK;
    } else {
      GST_DEBUG_OBJECT (pad, "Not selected, return NOT_LINKED");
      result = GST_FLOW_NOT_LINKED;
    }
    GST_OBJECT_UNLOCK (selpad);

    goto done;
  }
}

/* must be called with the SELECTOR_LOCK, will block while the pad is blocked 
 * or return TRUE when flushing */
static gboolean
gst_input_selector_wait (GstInputSelector * self, GstPad * pad)
{
  while (self->blocked && !self->flushing) {
    /* we can be unlocked here when we are shutting down (flushing) or when we
     * get unblocked */
    GST_INPUT_SELECTOR_WAIT (self);
  }
  return self->flushing;
}

static GstFlowReturn
gst_selector_pad_chain (GstPad * pad, GstBuffer * buf)
{
  GstInputSelector *sel;
  GstFlowReturn res;
  GstPad *active_sinkpad;
  GstPad *prev_active_sinkpad;
  GstSelectorPad *selpad;
  GstClockTime start_time;
  GstSegment *seg;
  GstEvent *close_event = NULL, *start_event = NULL;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);
  seg = &selpad->segment;

  GST_INPUT_SELECTOR_LOCK (sel);
  /* wait or check for flushing */
  if (gst_input_selector_wait (sel, pad))
    goto flushing;

  GST_DEBUG_OBJECT (pad, "getting active pad");

  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* update the segment on the srcpad */
  start_time = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (start_time)) {
    GST_DEBUG_OBJECT (pad, "received start time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time));
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      GST_DEBUG_OBJECT (pad, "received end time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start_time + GST_BUFFER_DURATION (buf)));

    GST_OBJECT_LOCK (pad);
    gst_segment_set_last_stop (seg, seg->format, start_time);
    GST_OBJECT_UNLOCK (pad);
  }

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad)
    goto ignore;

  if (G_UNLIKELY (sel->pending_close)) {
    GstSegment *cseg = &sel->segment;

    GST_DEBUG_OBJECT (sel,
        "pushing close NEWSEGMENT update %d, rate %lf, applied rate %lf, "
        "format %d, "
        "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
        G_GINT64_FORMAT, TRUE, cseg->rate, cseg->applied_rate, cseg->format,
        cseg->start, cseg->stop, cseg->time);

    /* create update segment */
    close_event = gst_event_new_new_segment_full (TRUE, cseg->rate,
        cseg->applied_rate, cseg->format, cseg->start, cseg->stop, cseg->time);

    sel->pending_close = FALSE;
  }
  /* if we have a pending segment, push it out now */
  if (G_UNLIKELY (selpad->segment_pending)) {
    GST_DEBUG_OBJECT (pad,
        "pushing pending NEWSEGMENT update %d, rate %lf, applied rate %lf, "
        "format %d, "
        "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
        G_GINT64_FORMAT, FALSE, seg->rate, seg->applied_rate, seg->format,
        seg->start, seg->stop, seg->time);

    start_event = gst_event_new_new_segment_full (FALSE, seg->rate,
        seg->applied_rate, seg->format, seg->start, seg->stop, seg->time);

    selpad->segment_pending = FALSE;
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad)
    g_object_notify (G_OBJECT (sel), "active-pad");

  if (close_event)
    gst_pad_push_event (sel->srcpad, close_event);

  if (start_event)
    gst_pad_push_event (sel->srcpad, start_event);

  if (selpad->discont) {
    buf = gst_buffer_make_metadata_writable (buf);

    GST_DEBUG_OBJECT (pad, "Marking discont buffer %p", buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    selpad->discont = FALSE;
  }

  /* forward */
  GST_DEBUG_OBJECT (pad, "Forwarding buffer %p from pad %s:%s", buf,
      GST_DEBUG_PAD_NAME (pad));

  if ((caps = GST_BUFFER_CAPS (buf))) {
    if (GST_PAD_CAPS (sel->srcpad) != caps)
      gst_pad_set_caps (sel->srcpad, caps);
  }

  res = gst_pad_push (sel->srcpad, buf);

done:
  gst_object_unref (sel);
  return res;

  /* dropped buffers */
ignore:
  {
    GST_DEBUG_OBJECT (pad, "Pad not active, discard buffer %p", buf);
    /* when we drop a buffer, we're creating a discont on this pad */
    selpad->discont = TRUE;
    GST_INPUT_SELECTOR_UNLOCK (sel);
    gst_buffer_unref (buf);

    /* figure out what to return upstream */
    GST_OBJECT_LOCK (selpad);
    if (selpad->always_ok)
      res = GST_FLOW_OK;
    else
      res = GST_FLOW_NOT_LINKED;
    GST_OBJECT_UNLOCK (selpad);

    goto done;
  }
flushing:
  {
    GST_DEBUG_OBJECT (pad, "We are flushing, discard buffer %p", buf);
    GST_INPUT_SELECTOR_UNLOCK (sel);
    gst_buffer_unref (buf);
    res = GST_FLOW_WRONG_STATE;
    goto done;
  }
}

static void gst_input_selector_init (GstInputSelector * sel);
static void gst_input_selector_base_init (GstInputSelectorClass * klass);
static void gst_input_selector_class_init (GstInputSelectorClass * klass);

static void gst_input_selector_dispose (GObject * object);

static void gst_input_selector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_input_selector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_input_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_input_selector_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_input_selector_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_input_selector_getcaps (GstPad * pad);
static gboolean gst_input_selector_event (GstPad * pad, GstEvent * event);
static gboolean gst_input_selector_query (GstPad * pad, GstQuery * query);
static gint64 gst_input_selector_block (GstInputSelector * self);
static void gst_input_selector_switch (GstInputSelector * self,
    GstPad * pad, gint64 stop_time, gint64 start_time);

static GstElementClass *parent_class = NULL;

GType
gst_input_selector_get_type (void)
{
  static GType input_selector_type = 0;

  if (!input_selector_type) {
    static const GTypeInfo input_selector_info = {
      sizeof (GstInputSelectorClass),
      (GBaseInitFunc) gst_input_selector_base_init,
      NULL,
      (GClassInitFunc) gst_input_selector_class_init,
      NULL,
      NULL,
      sizeof (GstInputSelector),
      0,
      (GInstanceInitFunc) gst_input_selector_init,
    };
    input_selector_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstPlaybin2InputSelector", &input_selector_info, 0);
    GST_DEBUG_CATEGORY_INIT (input_selector_debug,
        "playbin2-input-selector", 0, "Playbin2 input stream selector element");
  }

  return input_selector_type;
}

static void
gst_input_selector_base_init (GstInputSelectorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "Input selector", "Generic",
      "N-to-1 input stream selectoring",
      "Julien Moutte <julien@moutte.net>, "
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Wim Taymans <wim.taymans@gmail.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_input_selector_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_input_selector_src_factory));
}

static void
gst_input_selector_class_init (GstInputSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_input_selector_dispose;

  gobject_class->set_property = gst_input_selector_set_property;
  gobject_class->get_property = gst_input_selector_get_property;

  g_object_class_install_property (gobject_class, PROP_N_PADS,
      g_param_spec_uint ("n-pads", "Number of Pads",
          "The number of sink pads", 0, G_MAXUINT, 0, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active pad",
          "The currently active sink pad", GST_TYPE_PAD, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SELECT_ALL,
      g_param_spec_boolean ("select-all", "Select all mode",
          "Forwards data from all input pads", FALSE, G_PARAM_READWRITE));

  /**
   * GstInputSelector::block:
   * @inputselector: the #GstInputSelector
   *
   * Block all sink pads in preparation for a switch. Returns the stop time of
   * the current switch segment, as a running time, or 0 if there is no current
   * active pad or the current active pad never received data.
   */
  gst_input_selector_signals[SIGNAL_BLOCK] =
      g_signal_new ("block", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstInputSelectorClass, block), NULL, NULL,
      gst_play_marshal_INT64__VOID, G_TYPE_INT64, 0);
  /**
   * GstInputSelector::switch:
   * @inputselector: the #GstInputSelector
   * @pad:            the pad to switch to
   * @stop_time:      running time at which to close the previous segment, or -1
   *                  to use the running time of the previously active sink pad
   * @start_time:     running time at which to start the new segment, or -1 to
   *                  use the running time of the newly active sink pad
   *
   * Switch to a new feed. The segment opened by the previously active pad, if
   * any, will be closed, and a new segment opened before data flows again.
   *
   * This signal must be emitted when the element has been blocked via the <link
   * linkend="GstInputSelector-block">block</link> signal.
   *
   * If you have a stream with only one switch element, such as an audio-only
   * stream, a stream switch should be performed by first emitting the block
   * signal, and then emitting the switch signal with -1 for the stop and start
   * time values.
   *
   * The intention of the @stop_time and @start_time arguments is to allow
   * multiple switch elements to switch and maintain stream synchronization.
   * When switching a stream with multiple feeds, you will need as many switch
   * elements as you have feeds. For example, a feed with audio and video will
   * have one switch element between the audio feeds and one for video.
   *
   * A switch over multiple switch elements should be performed as follows:
   * First, emit the <link linkend="GstInputSelector-block">block</link>
   * signal, collecting the returned values. The maximum running time returned
   * by block should then be used as the time at which to close the previous
   * segment.
   *
   * Then, query the running times of the new audio and video pads that you will
   * switch to. Naturally, these pads are on separate switch elements. Take the
   * minimum running time for those streams and use it for the time at which to
   * open the new segment.
   *
   * If @pad is the same as the current active pad, the element will cancel any
   * previous block without adjusting segments.
   *
   * <note><simpara>
   * the signal changed from accepting the pad name to the pad object.
   * </simpara></note>
   *
   * Since: 0.10.7
   */
  gst_input_selector_signals[SIGNAL_SWITCH] =
      g_signal_new ("switch", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstInputSelectorClass, switch_),
      NULL, NULL, gst_play_marshal_VOID__OBJECT_INT64_INT64,
      G_TYPE_NONE, 3, GST_TYPE_PAD, G_TYPE_INT64, G_TYPE_INT64);

  gstelement_class->request_new_pad = gst_input_selector_request_new_pad;
  gstelement_class->release_pad = gst_input_selector_release_pad;
  gstelement_class->change_state = gst_input_selector_change_state;

  klass->block = GST_DEBUG_FUNCPTR (gst_input_selector_block);
  /* note the underscore because switch is a keyword otherwise */
  klass->switch_ = GST_DEBUG_FUNCPTR (gst_input_selector_switch);
}

static void
gst_input_selector_init (GstInputSelector * sel)
{
  sel->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_iterate_internal_links_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_iterate_linked_pads));
  gst_pad_set_getcaps_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_input_selector_getcaps));
  gst_pad_set_query_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_input_selector_query));
  gst_pad_set_event_function (sel->srcpad,
      GST_DEBUG_FUNCPTR (gst_input_selector_event));
  gst_element_add_pad (GST_ELEMENT (sel), sel->srcpad);
  /* sinkpad management */
  sel->active_sinkpad = NULL;
  sel->padcount = 0;
  gst_segment_init (&sel->segment, GST_FORMAT_UNDEFINED);

  sel->lock = g_mutex_new ();
  sel->cond = g_cond_new ();
  sel->blocked = FALSE;

  sel->select_all = FALSE;
}

static void
gst_input_selector_dispose (GObject * object)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (object);

  if (sel->active_sinkpad) {
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }
  if (sel->lock) {
    g_mutex_free (sel->lock);
    sel->lock = NULL;
  }
  if (sel->cond) {
    g_cond_free (sel->cond);
    sel->cond = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* Solve the following equation for B.timestamp, and set that as the segment
 * stop:
 * B.running_time = (B.timestamp - NS.start) / NS.abs_rate + NS.accum
 */
static gint64
gst_segment_get_timestamp (GstSegment * segment, gint64 running_time)
{
  if (running_time <= segment->accum)
    return segment->start;
  else
    return (running_time - segment->accum) * segment->abs_rate + segment->start;
}

static void
gst_segment_set_stop (GstSegment * segment, gint64 running_time)
{
  segment->stop = gst_segment_get_timestamp (segment, running_time);
  segment->last_stop = -1;
}

static void
gst_segment_set_start (GstSegment * segment, gint64 running_time)
{
  gint64 new_start, duration;

  new_start = gst_segment_get_timestamp (segment, running_time);

  /* this is the duration we skipped */
  duration = new_start - segment->start;
  /* add the duration to the accumulated segment time */
  segment->accum += duration;
  /* move position in the segment */
  segment->time += duration;
  segment->start += duration;
}

/* this function must be called with the SELECTOR_LOCK. It returns TRUE when the
 * active pad changed. */
static gboolean
gst_input_selector_set_active_pad (GstInputSelector * self,
    GstPad * pad, gint64 stop_time, gint64 start_time)
{
  GstSelectorPad *old, *new;
  GstPad **active_pad_p;

  if (pad == self->active_sinkpad)
    return FALSE;

  old = GST_SELECTOR_PAD_CAST (self->active_sinkpad);
  new = GST_SELECTOR_PAD_CAST (pad);

  GST_DEBUG_OBJECT (self, "setting active pad to %s:%s",
      GST_DEBUG_PAD_NAME (new));

  if (stop_time == -1 && old) {
    /* no stop time given, get the latest running_time on the active pad to 
     * close and open the new segment */
    stop_time = start_time = gst_selector_pad_get_running_time (old);
    GST_DEBUG_OBJECT (self, "using start/stop of %" G_GINT64_FORMAT,
        start_time);
  }

  if (old && old->active && !self->pending_close && stop_time >= 0) {
    /* schedule a last_stop update if one isn't already scheduled, and a
       segment has been pushed before. */
    memcpy (&self->segment, &old->segment, sizeof (self->segment));

    GST_DEBUG_OBJECT (self, "setting stop_time to %" G_GINT64_FORMAT,
        stop_time);
    gst_segment_set_stop (&self->segment, stop_time);
    self->pending_close = TRUE;
  }

  if (new && new->active && start_time >= 0) {
    GST_DEBUG_OBJECT (self, "setting start_time to %" G_GINT64_FORMAT,
        start_time);
    /* schedule a new segment push */
    gst_segment_set_start (&new->segment, start_time);
    new->segment_pending = TRUE;
  }

  active_pad_p = &self->active_sinkpad;
  gst_object_replace ((GstObject **) active_pad_p, GST_OBJECT_CAST (pad));
  GST_DEBUG_OBJECT (self, "New active pad is %" GST_PTR_FORMAT,
      self->active_sinkpad);

  return TRUE;
}

static void
gst_input_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
    {
      GstPad *pad;

      pad = g_value_get_object (value);

      GST_INPUT_SELECTOR_LOCK (sel);
      gst_input_selector_set_active_pad (sel, pad,
          GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    }
    case PROP_SELECT_ALL:
      GST_INPUT_SELECTOR_LOCK (object);
      sel->select_all = g_value_get_boolean (value);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_input_selector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstInputSelector *sel = GST_INPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_N_PADS:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_uint (value, sel->n_pads);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    case PROP_ACTIVE_PAD:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_object (value, sel->active_sinkpad);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    case PROP_SELECT_ALL:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_boolean (value, sel->select_all);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_input_selector_get_linked_pad (GstPad * pad, gboolean strict)
{
  GstInputSelector *sel;
  GstPad *otherpad = NULL;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));

  GST_INPUT_SELECTOR_LOCK (sel);
  if (pad == sel->srcpad)
    otherpad = sel->active_sinkpad;
  else if (pad == sel->active_sinkpad || !strict)
    otherpad = sel->srcpad;
  if (otherpad)
    gst_object_ref (otherpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  gst_object_unref (sel);

  return otherpad;
}

static gboolean
gst_input_selector_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstPad *otherpad;

  otherpad = gst_input_selector_get_linked_pad (pad, TRUE);

  if (otherpad) {
    res = gst_pad_push_event (otherpad, event);

    gst_object_unref (otherpad);
  } else
    gst_event_unref (event);
  return res;
}

/* query on the srcpad. We override this function because by default it will
 * only forward the query to one random sinkpad */
static gboolean
gst_input_selector_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstInputSelector *sel;
  GstPad *otherpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));

  otherpad = gst_input_selector_get_linked_pad (pad, TRUE);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GList *walk;
      GstClockTime resmin, resmax;
      gboolean reslive;

      resmin = 0;
      resmax = -1;
      reslive = FALSE;

      /* assume FALSE, we become TRUE if one query succeeds */
      res = FALSE;

      /* perform the query on all sinkpads and combine the results. We take the
       * max of min and the min of max for the result latency. */
      GST_INPUT_SELECTOR_LOCK (sel);
      for (walk = GST_ELEMENT_CAST (sel)->sinkpads; walk;
          walk = g_list_next (walk)) {
        GstPad *sinkpad = GST_PAD_CAST (walk->data);

        if (gst_pad_peer_query (sinkpad, query)) {
          GstClockTime min, max;
          gboolean live;

          /* one query succeeded, we succeed too */
          res = TRUE;

          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (sinkpad,
              "peer latency min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT
              ", live %d", GST_TIME_ARGS (min), GST_TIME_ARGS (max), live);

          if (live) {
            if (min > resmin)
              resmin = min;
            if (resmax == -1)
              resmax = max;
            else if (max < resmax)
              resmax = max;
            if (reslive == FALSE)
              reslive = live;
          }
        }
      }
      GST_INPUT_SELECTOR_UNLOCK (sel);
      if (res) {
        gst_query_set_latency (query, reslive, resmin, resmax);

        GST_DEBUG_OBJECT (sel,
            "total latency min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT
            ", live %d", GST_TIME_ARGS (resmin), GST_TIME_ARGS (resmax),
            reslive);
      }

      break;
    }
    default:
      if (otherpad)
        res = gst_pad_peer_query (otherpad, query);
      break;
  }
  if (otherpad)
    gst_object_unref (otherpad);
  gst_object_unref (sel);

  return res;
}

static GstCaps *
gst_input_selector_getcaps (GstPad * pad)
{
  GstPad *otherpad;
  GstObject *parent;
  GstCaps *caps;

  parent = gst_object_get_parent (GST_OBJECT (pad));

  otherpad = gst_input_selector_get_linked_pad (pad, FALSE);

  if (!otherpad) {
    if (GST_INPUT_SELECTOR (parent)->select_all) {
      GST_DEBUG_OBJECT (parent,
          "Pad %s:%s not linked, returning merge of caps",
          GST_DEBUG_PAD_NAME (pad));
      caps = gst_pad_proxy_getcaps (pad);
    } else {
      GST_DEBUG_OBJECT (parent,
          "Pad %s:%s not linked, returning ANY", GST_DEBUG_PAD_NAME (pad));
      caps = gst_caps_new_any ();
    }
  } else {
    GST_DEBUG_OBJECT (parent,
        "Pad %s:%s is linked (to %s:%s), returning peer caps",
        GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (otherpad));
    /* if the peer has caps, use those. If the pad is not linked, this function
     * returns NULL and we return ANY */
    if (!(caps = gst_pad_peer_get_caps_reffed (otherpad)))
      caps = gst_caps_new_any ();
    gst_object_unref (otherpad);
  }

  gst_object_unref (parent);
  return caps;
}

/* check if the pad is the active sinkpad */
static gboolean
gst_input_selector_is_active_sinkpad (GstInputSelector * sel, GstPad * pad)
{
  gboolean res;

  GST_INPUT_SELECTOR_LOCK (sel);
  res = (pad == sel->active_sinkpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  return res;
}

/* Get or create the active sinkpad, must be called with SELECTOR_LOCK */
static GstPad *
gst_input_selector_activate_sinkpad (GstInputSelector * sel, GstPad * pad)
{
  GstPad *active_sinkpad;
  GstSelectorPad *selpad;

  selpad = GST_SELECTOR_PAD_CAST (pad);

  selpad->active = TRUE;
  active_sinkpad = sel->active_sinkpad;
  if (active_sinkpad == NULL || sel->select_all) {
    /* first pad we get activity on becomes the activated pad by default, if we
     * select all, we also remember the last used pad. */
    if (sel->active_sinkpad)
      gst_object_unref (sel->active_sinkpad);
    active_sinkpad = sel->active_sinkpad = gst_object_ref (pad);
    GST_DEBUG_OBJECT (sel, "Activating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }

  return active_sinkpad;
}

static GstPad *
gst_input_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused)
{
  GstInputSelector *sel;
  gchar *name = NULL;
  GstPad *sinkpad = NULL;

  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);

  sel = GST_INPUT_SELECTOR (element);

  GST_INPUT_SELECTOR_LOCK (sel);

  GST_LOG_OBJECT (sel, "Creating new pad %d", sel->padcount);
  name = g_strdup_printf ("sink%d", sel->padcount++);
  sinkpad = g_object_new (GST_TYPE_SELECTOR_PAD,
      "name", name, "direction", templ->direction, "template", templ, NULL);
  g_free (name);

  sel->n_pads++;

  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_event));
  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_getcaps));
  gst_pad_set_acceptcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_acceptcaps));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_chain));
  gst_pad_set_iterate_internal_links_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_iterate_linked_pads));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_selector_pad_bufferalloc));

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (sel), sinkpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  return sinkpad;
}

static void
gst_input_selector_release_pad (GstElement * element, GstPad * pad)
{
  GstInputSelector *sel;

  sel = GST_INPUT_SELECTOR (element);
  GST_LOG_OBJECT (sel, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_INPUT_SELECTOR_LOCK (sel);
  /* if the pad was the active pad, makes us select a new one */
  if (sel->active_sinkpad == pad) {
    GST_DEBUG_OBJECT (sel, "Deactivating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }
  sel->n_pads--;

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT (sel), pad);
  GST_INPUT_SELECTOR_UNLOCK (sel);
}

static void
gst_input_selector_reset (GstInputSelector * sel)
{
  GList *walk;

  GST_INPUT_SELECTOR_LOCK (sel);
  /* clear active pad */
  if (sel->active_sinkpad) {
    gst_object_unref (sel->active_sinkpad);
    sel->active_sinkpad = NULL;
  }
  /* reset segment */
  gst_segment_init (&sel->segment, GST_FORMAT_UNDEFINED);
  sel->pending_close = FALSE;
  /* reset each of our sinkpads state */
  for (walk = GST_ELEMENT_CAST (sel)->sinkpads; walk; walk = g_list_next (walk)) {
    GstSelectorPad *selpad = GST_SELECTOR_PAD_CAST (walk->data);

    gst_selector_pad_reset (selpad);

    if (selpad->tags) {
      gst_tag_list_free (selpad->tags);
      selpad->tags = NULL;
    }
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);
}

static GstStateChangeReturn
gst_input_selector_change_state (GstElement * element,
    GstStateChange transition)
{
  GstInputSelector *self = GST_INPUT_SELECTOR (element);
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_INPUT_SELECTOR_LOCK (self);
      self->blocked = FALSE;
      self->flushing = FALSE;
      GST_INPUT_SELECTOR_UNLOCK (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* first unlock before we call the parent state change function, which
       * tries to acquire the stream lock when going to ready. */
      GST_INPUT_SELECTOR_LOCK (self);
      self->blocked = FALSE;
      self->flushing = TRUE;
      GST_INPUT_SELECTOR_BROADCAST (self);
      GST_INPUT_SELECTOR_UNLOCK (self);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_input_selector_reset (self);
      break;
    default:
      break;
  }

  return result;
}

static gint64
gst_input_selector_block (GstInputSelector * self)
{
  gint64 ret = 0;
  GstSelectorPad *spad;

  GST_INPUT_SELECTOR_LOCK (self);

  if (self->blocked)
    GST_WARNING_OBJECT (self, "switch already blocked");

  self->blocked = TRUE;
  spad = GST_SELECTOR_PAD_CAST (self->active_sinkpad);

  if (spad)
    ret = gst_selector_pad_get_running_time (spad);
  else
    GST_DEBUG_OBJECT (self, "no active pad while blocking");

  GST_INPUT_SELECTOR_UNLOCK (self);

  return ret;
}

/* stop_time and start_time are running times */
static void
gst_input_selector_switch (GstInputSelector * self, GstPad * pad,
    gint64 stop_time, gint64 start_time)
{
  gboolean changed;

  g_return_if_fail (self->blocked == TRUE);

  GST_INPUT_SELECTOR_LOCK (self);
  changed =
      gst_input_selector_set_active_pad (self, pad, stop_time, start_time);

  self->blocked = FALSE;
  GST_INPUT_SELECTOR_BROADCAST (self);
  GST_INPUT_SELECTOR_UNLOCK (self);

  if (changed)
    g_object_notify (G_OBJECT (self), "active-pad");
}

static gboolean
gst_input_selector_check_eos (GstElement * selector)
{
  GstIterator *it = gst_element_iterate_sink_pads (selector);
  GstIteratorResult ires;
  gpointer item;
  gboolean done = FALSE, is_eos = FALSE;
  GstSelectorPad *pad;

  while (!done) {
    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        GST_INFO_OBJECT (selector, "all sink pads have eos");
        done = TRUE;
        is_eos = TRUE;
        break;
      case GST_ITERATOR_OK:
        pad = GST_SELECTOR_PAD_CAST (item);
        if (!pad->eos) {
          done = TRUE;
        }
        gst_object_unref (pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      default:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  return is_eos;
}
