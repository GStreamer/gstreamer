/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstringbuffer.c: 
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

#include <string.h>

#include "gstringbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_ringbuffer_debug);
#define GST_CAT_DEFAULT gst_ringbuffer_debug

static void gst_ringbuffer_class_init (GstRingBufferClass * klass);
static void gst_ringbuffer_init (GstRingBuffer * ringbuffer);
static void gst_ringbuffer_dispose (GObject * object);
static void gst_ringbuffer_finalize (GObject * object);

static GstObjectClass *parent_class = NULL;

/* ringbuffer abstract base class */
GType
gst_ringbuffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ringbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstRingBuffer),
      0,
      (GInstanceInitFunc) gst_ringbuffer_init,
      NULL
    };

    ringbuffer_type = g_type_register_static (GST_TYPE_OBJECT, "GstRingBuffer",
        &ringbuffer_info, G_TYPE_FLAG_ABSTRACT);

    GST_DEBUG_CATEGORY_INIT (gst_ringbuffer_debug, "ringbuffer", 0,
        "ringbuffer class");
  }
  return ringbuffer_type;
}

static void
gst_ringbuffer_class_init (GstRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ringbuffer_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ringbuffer_finalize);
}

static void
gst_ringbuffer_init (GstRingBuffer * ringbuffer)
{
  ringbuffer->acquired = FALSE;
  ringbuffer->state = GST_RINGBUFFER_STATE_STOPPED;
  ringbuffer->cond = g_cond_new ();
  ringbuffer->waiting = 0;
  ringbuffer->empty_seg = NULL;
}

static void
gst_ringbuffer_dispose (GObject * object)
{
  GstRingBuffer *ringbuffer = GST_RINGBUFFER (object);

  G_OBJECT_CLASS (parent_class)->dispose (G_OBJECT (ringbuffer));
}

static void
gst_ringbuffer_finalize (GObject * object)
{
  GstRingBuffer *ringbuffer = GST_RINGBUFFER (object);

  g_cond_free (ringbuffer->cond);
  g_free (ringbuffer->empty_seg);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (ringbuffer));
}

/**
 * gst_ringbuffer_set_callback:
 * @buf: the #GstRingBuffer to set the callback on
 * @cb: the callback to set
 * @user_data: user data passed to the callback
 *
 * Sets the given callback function on the buffer. This function
 * will be called every time a segment has been written to a device.
 *
 * MT safe.
 */
void
gst_ringbuffer_set_callback (GstRingBuffer * buf, GstRingBufferCallback cb,
    gpointer user_data)
{
  g_return_if_fail (buf != NULL);

  GST_LOCK (buf);
  buf->callback = cb;
  buf->cb_data = data;
  GST_UNLOCK (buf);
}

/**
 * gst_ringbuffer_acquire:
 * @buf: the #GstRingBuffer to acquire
 * @spec: the specs of the buffer
 *
 * Allocate the resources for the ringbuffer. This function fills
 * in the data pointer of the ring buffer with a valid #GstBuffer
 * to which samples can be written.
 *
 * Returns: TRUE if the device could be acquired, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_LOCK (buf);
  if (buf->acquired) {
    res = TRUE;
    goto done;
  }
  buf->acquired = TRUE;

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->acquire)
    res = rclass->acquire (buf, spec);

  if (!res) {
    buf->acquired = FALSE;
  } else {
    if (buf->spec.bytes_per_sample != 0) {
      gint i, j;

      buf->samples_per_seg = buf->spec.segsize / buf->spec.bytes_per_sample;

      /* create an empty segment */
      g_free (buf->empty_seg);
      buf->empty_seg = g_malloc (buf->spec.segsize);
      for (i = 0, j = 0; i < buf->spec.segsize; i++) {
        buf->empty_seg[i] = buf->spec.silence_sample[j];
        j = (j + 1) % buf->spec.bytes_per_sample;
      }
      /* set sample position to 0 */
      gst_ringbuffer_set_sample (buf, 0);
    } else {
      g_warning
          ("invalid bytes_per_sample from acquire ringbuffer, fix the element");
      buf->acquired = FALSE;
      res = FALSE;
    }
  }
done:
  GST_UNLOCK (buf);

  return res;
}

/**
 * gst_ringbuffer_release:
 * @buf: the #GstRingBuffer to release
 *
 * Free the resources of the ringbuffer.
 *
 * Returns: TRUE if the device could be released, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_release (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (buf != NULL, FALSE);

  gst_ringbuffer_stop (buf);

  GST_LOCK (buf);
  if (!buf->acquired) {
    res = TRUE;
    goto done;
  }
  buf->acquired = FALSE;

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->release)
    res = rclass->release (buf);

  /* signal any waiters */
  GST_RINGBUFFER_SIGNAL (buf);

  if (!res) {
    buf->acquired = TRUE;
  } else {
    g_free (buf->empty_seg);
    buf->empty_seg = NULL;
  }

done:
  GST_UNLOCK (buf);

  return res;
}

/**
 * gst_ringbuffer_is_acquired:
 * @buf: the #GstRingBuffer to check
 *
 * Check if the ringbuffer is acquired and ready to use.
 *
 * Returns: TRUE if the ringbuffer is acquired, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_is_acquired (GstRingBuffer * buf)
{
  gboolean res;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_LOCK (buf);
  res = buf->acquired;
  GST_UNLOCK (buf);

  return res;
}


/**
 * gst_ringbuffer_play:
 * @buf: the #GstRingBuffer to play
 *
 * Start playing samples from the ringbuffer.
 *
 * Returns: TRUE if the device could be started, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_play (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;
  gboolean resume = FALSE;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_LOCK (buf);
  /* if paused, set to playing */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RINGBUFFER_STATE_STOPPED, GST_RINGBUFFER_STATE_PLAYING);

  if (!res) {
    /* was not stopped, try from paused */
    res = g_atomic_int_compare_and_exchange (&buf->state,
        GST_RINGBUFFER_STATE_PAUSED, GST_RINGBUFFER_STATE_PLAYING);
    if (!res) {
      /* was not paused either, must be playing then */
      res = TRUE;
      goto done;
    }
    resume = TRUE;
  }

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (resume) {
    if (rclass->resume)
      res = rclass->resume (buf);
  } else {
    if (rclass->play)
      res = rclass->play (buf);
  }

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_PAUSED;
  }

done:
  GST_UNLOCK (buf);

  return res;
}

/**
 * gst_ringbuffer_pause:
 * @buf: the #GstRingBuffer to pause
 *
 * Pause playing samples from the ringbuffer.
 *
 * Returns: TRUE if the device could be paused, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_pause (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_LOCK (buf);
  /* if playing, set to paused */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RINGBUFFER_STATE_PLAYING, GST_RINGBUFFER_STATE_PAUSED);

  if (!res) {
    /* was not playing */
    res = TRUE;
    goto done;
  }

  /* signal any waiters */
  GST_RINGBUFFER_SIGNAL (buf);

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->pause)
    res = rclass->pause (buf);

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_PLAYING;
  }
done:
  GST_UNLOCK (buf);

  return res;
}

/**
 * gst_ringbuffer_stop:
 * @buf: the #GstRingBuffer to stop
 *
 * Stop playing samples from the ringbuffer.
 *
 * Returns: TRUE if the device could be stopped, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_stop (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_LOCK (buf);
  /* if playing, set to stopped */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RINGBUFFER_STATE_PLAYING, GST_RINGBUFFER_STATE_STOPPED);

  if (!res) {
    /* was not playing, must be stopped then */
    res = TRUE;
    goto done;
  }

  /* signal any waiters */
  GST_RINGBUFFER_SIGNAL (buf);

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->stop)
    res = rclass->stop (buf);

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_PLAYING;
  } else {
    gst_ringbuffer_set_sample (buf, 0);
  }
done:
  GST_UNLOCK (buf);

  return res;
}

/**
 * gst_ringbuffer_delay:
 * @buf: the #GstRingBuffer to query
 *
 * Get the number of samples queued in the audio device. This is
 * usually less than the segment size but can be bigger when the
 * implementation uses another internal buffer between the audio
 * device.
 *
 * Returns: The number of samples queued in the audio device.
 *
 * MT safe.
 */
guint
gst_ringbuffer_delay (GstRingBuffer * buf)
{
  GstRingBufferClass *rclass;
  guint res = 0;

  g_return_val_if_fail (buf != NULL, 0);

  if (!gst_ringbuffer_is_acquired (buf))
    return 0;

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->delay)
    res = rclass->delay (buf);

  return res;
}

/**
 * gst_ringbuffer_played_samples:
 * @buf: the #GstRingBuffer to query
 *
 * Get the number of samples that were played by the ringbuffer
 * since it was last started.
 *
 * Returns: The number of samples played by the ringbuffer.
 *
 * MT safe.
 */
guint64
gst_ringbuffer_played_samples (GstRingBuffer * buf)
{
  gint segplayed;
  guint64 raw, samples;
  guint delay;

  g_return_val_if_fail (buf != NULL, 0);

  /* get the amount of segments we played */
  segplayed = g_atomic_int_get (&buf->segplayed);

  /* and the number of samples not yet played */
  delay = gst_ringbuffer_delay (buf);

  samples = (segplayed * buf->samples_per_seg);
  raw = samples;

  if (samples >= delay)
    samples -= delay;

  GST_DEBUG ("played samples: raw %llu, delay %u, real %llu", raw, delay,
      samples);

  return samples;
}

/**
 * gst_ringbuffer_set_sample:
 * @buf: the #GstRingBuffer to use
 * @sample: the sample number to set
 *
 * Make sure that the next sample written to the device is
 * accounted for as being the @sample sample written to the
 * device. This value will be used in reporting the current
 * sample position of the ringbuffer.
 *
 * This function will also clear the buffer with silence.
 *
 * MT safe.
 */
void
gst_ringbuffer_set_sample (GstRingBuffer * buf, guint64 sample)
{
  gint i;

  g_return_if_fail (buf != NULL);

  if (sample == -1)
    sample = 0;

  /* FIXME, we assume the ringbuffer can restart at a random 
   * position, round down to the beginning and keep track of
   * offset when calculating the played samples. */
  buf->segplayed = sample / buf->samples_per_seg;
  buf->next_sample = sample;

  for (i = 0; i < buf->spec.segtotal; i++) {
    gst_ringbuffer_clear (buf, i);
  }

  GST_DEBUG ("setting sample to %llu, segplayed %d", sample, buf->segplayed);
}

static gboolean
wait_segment (GstRingBuffer * buf)
{
  /* buffer must be playing now or we deadlock since nobody is reading */
  if (g_atomic_int_get (&buf->state) != GST_RINGBUFFER_STATE_PLAYING) {
    GST_DEBUG ("play!");
    gst_ringbuffer_play (buf);
  }

  /* take lock first, then update our waiting flag */
  GST_LOCK (buf);
  if (g_atomic_int_compare_and_exchange (&buf->waiting, 0, 1)) {
    GST_DEBUG ("waiting..");
    if (g_atomic_int_get (&buf->state) != GST_RINGBUFFER_STATE_PLAYING)
      goto not_playing;

    GST_RINGBUFFER_WAIT (buf);

    if (g_atomic_int_get (&buf->state) != GST_RINGBUFFER_STATE_PLAYING)
      goto not_playing;
  }
  GST_UNLOCK (buf);

  return TRUE;

  /* ERROR */
not_playing:
  {
    GST_UNLOCK (buf);
    GST_DEBUG ("stopped playing");
    return FALSE;
  }
}

/**
 * gst_ringbuffer_commit:
 * @buf: the #GstRingBuffer to commit
 * @sample: the sample position of the data
 * @data: the data to commit
 * @len: the length of the data to commit
 *
 * Commit @length samples pointed to by @data to the ringbuffer
 * @buf. The first sample should be written at position @sample in
 * the ringbuffer.
 *
 * @len should not be a multiple of the segment size of the ringbuffer
 * although it is recommended.
 *
 * Returns: The number of samples written to the ringbuffer or -1 on
 * error.
 *
 * MT safe.
 */
guint
gst_ringbuffer_commit (GstRingBuffer * buf, guint64 sample, guchar * data,
    guint len)
{
  gint segplayed;
  gint segsize, segtotal, bps, sps;
  guint8 *dest;

  g_return_val_if_fail (buf != NULL, -1);
  g_return_val_if_fail (buf->data != NULL, -1);
  g_return_val_if_fail (data != NULL, -1);

  if (sample == -1) {
    /* play aligned with last sample */
    sample = buf->next_sample;
  } else {
    if (sample != buf->next_sample) {
      GST_WARNING ("discontinuity found got %" G_GUINT64_FORMAT
          ", expected %" G_GUINT64_FORMAT, sample, buf->next_sample);
    }
  }

  dest = GST_BUFFER_DATA (buf->data);
  segsize = buf->spec.segsize;
  segtotal = buf->spec.segtotal;
  bps = buf->spec.bytes_per_sample;
  sps = buf->samples_per_seg;

  /* we assume the complete buffer will be consumed and the next sample
   * should be written after this */
  buf->next_sample = sample + len / bps;

  /* write out all bytes */
  while (len > 0) {
    gint writelen;
    gint writeseg, writeoff;

    /* figure out the segment and the offset inside the segment where
     * the sample should be written. */
    writeseg = sample / sps;
    writeoff = (sample % sps) * bps;

    while (TRUE) {
      gint diff;

      /* get the currently playing segment */
      segplayed = g_atomic_int_get (&buf->segplayed);

      /* see how far away it is from the write segment */
      diff = writeseg - segplayed;

      GST_DEBUG
          ("pointer at %d, sample %llu, write to %d-%d, len %d, diff %d, segtotal %d, segsize %d",
          segplayed, sample, writeseg, writeoff, len, diff, segtotal, segsize);

      /* play segment too far ahead, we need to drop */
      if (diff < 0) {
        /* we need to drop one segment at a time, pretend we wrote a
         * segment. */
        writelen = MIN (segsize, len);
        goto next;
      }

      /* write segment is within writable range, we can break the loop and
       * start writing the data. */
      if (diff < segtotal)
        break;

      /* else we need to wait for the segment to become writable. */
      if (!wait_segment (buf))
        goto not_playing;
    }

    /* we can write now */
    writeseg = writeseg % segtotal;
    writelen = MIN (segsize - writeoff, len);

    GST_DEBUG ("write @%p seg %d, off %d, len %d",
        dest + writeseg * segsize, writeseg, writeoff, writelen);

    memcpy (dest + writeseg * segsize + writeoff, data, writelen);

  next:
    len -= writelen;
    data += writelen;
    sample += writelen / bps;
  }

  return len;

  /* ERRORS */
not_playing:
  {
    GST_DEBUG ("stopped playing");
    return -1;
  }
}

/**
 * gst_ringbuffer_prepare_read:
 * @buf: the #GstRingBuffer to read from
 * @segment: the segment to read
 * @readptr: the pointer to the memory where samples can be read
 * @len: the number of bytes to read
 *
 * Returns a pointer to memory where the data from segment @segment
 * can be found. This function is used by subclasses.
 *
 * Returns: FALSE if the buffer is not playing.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_prepare_read (GstRingBuffer * buf, gint * segment,
    guint8 ** readptr, gint * len)
{
  guint8 *data;
  gint segplayed;

  /* buffer must be playing */
  if (g_atomic_int_get (&buf->state) != GST_RINGBUFFER_STATE_PLAYING)
    return FALSE;

  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (buf->data != NULL, FALSE);
  g_return_val_if_fail (readptr != NULL, FALSE);
  g_return_val_if_fail (len != NULL, FALSE);

  data = GST_BUFFER_DATA (buf->data);

  /* get the position of the play pointer */
  segplayed = g_atomic_int_get (&buf->segplayed);

  *segment = segplayed % buf->spec.segtotal;
  *len = buf->spec.segsize;
  *readptr = data + *segment * *len;

  /* callback to fill the memory with data */
  if (buf->callback)
    buf->callback (buf, *readptr, *len, buf->cb_data);

  GST_DEBUG ("prepare read from segment %d (real %d) @%p",
      *segment, segplayed, *readptr);

  return TRUE;
}

/**
 * gst_ringbuffer_advance:
 * @buf: the #GstRingBuffer to advance
 * @advance: the number of segments written
 *
 * Subclasses should call this function to notify the fact that 
 * @advance segments are now played by the device.
 *
 * MT safe.
 */
void
gst_ringbuffer_advance (GstRingBuffer * buf, guint advance)
{
  g_return_if_fail (buf != NULL);

  /* update counter */
  g_atomic_int_add (&buf->segplayed, advance);

  /* the lock is already taken when the waiting flag is set,
   * we grab the lock as well to make sure the waiter is actually
   * waiting for the signal */
  if (g_atomic_int_compare_and_exchange (&buf->waiting, 1, 0)) {
    GST_LOCK (buf);
    GST_DEBUG ("signal waiter");
    GST_RINGBUFFER_SIGNAL (buf);
    GST_UNLOCK (buf);
  }
}

/**
 * gst_ringbuffer_clear:
 * @buf: the #GstRingBuffer to clear
 * @segment: the segment to clear
 *
 * Clear the given segment of the buffer with silence samples.
 * This function is used by subclasses.
 *
 * MT safe.
 */
void
gst_ringbuffer_clear (GstRingBuffer * buf, gint segment)
{
  guint8 *data;

  g_return_if_fail (buf != NULL);
  g_return_if_fail (buf->data != NULL);
  g_return_if_fail (buf->empty_seg != NULL);

  data = GST_BUFFER_DATA (buf->data);
  data += (segment % buf->spec.segtotal) * buf->spec.segsize;

  GST_DEBUG ("clear segment %d @%p", segment, data);

  memcpy (data, buf->empty_seg, buf->spec.segsize);
}
