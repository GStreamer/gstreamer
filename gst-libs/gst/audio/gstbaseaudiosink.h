/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbaseaudiosink.h:
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

/* a base class for audio sinks.
 *
 * It uses a ringbuffer to schedule playback of samples. This makes
 * it very easy to drop or insert samples to align incoming
 * buffers to the exact playback timestamp.
 *
 * Subclasses must provide a ringbuffer pointing to either DMA
 * memory or regular memory. A subclass should also call a callback
 * function when it has played N segments in the buffer. The subclass
 * is free to use a thread to signal this callback, use EIO or any
 * other mechanism.
 *
 * The base class is able to operate in push or pull mode. The chain
 * mode will queue the samples in the ringbuffer as much as possible.
 * The available space is calculated in the callback function.
 *
 * The pull mode will pull_range() a new buffer of N samples with a
 * configurable latency. This allows for high-end real time
 * audio processing pipelines driven by the audiosink. The callback
 * function will be used to perform a pull_range() on the sinkpad.
 * The thread scheduling the callback can be a real-time thread.
 *
 * Subclasses must implement a GstRingBuffer in addition to overriding
 * the methods in GstBaseSink and this class.
 */

#ifndef __GST_BASE_AUDIO_SINK_H__
#define __GST_BASE_AUDIO_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstringbuffer.h"
#include "gstaudioclock.h"

G_BEGIN_DECLS

#define GST_TYPE_BASE_AUDIO_SINK                (gst_base_audio_sink_get_type())
#define GST_BASE_AUDIO_SINK(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_AUDIO_SINK,GstBaseAudioSink))
#define GST_BASE_AUDIO_SINK_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_AUDIO_SINK,GstBaseAudioSinkClass))
#define GST_BASE_AUDIO_SINK_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BASE_AUDIO_SINK, GstBaseAudioSinkClass))
#define GST_IS_BASE_AUDIO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_AUDIO_SINK))
#define GST_IS_BASE_AUDIO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_AUDIO_SINK))

#define GST_BASE_AUDIO_SINK_CLOCK(obj)   (GST_BASE_AUDIO_SINK (obj)->clock)
#define GST_BASE_AUDIO_SINK_PAD(obj)     (GST_BASE_SINK (obj)->sinkpad)

typedef struct _GstBaseAudioSink GstBaseAudioSink;
typedef struct _GstBaseAudioSinkClass GstBaseAudioSinkClass;

struct _GstBaseAudioSink {
  GstBaseSink    element;

  /*< protected >*/ /* with LOCK */
  /* our ringbuffer */
  GstRingBuffer *ringbuffer;

  /* required buffer and latency */
  GstClockTime   buffer_time;
  GstClockTime   latency_time;

  /* the next sample to write */
  guint64        next_sample;

  /* clock */
  gboolean       provide_clock;
  GstClock      *provided_clock;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBaseAudioSinkClass {
  GstBaseSinkClass parent_class;

  /* subclass ringbuffer allocation */
  GstRingBuffer* (*create_ringbuffer)  (GstBaseAudioSink *sink);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_base_audio_sink_get_type(void);

GstRingBuffer *gst_base_audio_sink_create_ringbuffer (GstBaseAudioSink *sink);

G_END_DECLS

#endif /* __GST_BASE_AUDIO_SINK_H__ */
