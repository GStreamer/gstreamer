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
 * @see_also: pulsesrc, pulsemixer
 *
 * This element outputs audio to a
 * <ulink href="http://www.pulseaudio.org">PulseAudio sound server</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! pulsesink
 * ]| Play an Ogg/Vorbis file.
 * |[
 * gst-launch -v audiotestsrc ! audioconvert ! volume volume=0.4 ! pulsesink
 * ]| Play a 440Hz sine wave.
 * </refsect2>
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

static gboolean gst_pulsesink_is_dead (GstPulseSink * pulsesink);

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
          "channels = (int) [ 1, 32 ];"
          "audio/x-raw-float, "
          "endianness = (int) { " ENDIANNESS " }, "
          "width = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 32, "
          "depth = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-raw-int, "
          "signed = (boolean) FALSE, "
          "width = (int) 8, "
          "depth = (int) 8, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-alaw, "
          "rate = (int) [ 1, MAX], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-mulaw, "
          "rate = (int) [ 1, MAX], " "channels = (int) [ 1, 32 ]")
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
#if HAVE_PULSE_0_9_12
  g_object_class_install_property (gobject_class,
      PROP_VOLUME,
      g_param_spec_double ("volume", "Volume",
          "Volume of this stream", 0.0, 1000.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
}

static void
gst_pulsesink_init (GstPulseSink * pulsesink, GstPulseSinkClass * klass)
{
  int e;

  pulsesink->server = pulsesink->device = pulsesink->stream_name =
      pulsesink->device_description = NULL;

  pulsesink->context = NULL;
  pulsesink->stream = NULL;

  pulsesink->volume = 1.0;
  pulsesink->volume_set = FALSE;

#if HAVE_PULSE_0_9_13
  pa_sample_spec_init (&pulsesink->sample_spec);
#else
  pulsesink->sample_spec.format = PA_SAMPLE_INVALID;
  pulsesink->sample_spec.rate = 0;
  pulsesink->sample_spec.channels = 0;
#endif

  pulsesink->operation_success = FALSE;
  pulsesink->did_reset = FALSE;
  pulsesink->in_write = FALSE;

  pulsesink->mainloop = pa_threaded_mainloop_new ();
  g_assert (pulsesink->mainloop);

  e = pa_threaded_mainloop_start (pulsesink->mainloop);
  g_assert (e == 0);

  pulsesink->probe = gst_pulseprobe_new (G_OBJECT (pulsesink), G_OBJECT_GET_CLASS (pulsesink), PROP_DEVICE, pulsesink->device, TRUE, FALSE);    /* TRUE for sinks, FALSE for sources */
}

static void
gst_pulsesink_destroy_stream (GstPulseSink * pulsesink)
{
  if (pulsesink->stream) {
    pa_stream_disconnect (pulsesink->stream);

    /* Make sure we don't get any further callbacks */
    pa_stream_set_state_callback (pulsesink->stream, NULL, NULL);
    pa_stream_set_write_callback (pulsesink->stream, NULL, NULL);
    pa_stream_set_latency_update_callback (pulsesink->stream, NULL, NULL);

    pa_stream_unref (pulsesink->stream);
    pulsesink->stream = NULL;
  }

  g_free (pulsesink->stream_name);
  pulsesink->stream_name = NULL;

  g_free (pulsesink->device_description);
  pulsesink->device_description = NULL;
}

static void
gst_pulsesink_destroy_context (GstPulseSink * pulsesink)
{

  gst_pulsesink_destroy_stream (pulsesink);

  if (pulsesink->context) {
    pa_context_disconnect (pulsesink->context);

    /* Make sure we don't get any further callbacks */
    pa_context_set_state_callback (pulsesink->context, NULL, NULL);
    pa_context_set_subscribe_callback (pulsesink->context, NULL, NULL);

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

  pa_threaded_mainloop_free (pulsesink->mainloop);

  if (pulsesink->probe) {
    gst_pulseprobe_free (pulsesink->probe);
    pulsesink->probe = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#if HAVE_PULSE_0_9_12
static void
gst_pulsesink_set_volume (GstPulseSink * pulsesink, gdouble volume)
{
  pa_cvolume v;
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  pulsesink->volume = volume;
  pulsesink->volume_set = TRUE;

  if (!pulsesink->stream)
    goto unlock;

  gst_pulse_cvolume_from_linear (&v, pulsesink->sample_spec.channels, volume);

  if (!(o = pa_context_set_sink_input_volume (pulsesink->context,
              pa_stream_get_index (pulsesink->stream), &v, NULL, NULL))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_set_sink_input_volume() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock;
  }

  /* We don't really care about the result of this call */

unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}

static void
gst_pulsesink_sink_input_info_cb (pa_context * c, const pa_sink_input_info * i,
    int eol, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  if (!i)
    return;

  if (!pulsesink->stream)
    return;

  g_assert (i->index == pa_stream_get_index (pulsesink->stream));

  pulsesink->volume = pa_sw_volume_to_linear (pa_cvolume_max (&i->volume));
}

static gdouble
gst_pulsesink_get_volume (GstPulseSink * pulsesink)
{
  pa_operation *o = NULL;
  gdouble v;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!pulsesink->stream)
    goto unlock;

  if (!(o = pa_context_get_sink_input_info (pulsesink->context,
              pa_stream_get_index (pulsesink->stream),
              gst_pulsesink_sink_input_info_cb, pulsesink))) {

    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_get_sink_input_info() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock;
  }

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesink_is_dead (pulsesink))
      goto unlock;

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

unlock:

  if (o)
    pa_operation_unref (o);

  v = pulsesink->volume;

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return v;
}
#endif

static gboolean
gst_pulsesink_is_dead (GstPulseSink * pulsesink)
{

  if (!pulsesink->context
      || !PA_CONTEXT_IS_GOOD (pa_context_get_state (pulsesink->context))
      || !pulsesink->stream
      || !PA_STREAM_IS_GOOD (pa_stream_get_state (pulsesink->stream))) {
    const gchar *err_str = pulsesink->context ?
        pa_strerror (pa_context_errno (pulsesink->context)) : NULL;

    GST_ELEMENT_ERROR ((pulsesink), RESOURCE, FAILED, ("Disconnected: %s",
            err_str), (NULL));
    return TRUE;
  }

  return FALSE;
}

static void
gst_pulsesink_sink_info_cb (pa_context * c, const pa_sink_info * i, int eol,
    void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  if (!i)
    return;

  if (!pulsesink->stream)
    return;

  g_assert (i->index == pa_stream_get_device_index (pulsesink->stream));

  g_free (pulsesink->device_description);
  pulsesink->device_description = g_strdup (i->description);
}

static gchar *
gst_pulsesink_device_description (GstPulseSink * pulsesink)
{
  pa_operation *o = NULL;
  gchar *t;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!pulsesink->stream)
    goto unlock;

  if (!(o = pa_context_get_sink_info_by_index (pulsesink->context,
              pa_stream_get_device_index (pulsesink->stream),
              gst_pulsesink_sink_info_cb, pulsesink))) {

    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_get_sink_info() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock;
  }

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesink_is_dead (pulsesink))
      goto unlock;

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

unlock:

  if (o)
    pa_operation_unref (o);

  t = g_strdup (pulsesink->device_description);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return t;
}

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

#if HAVE_PULSE_0_9_12
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

    case PROP_DEVICE_NAME:{
      char *t = gst_pulsesink_device_description (pulsesink);
      g_value_set_string (value, t);
      g_free (t);
      break;
    }

#if HAVE_PULSE_0_9_12
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

#if HAVE_PULSE_0_9_12
static void
gst_pulsesink_context_subscribe_cb (pa_context * c,
    pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  if (t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_CHANGE) &&
      t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_NEW))
    return;

  if (!pulsesink->stream)
    return;

  if (idx != pa_stream_get_index (pulsesink->stream))
    return;

  /* Actually this event is also triggered when other properties of
   * the stream change that are unrelated to the volume. However it is
   * probably cheaper to signal the change here and check for the
   * volume when the GObject property is read instead of querying it always.
   *
   * Lennart thinks this is a race because g_object_notify() is not
   * thread safe and this function is run from a PA controlled
   * thread. But folks on #gstreamer told me that was ok. */

  g_object_notify (G_OBJECT (pulsesink), "volume");
}
#endif

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

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  g_assert (!pulsesink->context);
  g_assert (!pulsesink->stream);

  if (!(pulsesink->context =
          pa_context_new (pa_threaded_mainloop_get_api (pulsesink->mainloop),
              name))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to create context"), (NULL));
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (pulsesink->context,
      gst_pulsesink_context_state_cb, pulsesink);
#if HAVE_PULSE_0_9_12
  pa_context_set_subscribe_callback (pulsesink->context,
      gst_pulsesink_context_subscribe_cb, pulsesink);
#endif

  if (pa_context_connect (pulsesink->context, pulsesink->server, 0, NULL) < 0) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  for (;;) {
    pa_context_state_t state;

    state = pa_context_get_state (pulsesink->context);

    if (!PA_CONTEXT_IS_GOOD (state)) {
      GST_DEBUG_OBJECT (pulsesink, "Context state was not READY. Got: %d",
          state);
      GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Failed to connect: %s",
              pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
      goto unlock_and_fail;
    }

    if (state == PA_CONTEXT_READY)
      break;

    /* Wait until the context is ready */
    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  g_free (name);
  return TRUE;

unlock_and_fail:

  gst_pulsesink_destroy_context (pulsesink);

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
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  pa_operation *o = NULL;
  pa_cvolume v;

  if (!gst_pulse_fill_sample_spec (spec, &pulsesink->sample_spec)) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, SETTINGS,
        ("Invalid sample specification."), (NULL));
    return FALSE;
  }

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!pulsesink->context) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Bad context"), (NULL));
    goto unlock_and_fail;
  }

  g_assert (!pulsesink->stream);

  if (!(o =
          pa_context_subscribe (pulsesink->context,
              PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL))) {
    const gchar *err_str = pulsesink->context ?
        pa_strerror (pa_context_errno (pulsesink->context)) : NULL;

    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_context_subscribe() failed: %s", err_str), (NULL));
    goto unlock_and_fail;
  }

  pa_operation_unref (o);

  if (!(pulsesink->stream = pa_stream_new (pulsesink->context,
              pulsesink->stream_name ?
              pulsesink->stream_name : "Playback Stream",
              &pulsesink->sample_spec,
              gst_pulse_gst_to_channel_map (&channel_map, spec)))) {
    const gchar *err_str = pulsesink->context ?
        pa_strerror (pa_context_errno (pulsesink->context)) : NULL;

    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to create stream: %s", err_str), (NULL));
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
  buf_attr.prebuf = buf_attr.tlength;
  buf_attr.minreq = spec->segsize;

  if (pulsesink->volume_set)
    gst_pulse_cvolume_from_linear (&v, pulsesink->sample_spec.channels,
        pulsesink->volume);

  if (pa_stream_connect_playback (pulsesink->stream, pulsesink->device,
          &buf_attr,
          PA_STREAM_INTERPOLATE_TIMING |
          PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_NOT_MONOTONOUS |
#if HAVE_PULSE_0_9_11
          PA_STREAM_ADJUST_LATENCY |
#endif
          PA_STREAM_START_CORKED, pulsesink->volume_set ? &v : NULL, NULL) < 0) {

    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  for (;;) {
    pa_stream_state_t state;

    state = pa_stream_get_state (pulsesink->stream);

    if (!PA_STREAM_IS_GOOD (state)) {
      GST_DEBUG_OBJECT (pulsesink, "Stream state was not READY. Got: %d",
          state);
      GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
          ("Failed to connect stream: %s",
              pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
      goto unlock_and_fail;
    }

    if (state == PA_STREAM_READY)
      break;

    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return TRUE;

unlock_and_fail:

  gst_pulsesink_destroy_stream (pulsesink);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

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

static guint
gst_pulsesink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  size_t sum = 0;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  pulsesink->in_write = TRUE;

  while (length > 0) {
    size_t l;

    for (;;) {
      if (gst_pulsesink_is_dead (pulsesink))
        goto unlock_and_fail;

      if ((l = pa_stream_writable_size (pulsesink->stream)) == (size_t) - 1) {
        GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
            ("pa_stream_writable_size() failed: %s",
                pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
        goto unlock_and_fail;
      }

      if (l > 0)
        break;

      if (pulsesink->did_reset)
        goto unlock_and_fail;

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

  pulsesink->did_reset = FALSE;
  pulsesink->in_write = FALSE;

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return sum;

unlock_and_fail:

  pulsesink->did_reset = FALSE;
  pulsesink->in_write = FALSE;

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return (guint) - 1;
}

static guint
gst_pulsesink_delay (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  pa_usec_t t;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  for (;;) {
    if (gst_pulsesink_is_dead (pulsesink))
      goto unlock_and_fail;

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

  pulsesink->operation_success = !!success;
  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static void
gst_pulsesink_reset (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (gst_pulsesink_is_dead (pulsesink))
    goto unlock_and_fail;

  if (!(o =
          pa_stream_flush (pulsesink->stream, gst_pulsesink_success_cb,
              pulsesink))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_flush() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* Inform anyone waiting in _write() call that it shall wakeup */
  if (pulsesink->in_write) {
    pulsesink->did_reset = TRUE;
    pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
  }

  pulsesink->operation_success = FALSE;
  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesink_is_dead (pulsesink))
      goto unlock_and_fail;

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

  if (gst_pulsesink_is_dead (pulsesink))
    goto unlock;

  if (!(o =
          pa_stream_set_name (pulsesink->stream, pulsesink->stream_name, NULL,
              NULL))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_set_name() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock;
  }

  /* We're not interested if this operation failed or not */

unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}

#if HAVE_PULSE_0_9_11
static void
gst_pulsesink_change_props (GstPulseSink * pulsesink, GstTagList * l)
{

  static const gchar *const map[] = {
    GST_TAG_TITLE, PA_PROP_MEDIA_TITLE,
    GST_TAG_ARTIST, PA_PROP_MEDIA_ARTIST,
    GST_TAG_LANGUAGE_CODE, PA_PROP_MEDIA_LANGUAGE,
    GST_TAG_LOCATION, PA_PROP_MEDIA_FILENAME,
    /* We might add more here later on ... */
    NULL
  };

  pa_proplist *pl = NULL;
  const gchar *const *t;
  gboolean empty = TRUE;
  pa_operation *o = NULL;

  pl = pa_proplist_new ();

  for (t = map; *t; t += 2) {
    gchar *n = NULL;

    if (gst_tag_list_get_string (l, *t, &n)) {

      if (n && *n) {
        pa_proplist_sets (pl, *(t + 1), n);
        empty = FALSE;
      }

      g_free (n);
    }
  }

  if (empty)
    goto finish;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (gst_pulsesink_is_dead (pulsesink))
    goto unlock;

  if (!(o =
          pa_stream_proplist_update (pulsesink->stream, PA_UPDATE_REPLACE, pl,
              NULL, NULL))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_proplist_update() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock;
  }

  /* We're not interested if this operation failed or not */

unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

finish:

  if (pl)
    pa_proplist_free (pl);
}
#endif

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

#if HAVE_PULSE_0_9_11
      gst_pulsesink_change_props (pulsesink, l);
#endif

      break;
    }
    default:
      ;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static void
gst_pulsesink_pause (GstPulseSink * pulsesink, gboolean b)
{
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (gst_pulsesink_is_dead (pulsesink))
    goto unlock;

  if (!(o = pa_stream_cork (pulsesink->stream, b, NULL, NULL))) {

    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_cork() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock;
  }

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesink_is_dead (pulsesink))
      goto unlock;

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}


static GstStateChangeReturn
gst_pulsesink_change_state (GstElement * element, GstStateChange transition)
{
  GstPulseSink *this = GST_PULSESINK (element);

  switch (transition) {

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:

      gst_pulsesink_pause (this,
          GST_STATE_TRANSITION_NEXT (transition) == GST_STATE_PAUSED);
      break;

    default:
      ;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
