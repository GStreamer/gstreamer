/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "ipc.h"

#include "gsta2dpsink.h"

GST_DEBUG_CATEGORY_STATIC (a2dp_sink_debug);
#define GST_CAT_DEFAULT a2dp_sink_debug

#define DEFAULT_DEVICE "default"

enum
{
  PROP_0,
  PROP_DEVICE,
};

GST_BOILERPLATE (GstA2dpSink, gst_a2dp_sink, GstAudioSink, GST_TYPE_AUDIO_SINK);

static const GstElementDetails a2dp_sink_details =
GST_ELEMENT_DETAILS ("Bluetooth A2DP sink",
    "Sink/Audio",
    "Plays audio to an A2DP device",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate a2dp_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "mode = (string) { mono, dual, stereo, joint }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation = (string) { snr, loudness }; "
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) { 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]"));

static void
gst_a2dp_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&a2dp_sink_factory));

  gst_element_class_set_details (element_class, &a2dp_sink_details);
}

static void
gst_a2dp_sink_finalize (GObject * object)
{
  GstA2dpSink *sink = GST_A2DP_SINK (object);

  g_io_channel_close (sink->server);

  g_free (sink->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_a2dp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstA2dpSink *sink = GST_A2DP_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (sink->device);
      sink->device = g_value_dup_string (value);

      if (sink->device == NULL)
        sink->device = g_strdup (DEFAULT_DEVICE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_a2dp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstA2dpSink *sink = GST_A2DP_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, sink->device);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_a2dp_sink_open (GstAudioSink * self)
{
  GstA2dpSink *sink = GST_A2DP_SINK (self);

  printf ("device %s\n", sink->device);
  printf ("open\n");

  return TRUE;
}

static gboolean
gst_a2dp_sink_prepare (GstAudioSink * self, GstRingBufferSpec * spec)
{
  printf ("perpare\n");
  printf ("rate %d\n", spec->rate);
  printf ("channels %d\n", spec->channels);

  return TRUE;
}

static gboolean
gst_a2dp_sink_unprepare (GstAudioSink * self)
{
  printf ("unprepare\n");

  return TRUE;
}

static gboolean
gst_a2dp_sink_close (GstAudioSink * self)
{
  printf ("close\n");

  return TRUE;
}

static guint
gst_a2dp_sink_write (GstAudioSink * self, gpointer data, guint length)
{
  return 0;
}

static guint
gst_a2dp_sink_delay (GstAudioSink * audiosink)
{
  printf ("delay\n");

  return 0;
}

static void
gst_a2dp_sink_reset (GstAudioSink * audiosink)
{
  printf ("reset\n");
}

static gboolean
server_callback (GIOChannel * chan, GIOCondition cond, gpointer data)
{
  printf ("callback\n");

  return TRUE;
}

static void
gst_a2dp_sink_class_init (GstA2dpSinkClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstAudioSinkClass *audiosink_class = GST_AUDIO_SINK_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_a2dp_sink_finalize);
  object_class->set_property = GST_DEBUG_FUNCPTR (gst_a2dp_sink_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_a2dp_sink_get_property);

  audiosink_class->open = GST_DEBUG_FUNCPTR (gst_a2dp_sink_open);
  audiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_a2dp_sink_prepare);
  audiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_a2dp_sink_unprepare);
  audiosink_class->close = GST_DEBUG_FUNCPTR (gst_a2dp_sink_close);
  audiosink_class->write = GST_DEBUG_FUNCPTR (gst_a2dp_sink_write);
  audiosink_class->delay = GST_DEBUG_FUNCPTR (gst_a2dp_sink_delay);
  audiosink_class->reset = GST_DEBUG_FUNCPTR (gst_a2dp_sink_reset);

  g_object_class_install_property (object_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Bluetooth remote device", DEFAULT_DEVICE, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (a2dp_sink_debug, "a2dpsink", 0, "A2DP sink element");
}

static void
gst_a2dp_sink_init (GstA2dpSink * self, GstA2dpSinkClass * klass)
{
  struct sockaddr_un addr = { AF_UNIX, IPC_SOCKET_NAME };
  int sk;

  self->device = g_strdup (DEFAULT_DEVICE);

  sk = socket (PF_LOCAL, SOCK_STREAM, 0);
  if (sk < 0)
    return;

  if (connect (sk, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    close (sk);
    return;
  }

  self->server = g_io_channel_unix_new (sk);

  g_io_add_watch (self->server, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
      server_callback, self);

  g_io_channel_unref (self->server);
}
