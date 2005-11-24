/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbasesrc.c:
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
 * SECTION:gstbasesrc
 * @short_description: Base class for getrange based source elements
 * @see_also: #GstBaseTransform, #GstBaseSink
 *
 * This class is mostly useful for elements that do byte based
 * access to a random access resource, like files.
 * If random access is not possible, the live-mode should be set
 * to TRUE.
 *
 * <itemizedlist>
 *   <listitem><para>one source pad</para></listitem>
 *   <listitem><para>handles state changes</para></listitem>
 *   <listitem><para>does flushing</para></listitem>
 *   <listitem><para>preroll with optional preview</para></listitem>
 *   <listitem><para>pull/push mode</para></listitem>
 *   <listitem><para>EOS handling</para></listitem>
 * </itemizedlist>
 */

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbasesrc.h"
#include "gsttypefindhelper.h"
#include <gst/gstmarshal.h>
#include <gst/gst-i18n-lib.h>

#define DEFAULT_BLOCKSIZE	4096
#define DEFAULT_NUM_BUFFERS	-1

GST_DEBUG_CATEGORY_STATIC (gst_base_src_debug);
#define GST_CAT_DEFAULT gst_base_src_debug

#define GST_LIVE_GET_LOCK(elem)               (GST_BASE_SRC_CAST(elem)->live_lock)
#define GST_LIVE_LOCK(elem)                   g_mutex_lock(GST_LIVE_GET_LOCK(elem))
#define GST_LIVE_TRYLOCK(elem)                g_mutex_trylock(GST_LIVE_GET_LOCK(elem))
#define GST_LIVE_UNLOCK(elem)                 g_mutex_unlock(GST_LIVE_GET_LOCK(elem))
#define GST_LIVE_GET_COND(elem)               (GST_BASE_SRC_CAST(elem)->live_cond)
#define GST_LIVE_WAIT(elem)                   g_cond_wait (GST_LIVE_GET_COND (elem), GST_LIVE_GET_LOCK (elem))
#define GST_LIVE_TIMED_WAIT(elem, timeval)    g_cond_timed_wait (GST_LIVE_GET_COND (elem), GST_LIVE_GET_LOCK (elem),\
		                                                                timeval)
#define GST_LIVE_SIGNAL(elem)                 g_cond_signal (GST_LIVE_GET_COND (elem));
#define GST_LIVE_BROADCAST(elem)              g_cond_broadcast (GST_LIVE_GET_COND (elem));

/* BaseSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_BLOCKSIZE,
  PROP_NUM_BUFFERS,
};

static GstElementClass *parent_class = NULL;

static void gst_base_src_base_init (gpointer g_class);
static void gst_base_src_class_init (GstBaseSrcClass * klass);
static void gst_base_src_init (GstBaseSrc * src, gpointer g_class);
static void gst_base_src_finalize (GObject * object);


GType
gst_base_src_get_type (void)
{
  static GType base_src_type = 0;

  if (!base_src_type) {
    static const GTypeInfo base_src_info = {
      sizeof (GstBaseSrcClass),
      (GBaseInitFunc) gst_base_src_base_init,
      NULL,
      (GClassInitFunc) gst_base_src_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSrc),
      0,
      (GInstanceInitFunc) gst_base_src_init,
    };

    base_src_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseSrc", &base_src_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_src_type;
}
static GstCaps *gst_base_src_getcaps (GstPad * pad);
static gboolean gst_base_src_setcaps (GstPad * pad, GstCaps * caps);

static gboolean gst_base_src_activate_push (GstPad * pad, gboolean active);
static gboolean gst_base_src_activate_pull (GstPad * pad, gboolean active);
static void gst_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_base_src_event_handler (GstPad * pad, GstEvent * event);
static gboolean gst_base_src_send_event (GstElement * elem, GstEvent * event);

static gboolean gst_base_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_base_src_default_negotiate (GstBaseSrc * basesrc);
static gboolean gst_base_src_default_newsegment (GstBaseSrc * src);

static gboolean gst_base_src_unlock (GstBaseSrc * basesrc);
static gboolean gst_base_src_get_size (GstBaseSrc * basesrc, guint64 * size);
static gboolean gst_base_src_start (GstBaseSrc * basesrc);
static gboolean gst_base_src_stop (GstBaseSrc * basesrc);

static GstStateChangeReturn gst_base_src_change_state (GstElement * element,
    GstStateChange transition);

static void gst_base_src_loop (GstPad * pad);
static gboolean gst_base_src_check_get_range (GstPad * pad);
static GstFlowReturn gst_base_src_pad_get_range (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buf);
static GstFlowReturn gst_base_src_get_range (GstBaseSrc * src, guint64 offset,
    guint length, GstBuffer ** buf);

static void
gst_base_src_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_base_src_debug, "basesrc", 0, "basesrc element");
}

static void
gst_base_src_class_init (GstBaseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_src_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_base_src_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_base_src_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to read per buffer", 1, G_MAXULONG, DEFAULT_BLOCKSIZE,
          G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_NUM_BUFFERS,
      g_param_spec_int ("num-buffers", "num-buffers",
          "Number of buffers to output before sending EOS", -1, G_MAXINT,
          DEFAULT_NUM_BUFFERS, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_src_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_base_src_send_event);

  klass->negotiate = gst_base_src_default_negotiate;
  klass->newsegment = gst_base_src_default_newsegment;
}

static void
gst_base_src_init (GstBaseSrc * basesrc, gpointer g_class)
{
  GstPad *pad;
  GstPadTemplate *pad_template;

  basesrc->is_live = FALSE;
  basesrc->live_lock = g_mutex_new ();
  basesrc->live_cond = g_cond_new ();
  basesrc->num_buffers = DEFAULT_NUM_BUFFERS;
  basesrc->num_buffers_left = -1;

  basesrc->can_activate_push = TRUE;
  basesrc->pad_mode = GST_ACTIVATE_NONE;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);

  GST_DEBUG_OBJECT (basesrc, "creating src pad");
  pad = gst_pad_new_from_template (pad_template, "src");

  GST_DEBUG_OBJECT (basesrc, "setting functions on src pad");
  gst_pad_set_activatepush_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_src_activate_push));
  gst_pad_set_activatepull_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_src_activate_pull));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_src_event_handler));
  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_base_src_query));
  gst_pad_set_checkgetrange_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_src_check_get_range));
  gst_pad_set_getrange_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_src_pad_get_range));
  gst_pad_set_getcaps_function (pad, GST_DEBUG_FUNCPTR (gst_base_src_getcaps));
  gst_pad_set_setcaps_function (pad, GST_DEBUG_FUNCPTR (gst_base_src_setcaps));

  /* hold pointer to pad */
  basesrc->srcpad = pad;
  GST_DEBUG_OBJECT (basesrc, "adding src pad");
  gst_element_add_pad (GST_ELEMENT (basesrc), pad);

  basesrc->blocksize = DEFAULT_BLOCKSIZE;
  basesrc->clock_id = NULL;
  gst_segment_init (&basesrc->segment, GST_FORMAT_BYTES);

  GST_OBJECT_FLAG_UNSET (basesrc, GST_BASE_SRC_STARTED);

  GST_DEBUG_OBJECT (basesrc, "init done");
}

static void
gst_base_src_finalize (GObject * object)
{
  GstBaseSrc *basesrc;

  basesrc = GST_BASE_SRC (object);

  g_mutex_free (basesrc->live_lock);
  g_cond_free (basesrc->live_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_base_src_set_live:
 * @src: base source instance
 * @live: new live-mode
 *
 * If the element listens to a live source, the @livemode should
 * be set to %TRUE. This declares that this source can't seek.
 */
void
gst_base_src_set_live (GstBaseSrc * src, gboolean live)
{
  GST_LIVE_LOCK (src);
  src->is_live = live;
  GST_LIVE_UNLOCK (src);
}

/**
 * gst_base_src_is_live:
 * @src: base source instance
 *
 * Check if an element is in live mode.
 *
 * Returns: %TRUE if element is in live mode.
 */
gboolean
gst_base_src_is_live (GstBaseSrc * src)
{
  gboolean result;

  GST_LIVE_LOCK (src);
  result = src->is_live;
  GST_LIVE_UNLOCK (src);

  return result;
}

static gboolean
gst_base_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseSrcClass *bclass;
  GstBaseSrc *bsrc;
  gboolean res = TRUE;

  bsrc = GST_BASE_SRC (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SRC_GET_CLASS (bsrc);

  if (bclass->set_caps)
    res = bclass->set_caps (bsrc, caps);

  return res;
}

static GstCaps *
gst_base_src_getcaps (GstPad * pad)
{
  GstBaseSrcClass *bclass;
  GstBaseSrc *bsrc;
  GstCaps *caps = NULL;

  bsrc = GST_BASE_SRC (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SRC_GET_CLASS (bsrc);
  if (bclass->get_caps)
    caps = bclass->get_caps (bsrc);

  if (caps == NULL) {
    GstPadTemplate *pad_template;

    pad_template =
        gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
    if (pad_template != NULL) {
      caps = gst_caps_ref (gst_pad_template_get_caps (pad_template));
    }
  }
  return caps;
}

static gboolean
gst_base_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseSrc *src;
  gboolean res;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
          gst_query_set_position (query, GST_FORMAT_BYTES, src->offset);
          res = TRUE;
          break;
        case GST_FORMAT_PERCENT:
        {
          gboolean b;
          gint64 i64;
          guint64 ui64;

          b = gst_base_src_get_size (src, &ui64);
          if (b && src->offset < ui64)
            i64 = gst_util_uint64_scale (GST_FORMAT_PERCENT_MAX, src->offset,
                ui64);
          else
            i64 = GST_FORMAT_PERCENT_MAX;

          gst_query_set_position (query, GST_FORMAT_PERCENT, i64);
          res = TRUE;
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
        {
          gboolean b;
          gint64 i64;
          guint64 ui64;

          b = gst_base_src_get_size (src, &ui64);
          /* better to make get_size take an int64 */
          i64 = b ? (gint64) ui64 : -1;
          gst_query_set_duration (query, GST_FORMAT_BYTES, i64);
          res = TRUE;
          break;
        }
        case GST_FORMAT_PERCENT:
          gst_query_set_duration (query, GST_FORMAT_PERCENT,
              GST_FORMAT_PERCENT_MAX);
          res = TRUE;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    }

    case GST_QUERY_SEEKING:
      gst_query_set_seeking (query, GST_FORMAT_BYTES,
          src->seekable, 0, src->size);
      res = TRUE;
      break;

    case GST_QUERY_SEGMENT:
    {
      gint64 start, stop;

      start = src->segment.start;
      /* no end segment configured, current size then */
      if ((stop = src->segment.stop) == -1)
        stop = src->size;

      /* FIXME, we can't report our rate as we did not store it, d'oh!.
       * Also, subclasses might want to support other formats. */
      gst_query_set_segment (query, 1.0, GST_FORMAT_BYTES, start, stop);
      res = TRUE;
      break;
    }

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 3, GST_FORMAT_DEFAULT,
          GST_FORMAT_BYTES, GST_FORMAT_PERCENT);
      res = TRUE;
      break;

    case GST_QUERY_LATENCY:
    case GST_QUERY_JITTER:
    case GST_QUERY_RATE:
    case GST_QUERY_CONVERT:
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (src);
  return res;
}

static gboolean
gst_base_src_default_newsegment (GstBaseSrc * src)
{
  GstEvent *event;

  GST_DEBUG_OBJECT (src, "Sending newsegment from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, src->segment.start, src->segment.stop);

  event = gst_event_new_new_segment (FALSE, 1.0,
      GST_FORMAT_BYTES, src->segment.start, src->segment.stop,
      src->segment.start);

  return gst_pad_push_event (src->srcpad, event);
}

static gboolean
gst_base_src_newsegment (GstBaseSrc * src)
{
  GstBaseSrcClass *bclass;
  gboolean result = FALSE;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->newsegment)
    result = bclass->newsegment (src);

  return result;
}

/* based on the event parameters configure the segment.start/stop
 * times. Called with STREAM_LOCK.
 */
static gboolean
gst_base_src_configure_segment (GstBaseSrc * src, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean update;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  gst_segment_set_seek (&src->segment, rate, format, flags,
      cur_type, cur, stop_type, stop, &update);

  /* update our offset if it was updated */
  if (update)
    src->offset = cur;

  GST_DEBUG_OBJECT (src, "segment configured from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, src->segment.start, src->segment.stop);

  return TRUE;
}

/* this code implements the seeking. It is a good example
 * handling all cases (modulo the FIXMEs).
 *
 * A seek updates the currently configured segment.start
 * and segment.stop values based on the SEEK_TYPE. If the
 * segment.start value is updated, a seek to this new position
 * should be performed.
 *
 * The seek can only be executed when we are not currently
 * streaming any data, to make sure that this is the case, we
 * acquire the STREAM_LOCK which is taken when we are in the
 * _loop() function or when a getrange() is called. Normally
 * we will not receive a seek if we are operating in pull mode
 * though.
 *
 * When we are in the loop() function, we might be in the middle
 * of pushing a buffer, which might block in a sink. To make sure
 * that the push gets unblocked we push out a FLUSH_START event.
 * Our loop function will get a WRONG_STATE return value from
 * the push and will pause, effectively releasing the STREAM_LOCK.
 *
 * For a non-flushing seek, we pause the task, which might eventually
 * release the STREAM_LOCK. We say eventually because when the sink
 * blocks on the sample we might wait a very long time until the sink
 * unblocks the sample. In any case we acquire the STREAM_LOCK and
 * can continue the seek. A non-flushing seek is normally done in a 
 * running pipeline to perform seamless playback.
 *
 * After updating the segment.start/stop values, we prepare for
 * streaming again. We push out a FLUSH_STOP to make the peer pad
 * accept data again and we start our task again.
 *
 * A segment seek posts a message on the bus saying that the playback
 * of the segment started. We store the segment flag internally because
 * when we reach the segment.stop we have to post a segment.done
 * instead of EOS when doing a segment seek.
 */
static gboolean
gst_base_src_do_seek (GstBaseSrc * src, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  gboolean flush;

  gst_event_parse_seek (event, &rate, &format, &flags, NULL, NULL, NULL, NULL);

  /* FIXME subclasses should be able to provide other formats */
  /* get seek format */
  if (format == GST_FORMAT_DEFAULT)
    format = GST_FORMAT_BYTES;
  /* we can only seek bytes */
  if (format != GST_FORMAT_BYTES)
    goto unsupported_seek;

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* send flush start */
  if (flush)
    gst_pad_push_event (src->srcpad, gst_event_new_flush_start ());
  else
    gst_pad_pause_task (src->srcpad);

  /* unblock streaming thread */
  gst_base_src_unlock (src);

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or out streaming thread stopped 
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (src->srcpad);

  /* now configure the segment */
  gst_base_src_configure_segment (src, event);

  /* and prepare to continue streaming */
  if (flush)
    /* send flush stop, peer will accept data and events again. We
     * are not yet providing data as we still have the STREAM_LOCK. */
    gst_pad_push_event (src->srcpad, gst_event_new_flush_stop ());

  /* now make sure the newsegment will be send from the streaming
   * thread. We could opt to send it here too. */
  src->need_newsegment = TRUE;

  if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    /* FIXME subclasses should be able to provide other formats */
    gst_element_post_message (GST_ELEMENT (src),
        gst_message_new_segment_start (GST_OBJECT (src), GST_FORMAT_BYTES,
            src->segment.start));
  }

  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (src->srcpad, (GstTaskFunction) gst_base_src_loop,
      src->srcpad);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (src->srcpad);

  return TRUE;

  /* ERROR */
unsupported_seek:
  {
    GST_DEBUG_OBJECT (src, "invalid format, seek aborted.");

    return FALSE;
  }
}

/* all events send to this element directly
 */
static gboolean
gst_base_src_send_event (GstElement * element, GstEvent * event)
{
  GstBaseSrc *src;
  gboolean result;

  src = GST_BASE_SRC (element);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      result = gst_base_src_configure_segment (src, event);
      break;
    default:
      result = FALSE;
      break;
  }

  return result;
}

static gboolean
gst_base_src_event_handler (GstPad * pad, GstEvent * event)
{
  GstBaseSrc *src;
  GstBaseSrcClass *bclass;
  gboolean result;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));
  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->event) {
    if (!(result = bclass->event (src, event)))
      goto subclass_failed;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* is normally called when in push mode */
      if (!src->seekable)
        goto not_seekable;

      result = gst_base_src_do_seek (src, event);
      break;
    case GST_EVENT_FLUSH_START:
      /* cancel any blocking getrange, is normally called
       * when in pull mode. */
      result = gst_base_src_unlock (src);
      break;
    case GST_EVENT_FLUSH_STOP:
    default:
      result = TRUE;
      break;
  }
  gst_event_unref (event);
  gst_object_unref (src);

  return result;

  /* ERRORS */
subclass_failed:
  {
    GST_DEBUG_OBJECT (src, "subclass refused event");
    gst_object_unref (src);
    gst_event_unref (event);
    return result;
  }
not_seekable:
  {
    GST_DEBUG_OBJECT (src, "is not seekable");
    gst_object_unref (src);
    gst_event_unref (event);
    return FALSE;
  }
}

static void
gst_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseSrc *src;

  src = GST_BASE_SRC (object);

  switch (prop_id) {
    case PROP_BLOCKSIZE:
      src->blocksize = g_value_get_ulong (value);
      break;
    case PROP_NUM_BUFFERS:
      src->num_buffers = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseSrc *src;

  src = GST_BASE_SRC (object);

  switch (prop_id) {
    case PROP_BLOCKSIZE:
      g_value_set_ulong (value, src->blocksize);
      break;
    case PROP_NUM_BUFFERS:
      g_value_set_int (value, src->num_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* with STREAM_LOCK and LOCK*/
static GstClockReturn
gst_base_src_wait (GstBaseSrc * basesrc, GstClockTime time)
{
  GstClockReturn ret;
  GstClockID id;
  GstClock *clock;

  if ((clock = GST_ELEMENT_CLOCK (basesrc)) == NULL)
    return GST_CLOCK_OK;

  /* clock_id should be NULL outside of this function */
  g_assert (basesrc->clock_id == NULL);
  g_assert (GST_CLOCK_TIME_IS_VALID (time));

  id = gst_clock_new_single_shot_id (clock, time);

  basesrc->clock_id = id;
  /* release the object lock while waiting */
  GST_OBJECT_UNLOCK (basesrc);

  ret = gst_clock_id_wait (id, NULL);

  GST_OBJECT_LOCK (basesrc);
  gst_clock_id_unref (id);
  basesrc->clock_id = NULL;

  return ret;
}


/* perform synchronisation on a buffer
 */
static GstClockReturn
gst_base_src_do_sync (GstBaseSrc * basesrc, GstBuffer * buffer)
{
  GstClockReturn result = GST_CLOCK_OK;
  GstClockTime start, end;
  GstBaseSrcClass *bclass;
  gboolean start_valid;
  GstClockTime base_time;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);

  start = end = -1;
  if (bclass->get_times)
    bclass->get_times (basesrc, buffer, &start, &end);

  start_valid = GST_CLOCK_TIME_IS_VALID (start);

  /* if we don't have a timestamp, we don't sync */
  if (!start_valid) {
    GST_DEBUG_OBJECT (basesrc, "get_times returned invalid start");
    goto done;
  }

  GST_DEBUG_OBJECT (basesrc, "got times start: %" GST_TIME_FORMAT
      ", end: %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  /* now do clocking */
  GST_OBJECT_LOCK (basesrc);
  base_time = GST_ELEMENT_CAST (basesrc)->base_time;

  GST_LOG_OBJECT (basesrc,
      "waiting for clock, base time %" GST_TIME_FORMAT
      ", stream_start %" GST_TIME_FORMAT,
      GST_TIME_ARGS (base_time), GST_TIME_ARGS (start));

  result = gst_base_src_wait (basesrc, start + base_time);
  GST_OBJECT_UNLOCK (basesrc);

  GST_LOG_OBJECT (basesrc, "clock entry done: %d", result);

done:
  return result;
}


static GstFlowReturn
gst_base_src_get_range (GstBaseSrc * src, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstBaseSrcClass *bclass;
  gint64 maxsize;
  GstClockReturn status;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  GST_LIVE_LOCK (src);
  if (src->is_live) {
    while (!src->live_running) {
      GST_DEBUG ("live source signal waiting");
      GST_LIVE_SIGNAL (src);
      GST_DEBUG ("live source waiting for running state");
      GST_LIVE_WAIT (src);
      GST_DEBUG ("live source unlocked");
    }
    /* FIXME, use another variable to signal stopping */
    GST_OBJECT_LOCK (src->srcpad);
    if (GST_PAD_IS_FLUSHING (src->srcpad))
      goto flushing;
    GST_OBJECT_UNLOCK (src->srcpad);
  }
  GST_LIVE_UNLOCK (src);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_STARTED))
    goto not_started;

  if (G_UNLIKELY (!bclass->create))
    goto no_function;

  /* the max amount of bytes to read is the total size or
   * up to the segment.stop if present. */
  if (src->segment.stop != -1)
    maxsize = MIN (src->size, src->segment.stop);
  else
    maxsize = src->size;

  GST_DEBUG_OBJECT (src,
      "reading offset %" G_GUINT64_FORMAT ", length %u, size %" G_GINT64_FORMAT
      ", segment.stop %" G_GINT64_FORMAT ", maxsize %" G_GINT64_FORMAT, offset,
      length, src->size, src->segment.stop, maxsize);

  /* check size */
  if (maxsize != -1) {
    if (offset > maxsize)
      goto unexpected_length;

    if (offset + length > maxsize) {
      /* see if length of the file changed */
      if (bclass->get_size)
        bclass->get_size (src, &src->size);

      if (src->segment.stop != -1)
        maxsize = MIN (src->size, src->segment.stop);
      else
        maxsize = src->size;

      if (offset + length > maxsize) {
        length = maxsize - offset;
      }
    }
  }
  if (length == 0)
    goto unexpected_length;

  if (src->num_buffers_left == 0) {
    goto reached_num_buffers;
  } else {
    if (src->num_buffers_left > 0)
      src->num_buffers_left--;
  }

  ret = bclass->create (src, offset, length, buf);
  if (ret != GST_FLOW_OK)
    goto done;

  /* now sync before pushing the buffer */
  status = gst_base_src_do_sync (src, *buf);
  switch (status) {
    case GST_CLOCK_EARLY:
      GST_DEBUG_OBJECT (src, "buffer too late!, returning anyway");
      break;
    case GST_CLOCK_OK:
      GST_DEBUG_OBJECT (src, "buffer ok");
      break;
    default:
      GST_DEBUG_OBJECT (src, "clock returned %d, not returning", status);
      gst_buffer_unref (*buf);
      *buf = NULL;
      ret = GST_FLOW_WRONG_STATE;
      break;
  }
done:
  return ret;

  /* ERROR */
flushing:
  {
    GST_DEBUG_OBJECT (src, "pad is flushing");
    GST_OBJECT_UNLOCK (src->srcpad);
    GST_LIVE_UNLOCK (src);
    return GST_FLOW_WRONG_STATE;
  }
not_started:
  {
    GST_DEBUG_OBJECT (src, "getrange but not started");
    return GST_FLOW_WRONG_STATE;
  }
no_function:
  {
    GST_DEBUG_OBJECT (src, "no create function");
    return GST_FLOW_ERROR;
  }
unexpected_length:
  {
    GST_DEBUG_OBJECT (src, "unexpected length %u (offset=%" G_GUINT64_FORMAT
        ", size=%" G_GUINT64_FORMAT ")", length, offset, src->size);
    return GST_FLOW_UNEXPECTED;
  }
reached_num_buffers:
  {
    GST_DEBUG_OBJECT (src, "sent all buffers");
    return GST_FLOW_UNEXPECTED;
  }
}

static GstFlowReturn
gst_base_src_pad_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstBaseSrc *src;
  GstFlowReturn res;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  res = gst_base_src_get_range (src, offset, length, buf);

  gst_object_unref (src);

  return res;
}

static gboolean
gst_base_src_check_get_range (GstPad * pad)
{
  GstBaseSrc *src;

  src = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_STARTED)) {
    gst_base_src_start (src);
    gst_base_src_stop (src);
  }

  return src->seekable;
}

static void
gst_base_src_loop (GstPad * pad)
{
  GstBaseSrc *src;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  /* only send segments when operating in push mode */
  if (G_UNLIKELY (src->need_newsegment)) {
    /* now send newsegment */
    gst_base_src_newsegment (src);
    src->need_newsegment = FALSE;
  }

  ret = gst_base_src_get_range (src, src->offset, src->blocksize, &buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    if (ret == GST_FLOW_UNEXPECTED)
      goto eos;
    else
      goto pause;
  }
  if (G_UNLIKELY (buf == NULL))
    goto error;

  src->offset += GST_BUFFER_SIZE (buf);

  ret = gst_pad_push (pad, buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pause;

  gst_object_unref (src);
  return;

  /* special cases */
eos:
  {
    GST_DEBUG_OBJECT (src, "going to EOS, getrange returned UNEXPECTED");
    gst_pad_pause_task (pad);
    if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      /* FIXME, subclass might want to use another format */
      gst_element_post_message (GST_ELEMENT (src),
          gst_message_new_segment_done (GST_OBJECT (src),
              GST_FORMAT_BYTES, src->segment.stop));
    } else {
      gst_pad_push_event (pad, gst_event_new_eos ());
    }

    gst_object_unref (src);
    return;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (src, "pausing task, reason %s", reason);
    gst_pad_pause_task (pad);
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (src, STREAM, FAILED,
          (_("Internal data flow error.")),
          ("streaming task paused, reason %s", reason));
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
    gst_object_unref (src);
    return;
  }
error:
  {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        (_("Internal data flow error.")), ("element returned NULL buffer"));
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new_eos ());

    gst_object_unref (src);
    return;
  }
}

/* this will always be called between start() and stop(). So you can rely on
   resources allocated by start() and freed from stop(). This needs to be added
   to the docs at some point. */
static gboolean
gst_base_src_unlock (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result = TRUE;

  GST_DEBUG ("unlock");
  /* unblock whatever the subclass is doing */
  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->unlock)
    result = bclass->unlock (basesrc);

  GST_DEBUG ("unschedule clock");
  /* and unblock the clock as well, if any */
  GST_OBJECT_LOCK (basesrc);
  if (basesrc->clock_id) {
    gst_clock_id_unschedule (basesrc->clock_id);
  }
  GST_OBJECT_UNLOCK (basesrc);

  GST_DEBUG ("unlock done");

  return result;
}

static gboolean
gst_base_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstBaseSrcClass *bclass;
  gboolean result = FALSE;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->get_size)
    result = bclass->get_size (basesrc, size);

  if (result)
    basesrc->size = *size;

  return result;
}

static gboolean
gst_base_src_is_seekable (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);

  /* check if we can seek */
  if (bclass->is_seekable)
    basesrc->seekable = bclass->is_seekable (basesrc);
  else
    basesrc->seekable = FALSE;

  GST_DEBUG_OBJECT (basesrc, "is seekable: %d", basesrc->seekable);

  return basesrc->seekable;
}

/* default negotiation code */
static gboolean
gst_base_src_default_negotiate (GstBaseSrc * basesrc)
{
  GstCaps *thiscaps;
  GstCaps *caps = NULL;
  GstCaps *peercaps = NULL;
  gboolean result = FALSE;

  /* first see what is possible on our source pad */
  thiscaps = gst_pad_get_caps (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG_OBJECT (basesrc, "caps of src: %" GST_PTR_FORMAT, thiscaps);
  /* nothing or anything is allowed, we're done */
  if (thiscaps == NULL || gst_caps_is_any (thiscaps))
    goto no_nego_needed;

  /* get the peer caps */
  peercaps = gst_pad_peer_get_caps (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG_OBJECT (basesrc, "caps of peer: %" GST_PTR_FORMAT, peercaps);
  if (peercaps) {
    GstCaps *icaps;

    /* get intersection */
    icaps = gst_caps_intersect (thiscaps, peercaps);
    GST_DEBUG_OBJECT (basesrc, "intersect: %" GST_PTR_FORMAT, icaps);
    gst_caps_unref (thiscaps);
    gst_caps_unref (peercaps);
    if (icaps) {
      /* take first (and best) possibility */
      caps = gst_caps_copy_nth (icaps, 0);
      gst_caps_unref (icaps);
    }
  } else {
    /* no peer, work with our own caps then */
    caps = thiscaps;
  }
  if (caps) {
    caps = gst_caps_make_writable (caps);
    gst_caps_truncate (caps);

    /* now fixate */
    if (!gst_caps_is_empty (caps)) {
      gst_pad_fixate_caps (GST_BASE_SRC_PAD (basesrc), caps);
      GST_DEBUG_OBJECT (basesrc, "fixated to: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_any (caps)) {
        /* hmm, still anything, so element can do anything and
         * nego is not needed */
        gst_caps_unref (caps);
        result = TRUE;
      } else if (gst_caps_is_fixed (caps)) {
        /* yay, fixed caps, use those then */
        gst_pad_set_caps (GST_BASE_SRC_PAD (basesrc), caps);
        gst_caps_unref (caps);
        result = TRUE;
      }
    }
  }
  return result;

no_nego_needed:
  {
    GST_DEBUG_OBJECT (basesrc, "no negotiation needed");
    if (thiscaps)
      gst_caps_unref (thiscaps);
    return TRUE;
  }
}

static gboolean
gst_base_src_negotiate (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result = TRUE;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);

  if (bclass->negotiate)
    result = bclass->negotiate (basesrc);

  return result;
}

static gboolean
gst_base_src_start (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result;

  if (GST_OBJECT_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED))
    return TRUE;

  GST_DEBUG_OBJECT (basesrc, "starting source");

  basesrc->num_buffers_left = basesrc->num_buffers;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->start)
    result = bclass->start (basesrc);
  else
    result = TRUE;

  if (!result)
    goto could_not_start;

  GST_OBJECT_FLAG_SET (basesrc, GST_BASE_SRC_STARTED);

  /* figure out the size */
  if (bclass->get_size) {
    result = bclass->get_size (basesrc, &basesrc->size);
    if (result == FALSE)
      basesrc->size = -1;
  } else {
    result = FALSE;
    basesrc->size = -1;
  }

  GST_DEBUG ("size %d %lld", result, basesrc->size);

  /* check if we can seek, updates ->seekable */
  gst_base_src_is_seekable (basesrc);

  basesrc->need_newsegment = TRUE;

  /* run typefind */
#if 0
  if (basesrc->seekable) {
    GstCaps *caps;

    caps = gst_type_find_helper (basesrc->srcpad, basesrc->size);
    gst_pad_set_caps (basesrc->srcpad, caps);
    gst_caps_unref (caps);
  }
#endif

  if (!gst_base_src_negotiate (basesrc))
    goto could_not_negotiate;

  return TRUE;

  /* ERROR */
could_not_start:
  {
    GST_DEBUG_OBJECT (basesrc, "could not start");
    return FALSE;
  }
could_not_negotiate:
  {
    GST_DEBUG_OBJECT (basesrc, "could not negotiate, stopping");
    GST_ELEMENT_ERROR (basesrc, STREAM, FORMAT,
        ("Could not connect source to pipeline"),
        ("Check your filtered caps, if any"));
    gst_base_src_stop (basesrc);
    return FALSE;
  }
}

static gboolean
gst_base_src_stop (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result = TRUE;

  if (!GST_OBJECT_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED))
    return TRUE;

  GST_DEBUG_OBJECT (basesrc, "stopping source");

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->stop)
    result = bclass->stop (basesrc);

  if (result)
    GST_OBJECT_FLAG_UNSET (basesrc, GST_BASE_SRC_STARTED);

  return result;
}

static gboolean
gst_base_src_deactivate (GstBaseSrc * basesrc, GstPad * pad)
{
  gboolean result;

  GST_LIVE_LOCK (basesrc);
  basesrc->live_running = TRUE;
  GST_LIVE_SIGNAL (basesrc);
  GST_LIVE_UNLOCK (basesrc);

  /* step 1, unblock clock sync (if any) */
  result = gst_base_src_unlock (basesrc);

  /* step 2, make sure streaming finishes */
  result &= gst_pad_stop_task (pad);

  return result;
}

static gboolean
gst_base_src_activate_push (GstPad * pad, gboolean active)
{
  GstBaseSrc *basesrc;
  gboolean res;

  basesrc = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  /* prepare subclass first */
  if (active) {
    GST_DEBUG_OBJECT (basesrc, "Activating in push mode");

    if (!basesrc->can_activate_push)
      goto no_push_activation;

    if (!gst_base_src_start (basesrc))
      goto error_start;

    res = gst_pad_start_task (pad, (GstTaskFunction) gst_base_src_loop, pad);
  } else {
    GST_DEBUG_OBJECT (basesrc, "Deactivating in push mode");
    res = gst_base_src_deactivate (basesrc, pad);
  }
  return res;

  /* ERRORS */
no_push_activation:
  {
    GST_DEBUG_OBJECT (basesrc, "Subclass disabled push-mode activation");
    return FALSE;
  }
error_start:
  {
    gst_base_src_stop (basesrc);
    GST_DEBUG_OBJECT (basesrc, "Failed to start in push mode");
    return FALSE;
  }
}

static gboolean
gst_base_src_activate_pull (GstPad * pad, gboolean active)
{
  GstBaseSrc *basesrc;

  basesrc = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  /* prepare subclass first */
  if (active) {
    GST_DEBUG_OBJECT (basesrc, "Activating in pull mode");
    if (!gst_base_src_start (basesrc))
      goto error_start;

    if (!basesrc->seekable) {
      gst_base_src_stop (basesrc);
      return FALSE;
    }

    return TRUE;
  } else {
    GST_DEBUG_OBJECT (basesrc, "Deactivating in pull mode");

    if (!gst_base_src_stop (basesrc))
      goto error_stop;

    return gst_base_src_deactivate (basesrc, pad);
  }

error_start:
  {
    gst_base_src_stop (basesrc);
    GST_DEBUG_OBJECT (basesrc, "Failed to start in pull mode");
    return FALSE;
  }
error_stop:
  {
    GST_DEBUG_OBJECT (basesrc, "Failed to stop in pull mode");
    return FALSE;
  }
}

static GstStateChangeReturn
gst_base_src_change_state (GstElement * element, GstStateChange transition)
{
  GstBaseSrc *basesrc;
  GstStateChangeReturn result;
  gboolean no_preroll = FALSE;

  basesrc = GST_BASE_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        no_preroll = TRUE;
        basesrc->live_running = FALSE;
      }
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        basesrc->live_running = TRUE;
        GST_LIVE_SIGNAL (element);
      }
      GST_LIVE_UNLOCK (element);
      break;
    default:
      break;
  }

  if ((result =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* we always run from start to end when in READY, after putting
       * the element to READY a seek can be done on the element to
       * configure the segment when going to PAUSED. */
      gst_segment_init (&basesrc->segment, GST_FORMAT_BYTES);
      basesrc->offset = 0;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        no_preroll = TRUE;
        basesrc->live_running = FALSE;
      }
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_base_src_stop (basesrc))
        goto error_stop;
      /* we always run from start to end when in READY */
      gst_segment_init (&basesrc->segment, GST_FORMAT_BYTES);
      basesrc->offset = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (no_preroll && result == GST_STATE_CHANGE_SUCCESS)
    result = GST_STATE_CHANGE_NO_PREROLL;

  return result;

  /* ERRORS */
failure:
  {
    GST_DEBUG_OBJECT (basesrc, "parent failed state change");
    gst_base_src_stop (basesrc);
    return result;
  }
error_stop:
  {
    GST_DEBUG_OBJECT (basesrc, "Failed to stop");
    return GST_STATE_CHANGE_FAILURE;
  }
}
