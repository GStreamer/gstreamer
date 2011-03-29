/*-*- Mode: C; c-basic-offset: 2 -*-*/

/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

#ifndef __GST_PULSESINK_H__
#define __GST_PULSESINK_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>

#include "pulseprobe.h"

G_BEGIN_DECLS

#define GST_TYPE_PULSESINK \
  (gst_pulsesink_get_type())
#define GST_PULSESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PULSESINK,GstPulseSink))
#define GST_PULSESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PULSESINK,GstPulseSinkClass))
#define GST_IS_PULSESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PULSESINK))
#define GST_IS_PULSESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PULSESINK))
#define GST_PULSESINK_CAST(obj) \
  ((GstPulseSink *)(obj))

typedef struct _GstPulseSink GstPulseSink;
typedef struct _GstPulseSinkClass GstPulseSinkClass;

struct _GstPulseSink
{
  GstBaseAudioSink sink;

  gchar *server, *device, *stream_name, *client_name;
  gchar *device_description;

  GstPulseProbe *probe;

  gdouble volume;
  gboolean volume_set:1;
  gboolean mute:1;
  gboolean mute_set:1;

  guint defer_pending;

  gint notify; /* atomic */

  const gchar *pa_version;

  GstStructure *properties;
  pa_proplist *proplist;

#ifdef HAVE_PULSE_1_0
  GMutex *sink_formats_lock;
  GList *sink_formats;
  volatile gint format_lost;
  GstClockTime format_lost_time;
#endif
};

struct _GstPulseSinkClass
{
  GstBaseAudioSinkClass parent_class;
};

GType gst_pulsesink_get_type (void);

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
# define ENDIANNESS   "LITTLE_ENDIAN, BIG_ENDIAN"
#else
# define ENDIANNESS   "BIG_ENDIAN, LITTLE_ENDIAN"
#endif

#define _PULSE_SINK_CAPS_COMMON \
    "audio/x-raw-int, " \
      "endianness = (int) { " ENDIANNESS " }, " \
      "signed = (boolean) TRUE, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, 32 ];" \
    "audio/x-raw-float, " \
      "endianness = (int) { " ENDIANNESS " }, " \
      "width = (int) 32, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, 32 ];" \
    "audio/x-raw-int, " \
      "endianness = (int) { " ENDIANNESS " }, " \
      "signed = (boolean) TRUE, " \
      "width = (int) 32, " \
      "depth = (int) 32, " \
      "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 32 ];" \
    "audio/x-raw-int, " \
      "signed = (boolean) FALSE, " \
      "width = (int) 8, " \
      "depth = (int) 8, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, 32 ];" \
    "audio/x-alaw, " \
      "rate = (int) [ 1, MAX], " \
      "channels = (int) [ 1, 32 ];" \
    "audio/x-mulaw, " \
      "rate = (int) [ 1, MAX], " "channels = (int) [ 1, 32 ];" \
    "audio/x-raw-int, " \
      "endianness = (int) { " ENDIANNESS " }, " \
      "signed = (boolean) TRUE, " \
      "width = (int) 24, " \
      "depth = (int) 24, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, 32 ];" \
    "audio/x-raw-int, " \
      "endianness = (int) { " ENDIANNESS " }, " \
      "signed = (boolean) TRUE, " \
      "width = (int) 32, " \
      "depth = (int) 24, " \
      "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 32 ];"

#ifdef HAVE_PULSE_1_0
#define _PULSE_SINK_CAPS_1_0 \
    "audio/x-ac3, framed = (boolean) true;" \
    "audio/x-eac3, framed = (boolean) true; " \
    "audio/x-dts, framed = (boolean) true, " \
      "block-size = (int) { 512, 1024, 2048 }; " \
    "audio/mpeg, mpegversion = (int) 1, " \
      "mpegaudioversion = (int) [ 1, 2 ], parsed = (boolean) true;"
#else
#define _PULSE_SINK_CAPS_1_0 ""
#endif

#define PULSE_SINK_TEMPLATE_CAPS \
  _PULSE_SINK_CAPS_COMMON \
  _PULSE_SINK_CAPS_1_0

#ifdef HAVE_PULSE_1_0

#define GST_TYPE_PULSE_AUDIO_SINK \
  (gst_pulse_audio_sink_get_type())
#define GST_PULSE_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PULSE_AUDIO_SINK,GstPulseAudioSink))
#define GST_PULSE_AUDIO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PULSE_AUDIO_SINK,GstPulseAudioSinkClass))
#define GST_IS_PULSE_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PULSE_AUDIO_SINK))
#define GST_IS_PULSE_AUDIO_SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PULSE_AUDIO_SINK))
#define GST_PULSE_AUDIO_SINK_CAST(obj) \
  ((GstPulseAudioSink *)(obj))

GType gst_pulse_audio_sink_get_type (void);

#endif /* HAVE_PULSE_1_0 */

G_END_DECLS

#endif /* __GST_PULSESINK_H__ */
