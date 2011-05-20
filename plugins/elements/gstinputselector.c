/* GStreamer input selector
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2007 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2007 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
 * Copyright (C) 2011 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 *
 * The input pads are from a GstPad subclass and have additional
 * properties, which users may find useful, namely:
 *
 * <itemizedlist>
 * <listitem>
 * "running-time": Running time of stream on pad (#gint64)
 * </listitem>
 * <listitem>
 * "tags": The currently active tags on the pad (#GstTagList, boxed type)
 * </listitem>
 * <listitem>
 * "active": If the pad is currently active (#gboolean)
 * </listitem>
 * <listitem>
 * "always-ok" : Make an inactive pads return #GST_FLOW_OK instead of
 * #GST_FLOW_NOT_LINKED
 * </listitem>
 * </itemizedlist>
 *
 * Since: 0.10.32
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstinputselector.h"

GST_DEBUG_CATEGORY_STATIC (input_selector_debug);
#define GST_CAT_DEFAULT input_selector_debug

#if GLIB_CHECK_VERSION(2, 26, 0)
#define NOTIFY_MUTEX_LOCK()
#define NOTIFY_MUTEX_UNLOCK()
#else
static GStaticRecMutex notify_mutex = G_STATIC_REC_MUTEX_INIT;
#define NOTIFY_MUTEX_LOCK() g_static_rec_mutex_lock (&notify_mutex)
#define NOTIFY_MUTEX_UNLOCK() g_static_rec_mutex_unlock (&notify_mutex)
#endif

#define GST_INPUT_SELECTOR_GET_LOCK(sel) (((GstInputSelector*)(sel))->lock)
#define GST_INPUT_SELECTOR_GET_COND(sel) (((GstInputSelector*)(sel))->cond)
#define GST_INPUT_SELECTOR_LOCK(sel) (g_mutex_lock (GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_UNLOCK(sel) (g_mutex_unlock (GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_WAIT(sel) (g_cond_wait (GST_INPUT_SELECTOR_GET_COND(sel), \
			GST_INPUT_SELECTOR_GET_LOCK(sel)))
#define GST_INPUT_SELECTOR_BROADCAST(sel) (g_cond_broadcast (GST_INPUT_SELECTOR_GET_COND(sel)))

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
  PROP_SYNC_STREAMS
};

#define DEFAULT_SYNC_STREAMS FALSE

#define DEFAULT_PAD_ALWAYS_OK TRUE

enum
{
  PROP_PAD_0,
  PROP_PAD_RUNNING_TIME,
  PROP_PAD_TAGS,
  PROP_PAD_ACTIVE,
  PROP_PAD_ALWAYS_OK
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
static GstPad *gst_input_selector_get_linked_pad (GstInputSelector * sel,
    GstPad * pad, gboolean strict);

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
  gboolean pushed;              /* when buffer was pushed downstream since activation */
  gboolean eos;                 /* when EOS has been received */
  gboolean eos_sent;            /* when EOS was sent downstream */
  gboolean discont;             /* after switching we create a discont */
  gboolean flushing;            /* set after flush-start and before flush-stop */
  gboolean always_ok;
  GstTagList *tags;             /* last tags received on the pad */

  GstClockTime position;        /* the current position in the segment */
  GstSegment segment;           /* the current segment on the pad */
  guint32 segment_seqnum;       /* sequence number of the current segment */

  gboolean segment_pending;
};

struct _GstSelectorPadClass
{
  GstPadClass parent;
};

GType gst_selector_pad_get_type (void);
static void gst_selector_pad_finalize (GObject * object);
static void gst_selector_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_selector_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static gint64 gst_selector_pad_get_running_time (GstSelectorPad * pad);
static void gst_selector_pad_reset (GstSelectorPad * pad);
static gboolean gst_selector_pad_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_selector_pad_getcaps (GstPad * pad, GstCaps * filter);
static gboolean gst_selector_pad_acceptcaps (GstPad * pad, GstCaps * caps);
static GstIterator *gst_selector_pad_iterate_linked_pads (GstPad * pad);
static GstFlowReturn gst_selector_pad_chain (GstPad * pad, GstBuffer * buf);

G_DEFINE_TYPE (GstSelectorPad, gst_selector_pad, GST_TYPE_PAD);

static void
gst_selector_pad_class_init (GstSelectorPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_selector_pad_finalize;

  gobject_class->get_property = gst_selector_pad_get_property;
  gobject_class->set_property = gst_selector_pad_set_property;

  g_object_class_install_property (gobject_class, PROP_PAD_RUNNING_TIME,
      g_param_spec_int64 ("running-time", "Running time",
          "Running time of stream on pad", 0, G_MAXINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_TAGS,
      g_param_spec_boxed ("tags", "Tags",
          "The currently active tags on the pad", GST_TYPE_TAG_LIST,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "If the pad is currently active", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /* FIXME: better property name? */
  g_object_class_install_property (gobject_class, PROP_PAD_ALWAYS_OK,
      g_param_spec_boolean ("always-ok", "Always OK",
          "Make an inactive pad return OK instead of NOT_LINKED",
          DEFAULT_PAD_ALWAYS_OK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  G_OBJECT_CLASS (gst_selector_pad_parent_class)->finalize (object);
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
    guint64 position = pad->position;
    GstFormat format = pad->segment.format;

    ret = gst_segment_to_running_time (&pad->segment, format, position);
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
  pad->pushed = FALSE;
  pad->eos = FALSE;
  pad->eos_sent = FALSE;
  pad->segment_pending = FALSE;
  pad->discont = FALSE;
  pad->flushing = FALSE;
  pad->position = GST_CLOCK_TIME_NONE;
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  GST_OBJECT_UNLOCK (pad);
}

/* strictly get the linked pad from the sinkpad. If the pad is active we return
 * the srcpad else we return NULL */
static GstIterator *
gst_selector_pad_iterate_linked_pads (GstPad * pad)
{
  GstInputSelector *sel;
  GstPad *otherpad;
  GstIterator *it;
  GValue val = { 0, };

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return NULL;

  otherpad = gst_input_selector_get_linked_pad (sel, pad, TRUE);
  g_value_init (&val, GST_TYPE_PAD);
  g_value_set_object (&val, otherpad);
  it = gst_iterator_new_single (GST_TYPE_PAD, &val);
  g_value_unset (&val);

  if (otherpad)
    gst_object_unref (otherpad);
  gst_object_unref (sel);

  return it;
}

static gboolean
gst_selector_pad_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  gboolean forward;
  GstInputSelector *sel;
  GstSelectorPad *selpad;
  GstPad *prev_active_sinkpad;
  GstPad *active_sinkpad;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }
  selpad = GST_SELECTOR_PAD_CAST (pad);

  GST_INPUT_SELECTOR_LOCK (sel);
  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* only forward if we are dealing with the active sinkpad */
  forward = (pad == active_sinkpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad) {
    NOTIFY_MUTEX_LOCK ();
    g_object_notify (G_OBJECT (sel), "active-pad");
    NOTIFY_MUTEX_UNLOCK ();
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* Unblock the pad if it's waiting */
      GST_INPUT_SELECTOR_LOCK (sel);
      selpad->flushing = TRUE;
      GST_INPUT_SELECTOR_BROADCAST (sel);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_INPUT_SELECTOR_LOCK (sel);
      gst_selector_pad_reset (selpad);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    case GST_EVENT_SEGMENT:
    {
      GST_INPUT_SELECTOR_LOCK (sel);
      GST_OBJECT_LOCK (selpad);
      gst_event_copy_segment (event, &selpad->segment);
      selpad->segment_seqnum = gst_event_get_seqnum (event);

      /* Update the position */
      if (selpad->position == GST_CLOCK_TIME_NONE
          || selpad->segment.position > selpad->position) {
        selpad->position = selpad->segment.position;
      } else if (selpad->position != GST_CLOCK_TIME_NONE
          && selpad->position > selpad->segment.position) {
        selpad->segment.position = selpad->position;

        if (forward) {
          gst_event_unref (event);
          event = gst_event_new_segment (&selpad->segment);
          gst_event_set_seqnum (event, selpad->segment_seqnum);
        }
      }
      GST_DEBUG_OBJECT (pad, "configured SEGMENT %" GST_SEGMENT_FORMAT,
          &selpad->segment);

      /* If we aren't forwarding the event because the pad is not the
       * active_sinkpad, then set the flag on the pad
       * that says a segment needs sending if/when that pad is activated.
       * For all other cases, we send the event immediately, which makes
       * sparse streams and other segment updates work correctly downstream.
       */
      if (!forward)
        selpad->segment_pending = TRUE;

      GST_OBJECT_UNLOCK (selpad);
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

      if (forward) {
        selpad->eos_sent = TRUE;
      } else {
        GstSelectorPad *tmp;

        /* If the active sinkpad is in EOS state but EOS
         * was not sent downstream this means that the pad
         * got EOS before it was set as active pad and that
         * the previously active pad got EOS after it was
         * active
         */
        GST_INPUT_SELECTOR_LOCK (sel);
        active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);
        tmp = GST_SELECTOR_PAD (active_sinkpad);
        forward = (tmp->eos && !tmp->eos_sent);
        tmp->eos_sent = TRUE;
        GST_INPUT_SELECTOR_UNLOCK (sel);
      }
      GST_DEBUG_OBJECT (pad, "received EOS");
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
gst_selector_pad_getcaps (GstPad * pad, GstCaps * filter)
{
  GstInputSelector *sel;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return (filter ? gst_caps_ref (filter) : gst_caps_new_any ());

  GST_DEBUG_OBJECT (sel, "Getting caps of srcpad peer");
  caps = gst_pad_peer_get_caps (sel->srcpad, filter);
  if (caps == NULL)
    caps = (filter ? gst_caps_ref (filter) : gst_caps_new_any ());

  gst_object_unref (sel);

  return caps;
}

static gboolean
gst_selector_pad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstInputSelector *sel;
  gboolean res;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return FALSE;

  GST_DEBUG_OBJECT (sel, "Checking acceptcaps of srcpad peer");
  res = gst_pad_peer_accept_caps (sel->srcpad, caps);
  gst_object_unref (sel);

  return res;
}

/* must be called with the SELECTOR_LOCK, will block while the pad is blocked 
 * or return TRUE when flushing */
static gboolean
gst_input_selector_wait (GstInputSelector * self, GstSelectorPad * pad)
{
  while (self->blocked && !self->flushing && !pad->flushing) {
    /* we can be unlocked here when we are shutting down (flushing) or when we
     * get unblocked */
    GST_INPUT_SELECTOR_WAIT (self);
  }
  return self->flushing;
}

/* must be called with the SELECTOR_LOCK, will block until the running time
 * of the active pad is after this pad or return TRUE when flushing */
static gboolean
gst_input_selector_wait_running_time (GstInputSelector * sel,
    GstSelectorPad * pad, GstBuffer * buf)
{
  GstPad *active_sinkpad;
  GstSelectorPad *active_selpad;
  GstSegment *seg, *active_seg;
  GstClockTime running_time, active_running_time = -1;

  seg = &pad->segment;

  active_sinkpad =
      gst_input_selector_activate_sinkpad (sel, GST_PAD_CAST (pad));
  active_selpad = GST_SELECTOR_PAD_CAST (active_sinkpad);
  active_seg = &active_selpad->segment;

  /* We can only sync if the segments are in time format or
   * if the active pad had no newsegment event yet */
  if (seg->format != GST_FORMAT_TIME ||
      (active_seg->format != GST_FORMAT_TIME
          && active_seg->format != GST_FORMAT_UNDEFINED))
    return FALSE;

  /* If we have no valid timestamp we can't sync this buffer */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf))
    return FALSE;

  running_time = GST_BUFFER_TIMESTAMP (buf);
  /* If possible try to get the running time at the end of the buffer */
  if (GST_BUFFER_DURATION_IS_VALID (buf))
    running_time += GST_BUFFER_DURATION (buf);
  if (running_time > seg->stop)
    running_time = seg->stop;
  running_time =
      gst_segment_to_running_time (seg, GST_FORMAT_TIME, running_time);
  /* If this is outside the segment don't sync */
  if (running_time == -1)
    return FALSE;

  /* Get active pad's running time, if no configured segment yet keep at -1 */
  if (active_seg->format == GST_FORMAT_TIME)
    active_running_time =
        gst_segment_to_running_time (active_seg, GST_FORMAT_TIME,
        active_selpad->position);

  /* Wait until
   *   a) this is the active pad
   *   b) the pad or the selector is flushing
   *   c) the selector is not blocked
   *   d) the active pad has no running time or the active
   *      pad's running time is before this running time
   *   e) the active pad has a non-time segment
   */
  while (pad != active_selpad && !sel->flushing && !pad->flushing &&
      (sel->blocked || active_running_time == -1
          || running_time >= active_running_time)) {
    if (!sel->blocked)
      GST_DEBUG_OBJECT (pad,
          "Waiting for active streams to advance. %" GST_TIME_FORMAT " >= %"
          GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
          GST_TIME_ARGS (active_running_time));

    GST_INPUT_SELECTOR_WAIT (sel);

    /* Get new active pad, it might have changed */
    active_sinkpad =
        gst_input_selector_activate_sinkpad (sel, GST_PAD_CAST (pad));
    active_selpad = GST_SELECTOR_PAD_CAST (active_sinkpad);
    active_seg = &active_selpad->segment;

    /* If the active segment is configured but not to time format
     * we can't do any syncing at all */
    if (active_seg->format != GST_FORMAT_TIME
        && active_seg->format != GST_FORMAT_UNDEFINED)
      break;

    /* Get the new active pad running time */
    if (active_seg->format == GST_FORMAT_TIME)
      active_running_time =
          gst_segment_to_running_time (active_seg, GST_FORMAT_TIME,
          active_selpad->position);
    else
      active_running_time = -1;

    if (!sel->blocked)
      GST_DEBUG_OBJECT (pad,
          "Waited for active streams to advance. %" GST_TIME_FORMAT " >= %"
          GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
          GST_TIME_ARGS (active_running_time));

  }

  /* Return TRUE if the selector or the pad is flushing */
  return (sel->flushing || pad->flushing);
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
  GstEvent *start_event = NULL;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  selpad = GST_SELECTOR_PAD_CAST (pad);
  seg = &selpad->segment;

  GST_INPUT_SELECTOR_LOCK (sel);
  /* wait or check for flushing */
  if (gst_input_selector_wait (sel, selpad))
    goto flushing;

  GST_LOG_OBJECT (pad, "getting active pad");

  prev_active_sinkpad = sel->active_sinkpad;
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* In sync mode wait until the active pad has advanced
   * after the running time of the current buffer */
  if (sel->sync_streams && active_sinkpad != pad) {
    if (gst_input_selector_wait_running_time (sel, selpad, buf))
      goto flushing;
  }

  /* Might have changed while waiting */
  active_sinkpad = gst_input_selector_activate_sinkpad (sel, pad);

  /* update the segment on the srcpad */
  start_time = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (start_time)) {
    GST_LOG_OBJECT (pad, "received start time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start_time));
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      GST_LOG_OBJECT (pad, "received end time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start_time + GST_BUFFER_DURATION (buf)));

    GST_OBJECT_LOCK (pad);
    selpad->position = start_time;
    GST_OBJECT_UNLOCK (pad);
  }

  /* Ignore buffers from pads except the selected one */
  if (pad != active_sinkpad)
    goto ignore;

  /* Tell all non-active pads that we advanced the running time */
  if (sel->sync_streams)
    GST_INPUT_SELECTOR_BROADCAST (sel);

  /* if we have a pending segment, push it out now */
  if (G_UNLIKELY (selpad->segment_pending)) {
    GST_DEBUG_OBJECT (pad,
        "pushing pending NEWSEGMENT update %d, rate %lf, applied rate %lf, "
        "format %d, "
        "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
        G_GINT64_FORMAT, FALSE, seg->rate, seg->applied_rate, seg->format,
        seg->start, seg->stop, seg->time);

    start_event = gst_event_new_segment (seg);
    gst_event_set_seqnum (start_event, selpad->segment_seqnum);

    selpad->segment_pending = FALSE;
  }
  GST_INPUT_SELECTOR_UNLOCK (sel);

  if (prev_active_sinkpad != active_sinkpad && pad == active_sinkpad) {
    NOTIFY_MUTEX_LOCK ();
    g_object_notify (G_OBJECT (sel), "active-pad");
    NOTIFY_MUTEX_UNLOCK ();
  }

  if (start_event)
    gst_pad_push_event (sel->srcpad, start_event);

  if (selpad->discont) {
    buf = gst_buffer_make_writable (buf);

    GST_DEBUG_OBJECT (pad, "Marking discont buffer %p", buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    selpad->discont = FALSE;
  }

  /* forward */
  GST_LOG_OBJECT (pad, "Forwarding buffer %p", buf);

  res = gst_pad_push (sel->srcpad, buf);
  selpad->pushed = TRUE;

done:
  gst_object_unref (sel);
  return res;

  /* dropped buffers */
ignore:
  {
    gboolean active_pad_pushed = GST_SELECTOR_PAD_CAST (active_sinkpad)->pushed;

    GST_DEBUG_OBJECT (pad, "Pad not active, discard buffer %p", buf);
    /* when we drop a buffer, we're creating a discont on this pad */
    selpad->discont = TRUE;
    GST_INPUT_SELECTOR_UNLOCK (sel);
    gst_buffer_unref (buf);

    /* figure out what to return upstream */
    GST_OBJECT_LOCK (selpad);
    if (selpad->always_ok || !active_pad_pushed)
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

static void gst_input_selector_dispose (GObject * object);

static void gst_input_selector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_input_selector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_input_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused, const GstCaps * caps);
static void gst_input_selector_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_input_selector_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_input_selector_getcaps (GstPad * pad, GstCaps * filter);
static gboolean gst_input_selector_event (GstPad * pad, GstEvent * event);
static gboolean gst_input_selector_query (GstPad * pad, GstQuery * query);
static gint64 gst_input_selector_block (GstInputSelector * self);

/* FIXME: create these marshallers using glib-genmarshal */
static void
gst_input_selector_marshal_INT64__VOID (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef gint64 (*GMarshalFunc_INT64__VOID) (gpointer data1, gpointer data2);
  register GMarshalFunc_INT64__VOID callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  gint64 v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_INT64__VOID) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1, data2);

  g_value_set_int64 (return_value, v_return);
}

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (input_selector_debug, \
        "input-selector", 0, "An input stream selector element");
#define gst_input_selector_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstInputSelector, gst_input_selector, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_input_selector_class_init (GstInputSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = gst_input_selector_dispose;

  gobject_class->set_property = gst_input_selector_set_property;
  gobject_class->get_property = gst_input_selector_get_property;

  g_object_class_install_property (gobject_class, PROP_N_PADS,
      g_param_spec_uint ("n-pads", "Number of Pads",
          "The number of sink pads", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active pad",
          "The currently active sink pad", GST_TYPE_PAD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstInputSelector:sync-streams
   *
   * If set to %TRUE all inactive streams will be synced to the
   * running time of the active stream. This makes sure that no
   * buffers are dropped by input-selector that might be needed
   * when switching the active pad.
   *
   * Since: 0.10.35
   */
  g_object_class_install_property (gobject_class, PROP_SYNC_STREAMS,
      g_param_spec_boolean ("sync-streams", "Sync Streams",
          "Synchronize inactive streams to the running time of the active stream",
          DEFAULT_SYNC_STREAMS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
      gst_input_selector_marshal_INT64__VOID, G_TYPE_INT64, 0);

  gst_element_class_set_details_simple (gstelement_class, "Input selector",
      "Generic", "N-to-1 input stream selector",
      "Julien Moutte <julien@moutte.net>, "
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Wim Taymans <wim.taymans@gmail.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_input_selector_sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_input_selector_src_factory));

  gstelement_class->request_new_pad = gst_input_selector_request_new_pad;
  gstelement_class->release_pad = gst_input_selector_release_pad;
  gstelement_class->change_state = gst_input_selector_change_state;

  klass->block = GST_DEBUG_FUNCPTR (gst_input_selector_block);
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
  sel->sync_streams = DEFAULT_SYNC_STREAMS;

  sel->lock = g_mutex_new ();
  sel->cond = g_cond_new ();
  sel->blocked = FALSE;
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

/* this function must be called with the SELECTOR_LOCK. It returns TRUE when the
 * active pad changed. */
static gboolean
gst_input_selector_set_active_pad (GstInputSelector * self, GstPad * pad)
{
  GstSelectorPad *old, *new;
  GstPad **active_pad_p;

  if (pad == self->active_sinkpad)
    return FALSE;

  old = GST_SELECTOR_PAD_CAST (self->active_sinkpad);
  new = GST_SELECTOR_PAD_CAST (pad);

  GST_DEBUG_OBJECT (self, "setting active pad to %s:%s",
      GST_DEBUG_PAD_NAME (new));

  if (old)
    old->pushed = FALSE;
  if (new)
    new->pushed = FALSE;

  active_pad_p = &self->active_sinkpad;
  gst_object_replace ((GstObject **) active_pad_p, GST_OBJECT_CAST (pad));

  /* Wake up all non-active pads in sync mode, they might be
   * the active pad now */
  if (self->sync_streams)
    GST_INPUT_SELECTOR_BROADCAST (self);

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
      gst_input_selector_set_active_pad (sel, pad);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    }
    case PROP_SYNC_STREAMS:
    {
      GST_INPUT_SELECTOR_LOCK (sel);
      sel->sync_streams = g_value_get_boolean (value);
      GST_INPUT_SELECTOR_UNLOCK (sel);
      break;
    }
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
    case PROP_SYNC_STREAMS:
      GST_INPUT_SELECTOR_LOCK (object);
      g_value_set_boolean (value, sel->sync_streams);
      GST_INPUT_SELECTOR_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_input_selector_get_linked_pad (GstInputSelector * sel, GstPad * pad,
    gboolean strict)
{
  GstPad *otherpad = NULL;

  GST_INPUT_SELECTOR_LOCK (sel);
  if (pad == sel->srcpad)
    otherpad = sel->active_sinkpad;
  else if (pad == sel->active_sinkpad || !strict)
    otherpad = sel->srcpad;
  if (otherpad)
    gst_object_ref (otherpad);
  GST_INPUT_SELECTOR_UNLOCK (sel);

  return otherpad;
}

static gboolean
gst_input_selector_event (GstPad * pad, GstEvent * event)
{
  GstInputSelector *sel;
  gboolean result = FALSE;
  GstIterator *iter;
  gboolean done = FALSE;
  GValue item = { 0, };
  GstPad *eventpad;
  GList *pushed_pads = NULL;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  /* Send upstream events to all sinkpads */
  iter = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (sel));

  /* This is now essentially a copy of gst_pad_event_default_dispatch
   * with a different iterator */
  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        eventpad = g_value_get_object (&item);

        /* if already pushed,  skip */
        if (g_list_find (pushed_pads, eventpad)) {
          g_value_reset (&item);
          break;
        }

        gst_event_ref (event);
        result |= gst_pad_push_event (eventpad, event);

        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        /* We don't reset the result here because we don't push the event
         * again on pads that got the event already and because we need
         * to consider the result of the previous pushes */
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR_OBJECT (pad, "Could not iterate over sinkpads");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

  g_list_free (pushed_pads);

  gst_event_unref (event);

  return result;
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
  if (G_UNLIKELY (sel == NULL))
    return FALSE;

  otherpad = gst_input_selector_get_linked_pad (sel, pad, TRUE);

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
gst_input_selector_getcaps (GstPad * pad, GstCaps * filter)
{
  GstPad *otherpad;
  GstInputSelector *sel;
  GstCaps *caps;

  sel = GST_INPUT_SELECTOR (gst_pad_get_parent (pad));
  if (G_UNLIKELY (sel == NULL))
    return (filter ? gst_caps_ref (filter) : gst_caps_new_any ());

  otherpad = gst_input_selector_get_linked_pad (sel, pad, FALSE);

  if (!otherpad) {
    GST_DEBUG_OBJECT (pad, "Pad not linked, returning ANY");
    caps = (filter ? gst_caps_ref (filter) : gst_caps_new_any ());
  } else {
    GST_DEBUG_OBJECT (pad, "Pad is linked (to %s:%s), returning peer caps",
        GST_DEBUG_PAD_NAME (otherpad));
    /* if the peer has caps, use those. If the pad is not linked, this function
     * returns NULL and we return ANY */
    if (!(caps = gst_pad_peer_get_caps (otherpad, filter)))
      caps = (filter ? gst_caps_ref (filter) : gst_caps_new_any ());
    gst_object_unref (otherpad);
  }

  gst_object_unref (sel);
  return caps;
}

/* check if the pad is the active sinkpad */
static inline gboolean
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
  if (active_sinkpad == NULL) {
    /* first pad we get activity on becomes the activated pad by default */
    if (sel->active_sinkpad)
      gst_object_unref (sel->active_sinkpad);
    active_sinkpad = sel->active_sinkpad = gst_object_ref (pad);
    GST_DEBUG_OBJECT (sel, "Activating pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  }

  return active_sinkpad;
}

static GstPad *
gst_input_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused, const GstCaps * caps)
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
