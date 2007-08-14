/* GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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
#include <gst/base/gstbasesink.h>

#include <string.h>

#include "gstappsink.h"


GST_DEBUG_CATEGORY (app_sink_debug);
#define GST_CAT_DEFAULT app_sink_debug

static const GstElementDetails app_sink_details =
GST_ELEMENT_DETAILS ("AppSink",
    "Generic/Sink",
    "Allow the application to get access to raw buffer",
    "David Schleef <ds@schleef.org>, Wim Taymans <wim.taymans@gmail.com");

enum
{
  PROP_0
};

static GstStaticPadTemplate gst_app_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_app_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_app_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_app_sink_dispose (GObject * object);
static gboolean gst_app_sink_start (GstBaseSink * psink);
static gboolean gst_app_sink_stop (GstBaseSink * psink);
static gboolean gst_app_sink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_app_sink_preroll (GstBaseSink * psink,
    GstBuffer * buffer);
static GstFlowReturn gst_app_sink_render (GstBaseSink * psink,
    GstBuffer * buffer);
static GstCaps *gst_app_sink_get_caps (GstBaseSink * psink);

GST_BOILERPLATE (GstAppSink, gst_app_sink, GstBaseSink, GST_TYPE_BASE_SINK);

static void
gst_app_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (app_sink_debug, "appsink", 0, "appsink element");

  gst_element_class_set_details (element_class, &app_sink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_app_sink_template));
}

static void
gst_app_sink_class_init (GstAppSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_app_sink_set_property;
  gobject_class->get_property = gst_app_sink_get_property;
  gobject_class->dispose = gst_app_sink_dispose;

  basesink_class->start = gst_app_sink_start;
  basesink_class->stop = gst_app_sink_stop;
  basesink_class->event = gst_app_sink_event;
  basesink_class->render = gst_app_sink_preroll;
  basesink_class->render = gst_app_sink_render;
  basesink_class->get_caps = gst_app_sink_get_caps;
}

static void
gst_app_sink_dispose (GObject * obj)
{
  GstAppSink *appsink = GST_APP_SINK (obj);

  if (appsink->caps) {
    gst_caps_unref (appsink->caps);
    appsink->caps = NULL;
  }
  if (appsink->preroll) {
    gst_buffer_unref (appsink->preroll);
    appsink->preroll = NULL;
  }
  if (appsink->mutex) {
    g_mutex_free (appsink->mutex);
    appsink->mutex = NULL;
  }
  if (appsink->cond) {
    g_cond_free (appsink->cond);
    appsink->cond = NULL;
  }
  if (appsink->queue) {
    g_queue_foreach (appsink->queue, (GFunc) gst_mini_object_unref, NULL);
    g_queue_free (appsink->queue);
    appsink->queue = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_app_sink_init (GstAppSink * appsink, GstAppSinkClass * klass)
{
  appsink->mutex = g_mutex_new ();
  appsink->cond = g_cond_new ();
  appsink->queue = g_queue_new ();
}

static void
gst_app_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAppSink *appsink = GST_APP_SINK (object);

  GST_OBJECT_LOCK (appsink);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (appsink);
}

static void
gst_app_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAppSink *appsink = GST_APP_SINK (object);

  GST_OBJECT_LOCK (appsink);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (appsink);
}

static void
gst_app_sink_flush_unlocked (GstAppSink * appsink)
{
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (appsink, "flushing appsink");
  appsink->end_of_stream = FALSE;
  gst_buffer_replace (&appsink->preroll, NULL);
  while ((buffer = g_queue_pop_head (appsink->queue)))
    gst_buffer_unref (buffer);
  g_cond_signal (appsink->cond);
}

static gboolean
gst_app_sink_start (GstBaseSink * psink)
{
  GstAppSink *appsink = GST_APP_SINK (psink);

  g_mutex_lock (appsink->mutex);
  appsink->end_of_stream = FALSE;
  appsink->started = TRUE;
  GST_DEBUG_OBJECT (appsink, "starting");
  g_mutex_unlock (appsink->mutex);

  return TRUE;
}

static gboolean
gst_app_sink_stop (GstBaseSink * psink)
{
  GstAppSink *appsink = GST_APP_SINK (psink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "stopping");
  appsink->started = FALSE;
  gst_app_sink_flush_unlocked (appsink);
  g_mutex_unlock (appsink->mutex);

  return TRUE;
}

static gboolean
gst_app_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstAppSink *appsink = GST_APP_SINK (sink);

  switch (event->type) {
    case GST_EVENT_EOS:
      g_mutex_lock (appsink->mutex);
      GST_DEBUG_OBJECT (appsink, "receiving EOS");
      appsink->end_of_stream = TRUE;
      g_cond_signal (appsink->cond);
      g_mutex_unlock (appsink->mutex);
      break;
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      g_mutex_lock (appsink->mutex);
      GST_DEBUG_OBJECT (appsink, "received FLUSH_STOP");
      gst_app_sink_flush_unlocked (appsink);
      g_mutex_unlock (appsink->mutex);
      break;
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_app_sink_preroll (GstBaseSink * psink, GstBuffer * buffer)
{
  GstAppSink *appsink = GST_APP_SINK (psink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "setting preroll buffer %p", buffer);
  gst_buffer_replace (&appsink->preroll, buffer);
  g_cond_signal (appsink->cond);
  g_mutex_unlock (appsink->mutex);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_app_sink_render (GstBaseSink * psink, GstBuffer * buffer)
{
  GstAppSink *appsink = GST_APP_SINK (psink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "pushing render buffer %p on queue", buffer);
  g_queue_push_tail (appsink->queue, gst_buffer_ref (buffer));
  g_cond_signal (appsink->cond);
  g_mutex_unlock (appsink->mutex);

  return GST_FLOW_OK;
}

static GstCaps *
gst_app_sink_get_caps (GstBaseSink * psink)
{
  GstCaps *caps;

  GstAppSink *appsink = GST_APP_SINK (psink);

  GST_OBJECT_LOCK (appsink);
  if ((caps = appsink->caps))
    gst_caps_ref (caps);
  GST_DEBUG_OBJECT (appsink, "got caps " GST_PTR_FORMAT, caps);
  GST_OBJECT_UNLOCK (appsink);

  return caps;
}

/* external API */

/**
 * gst_app_sink_set_caps:
 * @appsink: a #GstAppSink
 * @caps: caps to set
 *
 * Set the capabilities on the appsink element.  This function takes
 * a ref of the caps structure. After calling this method, the sink will only
 * accept caps that match @caps. If @caps is non-fixed, you must check the caps
 * on the buffers to get the actual used caps.
 */
void
gst_app_sink_set_caps (GstAppSink * appsink, GstCaps * caps)
{
  g_return_if_fail (appsink != NULL);
  g_return_if_fail (GST_IS_APP_SINK (appsink));

  GST_OBJECT_LOCK (appsink);
  GST_DEBUG_OBJECT (appsink, "setting caps to " GST_PTR_FORMAT, caps);
  gst_caps_replace (&appsink->caps, caps);
  GST_OBJECT_UNLOCK (appsink);
}

/**
 * gst_app_sink_end_of_stream:
 * @appsink: a #GstAppSink
 *
 * Check if @appsink is EOS, which is when no more buffers can be pulled because
 * an EOS event was received.
 *
 * This function also returns %TRUE when the appsink is not in the PAUSED or
 * PLAYING state.
 *
 * Returns: %TRUE if no more buffers can be pulled and the appsink is EOS.
 */
gboolean
gst_app_sink_end_of_stream (GstAppSink * appsink)
{
  gboolean ret;

  g_return_val_if_fail (appsink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), FALSE);

  g_mutex_lock (appsink->mutex);
  if (!appsink->started)
    goto not_started;

  if (appsink->end_of_stream && g_queue_is_empty (appsink->queue)) {
    GST_DEBUG_OBJECT (appsink, "we are EOS and the queue is empty");
    ret = TRUE;
  } else {
    GST_DEBUG_OBJECT (appsink, "we are not yet EOS");
    ret = FALSE;
  }
  g_mutex_unlock (appsink->mutex);

  return ret;

not_started:
  {
    GST_DEBUG_OBJECT (appsink, "we are stopped, return TRUE");
    g_mutex_unlock (appsink->mutex);
    return TRUE;
  }
}

/**
 * gst_app_sink_pull_preroll:
 * @appsink: a #GstAppSink
 *
 * Get the last preroll buffer in @appsink. This was the buffer that caused the
 * appsink to preroll in the PAUSED state. This buffer can be pulled many times
 * and remains available to the application even after EOS.
 *
 * This function is typically used when dealing with a pipeline in the PAUSED
 * state. Calling this function after doing a seek will give the buffer right
 * after the seek position.
 *
 * Note that the preroll buffer will also be returned as the first buffer
 * when calling gst_app_sink_pull_buffer().
 *
 * If an EOS event was received before any buffers, this function also returns
 * %NULL. 
 *
 * This function blocks until a preroll buffer or EOS is received or the appsink
 * element is set to the READY/NULL state. 
 *
 * Returns: a #GstBuffer or NULL when the appsink is stopped or EOS.
 */
GstBuffer *
gst_app_sink_pull_preroll (GstAppSink * appsink)
{
  GstBuffer *buf = NULL;

  g_return_val_if_fail (appsink != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), NULL);

  g_mutex_lock (appsink->mutex);

  while (TRUE) {
    GST_DEBUG_OBJECT (appsink, "trying to grab a buffer");
    if (!appsink->started)
      goto not_started;

    if (appsink->preroll != NULL)
      break;

    if (appsink->end_of_stream)
      goto eos;

    /* nothing to return, wait */
    GST_DEBUG_OBJECT (appsink, "waiting for the preroll buffer");
    g_cond_wait (appsink->cond, appsink->mutex);
  }
  buf = gst_buffer_ref (appsink->preroll);
  GST_DEBUG_OBJECT (appsink, "we have the preroll buffer %p", buf);
  g_mutex_unlock (appsink->mutex);

  return buf;

  /* special conditions */
eos:
  {
    GST_DEBUG_OBJECT (appsink, "we are EOS, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
not_started:
  {
    GST_DEBUG_OBJECT (appsink, "we are stopped, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
}

/**
 * gst_app_sink_pull_buffer:
 * @appsink: a #GstAppSink
 *
 * This function blocks until a buffer or EOS becomes available or the appsink
 * element is set to the READY/NULL state. 
 *
 * This function will only return buffers when the appsink is in the PLAYING
 * state. All rendered buffers will be put in a queue so that the application
 * can pull buffers at its own rate. Note that when the application does not
 * pull buffers fast enough, the queued buffers could consume a lot of memory,
 * especially when dealing with raw video frames.
 *
 * If an EOS event was received before any buffers, this function returns
 * %NULL. 
 *
 * Returns: a #GstBuffer or NULL when the appsink is stopped or EOS.
 */
GstBuffer *
gst_app_sink_pull_buffer (GstAppSink * appsink)
{
  GstBuffer *buf = NULL;

  g_return_val_if_fail (appsink != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), NULL);

  g_mutex_lock (appsink->mutex);

  while (TRUE) {
    GST_DEBUG_OBJECT (appsink, "trying to grab a buffer");
    if (!appsink->started)
      goto not_started;

    if (!g_queue_is_empty (appsink->queue))
      break;

    if (appsink->end_of_stream)
      goto eos;

    /* nothing to return, wait */
    GST_DEBUG_OBJECT (appsink, "waiting for a buffer");
    g_cond_wait (appsink->cond, appsink->mutex);
  }
  buf = g_queue_pop_head (appsink->queue);
  GST_DEBUG_OBJECT (appsink, "we have a buffer %p", buf);
  g_mutex_unlock (appsink->mutex);

  return buf;

  /* special conditions */
eos:
  {
    GST_DEBUG_OBJECT (appsink, "we are EOS, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
not_started:
  {
    GST_DEBUG_OBJECT (appsink, "we are stopped, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
}
