/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstaudioringbuffer.h:
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

#ifndef __GST_AUDIO_RING_BUFFER_H__
#define __GST_AUDIO_RING_BUFFER_H__

#include <gst/gst.h>
#include <gst/audio/gstringbuffer.h>

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_RING_BUFFER             (gst_audio_ring_buffer_get_type())
#define GST_AUDIO_RING_BUFFER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_RING_BUFFER,GstAudioRingBuffer))
#define GST_AUDIO_RING_BUFFER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_RING_BUFFER,GstAudioRingBufferClass))
#define GST_AUDIO_RING_BUFFER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_AUDIO_RING_BUFFER,GstAudioRingBufferClass))
#define GST_IS_AUDIO_RING_BUFFER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_RING_BUFFER))
#define GST_IS_AUDIO_RING_BUFFER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_RING_BUFFER))
#define GST_AUDIO_RING_BUFFER_CAST(obj)        ((GstAudioRingBuffer *)obj)

typedef struct _GstAudioRingBuffer GstAudioRingBuffer;
typedef struct _GstAudioRingBufferClass GstAudioRingBufferClass;

#include <gst/audio/gstringbufferthread.h>

typedef enum {
  GST_AUDIO_RING_BUFFER_MODE_UNKNOWN,
  GST_AUDIO_RING_BUFFER_MODE_PLAYBACK,
  GST_AUDIO_RING_BUFFER_MODE_CAPTURE
} GstAudioRingBufferMode;
/**
 * GstAudioRingBuffer:
 *
 * Opaque #GstAudioRingBuffer.
 */
struct _GstAudioRingBuffer {
  GstRingBuffer       element;

  /*< protected >*/
  GstAudioRingBufferMode  mode;
  GstRingBufferThread    *thread;

  GstAudioRingBuffer     *link;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstAudioRingBufferClass:
 * @parent_class: the parent class structure.
 * @process: Write/Read data to/from the device.
 * @reset: Returns as quickly as possible from a write/read and flush any pending
 *         samples from the device.
 *
 * #GstAudioRingBuffer class. Override the vmethods to implement functionality.
 */
struct _GstAudioRingBufferClass {
  GstRingBufferClass parent_class;

  /* vtable */

  /* write/read samples to the device */
  gint      (*process)   (GstAudioRingBuffer *buf, gpointer data, guint length);
  /* reset the audio device, unblock from a read/write */
  void      (*reset)     (GstAudioRingBuffer *buf);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_audio_ring_buffer_get_type(void);

gboolean gst_audio_ring_buffer_link       (GstAudioRingBuffer *buf1, GstAudioRingBuffer *buf2);

gboolean gst_audio_ring_buffer_set_thread (GstAudioRingBuffer *buf, GstRingBufferThread *thread);

G_END_DECLS

#endif /* __GST_AUDIO_RING_BUFFER_H__ */
