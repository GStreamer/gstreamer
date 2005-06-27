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


#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbasesrc.h"
#include "gsttypefindhelper.h"
#include <gst/gstmarshal.h>

#define DEFAULT_BLOCKSIZE	4096

GST_DEBUG_CATEGORY_STATIC (gst_basesrc_debug);
#define GST_CAT_DEFAULT gst_basesrc_debug

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
  PROP_HAS_LOOP,
  PROP_HAS_GETRANGE,
};

static GstElementClass *parent_class = NULL;

static void gst_basesrc_base_init (gpointer g_class);
static void gst_basesrc_class_init (GstBaseSrcClass * klass);
static void gst_basesrc_init (GstBaseSrc * src, gpointer g_class);

GType
gst_basesrc_get_type (void)
{
  static GType basesrc_type = 0;

  if (!basesrc_type) {
    static const GTypeInfo basesrc_info = {
      sizeof (GstBaseSrcClass),
      (GBaseInitFunc) gst_basesrc_base_init,
      NULL,
      (GClassInitFunc) gst_basesrc_class_init,
      NULL,
      NULL,
      sizeof (GstBaseSrc),
      0,
      (GInstanceInitFunc) gst_basesrc_init,
    };

    basesrc_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseSrc", &basesrc_info, G_TYPE_FLAG_ABSTRACT);
  }
  return basesrc_type;
}

static gboolean gst_basesrc_activate (GstPad * pad, GstActivateMode mode);
static void gst_basesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_basesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_basesrc_event_handler (GstPad * pad, GstEvent * event);

static gboolean gst_basesrc_query (GstPad * pad, GstQuery * query);

#if 0
static const GstEventMask *gst_basesrc_get_event_mask (GstPad * pad);
#endif

static gboolean gst_basesrc_unlock (GstBaseSrc * basesrc);
static gboolean gst_basesrc_get_size (GstBaseSrc * basesrc, guint64 * size);
static gboolean gst_basesrc_start (GstBaseSrc * basesrc);
static gboolean gst_basesrc_stop (GstBaseSrc * basesrc);

static GstElementStateReturn gst_basesrc_change_state (GstElement * element);

static void gst_basesrc_set_dataflow_funcs (GstBaseSrc * this);
static void gst_basesrc_loop (GstPad * pad);
static gboolean gst_basesrc_check_get_range (GstPad * pad);
static GstFlowReturn gst_basesrc_get_range (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buf);

static void
gst_basesrc_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_basesrc_debug, "basesrc", 0, "basesrc element");
}

static void
gst_basesrc_class_init (GstBaseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_basesrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_basesrc_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to read per buffer", 1, G_MAXULONG, DEFAULT_BLOCKSIZE,
          G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_LOOP,
      g_param_spec_boolean ("has-loop", "Has loop function",
          "True if the element should expose a loop function", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_GETRANGE,
      g_param_spec_boolean ("has-getrange", "Has getrange function",
          "True if the element should expose a getrange function", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_basesrc_change_state);
}

static void
gst_basesrc_init (GstBaseSrc * basesrc, gpointer g_class)
{
  GstPad *pad;
  GstPadTemplate *pad_template;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);

  pad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_activate_function (pad, gst_basesrc_activate);
  gst_pad_set_event_function (pad, gst_basesrc_event_handler);
  gst_pad_set_query_function (pad, gst_basesrc_query);

  gst_pad_set_checkgetrange_function (pad, gst_basesrc_check_get_range);

  basesrc->is_live = FALSE;
  basesrc->live_lock = g_mutex_new ();
  basesrc->live_cond = g_cond_new ();

  /* hold ref to pad */
  basesrc->srcpad = pad;
  gst_element_add_pad (GST_ELEMENT (basesrc), pad);

  basesrc->segment_start = -1;
  basesrc->segment_end = -1;
  basesrc->blocksize = DEFAULT_BLOCKSIZE;
  basesrc->clock_id = NULL;

  GST_FLAG_UNSET (basesrc, GST_BASESRC_STARTED);
}

void
gst_basesrc_set_live (GstBaseSrc * src, gboolean live)
{
  GST_LIVE_LOCK (src);
  src->is_live = live;
  GST_LIVE_UNLOCK (src);
}

gboolean
gst_basesrc_is_live (GstBaseSrc * src)
{
  gboolean result;

  GST_LIVE_LOCK (src);
  result = src->is_live;
  GST_LIVE_UNLOCK (src);

  return result;
}

static void
gst_basesrc_set_dataflow_funcs (GstBaseSrc * this)
{
  GST_DEBUG ("updating dataflow functions");

  if (this->has_loop)
    gst_pad_set_loop_function (this->srcpad, gst_basesrc_loop);
  else
    gst_pad_set_loop_function (this->srcpad, NULL);

  if (this->has_getrange)
    gst_pad_set_getrange_function (this->srcpad, gst_basesrc_get_range);
  else
    gst_pad_set_getrange_function (this->srcpad, NULL);
}

static gboolean
gst_basesrc_query (GstPad * pad, GstQuery * query)
{
  gboolean b;
  guint64 ui64;
  gint64 i64;
  GstBaseSrc *src;

  src = GST_BASESRC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
          b = gst_basesrc_get_size (src, &ui64);
          /* better to make get_size take an int64 */
          i64 = b ? (gint64) ui64 : -1;
          gst_query_set_position (query, GST_FORMAT_BYTES, src->offset, i64);
          return TRUE;
        case GST_FORMAT_PERCENT:
          b = gst_basesrc_get_size (src, &ui64);
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

#if 0
static const GstEventMask *
gst_basesrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_CUR | GST_SEEK_METHOD_SET |
          GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH |
          GST_SEEK_FLAG_SEGMENT_LOOP},
    {GST_EVENT_FLUSH, 0},
    {GST_EVENT_SIZE, 0},
    {0, 0},
  };
  return masks;
}
#endif

static gboolean
gst_basesrc_do_seek (GstBaseSrc * src, GstEvent * event)
{
  GstFormat format;
  gint64 offset;

  format = GST_EVENT_SEEK_FORMAT (event);

  /* get seek format */
  if (format == GST_FORMAT_DEFAULT)
    format = GST_FORMAT_BYTES;
  /* we can only seek bytes */
  if (format != GST_FORMAT_BYTES)
    return FALSE;

  /* get seek positions */
  offset = GST_EVENT_SEEK_OFFSET (event);
  src->segment_loop = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_SEGMENT_LOOP;

  switch (GST_EVENT_SEEK_METHOD (event)) {
    case GST_SEEK_METHOD_SET:
      if (offset < 0)
        goto error;
      src->offset = MIN (offset, src->size);
      src->segment_start = src->offset;
      src->segment_end = MIN (GST_EVENT_SEEK_ENDOFFSET (event), src->size);
      GST_DEBUG_OBJECT (src, "seek set pending to %" G_GINT64_FORMAT,
          src->offset);
      break;
    case GST_SEEK_METHOD_CUR:
      offset += src->offset;
      src->offset = CLAMP (offset, 0, src->size);
      src->segment_start = src->offset;
      src->segment_end = GST_EVENT_SEEK_ENDOFFSET (event);
      GST_DEBUG_OBJECT (src, "seek cur pending to %" G_GINT64_FORMAT,
          src->offset);
      break;
    case GST_SEEK_METHOD_END:
      if (offset > 0)
        goto error;
      offset = src->size + offset;
      src->offset = MAX (0, offset);
      src->segment_start = src->offset;
      src->segment_end = GST_EVENT_SEEK_ENDOFFSET (event);
      GST_DEBUG_OBJECT (src, "seek end pending to %" G_GINT64_FORMAT,
          src->offset);
      break;
    default:
      goto error;
  }

  /* send flush start */
  gst_pad_push_event (src->srcpad, gst_event_new_flush (FALSE));

  /* unblock streaming thread */
  gst_basesrc_unlock (src);

  /* grab streaming lock */
  GST_STREAM_LOCK (src->srcpad);

  /* send flush end */
  gst_pad_push_event (src->srcpad, gst_event_new_flush (TRUE));

  /* now send discont */
  {
    GstEvent *event;

    event = gst_event_new_discontinuous (1.0,
        GST_FORMAT_BYTES,
        (gint64) src->segment_start, (gint64) src->segment_end, NULL);

    gst_pad_push_event (src->srcpad, event);
  }

  /* and restart the task */
  gst_pad_start_task (src->srcpad, (GstTaskFunction) gst_basesrc_loop,
      src->srcpad);
  GST_STREAM_UNLOCK (src->srcpad);

  gst_event_unref (event);

  return TRUE;

  /* ERROR */
error:
  {
    GST_DEBUG_OBJECT (src, "seek error");
    gst_event_unref (event);
    return FALSE;
  }
}

static gboolean
gst_basesrc_event_handler (GstPad * pad, GstEvent * event)
{
  GstBaseSrc *src;
  GstBaseSrcClass *bclass;
  gboolean result;

  src = GST_BASESRC (GST_PAD_PARENT (pad));
  bclass = GST_BASESRC_GET_CLASS (src);

  if (bclass->event)
    result = bclass->event (src, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      return gst_basesrc_do_seek (src, event);
    case GST_EVENT_SIZE:
    {
      GstFormat format;

      format = GST_EVENT_SIZE_FORMAT (event);
      if (format == GST_FORMAT_DEFAULT)
        format = GST_FORMAT_BYTES;
      /* we can only accept bytes */
      if (format != GST_FORMAT_BYTES)
        return FALSE;

      src->blocksize = GST_EVENT_SIZE_VALUE (event);
      g_object_notify (G_OBJECT (src), "blocksize");
      break;
    }
    case GST_EVENT_FLUSH:
      /* cancel any blocking getrange */
      if (!GST_EVENT_FLUSH_DONE (event))
        gst_basesrc_unlock (src);
      break;
    default:
      break;
  }
  gst_event_unref (event);

  return TRUE;
}

static void
gst_basesrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstBaseSrc *src;

  src = GST_BASESRC (object);

  switch (prop_id) {
    case PROP_BLOCKSIZE:
      src->blocksize = g_value_get_ulong (value);
      break;
    case PROP_HAS_LOOP:
      src->has_loop = g_value_get_boolean (value);
      gst_basesrc_set_dataflow_funcs (src);
      break;
    case PROP_HAS_GETRANGE:
      src->has_getrange = g_value_get_boolean (value);
      gst_basesrc_set_dataflow_funcs (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_basesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseSrc *src;

  src = GST_BASESRC (object);

  switch (prop_id) {
    case PROP_BLOCKSIZE:
      g_value_set_ulong (value, src->blocksize);
      break;
    case PROP_HAS_LOOP:
      g_value_set_boolean (value, src->has_loop);
      break;
    case PROP_HAS_GETRANGE:
      g_value_set_boolean (value, src->has_getrange);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_basesrc_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstBaseSrc *src;
  GstBaseSrcClass *bclass;

  src = GST_BASESRC (GST_OBJECT_PARENT (pad));
  bclass = GST_BASESRC_GET_CLASS (src);

  GST_LIVE_LOCK (src);
  if (src->is_live) {
    while (!src->live_running) {
      GST_DEBUG ("live source waiting for running state");
      GST_LIVE_WAIT (src);
      GST_DEBUG ("live source unlocked");
    }
  }
  GST_LIVE_UNLOCK (src);

  if (!GST_FLAG_IS_SET (src, GST_BASESRC_STARTED))
    goto not_started;

  if (!bclass->create)
    goto no_function;

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

  ret = bclass->create (src, offset, length, buf);

  return ret;

  /* ERROR */
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
    GST_DEBUG_OBJECT (src, "unexpected length %u", length);
    return GST_FLOW_UNEXPECTED;
  }
}

static gboolean
gst_basesrc_check_get_range (GstPad * pad)
{
  GstBaseSrc *src;

  src = GST_BASESRC (GST_OBJECT_PARENT (pad));

  if (!GST_FLAG_IS_SET (src, GST_BASESRC_STARTED)) {
    gst_basesrc_start (src);
    gst_basesrc_stop (src);
  }

  return src->seekable;
}

static void
gst_basesrc_loop (GstPad * pad)
{
  GstBaseSrc *src;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;

  src = GST_BASESRC (GST_OBJECT_PARENT (pad));

  ret = gst_basesrc_get_range (pad, src->offset, src->blocksize, &buf);
  if (ret != GST_FLOW_OK)
    goto eos;

  src->offset += GST_BUFFER_SIZE (buf);

  ret = gst_pad_push (pad, buf);
  if (ret != GST_FLOW_OK)
    goto pause;

  return;

eos:
  {
    GST_DEBUG_OBJECT (src, "going to EOS");
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new (GST_EVENT_EOS));
    return;
  }
pause:
  {
    GST_DEBUG_OBJECT (src, "pausing task");
    gst_pad_pause_task (pad);
    return;
  }
}

static gboolean
gst_basesrc_unlock (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result = FALSE;

  GST_DEBUG ("unlock");
  /* unblock whatever the subclass is doing */
  bclass = GST_BASESRC_GET_CLASS (basesrc);
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
gst_basesrc_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstBaseSrcClass *bclass;
  gboolean result = FALSE;

  bclass = GST_BASESRC_GET_CLASS (basesrc);
  if (bclass->get_size)
    result = bclass->get_size (basesrc, size);

  if (result)
    basesrc->size = *size;

  return result;
}

static gboolean
gst_basesrc_is_seekable (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;

  bclass = GST_BASESRC_GET_CLASS (basesrc);

  /* check if we can seek */
  if (bclass->is_seekable)
    basesrc->seekable = bclass->is_seekable (basesrc);
  else
    basesrc->seekable = FALSE;

  return basesrc->seekable;
}

static gboolean
gst_basesrc_start (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result;

  if (GST_FLAG_IS_SET (basesrc, GST_BASESRC_STARTED))
    return TRUE;

  bclass = GST_BASESRC_GET_CLASS (basesrc);
  if (bclass->start)
    result = bclass->start (basesrc);
  else
    result = TRUE;

  if (!result)
    goto could_not_start;

  GST_FLAG_SET (basesrc, GST_BASESRC_STARTED);

  /* start in the beginning */
  basesrc->offset = 0;
  basesrc->segment_start = 0;

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
  basesrc->segment_end = -1;

  /* check if we can seek, updates ->seekable */
  gst_basesrc_is_seekable (basesrc);

  /* run typefind */
#if 0
  if (basesrc->seekable) {
    GstCaps *caps;

    caps = gst_type_find_helper (basesrc->srcpad, basesrc->size);
    gst_pad_set_caps (basesrc->srcpad, caps);
  }
#endif

  return TRUE;

  /* ERROR */
could_not_start:
  {
    GST_DEBUG_OBJECT (basesrc, "could not start");
    return FALSE;
  }
}

static gboolean
gst_basesrc_stop (GstBaseSrc * basesrc)
{
  GstBaseSrcClass *bclass;
  gboolean result = TRUE;

  if (!GST_FLAG_IS_SET (basesrc, GST_BASESRC_STARTED))
    return TRUE;

  bclass = GST_BASESRC_GET_CLASS (basesrc);
  if (bclass->stop)
    result = bclass->stop (basesrc);

  if (result)
    GST_FLAG_UNSET (basesrc, GST_BASESRC_STARTED);

  return result;
}

static gboolean
gst_basesrc_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result;
  GstBaseSrc *basesrc;

  basesrc = GST_BASESRC (GST_OBJECT_PARENT (pad));

  /* prepare subclass first */
  switch (mode) {
    case GST_ACTIVATE_PUSH:
    case GST_ACTIVATE_PULL:
      result = gst_basesrc_start (basesrc);
      break;
    default:
      result = TRUE;
      break;
  }
  /* if that failed we can stop here */
  if (!result)
    goto error_start;

  result = FALSE;
  switch (mode) {
    case GST_ACTIVATE_PUSH:
      result =
          gst_pad_start_task (pad, (GstTaskFunction) gst_basesrc_loop, pad);
      break;
    case GST_ACTIVATE_PULL:
      result = basesrc->seekable;
      if (!result)
        gst_basesrc_stop (basesrc);
      break;
    case GST_ACTIVATE_NONE:
      GST_LIVE_LOCK (basesrc);
      basesrc->live_running = TRUE;
      GST_LIVE_SIGNAL (basesrc);
      GST_LIVE_UNLOCK (basesrc);

      /* step 1, unblock clock sync (if any) */
      gst_basesrc_unlock (basesrc);

      /* step 2, make sure streaming finishes */
      result = gst_pad_stop_task (pad);
      break;
  }
  return result;

  /* ERROR */
error_start:
  {
    GST_DEBUG_OBJECT (basesrc, "failed to start");
    return FALSE;
  }
}

static GstElementStateReturn
gst_basesrc_change_state (GstElement * element)
{
  GstBaseSrc *basesrc;
  GstElementStateReturn result = GST_STATE_SUCCESS;
  GstElementStateReturn presult;
  GstElementState transition;

  basesrc = GST_BASESRC (element);

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        result = GST_STATE_NO_PREROLL;
        basesrc->live_running = FALSE;
      }
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      GST_LIVE_LOCK (element);
      basesrc->live_running = TRUE;
      GST_LIVE_SIGNAL (element);
      GST_LIVE_UNLOCK (element);
      break;
    default:
      break;
  }

  if ((presult = GST_ELEMENT_CLASS (parent_class)->change_state (element)) !=
      GST_STATE_SUCCESS)
    return presult;

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      GST_LIVE_LOCK (element);
      if (basesrc->is_live) {
        result = GST_STATE_NO_PREROLL;
        basesrc->live_running = FALSE;
      }
      GST_LIVE_UNLOCK (element);
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (!gst_basesrc_stop (basesrc))
        result = GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return result;
}
