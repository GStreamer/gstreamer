/* GStreamer concat element
 *
 *  Copyright (c) 2014 Sebastian Dröge <sebastian@centricular.com>
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
 *
 */
/**
 * SECTION:element-concat
 * @title: concat
 * @see_also: #GstFunnel
 *
 * Concatenates streams together to one continous stream.
 *
 * All streams but the current one are blocked until the current one
 * finished with %GST_EVENT_EOS. Then the next stream is enabled, while
 * keeping the running time continous for %GST_FORMAT_TIME segments or
 * keeping the segment continous for %GST_FORMAT_BYTES segments.
 *
 * Streams are switched in the order in which the sinkpads were requested.
 *
 * By default, the stream segment's base values are adjusted to ensure
 * the segment transitions between streams are continuous. In some cases,
 * it may be desirable to turn off these adjustments (for example, because
 * another downstream element like a streamsynchronizer adjusts the base
 * values on its own). The adjust-base property can be used for this purpose.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 concat name=c ! xvimagesink  videotestsrc num-buffers=100 ! c.   videotestsrc num-buffers=100 pattern=ball ! c.
 * ]| Plays two video streams one after another.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstconcat.h"

GST_DEBUG_CATEGORY_STATIC (gst_concat_debug);
#define GST_CAT_DEFAULT gst_concat_debug

G_GNUC_INTERNAL GType gst_concat_pad_get_type (void);

#define GST_TYPE_CONCAT_PAD (gst_concat_pad_get_type())
#define GST_CONCAT_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_CONCAT_PAD, GstConcatPad))
#define GST_CONCAT_PAD_CAST(obj) ((GstConcatPad *)(obj))
#define GST_CONCAT_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_CONCAT_PAD, GstConcatPadClass))
#define GST_IS_CONCAT_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_CONCAT_PAD))
#define GST_IS_CONCAT_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_CONCAT_PAD))

typedef struct _GstConcatPad GstConcatPad;
typedef struct _GstConcatPadClass GstConcatPadClass;

struct _GstConcatPad
{
  GstPad parent;

  GstSegment segment;

  /* Protected by the concat lock */
  gboolean flushing;
};

struct _GstConcatPadClass
{
  GstPadClass parent;
};

G_DEFINE_TYPE (GstConcatPad, gst_concat_pad, GST_TYPE_PAD);

static void
gst_concat_pad_class_init (GstConcatPadClass * klass)
{
}

static void
gst_concat_pad_init (GstConcatPad * self)
{
  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
  self->flushing = FALSE;
}

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_ACTIVE_PAD,
  PROP_ADJUST_BASE
};

#define DEFAULT_ADJUST_BASE TRUE

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_concat_debug, "concat", 0, "concat element");
#define gst_concat_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstConcat, gst_concat, GST_TYPE_ELEMENT, _do_init);

static void gst_concat_dispose (GObject * object);
static void gst_concat_finalize (GObject * object);
static void gst_concat_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_concat_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_concat_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_concat_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_concat_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn gst_concat_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_concat_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_concat_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static gboolean gst_concat_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_concat_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static gboolean gst_concat_switch_pad (GstConcat * self);

static void gst_concat_notify_active_pad (GstConcat * self);

static GParamSpec *pspec_active_pad = NULL;

static void
gst_concat_class_init (GstConcatClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_concat_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_concat_finalize);

  gobject_class->get_property = gst_concat_get_property;
  gobject_class->set_property = gst_concat_set_property;

  pspec_active_pad = g_param_spec_object ("active-pad", "Active pad",
      "Currently active src pad", GST_TYPE_PAD, G_PARAM_READABLE |
      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      pspec_active_pad);
  g_object_class_install_property (gobject_class, PROP_ADJUST_BASE,
      g_param_spec_boolean ("adjust-base", "Adjust segment base",
          "Adjust the base value of segments to ensure they are adjacent",
          DEFAULT_ADJUST_BASE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Concat", "Generic", "Concatenate multiple streams",
      "Sebastian Dröge <sebastian@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_concat_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_concat_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_concat_change_state);
}

static void
gst_concat_init (GstConcat * self)
{
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_concat_src_event));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_concat_src_query));
  gst_pad_use_fixed_caps (self->srcpad);

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->adjust_base = DEFAULT_ADJUST_BASE;
}

static void
gst_concat_dispose (GObject * object)
{
  GstConcat *self = GST_CONCAT (object);
  GList *item;

  gst_object_replace ((GstObject **) & self->current_sinkpad, NULL);

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);

    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_concat_finalize (GObject * object)
{
  GstConcat *self = GST_CONCAT (object);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_concat_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstConcat *self = GST_CONCAT (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:{
      g_mutex_lock (&self->lock);
      g_value_set_object (value, self->current_sinkpad);
      g_mutex_unlock (&self->lock);
      break;
    }
    case PROP_ADJUST_BASE:{
      g_mutex_lock (&self->lock);
      g_value_set_boolean (value, self->adjust_base);
      g_mutex_unlock (&self->lock);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_concat_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstConcat *self = GST_CONCAT (object);

  switch (prop_id) {
    case PROP_ADJUST_BASE:{
      g_mutex_lock (&self->lock);
      self->adjust_base = g_value_get_boolean (value);
      g_mutex_unlock (&self->lock);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_concat_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstConcat *self = GST_CONCAT (element);
  GstPad *sinkpad;
  gchar *pad_name;
  gboolean do_notify = FALSE;

  GST_DEBUG_OBJECT (element, "requesting pad");

  g_mutex_lock (&self->lock);
  pad_name = g_strdup_printf ("sink_%u", self->pad_count);
  self->pad_count++;
  g_mutex_unlock (&self->lock);

  sinkpad = GST_PAD_CAST (g_object_new (GST_TYPE_CONCAT_PAD,
          "name", pad_name, "direction", templ->direction, "template", templ,
          NULL));
  g_free (pad_name);

  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_concat_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_concat_sink_event));
  gst_pad_set_query_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_concat_sink_query));
  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_CAPS);
  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_pad_set_active (sinkpad, TRUE);

  g_mutex_lock (&self->lock);
  self->sinkpads = g_list_prepend (self->sinkpads, gst_object_ref (sinkpad));
  if (!self->current_sinkpad) {
    do_notify = TRUE;
    self->current_sinkpad = gst_object_ref (sinkpad);
  }
  g_mutex_unlock (&self->lock);

  gst_element_add_pad (element, sinkpad);

  if (do_notify)
    gst_concat_notify_active_pad (self);

  return sinkpad;
}

static void
gst_concat_release_pad (GstElement * element, GstPad * pad)
{
  GstConcat *self = GST_CONCAT (element);
  GstConcatPad *spad = GST_CONCAT_PAD_CAST (pad);
  GList *l;
  gboolean current_pad_removed = FALSE;
  gboolean eos = FALSE;
  gboolean do_notify = FALSE;

  GST_DEBUG_OBJECT (self, "releasing pad");

  g_mutex_lock (&self->lock);
  spad->flushing = TRUE;
  g_cond_broadcast (&self->cond);
  g_mutex_unlock (&self->lock);

  gst_pad_set_active (pad, FALSE);

  /* Now the pad is definitely not running anymore */

  g_mutex_lock (&self->lock);
  if (self->current_sinkpad == GST_PAD_CAST (spad)) {
    eos = !gst_concat_switch_pad (self);
    current_pad_removed = TRUE;
    do_notify = TRUE;
  }

  for (l = self->sinkpads; l; l = l->next) {
    if ((gpointer) spad == l->data) {
      gst_object_unref (spad);
      self->sinkpads = g_list_delete_link (self->sinkpads, l);
      break;
    }
  }
  g_mutex_unlock (&self->lock);

  gst_element_remove_pad (GST_ELEMENT_CAST (self), pad);

  if (do_notify)
    gst_concat_notify_active_pad (self);

  if (GST_STATE (self) > GST_STATE_READY) {
    if (current_pad_removed && !eos)
      gst_element_post_message (GST_ELEMENT_CAST (self),
          gst_message_new_duration_changed (GST_OBJECT_CAST (self)));

    /* FIXME: Sending EOS from application thread */
    if (eos)
      gst_pad_push_event (self->srcpad, gst_event_new_eos ());
  }
}

/* Returns FALSE if flushing
 * Must be called from the pad's streaming thread
 */
static gboolean
gst_concat_pad_wait (GstConcatPad * spad, GstConcat * self)
{
  g_mutex_lock (&self->lock);
  if (spad->flushing) {
    g_mutex_unlock (&self->lock);
    GST_DEBUG_OBJECT (spad, "Flushing");
    return FALSE;
  }

  while (spad != GST_CONCAT_PAD_CAST (self->current_sinkpad)) {
    GST_TRACE_OBJECT (spad, "Not the current sinkpad - waiting");
    g_cond_wait (&self->cond, &self->lock);
    if (spad->flushing) {
      g_mutex_unlock (&self->lock);
      GST_DEBUG_OBJECT (spad, "Flushing");
      return FALSE;
    }
  }
  /* This pad can only become not the current sinkpad from
   * a) This streaming thread (we hold the stream lock)
   * b) Releasing the pad (takes the stream lock, see above)
   *
   * Unlocking here is thus safe and we can safely push
   * serialized data to our srcpad
   */
  GST_DEBUG_OBJECT (spad, "Now the current sinkpad");
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static GstFlowReturn
gst_concat_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstConcat *self = GST_CONCAT (parent);
  GstConcatPad *spad = GST_CONCAT_PAD (pad);

  GST_LOG_OBJECT (pad, "received buffer %p", buffer);

  if (!gst_concat_pad_wait (spad, self))
    return GST_FLOW_FLUSHING;

  if (self->last_stop == GST_CLOCK_TIME_NONE)
    self->last_stop = spad->segment.start;

  if (self->format == GST_FORMAT_TIME) {
    GstClockTime start_time = GST_BUFFER_TIMESTAMP (buffer);
    GstClockTime end_time = GST_CLOCK_TIME_NONE;

    if (start_time != GST_CLOCK_TIME_NONE)
      end_time = start_time;
    if (GST_BUFFER_DURATION_IS_VALID (buffer))
      end_time += GST_BUFFER_DURATION (buffer);

    if (end_time != GST_CLOCK_TIME_NONE && end_time > self->last_stop)
      self->last_stop = end_time;
  } else {
    self->last_stop += gst_buffer_get_size (buffer);
  }

  ret = gst_pad_push (self->srcpad, buffer);

  GST_LOG_OBJECT (pad, "handled buffer %s", gst_flow_get_name (ret));

  return ret;
}

/* Returns FALSE if no further pad, must be called with concat lock */
static gboolean
gst_concat_switch_pad (GstConcat * self)
{
  GList *l;
  gboolean next;
  GstSegment segment;
  gint64 last_stop;

  segment = GST_CONCAT_PAD (self->current_sinkpad)->segment;

  last_stop = self->last_stop;
  if (last_stop == GST_CLOCK_TIME_NONE)
    last_stop = segment.stop;
  if (last_stop == GST_CLOCK_TIME_NONE)
    last_stop = segment.start;
  g_assert (last_stop != GST_CLOCK_TIME_NONE);

  if (last_stop > segment.stop)
    last_stop = segment.stop;

  if (segment.format == GST_FORMAT_TIME)
    last_stop =
        gst_segment_to_running_time (&segment, segment.format, last_stop);
  else
    last_stop += segment.start;

  self->current_start_offset += last_stop;

  for (l = self->sinkpads; l; l = l->next) {
    if ((gpointer) self->current_sinkpad == l->data) {
      l = l->prev;
      GST_DEBUG_OBJECT (self,
          "Switching from pad %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
          self->current_sinkpad, l ? l->data : NULL);
      gst_object_unref (self->current_sinkpad);
      self->current_sinkpad = l ? gst_object_ref (l->data) : NULL;
      g_cond_broadcast (&self->cond);
      break;
    }
  }

  next = self->current_sinkpad != NULL;

  self->last_stop = GST_CLOCK_TIME_NONE;

  return next;
}

static void
gst_concat_notify_active_pad (GstConcat * self)
{
  g_object_notify_by_pspec ((GObject *) self, pspec_active_pad);
}

static gboolean
gst_concat_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstConcat *self = GST_CONCAT (parent);
  GstConcatPad *spad = GST_CONCAT_PAD_CAST (pad);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (pad, "received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:{
      if (!gst_concat_pad_wait (spad, self)) {
        ret = FALSE;
        gst_event_unref (event);
      } else {
        ret = gst_pad_event_default (pad, parent, event);
      }
      break;
    }
    case GST_EVENT_SEGMENT:{
      gboolean adjust_base;

      /* Drop segment event, we create our own one */
      gst_event_copy_segment (event, &spad->segment);
      gst_event_unref (event);

      g_mutex_lock (&self->lock);
      adjust_base = self->adjust_base;
      if (self->format == GST_FORMAT_UNDEFINED) {
        if (spad->segment.format != GST_FORMAT_TIME
            && spad->segment.format != GST_FORMAT_BYTES) {
          g_mutex_unlock (&self->lock);
          GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
              ("Can only operate in TIME or BYTES format"));
          ret = FALSE;
          break;
        }
        self->format = spad->segment.format;
        GST_DEBUG_OBJECT (self, "Operating in %s format",
            gst_format_get_name (self->format));
        g_mutex_unlock (&self->lock);
      } else if (self->format != spad->segment.format) {
        g_mutex_unlock (&self->lock);
        GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
            ("Operating in %s format but new pad has %s",
                gst_format_get_name (self->format),
                gst_format_get_name (spad->segment.format)));
        ret = FALSE;
      } else {
        g_mutex_unlock (&self->lock);
      }

      if (!gst_concat_pad_wait (spad, self)) {
        ret = FALSE;
      } else {
        GstSegment segment = spad->segment;

        if (adjust_base) {
          /* We know no duration */
          segment.duration = -1;

          /* Update segment values to be continous with last stream */
          if (self->format == GST_FORMAT_TIME) {
            segment.base += self->current_start_offset;
          } else {
            /* Shift start/stop byte position */
            segment.start += self->current_start_offset;
            if (segment.stop != -1)
              segment.stop += self->current_start_offset;
          }
        }

        gst_pad_push_event (self->srcpad, gst_event_new_segment (&segment));
      }
      break;
    }
    case GST_EVENT_EOS:{
      gst_event_unref (event);

      if (!gst_concat_pad_wait (spad, self)) {
        ret = FALSE;
      } else {
        gboolean next;

        g_mutex_lock (&self->lock);
        next = gst_concat_switch_pad (self);
        g_mutex_unlock (&self->lock);
        ret = TRUE;

        gst_concat_notify_active_pad (self);

        if (!next) {
          gst_pad_push_event (self->srcpad, gst_event_new_eos ());
        } else {
          gst_element_post_message (GST_ELEMENT_CAST (self),
              gst_message_new_duration_changed (GST_OBJECT_CAST (self)));
        }
      }
      break;
    }
    case GST_EVENT_FLUSH_START:{
      gboolean forward;

      g_mutex_lock (&self->lock);
      spad->flushing = TRUE;
      g_cond_broadcast (&self->cond);
      forward = (self->current_sinkpad == GST_PAD_CAST (spad));
      g_mutex_unlock (&self->lock);

      if (forward)
        ret = gst_pad_event_default (pad, parent, event);
      else
        gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      gboolean forward;

      gst_segment_init (&spad->segment, GST_FORMAT_UNDEFINED);
      spad->flushing = FALSE;

      g_mutex_lock (&self->lock);
      forward = (self->current_sinkpad == GST_PAD_CAST (spad));
      g_mutex_unlock (&self->lock);

      if (forward) {
        gboolean reset_time;

        gst_event_parse_flush_stop (event, &reset_time);
        if (reset_time) {
          GST_DEBUG_OBJECT (self,
              "resetting start offset to 0 after flushing with reset_time = TRUE");
          self->current_start_offset = 0;
        }
        ret = gst_pad_event_default (pad, parent, event);
      } else {
        gst_event_unref (event);
      }
      break;
    }
    default:{
      /* Wait for other serialized events before forwarding */
      if (GST_EVENT_IS_SERIALIZED (event) && !gst_concat_pad_wait (spad, self)) {
        gst_event_unref (event);
        ret = FALSE;
      } else {
        ret = gst_pad_event_default (pad, parent, event);
      }
      break;
    }
  }

  return ret;
}

static gboolean
gst_concat_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstConcat *self = GST_CONCAT (parent);
  GstConcatPad *spad = GST_CONCAT_PAD_CAST (pad);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (pad, "received query %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    default:
      /* Wait for other serialized queries before forwarding */
      if (GST_QUERY_IS_SERIALIZED (query) && !gst_concat_pad_wait (spad, self)) {
        ret = FALSE;
      } else {
        ret = gst_pad_query_default (pad, parent, query);
      }
      break;
  }

  return ret;
}

static gboolean
gst_concat_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstConcat *self = GST_CONCAT (parent);
  gboolean ret = TRUE;

  GST_LOG_OBJECT (pad, "received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstPad *sinkpad = NULL;

      g_mutex_lock (&self->lock);
      if ((sinkpad = self->current_sinkpad))
        gst_object_ref (sinkpad);
      g_mutex_unlock (&self->lock);
      if (sinkpad) {
        ret = gst_pad_push_event (sinkpad, event);
        gst_object_unref (sinkpad);
      } else {
        gst_event_unref (event);
        ret = FALSE;
      }
      break;
    }
    case GST_EVENT_QOS:{
      GstQOSType type;
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);
      gst_event_unref (event);

      if (timestamp != GST_CLOCK_TIME_NONE
          && timestamp > self->current_start_offset) {
        timestamp -= self->current_start_offset;
        event = gst_event_new_qos (type, proportion, diff, timestamp);
        ret = gst_pad_push_event (self->current_sinkpad, event);
      } else {
        ret = FALSE;
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      gboolean reset_time;

      gst_event_parse_flush_stop (event, &reset_time);
      if (reset_time) {
        GST_DEBUG_OBJECT (self,
            "resetting start offset to 0 after flushing with reset_time = TRUE");
        self->current_start_offset = 0;
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_concat_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = TRUE;

  GST_LOG_OBJECT (pad, "received query %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static void
reset_pad (const GValue * data, gpointer user_data)
{
  GstPad *pad = g_value_get_object (data);
  GstConcatPad *spad = GST_CONCAT_PAD_CAST (pad);

  gst_segment_init (&spad->segment, GST_FORMAT_UNDEFINED);
  spad->flushing = FALSE;
}

static void
unblock_pad (const GValue * data, gpointer user_data)
{
  GstPad *pad = g_value_get_object (data);
  GstConcatPad *spad = GST_CONCAT_PAD_CAST (pad);

  spad->flushing = TRUE;
}

static GstStateChangeReturn
gst_concat_change_state (GstElement * element, GstStateChange transition)
{
  GstConcat *self = GST_CONCAT (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GstIterator *iter = gst_element_iterate_sink_pads (element);
      GstIteratorResult res;

      self->format = GST_FORMAT_UNDEFINED;
      self->current_start_offset = 0;
      self->last_stop = GST_CLOCK_TIME_NONE;

      while ((res =
              gst_iterator_foreach (iter, reset_pad,
                  NULL)) == GST_ITERATOR_RESYNC)
        gst_iterator_resync (iter);
      gst_iterator_free (iter);

      if (res == GST_ITERATOR_ERROR)
        return GST_STATE_CHANGE_FAILURE;
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      GstIterator *iter = gst_element_iterate_sink_pads (element);
      GstIteratorResult res;

      g_mutex_lock (&self->lock);
      while ((res =
              gst_iterator_foreach (iter, unblock_pad,
                  NULL)) == GST_ITERATOR_RESYNC)
        gst_iterator_resync (iter);
      gst_iterator_free (iter);
      g_cond_broadcast (&self->cond);
      g_mutex_unlock (&self->lock);

      if (res == GST_ITERATOR_ERROR)
        return GST_STATE_CHANGE_FAILURE;

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}
