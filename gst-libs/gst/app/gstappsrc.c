/* GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <string.h>

#include "gstapp-marshal.h"
#include "gstappsrc.h"


GST_DEBUG_CATEGORY (app_src_debug);
#define GST_CAT_DEFAULT app_src_debug

static const GstElementDetails app_src_details = GST_ELEMENT_DETAILS ("AppSrc",
    "Generic/Src",
    "Allow the application to feed buffers to a pipeline",
    "David Schleef <ds@schleef.org>, Wim Taymans <wim.taymans@gmail.com");

enum
{
  /* signals */
  SIGNAL_NEED_DATA,
  SIGNAL_ENOUGH_DATA,
  SIGNAL_SEEK_DATA,

  /* actions */
  SIGNAL_PUSH_BUFFER,
  SIGNAL_END_OF_STREAM,

  LAST_SIGNAL
};

#define DEFAULT_PROP_MAX_BUFFERS   0
#define DEFAULT_PROP_SIZE          -1
#define DEFAULT_PROP_SEEKABLE      FALSE

enum
{
  PROP_0,
  PROP_CAPS,
  PROP_SIZE,
  PROP_SEEKABLE,
  PROP_MAX_BUFFERS,

  PROP_LAST
};

static GstStaticPadTemplate gst_app_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_app_src_dispose (GObject * object);
static void gst_app_src_finalize (GObject * object);

static void gst_app_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_app_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_app_src_create (GstPushSrc * psrc, GstBuffer ** buf);
static gboolean gst_app_src_start (GstBaseSrc * psrc);
static gboolean gst_app_src_stop (GstBaseSrc * psrc);
static gboolean gst_app_src_unlock (GstBaseSrc * psrc);
static gboolean gst_app_src_unlock_stop (GstBaseSrc * psrc);

static guint gst_app_src_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstAppSrc, gst_app_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void
gst_app_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (app_src_debug, "appsrc", 0, "appsrc element");

  gst_element_class_set_details (element_class, &app_src_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_app_src_template));
}

static void
gst_app_src_class_init (GstAppSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstPushSrcClass *pushsrc_class = (GstPushSrcClass *) klass;
  GstBaseSrcClass *basesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->dispose = gst_app_src_dispose;
  gobject_class->finalize = gst_app_src_finalize;

  gobject_class->set_property = gst_app_src_set_property;
  gobject_class->get_property = gst_app_src_get_property;

  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The allowed caps for the src pad", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SIZE,
      g_param_spec_int64 ("size", "Size",
          "The size of the data stream (-1 if unknown)",
          -1, G_MAXINT64, DEFAULT_PROP_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SEEKABLE,
      g_param_spec_boolean ("seekable", "Seekable",
          "If the source is seekable", DEFAULT_PROP_SEEKABLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BUFFERS,
      g_param_spec_uint ("max-buffers", "Max Buffers",
          "The maximum number of buffers to queue internally (0 = unlimited)",
          0, G_MAXUINT, DEFAULT_PROP_MAX_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstAppSrc::need-data:
   * @appsrc: the appsrc element that emited the signal
   *
   * Signal that the source needs more data. In the callback you should call
   * push-buffer or end-of-stream.
   */
  gst_app_src_signals[SIGNAL_NEED_DATA] =
      g_signal_new ("need-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSrcClass, need_data),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstAppSrc::enough-data:
   * @appsrc: the appsrc element that emited the signal
   *
   * Signal that the source has enough data. It is recommended that the
   * application stops calling push-buffer until the need-data signal is
   * emited again to avoid excessive buffer queueing.
   */
  gst_app_src_signals[SIGNAL_NEED_DATA] =
      g_signal_new ("enough-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSrcClass, enough_data),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);
  /**
   * GstAppSrc::seek-data:
   * @appsrc: the appsrc element that emited the signal
   * @offset: the offset to seek to
   *
   * Seek to the given offset. The next push-buffer should produce buffers from
   * the new @offset.
   */
  gst_app_src_signals[SIGNAL_SEEK_DATA] =
      g_signal_new ("seek-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSrcClass, seek_data),
      NULL, NULL, gst_app_marshal_VOID__UINT64, G_TYPE_NONE, 1, G_TYPE_UINT64);

  gst_app_src_signals[SIGNAL_PUSH_BUFFER] =
      g_signal_new ("push-buffer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstAppSrcClass,
          push_buffer), NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GST_TYPE_BUFFER);

  gst_app_src_signals[SIGNAL_END_OF_STREAM] =
      g_signal_new ("end-of-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstAppSrcClass,
          end_of_stream), NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  pushsrc_class->create = gst_app_src_create;
  basesrc_class->start = gst_app_src_start;
  basesrc_class->stop = gst_app_src_stop;
  basesrc_class->unlock = gst_app_src_unlock;
  basesrc_class->unlock_stop = gst_app_src_unlock_stop;
}

static void
gst_app_src_init (GstAppSrc * appsrc, GstAppSrcClass * klass)
{
  appsrc->mutex = g_mutex_new ();
  appsrc->cond = g_cond_new ();
  appsrc->queue = g_queue_new ();

  appsrc->size = DEFAULT_PROP_SIZE;
  appsrc->seekable = DEFAULT_PROP_SEEKABLE;
  appsrc->max_buffers = DEFAULT_PROP_MAX_BUFFERS;
}

static void
gst_app_src_dispose (GObject * obj)
{
  GstAppSrc *appsrc = GST_APP_SRC (obj);

  if (appsrc->caps) {
    gst_caps_unref (appsrc->caps);
    appsrc->caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_app_src_finalize (GObject * obj)
{
  GstAppSrc *appsrc = GST_APP_SRC (obj);

  g_mutex_free (appsrc->mutex);
  g_cond_free (appsrc->cond);
  g_queue_free (appsrc->queue);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_app_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAppSrc *appsrc = GST_APP_SRC (object);

  switch (prop_id) {
    case PROP_CAPS:
      gst_app_src_set_caps (appsrc, gst_value_get_caps (value));
      break;
    case PROP_SIZE:
      gst_app_src_set_size (appsrc, g_value_get_int64 (value));
      break;
    case PROP_SEEKABLE:
      gst_app_src_set_seekable (appsrc, g_value_get_boolean (value));
      break;
    case PROP_MAX_BUFFERS:
      gst_app_src_set_max_buffers (appsrc, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_app_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAppSrc *appsrc = GST_APP_SRC (object);

  switch (prop_id) {
    case PROP_CAPS:
    {
      GstCaps *caps;

      caps = gst_app_src_get_caps (appsrc);
      gst_value_set_caps (value, caps);
      if (caps)
        gst_caps_unref (caps);
      break;
    }
    case PROP_SIZE:
      g_value_set_int64 (value, gst_app_src_get_size (appsrc));
      break;
    case PROP_SEEKABLE:
      g_value_set_boolean (value, gst_app_src_get_seekable (appsrc));
      break;
    case PROP_MAX_BUFFERS:
      g_value_set_uint (value, gst_app_src_get_max_buffers (appsrc));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_app_src_unlock (GstBaseSrc * psrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (psrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "unlock start");
  appsrc->flushing = TRUE;
  g_cond_signal (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static gboolean
gst_app_src_unlock_stop (GstBaseSrc * psrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (psrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "unlock stop");
  appsrc->flushing = FALSE;
  g_cond_signal (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static gboolean
gst_app_src_start (GstBaseSrc * psrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (psrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "starting");
  appsrc->started = TRUE;
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static gboolean
gst_app_src_stop (GstBaseSrc * psrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (psrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "stopping");
  appsrc->is_eos = FALSE;
  appsrc->flushing = TRUE;
  appsrc->started = FALSE;
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static GstFlowReturn
gst_app_src_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstAppSrc *appsrc = GST_APP_SRC (psrc);
  GstFlowReturn ret;

  g_mutex_lock (appsrc->mutex);
  while (TRUE) {
    /* check flushing first */
    if (appsrc->flushing)
      goto flushing;

    /* return data as long as we have some */
    if (!g_queue_is_empty (appsrc->queue)) {
      *buf = g_queue_pop_head (appsrc->queue);

      gst_buffer_set_caps (*buf, appsrc->caps);

      GST_DEBUG_OBJECT (appsrc, "we have buffer %p", *buf);
      ret = GST_FLOW_OK;
      break;
    }

    /* check EOS */
    if (appsrc->is_eos)
      goto eos;

    /* nothing to return, wait a while for new data or flushing */
    g_cond_wait (appsrc->cond, appsrc->mutex);
  }
  g_mutex_unlock (appsrc->mutex);

  return ret;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (appsrc, "we are flushing");
    g_mutex_unlock (appsrc->mutex);
    return GST_FLOW_WRONG_STATE;
  }
eos:
  {
    GST_DEBUG_OBJECT (appsrc, "we are EOS");
    g_mutex_unlock (appsrc->mutex);
    return GST_FLOW_UNEXPECTED;
  }
}


/* external API */

/**
 * gst_app_src_set_caps:
 * @appsrc: a #GstAppSrc
 * @caps: caps to set
 *
 * Set the capabilities on the appsrc element.  This function takes
 * a copy of the caps structure. After calling this method, the source will
 * only produce caps that match @caps. @caps must be fixed and the caps on the
 * buffers must match the caps or left NULL.
 */
void
gst_app_src_set_caps (GstAppSrc * appsrc, const GstCaps * caps)
{
  GstCaps *old;

  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  GST_OBJECT_LOCK (appsrc);
  GST_DEBUG_OBJECT (appsrc, "setting caps to %" GST_PTR_FORMAT, caps);
  if ((old = appsrc->caps) != caps) {
    if (caps)
      appsrc->caps = gst_caps_copy (caps);
    else
      appsrc->caps = NULL;
    if (old)
      gst_caps_unref (old);
  }
  GST_OBJECT_UNLOCK (appsrc);
}

/**
 * gst_app_src_get_caps:
 * @appsrc: a #GstAppSrc
 *
 * Get the configured caps on @appsrc.
 *
 * Returns: the #GstCaps produced by the source. gst_caps_unref() after usage.
 */
GstCaps *
gst_app_src_get_caps (GstAppSrc * appsrc)
{
  GstCaps *caps;

  g_return_val_if_fail (appsrc != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), NULL);

  GST_OBJECT_LOCK (appsrc);
  if ((caps = appsrc->caps))
    gst_caps_ref (caps);
  GST_DEBUG_OBJECT (appsrc, "getting caps of %" GST_PTR_FORMAT, caps);
  GST_OBJECT_UNLOCK (appsrc);

  return caps;
}

/**
 * gst_app_src_set_size:
 * @appsrc: a #GstAppSrc
 * @size: the size to set
 *
 * Set the size of the stream in bytes. A value of -1 means that the size is
 * not known. 
 */
void
gst_app_src_set_size (GstAppSrc * appsrc, gint64 size)
{
  g_return_if_fail (appsrc != NULL);
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  GST_OBJECT_LOCK (appsrc);
  GST_DEBUG_OBJECT (appsrc, "setting size of %" G_GINT64_FORMAT, size);
  appsrc->size = size;
  GST_OBJECT_UNLOCK (appsrc);
}

/**
 * gst_app_src_get_size:
 * @appsrc: a #GstAppSrc
 *
 * Get the size of the stream in bytes. A value of -1 means that the size is
 * not known. 
 *
 * Returns: the size of the stream previously set with gst_app_src_set_size();
 */
gint64
gst_app_src_get_size (GstAppSrc * appsrc)
{
  gint64 size;

  g_return_val_if_fail (appsrc != NULL, -1);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), -1);

  GST_OBJECT_LOCK (appsrc);
  size = appsrc->size;
  GST_DEBUG_OBJECT (appsrc, "getting size of %" G_GINT64_FORMAT, size);
  GST_OBJECT_UNLOCK (appsrc);

  return size;
}

/**
 * gst_app_src_set_seekable:
 * @appsrc: a #GstAppSrc
 * @seekable: the new state
 *
 * Set whether the data is seekable. When this flag is set to %TRUE, the
 * "seek" signal must be connected to.
 */
void
gst_app_src_set_seekable (GstAppSrc * appsrc, gboolean seekable)
{
  g_return_if_fail (appsrc != NULL);
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  GST_OBJECT_LOCK (appsrc);
  GST_DEBUG_OBJECT (appsrc, "setting seekable of %d", seekable);
  appsrc->seekable = seekable;
  GST_OBJECT_UNLOCK (appsrc);
}

/**
 * gst_app_src_get_seekable:
 * @appsrc: a #GstAppSrc
 *
 * Get whether the stream is seekable. Control the seeking behaviour of the
 * stream with gst_app_src_set_seekable().
 *
 * Returns: %TRUE if the stream is seekable.
 */
gboolean
gst_app_src_get_seekable (GstAppSrc * appsrc)
{
  gboolean seekable;

  g_return_val_if_fail (appsrc != NULL, FALSE);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), FALSE);

  GST_OBJECT_LOCK (appsrc);
  seekable = appsrc->seekable;
  GST_DEBUG_OBJECT (appsrc, "getting seekable of %d", seekable);
  GST_OBJECT_UNLOCK (appsrc);

  return seekable;
}

/**
 * gst_app_src_set_max_buffers:
 * @appsrc: a #GstAppSrc
 * @max: the maximum number of buffers to queue
 *
 * Set the maximum amount of buffers that can be queued in @appsrc.
 * After the maximum amount of buffers are queued, @appsrc will emit the
 * "enough-data" signal.
 */
void
gst_app_src_set_max_buffers (GstAppSrc * appsrc, guint max)
{
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  g_mutex_lock (appsrc->mutex);
  if (max != appsrc->max_buffers) {
    GST_DEBUG_OBJECT (appsrc, "setting max-buffers to %u", max);
    appsrc->max_buffers = max;
    /* signal the change */
    g_cond_signal (appsrc->cond);
  }
  g_mutex_unlock (appsrc->mutex);
}

/**
 * gst_app_src_get_max_buffers:
 * @appsrc: a #GstAppSrc
 *
 * Get the maximum amount of buffers that can be queued in @appsrc.
 *
 * Returns: The maximum amount of buffers that can be queued.
 */
guint
gst_app_src_get_max_buffers (GstAppSrc * appsrc)
{
  guint result;

  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), 0);

  g_mutex_lock (appsrc->mutex);
  result = appsrc->max_buffers;
  GST_DEBUG_OBJECT (appsrc, "getting max-buffers of %u", result);
  g_mutex_unlock (appsrc->mutex);

  return result;
}

/**
 * gst_app_src_push_buffer:
 * @appsrc:
 * @buffer:
 *
 * Adds a buffer to the queue of buffers that the appsrc element will
 * push to its source pad.  This function takes ownership of the buffer.
 */
void
gst_app_src_push_buffer (GstAppSrc * appsrc, GstBuffer * buffer)
{
  g_return_if_fail (appsrc);
  g_return_if_fail (GST_IS_APP_SRC (appsrc));
  g_return_if_fail (GST_IS_BUFFER (buffer));

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "queueing buffer %p", buffer);
  g_queue_push_tail (appsrc->queue, buffer);
  g_cond_signal (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);
}

/**
 * gst_app_src_end_of_stream:
 * @appsrc:
 *
 * Indicates to the appsrc element that the last buffer queued in the
 * element is the last buffer of the stream.
 */
void
gst_app_src_end_of_stream (GstAppSrc * appsrc)
{
  g_return_if_fail (appsrc);
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "sending EOS");
  appsrc->is_eos = TRUE;
  g_cond_signal (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);
}
