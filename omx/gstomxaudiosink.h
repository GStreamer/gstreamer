/*
 * Copyright (C) 2014, Fluendo, S.A.
 * Copyright (C) 2014, Metrological Media Innovations B.V.
 *   Author: Josep Torra <josep@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_AUDIO_SINK_H__
#define __GST_OMX_AUDIO_SINK_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstomx.h"

G_BEGIN_DECLS

#define GST_TYPE_OMX_AUDIO_SINK \
  (gst_omx_audio_sink_get_type())
#define GST_OMX_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_AUDIO_SINK,GstOMXAudioSink))
#define GST_OMX_AUDIO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_AUDIO_SINK,GstOMXAudioSinkClass))
#define GST_OMX_AUDIO_SINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_AUDIO_SINK,GstOMXAudioSinkClass))
#define GST_IS_OMX_AUDIO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_AUDIO_SINK))
#define GST_IS_OMX_AUDIO_SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_AUDIO_SINK))
#define GST_OMX_AUDIO_SINK_CAST(obj)      ((GstOMXAudioSink *) (obj))

#define GST_OMX_AUDIO_SINK_GET_LOCK(obj)	(&GST_OMX_AUDIO_SINK_CAST (obj)->lock)
#define GST_OMX_AUDIO_SINK_LOCK(obj)	    (g_mutex_lock (GST_OMX_AUDIO_SINK_GET_LOCK (obj)))
#define GST_OMX_AUDIO_SINK_UNLOCK(obj)    (g_mutex_unlock (GST_OMX_AUDIO_SINK_GET_LOCK (obj)))

#define PASSTHROUGH_CAPS \
    "audio/x-ac3, framed = (boolean) true;" \
    "audio/x-eac3, framed = (boolean) true; " \
    "audio/x-dts, framed = (boolean) true, " \
      "block-size = (int) { 512, 1024, 2048 }; " \
    "audio/mpeg, mpegversion = (int) 1, " \
      "mpegaudioversion = (int) [ 1, 2 ], parsed = (boolean) true;"

typedef struct _GstOMXAudioSink GstOMXAudioSink;
typedef struct _GstOMXAudioSinkClass GstOMXAudioSinkClass;

struct _GstOMXAudioSink
{
  GstAudioSink parent;

  /* < protected > */
  GstOMXComponent *comp;
  GstOMXPort *in_port, *out_port;
  
  gboolean mute;
  gdouble volume;

  gboolean iec61937;
  guint endianness;
  guint rate;
  guint channels;
  guint width;
  gboolean is_signed;
  gboolean is_float;

  guint buffer_size;
  guint samples;

  GMutex lock;
};

struct _GstOMXAudioSinkClass
{
  GstAudioSinkClass parent_class;

  GstOMXClassData cdata;
  const gchar * destination;
};

GType gst_omx_audio_sink_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_AUDIO_SINK_H__ */

