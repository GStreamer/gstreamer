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

G_END_DECLS

#endif /* __GST_PULSESINK_H__ */
