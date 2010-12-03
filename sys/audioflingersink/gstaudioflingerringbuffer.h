/* GStreamer
 * Copyright (C) 2010 Alessandro Decina <alessandro.decina@collabora.co.uk>
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

#ifndef GST_AUDIO_FLINGER_RING_BUFFER_H
#define GST_AUDIO_FLINGER_RING_BUFFER_H

#include <string.h>

#include "gstaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_audio_sink_debug);
#define GST_CAT_DEFAULT gst_audio_sink_debug

#define GST_TYPE_AUDIORING_BUFFER        \
        (gst_audioringbuffer_get_type())
#define GST_AUDIORING_BUFFER(obj)        \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIORING_BUFFER,GstAudioRingBuffer))
#define GST_AUDIORING_BUFFER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIORING_BUFFER,GstAudioRingBufferClass))
#define GST_AUDIORING_BUFFER_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_AUDIORING_BUFFER, GstAudioRingBufferClass))
#define GST_AUDIORING_BUFFER_CAST(obj)        \
        ((GstAudioRingBuffer *)obj)
#define GST_IS_AUDIORING_BUFFER(obj)     \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIORING_BUFFER))
#define GST_IS_AUDIORING_BUFFER_CLASS(klass)\
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIORING_BUFFER))

typedef struct _GstAudioRingBuffer GstAudioRingBuffer;
typedef struct _GstAudioRingBufferClass GstAudioRingBufferClass;

#define GST_AUDIORING_BUFFER_GET_COND(buf) (((GstAudioRingBuffer *)buf)->cond)
#define GST_AUDIORING_BUFFER_WAIT(buf)     (g_cond_wait (GST_AUDIORING_BUFFER_GET_COND (buf), GST_OBJECT_GET_LOCK (buf)))
#define GST_AUDIORING_BUFFER_SIGNAL(buf)   (g_cond_signal (GST_AUDIORING_BUFFER_GET_COND (buf)))
#define GST_AUDIORING_BUFFER_BROADCAST(buf)(g_cond_broadcast (GST_AUDIORING_BUFFER_GET_COND (buf)))

struct _GstAudioRingBuffer
{
  GstRingBuffer object;

  gboolean running;
  gint queuedseg;

  GCond *cond;
};

struct _GstAudioRingBufferClass
{
  GstRingBufferClass parent_class;
};

static void gst_audioringbuffer_class_init (GstAudioRingBufferClass * klass);
static void gst_audioringbuffer_init (GstAudioRingBuffer * ringbuffer,
    GstAudioRingBufferClass * klass);
static void gst_audioringbuffer_dispose (GObject * object);
static void gst_audioringbuffer_finalize (GObject * object);

static GstRingBufferClass *ring_parent_class = NULL;

static gboolean gst_audioringbuffer_open_device (GstRingBuffer * buf);
static gboolean gst_audioringbuffer_close_device (GstRingBuffer * buf);
static gboolean gst_audioringbuffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec);
static gboolean gst_audioringbuffer_release (GstRingBuffer * buf);
static gboolean gst_audioringbuffer_start (GstRingBuffer * buf);
static gboolean gst_audioringbuffer_pause (GstRingBuffer * buf);
static gboolean gst_audioringbuffer_stop (GstRingBuffer * buf);
static guint gst_audioringbuffer_delay (GstRingBuffer * buf);
static gboolean gst_audioringbuffer_activate (GstRingBuffer * buf,
    gboolean active);

GType gst_audioringbuffer_get_type (void);

#endif /* GST_AUDIO_FLINGER_RING_BUFFER_H */
