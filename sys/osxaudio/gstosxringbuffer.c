/*
 * GStreamer
 * Copyright 2006 Zaheer Abbas Merali  <zaheerabbas at merali dot org>
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

#include <CoreAudio/CoreAudio.h>
#include <gst/gst.h>
#include "gstosxringbuffer.h"

GST_DEBUG_CATEGORY_STATIC (osx_audio_debug);
#define GST_CAT_DEFAULT osx_audio_debug

static void gst_osx_ring_buffer_class_init (GstOsxRingBufferClass * klass);
static void gst_osx_ring_buffer_init (GstOsxRingBuffer * ringbuffer,
    GstOsxRingBufferClass * g_class);
static void gst_osx_ring_buffer_dispose (GObject * object);
static void gst_osx_ring_buffer_finalize (GObject * object);
static gboolean gst_osx_ring_buffer_open_device (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_close_device (GstRingBuffer * buf);

static gboolean gst_osx_ring_buffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec);
static gboolean gst_osx_ring_buffer_release (GstRingBuffer * buf);

/* static gboolean gst_osx_ring_buffer_device_is_acquired (GstRingBuffer * buf); */

static gboolean gst_osx_ring_buffer_start (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_pause (GstRingBuffer * buf);
static gboolean gst_osx_ring_buffer_stop (GstRingBuffer * buf);
static guint gst_osx_ring_buffer_delay (GstRingBuffer * buf);
static GstRingBufferClass *ring_parent_class = NULL;

/* ringbuffer abstract base class */

GType
gst_osx_ring_buffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstOsxRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_osx_ring_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstOsxRingBuffer),
      0,
      (GInstanceInitFunc) gst_osx_ring_buffer_init,
      NULL
    };
    GST_DEBUG_CATEGORY_INIT (osx_audio_debug, "osxaudio", 0,
        "OSX Audio Elements");
    GST_DEBUG ("Creating osx ring buffer type\n");

    ringbuffer_type =
        g_type_register_static (GST_TYPE_RING_BUFFER, "GstOsxRingBuffer",
        &ringbuffer_info, 0);
  }
  return ringbuffer_type;
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

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_osx_ring_buffer_finalize);

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

  GST_DEBUG ("osx ring buffer class init\n");
}

static void
gst_osx_ring_buffer_init (GstOsxRingBuffer * ringbuffer,
    GstOsxRingBufferClass * g_class)
{
  OSStatus status;
  UInt32 propertySize;

  /* currently do bugger all */
  GST_DEBUG ("osx ring buffer init\n");
  propertySize = sizeof (ringbuffer->device_id);
  status =
      AudioHardwareGetProperty (kAudioHardwarePropertyDefaultOutputDevice,
      &propertySize, &(ringbuffer->device_id));
  GST_DEBUG ("osx ring buffer called AudioHardwareGetProperty\n");
  if (status) {
    GST_DEBUG ("AudioHardwareGetProperty returned %d\n", (int) status);
  } else {
    GST_DEBUG ("AudioHardwareGetProperty returned 0\n");
  }
  if (ringbuffer->device_id == kAudioDeviceUnknown) {
    GST_DEBUG ("AudioHardwareGetProperty: device_id is kAudioDeviceUnknown\n");
  }
  GST_DEBUG ("AudioHardwareGetProperty: device_id is %d\n",
      ringbuffer->device_id);
  /* get requested buffer length */
  propertySize = sizeof (ringbuffer->buffer_len);
  status =
      AudioDeviceGetProperty (ringbuffer->device_id, 0, false,
      kAudioDevicePropertyBufferSize, &propertySize, &ringbuffer->buffer_len);
  if (status) {
    GST_DEBUG
        ("AudioDeviceGetProperty returned %d when getting kAudioDevicePropertyBufferSize\n",
        (int) status);
  }
  GST_DEBUG ("%5d ringbuffer->buffer_len\n", (int) ringbuffer->buffer_len);
}

static void
gst_osx_ring_buffer_dispose (GObject * object)
{
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
  /* stub, we need to open device..maybe do nothing */
  return TRUE;
}

static gboolean
gst_osx_ring_buffer_close_device (GstRingBuffer * buf)
{
  /* stub, we need to close device..maybe do nothing */
  return TRUE;
}

static gboolean
gst_osx_ring_buffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  /* stub, we need to allocate ringbuffer memory */
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  spec->segsize = osxbuf->buffer_len;
  spec->segtotal = 16;

  GST_DEBUG ("osx ring buffer acquire\n");

  buf->data = gst_buffer_new_and_alloc (spec->segtotal * spec->segsize);
  memset (GST_BUFFER_DATA (buf->data), 0, GST_BUFFER_SIZE (buf->data));

  return TRUE;
}

static gboolean
gst_osx_ring_buffer_release (GstRingBuffer * buf)
{
  /* stub, we need to deallocate ringbuffer memory */
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  gst_buffer_unref (buf->data);
  buf->data = NULL;

  return TRUE;
}

static gboolean
gst_osx_ring_buffer_start (GstRingBuffer * buf)
{
  /* stub */
  OSErr status;
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  GST_DEBUG ("osx ring buffer start ioproc: 0x%x device_id %d\n",
      osxbuf->element->io_proc, osxbuf->device_id);
  status =
      AudioDeviceAddIOProc (osxbuf->device_id, osxbuf->element->io_proc,
      osxbuf);

  if (status) {
    GST_DEBUG ("AudioDeviceAddIOProc returned %d\n", (int) status);
    return FALSE;
  }
  status = AudioDeviceStart (osxbuf->device_id, osxbuf->element->io_proc);
  if (status) {
    GST_DEBUG ("AudioDeviceStart returned %d\n", (int) status);
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_osx_ring_buffer_pause (GstRingBuffer * buf)
{
  /* stub */
  /* stop callback */
  OSErr status;
  GstOsxRingBuffer *osxbuf = GST_OSX_RING_BUFFER (buf);

  status = AudioDeviceStop (osxbuf->device_id, osxbuf->element->io_proc);
  if (status)
    GST_DEBUG ("AudioDeviceStop returned %d\n", (int) status);
  return TRUE;
}

static gboolean
gst_osx_ring_buffer_stop (GstRingBuffer * buf)
{
  /* stub */
  OSErr status;
  GstOsxRingBuffer *osxbuf;

  osxbuf = GST_OSX_RING_BUFFER (buf);

  /* stop callback */
  status = AudioDeviceStop (osxbuf->device_id, osxbuf->element->io_proc);
  if (status)
    GST_DEBUG ("AudioDeviceStop returned %d\n", (int) status);

  status =
      AudioDeviceRemoveIOProc (osxbuf->device_id, osxbuf->element->io_proc);
  if (status)
    GST_DEBUG ("AudioDeviceRemoveIOProc " "returned %d\n", (int) status);

  return TRUE;
}

static guint
gst_osx_ring_buffer_delay (GstRingBuffer * buf)
{
  /* stub */
  return 0;
}
