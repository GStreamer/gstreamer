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
 * ]| Record from a sound card using pulseaudio and encode to Ogg/Vorbis.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/base/gstbasesrc.h>
#include <gst/gsttaglist.h>
#ifdef HAVE_PULSE_1_0
#include <gst/interfaces/streamvolume.h>
#endif

#include "pulsesrc.h"
#include "pulseutil.h"
#include "pulsemixerctrl.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

#define DEFAULT_SERVER            NULL
#define DEFAULT_DEVICE            NULL
#define DEFAULT_DEVICE_NAME       NULL

#ifdef HAVE_PULSE_1_0
#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            FALSE
#define MAX_VOLUME              10.0
#endif

enum
{
  PROP_0,
  PROP_SERVER,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_CLIENT,
  PROP_STREAM_PROPERTIES,
  PROP_SOURCE_OUTPUT_INDEX,
#ifdef HAVE_PULSE_1_0
  PROP_VOLUME,
  PROP_MUTE,
#endif
  PROP_LAST
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
  GstPulseSrc *this = GST_PULSESRC_CAST (iface);

  if (interface_type == GST_TYPE_MIXER && this->mixer)
    return TRUE;

  if (interface_type == GST_TYPE_PROPERTY_PROBE && this->probe)
    return TRUE;

#ifdef HAVE_PULSE_1_0
  if (interface_type == GST_TYPE_STREAM_VOLUME)
    return TRUE;
#endif

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
#ifdef HAVE_PULSE_1_0
  static const GInterfaceInfo svol_iface_info = {
    NULL, NULL, NULL,
  };
#endif
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

#ifdef HAVE_PULSE_1_0
  g_type_add_interface_static (type, GST_TYPE_STREAM_VOLUME, &svol_iface_info);
#endif
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
  gst_element_class_add_static_pad_template (element_class, &pad_template);
}

static void
gst_pulsesrc_class_init (GstPulseSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioSrcClass *gstaudiosrc_class = GST_AUDIO_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  gchar *clientname;

  gobject_class->finalize = gst_pulsesrc_finalize;
  gobject_class->set_property = gst_pulsesrc_set_property;
  gobject_class->get_property = gst_pulsesrc_get_property;

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
          "The PulseAudio server to connect to", DEFAULT_SERVER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "The PulseAudio source device to connect to", DEFAULT_DEVICE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", DEFAULT_DEVICE_NAME,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  clientname = gst_pulse_client_name ();
  /**
   * GstPulseSrc:client
   *
   * The PulseAudio client name to use.
   *
   * Since: 0.10.27
   */
  g_object_class_install_property (gobject_class,
      PROP_CLIENT,
      g_param_spec_string ("client", "Client",
          "The PulseAudio client_name_to_use", clientname,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_free (clientname);

  /**
   * GstPulseSrc:stream-properties
   *
   * List of pulseaudio stream properties. A list of defined properties can be
   * found in the <ulink href="http://0pointer.de/lennart/projects/pulseaudio/doxygen/proplist_8h.html">pulseaudio api docs</ulink>.
   *
   * Below is an example for registering as a music application to pulseaudio.
   * |[
   * GstStructure *props;
   *
   * props = gst_structure_from_string ("props,media.role=music", NULL);
   * g_object_set (pulse, "stream-properties", props, NULL);
   * gst_structure_free (props);
   * ]|
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class,
      PROP_STREAM_PROPERTIES,
      g_param_spec_boxed ("stream-properties", "stream properties",
          "list of pulseaudio stream properties",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPulseSrc:source-output-index
   *
   * The index of the PulseAudio source output corresponding to this element.
   *
   * Since: 0.10.31
   */
  g_object_class_install_property (gobject_class,
      PROP_SOURCE_OUTPUT_INDEX,
      g_param_spec_uint ("source-output-index", "source output index",
          "The index of the PulseAudio source output corresponding to this "
          "record stream", 0, G_MAXUINT, PA_INVALID_INDEX,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

#ifdef HAVE_PULSE_1_0
  /**
   * GstPulseSrc:volume
   *
   * The volume of the record stream. Only works when using PulseAudio 1.0 or
   * later.
   *
   * Since: 0.10.36
   */
  g_object_class_install_property (gobject_class,
      PROP_VOLUME, g_param_spec_double ("volume", "Volume",
          "Linear volume of this stream, 1.0=100%",
          0.0, MAX_VOLUME, DEFAULT_VOLUME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstPulseSrc:mute
   *
   * Whether the stream is muted or not. Only works when using PulseAudio 1.0
   * or later.
   *
   * Since: 0.10.36
   */
  g_object_class_install_property (gobject_class,
      PROP_MUTE, g_param_spec_boolean ("mute", "Mute",
          "Mute state of this stream",
          DEFAULT_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
}

static void
gst_pulsesrc_init (GstPulseSrc * pulsesrc, GstPulseSrcClass * klass)
{
  pulsesrc->server = NULL;
  pulsesrc->device = NULL;
  pulsesrc->client_name = gst_pulse_client_name ();
  pulsesrc->device_description = NULL;

  pulsesrc->context = NULL;
  pulsesrc->stream = NULL;
  pulsesrc->source_output_idx = PA_INVALID_INDEX;

  pulsesrc->read_buffer = NULL;
  pulsesrc->read_buffer_length = 0;

  pa_sample_spec_init (&pulsesrc->sample_spec);

  pulsesrc->operation_success = FALSE;
  pulsesrc->paused = FALSE;
  pulsesrc->in_read = FALSE;

#ifdef HAVE_PULSE_1_0
  pulsesrc->volume = DEFAULT_VOLUME;
  pulsesrc->volume_set = FALSE;

  pulsesrc->mute = DEFAULT_MUTE;
  pulsesrc->mute_set = FALSE;

  pulsesrc->notify = 0;
#endif

  pulsesrc->mixer = NULL;

  pulsesrc->properties = NULL;
  pulsesrc->proplist = NULL;

  pulsesrc->probe = gst_pulseprobe_new (G_OBJECT (pulsesrc), G_OBJECT_GET_CLASS (pulsesrc), PROP_DEVICE, pulsesrc->server, FALSE, TRUE);        /* FALSE for sinks, TRUE for sources */

  /* this should be the default but it isn't yet */
  gst_base_audio_src_set_slave_method (GST_BASE_AUDIO_SRC (pulsesrc),
      GST_BASE_AUDIO_SRC_SLAVE_SKEW);
}

static void
gst_pulsesrc_destroy_stream (GstPulseSrc * pulsesrc)
{
  if (pulsesrc->stream) {
    pa_stream_disconnect (pulsesrc->stream);
    pa_stream_unref (pulsesrc->stream);
    pulsesrc->stream = NULL;
    pulsesrc->source_output_idx = PA_INVALID_INDEX;
    g_object_notify (G_OBJECT (pulsesrc), "source-output-index");
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

    /* Make sure we don't get any further callbacks */
    pa_context_set_state_callback (pulsesrc->context, NULL, NULL);
#ifdef HAVE_PULSE_1_0
    pa_context_set_subscribe_callback (pulsesrc->context, NULL, NULL);
#endif

    pa_context_unref (pulsesrc->context);

    pulsesrc->context = NULL;
  }
}

static void
gst_pulsesrc_finalize (GObject * object)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (object);

  g_free (pulsesrc->server);
  g_free (pulsesrc->device);
  g_free (pulsesrc->client_name);

  if (pulsesrc->properties)
    gst_structure_free (pulsesrc->properties);
  if (pulsesrc->proplist)
    pa_proplist_free (pulsesrc->proplist);

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

#define CONTEXT_OK(c) ((c) && PA_CONTEXT_IS_GOOD (pa_context_get_state ((c))))
#define STREAM_OK(s) ((s) && PA_STREAM_IS_GOOD (pa_stream_get_state ((s))))

static gboolean
gst_pulsesrc_is_dead (GstPulseSrc * pulsesrc, gboolean check_stream)
{
  if (!CONTEXT_OK (pulsesrc->context))
    goto error;

  if (check_stream && !STREAM_OK (pulsesrc->stream))
    goto error;

  return FALSE;

error:
  {
    const gchar *err_str = pulsesrc->context ?
        pa_strerror (pa_context_errno (pulsesrc->context)) : NULL;
    GST_ELEMENT_ERROR ((pulsesrc), RESOURCE, FAILED, ("Disconnected: %s",
            err_str), (NULL));
    return TRUE;
  }
}

static void
gst_pulsesrc_source_info_cb (pa_context * c, const pa_source_info * i, int eol,
    void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (userdata);

  if (!i)
    goto done;

  g_free (pulsesrc->device_description);
  pulsesrc->device_description = g_strdup (i->description);

done:
  pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
}

static gchar *
gst_pulsesrc_device_description (GstPulseSrc * pulsesrc)
{
  pa_operation *o = NULL;
  gchar *t;

  if (!pulsesrc->mainloop)
    goto no_mainloop;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (!(o = pa_context_get_source_info_by_name (pulsesrc->context,
              pulsesrc->device, gst_pulsesrc_source_info_cb, pulsesrc))) {

    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_get_source_info() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesrc_is_dead (pulsesrc, FALSE))
      goto unlock;

    pa_threaded_mainloop_wait (pulsesrc->mainloop);
  }

unlock:

  if (o)
    pa_operation_unref (o);

  t = g_strdup (pulsesrc->device_description);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return t;

no_mainloop:
  {
    GST_DEBUG_OBJECT (pulsesrc, "have no mainloop");
    return NULL;
  }
}

#ifdef HAVE_PULSE_1_0
static void
gst_pulsesrc_source_output_info_cb (pa_context * c,
    const pa_source_output_info * i, int eol, void *userdata)
{
  GstPulseSrc *psrc;

  psrc = GST_PULSESRC_CAST (userdata);

  if (!i)
    goto done;

  /* If the index doesn't match our current stream,
   * it implies we just recreated the stream (caps change)
   */
  if (i->index == psrc->source_output_idx) {
    psrc->volume = pa_sw_volume_to_linear (pa_cvolume_max (&i->volume));
    psrc->mute = i->mute;
  }

done:
  pa_threaded_mainloop_signal (psrc->mainloop, 0);
}

static gdouble
gst_pulsesrc_get_stream_volume (GstPulseSrc * pulsesrc)
{
  pa_operation *o = NULL;
  gdouble v;

  if (!pulsesrc->mainloop)
    goto no_mainloop;

  if (pulsesrc->source_output_idx == PA_INVALID_INDEX)
    goto no_index;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (!(o = pa_context_get_source_output_info (pulsesrc->context,
              pulsesrc->source_output_idx, gst_pulsesrc_source_output_info_cb,
              pulsesrc)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (pulsesrc->mainloop);
    if (gst_pulsesrc_is_dead (pulsesrc, TRUE))
      goto unlock;
  }

unlock:
  v = pulsesrc->volume;

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  if (v > MAX_VOLUME) {
    GST_WARNING_OBJECT (pulsesrc, "Clipped volume from %f to %f", v,
        MAX_VOLUME);
    v = MAX_VOLUME;
  }

  return v;

  /* ERRORS */
no_mainloop:
  {
    v = pulsesrc->volume;
    GST_DEBUG_OBJECT (pulsesrc, "we have no mainloop");
    return v;
  }
no_index:
  {
    v = pulsesrc->volume;
    GST_DEBUG_OBJECT (pulsesrc, "we don't have a stream index");
    return v;
  }
info_failed:
  {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_context_get_source_output_info() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }
}

static gboolean
gst_pulsesrc_get_stream_mute (GstPulseSrc * pulsesrc)
{
  pa_operation *o = NULL;
  gboolean mute;

  if (!pulsesrc->mainloop)
    goto no_mainloop;

  if (pulsesrc->source_output_idx == PA_INVALID_INDEX)
    goto no_index;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  if (!(o = pa_context_get_source_output_info (pulsesrc->context,
              pulsesrc->source_output_idx, gst_pulsesrc_source_output_info_cb,
              pulsesrc)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (pulsesrc->mainloop);
    if (gst_pulsesrc_is_dead (pulsesrc, TRUE))
      goto unlock;
  }

unlock:
  mute = pulsesrc->mute;

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return mute;

  /* ERRORS */
no_mainloop:
  {
    mute = pulsesrc->mute;
    GST_DEBUG_OBJECT (pulsesrc, "we have no mainloop");
    return mute;
  }
no_index:
  {
    mute = pulsesrc->mute;
    GST_DEBUG_OBJECT (pulsesrc, "we don't have a stream index");
    return mute;
  }
info_failed:
  {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_context_get_source_output_info() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }
}

static void
gst_pulsesrc_set_stream_volume (GstPulseSrc * pulsesrc, gdouble volume)
{
  pa_cvolume v;
  pa_operation *o = NULL;

  if (!pulsesrc->mainloop)
    goto no_mainloop;

  if (!pulsesrc->source_output_idx)
    goto no_index;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  GST_DEBUG_OBJECT (pulsesrc, "setting volume to %f", volume);

  gst_pulse_cvolume_from_linear (&v, pulsesrc->sample_spec.channels, volume);

  if (!(o = pa_context_set_source_output_volume (pulsesrc->context,
              pulsesrc->source_output_idx, &v, NULL, NULL)))
    goto volume_failed;

  /* We don't really care about the result of this call */
unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return;

  /* ERRORS */
no_mainloop:
  {
    pulsesrc->volume = volume;
    pulsesrc->volume_set = TRUE;
    GST_DEBUG_OBJECT (pulsesrc, "we have no mainloop");
    return;
  }
no_index:
  {
    pulsesrc->volume = volume;
    pulsesrc->volume_set = TRUE;
    GST_DEBUG_OBJECT (pulsesrc, "we don't have a stream index");
    return;
  }
volume_failed:
  {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_set_source_output_volume() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }
}

static void
gst_pulsesrc_set_stream_mute (GstPulseSrc * pulsesrc, gboolean mute)
{
  pa_operation *o = NULL;

  if (!pulsesrc->mainloop)
    goto no_mainloop;

  if (!pulsesrc->source_output_idx)
    goto no_index;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  GST_DEBUG_OBJECT (pulsesrc, "setting mute state to %d", mute);

  if (!(o = pa_context_set_source_output_mute (pulsesrc->context,
              pulsesrc->source_output_idx, mute, NULL, NULL)))
    goto mute_failed;

  /* We don't really care about the result of this call */
unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return;

  /* ERRORS */
no_mainloop:
  {
    pulsesrc->mute = mute;
    pulsesrc->mute_set = TRUE;
    GST_DEBUG_OBJECT (pulsesrc, "we have no mainloop");
    return;
  }
no_index:
  {
    pulsesrc->mute = mute;
    pulsesrc->mute_set = TRUE;
    GST_DEBUG_OBJECT (pulsesrc, "we don't have a stream index");
    return;
  }
mute_failed:
  {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_set_source_output_mute() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock;
  }
}
#endif

static void
gst_pulsesrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{

  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (object);

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
    case PROP_CLIENT:
      g_free (pulsesrc->client_name);
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (pulsesrc,
            "Empty PulseAudio client name not allowed. Resetting to default value");
        pulsesrc->client_name = gst_pulse_client_name ();
      } else
        pulsesrc->client_name = g_value_dup_string (value);
      break;
    case PROP_STREAM_PROPERTIES:
      if (pulsesrc->properties)
        gst_structure_free (pulsesrc->properties);
      pulsesrc->properties =
          gst_structure_copy (gst_value_get_structure (value));
      if (pulsesrc->proplist)
        pa_proplist_free (pulsesrc->proplist);
      pulsesrc->proplist = gst_pulse_make_proplist (pulsesrc->properties);
      break;
#ifdef HAVE_PULSE_1_0
    case PROP_VOLUME:
      gst_pulsesrc_set_stream_volume (pulsesrc, g_value_get_double (value));
      break;
    case PROP_MUTE:
      gst_pulsesrc_set_stream_mute (pulsesrc, g_value_get_boolean (value));
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{

  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, pulsesrc->server);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, pulsesrc->device);
      break;
    case PROP_DEVICE_NAME:
      g_value_take_string (value, gst_pulsesrc_device_description (pulsesrc));
      break;
    case PROP_CLIENT:
      g_value_set_string (value, pulsesrc->client_name);
      break;
    case PROP_STREAM_PROPERTIES:
      gst_value_set_structure (value, pulsesrc->properties);
      break;
    case PROP_SOURCE_OUTPUT_INDEX:
      g_value_set_uint (value, pulsesrc->source_output_idx);
      break;
#ifdef HAVE_PULSE_1_0
    case PROP_VOLUME:
      g_value_set_double (value, gst_pulsesrc_get_stream_volume (pulsesrc));
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_pulsesrc_get_stream_mute (pulsesrc));
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesrc_context_state_cb (pa_context * c, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (userdata);

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
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (userdata);

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
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (userdata);

  GST_LOG_OBJECT (pulsesrc, "got request for length %" G_GSIZE_FORMAT, length);

  if (pulsesrc->in_read) {
    /* only signal when reading */
    pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
  }
}

static void
gst_pulsesrc_stream_latency_update_cb (pa_stream * s, void *userdata)
{
  const pa_timing_info *info;
  pa_usec_t source_usec;

  info = pa_stream_get_timing_info (s);

  if (!info) {
    GST_LOG_OBJECT (GST_PULSESRC_CAST (userdata),
        "latency update (information unknown)");
    return;
  }
  source_usec = info->configured_source_usec;

  GST_LOG_OBJECT (GST_PULSESRC_CAST (userdata),
      "latency_update, %" G_GUINT64_FORMAT ", %d:%" G_GINT64_FORMAT ", %d:%"
      G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT,
      GST_TIMEVAL_TO_TIME (info->timestamp), info->write_index_corrupt,
      info->write_index, info->read_index_corrupt, info->read_index,
      info->source_usec, source_usec);
}

static void
gst_pulsesrc_stream_underflow_cb (pa_stream * s, void *userdata)
{
  GST_WARNING_OBJECT (GST_PULSESRC_CAST (userdata), "Got underflow");
}

static void
gst_pulsesrc_stream_overflow_cb (pa_stream * s, void *userdata)
{
  GST_WARNING_OBJECT (GST_PULSESRC_CAST (userdata), "Got overflow");
}

#ifdef HAVE_PULSE_1_0
static void
gst_pulsesrc_context_subscribe_cb (pa_context * c,
    pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
  GstPulseSrc *psrc = GST_PULSESRC (userdata);

  if (t != (PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT | PA_SUBSCRIPTION_EVENT_CHANGE)
      && t != (PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT | PA_SUBSCRIPTION_EVENT_NEW))
    return;

  if (idx != psrc->source_output_idx)
    return;

  /* Actually this event is also triggered when other properties of the stream
   * change that are unrelated to the volume. However it is probably cheaper to
   * signal the change here and check for the volume when the GObject property
   * is read instead of querying it always. */

  /* inform streaming thread to notify */
  g_atomic_int_compare_and_exchange (&psrc->notify, 0, 1);
}
#endif

static gboolean
gst_pulsesrc_open (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

  g_assert (!pulsesrc->context);
  g_assert (!pulsesrc->stream);

  GST_DEBUG_OBJECT (pulsesrc, "opening device");

  if (!(pulsesrc->context =
          pa_context_new (pa_threaded_mainloop_get_api (pulsesrc->mainloop),
              pulsesrc->client_name))) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED, ("Failed to create context"),
        (NULL));
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (pulsesrc->context,
      gst_pulsesrc_context_state_cb, pulsesrc);
#ifdef HAVE_PULSE_1_0
  pa_context_set_subscribe_callback (pulsesrc->context,
      gst_pulsesrc_context_subscribe_cb, pulsesrc);
#endif

  GST_DEBUG_OBJECT (pulsesrc, "connect to server %s",
      GST_STR_NULL (pulsesrc->server));

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
  GST_DEBUG_OBJECT (pulsesrc, "connected");

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return TRUE;

  /* ERRORS */
unlock_and_fail:
  {
    gst_pulsesrc_destroy_context (pulsesrc);

    pa_threaded_mainloop_unlock (pulsesrc->mainloop);

    return FALSE;
  }
}

static gboolean
gst_pulsesrc_close (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);

  pa_threaded_mainloop_lock (pulsesrc->mainloop);
  gst_pulsesrc_destroy_context (pulsesrc);
  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return TRUE;
}

static gboolean
gst_pulsesrc_unprepare (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);

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
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);
  size_t sum = 0;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);
  pulsesrc->in_read = TRUE;

#ifdef HAVE_PULSE_1_0
  if (g_atomic_int_compare_and_exchange (&pulsesrc->notify, 1, 0)) {
    g_object_notify (G_OBJECT (pulsesrc), "volume");
    g_object_notify (G_OBJECT (pulsesrc), "mute");
  }
#endif

  if (pulsesrc->paused)
    goto was_paused;

  while (length > 0) {
    size_t l;

    GST_LOG_OBJECT (pulsesrc, "reading %u bytes", length);

    /*check if we have a leftover buffer */
    if (!pulsesrc->read_buffer) {
      for (;;) {
        if (gst_pulsesrc_is_dead (pulsesrc, TRUE))
          goto unlock_and_fail;

        /* read all available data, we keep a pointer to the data and the length
         * and take from it what we need. */
        if (pa_stream_peek (pulsesrc->stream, &pulsesrc->read_buffer,
                &pulsesrc->read_buffer_length) < 0)
          goto peek_failed;

        GST_LOG_OBJECT (pulsesrc, "have data of %" G_GSIZE_FORMAT " bytes",
            pulsesrc->read_buffer_length);

        /* if we have data, process if */
        if (pulsesrc->read_buffer && pulsesrc->read_buffer_length)
          break;

        /* now wait for more data to become available */
        GST_LOG_OBJECT (pulsesrc, "waiting for data");
        pa_threaded_mainloop_wait (pulsesrc->mainloop);

        if (pulsesrc->paused)
          goto was_paused;
      }
    }

    l = pulsesrc->read_buffer_length >
        length ? length : pulsesrc->read_buffer_length;

    memcpy (data, pulsesrc->read_buffer, l);

    pulsesrc->read_buffer = (const guint8 *) pulsesrc->read_buffer + l;
    pulsesrc->read_buffer_length -= l;

    data = (guint8 *) data + l;
    length -= l;
    sum += l;

    if (pulsesrc->read_buffer_length <= 0) {
      /* we copied all of the data, drop it now */
      if (pa_stream_drop (pulsesrc->stream) < 0)
        goto drop_failed;

      /* reset pointer to data */
      pulsesrc->read_buffer = NULL;
      pulsesrc->read_buffer_length = 0;
    }
  }

  pulsesrc->in_read = FALSE;
  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return sum;

  /* ERRORS */
was_paused:
  {
    GST_LOG_OBJECT (pulsesrc, "we are paused");
    goto unlock_and_fail;
  }
peek_failed:
  {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_peek() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }
drop_failed:
  {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_drop() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }
unlock_and_fail:
  {
    pulsesrc->in_read = FALSE;
    pa_threaded_mainloop_unlock (pulsesrc->mainloop);

    return (guint) - 1;
  }
}

/* return the delay in samples */
static guint
gst_pulsesrc_delay (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);
  pa_usec_t t;
  int negative, res;
  guint result;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);
  if (gst_pulsesrc_is_dead (pulsesrc, TRUE))
    goto server_dead;

  /* get the latency, this can fail when we don't have a latency update yet.
   * We don't want to wait for latency updates here but we just return 0. */
  res = pa_stream_get_latency (pulsesrc->stream, &t, &negative);

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  if (res < 0) {
    GST_DEBUG_OBJECT (pulsesrc, "could not get latency");
    result = 0;
  } else {
    if (negative)
      result = 0;
    else
      result = (guint) ((t * pulsesrc->sample_spec.rate) / 1000000LL);
  }
  return result;

  /* ERRORS */
server_dead:
  {
    GST_DEBUG_OBJECT (pulsesrc, "the server is dead");
    pa_threaded_mainloop_unlock (pulsesrc->mainloop);
    return 0;
  }
}

static gboolean
gst_pulsesrc_create_stream (GstPulseSrc * pulsesrc, GstCaps * caps)
{
  pa_channel_map channel_map;
  GstStructure *s;
  gboolean need_channel_layout = FALSE;
  GstRingBufferSpec spec;
  const gchar *name;

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

  name = "Record Stream";
  if (pulsesrc->proplist) {
    if (!(pulsesrc->stream = pa_stream_new_with_proplist (pulsesrc->context,
                name, &pulsesrc->sample_spec,
                (need_channel_layout) ? NULL : &channel_map,
                pulsesrc->proplist))) {
      GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
          ("Failed to create stream: %s",
              pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
      goto unlock_and_fail;
    }
  } else if (!(pulsesrc->stream = pa_stream_new (pulsesrc->context,
              name, &pulsesrc->sample_spec,
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
  pa_stream_set_underflow_callback (pulsesrc->stream,
      gst_pulsesrc_stream_underflow_cb, pulsesrc);
  pa_stream_set_overflow_callback (pulsesrc->stream,
      gst_pulsesrc_stream_overflow_cb, pulsesrc);
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
  thiscaps = gst_pad_get_caps_reffed (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG_OBJECT (basesrc, "caps of src: %" GST_PTR_FORMAT, thiscaps);
  /* nothing or anything is allowed, we're done */
  if (thiscaps == NULL || gst_caps_is_any (thiscaps))
    goto no_nego_needed;

  /* get the peer caps */
  peercaps = gst_pad_peer_get_caps_reffed (GST_BASE_SRC_PAD (basesrc));
  GST_DEBUG_OBJECT (basesrc, "caps of peer: %" GST_PTR_FORMAT, peercaps);
  if (peercaps) {
    /* get intersection */
    caps = gst_caps_intersect (thiscaps, peercaps);
    GST_DEBUG_OBJECT (basesrc, "intersect: %" GST_PTR_FORMAT, caps);
    gst_caps_unref (thiscaps);
    gst_caps_unref (peercaps);
  } else {
    /* no peer, work with our own caps then */
    caps = thiscaps;
  }
  if (caps) {
    /* take first (and best, since they are sorted) possibility */
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
        result = gst_pulsesrc_create_stream (GST_PULSESRC_CAST (basesrc), caps);
        if (result)
          result = gst_pad_set_caps (GST_BASE_SRC_PAD (basesrc), caps);
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
  pa_buffer_attr wanted;
  const pa_buffer_attr *actual;
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);
  pa_stream_flags_t flags;
#ifdef HAVE_PULSE_1_0
  pa_operation *o;
#endif

  pa_threaded_mainloop_lock (pulsesrc->mainloop);

#ifdef HAVE_PULSE_1_0
  /* enable event notifications */
  GST_LOG_OBJECT (pulsesrc, "subscribing to context events");
  if (!(o = pa_context_subscribe (pulsesrc->context,
              PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL))) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_context_subscribe() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_operation_unref (o);
#endif

  wanted.maxlength = -1;
  wanted.tlength = -1;
  wanted.prebuf = 0;
  wanted.minreq = -1;
  wanted.fragsize = spec->segsize;

  GST_INFO_OBJECT (pulsesrc, "maxlength: %d", wanted.maxlength);
  GST_INFO_OBJECT (pulsesrc, "tlength:   %d", wanted.tlength);
  GST_INFO_OBJECT (pulsesrc, "prebuf:    %d", wanted.prebuf);
  GST_INFO_OBJECT (pulsesrc, "minreq:    %d", wanted.minreq);
  GST_INFO_OBJECT (pulsesrc, "fragsize:  %d", wanted.fragsize);

  flags = PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
      PA_STREAM_NOT_MONOTONIC | PA_STREAM_ADJUST_LATENCY |
      PA_STREAM_START_CORKED;

#ifdef HAVE_PULSE_1_0
  if (pulsesrc->mute_set && pulsesrc->mute)
    flags |= PA_STREAM_START_MUTED;
#endif

  if (pa_stream_connect_record (pulsesrc->stream, pulsesrc->device, &wanted,
          flags) < 0) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  pulsesrc->corked = TRUE;

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

  /* store the source output index so it can be accessed via a property */
  pulsesrc->source_output_idx = pa_stream_get_index (pulsesrc->stream);
  g_object_notify (G_OBJECT (pulsesrc), "source-output-index");

#ifdef HAVE_PULSE_1_0
  if (pulsesrc->volume_set) {
    gst_pulsesrc_set_stream_volume (pulsesrc, pulsesrc->volume);
    pulsesrc->volume_set = FALSE;
  }
#endif

  /* get the actual buffering properties now */
  actual = pa_stream_get_buffer_attr (pulsesrc->stream);

  GST_INFO_OBJECT (pulsesrc, "maxlength: %d", actual->maxlength);
  GST_INFO_OBJECT (pulsesrc, "tlength:   %d (wanted: %d)",
      actual->tlength, wanted.tlength);
  GST_INFO_OBJECT (pulsesrc, "prebuf:    %d", actual->prebuf);
  GST_INFO_OBJECT (pulsesrc, "minreq:    %d (wanted %d)", actual->minreq,
      wanted.minreq);
  GST_INFO_OBJECT (pulsesrc, "fragsize:  %d (wanted %d)",
      actual->fragsize, wanted.fragsize);

  if (actual->fragsize >= wanted.fragsize) {
    spec->segsize = actual->fragsize;
  } else {
    spec->segsize = actual->fragsize * (wanted.fragsize / actual->fragsize);
  }
  spec->segtotal = actual->maxlength / spec->segsize;

  pa_threaded_mainloop_unlock (pulsesrc->mainloop);

  return TRUE;

unlock_and_fail:
  {
    gst_pulsesrc_destroy_stream (pulsesrc);

    pa_threaded_mainloop_unlock (pulsesrc->mainloop);
    return FALSE;
  }
}

static void
gst_pulsesrc_success_cb (pa_stream * s, int success, void *userdata)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (userdata);

  pulsesrc->operation_success = ! !success;
  pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
}

static void
gst_pulsesrc_reset (GstAudioSrc * asrc)
{
  GstPulseSrc *pulsesrc = GST_PULSESRC_CAST (asrc);
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesrc->mainloop);
  GST_DEBUG_OBJECT (pulsesrc, "reset");

  if (gst_pulsesrc_is_dead (pulsesrc, TRUE))
    goto unlock_and_fail;

  if (!(o =
          pa_stream_flush (pulsesrc->stream, gst_pulsesrc_success_cb,
              pulsesrc))) {
    GST_ELEMENT_ERROR (pulsesrc, RESOURCE, FAILED,
        ("pa_stream_flush() failed: %s",
            pa_strerror (pa_context_errno (pulsesrc->context))), (NULL));
    goto unlock_and_fail;
  }

  pulsesrc->paused = TRUE;
  /* Inform anyone waiting in _write() call that it shall wakeup */
  if (pulsesrc->in_read) {
    pa_threaded_mainloop_signal (pulsesrc->mainloop, 0);
  }

  pulsesrc->operation_success = FALSE;
  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {

    if (gst_pulsesrc_is_dead (pulsesrc, TRUE))
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

/* update the corked state of a stream, must be called with the mainloop
 * lock */
static gboolean
gst_pulsesrc_set_corked (GstPulseSrc * psrc, gboolean corked, gboolean wait)
{
  pa_operation *o = NULL;
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (psrc, "setting corked state to %d", corked);
  if (psrc->corked != corked) {
    if (!(o = pa_stream_cork (psrc->stream, corked,
                gst_pulsesrc_success_cb, psrc)))
      goto cork_failed;

    while (wait && pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait (psrc->mainloop);
      if (gst_pulsesrc_is_dead (psrc, TRUE))
        goto server_dead;
    }
    psrc->corked = corked;
  } else {
    GST_DEBUG_OBJECT (psrc, "skipping, already in requested state");
  }
  res = TRUE;

cleanup:
  if (o)
    pa_operation_unref (o);

  return res;

  /* ERRORS */
server_dead:
  {
    GST_DEBUG_OBJECT (psrc, "the server is dead");
    goto cleanup;
  }
cork_failed:
  {
    GST_ELEMENT_ERROR (psrc, RESOURCE, FAILED,
        ("pa_stream_cork() failed: %s",
            pa_strerror (pa_context_errno (psrc->context))), (NULL));
    goto cleanup;
  }
}

/* start/resume playback ASAP */
static gboolean
gst_pulsesrc_play (GstPulseSrc * psrc)
{
  pa_threaded_mainloop_lock (psrc->mainloop);
  GST_DEBUG_OBJECT (psrc, "playing");
  psrc->paused = FALSE;
  gst_pulsesrc_set_corked (psrc, FALSE, FALSE);
  pa_threaded_mainloop_unlock (psrc->mainloop);

  return TRUE;
}

/* pause/stop playback ASAP */
static gboolean
gst_pulsesrc_pause (GstPulseSrc * psrc)
{
  pa_threaded_mainloop_lock (psrc->mainloop);
  GST_DEBUG_OBJECT (psrc, "pausing");
  /* make sure the commit method stops writing */
  psrc->paused = TRUE;
  if (psrc->in_read) {
    /* we are waiting in a read, signal */
    GST_DEBUG_OBJECT (psrc, "signal read");
    pa_threaded_mainloop_signal (psrc->mainloop, 0);
  }
  pa_threaded_mainloop_unlock (psrc->mainloop);

  return TRUE;
}

static GstStateChangeReturn
gst_pulsesrc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPulseSrc *this = GST_PULSESRC_CAST (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(this->mainloop = pa_threaded_mainloop_new ()))
        goto mainloop_failed;
      if (pa_threaded_mainloop_start (this->mainloop) < 0) {
        pa_threaded_mainloop_free (this->mainloop);
        this->mainloop = NULL;
        goto mainloop_start_failed;
      }

      if (!this->mixer)
        this->mixer =
            gst_pulsemixer_ctrl_new (G_OBJECT (this), this->server,
            this->device, GST_PULSEMIXER_SOURCE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* uncork and start recording */
      gst_pulsesrc_play (this);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* stop recording ASAP by corking */
      pa_threaded_mainloop_lock (this->mainloop);
      GST_DEBUG_OBJECT (this, "corking");
      gst_pulsesrc_set_corked (this, TRUE, FALSE);
      pa_threaded_mainloop_unlock (this->mainloop);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* now make sure we get out of the _read method */
      gst_pulsesrc_pause (this);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (this->mixer) {
        gst_pulsemixer_ctrl_free (this->mixer);
        this->mixer = NULL;
      }

      if (this->mainloop)
        pa_threaded_mainloop_stop (this->mainloop);

      gst_pulsesrc_destroy_context (this);

      if (this->mainloop) {
        pa_threaded_mainloop_free (this->mainloop);
        this->mainloop = NULL;
      }
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
mainloop_failed:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, FAILED,
        ("pa_threaded_mainloop_new() failed"), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
mainloop_start_failed:
  {
    GST_ELEMENT_ERROR (this, RESOURCE, FAILED,
        ("pa_threaded_mainloop_start() failed"), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
}
