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
 *     Pads are added to the collection with add/remove_pad. The pad
 *     has to be a sinkpad. The chain function of the pad is
 *     overridden. The element_private of the pad is used to store
 *     private information.
 *   </para></listitem>
 *   <listitem><para>
 *     For each pad, data is queued in the chain function or by
 *     performing a pull_range.
 *   </para></listitem>
 *   <listitem><para>
 *     When data is queued on all pads, a callback function is called.
 *   </para></listitem>
 *   <listitem><para>
 *     Data can be dequeued from the pad with the _pop() method.
 *     One can _peek() at the data with the peek function.
 *   </para></listitem>
 *   <listitem><para>
 *     Data can also be dequeued with the available/read/flush calls.
 *   </para></listitem>
 * </itemizedlist>
 */

#include "gstcollectpads.h"

static GstFlowReturn gst_collectpads_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_collectpads_event (GstPad * pad, GstEvent * event);

static void gst_collectpads_class_init (GstCollectPadsClass * klass);
static void gst_collectpads_init (GstCollectPads * pads);
static void gst_collectpads_finalize (GObject * object);

static GstObjectClass *parent_class = NULL;

GType
gst_collectpads_get_type (void)
{
  static GType collect_type = 0;

  if (!collect_type) {
    static const GTypeInfo collect_info = {
      sizeof (GstCollectPadsClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_collectpads_class_init,
      NULL,
      NULL,
      sizeof (GstCollectPads),
      0,
      (GInstanceInitFunc) gst_collectpads_init,
      NULL
    };

    collect_type = g_type_register_static (GST_TYPE_OBJECT, "GstCollectPads",
        &collect_info, 0);
  }
  return collect_type;
}

static void
gst_collectpads_class_init (GstCollectPadsClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_collectpads_finalize);

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);
}

static void
gst_collectpads_init (GstCollectPads * pads)
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
gst_collectpads_finalize (GObject * object)
{
  GstCollectPads *pads = GST_COLLECTPADS (object);

  gst_collectpads_stop (pads);
  g_cond_free (pads->cond);
  /* FIXME, free data */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_collectpads_new:
 *
 * Create a new instance of #GstCollectsPads.
 *
 * Returns: a new #GstCollectPads, or NULL in case of an error.
 *
 * MT safe.
 */
GstCollectPads *
gst_collectpads_new (void)
{
  GstCollectPads *newcoll;

  newcoll = g_object_new (GST_TYPE_COLLECTPADS, NULL);

  return newcoll;
}

/**
 * gst_collectpads_set_function:
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
gst_collectpads_set_function (GstCollectPads * pads,
    GstCollectPadsFunction func, gpointer user_data)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECTPADS (pads));

  GST_LOCK (pads);
  pads->func = func;
  pads->user_data = user_data;
  GST_UNLOCK (pads);
}

/**
 * gst_collectpads_add_pad:
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
gst_collectpads_add_pad (GstCollectPads * pads, GstPad * pad, guint size)
{
  GstCollectData *data;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), NULL);
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_PAD_IS_SINK (pad), NULL);
  g_return_val_if_fail (size >= sizeof (GstCollectData), NULL);

  data = g_malloc0 (size);
  data->collect = pads;
  data->pad = pad;
  data->buffer = NULL;

  GST_LOCK (pads);
  pads->data = g_slist_append (pads->data, data);
  gst_pad_set_chain_function (pad, gst_collectpads_chain);
  gst_pad_set_event_function (pad, gst_collectpads_event);
  gst_pad_set_element_private (pad, data);
  pads->numpads++;
  pads->cookie++;
  GST_UNLOCK (pads);

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
 * gst_collectpads_remove_pad:
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
gst_collectpads_remove_pad (GstCollectPads * pads, GstPad * pad)
{
  GSList *list;

  g_return_val_if_fail (pads != NULL, FALSE);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_LOCK (pads);
  list = g_slist_find_custom (pads->data, pad, (GCompareFunc) find_pad);
  if (list) {
    g_free (list->data);
    pads->data = g_slist_delete_link (pads->data, list);
  }
  pads->numpads--;
  pads->cookie++;
  GST_UNLOCK (pads);

  return list != NULL;
}

/**
 * gst_collectpads_is_active:
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
gst_collectpads_is_active (GstCollectPads * pads, GstPad * pad)
{
  g_return_val_if_fail (pads != NULL, FALSE);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  return FALSE;
}

/**
 * gst_collectpads_collect:
 * @pads: the collectspads to use
 *
 * Collect data on all pads. This function is usually called
 * from a GstTask function in an element.
 *
 * Returns: GstFlowReturn of the operation.
 *
 * MT safe.
 */
GstFlowReturn
gst_collectpads_collect (GstCollectPads * pads)
{
  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), GST_FLOW_ERROR);

  return GST_FLOW_ERROR;
}

/**
 * gst_collectpads_collect_range:
 * @pads: the collectspads to use
 * @offset: the offset to collect
 * @length: the length to collect
 *
 * Collect data with @offset and @length on all pads. This function
 * is typically called in the getrange function of an element.
 *
 * Returns: GstFlowReturn of the operation.
 *
 * MT safe.
 */
GstFlowReturn
gst_collectpads_collect_range (GstCollectPads * pads, guint64 offset,
    guint length)
{
  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), GST_FLOW_ERROR);

  return GST_FLOW_ERROR;
}

/**
 * gst_collectpads_start:
 * @pads: the collectspads to use
 *
 * Starts the processing of data in the collectpads.
 *
 * MT safe.
 */
void
gst_collectpads_start (GstCollectPads * pads)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECTPADS (pads));

  GST_LOCK (pads);
  pads->started = TRUE;
  GST_UNLOCK (pads);
}

/**
 * gst_collectpads_stop:
 * @pads: the collectspads to use
 *
 * Stops the processing of data in the collectpads. this function
 * will also unblock any blocking operations.
 *
 * MT safe.
 */
void
gst_collectpads_stop (GstCollectPads * pads)
{
  g_return_if_fail (pads != NULL);
  g_return_if_fail (GST_IS_COLLECTPADS (pads));

  GST_LOCK (pads);
  pads->started = FALSE;
  GST_COLLECTPADS_BROADCAST (pads);
  GST_UNLOCK (pads);
}

/**
 * gst_collectpads_peek:
 * @pads: the collectspads to peek
 * @data: the data to use
 *
 * Peek at the buffer currently queued in @data. This function
 * should be called with the @pads LOCK held, such as in the callback
 * handler.
 *
 * Returns: The buffer in @data or NULL if no buffer is queued. You
 *  should unref the buffer after usage.
 *
 * MT safe.
 */
GstBuffer *
gst_collectpads_peek (GstCollectPads * pads, GstCollectData * data)
{
  GstBuffer *result;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  result = data->buffer;

  if (result)
    gst_buffer_ref (result);

  return result;
}

/**
 * gst_collectpads_pop:
 * @pads: the collectspads to pop
 * @data: the data to use
 *
 * Pop the buffer currently queued in @data. This function
 * should be called with the @pads LOCK held, such as in the callback
 * handler.
 *
 * Returns: The buffer in @data or NULL if no buffer was queued. The
 *   You should unref the buffer after usage.
 *
 * MT safe.
 */
GstBuffer *
gst_collectpads_pop (GstCollectPads * pads, GstCollectData * data)
{
  GstBuffer *result;

  g_return_val_if_fail (pads != NULL, NULL);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), NULL);
  g_return_val_if_fail (data != NULL, NULL);

  result = data->buffer;
  if (result) {
    gst_buffer_replace (&data->buffer, NULL);
    data->pos = 0;
    pads->queuedpads--;

  }

  GST_COLLECTPADS_SIGNAL (pads);

  return result;
}

/**
 * gst_collectpads_available:
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
gst_collectpads_available (GstCollectPads * pads)
{
  GSList *collected;
  guint result = G_MAXUINT;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), 0);

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
 * gst_collectpads_read:
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
gst_collectpads_read (GstCollectPads * pads, GstCollectData * data,
    guint8 ** bytes, guint size)
{
  guint readsize;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), 0);
  g_return_val_if_fail (data != NULL, 0);
  g_return_val_if_fail (bytes != NULL, 0);

  readsize = MIN (size, GST_BUFFER_SIZE (data->buffer) - data->pos);

  *bytes = GST_BUFFER_DATA (data->buffer) + data->pos;

  return readsize;
}

/**
 * gst_collectpads_flush:
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
gst_collectpads_flush (GstCollectPads * pads, GstCollectData * data, guint size)
{
  guint flushsize;

  g_return_val_if_fail (pads != NULL, 0);
  g_return_val_if_fail (GST_IS_COLLECTPADS (pads), 0);
  g_return_val_if_fail (data != NULL, 0);

  flushsize = MIN (size, GST_BUFFER_SIZE (data->buffer) - data->pos);

  data->pos += size;

  if (data->pos >= GST_BUFFER_SIZE (data->buffer)) {
    GstBuffer *buf;

    buf = gst_collectpads_pop (pads, data);
    gst_buffer_unref (buf);
  }

  return flushsize;
}

static gboolean
gst_collectpads_event (GstPad * pad, GstEvent * event)
{
  GstCollectData *data;
  GstCollectPads *pads;

  /* some magic to get the managing collectpads */
  data = (GstCollectData *) gst_pad_get_element_private (pad);
  if (data == NULL)
    goto not_ours;

  pads = data->collect;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn ret = GST_FLOW_OK;

      GST_LOCK (pads);

      pads->eospads++;

      /* if all pads are EOS and we have a function, call it */
      if ((pads->eospads == pads->numpads) && pads->func) {
        ret = pads->func (pads, pads->user_data);
      }

      GST_UNLOCK (pads);

      /* We eat this event */
      gst_event_unref (event);
      return TRUE;
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 segment_start, segment_stop, stream_time;
      gdouble segment_rate;
      GstFormat format;
      gboolean update;

      gst_event_parse_newsegment (event, &update, &segment_rate, &format,
          &segment_start, &segment_stop, &stream_time);

      if (format == GST_FORMAT_TIME) {
        data->segment_start = segment_start;
        data->segment_stop = segment_stop;
        data->stream_time = stream_time;
      }

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
    GST_DEBUG ("collectpads not ours");
    return FALSE;
  }
}


static GstFlowReturn
gst_collectpads_chain (GstPad * pad, GstBuffer * buffer)
{
  GstCollectData *data;
  GstCollectPads *pads;
  guint64 size;
  GstFlowReturn ret;

  GST_DEBUG ("chain");

  /* some magic to get the managing collectpads */
  data = (GstCollectData *) gst_pad_get_element_private (pad);
  if (data == NULL)
    goto not_ours;

  pads = data->collect;
  size = GST_BUFFER_SIZE (buffer);

  GST_LOCK (pads);

  /* if not started, bail out */
  if (!pads->started)
    goto not_started;

  /* Call the collected callback until a pad with a buffer is popped. */
  while (((pads->queuedpads + pads->eospads) == pads->numpads) && pads->func)
    ret = pads->func (pads, pads->user_data);

  /* queue buffer on this pad, block if filled */
  while (data->buffer != NULL) {
    GST_COLLECTPADS_WAIT (pads);
    /* after a signal,  we could be stopped */
    if (!pads->started)
      goto not_started;
  }
  pads->queuedpads++;
  gst_buffer_replace (&data->buffer, buffer);

  /* if all pads have data and we have a function, call it */
  if (((pads->queuedpads + pads->eospads) == pads->numpads) && pads->func) {
    ret = pads->func (pads, pads->user_data);
  } else {
    ret = GST_FLOW_OK;
  }
  GST_UNLOCK (pads);

  return ret;

  /* ERRORS */
not_ours:
  {
    GST_DEBUG ("collectpads not ours");
    return GST_FLOW_ERROR;
  }
not_started:
  {
    GST_UNLOCK (pads);
    GST_DEBUG ("collectpads not started");
    return GST_FLOW_WRONG_STATE;
  }
}
