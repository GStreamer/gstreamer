/*
 * GStreamer - SunAudio sink
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 * Copyright (C) 2006 Jan Schmidt <thaytan@mad.scientist.com>
 *
 * gstsunaudiosink.h:
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SUNAUDIO_SINK_H__
#define __GST_SUNAUDIO_SINK_H__

#include <sys/audioio.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

G_BEGIN_DECLS

#define GST_TYPE_SUNAUDIO_SINK            (gst_sunaudiosink_get_type())
#define GST_SUNAUDIO_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIO_SINK,GstSunAudioSink))
#define GST_SUNAUDIO_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIO_SINK,GstSunAudioSinkClass))
#define GST_IS_SUNAUDIO_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIO_SINK))
#define GST_IS_SUNAUDIO_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIO_SINK))

typedef struct _GstSunAudioSink GstSunAudioSink;
typedef struct _GstSunAudioSinkClass GstSunAudioSinkClass;

struct _GstSunAudioSink {
  GstAudioSink    sink;

  gchar *device;
  gint   fd;

  audio_device_t dev;
  audio_info_t info;

  /* Number of segments the ringbuffer is configured for */
  guint segtotal;
  guint segtotal_samples;

  /* Number of segments written to the device */
  gint segs_written;
  /* Number of samples written to the device */
  gint samples_written;
  guint bytes_per_sample;

  /* mutex and gcond used to control the write method */
  GMutex *write_mutex;
  GCond *sleep_cond;
  gboolean flushing;
};

struct _GstSunAudioSinkClass {
  GstAudioSinkClass parent_class;
};

GType gst_sunaudiosink_get_type(void);

G_END_DECLS

#endif /* __GST_SUNAUDIO_SINK_H__ */

