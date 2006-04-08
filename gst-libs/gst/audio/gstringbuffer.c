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
/**
 * SECTION:gstringbuffer
 * @short_description: Base class for audio ringbuffer implementations
 * @see_also: gstbaseaudiosink
 *
 * <refsect2>
 * <para>
 * This object is the base class for audio ringbuffers used by the base
 * audio source and sink classes.
 * </para>
 * <para>
 * The ringbuffer abstracts a circular buffer of data. One reader and
 * one writer can operate on the data from different threads in a lockfree 
 * manner. The base class is sufficiently flexible to be used as an
 * abstraction for DMA based ringbuffers as well as a pure software 
 * implementations.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-02-02 (0.10.4)
 */

#include <string.h>

#include "gstringbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_ring_buffer_debug);
#define GST_CAT_DEFAULT gst_ring_buffer_debug

static void gst_ring_buffer_class_init (GstRingBufferClass * klass);
static void gst_ring_buffer_init (GstRingBuffer * ringbuffer);
static void gst_ring_buffer_dispose (GObject * object);
static void gst_ring_buffer_finalize (GObject * object);

static gboolean gst_ring_buffer_pause_unlocked (GstRingBuffer * buf);

static GstObjectClass *parent_class = NULL;

/* ringbuffer abstract base class */
GType
gst_ring_buffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (G_UNLIKELY (!ringbuffer_type)) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ring_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstRingBuffer),
      0,
      (GInstanceInitFunc) gst_ring_buffer_init,
      NULL
    };

    ringbuffer_type = g_type_register_static (GST_TYPE_OBJECT, "GstRingBuffer",
        &ringbuffer_info, G_TYPE_FLAG_ABSTRACT);

    GST_DEBUG_CATEGORY_INIT (gst_ring_buffer_debug, "ringbuffer", 0,
        "ringbuffer class");
  }
  return ringbuffer_type;
}

static void
gst_ring_buffer_class_init (GstRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ring_buffer_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ring_buffer_finalize);
}

static void
gst_ring_buffer_init (GstRingBuffer * ringbuffer)
{
  ringbuffer->open = FALSE;
  ringbuffer->acquired = FALSE;
  ringbuffer->state = GST_RING_BUFFER_STATE_STOPPED;
  ringbuffer->cond = g_cond_new ();
  ringbuffer->waiting = 0;
  ringbuffer->empty_seg = NULL;
  ringbuffer->abidata.ABI.flushing = TRUE;
}

static void
gst_ring_buffer_dispose (GObject * object)
{
  GstRingBuffer *ringbuffer = GST_RING_BUFFER (object);

  G_OBJECT_CLASS (parent_class)->dispose (G_OBJECT (ringbuffer));
}

static void
gst_ring_buffer_finalize (GObject * object)
{
  GstRingBuffer *ringbuffer = GST_RING_BUFFER (object);

  g_cond_free (ringbuffer->cond);
  g_free (ringbuffer->empty_seg);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (ringbuffer));
}

static const int linear_formats[4 * 2 * 2] = {
  GST_S8,
  GST_S8,
  GST_U8,
  GST_U8,
  GST_S16_LE,
  GST_S16_BE,
  GST_U16_LE,
  GST_U16_BE,
  GST_S24_LE,
  GST_S24_BE,
  GST_U24_LE,
  GST_U24_BE,
  GST_S32_LE,
  GST_S32_BE,
  GST_U32_LE,
  GST_U32_BE
};

static const int linear24_formats[3 * 2 * 2] = {
  GST_S24_3LE,
  GST_S24_3BE,
  GST_U24_3LE,
  GST_U24_3BE,
  GST_S20_3LE,
  GST_S20_3BE,
  GST_U20_3LE,
  GST_U20_3BE,
  GST_S18_3LE,
  GST_S18_3BE,
  GST_U18_3LE,
  GST_U18_3BE,
};

static GstBufferFormat
build_linear_format (int depth, int width, int unsignd, int big_endian)
{
  const gint *formats;

  if (width == 24) {
    switch (depth) {
      case 24:
        formats = &linear24_formats[0];
        break;
      case 20:
        formats = &linear24_formats[4];
        break;
      case 18:
        formats = &linear24_formats[8];
        break;
      default:
        return GST_UNKNOWN;
    }
  } else {
    switch (depth) {
      case 8:
        formats = &linear_formats[0];
        break;
      case 16:
        formats = &linear_formats[4];
        break;
      case 24:
        formats = &linear_formats[8];
        break;
      case 32:
        formats = &linear_formats[12];
        break;
      default:
        return GST_UNKNOWN;
    }
  }
  if (unsignd)
    formats += 2;
  if (big_endian)
    formats += 1;
  return (GstBufferFormat) * formats;
}

/**
 * gst_ring_buffer_debug_spec_caps:
 * @spec: the spec to debug
 *
 * Print debug info about the parsed caps in @spec to the debug log.
 */
void
gst_ring_buffer_debug_spec_caps (GstRingBufferSpec * spec)
{
  GST_DEBUG ("spec caps: %p %" GST_PTR_FORMAT, spec->caps, spec->caps);
  GST_DEBUG ("parsed caps: type:         %d", spec->type);
  GST_DEBUG ("parsed caps: format:       %d", spec->format);
  GST_DEBUG ("parsed caps: width:        %d", spec->width);
  GST_DEBUG ("parsed caps: depth:        %d", spec->depth);
  GST_DEBUG ("parsed caps: sign:         %d", spec->sign);
  GST_DEBUG ("parsed caps: bigend:       %d", spec->bigend);
  GST_DEBUG ("parsed caps: rate:         %d", spec->rate);
  GST_DEBUG ("parsed caps: channels:     %d", spec->channels);
  GST_DEBUG ("parsed caps: sample bytes: %d", spec->bytes_per_sample);
}

/**
 * gst_ring_buffer_debug_spec_buff:
 * @spec: the spec to debug
 *
 * Print debug info about the buffer sized in @spec to the debug log.
 */
void
gst_ring_buffer_debug_spec_buff (GstRingBufferSpec * spec)
{
  GST_DEBUG ("acquire ringbuffer: buffer time: %" G_GINT64_FORMAT " usec",
      spec->buffer_time);
  GST_DEBUG ("acquire ringbuffer: latency time: %" G_GINT64_FORMAT " usec",
      spec->latency_time);
  GST_DEBUG ("acquire ringbuffer: total segments: %d", spec->segtotal);
  GST_DEBUG ("acquire ringbuffer: segment size: %d bytes = %d samples",
      spec->segsize, spec->segsize / spec->bytes_per_sample);
  GST_DEBUG ("acquire ringbuffer: buffer size: %d bytes = %d samples",
      spec->segsize * spec->segtotal,
      spec->segsize * spec->segtotal / spec->bytes_per_sample);
}

/**
 * gst_ring_buffer_parse_caps:
 * @spec: a spec
 * @caps: a #GstCaps
 *
 * Parse @caps into @spec.
 *
 * Returns: TRUE if the caps could be parsed.
 */
gboolean
gst_ring_buffer_parse_caps (GstRingBufferSpec * spec, GstCaps * caps)
{
  const gchar *mimetype;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  /* we have to differentiate between int and float formats */
  mimetype = gst_structure_get_name (structure);

  if (!strncmp (mimetype, "audio/x-raw-int", 15)) {
    gint endianness;

    spec->type = GST_BUFTYPE_LINEAR;

    /* extract the needed information from the cap */
    if (!(gst_structure_get_int (structure, "width", &spec->width) &&
            gst_structure_get_int (structure, "depth", &spec->depth) &&
            gst_structure_get_boolean (structure, "signed", &spec->sign)))
      goto parse_error;

    /* extract endianness if needed */
    if (spec->width > 8) {
      if (!gst_structure_get_int (structure, "endianness", &endianness))
        goto parse_error;
    } else {
      endianness = G_BYTE_ORDER;
    }

    spec->bigend = endianness == G_LITTLE_ENDIAN ? FALSE : TRUE;

    spec->format =
        build_linear_format (spec->depth, spec->width, spec->sign ? 0 : 1,
        spec->bigend ? 1 : 0);
  } else if (!strncmp (mimetype, "audio/x-raw-float", 17)) {

    spec->type = GST_BUFTYPE_FLOAT;

    /* get layout */
    if (!gst_structure_get_int (structure, "width", &spec->width))
      goto parse_error;

    /* match layout to format wrt to endianness */
    switch (spec->width) {
      case 32:
        spec->format =
            G_BYTE_ORDER == G_LITTLE_ENDIAN ? GST_FLOAT32_LE : GST_FLOAT32_BE;
        break;
      case 64:
        spec->format =
            G_BYTE_ORDER == G_LITTLE_ENDIAN ? GST_FLOAT64_LE : GST_FLOAT64_BE;
        break;
      default:
        goto parse_error;
    }
  } else if (!strncmp (mimetype, "audio/x-alaw", 12)) {
    spec->type = GST_BUFTYPE_A_LAW;
    spec->format = GST_A_LAW;
    spec->width = 8;
    spec->depth = 8;
  } else if (!strncmp (mimetype, "audio/x-mulaw", 13)) {
    spec->type = GST_BUFTYPE_MU_LAW;
    spec->format = GST_MU_LAW;
    spec->width = 8;
    spec->depth = 8;
  } else {
    goto parse_error;
  }

  /* get rate and channels */
  if (!(gst_structure_get_int (structure, "rate", &spec->rate) &&
          gst_structure_get_int (structure, "channels", &spec->channels)))
    goto parse_error;

  spec->bytes_per_sample = (spec->width >> 3) * spec->channels;

  gst_caps_replace (&spec->caps, caps);

  g_return_val_if_fail (spec->latency_time != 0, FALSE);

  /* calculate suggested segsize and segtotal */
  spec->segsize =
      spec->rate * spec->bytes_per_sample * spec->latency_time / GST_MSECOND;
  spec->segtotal = spec->buffer_time / spec->latency_time;

  gst_ring_buffer_debug_spec_caps (spec);
  gst_ring_buffer_debug_spec_buff (spec);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    GST_DEBUG ("could not parse caps");
    return FALSE;
  }
}

/**
 * gst_ring_buffer_set_callback:
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
gst_ring_buffer_set_callback (GstRingBuffer * buf, GstRingBufferCallback cb,
    gpointer user_data)
{
  g_return_if_fail (buf != NULL);

  GST_OBJECT_LOCK (buf);
  buf->callback = cb;
  buf->cb_data = user_data;
  GST_OBJECT_UNLOCK (buf);
}


/**
 * gst_ring_buffer_open_device:
 * @buf: the #GstRingBuffer
 *
 * Open the audio device associated with the ring buffer. Does not perform any
 * setup on the device. You must open the device before acquiring the ring
 * buffer.
 *
 * Returns: TRUE if the device could be opened, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_open_device (GstRingBuffer * buf)
{
  gboolean res = TRUE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (GST_IS_RING_BUFFER (buf), FALSE);

  GST_DEBUG_OBJECT (buf, "opening device");

  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (buf->open))
    goto was_opened;

  buf->open = TRUE;

  /* if this fails, something is wrong in this file */
  g_assert (!buf->acquired);

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->open_device))
    res = rclass->open_device (buf);

  if (G_UNLIKELY (!res))
    goto open_failed;

  GST_DEBUG_OBJECT (buf, "opened device");

done:
  GST_OBJECT_UNLOCK (buf);

  return res;

  /* ERRORS */
was_opened:
  {
    GST_DEBUG_OBJECT (buf, "Device for ring buffer already open");
    g_warning ("Device for ring buffer %p already open, fix your code", buf);
    res = TRUE;
    goto done;
  }
open_failed:
  {
    buf->open = FALSE;
    GST_DEBUG_OBJECT (buf, "failed opening device");
    goto done;
  }
}

/**
 * gst_ring_buffer_close_device:
 * @buf: the #GstRingBuffer
 *
 * Close the audio device associated with the ring buffer. The ring buffer
 * should already have been released via gst_ring_buffer_release().
 *
 * Returns: TRUE if the device could be closed, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_close_device (GstRingBuffer * buf)
{
  gboolean res = TRUE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (GST_IS_RING_BUFFER (buf), FALSE);

  GST_DEBUG_OBJECT (buf, "closing device");

  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (!buf->open))
    goto was_closed;

  if (G_UNLIKELY (buf->acquired))
    goto was_acquired;

  buf->open = FALSE;

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->close_device))
    res = rclass->close_device (buf);

  if (G_UNLIKELY (!res))
    goto close_error;

  GST_DEBUG_OBJECT (buf, "closed device");

done:
  GST_OBJECT_UNLOCK (buf);

  return res;

  /* ERRORS */
was_closed:
  {
    GST_DEBUG_OBJECT (buf, "Device for ring buffer already closed");
    g_warning ("Device for ring buffer %p already closed, fix your code", buf);
    res = TRUE;
    goto done;
  }
was_acquired:
  {
    GST_DEBUG_OBJECT (buf, "Resources for ring buffer still acquired");
    g_critical ("Resources for ring buffer %p still acquired", buf);
    res = FALSE;
    goto done;
  }
close_error:
  {
    buf->open = TRUE;
    GST_DEBUG_OBJECT (buf, "error closing device");
    goto done;
  }
}

/**
 * gst_ring_buffer_device_is_open:
 * @buf: the #GstRingBuffer
 *
 * Checks the status of the device associated with the ring buffer.
 *
 * Returns: TRUE if the device was open, FALSE if it was closed.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_device_is_open (GstRingBuffer * buf)
{
  gboolean res = TRUE;

  g_return_val_if_fail (GST_IS_RING_BUFFER (buf), FALSE);

  GST_OBJECT_LOCK (buf);
  res = buf->open;
  GST_OBJECT_UNLOCK (buf);

  return res;
}


/**
 * gst_ring_buffer_acquire:
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
gst_ring_buffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;
  gint i, j;
  gint segsize, bps;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_DEBUG_OBJECT (buf, "acquiring device");

  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (!buf->open))
    goto not_opened;

  if (G_UNLIKELY (buf->acquired))
    goto was_acquired;

  buf->acquired = TRUE;

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->acquire))
    res = rclass->acquire (buf, spec);

  if (G_UNLIKELY (!res))
    goto acquire_failed;

  if (G_UNLIKELY ((bps = buf->spec.bytes_per_sample) == 0))
    goto invalid_bps;

  segsize = buf->spec.segsize;

  buf->samples_per_seg = segsize / bps;

  /* create an empty segment */
  g_free (buf->empty_seg);
  buf->empty_seg = g_malloc (segsize);
  for (i = 0, j = 0; i < segsize; i++) {
    buf->empty_seg[i] = buf->spec.silence_sample[j];
    j = (j + 1) % bps;
  }
  GST_DEBUG_OBJECT (buf, "acquired device");

done:
  GST_OBJECT_UNLOCK (buf);

  return res;

  /* ERRORS */
not_opened:
  {
    GST_DEBUG_OBJECT (buf, "device not opened");
    g_critical ("Device for %p not opened", buf);
    res = FALSE;
    goto done;
  }
was_acquired:
  {
    res = TRUE;
    GST_DEBUG_OBJECT (buf, "device was acquired");
    goto done;
  }
acquire_failed:
  {
    buf->acquired = FALSE;
    GST_DEBUG_OBJECT (buf, "failed to acquire device");
    goto done;
  }
invalid_bps:
  {
    g_warning
        ("invalid bytes_per_sample from acquire ringbuffer, fix the element");
    buf->acquired = FALSE;
    res = FALSE;
    goto done;
  }
}

/**
 * gst_ring_buffer_release:
 * @buf: the #GstRingBuffer to release
 *
 * Free the resources of the ringbuffer.
 *
 * Returns: TRUE if the device could be released, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_release (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_DEBUG_OBJECT (buf, "releasing device");

  gst_ring_buffer_stop (buf);

  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (!buf->acquired))
    goto was_released;

  buf->acquired = FALSE;

  /* if this fails, something is wrong in this file */
  g_assert (buf->open == TRUE);

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->release))
    res = rclass->release (buf);

  /* signal any waiters */
  GST_DEBUG_OBJECT (buf, "signal waiter");
  GST_RING_BUFFER_SIGNAL (buf);

  if (G_UNLIKELY (!res))
    goto release_failed;

  g_free (buf->empty_seg);
  buf->empty_seg = NULL;
  GST_DEBUG_OBJECT (buf, "released device");

done:
  GST_OBJECT_UNLOCK (buf);

  return res;

  /* ERRORS */
was_released:
  {
    res = TRUE;
    GST_DEBUG_OBJECT (buf, "device was released");
    goto done;
  }
release_failed:
  {
    buf->acquired = TRUE;
    GST_DEBUG_OBJECT (buf, "failed to release device");
    goto done;
  }
}

/**
 * gst_ring_buffer_is_acquired:
 * @buf: the #GstRingBuffer to check
 *
 * Check if the ringbuffer is acquired and ready to use.
 *
 * Returns: TRUE if the ringbuffer is acquired, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_is_acquired (GstRingBuffer * buf)
{
  gboolean res;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_OBJECT_LOCK (buf);
  res = buf->acquired;
  GST_OBJECT_UNLOCK (buf);

  return res;
}

/**
 * gst_ring_buffer_set_flushing:
 * @buf: the #GstRingBuffer to flush
 *
 * Set the ringbuffer to flushing mode or normal mode.
 *
 * MT safe.
 */
void
gst_ring_buffer_set_flushing (GstRingBuffer * buf, gboolean flushing)
{
  GST_OBJECT_LOCK (buf);
  buf->abidata.ABI.flushing = flushing;

  gst_ring_buffer_clear_all (buf);
  if (flushing) {
    gst_ring_buffer_pause_unlocked (buf);
  }
  GST_OBJECT_UNLOCK (buf);
}

/**
 * gst_ring_buffer_start:
 * @buf: the #GstRingBuffer to start
 *
 * Start processing samples from the ringbuffer.
 *
 * Returns: TRUE if the device could be started, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_start (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;
  gboolean resume = FALSE;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_DEBUG_OBJECT (buf, "starting ringbuffer");

  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (buf->abidata.ABI.flushing))
    goto flushing;

  /* if stopped, set to started */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RING_BUFFER_STATE_STOPPED, GST_RING_BUFFER_STATE_STARTED);

  if (!res) {
    /* was not stopped, try from paused */
    res = g_atomic_int_compare_and_exchange (&buf->state,
        GST_RING_BUFFER_STATE_PAUSED, GST_RING_BUFFER_STATE_STARTED);
    if (!res) {
      /* was not paused either, must be started then */
      res = TRUE;
      GST_DEBUG_OBJECT (buf, "was started");
      goto done;
    }
    resume = TRUE;
    GST_DEBUG_OBJECT (buf, "resuming");
  }

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (resume) {
    if (G_LIKELY (rclass->resume))
      res = rclass->resume (buf);
  } else {
    if (G_LIKELY (rclass->start))
      res = rclass->start (buf);
  }

  if (G_UNLIKELY (!res)) {
    buf->state = GST_RING_BUFFER_STATE_PAUSED;
    GST_DEBUG_OBJECT (buf, "failed to start");
  } else {
    GST_DEBUG_OBJECT (buf, "started");
  }

done:
  GST_OBJECT_UNLOCK (buf);

  return res;

flushing:
  {
    GST_OBJECT_UNLOCK (buf);
    return FALSE;
  }
}

static gboolean
gst_ring_buffer_pause_unlocked (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  GST_DEBUG_OBJECT (buf, "pausing ringbuffer");

  /* if started, set to paused */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RING_BUFFER_STATE_STARTED, GST_RING_BUFFER_STATE_PAUSED);

  if (!res)
    goto not_started;

  /* signal any waiters */
  GST_DEBUG_OBJECT (buf, "signal waiter");
  GST_RING_BUFFER_SIGNAL (buf);

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->pause))
    res = rclass->pause (buf);

  if (G_UNLIKELY (!res)) {
    buf->state = GST_RING_BUFFER_STATE_STARTED;
    GST_DEBUG_OBJECT (buf, "failed to pause");
  } else {
    GST_DEBUG_OBJECT (buf, "paused");
  }

  return res;

not_started:
  {
    /* was not started */
    GST_DEBUG_OBJECT (buf, "was not started");
    return TRUE;
  }
}

/**
 * gst_ring_buffer_pause:
 * @buf: the #GstRingBuffer to pause
 *
 * Pause processing samples from the ringbuffer.
 *
 * Returns: TRUE if the device could be paused, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_pause (GstRingBuffer * buf)
{
  gboolean res = FALSE;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (buf->abidata.ABI.flushing))
    goto flushing;

  res = gst_ring_buffer_pause_unlocked (buf);
  GST_OBJECT_UNLOCK (buf);

  return res;

flushing:
  {
    GST_OBJECT_UNLOCK (buf);
    return FALSE;
  }
}

/**
 * gst_ring_buffer_stop:
 * @buf: the #GstRingBuffer to stop
 *
 * Stop processing samples from the ringbuffer.
 *
 * Returns: TRUE if the device could be stopped, FALSE on error.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_stop (GstRingBuffer * buf)
{
  gboolean res = FALSE;
  GstRingBufferClass *rclass;

  g_return_val_if_fail (buf != NULL, FALSE);

  GST_DEBUG_OBJECT (buf, "stopping");

  GST_OBJECT_LOCK (buf);

  /* if started, set to stopped */
  res = g_atomic_int_compare_and_exchange (&buf->state,
      GST_RING_BUFFER_STATE_STARTED, GST_RING_BUFFER_STATE_STOPPED);

  if (!res) {
    /* was not started, must be stopped then */
    GST_DEBUG_OBJECT (buf, "was not started");
    res = TRUE;
    goto done;
  }

  /* signal any waiters */
  GST_DEBUG_OBJECT (buf, "signal waiter");
  GST_RING_BUFFER_SIGNAL (buf);

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->stop))
    res = rclass->stop (buf);

  if (G_UNLIKELY (!res)) {
    buf->state = GST_RING_BUFFER_STATE_STARTED;
    GST_DEBUG_OBJECT (buf, "failed to stop");
  } else {
    GST_DEBUG_OBJECT (buf, "stopped");
  }
done:
  GST_OBJECT_UNLOCK (buf);

  return res;
}

/**
 * gst_ring_buffer_delay:
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
gst_ring_buffer_delay (GstRingBuffer * buf)
{
  GstRingBufferClass *rclass;
  guint res;

  g_return_val_if_fail (buf != NULL, 0);

  res = 0;

  /* buffer must be acquired */
  if (G_UNLIKELY (!gst_ring_buffer_is_acquired (buf)))
    goto done;

  rclass = GST_RING_BUFFER_GET_CLASS (buf);
  if (G_LIKELY (rclass->delay))
    res = rclass->delay (buf);

done:
  return res;
}

/**
 * gst_ring_buffer_samples_done:
 * @buf: the #GstRingBuffer to query
 *
 * Get the number of samples that were processed by the ringbuffer
 * since it was last started.
 *
 * Returns: The number of samples processed by the ringbuffer.
 *
 * MT safe.
 */
guint64
gst_ring_buffer_samples_done (GstRingBuffer * buf)
{
  gint segdone;
  guint64 raw, samples;
  guint delay;

  g_return_val_if_fail (buf != NULL, 0);

  /* get the amount of segments we processed */
  segdone = g_atomic_int_get (&buf->segdone);

  /* and the number of samples not yet processed */
  delay = gst_ring_buffer_delay (buf);

  raw = samples = ((guint64) segdone) * buf->samples_per_seg;

  if (G_LIKELY (samples >= delay))
    samples -= delay;

  GST_DEBUG_OBJECT (buf, "processed samples: raw %llu, delay %u, real %llu",
      raw, delay, samples);

  return samples;
}

/**
 * gst_ring_buffer_set_sample:
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
gst_ring_buffer_set_sample (GstRingBuffer * buf, guint64 sample)
{
  g_return_if_fail (buf != NULL);

  if (sample == -1)
    sample = 0;

  if (G_UNLIKELY (buf->samples_per_seg == 0))
    return;

  /* FIXME, we assume the ringbuffer can restart at a random 
   * position, round down to the beginning and keep track of
   * offset when calculating the processed samples. */
  buf->segbase = buf->segdone - sample / buf->samples_per_seg;

  gst_ring_buffer_clear_all (buf);

  GST_DEBUG_OBJECT (buf, "set sample to %llu, segbase %d", sample,
      buf->segbase);
}

/**
 * gst_ring_buffer_clear_all:
 * @buf: the #GstRingBuffer to clear
 *
 * Fill the ringbuffer with silence.
 *
 * MT safe.
 */
void
gst_ring_buffer_clear_all (GstRingBuffer * buf)
{
  gint i;

  g_return_if_fail (buf != NULL);

  /* not fatal, we just are not negotiated yet */
  if (G_UNLIKELY (buf->spec.segtotal <= 0))
    return;

  GST_DEBUG_OBJECT (buf, "clear all segments");

  for (i = 0; i < buf->spec.segtotal; i++) {
    gst_ring_buffer_clear (buf, i);
  }
}


static gboolean
wait_segment (GstRingBuffer * buf)
{
  /* buffer must be started now or we deadlock since nobody is reading */
  if (G_UNLIKELY (g_atomic_int_get (&buf->state) !=
          GST_RING_BUFFER_STATE_STARTED)) {
    /* see if we are allowed to start it */
    if (G_UNLIKELY (g_atomic_int_get (&buf->abidata.ABI.may_start) == FALSE))
      goto no_start;

    GST_DEBUG_OBJECT (buf, "start!");
    gst_ring_buffer_start (buf);
  }

  /* take lock first, then update our waiting flag */
  GST_OBJECT_LOCK (buf);
  if (G_UNLIKELY (buf->abidata.ABI.flushing))
    goto flushing;

  if (G_UNLIKELY (g_atomic_int_get (&buf->state) !=
          GST_RING_BUFFER_STATE_STARTED))
    goto not_started;

  if (g_atomic_int_compare_and_exchange (&buf->waiting, 0, 1)) {
    GST_DEBUG_OBJECT (buf, "waiting..");
    GST_RING_BUFFER_WAIT (buf);

    if (G_UNLIKELY (buf->abidata.ABI.flushing))
      goto flushing;

    if (G_UNLIKELY (g_atomic_int_get (&buf->state) !=
            GST_RING_BUFFER_STATE_STARTED))
      goto not_started;
  }
  GST_OBJECT_UNLOCK (buf);

  return TRUE;

  /* ERROR */
not_started:
  {
    g_atomic_int_compare_and_exchange (&buf->waiting, 1, 0);
    GST_DEBUG_OBJECT (buf, "stopped processing");
    GST_OBJECT_UNLOCK (buf);
    return FALSE;
  }
flushing:
  {
    g_atomic_int_compare_and_exchange (&buf->waiting, 1, 0);
    GST_DEBUG_OBJECT (buf, "flushing");
    GST_OBJECT_UNLOCK (buf);
    return FALSE;
  }
no_start:
  {
    GST_DEBUG_OBJECT (buf, "not allowed to start");
    return FALSE;
  }
}

/**
 * gst_ring_buffer_commit:
 * @buf: the #GstRingBuffer to commit
 * @sample: the sample position of the data
 * @data: the data to commit
 * @len: the number of samples in the data to commit
 *
 * Commit @len samples pointed to by @data to the ringbuffer
 * @buf. The first sample should be written at position @sample in
 * the ringbuffer.
 *
 * @len does not need to be a multiple of the segment size of the ringbuffer
 * although it is recommended for optimal performance.
 *
 * Returns: The number of samples written to the ringbuffer or -1 on
 * error.
 *
 * MT safe.
 */
guint
gst_ring_buffer_commit (GstRingBuffer * buf, guint64 sample, guchar * data,
    guint len)
{
  gint segdone;
  gint segsize, segtotal, bps, sps;
  guint8 *dest;
  guint to_write;

  g_return_val_if_fail (buf != NULL, -1);
  g_return_val_if_fail (buf->data != NULL, -1);
  g_return_val_if_fail (data != NULL, -1);

  dest = GST_BUFFER_DATA (buf->data);
  segsize = buf->spec.segsize;
  segtotal = buf->spec.segtotal;
  bps = buf->spec.bytes_per_sample;
  sps = buf->samples_per_seg;

  to_write = len;
  /* write out all samples */
  while (to_write > 0) {
    gint sampleslen;
    gint writeseg, sampleoff;

    /* figure out the segment and the offset inside the segment where
     * the sample should be written. */
    writeseg = sample / sps;
    sampleoff = (sample % sps);

    while (TRUE) {
      gint diff;

      /* get the currently processed segment */
      segdone = g_atomic_int_get (&buf->segdone) - buf->segbase;

      /* see how far away it is from the write segment */
      diff = writeseg - segdone;

      GST_DEBUG
          ("pointer at %d, sample %llu, write to %d-%d, to_write %d, diff %d, segtotal %d, segsize %d",
          segdone, sample, writeseg, sampleoff, to_write, diff, segtotal, sps);

      /* segment too far ahead, we need to drop, hopefully UNLIKELY */
      if (G_UNLIKELY (diff < 0)) {
        /* we need to drop one segment at a time, pretend we wrote a
         * segment. */
        sampleslen = MIN (sps, to_write);
        goto next;
      }

      /* write segment is within writable range, we can break the loop and
       * start writing the data. */
      if (diff < segtotal)
        break;

      /* else we need to wait for the segment to become writable. */
      if (!wait_segment (buf))
        goto not_started;
    }

    /* we can write now */
    writeseg = writeseg % segtotal;
    sampleslen = MIN (sps - sampleoff, to_write);

    GST_DEBUG_OBJECT (buf, "write @%p seg %d, off %d, sampleslen %d",
        dest + writeseg * segsize, writeseg, sampleoff, sampleslen);

    memcpy (dest + (writeseg * segsize) + (sampleoff * bps), data,
        (sampleslen * bps));

  next:
    to_write -= sampleslen;
    sample += sampleslen;
    data += sampleslen * bps;
  }

  return len - to_write;

  /* ERRORS */
not_started:
  {
    GST_DEBUG_OBJECT (buf, "stopped processing");
    return len - to_write;
  }
}

/**
 * gst_ring_buffer_read:
 * @buf: the #GstRingBuffer to read from
 * @sample: the sample position of the data
 * @data: where the data should be read
 * @len: the number of samples in data to read
 *
 * Read @len samples from the ringbuffer into the memory pointed 
 * to by @data.
 * The first sample should be read from position @sample in
 * the ringbuffer.
 *
 * @len should not be a multiple of the segment size of the ringbuffer
 * although it is recommended.
 *
 * Returns: The number of samples read from the ringbuffer or -1 on
 * error.
 *
 * MT safe.
 */
guint
gst_ring_buffer_read (GstRingBuffer * buf, guint64 sample, guchar * data,
    guint len)
{
  gint segdone;
  gint segsize, segtotal, bps, sps;
  guint8 *dest;

  g_return_val_if_fail (buf != NULL, -1);
  g_return_val_if_fail (buf->data != NULL, -1);
  g_return_val_if_fail (data != NULL, -1);

  dest = GST_BUFFER_DATA (buf->data);
  segsize = buf->spec.segsize;
  segtotal = buf->spec.segtotal;
  bps = buf->spec.bytes_per_sample;
  sps = buf->samples_per_seg;

  /* read enough samples */
  while (len > 0) {
    gint sampleslen;
    gint readseg, sampleoff;

    /* figure out the segment and the offset inside the segment where
     * the sample should be written. */
    readseg = sample / sps;
    sampleoff = (sample % sps);

    while (TRUE) {
      gint diff;

      /* get the currently processed segment */
      segdone = g_atomic_int_get (&buf->segdone) - buf->segbase;

      /* see how far away it is from the read segment */
      diff = segdone - readseg;

      GST_DEBUG
          ("pointer at %d, sample %llu, read from %d-%d, len %d, diff %d, segtotal %d, segsize %d",
          segdone, sample, readseg, sampleoff, len, diff, segtotal, segsize);

      /* segment too far ahead, we need to drop */
      if (diff < 0) {
        /* we need to drop one segment at a time, pretend we read an
         * empty segment. */
        sampleslen = MIN (sps, len);
        memcpy (data, buf->empty_seg, sampleslen * bps);
        goto next;
      }

      /* read segment is within readable range, we can break the loop and
       * start reading the data. */
      if (diff > 0 && diff < segtotal)
        break;

      /* flush if diff has grown bigger than ringbuffer */
      if (diff >= segtotal) {
        gst_ring_buffer_clear_all (buf);
        buf->segdone = readseg;
      }

      /* else we need to wait for the segment to become readable. */
      if (!wait_segment (buf))
        goto not_started;
    }

    /* we can read now */
    readseg = readseg % segtotal;
    sampleslen = MIN (sps - sampleoff, len);

    GST_DEBUG_OBJECT (buf, "read @%p seg %d, off %d, len %d",
        dest + readseg * segsize, readseg, sampleoff, sampleslen);

    memcpy (data, dest + (readseg * segsize) + (sampleoff * bps),
        (sampleslen * bps));

  next:
    len -= sampleslen;
    sample += sampleslen;
    data += sampleslen * bps;
  }

  return len;

  /* ERRORS */
not_started:
  {
    GST_DEBUG_OBJECT (buf, "stopped processing");
    return -1;
  }
}

/**
 * gst_ring_buffer_prepare_read:
 * @buf: the #GstRingBuffer to read from
 * @segment: the segment to read
 * @readptr: the pointer to the memory where samples can be read
 * @len: the number of bytes to read
 *
 * Returns a pointer to memory where the data from segment @segment
 * can be found. This function is mostly used by subclasses.
 *
 * Returns: FALSE if the buffer is not started.
 *
 * MT safe.
 */
gboolean
gst_ring_buffer_prepare_read (GstRingBuffer * buf, gint * segment,
    guint8 ** readptr, gint * len)
{
  guint8 *data;
  gint segdone;

  g_return_val_if_fail (buf != NULL, FALSE);

  /* buffer must be started */
  if (g_atomic_int_get (&buf->state) != GST_RING_BUFFER_STATE_STARTED)
    return FALSE;

  g_return_val_if_fail (buf->data != NULL, FALSE);
  g_return_val_if_fail (segment != NULL, FALSE);
  g_return_val_if_fail (readptr != NULL, FALSE);
  g_return_val_if_fail (len != NULL, FALSE);

  data = GST_BUFFER_DATA (buf->data);

  /* get the position of the pointer */
  segdone = g_atomic_int_get (&buf->segdone);

  *segment = segdone % buf->spec.segtotal;
  *len = buf->spec.segsize;
  *readptr = data + *segment * *len;

  /* callback to fill the memory with data, for pull based
   * scheduling. */
  if (buf->callback)
    buf->callback (buf, *readptr, *len, buf->cb_data);

  GST_LOG ("prepare read from segment %d (real %d) @%p",
      *segment, segdone, *readptr);

  return TRUE;
}

/**
 * gst_ring_buffer_advance:
 * @buf: the #GstRingBuffer to advance
 * @advance: the number of segments written
 *
 * Subclasses should call this function to notify the fact that 
 * @advance segments are now processed by the device.
 *
 * MT safe.
 */
void
gst_ring_buffer_advance (GstRingBuffer * buf, guint advance)
{
  g_return_if_fail (buf != NULL);

  /* update counter */
  g_atomic_int_add (&buf->segdone, advance);

  /* the lock is already taken when the waiting flag is set,
   * we grab the lock as well to make sure the waiter is actually
   * waiting for the signal */
  if (g_atomic_int_compare_and_exchange (&buf->waiting, 1, 0)) {
    GST_OBJECT_LOCK (buf);
    GST_DEBUG_OBJECT (buf, "signal waiter");
    GST_RING_BUFFER_SIGNAL (buf);
    GST_OBJECT_UNLOCK (buf);
  }
}

/**
 * gst_ring_buffer_clear:
 * @buf: the #GstRingBuffer to clear
 * @segment: the segment to clear
 *
 * Clear the given segment of the buffer with silence samples.
 * This function is used by subclasses.
 *
 * MT safe.
 */
void
gst_ring_buffer_clear (GstRingBuffer * buf, gint segment)
{
  guint8 *data;

  g_return_if_fail (buf != NULL);

  /* no data means it's already cleared */
  if (G_UNLIKELY (buf->data == NULL))
    return;

  /* no empty_seg means it's not opened */
  if (G_UNLIKELY (buf->empty_seg == NULL))
    return;

  segment %= buf->spec.segtotal;

  data = GST_BUFFER_DATA (buf->data);
  data += segment * buf->spec.segsize;

  GST_LOG ("clear segment %d @%p", segment, data);

  memcpy (data, buf->empty_seg, buf->spec.segsize);
}

/**
 * gst_ring_buffer_may_start:
 * @buf: the #GstRingBuffer
 * @allowed: the new value
 *
 * Tell the ringbuffer that it is allowed to start playback when
 * the ringbuffer is filled with samples. 
 *
 * Since: 0.10.6
 *
 * MT safe.
 */
void
gst_ring_buffer_may_start (GstRingBuffer * buf, gboolean allowed)
{
  GST_LOG_OBJECT (buf, "may start: %d", allowed);
  gst_atomic_int_set (&buf->abidata.ABI.may_start, allowed);
}
