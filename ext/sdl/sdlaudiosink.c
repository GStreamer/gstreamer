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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sdlaudiosink.h"

#include <SDL_byteorder.h>
#include <string.h>

#include <unistd.h>

#include <gst/glib-compat-private.h>

GST_DEBUG_CATEGORY_EXTERN (sdl_debug);
#define GST_CAT_DEFAULT sdl_debug

static void gst_sdlaudio_sink_dispose (GObject * object);

static GstCaps *gst_sdlaudio_sink_getcaps (GstBaseSink * bsink);

static gboolean gst_sdlaudio_sink_open (GstAudioSink * asink);
static gboolean gst_sdlaudio_sink_close (GstAudioSink * asink);
static gboolean gst_sdlaudio_sink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_sdlaudio_sink_unprepare (GstAudioSink * asink);
static guint gst_sdlaudio_sink_write (GstAudioSink * asink, gpointer data,
    guint length);

#if 0
static guint gst_sdlaudio_sink_delay (GstAudioSink * asink);
static void gst_sdlaudio_sink_reset (GstAudioSink * asink);
#endif


/* SdlaudioSink signals and args */
enum
{
  LAST_SIGNAL
};

#define  SEMAPHORE_INIT(s,f)			\
  do {						\
    s.cond = g_cond_new();			\
    s.mutex = g_mutex_new();			\
    s.mutexflag = f;				\
  } while(0)

#define  SEMAPHORE_CLOSE(s)			\
  do {						\
    if ( s.cond ) {				\
      g_cond_free(s.cond);			\
      s.cond = NULL;				\
    }						\
    if ( s.mutex ) {				\
      g_mutex_free(s.mutex);			\
      s.mutex = NULL;				\
    }						\
  } while(0)

#define SEMAPHORE_UP(s)					\
  do							\
    {							\
      g_mutex_lock(s.mutex);				\
      s.mutexflag = TRUE;				\
      g_mutex_unlock(s.mutex);				\
      g_cond_signal(s.cond);				\
    } while(0)

#define SEMAPHORE_DOWN(s, e)				\
  do							\
    {							\
      while (1) {					\
	g_mutex_lock(s.mutex);				\
	if (!s.mutexflag) {				\
	  if ( e ) {					\
	    g_mutex_unlock(s.mutex);			\
	    break;					\
	  }						\
	  g_cond_wait(s.cond,s.mutex);			\
	}						\
	else {						\
	  s.mutexflag = FALSE;				\
	  g_mutex_unlock(s.mutex);			\
	  break;					\
	}						\
	g_mutex_unlock(s.mutex);			\
      }							\
    } while(0)


static GstStaticPadTemplate sdlaudiosink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { " G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "endianness = (int) { " G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

GST_BOILERPLATE (GstSDLAudioSink, gst_sdlaudio_sink, GstAudioSink,
    GST_TYPE_AUDIO_SINK);

static void
gst_sdlaudio_sink_dispose (GObject * object)
{
  GstSDLAudioSink *sdlaudiosink = GST_SDLAUDIOSINK (object);

  SEMAPHORE_CLOSE (sdlaudiosink->semB);

  SEMAPHORE_CLOSE (sdlaudiosink->semA);

  if (sdlaudiosink->buffer) {
    g_free (sdlaudiosink->buffer);
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);

}

static void
gst_sdlaudio_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class, "SDL audio sink",
      "Sink/Audio",
      "Output to a sound card via SDLAUDIO",
      "Edgard Lima <edgard.lima@indt.org.br>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sdlaudiosink_sink_factory));
}

static void
gst_sdlaudio_sink_class_init (GstSDLAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_dispose);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_prepare);
  gstaudiosink_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_write);

#if 0
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_sdlaudio_sink_reset);
#endif

}

static void
gst_sdlaudio_sink_init (GstSDLAudioSink * sdlaudiosink,
    GstSDLAudioSinkClass * g_class)
{
  GST_DEBUG ("initializing sdlaudiosink");

  memset (&sdlaudiosink->fmt, 0, sizeof (SDL_AudioSpec));

  sdlaudiosink->buffer = NULL;
  sdlaudiosink->eos = FALSE;

  SEMAPHORE_INIT (sdlaudiosink->semA, TRUE);

  SEMAPHORE_INIT (sdlaudiosink->semB, FALSE);

}

static GstCaps *
gst_sdlaudio_sink_getcaps (GstBaseSink * bsink)
{
  return gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
          (bsink)));
}

static gint
gst_sdlaudio_sink_get_format (GstBufferFormat fmt)
{
  gint result = GST_UNKNOWN;

  switch (fmt) {
    case GST_U8:
      result = AUDIO_U8;
      break;
    case GST_S8:
      result = AUDIO_S8;
      break;
    case GST_S16_LE:
      result = AUDIO_S16LSB;
      break;
    case GST_S16_BE:
      result = AUDIO_S16MSB;
      break;
    case GST_U16_LE:
      result = AUDIO_U16LSB;
      break;
    case GST_U16_BE:
      result = AUDIO_U16MSB;
      break;
    default:
      break;
  }
  return result;
}

static gboolean
gst_sdlaudio_sink_open (GstAudioSink * asink)
{
  GstSDLAudioSink *sdlaudio;

  sdlaudio = GST_SDLAUDIOSINK (asink);

  if (SDL_Init (SDL_INIT_AUDIO) < 0) {
    goto open_failed;
  }

  return TRUE;

open_failed:
  {
    GST_ELEMENT_ERROR (sdlaudio, LIBRARY, INIT,
        ("Unable to init SDL: %s\n", SDL_GetError ()), (NULL));
    return FALSE;
  }
}

static gboolean
gst_sdlaudio_sink_close (GstAudioSink * asink)
{
  GstSDLAudioSink *sdlaudio = GST_SDLAUDIOSINK (asink);

  sdlaudio->eos = TRUE;
  SEMAPHORE_UP (sdlaudio->semA);
  SEMAPHORE_UP (sdlaudio->semB);
  SDL_QuitSubSystem (SDL_INIT_AUDIO);
  return TRUE;
}

static guint
gst_sdlaudio_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstSDLAudioSink *sdlaudio = GST_SDLAUDIOSINK (asink);

  if (sdlaudio->fmt.size != length) {
    GST_ERROR ("ring buffer segment length (%u) != sdl buffer len (%u)", length,
        sdlaudio->fmt.size);
  }

  SEMAPHORE_DOWN (sdlaudio->semA, sdlaudio->eos);

  if (!sdlaudio->eos)
    memcpy (sdlaudio->buffer, data, length);

  SEMAPHORE_UP (sdlaudio->semB);

  return sdlaudio->fmt.size;
}


static void
mixaudio (void *unused, Uint8 * stream, int len)
{
  GstSDLAudioSink *sdlaudio;

  sdlaudio = GST_SDLAUDIOSINK (unused);

  if (sdlaudio->fmt.size != len) {
    GST_ERROR ("fmt buffer len (%u) != sdl callback len (%d)",
        sdlaudio->fmt.size, len);
  }

  SEMAPHORE_DOWN (sdlaudio->semB, sdlaudio->eos);

  if (!sdlaudio->eos)
    SDL_MixAudio (stream, sdlaudio->buffer, sdlaudio->fmt.size,
        SDL_MIX_MAXVOLUME);

  SEMAPHORE_UP (sdlaudio->semA);

}

static gboolean
gst_sdlaudio_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstSDLAudioSink *sdlaudio;
  gint power2 = -1;

  sdlaudio = GST_SDLAUDIOSINK (asink);

  sdlaudio->fmt.format = gst_sdlaudio_sink_get_format (spec->format);
  if (sdlaudio->fmt.format == 0)
    goto wrong_format;

  if (spec->width != 16 && spec->width != 8)
    goto dodgy_width;

  sdlaudio->fmt.freq = spec->rate;
  sdlaudio->fmt.channels = spec->channels;
  sdlaudio->fmt.samples =
      spec->segsize / (spec->channels * ((sdlaudio->fmt.format & 0xFF) >> 3));
  sdlaudio->fmt.callback = mixaudio;
  sdlaudio->fmt.userdata = sdlaudio;

  GST_DEBUG ("set segsize: %d, segtotal: %d, samples: %d", spec->segsize,
      spec->segtotal, sdlaudio->fmt.samples);

  while (sdlaudio->fmt.samples) {
    sdlaudio->fmt.samples >>= 1;
    ++power2;
  }

  sdlaudio->fmt.samples = 1;
  sdlaudio->fmt.samples <<= power2;

  GST_DEBUG ("set segsize: %d, segtotal: %d, samples: %d", spec->segsize,
      spec->segtotal, sdlaudio->fmt.samples);

  if (SDL_OpenAudio (&sdlaudio->fmt, NULL) < 0) {
    goto unable_open;
  }

  spec->segsize = sdlaudio->fmt.size;

  sdlaudio->buffer = g_malloc (sdlaudio->fmt.size);
  memset (sdlaudio->buffer, sdlaudio->fmt.silence, sdlaudio->fmt.size);

  GST_DEBUG ("set segsize: %d, segtotal: %d, samples: %d", spec->segsize,
      spec->segtotal, sdlaudio->fmt.samples);

  spec->bytes_per_sample =
      spec->channels * ((sdlaudio->fmt.format & 0xFF) >> 3);
  memset (spec->silence_sample, sdlaudio->fmt.silence, spec->bytes_per_sample);

  SDL_PauseAudio (0);

  return TRUE;

unable_open:
  {
    GST_ELEMENT_ERROR (sdlaudio, RESOURCE, OPEN_READ,
        ("Unable to open audio: %s", SDL_GetError ()), (NULL));
    return FALSE;
  }
wrong_format:
  {
    GST_ELEMENT_ERROR (sdlaudio, RESOURCE, OPEN_READ,
        ("Unable to get format %d", spec->format), (NULL));
    return FALSE;
  }
dodgy_width:
  {
    GST_ELEMENT_ERROR (sdlaudio, RESOURCE, OPEN_READ,
        ("unexpected width %d", spec->width), (NULL));
    return FALSE;
  }
}

static gboolean
gst_sdlaudio_sink_unprepare (GstAudioSink * asink)
{

  SDL_CloseAudio ();

  return TRUE;

#if 0
  if (!gst_sdlaudio_sink_close (asink))
    goto couldnt_close;

  if (!gst_sdlaudio_sink_open (asink))
    goto couldnt_reopen;

  return TRUE;

couldnt_close:
  {
    GST_DEBUG ("Could not close the audio device");
    return FALSE;
  }
couldnt_reopen:
  {
    GST_DEBUG ("Could not reopen the audio device");
    return FALSE;
  }
#endif

}

#if 0
static guint
gst_sdlaudio_sink_delay (GstAudioSink * asink)
{
  GstSDLAudioSink *sdlaudio;

  sdlaudio = GST_SDLAUDIOSINK (asink);

  return 0;
}

static void
gst_sdlaudio_sink_reset (GstAudioSink * asink)
{
}
#endif
