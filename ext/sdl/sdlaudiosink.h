/* GStreamer
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifndef __GST_SDLAUDIOSINK_H__
#define __GST_SDLAUDIOSINK_H__


#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

#include <SDL.h>
#include <SDL_audio.h>

G_BEGIN_DECLS

#define GST_TYPE_SDLAUDIOSINK            (gst_sdlaudio_sink_get_type())
#define GST_SDLAUDIOSINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SDLAUDIOSINK,GstSDLAudioSink))
#define GST_SDLAUDIOSINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SDLAUDIOSINK,GstSDLAudioSinkClass))
#define GST_IS_SDLAUDIOSINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SDLAUDIOSINK))
#define GST_IS_SDLAUDIOSINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SDLAUDIOSINK))

typedef struct _GstSDLAudioSink GstSDLAudioSink;
typedef struct _GstSDLAudioSinkClass GstSDLAudioSinkClass;

typedef struct _gstsdl_semaphore {
  GCond *cond;
  GMutex *mutex;
  gboolean mutexflag;

} gstsdl_semaphore;

struct _GstSDLAudioSink {
  GstAudioSink    sink;

  SDL_AudioSpec fmt;
  guint8 *buffer;

  gstsdl_semaphore semA;

  gstsdl_semaphore semB;

  gboolean eos;

};

struct _GstSDLAudioSinkClass {
  GstAudioSinkClass parent_class;
};

GType gst_sdlaudio_sink_get_type(void);

G_END_DECLS

#endif /* __GST_SDLAUDIOSINK_H__ */
