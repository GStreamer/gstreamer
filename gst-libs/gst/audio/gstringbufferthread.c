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

#include "gstringbufferthread.h"

GST_DEBUG_CATEGORY_STATIC (gst_ring_buffer_thread_debug);
#define GST_CAT_DEFAULT gst_ring_buffer_thread_debug

static void gst_ring_buffer_thread_class_init (GstRingBufferThreadClass *
    klass);
static void gst_ring_buffer_thread_init (GstRingBufferThread * ringbuffer,
    GstRingBufferThreadClass * klass);
static void gst_ring_buffer_thread_dispose (GObject * object);
static void gst_ring_buffer_thread_finalize (GObject * object);

static GstRingBufferClass *ring_parent_class = NULL;

GType
gst_ring_buffer_thread_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstRingBufferThreadClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ring_buffer_thread_class_init,
      NULL,
      NULL,
      sizeof (GstRingBufferThread),
      0,
      (GInstanceInitFunc) gst_ring_buffer_thread_init,
      NULL
    };

    ringbuffer_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstRingBufferThread",
        &ringbuffer_info, 0);

    GST_DEBUG_CATEGORY_INIT (gst_ring_buffer_thread_debug, "ringbufferthread",
        0, "ringbuffer thread");
  }
  return ringbuffer_type;
}

static void
gst_ring_buffer_thread_class_init (GstRingBufferThreadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_ring_buffer_thread_dispose;
  gobject_class->finalize = gst_ring_buffer_thread_finalize;
}

typedef gint (*ProcessFunc) (GstAudioRingBuffer * buf, gpointer data,
    guint length);

/* this internal thread does nothing else but write samples to the audio device.
 * It will write each segment in the ringbuffer and will update the play
 * pointer.
 * The start/stop methods control the thread.
 */
static void
ring_buffer_thread_thread_func (GstRingBufferThread * thread)
{
  GstElement *parent = NULL;
  GstMessage *message;
  GValue val = { 0 };
  GstAudioRingBuffer *capture, *playback;
  ProcessFunc writefunc = NULL, readfunc = NULL;
  gint preroll = 1;

  GST_DEBUG_OBJECT (thread, "enter thread");

  GST_OBJECT_LOCK (thread);
  GST_DEBUG_OBJECT (thread, "signal wait");
  GST_RING_BUFFER_THREAD_SIGNAL (thread);
  if ((capture = thread->capture))
    gst_object_ref (capture);
  if ((playback = thread->playback))
    gst_object_ref (playback);
  GST_OBJECT_UNLOCK (thread);

  if (capture)
    readfunc = GST_AUDIO_RING_BUFFER_GET_CLASS (capture)->process;
  if (playback)
    writefunc = GST_AUDIO_RING_BUFFER_GET_CLASS (playback)->process;

  if (parent) {
    g_value_init (&val, G_TYPE_POINTER);
    g_value_set_pointer (&val, thread->thread);
    message = gst_message_new_stream_status (GST_OBJECT_CAST (thread),
        GST_STREAM_STATUS_TYPE_ENTER, NULL);
    gst_message_set_stream_status_object (message, &val);
    GST_DEBUG_OBJECT (thread, "posting ENTER stream status");
    gst_element_post_message (parent, message);
  }

  while (TRUE) {
    gint left, processed;
    guint8 *read_ptr, *write_ptr;
    gint read_seg, write_seg;
    gint read_len, write_len;
    gboolean read_active, write_active;

    if (playback)
      write_active =
          gst_ring_buffer_prepare_read (GST_RING_BUFFER_CAST (playback),
          &write_seg, &write_ptr, &write_len);
    else
      write_active = FALSE;

    if (playback) {
      if (!write_active) {
        write_ptr = GST_RING_BUFFER_CAST (playback)->empty_seg;
        write_len = GST_RING_BUFFER_CAST (playback)->spec.segsize;
      }

      left = write_len;
      do {
        processed = writefunc (playback, write_ptr, left);
        GST_LOG_OBJECT (thread, "written %d bytes of %d from segment %d",
            processed, left, write_seg);
        if (processed < 0 || processed > left) {
          /* might not be critical, it e.g. happens when aborting playback */
          GST_WARNING_OBJECT (thread,
              "error writing data in %s (reason: %s), skipping segment (left: %d, processed: %d)",
              GST_DEBUG_FUNCPTR_NAME (writefunc),
              (errno > 1 ? g_strerror (errno) : "unknown"), left, processed);
          break;
        }
        left -= processed;
        write_ptr += processed;
      } while (left > 0);

      /* we wrote one segment */
      gst_ring_buffer_advance (GST_RING_BUFFER_CAST (playback), 1);

      if (preroll > 0) {
        /* do not start reading until we have read enough data */
        preroll--;
        GST_DEBUG_OBJECT (thread, "need more preroll");
        continue;
      }
    }


    if (capture)
      read_active =
          gst_ring_buffer_prepare_read (GST_RING_BUFFER_CAST (capture),
          &read_seg, &read_ptr, &read_len);
    else
      read_active = FALSE;

    if (capture) {
      left = read_len;
      do {
        processed = readfunc (capture, read_ptr, left);
        GST_LOG_OBJECT (thread, "read %d bytes of %d from segment %d",
            processed, left, read_seg);
        if (processed < 0 || processed > left) {
          /* might not be critical, it e.g. happens when aborting playback */
          GST_WARNING_OBJECT (thread,
              "error reading data in %s (reason: %s), skipping segment (left: %d, processed: %d)",
              GST_DEBUG_FUNCPTR_NAME (readfunc),
              (errno > 1 ? g_strerror (errno) : "unknown"), left, processed);
          break;
        }
        left -= processed;
        read_ptr += processed;
      } while (left > 0);

      if (read_active)
        /* we read one segment */
        gst_ring_buffer_advance (GST_RING_BUFFER_CAST (capture), 1);
    }

    if (!read_active && !write_active) {
      GST_OBJECT_LOCK (thread);
      if (!thread->running)
        goto stop_running;
      GST_DEBUG_OBJECT (thread, "signal wait");
      GST_RING_BUFFER_THREAD_SIGNAL (thread);
      GST_DEBUG_OBJECT (thread, "wait for action");
      GST_RING_BUFFER_THREAD_WAIT (thread);
      GST_DEBUG_OBJECT (thread, "got signal");
      if (!thread->running)
        goto stop_running;
      GST_DEBUG_OBJECT (thread, "continue running");
      GST_OBJECT_UNLOCK (thread);
    }
  }

  /* Will never be reached */
  g_assert_not_reached ();
  return;

  /* ERROR */
stop_running:
  {
    GST_OBJECT_UNLOCK (thread);
    GST_DEBUG_OBJECT (thread, "stop running, exit thread");
    if (parent) {
      message = gst_message_new_stream_status (GST_OBJECT_CAST (thread),
          GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (thread));
      gst_message_set_stream_status_object (message, &val);
      GST_DEBUG_OBJECT (thread, "posting LEAVE stream status");
      gst_element_post_message (parent, message);
    }
    return;
  }
}

static void
gst_ring_buffer_thread_init (GstRingBufferThread * thread,
    GstRingBufferThreadClass * g_class)
{
  thread->running = FALSE;
  thread->cond = g_cond_new ();
}

static void
gst_ring_buffer_thread_dispose (GObject * object)
{
  GstRingBufferThread *thread = GST_RING_BUFFER_THREAD_CAST (object);

  GST_OBJECT_LOCK (thread);
  if (thread->playback) {
    gst_object_unref (thread->playback);
    thread->playback = NULL;
  }
  if (thread->capture) {
    gst_object_unref (thread->capture);
    thread->capture = NULL;
  }
  GST_OBJECT_UNLOCK (thread);

  G_OBJECT_CLASS (ring_parent_class)->dispose (object);
}

static void
gst_ring_buffer_thread_finalize (GObject * object)
{
  GstRingBufferThread *thread = GST_RING_BUFFER_THREAD_CAST (object);

  g_cond_free (thread->cond);

  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}

gboolean
gst_ring_buffer_thread_activate (GstRingBufferThread * thread, gboolean active)
{
  GError *error = NULL;

  GST_OBJECT_LOCK (thread);
  if (active) {
    if (thread->active_count == 0) {
      thread->running = TRUE;
      GST_DEBUG_OBJECT (thread, "starting thread");
      thread->thread =
          g_thread_create ((GThreadFunc) ring_buffer_thread_thread_func, thread,
          TRUE, &error);
      if (!thread->thread || error != NULL)
        goto thread_failed;

      GST_DEBUG_OBJECT (thread, "waiting for thread");
      /* the object lock is taken */
      GST_RING_BUFFER_THREAD_WAIT (thread);
      GST_DEBUG_OBJECT (thread, "thread is started");
    }
    thread->active_count++;
  } else {
    if (thread->active_count == 1) {
      thread->running = FALSE;
      GST_DEBUG_OBJECT (thread, "signal wait");
      GST_RING_BUFFER_THREAD_SIGNAL (thread);
      GST_OBJECT_UNLOCK (thread);

      /* join the thread */
      g_thread_join (thread->thread);

      GST_OBJECT_LOCK (thread);
    }
    thread->active_count--;
  }
  GST_OBJECT_UNLOCK (thread);

  return TRUE;

  /* ERRORS */
thread_failed:
  {
    if (error)
      GST_ERROR_OBJECT (thread, "could not create thread %s", error->message);
    else
      GST_ERROR_OBJECT (thread, "could not create thread for unknown reason");
    thread->running = FALSE;
    GST_OBJECT_UNLOCK (thread);
    return FALSE;
  }
}

gboolean
gst_ring_buffer_thread_set_ringbuffer (GstRingBufferThread * thread,
    GstAudioRingBuffer * buf)
{
  GstAudioRingBuffer *old, **new;

  g_return_val_if_fail (GST_IS_RING_BUFFER_THREAD (thread), FALSE);

  if (buf->mode == GST_AUDIO_RING_BUFFER_MODE_PLAYBACK)
    new = &thread->playback;
  else
    new = &thread->capture;

  old = *new;
  if (buf)
    gst_object_ref (buf);
  *new = buf;
  if (old)
    gst_object_unref (old);

  return TRUE;
}

gboolean
gst_ring_buffer_thread_start (GstRingBufferThread * thread)
{
  g_return_val_if_fail (GST_IS_RING_BUFFER_THREAD (thread), FALSE);

  GST_RING_BUFFER_THREAD_SIGNAL (thread);

  return TRUE;
}
