/*
 * GStreamer
 * Copyright (C) 2006 Zaheer Abbas Merali <zaheerabbas at merali dot org>
 * Copyright (C) 2008 Pioneers of the Inevitable <songbird@songbirdnest.com>
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/audio/multichannel.h>
#include "gstosxringbuffer.h"
#include "gstosxaudiosink.h"
#include "gstosxaudiosrc.h"

#include <unistd.h>             /* for getpid() */

GST_DEBUG_CATEGORY_STATIC (osx_audio_debug);
#define GST_CAT_DEFAULT osx_audio_debug

#include "gstosxcoreaudio.h"

static void gst_osx_ring_buffer_dispose (GObject * object);
static void gst_osx_ring_buffer_finalize (GObject * object);
static gboolean gst_osx_ring_buffer_open_device (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_close_device (GstRingBuffer * buf);

static gboolean gst_osx_ring_buffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec);
static gboolean gst_osx_ring_buffer_release (GstRingBuffer * buf);

static gboolean gst_osx_ring_buffer_start (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_pause (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_stop (GstRingBuffer * buf);
static guint gst_osx_ring_buffer_delay (GstRingBuffer * buf);
static GstRingBufferClass *ring_parent_class = NULL;

static void
gst_osx_ring_buffer_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (osx_audio_debug, "osxaudio", 0,
      "OSX Audio Elements");
}

GST_BOILERPLATE_FULL (GstOsxRingBuffer, gst_osx_ring_buffer,
    GstRingBuffer, GST_TYPE_RING_BUFFER, gst_osx_ring_buffer_do_init);

static void
gst_osx_ring_buffer_base_init (gpointer g_class)
{
  /* Nothing to do right now */
}

static void
gst_osx_ring_buffer_class_init (GstOsxRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstRingBufferClass *gstringbuffer_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstringbuffer_class = (GstRingBufferClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_osx_ring_buffer_dispose;
  gobject_class->finalize = gst_osx_ring_buffer_finalize;

  gstringbuffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_open_device);
  gstringbuffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_close_device);
  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_release);
  gstringbuffer_class->start = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_start);
  gstringbuffer_class->pause = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_pause);
  gstringbuffer_class->resume = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_start);
  gstringbuffer_class->stop = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_stop);

  gstringbuffer_class->delay = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_delay);

  GST_DEBUG ("osx ring buffer class init");
}

static void
gst_osx_ring_buffer_init (GstOsxRingBuffer * ringbuffer,
    GstOsxRingBufferClass * g_class)
{
  ringbuffer->core_audio = gst_core_audio_new (GST_OBJECT (ringbuffer));
}

static void
gst_osx_ring_buffer_dispose (GObject * object)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (object);

  if (osxbuf->core_audio) {
    g_object_unref (osxbuf->core_audio);
    osxbuf->core_audio = NULL;
  }
  G_OBJECT_CLASS (ring_parent_class)->dispose (object);
}

static void
gst_osx_ring_buffer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}

static gboolean
gst_osx_ring_buffer_open_device (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  return gst_core_audio_open (osxbuf->core_audio);
}

static gboolean
gst_osx_ring_buffer_close_device (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;
  osxbuf = GST_OSX_RING_BUFFER (buf);

  return gst_core_audio_close (osxbuf->core_audio);
}

static gboolean
gst_osx_ring_buffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  gboolean ret = FALSE, is_passthrough = FALSE;
  GstOsxRingBuffer *osxbuf;
  AudioStreamBasicDescription format;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  if (RINGBUFFER_IS_SPDIF (spec->type)) {
    format.mFormatID = kAudioFormat60958AC3;
    format.mSampleRate = (double) spec->rate;
    format.mChannelsPerFrame = 2;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger |
        kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonMixable;
    format.mBytesPerFrame = 0;
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = 6144;
    format.mFramesPerPacket = 1536;
    format.mReserved = 0;
    spec->segsize = 6144;
    spec->segtotal = 10;
    is_passthrough = TRUE;
  } else {
    int width, depth;
    /* Fill out the audio description we're going to be using */
    format.mFormatID = kAudioFormatLinearPCM;
    format.mSampleRate = (double) spec->rate;
    format.mChannelsPerFrame = spec->channels;
    if (spec->type == GST_BUFTYPE_FLOAT) {
      format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
      width = depth = spec->width;
    } else {
      format.mFormatFlags = kAudioFormatFlagIsSignedInteger;
      width = spec->width;
      depth = spec->depth;
      if (width == depth) {
        format.mFormatFlags |= kAudioFormatFlagIsPacked;
      } else {
        format.mFormatFlags |= kAudioFormatFlagIsAlignedHigh;
      }
      if (spec->bigend) {
        format.mFormatFlags |= kAudioFormatFlagIsBigEndian;
      }
    }
    format.mBytesPerFrame = spec->channels * (width >> 3);
    format.mBitsPerChannel = depth;
    format.mBytesPerPacket = spec->channels * (width >> 3);
    format.mFramesPerPacket = 1;
    format.mReserved = 0;
    spec->segsize =
        (spec->latency_time * spec->rate / G_USEC_PER_SEC) *
        spec->bytes_per_sample;
    spec->segtotal = spec->buffer_time / spec->latency_time;
    is_passthrough = FALSE;
  }

  GST_DEBUG_OBJECT (osxbuf, "Format: " CORE_AUDIO_FORMAT,
      CORE_AUDIO_FORMAT_ARGS (format));

  buf->data = gst_buffer_new_and_alloc (spec->segtotal * spec->segsize);
  memset (GST_BUFFER_DATA (buf->data), 0, GST_BUFFER_SIZE (buf->data));

  ret = gst_core_audio_initialize (osxbuf->core_audio, format, spec->caps,
      is_passthrough);

  if (!ret) {
    gst_buffer_unref (buf->data);
    buf->data = NULL;
  }

  osxbuf->segoffset = 0;

  return ret;
}

static gboolean
gst_osx_ring_buffer_release (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  gst_core_audio_unitialize (osxbuf->core_audio);

  gst_buffer_unref (buf->data);
  buf->data = NULL;

  return TRUE;
}

static gboolean
gst_osx_ring_buffer_start (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  return gst_core_audio_start_processing (osxbuf->core_audio);
}

static gboolean
gst_osx_ring_buffer_pause (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf = GST_OSX_RING_BUFFER (buf);

  return gst_core_audio_pause_processing (osxbuf->core_audio);
}


static gboolean
gst_osx_ring_buffer_stop (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  gst_core_audio_stop_processing (osxbuf->core_audio);

  return TRUE;
}

static guint
gst_osx_ring_buffer_delay (GstRingBuffer * buf)
{
  GstOsxRingBuffer *osxbuf;
  double latency;
  guint samples;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  if (!gst_core_audio_get_samples_and_latency (osxbuf->core_audio,
          GST_RING_BUFFER (buf)->spec.rate, &samples, &latency)) {
    return 0;
  }
  GST_DEBUG_OBJECT (buf, "Got latency: %f seconds -> %d samples",
      latency, samples);
  return samples;
}
