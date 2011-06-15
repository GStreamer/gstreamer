/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2009-2010 Chris Robinson <chris.kcat@gmail.com>
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

#ifndef __GST_OPENALSINK_H__
#define __GST_OPENALSINK_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>
#include <gst/audio/multichannel.h>

#ifdef _WIN32
#include <al.h>
#include <alc.h>
#include <alext.h>
#elif defined(__APPLE__)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#include <OpenAL/alext.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_OPENAL_SINK (gst_openal_sink_get_type())
#define GST_OPENAL_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OPENAL_SINK,GstOpenALSink))
#define GST_OPENAL_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OPENAL_SINK,GstOpenALSinkClass))
#define GST_IS_OPENAL_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OPENAL_SINK))
#define GST_IS_OPENAL_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OPENAL_SINK))

#if 1
#define GST_ALC_ERROR(Device)  ("ALC error: %s", alcGetString((Device), alcGetError((Device))))
#else
#define GST_ALC_ERROR(Device)  ("ALC error: 0x%x", alcGetError((Device)))
#endif

typedef struct _GstOpenALSink GstOpenALSink;
typedef struct _GstOpenALSinkClass GstOpenALSinkClass;

#define GST_OPENAL_SINK_CAST(obj)     ((GstOpenALSink*)obj)
#define GST_OPENAL_SINK_GET_LOCK(obj) (GST_OPENAL_SINK_CAST(obj)->openal_lock)
#define GST_OPENAL_SINK_LOCK(obj)     (g_mutex_lock(GST_OPENAL_SINK_GET_LOCK(obj)))
#define GST_OPENAL_SINK_UNLOCK(obj)   (g_mutex_unlock(GST_OPENAL_SINK_GET_LOCK(obj)))

struct _GstOpenALSink {
    GstAudioSink sink;

    gchar *devname;

    /* When set, we don't own device */
    ALCdevice  *custom_dev;
    /* When set, we don't own device or context */
    ALCcontext *custom_ctx;
    /* When set, we don't own sID */
    ALuint     custom_sID;

    ALCdevice  *device;
    ALCcontext *context;
    ALuint     sID;

    ALuint bID_idx;
    ALuint bID_count;
    ALuint *bIDs;
    ALuint bID_length;

    ALenum format;
    ALuint srate;
    ALuint bytes_per_sample;

    ALboolean write_reset;

    GstCaps *probed_caps;

    GMutex *openal_lock;
};

struct _GstOpenALSinkClass {
    GstAudioSinkClass parent_class;
};

GType gst_openal_sink_get_type(void);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_S16_NE     GST_S16_LE
#define GST_FLOAT32_NE GST_FLOAT32_LE
#define GST_FLOAT64_NE GST_FLOAT64_LE
#else
#define GST_S16_NE     GST_S16_BE
#define GST_FLOAT32_NE GST_FLOAT32_BE
#define GST_FLOAT64_NE GST_FLOAT64_BE
#endif

#define OPENAL_MIN_RATE    8000
#define OPENAL_MAX_RATE    192000

G_END_DECLS

#endif /* __GST_OPENALSINK_H__ */
