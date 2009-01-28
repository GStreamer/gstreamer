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
 * SECTION:element-pulsesrc
 * @see_also: pulsesink, pulsemixer
 *
 * This element captures audio from a
 * <ulink href="http://www.pulseaudio.org">PulseAudio sound server</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v pulsesrc ! audioconvert ! vorbisenc ! oggmux ! filesink location=alsasrc.ogg
 * ]| Record from a sound card using ALSA and encode to Ogg/Vorbis.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/base/gstbasesrc.h>
#include <gst/gsttaglist.h>

#include "pulsesrc.h"
#include "pulseutil.h"
#include "pulsemixerctrl.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

enum
{
  PROP_SERVER = 1,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

static void gst_pulsesrc_destroy_stream (GstPulseSrc * pulsesrc);

static void gst_pulsesrc_destroy_context (GstPulseSrc * pulsesrc);

static void gst_pulsesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulsesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulsesrc_finalize (GObject * object);

static gboolean gst_pulsesrc_open (GstAudioSrc * asrc);

static gboolean gst_pulsesrc_close (GstAudioSrc * asrc);

static gboolean gst_pulsesrc_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);

static gboolean gst_pulsesrc_unprepare (GstAudioSrc * asrc);

static guint gst_pulsesrc_read (GstAudioSrc * asrc, gpointer data,
    guint length);
static guint gst_pulsesrc_delay (GstAudioSrc * asrc);

static void gst_pulsesrc_reset (GstAudioSrc * src);

static gboolean gst_pulsesrc_negotiate (GstBaseSrc * basesrc);

static GstStateChangeReturn gst_pulsesrc_change_state (GstElement *
    element, GstStateChange transition);

static void gst_pulsesrc_init_interfaces (GType type);

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
# define ENDIANNESS   "LITTLE_ENDIAN, BIG_ENDIAN"
#else
# define ENDIANNESS   "BIG_ENDIAN, LITTLE_ENDIAN"
#endif

GST_IMPLEMENT_PULSEMIXER_CTRL_METHODS (GstPulseSrc, gst_pulsesrc);
GST_IMPLEMENT_PULSEPROBE_METHODS (GstPulseSrc, gst_pulsesrc);
GST_BOILERPLATE_FULL (GstPulseSrc, gst_pulsesrc, GstAudioSrc,
    GST_TYPE_AUDIO_SRC, gst_pulsesrc_init_interfaces);

static gboolean
gst_pulsesrc_interface_supported (GstImplementsInterface *
    iface, GType interface_type)
{
  GstPulseSrc *this = GST_PULSESRC (iface);

  if (interface_type == GST_TYPE_MIXER && this->mixer)
    return TRUE;

  if (interface_type == GST_TYPE_PROPERTY_PROBE && this->probe)
    return TRUE;

  return FALSE;
}

static void
gst_pulsesrc_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_pulsesrc_interface_supported;
}

static void
gst_pulsesrc_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_pulsesrc_implements_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo mixer_iface_info = {
    (GInterfaceInitFunc) gst_pulsesrc_mixer_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo probe_iface_info = {
    (GInterfaceInitFunc) gst_pulsesrc_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_iface_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}

static void
gst_pulsesrc_base_init (gpointer g_class)
{

  static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
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
      "PulseAudio Audio Source",
      "Source/Audio",
      "Captures audio from a PulseAudio server", "Lennart Poettering");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&pad_template));
}

static void
gst_pulsesrc_class_init (GstPulseSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioSrcClass *gstaudiosrc_class = GST_AUDIO_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pulsesrc_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pulsesrc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pulsesrc_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pulsesrc_change_state);

  gstbasesrc_class->negotiate = GST_DEBUG_FUNCPTR (gst_pulsesrc_negotiate);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_pulsesrc_open);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_pulsesrc_close);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_pulsesrc_prepare);
  gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR (gst_pulsesrc_unprepare);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_pulsesrc_read);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_pulsesrc_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_pulsesrc_reset);

  /* Overwrite GObject fields */
  g_object_class_install_property (gobject_class,
      PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The PulseAudio server to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Source",
          "The PulseAudio source device to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_pulsesrc_init (GstPulseSrc * pulsesrc, GstPulseSrcClass * klass)
{
  int e;

  pulsesrc->server = pulsesrc->device = pulsesrc->device_description = NULL;

  pulsesrc->context = NULL;
  pulsesrc->stream = NULL;

  pulsesrc->read_buffer = NULL;
  pulsesrc->read_buffer_length = 0;

#if HAVE_PULSE_0_9_13
  pa_sample_spec_init (&pulsesrc->sample_spec);
#else
  pulsesrc->sample_spec.format = PA_SAMPLE_INVALID;
  pulsesrc->sample_spec.rate = 0;
  pulsesrc->sample_spec.channels = 0;
#endif

  pulsesrc->operation_success = FALSE;
  pulsesrc->did_reset = FALSE;
  pulsesrc->in_read = FALSE;

  pulsesrc->mainloop = pa_threaded_mainloop_new ();
  g_assert (pulsesrc->mainloop);

  e = pa_threaded_mainloop_start (pulsesrc->mainloop);
  g_assert (e == 0);

  pulsesrc->mixer = NULL;

  pulsesrc->probe = gst_pulseprobe_new (G_OBJECT (pulsesrc), G_OBJECT_GET_CLASS (pulsesrc), PROP_DEVICE, pulsesrc->device, FALSE, TRUE);        /* FALSE for sinks, TRUE for sources */
}

static void
gst_pulsesrc_destroy_stream (GstPulseSrc * pulsesrc)
{
  if (pulsesrc->stream) {
    pa_stream_disconnect (pulsesrc->stream);
    pa_stream_unref (pulsesrc->stream);
    pulsesrc->stream = NULL;
  }

  g_free (pulsesrc->device_description);
  pulsesrc->device_description = NULL;
}

static void
gst_pulsesrc_destroy_context (GstPulseSrc * pulsesrc)
{

  gst_pulsesrc_destroy_stream (pulsesrc);

  if (pulsesrc->context) {
    pa_context_disconnect (pulsesrc->context);
    pa_context_unref (pulsesrc->context);
    pulsesrc->context = NULL;
  }
}

static void
gst_pulsesrc_finalize (GObject * object)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (object);

  pa_threaded_mainloop_stop (pulsesrc->mainloop);

  gst_pulsesrc_destroy_context (pulsesrc);

  g_free (pulsesrc->server);
  g_free (pulsesrc->device);

  pa_threaded_mainloop_free (pulsesrc->mainloop);

  if (pulsesrc->mixer) {
    gst_pulsemixer_ctrl_free (pulsesrc->mixer);
    pulsesrc->mixer = NULL;
  }

  if (pulsesrc->probe) {
    gst_pulseprobe_free (pulsesrc->probe);
    pulsesrc->probe = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_pulsesrc_is_dead (GstPulseSrc * pulsesrc)
{

  if (!pulsesrc->context
      || !PA_CONTEXT_IS_GOOD (pa_context_get_state (pulsesrc->context))
      || !pulsesrc->stream
      || !PA_STREAM_IS_GOOD (pa_stream_get_state (pulsesrc->stream))) {
    const gchar *err_str = pulsesrc->context ?
        pa_strerror (pa_context_errno (pulsesrc->context)) : NULL;

    GST_ELEMENT_ERROR ((pulsesrc), RESOURCE, FAILED, ("Disconnected: %s",
            err_str), (NULL));
    return TRUE;
  }

  return FALSE;
}


static void
gst_pulsesrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{

  GstPulseSrc *pulsesrc = GST_PULSESRC (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_free (pulsesrc->server);
      pulsesrc->server = g_value_dup_string (value);

      if (pulsesrc->probe)
        gst_pulseprobe_set_server (pulsesrc->probe, pulsesrc->server);

      break;

    case PROP_DEVICE:
      g_free (pulsesrc->device);
      pulsesrc->device = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesrc_source_info_cb (pa_context * c, const pa_source_info * i, int eol,
    void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (userdata);

  if (!i)
    return;

  if (!pulsesrc->stream)
    return;

  g_assert (i->index == pa_stream_get_device_index (pulsesrc->stream));

  g_free (pulsesrc->device_description);
  pulsesrc->device_description = g_strdup (i->description);
}

static gchar *
gst_pulsesrc_device_description (GstPulseSrc * pulsesrc)
{
  pa_operation *o = NULL;
  gchar *t;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (!pulsesrc->stream)
    goto unlock;

  if (!(o = pa_context_get_source_info_by_index (pulsesrc->context,
              pa_stream_get_device_index (pulsesrc->stream),
              gst_pulsesrc_source_info_cb, pulsesrc))) {

    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_get_source_info() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesrc_is_dead (pulsesrc))
      goto unlock;

    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

unlock:

  if (o)
    pa_operation_unref (o);

  t = g_strdup (pulsesrc->device_description);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return t;
}

static void
gst_pulsesrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{

  GstPulseSrc *pulsesrc = GST_PULSESRC (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, pulsesrc->server);
      break;

    case PROP_DEVICE:
      g_value_set_string (value, pulsesrc->device);
      break;

    case PROP_DEVICE_NAME:{
      char *t = gst_pulsesrc_device_description (pulsesrc);
      g_value_set_string (value, t);
      g_free (t);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesrc_context_state_cb (pa_context * c, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (userdata);

  switch (pa_context_get_state (c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

static void
gst_pulsesrc_stream_state_cb (pa_stream * s, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (userdata);

  switch (pa_stream_get_state (s)) {

    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
      pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
      break;

    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      break;
  }
}

static void
gst_pulsesrc_stream_request_cb (pa_stream * s, size_t length, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (userdata);

  pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
}

static void
gst_pulsesrc_stream_latency_update_cb (pa_stream * s, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (userdata);

  pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
}

static gboolean
gst_pulsesrc_open (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);
  gchar *name = gst_pulse_client_name ();

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  g_assert (!pulsesrc->context);
  g_assert (!pulsesrc->stream);

  if (!(pulsesrc->context =
          pa_context_new (pa_threaded_mainloop_get_api (pulsesrc->mainloop),
              name))) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED, ("Failed to create context"),
        (NULL));
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (pulsesrc->context,
      gst_pulsesrc_context_state_cb, pulsesrc);

  if (pa_context_connect (pulsesrc->context, pulsesrc->server, 0, NULL) < 0) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  for (;;) {
    pa_context_state_t state;

    state = pa_context_get_state (pulsesrc->context);

    if (!PA_CONTEXT_IS_GOOD (state)) {
      GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED, ("Failed to connect: %s",
              pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
      goto unlock_and_fail;
    }

    if (state == PA_CONTEXT_READY)
      break;

    /* Wait until the context is ready */
    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  g_free (name);
  return TRUE;

unlock_and_fail:

  gst_pulsesrc_destroy_context (pulsesrc);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  g_free (name);
  return FALSE;
}

static gboolean
gst_pulsesrc_close (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);

  pa_threaded_mainloop_lock (pulsesrc->mainloop);
  gst_pulsesrc_destroy_context (pulsesrc);
  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return TRUE;
}

static gboolean
gst_pulsesrc_unprepare (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);

  pa_threaded_mainloop_lock (pulsesrc->mainloop);
  gst_pulsesrc_destroy_stream (pulsesrc);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  pulsesrc->read_buffer = NULL;
  pulsesrc->read_buffer_length = 0;

  return TRUE;
}

static guint
gst_pulsesrc_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);
  size_t sum = 0;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  pulsesrc->in_read = TRUE;

  while (length > 0) {
    size_t l;

    if (!pulsesrc->read_buffer) {

      for (;;) {
        if (gst_pulsesrc_is_dead (pulsesrc))
          goto unlock_and_fail;

        if (pa_stream_peek (pulsesrc->stream, &pulsesrc->read_buffer,
                &pulsesrc->read_buffer_length) < 0) {
          GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
              ("pa_stream_peek() failed: %s",
                  pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
          goto unlock_and_fail;
        }

        if (pulsesrc->read_buffer)
          break;

        if (pulsesrc->did_reset)
          goto unlock_and_fail;

        pa_threaded_mainloop_wait (pulsesrc->mainloop);
      }
    }

    g_assert (pulsesrc->read_buffer && pulsesrc->read_buffer_length);

    l = pulsesrc->read_buffer_length >
        length ? length : pulsesrc->read_buffer_length;

    memcpy (data, pulsesrc->read_buffer, l);

    pulsesrc->read_buffer = (const guint8 *) pulsesrc->read_buffer + l;
    pulsesrc->read_buffer_length -= l;

    data = (guint8 *) data + l;
    length -= l;

    sum += l;

    if (pulsesrc->read_buffer_length <= 0) {

      if (pa_stream_drop (pulsesrc->stream) < 0) {
        GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
            ("pa_stream_drop() failed: %s",
                pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
        goto unlock_and_fail;
      }

      pulsesrc->read_buffer = NULL;
      pulsesrc->read_buffer_length = 0;
    }
  }

  pulsesrc->did_reset = FALSE;
  pulsesrc->in_read = FALSE;

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);
  return sum;

unlock_and_fail:

  pulsesrc->did_reset = FALSE;
  pulsesrc->in_read = FALSE;

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);
  return (guint) - 1;
}

static guint
gst_pulsesrc_delay (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);

  pa_usec_t t;

  int negative;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  for (;;) {
    if (gst_pulsesrc_is_dead (pulsesrc))
      goto unlock_and_fail;

    if (pa_stream_get_latency (pulsesrc->stream, &t, &negative) >= 0)
      break;

    if (pa_context_errno (pulsesrc->context) != PA_ERR_NODATA) {
      GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
          ("pa_stream_get_latency() failed: %s",
              pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
      goto unlock_and_fail;
    }

    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

  if (negative)
    t = 0;

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return (guint) ((t * pulsesrc->sample_spec.rate) / 1000000LL);

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);
  return 0;
}

static gboolean
gst_pulsesrc_create_stream (GstPulseSrc * pulsesrc, GstCaps * caps)
{
  pa_channel_map channel_map;
  GstStructure *s;
  gboolean need_channel_layout = FALSE;
  GstRingBufferSpec spec;

  memset (&spec, 0, sizeof (GstRingBufferSpec));
  spec.latency_time = GST_SECOND;
  if (!gst_ring_buffer_parse_caps (&spec, caps)) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, SETTINGS,
        ("Can't parse caps."), (NULL));
    goto fail;
  }
  /* Keep the refcount of the caps at 1 to make them writable */
  gst_caps_unref (spec.caps);

  if (!gst_pulse_fill_sample_spec (&spec, &pulsesrc->sample_spec)) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, SETTINGS,
        ("Invalid sample specification."), (NULL));
    goto fail;
  }

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (!pulsesrc->context) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED, ("Bad context"), (NULL));
    goto unlock_and_fail;
  }

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_has_field (s, "channel-layout") ||
      !gst_pulse_gst_to_channel_map (&channel_map, &spec)) {
    if (spec.channels == 1)
      pa_channel_map_init_mono (&channel_map);
    else if (spec.channels == 2)
      pa_channel_map_init_stereo (&channel_map);
    else
      need_channel_layout = TRUE;
  }

  if (!(pulsesrc->stream = pa_stream_new (pulsesrc->context,
              "Record Stream",
              &pulsesrc->sample_spec,
              (need_channel_layout) ? NULL : &channel_map))) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("Failed to create stream: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  if (need_channel_layout) {
    const pa_channel_map *m = pa_stream_get_channel_map (pulsesrc->stream);

    gst_pulse_channel_map_to_gst (m, &spec);
    caps = spec.caps;
  }

  GST_DEBUG_OBJECT (pulsesrc, "Caps are %" GST_PTR_FORMAT, caps);

  pa_stream_set_state_callback (pulsesrc->stream, gst_pulsesrc_stream_state_cb,
      pulsesrc);
  pa_stream_set_read_callback (pulsesrc->stream, gst_pulsesrc_stream_request_cb,
      pulsesrc);
  pa_stream_set_latency_update_callback (pulsesrc->stream,
      gst_pulsesrc_stream_latency_update_cb, pulsesrc);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return TRUE;

unlock_and_fail:
  gst_pulsesrc_destroy_stream (pulsesrc);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

fail:
  return FALSE;
}

/* This is essentially gst_base_src_negotiate_default() but the caps
 * are guaranteed to have a channel layout for > 2 channels
 */
static gboolean
gst_pulsesrc_negotiate (GstBaseSrc * basesrc)
{
  GstCaps *thiscaps;
  GstCaps *caps = NULL;
  GstCaps *peercaps = NULL;
  gboolean result = FALSE;

  /* first see what is possible on our source pad */
  thiscaps = gst_pad_get_caps (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG_OBJECT (basesrc, "caps of src: %" GST_PTR_FORMAT, thiscaps);
  /* nothing or anything is allowed, we're done */
  if (thiscaps == NULL || gst_caps_is_any (thiscaps))
    goto no_nego_needed;

  /* get the peer caps */
  peercaps = gst_pad_peer_get_caps (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG_OBJECT (basesrc, "caps of peer: %" GST_PTR_FORMAT, peercaps);
  if (peercaps) {
    GstCaps *icaps;

    /* get intersection */
    icaps = gst_caps_intersect (thiscaps, peercaps);
    GST_DEBUG_OBJECT (basesrc, "intersect: %" GST_PTR_FORMAT, icaps);
    gst_caps_unref (thiscaps);
    gst_caps_unref (peercaps);
    if (icaps) {
      /* take first (and best, since they are sorted) possibility */
      caps = gst_caps_copy_nth (icaps, 0);
      gst_caps_unref (icaps);
    }
  } else {
    /* no peer, work with our own caps then */
    caps = thiscaps;
  }
  if (caps) {
    caps = gst_caps_make_writable (caps);
    gst_caps_truncate (caps);

    /* now fixate */
    if (!gst_caps_is_empty (caps)) {
      gst_pad_fixate_caps (GST_BASE_SRC_PAD (basesrc), caps);
      GST_DEBUG_OBJECT (basesrc, "fixated to: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_any (caps)) {
        /* hmm, still anything, so element can do anything and
         * nego is not needed */
        result = TRUE;
      } else if (gst_caps_is_fixed (caps)) {
        /* yay, fixed caps, use those then */
        result = gst_pulsesrc_create_stream (GST_PULSESRC (basesrc), caps);
        if (result)
          gst_pad_set_caps (GST_BASE_SRC_PAD (basesrc), caps);
        result = TRUE;
      }
    }
    gst_caps_unref (caps);
  }
  return result;

no_nego_needed:
  {
    GST_DEBUG_OBJECT (basesrc, "no negotiation needed");
    if (thiscaps)
      gst_caps_unref (thiscaps);
    return TRUE;
  }
}

static gboolean
gst_pulsesrc_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  pa_buffer_attr buf_attr;
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  memset (&buf_attr, 0, sizeof (buf_attr));
  buf_attr.maxlength = spec->segtotal * spec->segsize * 2;
  buf_attr.fragsize = spec->segsize;

  if (pa_stream_connect_record (pulsesrc->stream, pulsesrc->device, &buf_attr,
          PA_STREAM_INTERPOLATE_TIMING |
          PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_NOT_MONOTONOUS |
#if HAVE_PULSE_0_9_11
          PA_STREAM_ADJUST_LATENCY |
#endif
          PA_STREAM_START_CORKED) < 0) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  for (;;) {
    pa_stream_state_t state;

    state = pa_stream_get_state (pulsesrc->stream);

    if (!PA_STREAM_IS_GOOD (state)) {
      GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
          ("Failed to connect stream: %s",
              pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
      goto unlock_and_fail;
    }

    if (state == PA_STREAM_READY)
      break;

    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return TRUE;

unlock_and_fail:

  gst_pulsesrc_destroy_stream (pulsesrc);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);
  return FALSE;
}

static void
gst_pulsesrc_success_cb (pa_stream * s, int success, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (userdata);

  pulsesrc->operation_success = !!success;
  pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
}

static void
gst_pulsesrc_reset (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC (asrc);
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (gst_pulsesrc_is_dead (pulsesrc))
    goto unlock_and_fail;

  if (!(o =
          pa_stream_flush (pulsesrc->stream, gst_pulsesrc_success_cb,
              pulsesrc))) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_flush() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  /* Inform anyone waiting in _write() call that it shall wakeup */
  if (pulsesrc->in_read) {
    pulsesrc->did_reset = TRUE;
    pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
  }

  pulsesrc->operation_success = FALSE;
  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesrc_is_dead (pulsesrc))
      goto unlock_and_fail;

    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

  if (!pulsesrc->operation_success) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED, ("Flush failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

unlock_and_fail:

  if (o) {
    pa_operation_cancel (o);
    pa_operation_unref (o);
  }

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);
}

static void
gst_pulsesrc_pause (GstPulseSrc * pulsesrc, gboolean b)
{
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (gst_pulsesrc_is_dead (pulsesrc))
    goto unlock;

  if (!(o = pa_stream_cork (pulsesrc->stream, b, NULL, NULL))) {

    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_cork() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesrc_is_dead (pulsesrc))
      goto unlock;

    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);
}

static GstStateChangeReturn
gst_pulsesrc_change_state (GstElement * element, GstStateChange transition)
{
  GstPulseSrc *this = GST_PULSESRC (element);

  switch (transition) {

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_pulsesrc_pause (this,
          GST_STATE_TRANSITION_NEXT (transition) == GST_STATE_PAUSED);
      break;

    case GST_STATE_CHANGE_NULL_TO_READY:

      if (!this->mixer)
        this->mixer =
            gst_pulsemixer_ctrl_new (G_OBJECT (this), this->server,
            this->device, GST_PULSEMIXER_SOURCE);

      break;

    case GST_STATE_CHANGE_READY_TO_NULL:

      if (this->mixer) {
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
