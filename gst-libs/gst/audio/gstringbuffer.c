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
  ringbuffer->playseg = -1;
  ringbuffer->writeseg = -1;
  ringbuffer->segfilled = 0;
  ringbuffer->freeseg = -1;
  ringbuffer->segplayed = 0;
  ringbuffer->cond = g_cond_new ();
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

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (ringbuffer));
}

/**
 * gst_ringbuffer_set_callback:
 * @buf: the #GstRingBuffer to set the callback on
 * @cb: the callback to set
 * @data: use data passed to the callback
 *
 * Sets the given callback function on the buffer. This function
 * will be called every time a segment has been written to a device.
 *
 * MT safe.
 */
void
gst_ringbuffer_set_callback (GstRingBuffer * buf, GstRingBufferCallback cb,
    gpointer data)
{
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
    buf->freeseg = spec->segtotal;
    if (buf->spec.bytes_per_sample != 0) {
      buf->samples_per_seg = buf->spec.segsize / buf->spec.bytes_per_sample;
    } else {
      g_warning ("invalid bytes_per_sample from acquire ringbuffer");
      buf->samples_per_seg = buf->spec.segsize;
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

  if (!res) {
    buf->acquired = TRUE;
  }

done:
  GST_UNLOCK (buf);

  return res;
}

static gboolean
gst_ringbuffer_play_unlocked (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  /* if paused, set to playing */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RINGBUFFER_STATE_STOPPED, GST_RINGBUFFER_STATE_PLAYING);

  if (!res) {
    /* was not stopped */
    res = TRUE;
    goto done;
  }

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->play)
    res = rclass->play (buf);

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_PAUSED;
  }
done:
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

  GST_LOCK (buf);
  res = gst_ringbuffer_play_unlocked (buf);
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
 * gst_ringbuffer_resume:
 * @buf: the #GstRingBuffer to resume
 *
 * Resume playing samples from the ringbuffer in the paused state.
 *
 * Returns: TRUE if the device could be paused, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ringbuffer_resume (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  GST_LOCK (buf);
  /* if playing, set to paused */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RINGBUFFER_STATE_PAUSED, GST_RINGBUFFER_STATE_PLAYING);

  if (!res) {
    /* was not paused */
    res = TRUE;
    goto done;
  }

  /* signal any waiters */
  GST_RINGBUFFER_SIGNAL (buf);

  rclass = GST_RINGBUFFER_GET_CLASS (buf);
  if (rclass->resume)
    res = rclass->resume (buf);

  if (!res) {
    buf->state = GST_RINGBUFFER_STATE_PAUSED;
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
    buf->segfilled = 0;
    buf->playseg = -1;
    buf->writeseg = -1;
    buf->freeseg = buf->spec.segtotal;
    buf->segplayed = 0;
  }
done:
  GST_UNLOCK (buf);

  return res;
}

/**
 * gst_ringbuffer_callback:
 * @buf: the #GstRingBuffer to callback
 * @advance: the number of segments written
 *
 * Subclasses should call this function to notify the fact that 
 * @advance segments are now played by the device.
 *
 * MT safe.
 */
void
gst_ringbuffer_callback (GstRingBuffer * buf, guint advance)
{
  gint prevfree;
  gint segtotal;

  if (advance == 0)
    return;

  segtotal = buf->spec.segtotal;

  /* update counter */
  g_atomic_int_add (&buf->segplayed, advance);

  /* update free segments counter */
  prevfree = g_atomic_int_exchange_and_add (&buf->freeseg, advance);
  if (prevfree + advance > segtotal) {
    g_warning ("underrun!! read %d, write %d, advance %d, free %d, prevfree %d",
        buf->playseg, buf->writeseg, advance, buf->freeseg, prevfree);
    buf->freeseg = segtotal;
    buf->writeseg = buf->playseg;
    /* make sure to signal */
    prevfree = -1;
  }

  buf->playseg = (buf->playseg + advance) % segtotal;

  if (prevfree == -1) {
    /* we need to take the lock to make sure the other thread is
     * blocking in the wait */
    GST_LOCK (buf);
    GST_RINGBUFFER_SIGNAL (buf);
    GST_UNLOCK (buf);
  }

  if (buf->callback)
    buf->callback (buf, advance, buf->cb_data);
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
  guint64 samples;
  guint delay;

  /* get the amount of segments we played */
  segplayed = g_atomic_int_get (&buf->segplayed);
  /* and the number of samples not yet played */
  delay = gst_ringbuffer_delay (buf);

  samples = (segplayed * buf->samples_per_seg) - delay;

  return samples;
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
 * Returns: The number of samples written to the ringbuffer.
 *
 * MT safe.
 */
/* FIXME, write the samples into the right position in the ringbuffer based
 * on the sample position argument 
 */
guint
gst_ringbuffer_commit (GstRingBuffer * buf, guint64 sample, guchar * data,
    guint len)
{
  guint towrite = len;
  gint segsize, segtotal;
  guint8 *dest;

  if (buf->data == NULL)
    goto no_buffer;

  dest = GST_BUFFER_DATA (buf->data);
  segsize = buf->spec.segsize;
  segtotal = buf->spec.segtotal;

  /* we write the complete buffer in chunks of segsize so that we can check for
   * a filled buffer after each segment. */
  while (towrite > 0) {
    gint segavail;
    gint segwrite;
    gint writeseg;
    gint segfilled;

    segfilled = buf->segfilled;

    /* check for partial buffer */
    if (G_LIKELY (segfilled == 0)) {
      gint prevfree;
      gint newseg;

      /* no partial buffer to fill up, allocate a new one */
      prevfree = g_atomic_int_exchange_and_add (&buf->freeseg, -1);
      if (prevfree == 0) {
        /* nothing was free */
        GST_DEBUG ("filled %d %d", buf->writeseg, buf->playseg);

        GST_LOCK (buf);
        /* buffer must be playing now or we deadlock since nobody is reading */
        if (g_atomic_int_get (&buf->state) != GST_RINGBUFFER_STATE_PLAYING)
          gst_ringbuffer_play_unlocked (buf);

        GST_RINGBUFFER_WAIT (buf);
        if (g_atomic_int_get (&buf->state) != GST_RINGBUFFER_STATE_PLAYING)
          goto not_playing;
        GST_UNLOCK (buf);
      }

      /* need to do this atomic as the reader updates the write pointer on
       * overruns */
      do {
        writeseg = g_atomic_int_get (&buf->writeseg);
        newseg = (writeseg + 1) % segtotal;
      } while (!g_atomic_int_compare_and_exchange (&buf->writeseg, writeseg,
              newseg));
      writeseg = newseg;
    } else {
      /* this is the segment we should write to */
      writeseg = g_atomic_int_get (&buf->writeseg);
    }
    if (writeseg < 0 || writeseg > segtotal) {
      g_warning ("invalid segment %d", writeseg);
      writeseg = 0;
    }

    /* this is the available size now in the current segment */
    segavail = segsize - segfilled;

    /* we write up to the available space */
    segwrite = MIN (segavail, towrite);

    memcpy (dest + writeseg * segsize + segfilled, data, segwrite);

    towrite -= segwrite;
    data += segwrite;

    if (segfilled + segwrite == segsize) {
      buf->segfilled = 0;
    } else {
      buf->segfilled = segfilled + segwrite;
    }
  }
  return len - towrite;

no_buffer:
  {
    GST_DEBUG ("no buffer");
    return -1;
  }
not_playing:
  {
    GST_UNLOCK (buf);
    GST_DEBUG ("stopped playing");
    return len - towrite;
  }
}

/**
 * gst_ringbuffer_prepare_read:
 * @buf: the #GstRingBuffer to read from
 * @segment: the segment to read
 *
 * Returns a pointer to memory where the data from segment @segment
 * can be found.
 *
 * MT safe.
 */
guint8 *
gst_ringbuffer_prepare_read (GstRingBuffer * buf, gint segment)
{
  guint8 *data;

  data = GST_BUFFER_DATA (buf->data);

  return data + (segment % buf->spec.segtotal) * buf->spec.segsize;
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

  data = GST_BUFFER_DATA (buf->data);

  memset (data + (segment % buf->spec.segtotal) * buf->spec.segsize, 0,
      buf->spec.segsize);
}
