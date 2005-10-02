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

#define DEFAULT_BLOCKSIZE	4096
#define DEFAULT_NUM_BUFFERS	-1

GST_DEBUG_CATEGORY_STATIC (gst_base_src_debug);
#define GST_CAT_DEFAULT gst_base_src_debug

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

static gboolean gst_base_src_query (GstPad * pad, GstQuery * query);

#if 0
static const GstEventMask *gst_base_src_get_event_mask (GstPad * pad);
#endif
static gboolean gst_base_src_default_negotiate (GstBaseSrc * basesrc);

static gboolean gst_base_src_unlock (GstBaseSrc * basesrc);
static gboolean gst_base_src_get_size (GstBaseSrc * basesrc, guint64 * size);
static gboolean gst_base_src_start (GstBaseSrc * basesrc);
static gboolean gst_base_src_stop (GstBaseSrc * basesrc);

static GstStateChangeReturn gst_base_src_change_state (GstElement * element,
    GstStateChange transition);

static void gst_base_src_loop (GstPad * pad);
static gboolean gst_base_src_check_get_range (GstPad * pad);
static GstFlowReturn gst_base_src_get_range (GstPad * pad, guint64 offset,
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

  klass->negotiate = gst_base_src_default_negotiate;
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
      GST_DEBUG_FUNCPTR (gst_base_src_get_range));
  gst_pad_set_getcaps_function (pad, GST_DEBUG_FUNCPTR (gst_base_src_getcaps));
  gst_pad_set_setcaps_function (pad, GST_DEBUG_FUNCPTR (gst_base_src_setcaps));

  /* hold pointer to pad */
  basesrc->srcpad = pad;
  GST_DEBUG_OBJECT (basesrc, "adding src pad");
  gst_element_add_pad (GST_ELEMENT (basesrc), pad);

  basesrc->segment_start = -1;
  basesrc->segment_end = -1;
  basesrc->need_discont = TRUE;
  basesrc->blocksize = DEFAULT_BLOCKSIZE;
  basesrc->clock_id = NULL;

  GST_FLAG_UNSET (basesrc, GST_BASE_SRC_STARTED);

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
  gboolean b;
  guint64 ui64;
  gint64 i64;
  GstBaseSrc *src;

  src = GST_BASE_SRC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
          b = gst_base_src_get_size (src, &ui64);
          /* better to make get_size take an int64 */
          i64 = b ? (gint64) ui64 : -1;
          gst_query_set_position (query, GST_FORMAT_BYTES, src->offset, i64);
          return TRUE;
        case GST_FORMAT_PERCENT:
          b = gst_base_src_get_size (src, &ui64);
          i64 = GST_FORMAT_PERCENT_MAX;
          i64 *= b ? (src->offset / (gdouble) ui64) : 1.0;
          gst_query_set_position (query, GST_FORMAT_PERCENT,
              i64, GST_FORMAT_PERCENT_MAX);
          return TRUE;
        default:
          return FALSE;
      }
    }

    case GST_QUERY_SEEKING:
      gst_query_set_seeking (query, GST_FORMAT_BYTES,
          src->seekable, src->segment_start, src->segment_end);
      return TRUE;

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 3, GST_FORMAT_DEFAULT,
          GST_FORMAT_BYTES, GST_FORMAT_PERCENT);
      return TRUE;

    case GST_QUERY_LATENCY:
    case GST_QUERY_JITTER:
    case GST_QUERY_RATE:
    case GST_QUERY_CONVERT:
    default:
      return gst_pad_query_default (pad, query);
  }
}

static gboolean
gst_base_src_send_discont (GstBaseSrc * src)
{
  GstEvent *event;

  GST_DEBUG_OBJECT (src, "Sending newsegment from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, (gint64) src->segment_start,
      (gint64) src->segment_end);
  event = gst_event_new_newsegment (1.0,
      GST_FORMAT_BYTES,
      (gint64) src->segment_start, (gint64) src->segment_end, (gint64) 0);

  return gst_pad_push_event (src->srcpad, event);
}

static gboolean
gst_base_src_do_seek (GstBaseSrc * src, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  /* get seek format */
  if (format == GST_FORMAT_DEFAULT)
    format = GST_FORMAT_BYTES;
  /* we can only seek bytes */
  if (format != GST_FORMAT_BYTES)
    return FALSE;

  /* get seek positions */
  src->segment_loop = flags & GST_SEEK_FLAG_SEGMENT;

  /* send flush start */
  gst_pad_push_event (src->srcpad, gst_event_new_flush_start ());

  /* unblock streaming thread */
  gst_base_src_unlock (src);

  /* grab streaming lock */
  GST_STREAM_LOCK (src->srcpad);

  /* send flush stop */
  gst_pad_push_event (src->srcpad, gst_event_new_flush_stop ());

  /* perform the seek */
  switch (cur_type) {
    case GST_SEEK_TYPE_NONE:
      break;
    case GST_SEEK_TYPE_SET:
      if (cur < 0)
        goto error;
      src->offset = MIN (cur, src->size);
      src->segment_start = src->offset;
      break;
    case GST_SEEK_TYPE_CUR:
      src->offset = CLAMP (src->offset + cur, 0, src->size);
      src->segment_start = src->offset;
      break;
    case GST_SEEK_TYPE_END:
      if (cur > 0)
        goto error;
      src->offset = MAX (0, src->size + cur);
      src->segment_start = src->offset;
      break;
    default:
      goto error;
  }

  switch (stop_type) {
    case GST_SEEK_TYPE_NONE:
      break;
    case GST_SEEK_TYPE_SET:
      if (stop < 0)
        goto error;
      src->segment_end = MIN (stop, src->size);
      break;
    case GST_SEEK_TYPE_CUR:
      src->segment_end = CLAMP (src->segment_end + stop, 0, src->size);
      break;
    case GST_SEEK_TYPE_END:
      if (stop > 0)
        goto error;
      src->segment_end = src->size + stop;
      break;
    default:
      goto error;
  }

  GST_DEBUG_OBJECT (src, "seek pending for segment from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, src->segment_start, src->segment_end);

  /* now make sure the discont will be send */
  src->need_discont = TRUE;

  /* and restart the task */
  gst_pad_start_task (src->srcpad, (GstTaskFunction) gst_base_src_loop,
      src->srcpad);
  GST_STREAM_UNLOCK (src->srcpad);

  gst_event_unref (event);

  return TRUE;

  /* ERROR */
error:
  {
    GST_DEBUG_OBJECT (src, "seek error");
    GST_STREAM_UNLOCK (src->srcpad);
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_base_src_event_handler (GstPad * pad, GstEvent * event)
{
  GstBaseSrc *src;
  GstBaseSrcClass *bclass;
  gboolean result;

  src = GST_BASE_SRC (GST_PAD_PARENT (pad));
  bclass = GST_BASE_SRC_GET_CLASS (src);

  if (bclass->event)
    result = bclass->event (src, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (!src->seekable) {
        gst_event_unref (event);
        return FALSE;
      }
      return gst_base_src_do_seek (src, event);
    case GST_EVENT_FLUSH_START:
      /* cancel any blocking getrange */
      gst_base_src_unlock (src);
      break;
    case GST_EVENT_FLUSH_STOP:
      break;
    default:
      break;
  }
  gst_event_unref (event);

  return TRUE;
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

static GstFlowReturn
gst_base_src_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstBaseSrc *src;
  GstBaseSrcClass *bclass;

  src = GST_BASE_SRC (GST_OBJECT_PARENT (pad));
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
  }
  GST_LIVE_UNLOCK (src);

  GST_LOCK (pad);
  if (GST_PAD_IS_FLUSHING (pad))
    goto flushing;
  GST_UNLOCK (pad);

  if (!GST_FLAG_IS_SET (src, GST_BASE_SRC_STARTED))
    goto not_started;

  if (!bclass->create)
    goto no_function;

  GST_DEBUG_OBJECT (src,
      "reading offset %" G_GUINT64_FORMAT ", length %u, size %"
      G_GUINT64_FORMAT, offset, length, src->size);

  /* check size */
  if (src->size != -1) {
    if (offset > src->size)
      goto unexpected_length;

    if (offset + length > src->size) {
      if (bclass->get_size)
        bclass->get_size (src, &src->size);

      if (offset + length > src->size) {
        length = src->size - offset;
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

  return ret;

  /* ERROR */
flushing:
  {
    GST_DEBUG_OBJECT (src, "pad is flushing");
    GST_UNLOCK (pad);
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

static gboolean
gst_base_src_check_get_range (GstPad * pad)
{
  GstBaseSrc *src;

  src = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  if (!GST_FLAG_IS_SET (src, GST_BASE_SRC_STARTED)) {
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

  src = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  if (src->need_discont) {
    /* now send discont */
    gst_base_src_send_discont (src);
    src->need_discont = FALSE;
  }

  ret = gst_base_src_get_range (pad, src->offset, src->blocksize, &buf);
  if (ret != GST_FLOW_OK)
    goto eos;

  if (buf == NULL)
    goto error;

  src->offset += GST_BUFFER_SIZE (buf);

  ret = gst_pad_push (pad, buf);
  if (ret != GST_FLOW_OK)
    goto pause;

  return;

eos:
  {
    GST_DEBUG_OBJECT (src, "going to EOS");
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new_eos ());
    return;
  }
pause:
  {
    GST_DEBUG_OBJECT (src, "pausing task");
    gst_pad_pause_task (pad);
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (src, STREAM, STOPPED,
          ("streaming stopped, reason %s", gst_flow_get_name (ret)),
          ("streaming stopped, reason %s", gst_flow_get_name (ret)));
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
    return;
  }
error:
  {
    GST_ELEMENT_ERROR (src, STREAM, STOPPED,
        ("internal: element returned NULL buffer"),
        ("internal: element returned NULL buffer"));
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new_eos ());
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
  gboolean result = FALSE;

  GST_DEBUG ("unlock");
  /* unblock whatever the subclass is doing */
  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->unlock)
    result = bclass->unlock (basesrc);

  GST_DEBUG ("unschedule clock");
  /* and unblock the clock as well, if any */
  GST_LOCK (basesrc);
  if (basesrc->clock_id) {
    gst_clock_id_unschedule (basesrc->clock_id);
  }
  GST_UNLOCK (basesrc);

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

static gboolean
gst_base_src_default_negotiate (GstBaseSrc * basesrc)
{
  GstCaps *thiscaps;
  GstCaps *caps = NULL;
  GstCaps *peercaps = NULL;
  gboolean result = FALSE;

  thiscaps = gst_pad_get_caps (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG ("caps of src: %" GST_PTR_FORMAT, thiscaps);
  if (thiscaps == NULL || gst_caps_is_any (thiscaps))
    goto no_nego_needed;

  peercaps = gst_pad_peer_get_caps (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG ("caps of peer: %" GST_PTR_FORMAT, peercaps);
  if (peercaps) {
    GstCaps *icaps;

    icaps = gst_caps_intersect (thiscaps, peercaps);
    GST_DEBUG ("intersect: %" GST_PTR_FORMAT, icaps);
    gst_caps_unref (thiscaps);
    gst_caps_unref (peercaps);
    if (icaps) {
      caps = gst_caps_copy_nth (icaps, 0);
      gst_caps_unref (icaps);
    }
  } else {
    caps = thiscaps;
  }
  if (caps) {
    caps = gst_caps_make_writable (caps);
    gst_caps_truncate (caps);

    gst_pad_fixate_caps (GST_BASE_SRC_PAD (basesrc), caps);
    GST_DEBUG ("fixated to: %" GST_PTR_FORMAT, caps);

    if (gst_caps_is_any (caps)) {
      gst_caps_unref (caps);
      result = TRUE;
    } else if (gst_caps_is_fixed (caps)) {
      gst_pad_set_caps (GST_BASE_SRC_PAD (basesrc), caps);
      gst_caps_unref (caps);
      result = TRUE;
    }
  }
  return result;

no_nego_needed:
  {
    GST_DEBUG ("no negotiation needed");
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

  if (GST_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED))
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

  GST_FLAG_SET (basesrc, GST_BASE_SRC_STARTED);

  /* start in the beginning */
  basesrc->offset = 0;

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

  /* we always run to the end */
  basesrc->segment_start = 0;
  basesrc->segment_end = basesrc->size;
  basesrc->need_discont = TRUE;

  /* check if we can seek, updates ->seekable */
  gst_base_src_is_seekable (basesrc);

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

  if (!GST_FLAG_IS_SET (basesrc, GST_BASE_SRC_STARTED))
    return TRUE;

  GST_DEBUG_OBJECT (basesrc, "stopping source");

  bclass = GST_BASE_SRC_GET_CLASS (basesrc);
  if (bclass->stop)
    result = bclass->stop (basesrc);

  if (result)
    GST_FLAG_UNSET (basesrc, GST_BASE_SRC_STARTED);

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
  gst_base_src_unlock (basesrc);

  /* step 2, make sure streaming finishes */
  result = gst_pad_stop_task (pad);

  return result;
}

static gboolean
gst_base_src_activate_push (GstPad * pad, gboolean active)
{
  GstBaseSrc *basesrc;

  basesrc = GST_BASE_SRC (GST_OBJECT_PARENT (pad));

  /* prepare subclass first */
  if (active) {
    GST_DEBUG_OBJECT (basesrc, "Activating in push mode");

    if (!basesrc->can_activate_push)
      goto no_push_activation;

    if (!gst_base_src_start (basesrc))
      goto error_start;

    return gst_pad_start_task (pad, (GstTaskFunction) gst_base_src_loop, pad);
  } else {
    GST_DEBUG_OBJECT (basesrc, "Deactivating in push mode");
    return gst_base_src_deactivate (basesrc, pad);
  }

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
  GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;
  GstStateChangeReturn presult;

  basesrc = GST_BASE_SRC (element);


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        result = GST_STATE_CHANGE_NO_PREROLL;
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

  if ((presult =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        result = GST_STATE_CHANGE_NO_PREROLL;
        basesrc->live_running = FALSE;
      }
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_base_src_stop (basesrc))
        result = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return result;

  /* ERRORS */
failure:
  {
    gst_base_src_stop (basesrc);
    return presult;
  }
}
