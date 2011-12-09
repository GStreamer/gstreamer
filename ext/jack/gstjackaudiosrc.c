/* GStreamer
 * Copyright (C) 2008 Tristan Matthews <tristan@sat.qc.ca>
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

/**
 * SECTION:element-jackaudiosrc
 * @see_also: #GstBaseAudioSrc, #GstRingBuffer
 *
 * A Src that inputs data from Jack ports.
 * 
 * It will create N Jack ports named in_&lt;name&gt;_&lt;num&gt; where 
 * &lt;name&gt; is the element name and &lt;num&gt; is starting from 1.
 * Each port corresponds to a gstreamer channel.
 * 
 * The samplerate as exposed on the caps is always the same as the samplerate of
 * the jack server.
 * 
 * When the #GstJackAudioSrc:connect property is set to auto, this element
 * will try to connect each input port to a random physical jack output pin. 
 * 
 * When the #GstJackAudioSrc:connect property is set to none, the element will
 * accept any number of output channels and will create (but not connect) an
 * input port for each channel.
 * 
 * The element will generate an error when the Jack server is shut down when it
 * was PAUSED or PLAYING. This element does not support dynamic rate and buffer
 * size changes at runtime.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch jackaudiosrc connect=0 ! jackaudiosink connect=0
 * ]| Get audio input into gstreamer from jack.
 * </refsect2>
 *
 * Last reviewed on 2008-07-22 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>
#include <stdlib.h>
#include <string.h>

#include "gstjackaudiosrc.h"
#include "gstjackringbuffer.h"
#include "gstjackutil.h"

GST_DEBUG_CATEGORY_STATIC (gst_jack_audio_src_debug);
#define GST_CAT_DEFAULT gst_jack_audio_src_debug

static gboolean
gst_jack_audio_src_allocate_channels (GstJackAudioSrc * src, gint channels)
{
  jack_client_t *client;

  client = gst_jack_audio_client_get_client (src->client);

  /* remove ports we don't need */
  while (src->port_count > channels)
    jack_port_unregister (client, src->ports[--src->port_count]);

  /* alloc enough input ports */
  src->ports = g_realloc (src->ports, sizeof (jack_port_t *) * channels);
  src->buffers = g_realloc (src->buffers, sizeof (sample_t *) * channels);

  /* create an input port for each channel */
  while (src->port_count < channels) {
    gchar *name;

    /* port names start from 1 and are local to the element */
    name =
        g_strdup_printf ("in_%s_%d", GST_ELEMENT_NAME (src),
        src->port_count + 1);
    src->ports[src->port_count] =
        jack_port_register (client, name, JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput, 0);
    if (src->ports[src->port_count] == NULL)
      return FALSE;

    src->port_count++;

    g_free (name);
  }
  return TRUE;
}

static void
gst_jack_audio_src_free_channels (GstJackAudioSrc * src)
{
  gint res, i = 0;
  jack_client_t *client;

  client = gst_jack_audio_client_get_client (src->client);

  /* get rid of all ports */
  while (src->port_count) {
    GST_LOG_OBJECT (src, "unregister port %d", i);
    if ((res = jack_port_unregister (client, src->ports[i++])))
      GST_DEBUG_OBJECT (src, "unregister of port failed (%d)", res);

    src->port_count--;
  }
  g_free (src->ports);
  src->ports = NULL;
  g_free (src->buffers);
  src->buffers = NULL;
}

/* ringbuffer abstract base class */
static GType
gst_jack_ring_buffer_get_type (void)
{
  static volatile gsize ringbuffer_type = 0;

  if (g_once_init_enter (&ringbuffer_type)) {
    static const GTypeInfo ringbuffer_info = { sizeof (GstJackRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_jack_ring_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstJackRingBuffer),
      0,
      (GInstanceInitFunc) gst_jack_ring_buffer_init,
      NULL
    };
    GType tmp = g_type_register_static (GST_TYPE_RING_BUFFER,
        "GstJackAudioSrcRingBuffer", &ringbuffer_info, 0);
    g_once_init_leave (&ringbuffer_type, tmp);
  }

  return (GType) ringbuffer_type;
}

static void
gst_jack_ring_buffer_class_init (GstJackRingBufferClass * klass)
{
  GstRingBufferClass *gstringbuffer_class;

  gstringbuffer_class = (GstRingBufferClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gstringbuffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_open_device);
  gstringbuffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_close_device);
  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_release);
  gstringbuffer_class->start = GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_start);
  gstringbuffer_class->pause = GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_pause);
  gstringbuffer_class->resume = GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_start);
  gstringbuffer_class->stop = GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_stop);

  gstringbuffer_class->delay = GST_DEBUG_FUNCPTR (gst_jack_ring_buffer_delay);
}

/* this is the callback of jack. This should be RT-safe.
 * Writes samples from the jack input port's buffer to the gst ring buffer.
 */
static int
jack_process_cb (jack_nframes_t nframes, void *arg)
{
  GstJackAudioSrc *src;
  GstRingBuffer *buf;
  gint len;
  guint8 *writeptr;
  gint writeseg;
  gint channels, i, j, flen;
  sample_t *data;

  buf = GST_RING_BUFFER_CAST (arg);
  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  channels = buf->spec.channels;

  /* get input buffers */
  for (i = 0; i < channels; i++)
    src->buffers[i] =
        (sample_t *) jack_port_get_buffer (src->ports[i], nframes);

  if (gst_ring_buffer_prepare_read (buf, &writeseg, &writeptr, &len)) {
    flen = len / channels;

    /* the number of samples must be exactly the segment size */
    if (nframes * sizeof (sample_t) != flen)
      goto wrong_size;

    /* the samples in the jack input buffers have to be interleaved into the
     * ringbuffer */
    data = (sample_t *) writeptr;
    for (i = 0; i < nframes; ++i)
      for (j = 0; j < channels; ++j)
        *data++ = src->buffers[j][i];

    GST_DEBUG ("copy %d frames: %p, %d bytes, %d channels", nframes, writeptr,
        len / channels, channels);

    /* we wrote one segment */
    gst_ring_buffer_advance (buf, 1);
  }
  return 0;

  /* ERRORS */
wrong_size:
  {
    GST_ERROR_OBJECT (src, "nbytes (%d) != flen (%d)",
        (gint) (nframes * sizeof (sample_t)), flen);
    return 1;
  }
}

/* we error out */
static int
jack_sample_rate_cb (jack_nframes_t nframes, void *arg)
{
  GstJackAudioSrc *src;
  GstJackRingBuffer *abuf;

  abuf = GST_JACK_RING_BUFFER_CAST (arg);
  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (arg));

  if (abuf->sample_rate != -1 && abuf->sample_rate != nframes)
    goto not_supported;

  return 0;

  /* ERRORS */
not_supported:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        (NULL), ("Jack changed the sample rate, which is not supported"));
    return 1;
  }
}

/* we error out */
static int
jack_buffer_size_cb (jack_nframes_t nframes, void *arg)
{
  GstJackAudioSrc *src;
  GstJackRingBuffer *abuf;

  abuf = GST_JACK_RING_BUFFER_CAST (arg);
  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (arg));

  if (abuf->buffer_size != -1 && abuf->buffer_size != nframes)
    goto not_supported;

  return 0;

  /* ERRORS */
not_supported:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        (NULL), ("Jack changed the buffer size, which is not supported"));
    return 1;
  }
}

static void
jack_shutdown_cb (void *arg)
{
  GstJackAudioSrc *src;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (arg));

  GST_DEBUG_OBJECT (src, "shutdown");

  GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
      (NULL), ("Jack server shutdown"));
}

static void
gst_jack_ring_buffer_init (GstJackRingBuffer * buf,
    GstJackRingBufferClass * g_class)
{
  buf->channels = -1;
  buf->buffer_size = -1;
  buf->sample_rate = -1;
}

/* the _open_device method should make a connection with the server
*/
static gboolean
gst_jack_ring_buffer_open_device (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;
  jack_status_t status = 0;
  const gchar *name;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  GST_DEBUG_OBJECT (src, "open");

  if (src->client_name) {
    name = src->client_name;
  } else {
    name = g_get_application_name ();
  }
  if (!name)
    name = "GStreamer";

  src->client = gst_jack_audio_client_new (name, src->server,
      src->jclient,
      GST_JACK_CLIENT_SOURCE,
      jack_shutdown_cb,
      jack_process_cb, jack_buffer_size_cb, jack_sample_rate_cb, buf, &status);
  if (src->client == NULL)
    goto could_not_open;

  GST_DEBUG_OBJECT (src, "opened");

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    if (status & (JackServerFailed | JackFailure)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
          (_("Jack server not found")),
          ("Cannot connect to the Jack server (status %d)", status));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          (NULL), ("Jack client open error (status %d)", status));
    }
    return FALSE;
  }
}

/* close the connection with the server
*/
static gboolean
gst_jack_ring_buffer_close_device (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  GST_DEBUG_OBJECT (src, "close");

  gst_jack_audio_src_free_channels (src);
  gst_jack_audio_client_free (src->client);
  src->client = NULL;

  return TRUE;
}


/* allocate a buffer and setup resources to process the audio samples of
 * the format as specified in @spec.
 *
 * We allocate N jack ports, one for each channel. If we are asked to
 * automatically make a connection with physical ports, we connect as many
 * ports as there are physical ports, leaving leftover ports unconnected.
 *
 * It is assumed that samplerate and number of channels are acceptable since our
 * getcaps method will always provide correct values. If unacceptable caps are
 * received for some reason, we fail here.
 */
static gboolean
gst_jack_ring_buffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  GstJackAudioSrc *src;
  GstJackRingBuffer *abuf;
  const char **ports;
  gint sample_rate, buffer_size;
  gint i, channels, res;
  jack_client_t *client;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));
  abuf = GST_JACK_RING_BUFFER_CAST (buf);

  GST_DEBUG_OBJECT (src, "acquire");

  client = gst_jack_audio_client_get_client (src->client);

  /* sample rate must be that of the server */
  sample_rate = jack_get_sample_rate (client);
  if (sample_rate != spec->rate)
    goto wrong_samplerate;

  channels = spec->channels;

  if (!gst_jack_audio_src_allocate_channels (src, channels))
    goto out_of_ports;

  gst_jack_set_layout_on_caps (&spec->caps, channels);

  buffer_size = jack_get_buffer_size (client);

  /* the segment size in bytes, this is large enough to hold a buffer of 32bit floats
   * for all channels  */
  spec->segsize = buffer_size * sizeof (gfloat) * channels;
  spec->latency_time = gst_util_uint64_scale (spec->segsize,
      (GST_SECOND / GST_USECOND), spec->rate * spec->bytes_per_sample);
  /* segtotal based on buffer-time latency */
  spec->segtotal = spec->buffer_time / spec->latency_time;
  if (spec->segtotal < 2) {
    spec->segtotal = 2;
    spec->buffer_time = spec->latency_time * spec->segtotal;
  }

  GST_DEBUG_OBJECT (src, "buffer time: %" G_GINT64_FORMAT " usec",
      spec->buffer_time);
  GST_DEBUG_OBJECT (src, "latency time: %" G_GINT64_FORMAT " usec",
      spec->latency_time);
  GST_DEBUG_OBJECT (src, "buffer_size %d, segsize %d, segtotal %d",
      buffer_size, spec->segsize, spec->segtotal);

  /* allocate the ringbuffer memory now */
  buf->data = gst_buffer_new_and_alloc (spec->segtotal * spec->segsize);
  memset (GST_BUFFER_DATA (buf->data), 0, GST_BUFFER_SIZE (buf->data));

  if ((res = gst_jack_audio_client_set_active (src->client, TRUE)))
    goto could_not_activate;

  /* if we need to automatically connect the ports, do so now. We must do this
   * after activating the client. */
  if (src->connect == GST_JACK_CONNECT_AUTO
      || src->connect == GST_JACK_CONNECT_AUTO_FORCED) {
    /* find all the physical output ports. A physical output port is a port
     * associated with a hardware device. Someone needs connect to a physical
     * port in order to capture something. */
    ports =
        jack_get_ports (client, NULL, NULL,
        JackPortIsPhysical | JackPortIsOutput);
    if (ports == NULL) {
      /* no ports? fine then we don't do anything except for posting a warning
       * message. */
      GST_ELEMENT_WARNING (src, RESOURCE, NOT_FOUND, (NULL),
          ("No physical output ports found, leaving ports unconnected"));
      goto done;
    }

    for (i = 0; i < channels; i++) {
      /* stop when all output ports are exhausted */
      if (ports[i] == NULL) {
        /* post a warning that we could not connect all ports */
        GST_ELEMENT_WARNING (src, RESOURCE, NOT_FOUND, (NULL),
            ("No more physical ports, leaving some ports unconnected"));
        break;
      }
      GST_DEBUG_OBJECT (src, "try connecting to %s",
          jack_port_name (src->ports[i]));

      /* connect the physical port to a port */
      res = jack_connect (client, ports[i], jack_port_name (src->ports[i]));
      if (res != 0 && res != EEXIST)
        goto cannot_connect;
    }
    free (ports);
  }
done:

  abuf->sample_rate = sample_rate;
  abuf->buffer_size = buffer_size;
  abuf->channels = spec->channels;

  return TRUE;

  /* ERRORS */
wrong_samplerate:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Wrong samplerate, server is running at %d and we received %d",
            sample_rate, spec->rate));
    return FALSE;
  }
out_of_ports:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Cannot allocate more Jack ports"));
    return FALSE;
  }
could_not_activate:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Could not activate client (%d:%s)", res, g_strerror (res)));
    return FALSE;
  }
cannot_connect:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Could not connect input ports to physical ports (%d:%s)",
            res, g_strerror (res)));
    free (ports);
    return FALSE;
  }
}

/* function is called with LOCK */
static gboolean
gst_jack_ring_buffer_release (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;
  GstJackRingBuffer *abuf;
  gint res;

  abuf = GST_JACK_RING_BUFFER_CAST (buf);
  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  GST_DEBUG_OBJECT (src, "release");

  if ((res = gst_jack_audio_client_set_active (src->client, FALSE))) {
    /* we only warn, this means the server is probably shut down and the client
     * is gone anyway. */
    GST_ELEMENT_WARNING (src, RESOURCE, CLOSE, (NULL),
        ("Could not deactivate Jack client (%d)", res));
  }

  abuf->channels = -1;
  abuf->buffer_size = -1;
  abuf->sample_rate = -1;

  /* free the buffer */
  gst_buffer_unref (buf->data);
  buf->data = NULL;

  return TRUE;
}

static gboolean
gst_jack_ring_buffer_start (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  GST_DEBUG_OBJECT (src, "start");

  return TRUE;
}

static gboolean
gst_jack_ring_buffer_pause (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  GST_DEBUG_OBJECT (src, "pause");

  return TRUE;
}

static gboolean
gst_jack_ring_buffer_stop (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  GST_DEBUG_OBJECT (src, "stop");

  return TRUE;
}

#if defined (HAVE_JACK_0_120_1) || defined(HAVE_JACK_1_9_7)
static guint
gst_jack_ring_buffer_delay (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;
  guint i, res = 0;
  jack_latency_range_t range;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  for (i = 0; i < src->port_count; i++) {
    jack_port_get_latency_range (src->ports[i], JackCaptureLatency, &range);
    if (range.max > res)
      res = range.max;
  }

  GST_DEBUG_OBJECT (src, "delay %u", res);

  return res;
}
#else /* !(defined (HAVE_JACK_0_120_1) || defined(HAVE_JACK_1_9_7)) */
static guint
gst_jack_ring_buffer_delay (GstRingBuffer * buf)
{
  GstJackAudioSrc *src;
  guint i, res = 0;
  guint latency;
  jack_client_t *client;

  src = GST_JACK_AUDIO_SRC (GST_OBJECT_PARENT (buf));

  client = gst_jack_audio_client_get_client (src->client);

  for (i = 0; i < src->port_count; i++) {
    latency = jack_port_get_total_latency (client, src->ports[i]);
    if (latency > res)
      res = latency;
  }

  GST_DEBUG_OBJECT (src, "delay %u", res);

  return res;
}
#endif

/* Audiosrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_PROP_CONNECT 		GST_JACK_CONNECT_AUTO
#define DEFAULT_PROP_SERVER 		NULL
#define DEFAULT_PROP_CLIENT_NAME	NULL

enum
{
  PROP_0,
  PROP_CONNECT,
  PROP_SERVER,
  PROP_CLIENT,
  PROP_CLIENT_NAME,
  PROP_LAST
};


/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 32, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]")
    );

#define _do_init(bla) \
  GST_DEBUG_CATEGORY_INIT(gst_jack_audio_src_debug, "jacksrc", 0, "jacksrc element");

GST_BOILERPLATE_FULL (GstJackAudioSrc, gst_jack_audio_src, GstBaseAudioSrc,
    GST_TYPE_BASE_AUDIO_SRC, _do_init);

static void gst_jack_audio_src_dispose (GObject * object);
static void gst_jack_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_jack_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_jack_audio_src_getcaps (GstBaseSrc * bsrc);
static GstRingBuffer *gst_jack_audio_src_create_ringbuffer (GstBaseAudioSrc *
    src);

/* GObject vmethod implementations */

static void
gst_jack_audio_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_set_details_simple (element_class, "Audio Source (Jack)",
      "Source/Audio", "Captures audio from a JACK server",
      "Tristan Matthews <tristan@sat.qc.ca>");
}

/* initialize the jack_audio_src's class */
static void
gst_jack_audio_src_class_init (GstJackAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;

  gobject_class = (GObjectClass *) klass;

  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;

  gobject_class->dispose = gst_jack_audio_src_dispose;
  gobject_class->set_property = gst_jack_audio_src_set_property;
  gobject_class->get_property = gst_jack_audio_src_get_property;

  g_object_class_install_property (gobject_class, PROP_CONNECT,
      g_param_spec_enum ("connect", "Connect",
          "Specify how the input ports will be connected",
          GST_TYPE_JACK_CONNECT, DEFAULT_PROP_CONNECT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The Jack server to connect to (NULL = default)",
          DEFAULT_PROP_SERVER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstJackAudioSrc:client-name
   *
   * The client name to use.
   *
   * Since: 0.10.31
   */
  g_object_class_install_property (gobject_class, PROP_CLIENT_NAME,
      g_param_spec_string ("client-name", "Client name",
          "The client name of the Jack instance (NULL = default)",
          DEFAULT_PROP_CLIENT_NAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLIENT,
      g_param_spec_boxed ("client", "JackClient", "Handle for jack client",
          GST_TYPE_JACK_CLIENT,
          GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_jack_audio_src_getcaps);
  gstbaseaudiosrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_jack_audio_src_create_ringbuffer);

  /* ref class from a thread-safe context to work around missing bit of
   * thread-safety in GObject */
  g_type_class_ref (GST_TYPE_JACK_RING_BUFFER);

  gst_jack_audio_client_init ();
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_jack_audio_src_init (GstJackAudioSrc * src, GstJackAudioSrcClass * gclass)
{
  //gst_base_src_set_live(GST_BASE_SRC (src), TRUE);
  src->connect = DEFAULT_PROP_CONNECT;
  src->server = g_strdup (DEFAULT_PROP_SERVER);
  src->jclient = NULL;
  src->ports = NULL;
  src->port_count = 0;
  src->buffers = NULL;
  src->client_name = g_strdup (DEFAULT_PROP_CLIENT_NAME);
}

static void
gst_jack_audio_src_dispose (GObject * object)
{
  GstJackAudioSrc *src = GST_JACK_AUDIO_SRC (object);

  gst_caps_replace (&src->caps, NULL);

  if (src->client_name != NULL) {
    g_free (src->client_name);
    src->client_name = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_jack_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJackAudioSrc *src = GST_JACK_AUDIO_SRC (object);

  switch (prop_id) {
    case PROP_CLIENT_NAME:
      g_free (src->client_name);
      src->client_name = g_value_dup_string (value);
      break;
    case PROP_CONNECT:
      src->connect = g_value_get_enum (value);
      break;
    case PROP_SERVER:
      g_free (src->server);
      src->server = g_value_dup_string (value);
      break;
    case PROP_CLIENT:
      if (GST_STATE (src) == GST_STATE_NULL ||
          GST_STATE (src) == GST_STATE_READY) {
        src->jclient = g_value_get_boxed (value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_jack_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstJackAudioSrc *src = GST_JACK_AUDIO_SRC (object);

  switch (prop_id) {
    case PROP_CLIENT_NAME:
      g_value_set_string (value, src->client_name);
      break;
    case PROP_CONNECT:
      g_value_set_enum (value, src->connect);
      break;
    case PROP_SERVER:
      g_value_set_string (value, src->server);
      break;
    case PROP_CLIENT:
      g_value_set_boxed (value, src->jclient);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_jack_audio_src_getcaps (GstBaseSrc * bsrc)
{
  GstJackAudioSrc *src = GST_JACK_AUDIO_SRC (bsrc);
  const char **ports;
  gint min, max;
  gint rate;
  jack_client_t *client;

  if (src->client == NULL)
    goto no_client;

  client = gst_jack_audio_client_get_client (src->client);

  if (src->connect == GST_JACK_CONNECT_AUTO) {
    /* get a port count, this is the number of channels we can automatically
     * connect. */
    ports = jack_get_ports (client, NULL, NULL,
        JackPortIsPhysical | JackPortIsOutput);
    max = 0;
    if (ports != NULL) {
      for (; ports[max]; max++);

      free (ports);
    } else
      max = 0;
  } else {
    /* we allow any number of pads, something else is going to connect the
     * pads. */
    max = G_MAXINT;
  }
  min = MIN (1, max);

  rate = jack_get_sample_rate (client);

  GST_DEBUG_OBJECT (src, "got %d-%d ports, samplerate: %d", min, max, rate);

  if (!src->caps) {
    src->caps = gst_caps_new_simple ("audio/x-raw-float",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, 32,
        "rate", G_TYPE_INT, rate,
        "channels", GST_TYPE_INT_RANGE, min, max, NULL);
  }
  GST_INFO_OBJECT (src, "returning caps %" GST_PTR_FORMAT, src->caps);

  return gst_caps_ref (src->caps);

  /* ERRORS */
no_client:
  {
    GST_DEBUG_OBJECT (src, "device not open, using template caps");
    /* base class will get template caps for us when we return NULL */
    return NULL;
  }
}

static GstRingBuffer *
gst_jack_audio_src_create_ringbuffer (GstBaseAudioSrc * src)
{
  GstRingBuffer *buffer;

  buffer = g_object_new (GST_TYPE_JACK_RING_BUFFER, NULL);
  GST_DEBUG_OBJECT (src, "created ringbuffer @%p", buffer);

  return buffer;
}
