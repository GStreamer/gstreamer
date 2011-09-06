/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstaudioringbuffer.c: simple audio ringbuffer base class
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

#include "gstaudioringbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_audio_ring_buffer_debug);
#define GST_CAT_DEFAULT gst_audio_ring_buffer_debug

static void gst_audio_ring_buffer_class_init (GstAudioRingBufferClass * klass);
static void gst_audio_ring_buffer_init (GstAudioRingBuffer * ringbuffer,
    GstAudioRingBufferClass * klass);
static void gst_audio_ring_buffer_dispose (GObject * object);
static void gst_audio_ring_buffer_finalize (GObject * object);

static GstRingBufferClass *ring_parent_class = NULL;

static gboolean gst_audio_ring_buffer_start (GstRingBuffer * buf);
static gboolean gst_audio_ring_buffer_pause (GstRingBuffer * buf);
static gboolean gst_audio_ring_buffer_stop (GstRingBuffer * buf);
static gboolean gst_audio_ring_buffer_activate (GstRingBuffer * buf,
    gboolean active);

/* ringbuffer abstract base class */
GType
gst_audio_ring_buffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstAudioRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_audio_ring_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstAudioRingBuffer),
      0,
      (GInstanceInitFunc) gst_audio_ring_buffer_init,
      NULL
    };

    ringbuffer_type =
        g_type_register_static (GST_TYPE_RING_BUFFER, "GstAudioSinkRingBuffer",
        &ringbuffer_info, G_TYPE_FLAG_ABSTRACT);

    GST_DEBUG_CATEGORY_INIT (gst_audio_ring_buffer_debug, "audioringbuffer", 0,
        "audio ringbuffer");
  }
  return ringbuffer_type;
}

static void
gst_audio_ring_buffer_class_init (GstAudioRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstRingBufferClass *gstringbuffer_class;

  gobject_class = (GObjectClass *) klass;
  gstringbuffer_class = (GstRingBufferClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_audio_ring_buffer_dispose;
  gobject_class->finalize = gst_audio_ring_buffer_finalize;

  gstringbuffer_class->start = GST_DEBUG_FUNCPTR (gst_audio_ring_buffer_start);
  gstringbuffer_class->pause = GST_DEBUG_FUNCPTR (gst_audio_ring_buffer_pause);
  gstringbuffer_class->resume = GST_DEBUG_FUNCPTR (gst_audio_ring_buffer_start);
  gstringbuffer_class->stop = GST_DEBUG_FUNCPTR (gst_audio_ring_buffer_stop);

  gstringbuffer_class->activate =
      GST_DEBUG_FUNCPTR (gst_audio_ring_buffer_activate);
}

static void
gst_audio_ring_buffer_init (GstAudioRingBuffer * ringbuffer,
    GstAudioRingBufferClass * g_class)
{
}

static void
gst_audio_ring_buffer_dispose (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->dispose (object);
}

static void
gst_audio_ring_buffer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}

static gboolean
gst_audio_ring_buffer_activate (GstRingBuffer * buf, gboolean active)
{
  GstAudioRingBuffer *abuf;
  gboolean res;

  abuf = GST_AUDIO_RING_BUFFER_CAST (buf);

  GST_OBJECT_UNLOCK (buf);
  res = gst_ring_buffer_thread_activate (abuf->thread, active);
  GST_OBJECT_LOCK (buf);

  return res;
}

gboolean
gst_audio_ring_buffer_set_thread (GstAudioRingBuffer * buf,
    GstRingBufferThread * thread)
{
  GstRingBufferThread *old;

  g_return_val_if_fail (GST_IS_AUDIO_RING_BUFFER (buf), FALSE);

  old = buf->thread;
  if (thread)
    gst_object_ref (thread);
  buf->thread = thread;
  if (old)
    gst_object_unref (old);

  if (thread)
    gst_ring_buffer_thread_set_ringbuffer (thread, buf);

  return TRUE;
}

gboolean
gst_audio_ring_buffer_link (GstAudioRingBuffer * buf1,
    GstAudioRingBuffer * buf2)
{
  buf1->link = buf2;
  buf2->link = buf1;

  return TRUE;
}

static gboolean
gst_audio_ring_buffer_start (GstRingBuffer * buf)
{
  GstAudioRingBuffer *abuf;

  abuf = GST_AUDIO_RING_BUFFER_CAST (buf);

  GST_DEBUG_OBJECT (buf, "start, sending signal");

  return gst_ring_buffer_thread_start (abuf->thread);
}

static gboolean
gst_audio_ring_buffer_pause (GstRingBuffer * buf)
{
  GstAudioRingBuffer *abuf;
  GstAudioRingBufferClass *cbuf;

  abuf = GST_AUDIO_RING_BUFFER_CAST (buf);
  cbuf = GST_AUDIO_RING_BUFFER_GET_CLASS (abuf);

  /* unblock any pending writes to the audio device */
  if (cbuf->reset) {
    GST_DEBUG_OBJECT (abuf, "reset...");
    cbuf->reset (abuf);
    GST_DEBUG_OBJECT (abuf, "reset done");
  }
  return TRUE;
}

static gboolean
gst_audio_ring_buffer_stop (GstRingBuffer * buf)
{
  GstAudioRingBuffer *abuf;
  GstAudioRingBufferClass *cbuf;

  abuf = GST_AUDIO_RING_BUFFER_CAST (buf);
  cbuf = GST_AUDIO_RING_BUFFER_GET_CLASS (abuf);

  /* unblock any pending writes to the audio device */
  if (cbuf->reset) {
    GST_DEBUG_OBJECT (abuf, "reset...");
    cbuf->reset (abuf);
    GST_DEBUG_OBJECT (abuf, "reset done");
  }
#if 0
  if (abuf->running) {
    GST_DEBUG_OBJECT (sink, "stop, waiting...");
    GST_AUDIO_RING_BUFFER_WAIT (buf);
    GST_DEBUG_OBJECT (sink, "stopped");
  }
#endif

  return TRUE;
}
