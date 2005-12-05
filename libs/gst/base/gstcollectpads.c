/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstcollectpads.c:
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
 * SECTION:gstcollectpads
 * @short_description: manages a set of pads that operate in collect mode
 * @see_also:
 *
 * Manages a set of pads that operate in collect mode. This means that control
 * is given to the manager of this object when all pads have data.
 * <itemizedlist>
 *   <listitem><para>
 *     Collectpads are created with gst_collect_pads_new(). A callback should then
 *     be installed with gst_collect_pads_set_function (). 
 *   </para></listitem>
 *   <listitem><para>
 *     Pads are added to the collection with gst_collect_pads_add_pad()/
 *     gst_collect_pads_remove_pad(). The pad
 *     has to be a sinkpad. The chain function of the pad is
 *     overridden. The element_private of the pad is used to store
 *     private information.
 *   </para></listitem>
 *   <listitem><para>
 *     For each pad, data is queued in the chain function or by
 *     performing a pull_range.
 *   </para></listitem>
 *   <listitem><para>
 *     When data is queued on all pads, the callback function is called.
 *   </para></listitem>
 *   <listitem><para>
 *     Data can be dequeued from the pad with the gst_collect_pads_pop() method.
 *     One can peek at the data with the gst_collect_pads_peek() function.
 *     These functions will return NULL if the pad received an EOS event. When all
 *     pads return NULL from a gst_collect_pads_peek(), the element can emit an EOS
 *     event itself.
 *   </para></listitem>
 *   <listitem><para>
 *     Data can also be dequeued in byte units using the gst_collect_pads_available(), 
 *     gst_collect_pads_read() and gst_collect_pads_flush() calls.
 *   </para></listitem>
 *   <listitem><para>
 *     Elements should call gst_collect_pads_start() and gst_collect_pads_stop() in
 *     their state change functions to start and stop the processing of the collecpads.
 *     The gst_collect_pads_stop() call should be called before calling the parent
 *     element state change function in the PAUSED_TO_READY state change to ensure
 *     no pad is blocked and the element can finish streaming.
 *   </para></listitem>
 *   <listitem><para>
 *     gst_collect_pads_collect() and gst_collect_pads_collect_range() can be used by
 *     elements that start a #GstTask to drive the collect_pads.
 *   </para></listitem>
 * </itemizedlist>
 */

#include "gstcollectpads.h"

GST_DEBUG_CATEGORY_STATIC (collect_pads_debug);
#define GST_CAT_DEFAULT collect_pads_debug

GST_BOILERPLATE (GstCollectPads, gst_collect_pads, GstObject, GST_TYPE_OBJECT)

     static GstFlowReturn gst_collect_pads_chain (GstPad * pad,
    GstBuffer * buffer);
     static gboolean gst_collect_pads_event (GstPad * pad, GstEvent * event);
     static void gst_collect_pads_finalize (GObject * object);
     static void gst_collect_pads_init (GstCollectPads * pads,
    GstCollectPadsClass * g_class);

     static void gst_collect_pads_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (collect_pads_debug, "collectpads", 0,
      "GstCollectPads");
}

static void
gst_collect_pads_class_init (GstCollectPadsClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_collect_pads_finalize);
}

static void
gst_collect_pads_init (GstCollectPads * pads, GstCollectPadsClass * g_class)
{
  pads->cond = g_cond_new ();
  pads->data = NULL;
  pads->cookie = 0;
  pads->numpads = 0;
  pads->queuedpads = 0;
  pads->eospads = 0;
  pads->started = FALSE;
}

static void
gst_collect_pads_finalize (GObject * object)
{
  GstCollectPads *pads = GST_COLLECT_PADS (object);

  gst_collect_pads_stop (pads);
  g_cond_free (pads->cond);
  /* FIXME, free data */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_collect_pads_new:
 *
 * Create a new instance of #GstCollectsPads.
 *
 * Returns: a new #GstCollectPads, or NULL in case of an error.
 *
 * MT safe.
 */
GstCollectPads *
gst_collect_pads_new (void)
{
  GstCollectPads *newcoll;

  newcoll = g_object_new (GST_TYPE_COLLECT_PADS, NULL);

  return newcoll;
}

/**
 * gst_collect_pads_set_function:
 * @pads: the collectspads to use
 * @func: the function to set
 * @user_data: user data passed to the function
 *
 * Set the callback function and user data that will be called when
 * all the pads added to the collection have buffers queued.
 *
 * MT safe.
 */
void
gst_collect_pads_set_function (GstCollectPads * pads,
    GstCollectPadsFunction func, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS (pads));

  GST_OBJECT_LOCK (pads);
  pads->func = func;
  pads->user_data = user_data;
  GST_OBJECT_UNLOCK (pads);
}

/**
 * gst_collect_pads_add_pad:
 * @pads: the collectspads to use
 * @pad: the pad to add
 * @size: the size of the returned GstCollectData structure
 *
 * Add a pad to the collection of collect pads. The pad has to be
 * a sinkpad.
 *
 * You specify a size for the returned #GstCollectData structure
 * so that you can use it to store additional information.
 *
 * Returns: a new #GstCollectData to identify the new pad. Or NULL
 *   if wrong parameters are supplied.
 *
 * MT safe.
 */
GstCollectData *
gst_collect_pads_add_pad (GstCollectPads * pads, GstPad * pad, guint size)
{
  GstCollectData *data;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), NULL);
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_PAD_IS_SINK (pad), NULL);
  g_return_val_if_fail (size >= sizeof (GstCollectData), NULL);

  data = g_malloc0 (size);
  data->collect = pads;
  data->pad = pad;
  data->buffer = NULL;
  gst_segment_init (&data->segment, GST_FORMAT_UNDEFINED);

  GST_OBJECT_LOCK (pads);
  pads->data = g_slist_append (pads->data, data);
  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_collect_pads_chain));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_collect_pads_event));
  gst_pad_set_element_private (pad, data);
  pads->numpads++;
  pads->cookie++;
  GST_OBJECT_UNLOCK (pads);

  return data;
}

static gint
find_pad (GstCollectData * data, GstPad * pad)
{
  if (data->pad == pad)
    return 0;
  return 1;
}

/**
 * gst_collect_pads_remove_pad:
 * @pads: the collectspads to use
 * @pad: the pad to remove
 *
 * Remove a pad from the collection of collect pads.
 *
 * Returns: TRUE if the pad could be removed.
 *
 * MT safe.
 */
gboolean
gst_collect_pads_remove_pad (GstCollectPads * pads, GstPad * pad)
{
  GSList *list;

  g_return_val_if_fail (pads != NULL, FALSE);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_OBJECT_LOCK (pads);
  list = g_slist_find_custom (pads->data, pad, (GCompareFunc) find_pad);
  if (list) {
    g_free (list->data);
    pads->data = g_slist_delete_link (pads->data, list);
  }
  pads->numpads--;
  pads->cookie++;
  GST_OBJECT_UNLOCK (pads);

  return list != NULL;
}

/**
 * gst_collect_pads_is_active:
 * @pads: the collectspads to use
 * @pad: the pad to check
 *
 * Check if a pad is active.
 *
 * Returns: TRUE if the pad is active.
 *
 * MT safe.
 */
gboolean
gst_collect_pads_is_active (GstCollectPads * pads, GstPad * pad)
{
  g_return_val_if_fail (pads != NULL, FALSE);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  g_warning ("gst_collect_pads_is_active() is not implemented");

  return FALSE;
}

/**
 * gst_collect_pads_collect:
 * @pads: the collectspads to use
 *
 * Collect data on all pads. This function is usually called
 * from a GstTask function in an element. This function is
 * currently not implemented.
 *
 * Returns: GstFlowReturn of the operation.
 *
 * MT safe.
 */
GstFlowReturn
gst_collect_pads_collect (GstCollectPads * pads)
{
  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), GST_FLOW_ERROR);

  g_warning ("gst_collect_pads_collect() is not implemented");

  return GST_FLOW_ERROR;
}

/**
 * gst_collect_pads_collect_range:
 * @pads: the collectspads to use
 * @offset: the offset to collect
 * @length: the length to collect
 *
 * Collect data with @offset and @length on all pads. This function
 * is typically called in the getrange function of an element. This
 * function is currently not implemented.
 *
 * Returns: GstFlowReturn of the operation.
 *
 * MT safe.
 */
GstFlowReturn
gst_collect_pads_collect_range (GstCollectPads * pads, guint64 offset,
    guint length)
{
  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), GST_FLOW_ERROR);

  g_warning ("gst_collect_pads_collect_range() is not implemented");

  return GST_FLOW_ERROR;
}

/**
 * gst_collect_pads_start:
 * @pads: the collectspads to use
 *
 * Starts the processing of data in the collect_pads.
 *
 * MT safe.
 */
void
gst_collect_pads_start (GstCollectPads * pads)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS (pads));

  GST_OBJECT_LOCK (pads);
  pads->started = TRUE;
  GST_OBJECT_UNLOCK (pads);
}

/**
 * gst_collect_pads_stop:
 * @pads: the collectspads to use
 *
 * Stops the processing of data in the collect_pads. this function
 * will also unblock any blocking operations.
 *
 * MT safe.
 */
void
gst_collect_pads_stop (GstCollectPads * pads)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECT_PADS (pads));

  GST_OBJECT_LOCK (pads);
  pads->started = FALSE;
  GST_COLLECT_PADS_BROADCAST (pads);
  GST_OBJECT_UNLOCK (pads);
}

/**
 * gst_collect_pads_peek:
 * @pads: the collectspads to peek
 * @data: the data to use
 *
 * Peek at the buffer currently queued in @data. This function
 * should be called with the @pads LOCK held, such as in the callback
 * handler.
 *
 * Returns: The buffer in @data or NULL if no buffer is queued.
 *  should unref the buffer after usage.
 *
 * MT safe.
 */
GstBuffer *
gst_collect_pads_peek (GstCollectPads * pads, GstCollectData * data)
{
  GstBuffer *result;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  result = data->buffer;

  if (result)
    gst_buffer_ref (result);

  GST_DEBUG ("Peeking at pad %s:%s: buffer=%p",
      GST_DEBUG_PAD_NAME (data->pad), result);

  return result;
}

/**
 * gst_collect_pads_pop:
 * @pads: the collectspads to pop
 * @data: the data to use
 *
 * Pop the buffer currently queued in @data. This function
 * should be called with the @pads LOCK held, such as in the callback
 * handler.
 *
 * Returns: The buffer in @data or NULL if no buffer was queued.
 *   You should unref the buffer after usage.
 *
 * MT safe.
 */
GstBuffer *
gst_collect_pads_pop (GstCollectPads * pads, GstCollectData * data)
{
  GstBuffer *result;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  result = data->buffer;
  if (result) {
    gst_buffer_replace (&data->buffer, NULL);
    data->pos = 0;
    pads->queuedpads--;
  }

  GST_COLLECT_PADS_SIGNAL (pads);

  GST_DEBUG ("Pop buffer on pad %s:%s: buffer=%p",
      GST_DEBUG_PAD_NAME (data->pad), result);

  return result;
}

/**
 * gst_collect_pads_available:
 * @pads: the collectspads to query
 *
 * Query how much bytes can be read from each queued buffer. This means
 * that the result of this call is the maximum number of bytes that can
 * be read from each of the pads.
 *
 * This function should be called with @pads LOCK held, such as
 * in the callback.
 *
 * Returns: The maximum number of bytes queued on all pad. This function
 * returns 0 if a pad has no queued buffer.
 *
 * MT safe.
 */
guint
gst_collect_pads_available (GstCollectPads * pads)
{
  GSList *collected;
  guint result = G_MAXUINT;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), 0);

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstCollectData *pdata;
    gint size;

    pdata = (GstCollectData *) collected->data;

    if (pdata->buffer == NULL)
      goto not_filled;

    size = GST_BUFFER_SIZE (pdata->buffer) - pdata->pos;

    if (size < result)
      result = size;
  }
  return result;

not_filled:
  {
    return 0;
  }
}

/**
 * gst_collect_pads_read:
 * @pads: the collectspads to query
 * @data: the data to use
 * @bytes: a pointer to a byte array
 * @size: the number of bytes to read
 *
 * Get a pointer in @bytes where @size bytes can be read from the
 * given pad data.
 *
 * This function should be called with @pads LOCK held, such as
 * in the callback.
 *
 * Returns: The number of bytes available for consumption in the
 * memory pointed to by @bytes. This can be less than @size and
 * is 0 if the pad is end-of-stream.
 *
 * MT safe.
 */
guint
gst_collect_pads_read (GstCollectPads * pads, GstCollectData * data,
    guint8 ** bytes, guint size)
{
  guint readsize;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), 0);
  g_return_val_if_fail (data != NULL, 0);
  g_return_val_if_fail (bytes != NULL, 0);

  readsize = MIN (size, GST_BUFFER_SIZE (data->buffer) - data->pos);

  *bytes = GST_BUFFER_DATA (data->buffer) + data->pos;

  return readsize;
}

/**
 * gst_collect_pads_flush:
 * @pads: the collectspads to query
 * @data: the data to use
 * @size: the number of bytes to flush
 *
 * Flush @size bytes from the pad @data.
 *
 * This function should be called with @pads LOCK held, such as
 * in the callback.
 *
 * Returns: The number of bytes flushed This can be less than @size and
 * is 0 if the pad was end-of-stream.
 *
 * MT safe.
 */
guint
gst_collect_pads_flush (GstCollectPads * pads, GstCollectData * data,
    guint size)
{
  guint flushsize;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECT_PADS (pads), 0);
  g_return_val_if_fail (data != NULL, 0);

  flushsize = MIN (size, GST_BUFFER_SIZE (data->buffer) - data->pos);

  data->pos += size;

  if (data->pos >= GST_BUFFER_SIZE (data->buffer)) {
    GstBuffer *buf;

    buf = gst_collect_pads_pop (pads, data);
    gst_buffer_unref (buf);
  }

  return flushsize;
}

static gboolean
gst_collect_pads_event (GstPad * pad, GstEvent * event)
{
  GstCollectData *data;
  GstCollectPads *pads;

  /* some magic to get the managing collect_pads */
  data = (GstCollectData *) gst_pad_get_element_private (pad);
  if (data == NULL)
    goto not_ours;

  pads = data->collect;

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (data->pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn ret = GST_FLOW_OK;

      GST_OBJECT_LOCK (pads);

      pads->eospads++;

      /* if all pads are EOS and we have a function, call it */
      if ((pads->eospads == pads->numpads) && pads->func) {
        ret = pads->func (pads, pads->user_data);
      }

      GST_OBJECT_UNLOCK (pads);

      /* We eat this event */
      gst_event_unref (event);
      return TRUE;
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 start, stop, time;
      gdouble rate;
      GstFormat format;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &rate, &format,
          &start, &stop, &time);

      gst_segment_set_newsegment (&data->segment, update, rate, format,
          start, stop, time);
      goto beach;
    }
    default:
      goto beach;
  }

beach:
  return gst_pad_event_default (pad, event);

  /* ERRORS */
not_ours:
  {
    GST_DEBUG ("collect_pads not ours");
    return FALSE;
  }
}


static GstFlowReturn
gst_collect_pads_chain (GstPad * pad, GstBuffer * buffer)
{
  GstCollectData *data;
  GstCollectPads *pads;
  guint64 size;
  GstFlowReturn ret;

  GST_DEBUG ("Got buffer for pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* some magic to get the managing collect_pads */
  data = (GstCollectData *) gst_pad_get_element_private (pad);
  if (data == NULL)
    goto not_ours;

  pads = data->collect;
  size = GST_BUFFER_SIZE (buffer);

  GST_OBJECT_LOCK (pads);

  /* if not started, bail out */
  if (!pads->started)
    goto not_started;

  /* Call the collected callback until a pad with a buffer is popped. */
  while (((pads->queuedpads + pads->eospads) == pads->numpads) && pads->func)
    ret = pads->func (pads, pads->user_data);

  /* queue buffer on this pad, block if filled */
  while (data->buffer != NULL) {
    GST_DEBUG ("Pad %s:%s already has a buffer queued, waiting",
        GST_DEBUG_PAD_NAME (pad));
    GST_COLLECT_PADS_WAIT (pads);
    GST_DEBUG ("Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));
    /* after a signal,  we could be stopped */
    if (!pads->started)
      goto not_started;
  }

  GST_DEBUG ("Queuing buffer %p for pad %s:%s", buffer,
      GST_DEBUG_PAD_NAME (pad));

  pads->queuedpads++;
  gst_buffer_replace (&data->buffer, buffer);

  /* if all pads have data and we have a function, call it */
  if (((pads->queuedpads + pads->eospads) == pads->numpads) && pads->func) {
    GST_DEBUG ("All active pads have data, calling %s",
        GST_DEBUG_FUNCPTR_NAME (pads->func));
    ret = pads->func (pads, pads->user_data);
  } else {
    GST_DEBUG ("Not all active pads have data, continuing");
    ret = GST_FLOW_OK;
  }
  GST_OBJECT_UNLOCK (pads);

  return ret;

  /* ERRORS */
not_ours:
  {
    GST_DEBUG ("collect_pads not ours");
    return GST_FLOW_ERROR;
  }
not_started:
  {
    GST_OBJECT_UNLOCK (pads);
    GST_DEBUG ("collect_pads not started");
    return GST_FLOW_WRONG_STATE;
  }
}
