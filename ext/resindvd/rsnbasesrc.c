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

/*
 *
 *   This is a temporary copy of GstBaseSrc/GstPushSrc for the resin
 *   DVD components, to work around a deadlock with source elements that
 *   send seeks to themselves. 
 *
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "rsnbasesrc.h"
#include <gst/base/gsttypefindhelper.h>
#include <gst/gstmarshal.h>

#include <gst/gst-i18n-plugin.h>


GST_DEBUG_CATEGORY_STATIC (rsn_base_src_debug);
#define GST_CAT_DEFAULT rsn_base_src_debug

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

#define DEFAULT_BLOCKSIZE       4096
#define DEFAULT_NUM_BUFFERS     -1
#define DEFAULT_TYPEFIND	FALSE
#define DEFAULT_DO_TIMESTAMP	FALSE

enum
{
  PROP_0,
  PROP_BLOCKSIZE,
  PROP_NUM_BUFFERS,
  PROP_TYPEFIND,
  PROP_DO_TIMESTAMP
};

#define GST_BASE_SRC_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), RSN_TYPE_BASE_SRC, RsnBaseSrcPrivate))

struct _RsnBaseSrcPrivate
{
  gboolean last_sent_eos;       /* last thing we did was send an EOS (we set this
                                 * to avoid the sending of two EOS in some cases) */
  gboolean discont;

  /* two segments to be sent in the streaming thread with STREAM_LOCK */
  GstEvent *close_segment;
  GstEvent *start_segment;

  /* startup latency is the time it takes between going to PLAYING and producing
   * the first BUFFER with running_time 0. This value is included in the latency
   * reporting. */
  GstClockTime latency;
  /* timestamp offset, this is the offset add to the values of gst_times for
   * pseudo live sources */
  GstClockTimeDiff ts_offset;

  gboolean do_timestamp;
};

static GstElementClass *parent_class = NULL;

static void rsn_base_src_base_init (gpointer g_class);
static void rsn_base_src_class_init (RsnBaseSrcClass * klass);
static void rsn_base_src_init (RsnBaseSrc * src, gpointer g_class);
static void rsn_base_src_finalize (GObject * object);


GType
rsn_base_src_get_type (void)
{
  static GType base_src_type = 0;

  if (G_UNLIKELY (base_src_type == 0)) {
    static const GTypeInfo base_src_info = {
      sizeof (RsnBaseSrcClass),
      (GBaseInitFunc) rsn_base_src_base_init,
      NULL,
      (GClassInitFunc) rsn_base_src_class_init,
      NULL,
      NULL,
      sizeof (RsnBaseSrc),
      0,
      (GInstanceInitFunc) rsn_base_src_init,
    };

    base_src_type = g_type_register_static (GST_TYPE_ELEMENT,
        "RsnBaseSrc", &base_src_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_src_type;
}
static GstCaps *rsn_base_src_getcaps (GstPad * pad);
static gboolean rsn_base_src_setcaps (GstPad * pad, GstCaps * caps);
static void rsn_base_src_fixate (GstPad * pad, GstCaps * caps);

static gboolean rsn_base_src_activate_push (GstPad * pad, gboolean active);
static gboolean rsn_base_src_activate_pull (GstPad * pad, gboolean active);
static void rsn_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void rsn_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean rsn_base_src_event_handler (GstPad * pad, GstEvent * event);
static gboolean rsn_base_src_send_event (GstElement * elem, GstEvent * event);
static gboolean rsn_base_src_default_event (RsnBaseSrc * src, GstEvent * event);
static const GstQueryType *rsn_base_src_get_query_types (GstElement * element);

static gboolean rsn_base_src_query (GstPad * pad, GstQuery * query);

static gboolean rsn_base_src_default_negotiate (RsnBaseSrc * basesrc);
static gboolean rsn_base_src_default_do_seek (RsnBaseSrc * src,
    GstSegment * segment);
static gboolean rsn_base_src_default_query (RsnBaseSrc * src, GstQuery * query);
static gboolean rsn_base_src_default_prepare_seek_segment (RsnBaseSrc * src,
    GstEvent * event, GstSegment * segment);

static gboolean rsn_base_src_unlock (RsnBaseSrc * basesrc);
static gboolean rsn_base_src_unlock_stop (RsnBaseSrc * basesrc);
static gboolean rsn_base_src_start (RsnBaseSrc * basesrc);
static gboolean rsn_base_src_stop (RsnBaseSrc * basesrc);

static GstStateChangeReturn rsn_base_src_change_state (GstElement * element,
    GstStateChange transition);

static void rsn_base_src_loop (GstPad * pad);
static gboolean rsn_base_src_pad_check_get_range (GstPad * pad);
static gboolean rsn_base_src_default_check_get_range (RsnBaseSrc * bsrc);
static GstFlowReturn rsn_base_src_pad_get_range (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buf);
static GstFlowReturn rsn_base_src_get_range (RsnBaseSrc * src, guint64 offset,
    guint length, GstBuffer ** buf);

static void
rsn_base_src_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (rsn_base_src_debug, "basesrc", 0, "basesrc element");
}

static void
rsn_base_src_class_init (RsnBaseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (RsnBaseSrcPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (rsn_base_src_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (rsn_base_src_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (rsn_base_src_get_property);

  g_object_class_install_property (gobject_class, PROP_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to read per buffer (0 = default)", 0, G_MAXULONG,
          DEFAULT_BLOCKSIZE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_NUM_BUFFERS,
      g_param_spec_int ("num-buffers", "num-buffers",
          "Number of buffers to output before sending EOS", -1, G_MAXINT,
          DEFAULT_NUM_BUFFERS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_TYPEFIND,
      g_param_spec_boolean ("typefind", "Typefind",
          "Run typefind before negotiating", DEFAULT_TYPEFIND,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DO_TIMESTAMP,
      g_param_spec_boolean ("do-timestamp", "Do timestamp",
          "Apply current stream time to buffers", DEFAULT_DO_TIMESTAMP,
          G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (rsn_base_src_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (rsn_base_src_send_event);
  gstelement_class->get_query_types =
      GST_DEBUG_FUNCPTR (rsn_base_src_get_query_types);

  klass->negotiate = GST_DEBUG_FUNCPTR (rsn_base_src_default_negotiate);
  klass->event = GST_DEBUG_FUNCPTR (rsn_base_src_default_event);
  klass->do_seek = GST_DEBUG_FUNCPTR (rsn_base_src_default_do_seek);
  klass->query = GST_DEBUG_FUNCPTR (rsn_base_src_default_query);
  klass->check_get_range =
      GST_DEBUG_FUNCPTR (rsn_base_src_default_check_get_range);
  klass->prepare_seek_segment =
      GST_DEBUG_FUNCPTR (rsn_base_src_default_prepare_seek_segment);
}

static void
rsn_base_src_init (RsnBaseSrc * basesrc, gpointer g_class)
{
  GstPad *pad;
  GstPadTemplate *pad_template;

  basesrc->priv = GST_BASE_SRC_GET_PRIVATE (basesrc);

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
      GST_DEBUG_FUNCPTR (rsn_base_src_activate_push));
  gst_pad_set_activatepull_function (pad,
      GST_DEBUG_FUNCPTR (rsn_base_src_activate_pull));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (rsn_base_src_event_handler));
  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (rsn_base_src_query));
  gst_pad_set_checkgetrange_function (pad,
      GST_DEBUG_FUNCPTR (rsn_base_src_pad_check_get_range));
  gst_pad_set_getrange_function (pad,
      GST_DEBUG_FUNCPTR (rsn_base_src_pad_get_range));
  gst_pad_set_getcaps_function (pad, GST_DEBUG_FUNCPTR (rsn_base_src_getcaps));
  gst_pad_set_setcaps_function (pad, GST_DEBUG_FUNCPTR (rsn_base_src_setcaps));
  gst_pad_set_fixatecaps_function (pad,
      GST_DEBUG_FUNCPTR (rsn_base_src_fixate));

  /* hold pointer to pad */
  basesrc->srcpad = pad;
  GST_DEBUG_OBJECT (basesrc, "adding src pad");
  gst_element_add_pad (GST_ELEMENT (basesrc), pad);

  basesrc->blocksize = DEFAULT_BLOCKSIZE;
  basesrc->clock_id = NULL;
  /* we operate in BYTES by default */
  rsn_base_src_set_format (basesrc, GST_FORMAT_BYTES);
  basesrc->data.ABI.typefind = DEFAULT_TYPEFIND;
  basesrc->priv->do_timestamp = DEFAULT_DO_TIMESTAMP;

  GST_OBJECT_FLAG_UNSET (basesrc, GST_BASE_SRC_STARTED);

  GST_DEBUG_OBJECT (basesrc, "init done");
}

static void
rsn_base_src_finalize (GObject * object)
{
  RsnBaseSrc *basesrc;
  GstEvent **event_p;

  basesrc = GST_BASE_SRC (object);

  g_mutex_free (basesrc->live_lock);
  g_cond_free (basesrc->live_cond);

  event_p = &basesrc->data.ABI.pending_seek;
  gst_event_replace ((GstEvent **) event_p, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * rsn_base_src_wait_playing:
 * @src: the src
 *
 * If the #RsnBaseSrcClass::create method performs its own synchronisation against
 * the clock it must unblock when going from PLAYING to the PAUSED state and call
 * this method before continuing to produce the remaining data.
 *
 * This function will block until a state change to PLAYING happens (in which
 * case this function returns #GST_FLOW_OK) or the processing must be stopped due
 * to a state change to READY or a FLUSH event (in which case this function
 * returns #GST_FLOW_WRONG_STATE).
 *
 * Since: 0.10.12
 *
 * Returns: #GST_FLOW_OK if @src is PLAYING and processing can
 * continue. Any other return value should be returned from the create vmethod.
 */
GstFlowReturn
rsn_base_src_wait_playing (RsnBaseSrc * src)
{
  /* block until the state changes, or we get a flush, or something */
  GST_LIVE_LOCK (src);
  if (src->is_live) {
    while (G_UNLIKELY (!src->live_running)) {
      GST_DEBUG ("live source signal waiting");
      GST_LIVE_SIGNAL (src);
      GST_DEBUG ("live source waiting for running state");
      GST_LIVE_WAIT (src);
      GST_DEBUG ("live source unlocked");
    }
    /* FIXME, use another variable to signal stopping so that we don't
     * have to grab another lock. */
    GST_OBJECT_LOCK (src->srcpad);
    if (G_UNLIKELY (GST_PAD_IS_FLUSHING (src->srcpad)))
      goto flushing;
    GST_OBJECT_UNLOCK (src->srcpad);
  }
  GST_LIVE_UNLOCK (src);

  return GST_FLOW_OK;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (src, "pad is flushing");
    GST_OBJECT_UNLOCK (src->srcpad);
    GST_LIVE_UNLOCK (src);
    return GST_FLOW_WRONG_STATE;
  }
}

/**
 * rsn_base_src_set_live:
 * @src: base source instance
 * @live: new live-mode
 *
 * If the element listens to a live source, @live should
 * be set to %TRUE. 
 *
 * A live source will not produce data in the PAUSED state and
 * will therefore not be able to participate in the PREROLL phase
 * of a pipeline. To signal this fact to the application and the 
 * pipeline, the state change return value of the live source will
 * be GST_STATE_CHANGE_NO_PREROLL.
 */
void
rsn_base_src_set_live (RsnBaseSrc * src, gboolean live)
{
  GST_LIVE_LOCK (src);
  src->is_live = live;
  GST_LIVE_UNLOCK (src);
}

/**
 * rsn_base_src_is_live:
 * @src: base source instance
 *
 * Check if an element is in live mode.
 *
 * Returns: %TRUE if element is in live mode.
 */
gboolean
rsn_base_src_is_live (RsnBaseSrc * src)
{
  gboolean result;

  GST_LIVE_LOCK (src);
  result = src->is_live;
  GST_LIVE_UNLOCK (src);

  return result;
}

/**
 * rsn_base_src_set_format:
 * @src: base source instance
 * @format: the format to use
 *
 * Sets the default format of the source. This will be the format used
 * for sending NEW_SEGMENT events and for performing seeks.
 *
 * If a format of GST_FORMAT_BYTES is set, the element will be able to
 * operate in pull mode if the #RsnBaseSrc::is_seekable returns TRUE.
 *
 * @Since: 0.10.1
 */
void
rsn_base_src_set_format (RsnBaseSrc * src, GstFormat format)
{
  gst_segment_init (&src->segment, format);
}

/**
 * rsn_base_src_query_latency:
 * @src: the source
 * @live: if the source is live
 * @min_latency: the min latency of the source
 * @max_latency: the max latency of the source
 *
 * Query the source for the latency parameters. @live will be TRUE when @src is
 * configured as a live source. @min_latency will be set to the difference
 * between the running time and the timestamp of the first buffer.
 * @max_latency is always the undefined value of -1.
 *
 * This function is mostly used by subclasses. 
 *
 * Returns: TRUE if the query succeeded.
 *
 * Since: 0.10.13
 */
gboolean
rsn_base_src_query_latency (RsnBaseSrc * src, gboolean * live,
    GstClockTime * min_latency, GstClockTime * max_latency)
{
  GstClockTime min;

  GST_LIVE_LOCK (src);
  if (live)
    *live = src->is_live;

  /* if we have a startup latency, report this one, else report 0. Subclasses
   * are supposed to override the query function if they want something
   * else. */
  if (src->priv->latency != -1)
    min = src->priv->latency;
  else
    min = 0;

  if (min_latency)
    *min_latency = min;
  if (max_latency)
    *max_latency = -1;

  GST_LOG_OBJECT (src, "latency: live %d, min %" GST_TIME_FORMAT
      ", max %" GST_TIME_FORMAT, src->is_live, GST_TIME_ARGS (min),
      GST_TIME_ARGS (-1));
  GST_LIVE_UNLOCK (src);

  return TRUE;
}

/**
 * rsn_base_src_set_do_timestamp:
 * @src: the source
 * @timestamp: enable or disable timestamping
 *
 * Configure @src to automatically timestamp outgoing buffers based on the
 * current running_time of the pipeline. This property is mostly useful for live
 * sources.
 *
 * Since: 0.10.15
 */
void
rsn_base_src_set_do_timestamp (RsnBaseSrc * src, gboolean timestamp)
{
  GST_OBJECT_LOCK (src);
  src->priv->do_timestamp = timestamp;
  GST_OBJECT_UNLOCK (src);
}

/**
 * rsn_base_src_get_do_timestamp:
 * @src: the source
 *
 * Query if @src timestamps outgoing buffers based on the current running_time.
 *
 * Returns: %TRUE if the base class will automatically timestamp outgoing buffers.
 *
 * Since: 0.10.15
 */
gboolean
rsn_base_src_get_do_timestamp (RsnBaseSrc * src)
{
  gboolean res;

  GST_OBJECT_LOCK (src);
  res = src->priv->do_timestamp;
  GST_OBJECT_UNLOCK (src);

  return res;
}

static gboolean
rsn_base_src_setcaps (GstPad * pad, GstCaps * caps)
{
  RsnBaseSrcClass *bclass;
  RsnBaseSrc *bsrc;
  gboolean res = TRUE;

  bsrc = GST_BASE_SRC (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SRC_GET_CLASS (bsrc);

  if (bclass->set_caps)
    res = bclass->set_caps (bsrc, caps);

  return res;
}

static GstCaps *
rsn_base_src_getcaps (GstPad * pad)
{
  RsnBaseSrcClass *bclass;
  RsnBaseSrc *bsrc;
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

static void
rsn_base_src_fixate (GstPad * pad, GstCaps * caps)
{
  RsnBaseSrcClass *bclass;
  RsnBaseSrc *bsrc;

  bsrc = GST_BASE_SRC (gst_pad_get_parent (pad));
  bclass = GST_BASE_SRC_GET_CLASS (bsrc);

  if (bclass->fixate)
    bclass->fixate (bsrc, caps);

  gst_object_unref (bsrc);
}

static gboolean
rsn_base_src_default_query (RsnBaseSrc * src, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_PERCENT:
        {
          gint64 percent;
          gint64 position;
          gint64 duration;

          position = src->segment.last_stop;
          duration = src->segment.duration;

          if (position != -1 && duration != -1) {
            if (position < duration)
              percent = gst_util_uint64_scale (GST_FORMAT_PERCENT_MAX, position,
                  duration);
            else
              percent = GST_FORMAT_PERCENT_MAX;
          } else
            percent = -1;

          gst_query_set_position (query, GST_FORMAT_PERCENT, percent);
          res = TRUE;
          break;
        }
        default:
        {
          gint64 position;

          position = src->segment.last_stop;

          if (position != -1) {
            /* convert to requested format */
            res =
                gst_pad_query_convert (src->srcpad, src->segment.format,
                position, &format, &position);
          } else
            res = TRUE;

          gst_query_set_position (query, format, position);
          break;
        }
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      GST_DEBUG_OBJECT (src, "duration query in format %s",
          gst_format_get_name (format));
      switch (format) {
        case GST_FORMAT_PERCENT:
          gst_query_set_duration (query, GST_FORMAT_PERCENT,
              GST_FORMAT_PERCENT_MAX);
          res = TRUE;
          break;
        default:
        {
          gint64 duration;

          duration = src->segment.duration;

          if (duration != -1) {
            /* convert to requested format */
            res =
                gst_pad_query_convert (src->srcpad, src->segment.format,
                duration, &format, &duration);
          } else {
            res = TRUE;
          }
          gst_query_set_duration (query, format, duration);
          break;
        }
      }
      break;
    }

    case GST_QUERY_SEEKING:
    {
      gst_query_set_seeking (query, src->segment.format,
          src->seekable, 0, src->segment.duration);
      res = TRUE;
      break;
    }
    case GST_QUERY_SEGMENT:
    {
      gint64 start, stop;

      /* no end segment configured, current duration then */
      if ((stop = src->segment.stop) == -1)
        stop = src->segment.duration;
      start = src->segment.start;

      /* adjust to stream time */
      if (src->segment.time != -1) {
        start -= src->segment.time;
        if (stop != -1)
          stop -= src->segment.time;
      }
      gst_query_set_segment (query, src->segment.rate, src->segment.format,
          start, stop);
      res = TRUE;
      break;
    }

    case GST_QUERY_FORMATS:
    {
      gst_query_set_formats (query, 3, GST_FORMAT_DEFAULT,
          GST_FORMAT_BYTES, GST_FORMAT_PERCENT);
      res = TRUE;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);

      /* we can only convert between equal formats... */
      if (src_fmt == dest_fmt) {
        dest_val = src_val;
        res = TRUE;
      } else
        res = FALSE;

      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;

      /* Subclasses should override and implement something usefull */
      res = rsn_base_src_query_latency (src, &live, &min, &max);

      GST_LOG_OBJECT (src, "report latency: live %d, min %" GST_TIME_FORMAT
          ", max %" GST_TIME_FORMAT, live, GST_TIME_ARGS (min),
          GST_TIME_ARGS (max));

      gst_query_set_latency (query, live, min, max);
      break;
    }
    case GST_QUERY_JITTER:
    case GST_QUERY_RATE:
    default:
      res = FALSE;
      break;
  }
  GST_DEBUG_OBJECT (src, "query %s returns %d", GST_QUERY_TYPE_NAME (query),
      res);
  return res;
}

static gboolean
rsn_base_src_query (GstPad * pad, GstQuery * query)
{
  RsnBaseSrc *src;
  RsnBaseSrcClass *bclass;
  gboolean result = FALSE;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->query)
    result = bclass->query (src, query);
  else
    result = gst_pad_query_default (pad, query);

  gst_object_unref (src);

  return result;
}

static gboolean
rsn_base_src_default_do_seek (RsnBaseSrc * src, GstSegment * segment)
{
  gboolean res = TRUE;

  /* update our offset if the start/stop position was updated */
  if (segment->format == GST_FORMAT_BYTES) {
    segment->last_stop = segment->start;
    segment->time = segment->start;
  } else if (segment->start == 0) {
    /* seek to start, we can implement a default for this. */
    segment->last_stop = 0;
    segment->time = 0;
    res = TRUE;
  } else
    res = FALSE;

  return res;
}

static gboolean
rsn_base_src_do_seek (RsnBaseSrc * src, GstSegment * segment)
{
  RsnBaseSrcClass *bclass;
  gboolean result = FALSE;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->do_seek)
    result = bclass->do_seek (src, segment);

  return result;
}

#define SEEK_TYPE_IS_RELATIVE(t) (((t) != GST_SEEK_TYPE_NONE) && ((t) != GST_SEEK_TYPE_SET))

static gboolean
rsn_base_src_default_prepare_seek_segment (RsnBaseSrc * src, GstEvent * event,
    GstSegment * segment)
{
  /* By default, we try one of 2 things:
   *   - For absolute seek positions, convert the requested position to our 
   *     configured processing format and place it in the output segment \
   *   - For relative seek positions, convert our current (input) values to the
   *     seek format, adjust by the relative seek offset and then convert back to
   *     the processing format
   */
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  GstSeekFlags flags;
  GstFormat seek_format, dest_format;
  gdouble rate;
  gboolean update;
  gboolean res = TRUE;

  gst_event_parse_seek (event, &rate, &seek_format, &flags,
      &cur_type, &cur, &stop_type, &stop);
  dest_format = segment->format;

  if (seek_format == dest_format) {
    gst_segment_set_seek (segment, rate, seek_format, flags,
        cur_type, cur, stop_type, stop, &update);
    return TRUE;
  }

  if (cur_type != GST_SEEK_TYPE_NONE) {
    /* FIXME: Handle seek_cur & seek_end by converting the input segment vals */
    res =
        gst_pad_query_convert (src->srcpad, seek_format, cur, &dest_format,
        &cur);
    cur_type = GST_SEEK_TYPE_SET;
  }

  if (res && stop_type != GST_SEEK_TYPE_NONE) {
    /* FIXME: Handle seek_cur & seek_end by converting the input segment vals */
    res =
        gst_pad_query_convert (src->srcpad, seek_format, stop, &dest_format,
        &stop);
    stop_type = GST_SEEK_TYPE_SET;
  }

  /* And finally, configure our output segment in the desired format */
  gst_segment_set_seek (segment, rate, dest_format, flags, cur_type, cur,
      stop_type, stop, &update);

  if (!res)
    goto no_format;

  return res;

no_format:
  {
    GST_DEBUG_OBJECT (src, "undefined format given, seek aborted.");
    return FALSE;
  }
}

static gboolean
rsn_base_src_prepare_seek_segment (RsnBaseSrc * src, GstEvent * event,
    GstSegment * seeksegment)
{
  RsnBaseSrcClass *bclass;
  gboolean result = FALSE;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->prepare_seek_segment)
    result = bclass->prepare_seek_segment (src, event, seeksegment);

  return result;
}

/* this code implements the seeking. It is a good example
 * handling all cases.
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
 * In the case of a non-flushing seek we need to make sure that the
 * data we output after the seek is continuous with the previous data,
 * this is because a non-flushing seek does not reset the stream-time
 * to 0. We do this by closing the currently running segment, ie. sending
 * a new_segment event with the stop position set to the last processed 
 * position.
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
/* FIXME (0.11), we have the unlock gboolean here because most current 
 * implementations (fdsrc, -base/gst/tcp/, ...) unconditionally unlock, even when
 * the streaming thread isn't running, resulting in bogus unlocks later when it 
 * starts. This is fixed by adding unlock_stop, but we should still avoid unlocking
 * unnecessarily for backwards compatibility. Ergo, the unlock variable stays
 * until 0.11
 */
static gboolean
rsn_base_src_perform_seek (RsnBaseSrc * src, GstEvent * event, gboolean unlock)
{
  gboolean res = TRUE;
  gdouble rate;
  GstFormat seek_format, dest_format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean update;
  gboolean relative_seek = FALSE;
  gboolean seekseg_configured = FALSE;
  GstSegment seeksegment;

  GST_DEBUG_OBJECT (src, "doing seek");

  dest_format = src->segment.format;

  if (event) {
    gst_event_parse_seek (event, &rate, &seek_format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    relative_seek = SEEK_TYPE_IS_RELATIVE (cur_type) ||
        SEEK_TYPE_IS_RELATIVE (stop_type);

    if (dest_format != seek_format && !relative_seek) {
      /* If we have an ABSOLUTE position (SEEK_SET only), we can convert it
       * here before taking the stream lock, otherwise we must convert it later,
       * once we have the stream lock and can read the current position */
      gst_segment_init (&seeksegment, dest_format);

      if (!rsn_base_src_prepare_seek_segment (src, event, &seeksegment))
        goto prepare_failed;

      seekseg_configured = TRUE;
    }

    flush = flags & GST_SEEK_FLAG_FLUSH;
  } else {
    flush = FALSE;
  }

  /* send flush start */
  if (flush)
    gst_pad_push_event (src->srcpad, gst_event_new_flush_start ());
  else
    gst_pad_pause_task (src->srcpad);

  /* unblock streaming thread */
  if (unlock)
    rsn_base_src_unlock (src);

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or our streaming thread stopped 
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (src->srcpad);

  if (unlock)
    rsn_base_src_unlock_stop (src);

  /* If we configured the seeksegment above, don't overwrite it now. Otherwise
   * copy the current segment info into the temp segment that we can actually
   * attempt the seek with. We only update the real segment if the seek suceeds. */
  if (!seekseg_configured) {
    memcpy (&seeksegment, &src->segment, sizeof (GstSegment));

    /* now configure the final seek segment */
    if (event) {
      if (src->segment.format != seek_format) {
        /* OK, here's where we give the subclass a chance to convert the relative
         * seek into an absolute one in the processing format. We set up any
         * absolute seek above, before taking the stream lock. */
        if (!rsn_base_src_prepare_seek_segment (src, event, &seeksegment)) {
          GST_DEBUG_OBJECT (src, "Preparing the seek failed after flushing. "
              "Aborting seek");
          res = FALSE;
        }
      } else {
        /* The seek format matches our processing format, no need to ask the
         * the subclass to configure the segment. */
        gst_segment_set_seek (&seeksegment, rate, seek_format, flags,
            cur_type, cur, stop_type, stop, &update);
      }
    }
    /* Else, no seek event passed, so we're just (re)starting the 
       current segment. */
  }

  if (res) {
    GST_DEBUG_OBJECT (src, "segment configured from %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT ", position %" G_GINT64_FORMAT,
        seeksegment.start, seeksegment.stop, seeksegment.last_stop);

    /* do the seek, segment.last_stop contains the new position. */
    res = rsn_base_src_do_seek (src, &seeksegment);
  }

  /* and prepare to continue streaming */
  if (flush) {
    /* send flush stop, peer will accept data and events again. We
     * are not yet providing data as we still have the STREAM_LOCK. */
    gst_pad_push_event (src->srcpad, gst_event_new_flush_stop ());
  } else if (res && src->data.ABI.running) {
    /* we are running the current segment and doing a non-flushing seek, 
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (src, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, src->segment.start, src->segment.last_stop);

    /* queue the segment for sending in the stream thread */
    if (src->priv->close_segment)
      gst_event_unref (src->priv->close_segment);
    src->priv->close_segment =
        gst_event_new_new_segment_full (TRUE,
        src->segment.rate, src->segment.applied_rate, src->segment.format,
        src->segment.start, src->segment.last_stop, src->segment.time);
  }

  /* The subclass must have converted the segment to the processing format 
   * by now */
  if (res && seeksegment.format != dest_format) {
    GST_DEBUG_OBJECT (src, "Subclass failed to prepare a seek segment "
        "in the correct format. Aborting seek.");
    res = FALSE;
  }

  /* if successfull seek, we update our real segment and push
   * out the new segment. */
  if (res) {
    if (flush) {
      memcpy (&src->segment, &seeksegment, sizeof (GstSegment));
    } else {
      gst_segment_set_newsegment_full (&src->segment,
          FALSE, seeksegment.rate, seeksegment.applied_rate,
          seeksegment.format, seeksegment.last_stop,
          seeksegment.stop, seeksegment.time);

      gst_segment_set_last_stop (&src->segment, GST_FORMAT_TIME,
          seeksegment.last_stop);
    }

    if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT (src),
          gst_message_new_segment_start (GST_OBJECT (src),
              src->segment.format, src->segment.last_stop));
    }

    /* for deriving a stop position for the playback segment form the seek
     * segment, we must take the duration when the stop is not set */
    if ((stop = src->segment.stop) == -1)
      stop = src->segment.duration;

    GST_DEBUG_OBJECT (src, "Sending newsegment from %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, src->segment.start, stop);

    /* now replace the old segment so that we send it in the stream thread the
     * next time it is scheduled. */
    if (src->priv->start_segment)
      gst_event_unref (src->priv->start_segment);
    src->priv->start_segment =
        gst_event_new_new_segment_full (FALSE,
        src->segment.rate, src->segment.applied_rate, src->segment.format,
        src->segment.last_stop, stop, src->segment.time);
  }

  src->priv->discont = TRUE;
  src->data.ABI.running = TRUE;
  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (src->srcpad, (GstTaskFunction) rsn_base_src_loop,
      src->srcpad);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (src->srcpad);

  return res;

  /* ERROR */
prepare_failed:
  GST_DEBUG_OBJECT (src, "Preparing the seek failed before flushing. "
      "Aborting seek");
  return FALSE;
}

static const GstQueryType *
rsn_base_src_get_query_types (GstElement * element)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    GST_QUERY_SEEKING,
    GST_QUERY_SEGMENT,
    GST_QUERY_FORMATS,
    GST_QUERY_LATENCY,
    GST_QUERY_JITTER,
    GST_QUERY_RATE,
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}

/* all events send to this element directly. This is mainly done from the
 * application.
 */
static gboolean
rsn_base_src_send_event (GstElement * element, GstEvent * event)
{
  RsnBaseSrc *src;
  gboolean result = FALSE;

  src = GST_BASE_SRC (element);

  switch (GST_EVENT_TYPE (event)) {
      /* bidirectional events */
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
      /* sending random flushes downstream can break stuff,
       * especially sync since all segment info will get flushed */
      break;

      /* downstream serialized events */
    case GST_EVENT_EOS:
      /* FIXME, queue EOS and make sure the task or pull function 
       * perform the EOS actions. */
      break;
    case GST_EVENT_NEWSEGMENT:
      /* sending random NEWSEGMENT downstream can break sync. */
      break;
    case GST_EVENT_TAG:
      /* sending tags could be useful, FIXME insert in dataflow */
      break;
    case GST_EVENT_BUFFERSIZE:
      /* does not seem to make much sense currently */
      break;

      /* upstream events */
    case GST_EVENT_QOS:
      /* elements should override send_event and do something */
      break;
    case GST_EVENT_SEEK:
    {
      gboolean started;

      GST_OBJECT_LOCK (src->srcpad);
      if (GST_PAD_ACTIVATE_MODE (src->srcpad) == GST_ACTIVATE_PULL)
        goto wrong_mode;
      started = GST_PAD_ACTIVATE_MODE (src->srcpad) == GST_ACTIVATE_PUSH;
      GST_OBJECT_UNLOCK (src->srcpad);

      if (started) {
        /* when we are running in push mode, we can execute the
         * seek right now, we need to unlock. */
        result = rsn_base_src_perform_seek (src, event, TRUE);
      } else {
        GstEvent **event_p;

        /* else we store the event and execute the seek when we
         * get activated */
        GST_OBJECT_LOCK (src);
        event_p = &src->data.ABI.pending_seek;
        gst_event_replace ((GstEvent **) event_p, event);
        GST_OBJECT_UNLOCK (src);
        /* assume the seek will work */
        result = TRUE;
      }
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* could make sense for elements that do something with navigation events
       * but then they would need to override the send_event function */
      break;
    case GST_EVENT_LATENCY:
      /* does not seem to make sense currently */
      break;

      /* custom events */
    case GST_EVENT_CUSTOM_UPSTREAM:
      /* override send_event if you want this */
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_BOTH:
      /* FIXME, insert event in the dataflow */
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    case GST_EVENT_CUSTOM_BOTH_OOB:
      /* insert a random custom event into the pipeline */
      GST_DEBUG_OBJECT (src, "pushing custom OOB event downstream");
      result = gst_pad_push_event (src->srcpad, event);
      /* we gave away the ref to the event in the push */
      event = NULL;
      break;
    default:
      break;
  }
done:
  /* if we still have a ref to the event, unref it now */
  if (event)
    gst_event_unref (event);

  return result;

  /* ERRORS */
wrong_mode:
  {
    GST_DEBUG_OBJECT (src, "cannot perform seek when operating in pull mode");
    GST_OBJECT_UNLOCK (src->srcpad);
    result = FALSE;
    goto done;
  }
}

static gboolean
rsn_base_src_default_event (RsnBaseSrc * src, GstEvent * event)
{
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* is normally called when in push mode */
      if (!src->seekable)
        goto not_seekable;

      result = rsn_base_src_perform_seek (src, event, TRUE);
      break;
    case GST_EVENT_FLUSH_START:
      /* cancel any blocking getrange, is normally called
       * when in pull mode. */
      result = rsn_base_src_unlock (src);
      break;
    case GST_EVENT_FLUSH_STOP:
      result = rsn_base_src_unlock_stop (src);
      break;
    default:
      result = TRUE;
      break;
  }
  return result;

  /* ERRORS */
not_seekable:
  {
    GST_DEBUG_OBJECT (src, "is not seekable");
    return FALSE;
  }
}

static gboolean
rsn_base_src_event_handler (GstPad * pad, GstEvent * event)
{
  RsnBaseSrc *src;
  RsnBaseSrcClass *bclass;
  gboolean result = FALSE;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));
  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->event) {
    if (!(result = bclass->event (src, event)))
      goto subclass_failed;
  }

done:
  gst_event_unref (event);
  gst_object_unref (src);

  return result;

  /* ERRORS */
subclass_failed:
  {
    GST_DEBUG_OBJECT (src, "subclass refused event");
    goto done;
  }
}

static void
rsn_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  RsnBaseSrc *src;

  src = GST_BASE_SRC (object);

  switch (prop_id) {
    case PROP_BLOCKSIZE:
      src->blocksize = g_value_get_ulong (value);
      break;
    case PROP_NUM_BUFFERS:
      src->num_buffers = g_value_get_int (value);
      break;
    case PROP_TYPEFIND:
      src->data.ABI.typefind = g_value_get_boolean (value);
      break;
    case PROP_DO_TIMESTAMP:
      src->priv->do_timestamp = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rsn_base_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  RsnBaseSrc *src;

  src = GST_BASE_SRC (object);

  switch (prop_id) {
    case PROP_BLOCKSIZE:
      g_value_set_ulong (value, src->blocksize);
      break;
    case PROP_NUM_BUFFERS:
      g_value_set_int (value, src->num_buffers);
      break;
    case PROP_TYPEFIND:
      g_value_set_boolean (value, src->data.ABI.typefind);
      break;
    case PROP_DO_TIMESTAMP:
      g_value_set_boolean (value, src->priv->do_timestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* with STREAM_LOCK and LOCK */
static GstClockReturn
rsn_base_src_wait (RsnBaseSrc * basesrc, GstClock * clock, GstClockTime time)
{
  GstClockReturn ret;
  GstClockID id;

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

/* perform synchronisation on a buffer. 
 * with STREAM_LOCK.
 */
static GstClockReturn
rsn_base_src_do_sync (RsnBaseSrc * basesrc, GstBuffer * buffer)
{
  GstClockReturn result;
  GstClockTime start, end;
  RsnBaseSrcClass *bclass;
  GstClockTime base_time;
  GstClock *clock;
  GstClockTime now = GST_CLOCK_TIME_NONE, timestamp;
  gboolean do_timestamp, first, pseudo_live;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);

  start = end = -1;
  if (bclass->get_times)
    bclass->get_times (basesrc, buffer, &start, &end);

  /* get buffer timestamp */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* grab the lock to prepare for clocking and calculate the startup 
   * latency. */
  GST_OBJECT_LOCK (basesrc);

  /* if we are asked to sync against the clock we are a pseudo live element */
  pseudo_live = (start != -1 && basesrc->is_live);
  /* check for the first buffer */
  first = (basesrc->priv->latency == -1);

  if (timestamp != -1 && pseudo_live) {
    GstClockTime latency;

    /* we have a timestamp and a sync time, latency is the diff */
    if (timestamp <= start)
      latency = start - timestamp;
    else
      latency = 0;

    if (first) {
      GST_DEBUG_OBJECT (basesrc, "pseudo_live with latency %" GST_TIME_FORMAT,
          GST_TIME_ARGS (latency));
      /* first time we calculate latency, just configure */
      basesrc->priv->latency = latency;
    } else {
      if (basesrc->priv->latency != latency) {
        /* we have a new latency, FIXME post latency message */
        basesrc->priv->latency = latency;
        GST_DEBUG_OBJECT (basesrc, "latency changed to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (latency));
      }
    }
  } else if (first) {
    GST_DEBUG_OBJECT (basesrc, "no latency needed, live %d, sync %d",
        basesrc->is_live, start != -1);
    basesrc->priv->latency = 0;
  }

  /* get clock, if no clock, we can't sync or do timestamps */
  if ((clock = GST_ELEMENT_CLOCK (basesrc)) == NULL)
    goto no_clock;

  base_time = GST_ELEMENT_CAST (basesrc)->base_time;

  do_timestamp = basesrc->priv->do_timestamp;

  /* first buffer, calculate the timestamp offset */
  if (first) {
    GstClockTime running_time;

    now = gst_clock_get_time (clock);
    running_time = now - base_time;

    GST_LOG_OBJECT (basesrc,
        "startup timestamp: %" GST_TIME_FORMAT ", running_time %"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (running_time));

    if (pseudo_live && timestamp != -1) {
      /* live source and we need to sync, add startup latency to all timestamps
       * to get the real running_time. Live sources should always timestamp
       * according to the current running time. */
      basesrc->priv->ts_offset = GST_CLOCK_DIFF (timestamp, running_time);

      GST_LOG_OBJECT (basesrc, "live with sync, ts_offset %" GST_TIME_FORMAT,
          GST_TIME_ARGS (basesrc->priv->ts_offset));
    } else {
      basesrc->priv->ts_offset = 0;
      GST_LOG_OBJECT (basesrc, "no timestamp offset needed");
    }

    if (!GST_CLOCK_TIME_IS_VALID (timestamp)) {
      if (do_timestamp)
        timestamp = running_time;
      else
        timestamp = 0;

      GST_BUFFER_TIMESTAMP (buffer) = timestamp;

      GST_LOG_OBJECT (basesrc, "created timestamp: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
    }

    /* add the timestamp offset we need for sync */
    timestamp += basesrc->priv->ts_offset;
  } else {
    /* not the first buffer, the timestamp is the diff between the clock and
     * base_time */
    if (do_timestamp && !GST_CLOCK_TIME_IS_VALID (timestamp)) {
      now = gst_clock_get_time (clock);

      GST_BUFFER_TIMESTAMP (buffer) = now - base_time;

      GST_LOG_OBJECT (basesrc, "created timestamp: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (now - base_time));
    }
  }

  /* if we don't have a buffer timestamp, we don't sync */
  if (!GST_CLOCK_TIME_IS_VALID (start))
    goto no_sync;

  if (basesrc->is_live && GST_CLOCK_TIME_IS_VALID (timestamp)) {
    /* for pseudo live sources, add our ts_offset to the timestamp */
    GST_BUFFER_TIMESTAMP (buffer) += basesrc->priv->ts_offset;
    start += basesrc->priv->ts_offset;
  }

  GST_LOG_OBJECT (basesrc,
      "waiting for clock, base time %" GST_TIME_FORMAT
      ", stream_start %" GST_TIME_FORMAT,
      GST_TIME_ARGS (base_time), GST_TIME_ARGS (start));

  result = rsn_base_src_wait (basesrc, clock, start + base_time);
  GST_OBJECT_UNLOCK (basesrc);

  GST_LOG_OBJECT (basesrc, "clock entry done: %d", result);

  return result;

  /* special cases */
no_clock:
  {
    GST_DEBUG_OBJECT (basesrc, "we have no clock");
    GST_OBJECT_UNLOCK (basesrc);
    return GST_CLOCK_OK;
  }
no_sync:
  {
    GST_DEBUG_OBJECT (basesrc, "no sync needed");
    GST_OBJECT_UNLOCK (basesrc);
    return GST_CLOCK_OK;
  }
}

static gboolean
rsn_base_src_update_length (RsnBaseSrc * src, guint64 offset, guint * length)
{
  guint64 size, maxsize;
  RsnBaseSrcClass *bclass;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  /* only operate if we are working with bytes */
  if (src->segment.format != GST_FORMAT_BYTES)
    return TRUE;

  /* get total file size */
  size = (guint64) src->segment.duration;

  /* the max amount of bytes to read is the total size or
   * up to the segment.stop if present. */
  if (src->segment.stop != -1)
    maxsize = MIN (size, src->segment.stop);
  else
    maxsize = size;

  GST_DEBUG_OBJECT (src,
      "reading offset %" G_GUINT64_FORMAT ", length %u, size %" G_GINT64_FORMAT
      ", segment.stop %" G_GINT64_FORMAT ", maxsize %" G_GINT64_FORMAT, offset,
      *length, size, src->segment.stop, maxsize);

  /* check size if we have one */
  if (maxsize != -1) {
    /* if we run past the end, check if the file became bigger and 
     * retry. */
    if (G_UNLIKELY (offset + *length >= maxsize)) {
      /* see if length of the file changed */
      if (bclass->get_size)
        if (!bclass->get_size (src, &size))
          size = -1;

      gst_segment_set_duration (&src->segment, GST_FORMAT_BYTES, size);

      /* make sure we don't exceed the configured segment stop
       * if it was set */
      if (src->segment.stop != -1)
        maxsize = MIN (size, src->segment.stop);
      else
        maxsize = size;

      /* if we are at or past the end, EOS */
      if (G_UNLIKELY (offset >= maxsize))
        goto unexpected_length;

      /* else we can clip to the end */
      if (G_UNLIKELY (offset + *length >= maxsize))
        *length = maxsize - offset;

    }
  }

  /* keep track of current position. segment is in bytes, we checked 
   * that above. */
  gst_segment_set_last_stop (&src->segment, GST_FORMAT_BYTES, offset);

  return TRUE;

  /* ERRORS */
unexpected_length:
  {
    return FALSE;
  }
}

static GstFlowReturn
rsn_base_src_get_range (RsnBaseSrc * src, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  RsnBaseSrcClass *bclass;
  GstClockReturn status;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  ret = rsn_base_src_wait_playing (src);
  if (ret != GST_FLOW_OK)
    goto stopped;

  if (G_UNLIKELY (!GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_STARTED)))
    goto not_started;

  if (G_UNLIKELY (!bclass->create))
    goto no_function;

  if (G_UNLIKELY (!rsn_base_src_update_length (src, offset, &length)))
    goto unexpected_length;

  /* normally we don't count buffers */
  if (G_UNLIKELY (src->num_buffers_left >= 0)) {
    if (src->num_buffers_left == 0)
      goto reached_num_buffers;
    else
      src->num_buffers_left--;
  }

  GST_DEBUG_OBJECT (src,
      "calling create offset %" G_GUINT64_FORMAT " length %u, time %"
      G_GINT64_FORMAT, offset, length, src->segment.time);

  ret = bclass->create (src, offset, length, buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto not_ok;

  /* no timestamp set and we are at offset 0, we can timestamp with 0 */
  if (offset == 0 && src->segment.time == 0
      && GST_BUFFER_TIMESTAMP (*buf) == -1)
    GST_BUFFER_TIMESTAMP (*buf) = 0;

  /* now sync before pushing the buffer */
  status = rsn_base_src_do_sync (src, *buf);
  switch (status) {
    case GST_CLOCK_EARLY:
      /* the buffer is too late. We currently don't drop the buffer. */
      GST_DEBUG_OBJECT (src, "buffer too late!, returning anyway");
      break;
    case GST_CLOCK_OK:
      /* buffer synchronised properly */
      GST_DEBUG_OBJECT (src, "buffer ok");
      break;
    case GST_CLOCK_UNSCHEDULED:
      /* this case is triggered when we were waiting for the clock and
       * it got unlocked because we did a state change. We return 
       * WRONG_STATE in this case to stop the dataflow also get rid of the
       * produced buffer. */
      GST_DEBUG_OBJECT (src,
          "clock was unscheduled (%d), returning WRONG_STATE", status);
      gst_buffer_unref (*buf);
      *buf = NULL;
      ret = GST_FLOW_WRONG_STATE;
      break;
    default:
      /* all other result values are unexpected and errors */
      GST_ELEMENT_ERROR (src, CORE, CLOCK,
          (_("Internal clock error.")),
          ("clock returned unexpected return value %d", status));
      gst_buffer_unref (*buf);
      *buf = NULL;
      ret = GST_FLOW_ERROR;
      break;
  }
  return ret;

  /* ERROR */
stopped:
  {
    GST_DEBUG_OBJECT (src, "wait_playing returned %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
  }
not_ok:
  {
    GST_DEBUG_OBJECT (src, "create returned %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
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
        ", size=%" G_GINT64_FORMAT ")", length, offset, src->segment.duration);
    return GST_FLOW_UNEXPECTED;
  }
reached_num_buffers:
  {
    GST_DEBUG_OBJECT (src, "sent all buffers");
    return GST_FLOW_UNEXPECTED;
  }
}

static GstFlowReturn
rsn_base_src_pad_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buf)
{
  RsnBaseSrc *src;
  GstFlowReturn res;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  res = rsn_base_src_get_range (src, offset, length, buf);

  gst_object_unref (src);

  return res;
}

static gboolean
rsn_base_src_default_check_get_range (RsnBaseSrc * src)
{
  gboolean res;

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_BASE_SRC_STARTED)) {
    GST_LOG_OBJECT (src, "doing start/stop to check get_range support");
    if (G_LIKELY (rsn_base_src_start (src)))
      rsn_base_src_stop (src);
  }

  /* we can operate in getrange mode if the native format is bytes
   * and we are seekable, this condition is set in the random_access
   * flag and is set in the _start() method. */
  res = src->random_access;

  return res;
}

static gboolean
rsn_base_src_check_get_range (RsnBaseSrc * src)
{
  RsnBaseSrcClass *bclass;
  gboolean res;

  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->check_get_range == NULL)
    goto no_function;

  res = bclass->check_get_range (src);
  GST_LOG_OBJECT (src, "%s() returned %d",
      GST_DEBUG_FUNCPTR_NAME (bclass->check_get_range), (gint) res);

  return res;

  /* ERRORS */
no_function:
  {
    GST_WARNING_OBJECT (src, "no check_get_range function set");
    return FALSE;
  }
}

static gboolean
rsn_base_src_pad_check_get_range (GstPad * pad)
{
  RsnBaseSrc *src;
  gboolean res;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  res = rsn_base_src_check_get_range (src);

  gst_object_unref (src);

  return res;
}

static void
rsn_base_src_loop (GstPad * pad)
{
  RsnBaseSrc *src;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;
  gint64 position;
  gboolean eos;

  eos = FALSE;

  src = GST_BASE_SRC (gst_pad_get_parent (pad));

  src->priv->last_sent_eos = FALSE;

  /* if we operate in bytes, we can calculate an offset */
  if (src->segment.format == GST_FORMAT_BYTES)
    position = src->segment.last_stop;
  else
    position = -1;

  ret = rsn_base_src_get_range (src, position, src->blocksize, &buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_INFO_OBJECT (src, "pausing after rsn_base_src_get_range() = %s",
        gst_flow_get_name (ret));
    goto pause;
  }
  /* this should not happen */
  if (G_UNLIKELY (buf == NULL))
    goto null_buffer;

  /* push events to close/start our segment before we push the buffer. */
  if (src->priv->close_segment) {
    gst_pad_push_event (pad, src->priv->close_segment);
    src->priv->close_segment = NULL;
  }
  if (src->priv->start_segment) {
    gst_pad_push_event (pad, src->priv->start_segment);
    src->priv->start_segment = NULL;
  }

  /* figure out the new position */
  switch (src->segment.format) {
    case GST_FORMAT_BYTES:
      position += GST_BUFFER_SIZE (buf);
      break;
    case GST_FORMAT_TIME:
    {
      GstClockTime start, duration;

      start = GST_BUFFER_TIMESTAMP (buf);
      duration = GST_BUFFER_DURATION (buf);

      if (GST_CLOCK_TIME_IS_VALID (start))
        position = start;
      else
        position = src->segment.last_stop;

      if (GST_CLOCK_TIME_IS_VALID (duration))
        position += duration;
      break;
    }
    case GST_FORMAT_DEFAULT:
      position = GST_BUFFER_OFFSET_END (buf);
      break;
    default:
      position = -1;
      break;
  }
  if (position != -1) {
    if (src->segment.stop != -1) {
      if (position >= src->segment.stop) {
        eos = TRUE;
        position = src->segment.stop;
      }
    }
    gst_segment_set_last_stop (&src->segment, src->segment.format, position);
  }

  if (G_UNLIKELY (src->priv->discont)) {
    buf = gst_buffer_make_metadata_writable (buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    src->priv->discont = FALSE;
  }

  ret = gst_pad_push (pad, buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_INFO_OBJECT (src, "pausing after gst_pad_push() = %s",
        gst_flow_get_name (ret));
    goto pause;
  }

  if (eos) {
    GST_INFO_OBJECT (src, "pausing after EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto pause;
  }

done:
  gst_object_unref (src);
  return;

  /* special cases */
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (src, "pausing task, reason %s", reason);
    src->data.ABI.running = FALSE;
    gst_pad_pause_task (pad);
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gst_element_post_message (GST_ELEMENT_CAST (src),
              gst_message_new_segment_done (GST_OBJECT_CAST (src),
                  src->segment.format, src->segment.last_stop));
        } else {
          gst_pad_push_event (pad, gst_event_new_eos ());
          src->priv->last_sent_eos = TRUE;
        }
      } else {
        /* for fatal errors we post an error message, post the error
         * first so the app knows about the error first. */
        GST_ELEMENT_ERROR (src, STREAM, FAILED,
            (_("Internal data flow error.")),
            ("streaming task paused, reason %s (%d)", reason, ret));
        gst_pad_push_event (pad, gst_event_new_eos ());
        src->priv->last_sent_eos = TRUE;
      }
    }
    goto done;
  }
null_buffer:
  {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        (_("Internal data flow error.")), ("element returned NULL buffer"));
    /* we finished the segment on error */
    src->data.ABI.running = FALSE;
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new_eos ());
    src->priv->last_sent_eos = TRUE;
    goto done;
  }
}

/* this will always be called between start() and stop(). So you can rely on
 * resources allocated by start() and freed from stop(). This needs to be added
 * to the docs at some point. */
static gboolean
rsn_base_src_unlock (RsnBaseSrc * basesrc)
{
  RsnBaseSrcClass *bclass;
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

/* this will always be called between start() and stop(). So you can rely on
 * resources allocated by start() and freed from stop(). This needs to be added
 * to the docs at some point. */
static gboolean
rsn_base_src_unlock_stop (RsnBaseSrc * basesrc)
{
  RsnBaseSrcClass *bclass;
  gboolean result = TRUE;

  GST_DEBUG_OBJECT (basesrc, "unlock stop");

  /* Finish a previous unblock request, allowing subclasses to flush command
   * queues or whatever they need to do */
  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->unlock_stop)
    result = bclass->unlock_stop (basesrc);

  GST_DEBUG_OBJECT (basesrc, "unlock stop done");

  return result;
}

/* default negotiation code. 
 *
 * Take intersection between src and sink pads, take first
 * caps and fixate. 
 */
static gboolean
rsn_base_src_default_negotiate (RsnBaseSrc * basesrc)
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
      /* take first (and best, since they are sorted) possibility */
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
        result = TRUE;
      } else if (gst_caps_is_fixed (caps)) {
        /* yay, fixed caps, use those then */
        gst_pad_set_caps (GST_BASE_SRC_PAD (basesrc), caps);
        result = TRUE;
      }
    }
    gst_caps_unref (caps);
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
rsn_base_src_negotiate (RsnBaseSrc * basesrc)
{
  RsnBaseSrcClass *bclass;
  gboolean result = TRUE;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);

  if (bclass->negotiate)
    result = bclass->negotiate (basesrc);

  return result;
}

static gboolean
rsn_base_src_start (RsnBaseSrc * basesrc)
{
  RsnBaseSrcClass *bclass;
  gboolean result;
  guint64 size;

  if (GST_OBJECT_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED))
    return TRUE;

  GST_DEBUG_OBJECT (basesrc, "starting source");

  basesrc->num_buffers_left = basesrc->num_buffers;

  gst_segment_init (&basesrc->segment, basesrc->segment.format);
  basesrc->data.ABI.running = FALSE;

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->start)
    result = bclass->start (basesrc);
  else
    result = TRUE;

  if (!result)
    goto could_not_start;

  GST_OBJECT_FLAG_SET (basesrc, GST_BASE_SRC_STARTED);

  /* figure out the size */
  if (basesrc->segment.format == GST_FORMAT_BYTES) {
    if (bclass->get_size) {
      if (!(result = bclass->get_size (basesrc, &size)))
        size = -1;
    } else {
      result = FALSE;
      size = -1;
    }
    GST_DEBUG_OBJECT (basesrc, "setting size %" G_GUINT64_FORMAT, size);
    /* only update the size when operating in bytes, subclass is supposed
     * to set duration in the start method for other formats */
    gst_segment_set_duration (&basesrc->segment, GST_FORMAT_BYTES, size);
  } else {
    size = -1;
  }

  GST_DEBUG_OBJECT (basesrc,
      "format: %d, have size: %d, size: %" G_GUINT64_FORMAT ", duration: %"
      G_GINT64_FORMAT, basesrc->segment.format, result, size,
      basesrc->segment.duration);

  /* check if we can seek */
  if (bclass->is_seekable)
    basesrc->seekable = bclass->is_seekable (basesrc);
  else
    basesrc->seekable = FALSE;

  GST_DEBUG_OBJECT (basesrc, "is seekable: %d", basesrc->seekable);

  /* update for random access flag */
  basesrc->random_access = basesrc->seekable &&
      basesrc->segment.format == GST_FORMAT_BYTES;

  GST_DEBUG_OBJECT (basesrc, "is random_access: %d", basesrc->random_access);

  /* run typefind if we are random_access and the typefinding is enabled. */
  if (basesrc->random_access && basesrc->data.ABI.typefind && size != -1) {
    GstCaps *caps;

    caps = gst_type_find_helper (basesrc->srcpad, size);
    gst_pad_set_caps (basesrc->srcpad, caps);
    gst_caps_unref (caps);
  } else {
    /* use class or default negotiate function */
    if (!rsn_base_src_negotiate (basesrc))
      goto could_not_negotiate;
  }

  return TRUE;

  /* ERROR */
could_not_start:
  {
    GST_DEBUG_OBJECT (basesrc, "could not start");
    /* subclass is supposed to post a message. We don't have to call _stop. */
    return FALSE;
  }
could_not_negotiate:
  {
    GST_DEBUG_OBJECT (basesrc, "could not negotiate, stopping");
    GST_ELEMENT_ERROR (basesrc, STREAM, FORMAT,
        ("Could not negotiate format"), ("Check your filtered caps, if any"));
    /* we must call stop */
    rsn_base_src_stop (basesrc);
    return FALSE;
  }
}

static gboolean
rsn_base_src_stop (RsnBaseSrc * basesrc)
{
  RsnBaseSrcClass *bclass;
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
rsn_base_src_deactivate (RsnBaseSrc * basesrc, GstPad * pad)
{
  gboolean result;

  GST_LIVE_LOCK (basesrc);
  basesrc->live_running = TRUE;
  GST_LIVE_SIGNAL (basesrc);
  GST_LIVE_UNLOCK (basesrc);

  /* step 1, unblock clock sync (if any) */
  result = rsn_base_src_unlock (basesrc);

  /* step 2, make sure streaming finishes */
  result &= gst_pad_stop_task (pad);

  /* step 3, clear the unblock condition */
  result &= rsn_base_src_unlock_stop (basesrc);

  return result;
}

static gboolean
rsn_base_src_activate_push (GstPad * pad, gboolean active)
{
  RsnBaseSrc *basesrc;
  GstEvent *event;

  basesrc = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  /* prepare subclass first */
  if (active) {
    GST_DEBUG_OBJECT (basesrc, "Activating in push mode");

    if (G_UNLIKELY (!basesrc->can_activate_push))
      goto no_push_activation;

    if (G_UNLIKELY (!rsn_base_src_start (basesrc)))
      goto error_start;

    basesrc->priv->last_sent_eos = FALSE;

    /* do initial seek, which will start the task */
    GST_OBJECT_LOCK (basesrc);
    event = basesrc->data.ABI.pending_seek;
    basesrc->data.ABI.pending_seek = NULL;
    GST_OBJECT_UNLOCK (basesrc);

    /* no need to unlock anything, the task is certainly
     * not running here. The perform seek code will start the task when
     * finished. */
    if (G_UNLIKELY (!rsn_base_src_perform_seek (basesrc, event, FALSE)))
      goto seek_failed;

    if (event)
      gst_event_unref (event);
  } else {
    GST_DEBUG_OBJECT (basesrc, "Deactivating in push mode");
    /* call the unlock function and stop the task */
    if (G_UNLIKELY (!rsn_base_src_deactivate (basesrc, pad)))
      goto deactivate_failed;

    /* now we can stop the source */
    if (G_UNLIKELY (!rsn_base_src_stop (basesrc)))
      goto error_stop;
  }
  return TRUE;

  /* ERRORS */
no_push_activation:
  {
    GST_WARNING_OBJECT (basesrc, "Subclass disabled push-mode activation");
    return FALSE;
  }
error_start:
  {
    GST_WARNING_OBJECT (basesrc, "Failed to start in push mode");
    return FALSE;
  }
seek_failed:
  {
    GST_ERROR_OBJECT (basesrc, "Failed to perform initial seek");
    rsn_base_src_stop (basesrc);
    if (event)
      gst_event_unref (event);
    return FALSE;
  }
deactivate_failed:
  {
    GST_ERROR_OBJECT (basesrc, "Failed to deactivate in push mode");
    return FALSE;
  }
error_stop:
  {
    GST_DEBUG_OBJECT (basesrc, "Failed to stop in push mode");
    return FALSE;
  }
}

static gboolean
rsn_base_src_activate_pull (GstPad * pad, gboolean active)
{
  RsnBaseSrc *basesrc;

  basesrc = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  /* prepare subclass first */
  if (active) {
    GST_DEBUG_OBJECT (basesrc, "Activating in pull mode");
    if (G_UNLIKELY (!rsn_base_src_start (basesrc)))
      goto error_start;

    /* if not random_access, we cannot operate in pull mode for now */
    if (G_UNLIKELY (!rsn_base_src_check_get_range (basesrc)))
      goto no_get_range;
  } else {
    GST_DEBUG_OBJECT (basesrc, "Deactivating in pull mode");
    /* call the unlock function. We have no task to stop. */
    if (G_UNLIKELY (!rsn_base_src_deactivate (basesrc, pad)))
      goto deactivate_failed;

    /* don't send EOS when going from PAUSED => READY when in pull mode */
    basesrc->priv->last_sent_eos = TRUE;

    if (G_UNLIKELY (!rsn_base_src_stop (basesrc)))
      goto error_stop;
  }
  return TRUE;

  /* ERRORS */
error_start:
  {
    GST_ERROR_OBJECT (basesrc, "Failed to start in pull mode");
    return FALSE;
  }
no_get_range:
  {
    GST_ERROR_OBJECT (basesrc, "Cannot operate in pull mode, stopping");
    rsn_base_src_stop (basesrc);
    return FALSE;
  }
deactivate_failed:
  {
    GST_ERROR_OBJECT (basesrc, "Failed to deactivate in pull mode");
    return FALSE;
  }
error_stop:
  {
    GST_ERROR_OBJECT (basesrc, "Failed to stop in pull mode");
    return FALSE;
  }
}

static GstStateChangeReturn
rsn_base_src_change_state (GstElement * element, GstStateChange transition)
{
  RsnBaseSrc *basesrc;
  GstStateChangeReturn result;
  gboolean no_preroll = FALSE;

  basesrc = GST_BASE_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LIVE_LOCK (element);
      basesrc->priv->latency = -1;
      if (basesrc->is_live) {
        no_preroll = TRUE;
        basesrc->live_running = FALSE;
      }
      basesrc->priv->last_sent_eos = FALSE;
      basesrc->priv->discont = TRUE;
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        gboolean start;

        rsn_base_src_unlock_stop (basesrc);
        /* for live sources we restart the timestamp correction */
        basesrc->priv->latency = -1;
        basesrc->live_running = TRUE;
        GST_LIVE_SIGNAL (element);
        /* have to restart the task in case it stopped because of the unlock when
         * we went to PAUSED. Only do this if we operating in push mode. */
        GST_OBJECT_LOCK (basesrc->srcpad);
        start = (GST_PAD_ACTIVATE_MODE (basesrc->srcpad) == GST_ACTIVATE_PUSH);
        GST_OBJECT_UNLOCK (basesrc->srcpad);
        if (start)
          gst_pad_start_task (basesrc->srcpad,
              (GstTaskFunction) rsn_base_src_loop, basesrc->srcpad);
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
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        rsn_base_src_unlock (basesrc);
        no_preroll = TRUE;
        basesrc->live_running = FALSE;
      }
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      GstEvent **event_p;

      /* FIXME, deprecate this behaviour, it is very dangerous.
       * the prefered way of sending EOS downstream is by sending
       * the EOS event to the element */
      if (!basesrc->priv->last_sent_eos) {
        GST_DEBUG_OBJECT (basesrc, "Sending EOS event");
        gst_pad_push_event (basesrc->srcpad, gst_event_new_eos ());
        basesrc->priv->last_sent_eos = TRUE;
      }
      event_p = &basesrc->data.ABI.pending_seek;
      gst_event_replace (event_p, NULL);
      event_p = &basesrc->priv->close_segment;
      gst_event_replace (event_p, NULL);
      event_p = &basesrc->priv->start_segment;
      gst_event_replace (event_p, NULL);
      break;
    }
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
    return result;
  }
}
