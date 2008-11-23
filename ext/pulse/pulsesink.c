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

/**
 * SECTION:element-pulsesink
 * @short_description: Output audio to a PulseAudio sound server
 * @see_also: pulsesrc, pulsemixer
 *
 * <refsect2>
 * <para>
 * This element outputs audio to a PulseAudio sound server.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! pulsesink
 * </programlisting>
 * Play an Ogg/Vorbis file.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch -v audiotestsrc ! audioconvert ! volume volume=0.4 ! pulsesink
 * </programlisting>
 * Play a 440Hz sine wave.
 * </para>
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/base/gstbasesink.h>
#include <gst/gsttaglist.h>

#include "pulsesink.h"
#include "pulseutil.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

/* according to
 * http://www.pulseaudio.org/ticket/314
 * we need pulse-0.9.12 to use sink volume properties
 */
/*#define HAVE_PULSE_0_9_12 */

enum
{
  PROP_SERVER = 1,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_VOLUME
};

static void gst_pulsesink_destroy_stream (GstPulseSink * pulsesink);

static void gst_pulsesink_destroy_context (GstPulseSink * pulsesink);

static void gst_pulsesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulsesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulsesink_finalize (GObject * object);

static void gst_pulsesink_dispose (GObject * object);

static gboolean gst_pulsesink_open (GstAudioSink * asink);

static gboolean gst_pulsesink_close (GstAudioSink * asink);

static gboolean gst_pulsesink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_pulsesink_unprepare (GstAudioSink * asink);

static guint gst_pulsesink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_pulsesink_delay (GstAudioSink * asink);

static void gst_pulsesink_reset (GstAudioSink * asink);

static gboolean gst_pulsesink_event (GstBaseSink * sink, GstEvent * event);

static GstStateChangeReturn gst_pulsesink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_pulsesink_init_interfaces (GType type);

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
# define ENDIANNESS   "LITTLE_ENDIAN, BIG_ENDIAN"
#else
# define ENDIANNESS   "BIG_ENDIAN, LITTLE_ENDIAN"
#endif

GST_IMPLEMENT_PULSEPROBE_METHODS (GstPulseSink, gst_pulsesink);
GST_BOILERPLATE_FULL (GstPulseSink, gst_pulsesink, GstAudioSink,
    GST_TYPE_AUDIO_SINK, gst_pulsesink_init_interfaces);

static gboolean
gst_pulsesink_interface_supported (GstImplementsInterface *
    iface, GType interface_type)
{
  GstPulseSink *this = GST_PULSESINK (iface);

  if (interface_type == GST_TYPE_PROPERTY_PROBE && this->probe)
    return TRUE;

  return FALSE;
}

static void
gst_pulsesink_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_pulsesink_interface_supported;
}

static void
gst_pulsesink_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_pulsesink_implements_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo probe_iface_info = {
    (GInterfaceInitFunc) gst_pulsesink_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}

static void
gst_pulsesink_base_init (gpointer g_class)
{

  static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 16, "
          "depth = (int) 16, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-float, "
          "endianness = (int) { " ENDIANNESS " }, "
          "width = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 32, "
          "depth = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-int, "
          "signed = (boolean) FALSE, "
          "width = (int) 8, "
          "depth = (int) 8, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-alaw, "
          "rate = (int) [ 1, MAX], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-mulaw, "
          "rate = (int) [ 1, MAX], " "channels = (int) [ 1, 16 ]")
      );

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "PulseAudio Audio Sink",
      "Sink/Audio", "Plays audio to a PulseAudio server", "Lennart Poettering");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&pad_template));
}

static void
gst_pulsesink_class_init (GstPulseSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioSinkClass *gstaudiosink_class = GST_AUDIO_SINK_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pulsesink_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pulsesink_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pulsesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pulsesink_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pulsesink_change_state);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_pulsesink_event);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_pulsesink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_pulsesink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_pulsesink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_pulsesink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_pulsesink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_pulsesink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_pulsesink_reset);

  /* Overwrite GObject fields */
  g_object_class_install_property (gobject_class,
      PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The PulseAudio server to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Sink",
          "The PulseAudio sink device to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
#ifdef HAVE_PULSE_0_9_12
  g_object_class_install_property (gobject_class,
      PROP_VOLUME,
      g_param_spec_double ("volume", "Volume",
          "Volume of this stream", 0.0, 10.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
}

static void
gst_pulsesink_init (GstPulseSink * pulsesink, GstPulseSinkClass * klass)
{
  int e;

  pulsesink->server = pulsesink->device = pulsesink->stream_name = NULL;

  pulsesink->context = NULL;
  pulsesink->stream = NULL;

  pulsesink->mainloop = pa_threaded_mainloop_new ();
  g_assert (pulsesink->mainloop);

  e = pa_threaded_mainloop_start (pulsesink->mainloop);
  g_assert (e == 0);

  pulsesink->probe = gst_pulseprobe_new (G_OBJECT (pulsesink), G_OBJECT_GET_CLASS (pulsesink), PROP_DEVICE, pulsesink->device, TRUE, FALSE);    /* TRUE for sinks, FALSE for sources */
  pulsesink->mixer = NULL;
}

static void
gst_pulsesink_destroy_stream (GstPulseSink * pulsesink)
{
  if (pulsesink->stream) {
    pa_stream_disconnect (pulsesink->stream);
    pa_threaded_mainloop_wait (pulsesink->mainloop);
    pa_stream_unref (pulsesink->stream);
    pulsesink->stream = NULL;
  }

  g_free (pulsesink->stream_name);
  pulsesink->stream_name = NULL;
}

static void
gst_pulsesink_destroy_context (GstPulseSink * pulsesink)
{

  gst_pulsesink_destroy_stream (pulsesink);

  if (pulsesink->context) {
    pa_context_disconnect (pulsesink->context);
    pa_context_unref (pulsesink->context);
    pulsesink->context = NULL;
  }
}

static void
gst_pulsesink_finalize (GObject * object)
{
  GstPulseSink *pulsesink = GST_PULSESINK (object);

  pa_threaded_mainloop_stop (pulsesink->mainloop);

  gst_pulsesink_destroy_context (pulsesink);

  g_free (pulsesink->server);
  g_free (pulsesink->device);
  g_free (pulsesink->stream_name);

  pa_threaded_mainloop_free (pulsesink->mainloop);

  if (pulsesink->probe) {
    gst_pulseprobe_free (pulsesink->probe);
    pulsesink->probe = NULL;
  }

  if (pulsesink->mixer) {
    gst_pulsemixer_ctrl_free (pulsesink->mixer);
    pulsesink->mixer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pulsesink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

#ifdef HAVE_PULSE_0_9_12
static void
gst_pulsesink_set_volume (GstPulseSink * pulsesink, gdouble volume)
{
  if (pulsesink->mixer && pulsesink->mixer->track->num_channels > 0) {
    gint *volumes = g_new0 (gint, pulsesink->mixer->track->num_channels);
    gint i;

    g_print ("setting volume for real\n");

    for (i = 0; i < pulsesink->mixer->track->num_channels; i++)
      volumes[i] = volume;

    gst_pulsemixer_ctrl_set_volume (pulsesink->mixer, pulsesink->mixer->track,
        volumes);

    pulsesink->volume = volume;
    g_free (volumes);
  } else {
    pulsesink->volume = volume;
  }
}

static gdouble
gst_pulsesink_get_volume (GstPulseSink * pulsesink)
{
  if (pulsesink->mixer && pulsesink->mixer->track->num_channels > 0) {
    gint *volumes = g_new0 (gint, pulsesink->mixer->track->num_channels);
    gdouble volume = 0.0;
    gint i;

    gst_pulsemixer_ctrl_get_volume (pulsesink->mixer, pulsesink->mixer->track,
        volumes);

    for (i = 0; i < pulsesink->mixer->track->num_channels; i++)
      volume += volumes[i];
    volume /= pulsesink->mixer->track->num_channels;

    pulsesink->volume = volume;

    g_free (volumes);

    g_print ("real volume: %lf\n", volume);

    return volume;
  } else {
    return pulsesink->volume;
  }
}
#endif

static void
gst_pulsesink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPulseSink *pulsesink = GST_PULSESINK (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_free (pulsesink->server);
      pulsesink->server = g_value_dup_string (value);

      if (pulsesink->probe)
        gst_pulseprobe_set_server (pulsesink->probe, pulsesink->server);

      break;

    case PROP_DEVICE:
      g_free (pulsesink->device);
      pulsesink->device = g_value_dup_string (value);
      break;

#ifdef HAVE_PULSE_0_9_12
    case PROP_VOLUME:
      gst_pulsesink_set_volume (pulsesink, g_value_get_double (value));
      break;
#endif

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{

  GstPulseSink *pulsesink = GST_PULSESINK (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, pulsesink->server);
      break;

    case PROP_DEVICE:
      g_value_set_string (value, pulsesink->device);
      break;

    case PROP_DEVICE_NAME:
      if (pulsesink->mixer)
        g_value_set_string (value, pulsesink->mixer->description);
      else
        g_value_set_string (value, NULL);
      break;

#ifdef HAVE_PULSE_0_9_12
    case PROP_VOLUME:
      g_value_set_double (value, gst_pulsesink_get_volume (pulsesink));
      break;
#endif

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesink_context_state_cb (pa_context * c, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  switch (pa_context_get_state (c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

static void
gst_pulsesink_stream_state_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  switch (pa_stream_get_state (s)) {

    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
      pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
      break;

    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      break;
  }
}

static void
gst_pulsesink_stream_request_cb (pa_stream * s, size_t length, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static void
gst_pulsesink_stream_latency_update_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static gboolean
gst_pulsesink_open (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  gchar *name = gst_pulse_client_name ();
  pa_context_state_t state;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!(pulsesink->context =
          pa_context_new (pa_threaded_mainloop_get_api (pulsesink->mainloop),
              name))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to create context"), (NULL));
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (pulsesink->context,
      gst_pulsesink_context_state_cb, pulsesink);

  if (pa_context_connect (pulsesink->context, pulsesink->server, 0, NULL) < 0) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* Wait until the context is ready */
  pa_threaded_mainloop_wait (pulsesink->mainloop);

  state = pa_context_get_state (pulsesink->context);
  if (state != PA_CONTEXT_READY) {
    GST_DEBUG_OBJECT (pulsesink, "Context state was not READY. Got: %d", state);
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  g_free (name);
  return TRUE;

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  g_free (name);
  return FALSE;
}

static gboolean
gst_pulsesink_close (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  pa_threaded_mainloop_lock (pulsesink->mainloop);
  gst_pulsesink_destroy_context (pulsesink);
  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return TRUE;
}

static gboolean
gst_pulsesink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  pa_buffer_attr buf_attr;
  pa_channel_map channel_map;
  pa_stream_state_t s_state;
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  if (!gst_pulse_fill_sample_spec (spec, &pulsesink->sample_spec)) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, SETTINGS,
        ("Invalid sample specification."), (NULL));
    goto fail;
  }

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!pulsesink->context
      || pa_context_get_state (pulsesink->context) != PA_CONTEXT_READY) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Bad context state: %s",
            pulsesink->
            context ? pa_strerror (pa_context_errno (pulsesink->context)) :
            NULL), (NULL));
    goto unlock_and_fail;
  }

  if (!(pulsesink->stream = pa_stream_new (pulsesink->context,
              pulsesink->
              stream_name ? pulsesink->stream_name : "Playback Stream",
              &pulsesink->sample_spec,
              gst_pulse_gst_to_channel_map (&channel_map, spec)))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to create stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_stream_set_state_callback (pulsesink->stream,
      gst_pulsesink_stream_state_cb, pulsesink);
  pa_stream_set_write_callback (pulsesink->stream,
      gst_pulsesink_stream_request_cb, pulsesink);
  pa_stream_set_latency_update_callback (pulsesink->stream,
      gst_pulsesink_stream_latency_update_cb, pulsesink);

  memset (&buf_attr, 0, sizeof (buf_attr));
  buf_attr.tlength = spec->segtotal * spec->segsize;
  buf_attr.maxlength = buf_attr.tlength * 2;
  buf_attr.prebuf = buf_attr.tlength - spec->segsize;
  buf_attr.minreq = spec->segsize;

  if (pa_stream_connect_playback (pulsesink->stream, pulsesink->device,
          &buf_attr,
          PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
          PA_STREAM_NOT_MONOTONOUS, NULL, NULL) < 0) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* Wait until the stream is ready */
  pa_threaded_mainloop_wait (pulsesink->mainloop);

  s_state = pa_stream_get_state (pulsesink->stream);
  if (s_state != PA_STREAM_READY) {
    GST_DEBUG_OBJECT (pulsesink, "Stream state was not READY. Got: %d",
        s_state);
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

#ifdef HAVE_PULSE_0_9_12
  gst_pulsesink_set_volume (pulsesink, pulsesink->volume);
#endif

  return TRUE;

unlock_and_fail:
  pa_threaded_mainloop_unlock (pulsesink->mainloop);

fail:
  return FALSE;
}

static gboolean
gst_pulsesink_unprepare (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  pa_threaded_mainloop_lock (pulsesink->mainloop);
  gst_pulsesink_destroy_stream (pulsesink);
  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return TRUE;
}

#define CHECK_DEAD_GOTO(pulsesink, label) \
if (!(pulsesink)->context || pa_context_get_state((pulsesink)->context) != PA_CONTEXT_READY || \
    !(pulsesink)->stream || pa_stream_get_state((pulsesink)->stream) != PA_STREAM_READY) { \
    GST_ELEMENT_ERROR((pulsesink), RESOURCE, FAILED, ("Disconnected: %s", (pulsesink)->context ? pa_strerror(pa_context_errno((pulsesink)->context)) : NULL), (NULL)); \
    goto label; \
}

static guint
gst_pulsesink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  size_t sum = 0;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  while (length > 0) {
    size_t l;

    for (;;) {
      CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

      if ((l = pa_stream_writable_size (pulsesink->stream)) == (size_t) - 1) {
        GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
            ("pa_stream_writable_size() failed: %s",
                pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
        goto unlock_and_fail;
      }

      if (l > 0)
        break;

      pa_threaded_mainloop_wait (pulsesink->mainloop);
    }

    if (l > length)
      l = length;

    if (pa_stream_write (pulsesink->stream, data, l, NULL, 0,
            PA_SEEK_RELATIVE) < 0) {
      GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
          ("pa_stream_write() failed: %s",
              pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
      goto unlock_and_fail;
    }

    data = (guint8 *) data + l;
    length -= l;

    sum += l;
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return sum;

  /* ERRORS */
unlock_and_fail:
  {
    pa_threaded_mainloop_unlock (pulsesink->mainloop);
    return -1;
  }
}

static guint
gst_pulsesink_delay (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  pa_usec_t t;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  for (;;) {
    CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

    if (pa_stream_get_latency (pulsesink->stream, &t, NULL) >= 0)
      break;

    if (pa_context_errno (pulsesink->context) != PA_ERR_NODATA) {
      GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
          ("pa_stream_get_latency() failed: %s",
              pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
      goto unlock_and_fail;
    }

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return gst_util_uint64_scale_int (t, pulsesink->sample_spec.rate, 1000000LL);

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return 0;
}

static void
gst_pulsesink_success_cb (pa_stream * s, int success, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  pulsesink->operation_success = success;
  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static void
gst_pulsesink_reset (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

  if (!(o =
          pa_stream_flush (pulsesink->stream, gst_pulsesink_success_cb,
              pulsesink))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_flush() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pulsesink->operation_success = 0;
  while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
    CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

  if (!pulsesink->operation_success) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Flush failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

unlock_and_fail:

  if (o) {
    pa_operation_cancel (o);
    pa_operation_unref (o);
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}

static void
gst_pulsesink_change_title (GstPulseSink * pulsesink, const gchar * t)
{
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  g_free (pulsesink->stream_name);
  pulsesink->stream_name = g_strdup (t);

  if (!(pulsesink)->context
      || pa_context_get_state ((pulsesink)->context) != PA_CONTEXT_READY
      || !(pulsesink)->stream
      || pa_stream_get_state ((pulsesink)->stream) != PA_STREAM_READY) {
    goto unlock_and_fail;
  }

  if (!(o =
          pa_stream_set_name (pulsesink->stream, pulsesink->stream_name, NULL,
              pulsesink))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_set_name() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* We're not interested if this operation failed or not */

unlock_and_fail:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}

static gboolean
gst_pulsesink_event (GstBaseSink * sink, GstEvent * event)
{
  GstPulseSink *pulsesink = GST_PULSESINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      gchar *title = NULL, *artist = NULL, *location = NULL, *description =
          NULL, *t = NULL, *buf = NULL;
      GstTagList *l;

      gst_event_parse_tag (event, &l);

      gst_tag_list_get_string (l, GST_TAG_TITLE, &title);
      gst_tag_list_get_string (l, GST_TAG_ARTIST, &artist);
      gst_tag_list_get_string (l, GST_TAG_LOCATION, &location);
      gst_tag_list_get_string (l, GST_TAG_DESCRIPTION, &description);

      if (title && artist)
        t = buf =
            g_strdup_printf ("'%s' by '%s'", g_strstrip (title),
            g_strstrip (artist));
      else if (title)
        t = g_strstrip (title);
      else if (description)
        t = g_strstrip (description);
      else if (location)
        t = g_strstrip (location);

      if (t)
        gst_pulsesink_change_title (pulsesink, t);

      g_free (title);
      g_free (artist);
      g_free (location);
      g_free (description);
      g_free (buf);

      break;
    }
    default:
      ;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static GstStateChangeReturn
gst_pulsesink_change_state (GstElement * element, GstStateChange transition)
{
  GstPulseSink *this = GST_PULSESINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:

      if (!this->mixer) {
        this->mixer =
            gst_pulsemixer_ctrl_new (G_OBJECT (this), this->server,
            this->device, GST_PULSEMIXER_SINK);
      }
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:

      if (this->mixer) {
#ifdef HAVE_PULSE_0_9_12
        this->volume = gst_pulsesink_get_volume (this);
#endif
        gst_pulsemixer_ctrl_free (this->mixer);
        this->mixer = NULL;
      }

      break;

    default:
      ;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
