/*
 * GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2009-2010 Chris Robinson <chris.kcat@gmail.com>
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
 * SECTION:element-openalsink
 *
 * This element renders raw audio samples using the OpenAL API
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc ! audioconvert ! volume volume=0.1 ! openalsink
 * ]| will output a sine wave (continuous beep sound) to your sound card (with
 * a very low volume as precaution).
 * |[
 * gst-launch -v filesrc location=music.ogg ! decodebin ! audioconvert ! audioresample ! openalsink
 * ]| will play an Ogg/Vorbis audio file and output it using OpenAL.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstopenalsink.h"

GST_DEBUG_CATEGORY (openalsink_debug);

static void gst_openal_sink_dispose (GObject * object);
static void gst_openal_sink_finalize (GObject * object);

static void gst_openal_sink_get_property (GObject * object, guint prop_id,
    GValue * val, GParamSpec * pspec);
static void gst_openal_sink_set_property (GObject * object, guint prop_id,
    const GValue * val, GParamSpec * pspec);

static GstCaps *gst_openal_sink_getcaps (GstBaseSink * bsink);

static gboolean gst_openal_sink_open (GstAudioSink * asink);
static gboolean gst_openal_sink_close (GstAudioSink * asink);
static gboolean gst_openal_sink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_openal_sink_unprepare (GstAudioSink * asink);
static guint gst_openal_sink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_openal_sink_delay (GstAudioSink * asink);
static void gst_openal_sink_reset (GstAudioSink * asink);

#define DEFAULT_DEVICE NULL

enum
{
  PROP_0,

  PROP_DEVICE,
  PROP_DEVICE_NAME,

  PROP_DEVICE_HDL,
  PROP_CONTEXT_HDL,
  PROP_SOURCE_ID
};

static GstStaticPadTemplate openalsink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "width = (int) 32, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ]; "
        "audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ]; "
        "audio/x-raw-int, "
        "signed = (boolean) FALSE, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ]; "
        "audio/x-mulaw, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]")
    );

static PFNALCSETTHREADCONTEXTPROC palcSetThreadContext;
static PFNALCGETTHREADCONTEXTPROC palcGetThreadContext;

static inline ALCcontext *
pushContext (ALCcontext * ctx)
{
  ALCcontext *old;
  if (!palcGetThreadContext || !palcSetThreadContext)
    return NULL;

  old = palcGetThreadContext ();
  if (old != ctx)
    palcSetThreadContext (ctx);
  return old;
}

static inline void
popContext (ALCcontext * old, ALCcontext * ctx)
{
  if (!palcGetThreadContext || !palcSetThreadContext)
    return;

  if (old != ctx)
    palcSetThreadContext (old);
}

static inline ALenum
checkALError (const char *fname, unsigned int fline)
{
  ALenum err = alGetError ();
  if (err != AL_NO_ERROR)
    g_warning ("%s:%u: context error: %s", fname, fline, alGetString (err));
  return err;
}

#define checkALError() checkALError(__FILE__, __LINE__)

GST_BOILERPLATE (GstOpenALSink, gst_openal_sink, GstAudioSink,
    GST_TYPE_AUDIO_SINK);

static void
gst_openal_sink_dispose (GObject * object)
{
  GstOpenALSink *sink = GST_OPENAL_SINK (object);

  if (sink->probed_caps)
    gst_caps_unref (sink->probed_caps);
  sink->probed_caps = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* GObject vmethod implementations */
static void
gst_openal_sink_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class, "Audio sink (OpenAL)",
      "Sink/Audio",
      "Output to a sound device via OpenAL",
      "Chris Robinson <chris.kcat@gmail.com>");

  gst_element_class_add_static_pad_template (element_class,
      &openalsink_sink_factory);
}

/* initialize the plugin's class */
static void
gst_openal_sink_class_init (GstOpenALSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;
  GstAudioSinkClass *gstaudiosink_class = (GstAudioSinkClass *) klass;
  GParamSpec *spec;

  if (alcIsExtensionPresent (NULL, "ALC_EXT_thread_local_context")) {
    palcSetThreadContext = alcGetProcAddress (NULL, "alcSetThreadContext");
    palcGetThreadContext = alcGetProcAddress (NULL, "alcGetThreadContext");
  }

  GST_DEBUG_CATEGORY_INIT (openalsink_debug, "openalsink", 0, "OpenAL sink");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_openal_sink_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_openal_sink_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_openal_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_openal_sink_get_property);

  spec = g_param_spec_string ("device-name", "Device name",
      "Opened OpenAL device name", "", G_PARAM_READABLE);
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME, spec);

  spec = g_param_spec_string ("device", "Device", "OpenAL device string",
      DEFAULT_DEVICE, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_DEVICE, spec);

  spec = g_param_spec_pointer ("device-handle", "ALCdevice",
      "Custom playback device", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_DEVICE_HDL, spec);

  spec = g_param_spec_pointer ("context-handle", "ALCcontext",
      "Custom playback context", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_CONTEXT_HDL, spec);

  spec = g_param_spec_uint ("source-id", "Source ID", "Custom playback sID",
      0, UINT_MAX, 0, G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_SOURCE_ID, spec);

  parent_class = g_type_class_peek_parent (klass);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_openal_sink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_openal_sink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_openal_sink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_openal_sink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_openal_sink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_openal_sink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_openal_sink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_openal_sink_reset);
}

static void
gst_openal_sink_init (GstOpenALSink * sink, GstOpenALSinkClass * klass)
{
  GST_DEBUG_OBJECT (sink, "initializing openalsink");

  sink->devname = g_strdup (DEFAULT_DEVICE);

  sink->custom_dev = NULL;
  sink->custom_ctx = NULL;
  sink->custom_sID = 0;

  sink->device = NULL;
  sink->context = NULL;
  sink->sID = 0;

  sink->bID_idx = 0;
  sink->bID_count = 0;
  sink->bIDs = NULL;
  sink->bID_length = 0;

  sink->write_reset = AL_FALSE;
  sink->probed_caps = NULL;

  sink->openal_lock = g_mutex_new ();
}

static void
gst_openal_sink_finalize (GObject * object)
{
  GstOpenALSink *sink = GST_OPENAL_SINK (object);

  g_free (sink->devname);
  sink->devname = NULL;
  g_mutex_free (sink->openal_lock);
  sink->openal_lock = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_openal_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenALSink *sink = GST_OPENAL_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (sink->devname);
      sink->devname = g_value_dup_string (value);
      if (sink->probed_caps)
        gst_caps_unref (sink->probed_caps);
      sink->probed_caps = NULL;
      break;
    case PROP_DEVICE_HDL:
      if (!sink->device)
        sink->custom_dev = g_value_get_pointer (value);
      break;
    case PROP_CONTEXT_HDL:
      if (!sink->device)
        sink->custom_ctx = g_value_get_pointer (value);
      break;
    case PROP_SOURCE_ID:
      if (!sink->device)
        sink->custom_sID = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_openal_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenALSink *sink = GST_OPENAL_SINK (object);
  const ALCchar *name = sink->devname;
  ALCdevice *device = sink->device;
  ALCcontext *context = sink->context;
  ALuint sourceID = sink->sID;

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      name = "";
      if (device)
        name = alcGetString (device, ALC_DEVICE_SPECIFIER);
      /* fall-through */
    case PROP_DEVICE:
      g_value_set_string (value, name);
      break;
    case PROP_DEVICE_HDL:
      if (!device)
        device = sink->custom_dev;
      g_value_set_pointer (value, device);
      break;
    case PROP_CONTEXT_HDL:
      if (!context)
        context = sink->custom_ctx;
      g_value_set_pointer (value, context);
      break;
    case PROP_SOURCE_ID:
      if (!sourceID)
        sourceID = sink->custom_sID;
      g_value_set_uint (value, sourceID);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_openal_helper_probe_caps (ALCcontext * ctx)
{
  static const struct
  {
    gint count;
    GstAudioChannelPosition pos[8];
  } chans[] = {
    {
      1, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_MONO}}, {
      2, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}}, {
      4, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
            GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
            GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}}, {
      6, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
            GST_AUDIO_CHANNEL_POSITION_LFE,
            GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
            GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}}, {
      7, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
            GST_AUDIO_CHANNEL_POSITION_LFE,
            GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
            GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
            GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}}, {
      8, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
            GST_AUDIO_CHANNEL_POSITION_LFE,
            GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
            GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
            GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
            GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}},};
  GstStructure *structure;
  ALCcontext *old;
  GstCaps *caps;

  old = pushContext (ctx);

  caps = gst_caps_new_empty ();
  if (alIsExtensionPresent ("AL_EXT_MCFORMATS")) {
    const char *fmt32[] = {
      "AL_FORMAT_MONO_FLOAT32", "AL_FORMAT_STEREO_FLOAT32",
      "AL_FORMAT_QUAD32", "AL_FORMAT_51CHN32", "AL_FORMAT_61CHN32",
      "AL_FORMAT_71CHN32", NULL
    }, *fmt16[] = {
    "AL_FORMAT_MONO16", "AL_FORMAT_STEREO16", "AL_FORMAT_QUAD16",
          "AL_FORMAT_51CHN16", "AL_FORMAT_61CHN16", "AL_FORMAT_71CHN16", NULL},
        *fmt8[] = {
    "AL_FORMAT_MONO8", "AL_FORMAT_STEREO8", "AL_FORMAT_QUAD8",
          "AL_FORMAT_51CHN8", "AL_FORMAT_61CHN8", "AL_FORMAT_71CHN8", NULL};
    int i;

    if (alIsExtensionPresent ("AL_EXT_FLOAT32")) {
      for (i = 0; fmt32[i]; i++) {
        ALenum val = alGetEnumValue (fmt32[i]);
        if (checkALError () != AL_NO_ERROR || val == 0 || val == -1)
          continue;

        structure = gst_structure_new ("audio/x-raw-float",
            "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE,
            OPENAL_MAX_RATE, "width", G_TYPE_INT, 32, NULL);
        gst_structure_set (structure, "channels", G_TYPE_INT,
            chans[i].count, NULL);
        if (chans[i].count > 2)
          gst_audio_set_channel_positions (structure, chans[i].pos);
        gst_caps_append_structure (caps, structure);
      }
    }
    for (i = 0; fmt16[i]; i++) {
      ALenum val = alGetEnumValue (fmt16[i]);
      if (checkALError () != AL_NO_ERROR || val == 0 || val == -1)
        continue;

      structure = gst_structure_new ("audio/x-raw-int",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE,
          "width", G_TYPE_INT, 16,
          "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_structure_set (structure, "channels", G_TYPE_INT,
          chans[i].count, NULL);
      if (chans[i].count > 2)
        gst_audio_set_channel_positions (structure, chans[i].pos);
      gst_caps_append_structure (caps, structure);
    }
    for (i = 0; fmt8[i]; i++) {
      ALenum val = alGetEnumValue (fmt8[i]);
      if (checkALError () != AL_NO_ERROR || val == 0 || val == -1)
        continue;

      structure = gst_structure_new ("audio/x-raw-int",
          "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE,
          "width", G_TYPE_INT, 8,
          "depth", G_TYPE_INT, 8, "signed", G_TYPE_BOOLEAN, FALSE, NULL);
      gst_structure_set (structure, "channels", G_TYPE_INT,
          chans[i].count, NULL);
      if (chans[i].count > 2)
        gst_audio_set_channel_positions (structure, chans[i].pos);
      gst_caps_append_structure (caps, structure);
    }
  } else {
    if (alIsExtensionPresent ("AL_EXT_FLOAT32")) {
      structure = gst_structure_new ("audio/x-raw-float",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE,
          "width", G_TYPE_INT, 32, "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
      gst_caps_append_structure (caps, structure);
    }

    structure = gst_structure_new ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE,
        "width", G_TYPE_INT, 16,
        "depth", G_TYPE_INT, 16,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
    gst_caps_append_structure (caps, structure);

    structure = gst_structure_new ("audio/x-raw-int",
        "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE,
        "width", G_TYPE_INT, 8,
        "depth", G_TYPE_INT, 8,
        "signed", G_TYPE_BOOLEAN, FALSE,
        "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
    gst_caps_append_structure (caps, structure);
  }

  if (alIsExtensionPresent ("AL_EXT_MULAW_MCFORMATS")) {
    const char *fmtmulaw[] = {
      "AL_FORMAT_MONO_MULAW", "AL_FORMAT_STEREO_MULAW",
      "AL_FORMAT_QUAD_MULAW", "AL_FORMAT_51CHN_MULAW",
      "AL_FORMAT_61CHN_MULAW", "AL_FORMAT_71CHN_MULAW", NULL
    };
    int i;

    for (i = 0; fmtmulaw[i]; i++) {
      ALenum val = alGetEnumValue (fmtmulaw[i]);
      if (checkALError () != AL_NO_ERROR || val == 0 || val == -1)
        continue;

      structure = gst_structure_new ("audio/x-mulaw",
          "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE, NULL);
      gst_structure_set (structure, "channels", G_TYPE_INT,
          chans[i].count, NULL);
      if (chans[i].count > 2)
        gst_audio_set_channel_positions (structure, chans[i].pos);
      gst_caps_append_structure (caps, structure);
    }
  } else if (alIsExtensionPresent ("AL_EXT_MULAW")) {
    structure = gst_structure_new ("audio/x-mulaw",
        "rate", GST_TYPE_INT_RANGE, OPENAL_MIN_RATE, OPENAL_MAX_RATE,
        "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
    gst_caps_append_structure (caps, structure);
  }

  popContext (old, ctx);
  return caps;
}

static GstCaps *
gst_openal_sink_getcaps (GstBaseSink * bsink)
{
  GstOpenALSink *sink = GST_OPENAL_SINK (bsink);
  GstCaps *caps;

  if (sink->device == NULL) {
    GstPad *pad = GST_BASE_SINK_PAD (bsink);
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  } else if (sink->probed_caps)
    caps = gst_caps_copy (sink->probed_caps);
  else {
    if (sink->context)
      caps = gst_openal_helper_probe_caps (sink->context);
    else if (sink->custom_ctx)
      caps = gst_openal_helper_probe_caps (sink->custom_ctx);
    else {
      ALCcontext *ctx = alcCreateContext (sink->device, NULL);
      if (ctx) {
        caps = gst_openal_helper_probe_caps (ctx);
        alcDestroyContext (ctx);
      } else {
        GST_ELEMENT_WARNING (sink, RESOURCE, FAILED,
            ("Could not create temporary context."),
            GST_ALC_ERROR (sink->device));
        caps = NULL;
      }
    }

    if (caps && !gst_caps_is_empty (caps))
      sink->probed_caps = gst_caps_copy (caps);
  }

  return caps;
}

static gboolean
gst_openal_sink_open (GstAudioSink * asink)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);

  if (openal->custom_dev) {
    ALCint val = -1;
    alcGetIntegerv (openal->custom_dev, ALC_ATTRIBUTES_SIZE, 1, &val);
    if (val > 0) {
      if (!openal->custom_ctx ||
          alcGetContextsDevice (openal->custom_ctx) == openal->custom_dev)
        openal->device = openal->custom_dev;
    }
  } else if (openal->custom_ctx)
    openal->device = alcGetContextsDevice (openal->custom_ctx);
  else
    openal->device = alcOpenDevice (openal->devname);
  if (!openal->device) {
    GST_ELEMENT_ERROR (openal, RESOURCE, OPEN_WRITE,
        ("Could not open audio device for playback."),
        GST_ALC_ERROR (openal->device));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_openal_sink_close (GstAudioSink * asink)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);

  if (!openal->custom_dev && !openal->custom_ctx) {
    if (alcCloseDevice (openal->device) == ALC_FALSE) {
      GST_ELEMENT_ERROR (openal, RESOURCE, CLOSE,
          ("Could not close audio device."), GST_ALC_ERROR (openal->device));
      return FALSE;
    }
  }
  openal->device = NULL;

  if (openal->probed_caps)
    gst_caps_unref (openal->probed_caps);
  openal->probed_caps = NULL;

  return TRUE;
}

static void
gst_openal_sink_parse_spec (GstOpenALSink * openal,
    const GstRingBufferSpec * spec)
{
  ALuint format = AL_NONE;

  GST_DEBUG_OBJECT (openal, "Looking up format for type %d, gst-format %d, "
      "and %d channels", spec->type, spec->format, spec->channels);

  /* Don't need to verify supported formats, since the probed caps will only
   * report what was detected and we shouldn't get anything different */
  switch (spec->type) {
    case GST_BUFTYPE_LINEAR:
      switch (spec->format) {
        case GST_U8:
          if (spec->channels == 1)
            format = AL_FORMAT_MONO8;
          if (spec->channels == 2)
            format = AL_FORMAT_STEREO8;
          if (spec->channels == 4)
            format = AL_FORMAT_QUAD8;
          if (spec->channels == 6)
            format = AL_FORMAT_51CHN8;
          if (spec->channels == 7)
            format = AL_FORMAT_61CHN8;
          if (spec->channels == 8)
            format = AL_FORMAT_71CHN8;
          break;

        case GST_S16_NE:
          if (spec->channels == 1)
            format = AL_FORMAT_MONO16;
          if (spec->channels == 2)
            format = AL_FORMAT_STEREO16;
          if (spec->channels == 4)
            format = AL_FORMAT_QUAD16;
          if (spec->channels == 6)
            format = AL_FORMAT_51CHN16;
          if (spec->channels == 7)
            format = AL_FORMAT_61CHN16;
          if (spec->channels == 8)
            format = AL_FORMAT_71CHN16;
          break;

        default:
          break;
      }
      break;

    case GST_BUFTYPE_FLOAT:
      switch (spec->format) {
        case GST_FLOAT32_NE:
          if (spec->channels == 1)
            format = AL_FORMAT_MONO_FLOAT32;
          if (spec->channels == 2)
            format = AL_FORMAT_STEREO_FLOAT32;
          if (spec->channels == 4)
            format = AL_FORMAT_QUAD32;
          if (spec->channels == 6)
            format = AL_FORMAT_51CHN32;
          if (spec->channels == 7)
            format = AL_FORMAT_61CHN32;
          if (spec->channels == 8)
            format = AL_FORMAT_71CHN32;
          break;

        default:
          break;
      }
      break;

    case GST_BUFTYPE_MU_LAW:
      switch (spec->format) {
        case GST_MU_LAW:
          if (spec->channels == 1)
            format = AL_FORMAT_MONO_MULAW;
          if (spec->channels == 2)
            format = AL_FORMAT_STEREO_MULAW;
          if (spec->channels == 4)
            format = AL_FORMAT_QUAD_MULAW;
          if (spec->channels == 6)
            format = AL_FORMAT_51CHN_MULAW;
          if (spec->channels == 7)
            format = AL_FORMAT_61CHN_MULAW;
          if (spec->channels == 8)
            format = AL_FORMAT_71CHN_MULAW;
          break;

        default:
          break;
      }
      break;

    default:
      break;
  }

  openal->bytes_per_sample = spec->bytes_per_sample;
  openal->srate = spec->rate;
  openal->bID_count = spec->segtotal;
  openal->bID_length = spec->segsize;
  openal->format = format;
}

static gboolean
gst_openal_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);
  ALCcontext *ctx, *old;

  if (openal->context && !gst_openal_sink_unprepare (asink))
    return FALSE;

  if (openal->custom_ctx)
    ctx = openal->custom_ctx;
  else {
    ALCint attribs[3] = { 0, 0, 0 };

    /* Don't try to change the playback frequency of an app's device */
    if (!openal->custom_dev) {
      attribs[0] = ALC_FREQUENCY;
      attribs[1] = spec->rate;
      attribs[2] = 0;
    }

    ctx = alcCreateContext (openal->device, attribs);
    if (!ctx) {
      GST_ELEMENT_ERROR (openal, RESOURCE, FAILED,
          ("Unable to prepare device."), GST_ALC_ERROR (openal->device));
      return FALSE;
    }
  }

  old = pushContext (ctx);

  if (openal->custom_sID) {
    if (!openal->custom_ctx || !alIsSource (openal->custom_sID)) {
      GST_ELEMENT_ERROR (openal, RESOURCE, NOT_FOUND, (NULL),
          ("Invalid source ID specified for context"));
      goto fail;
    }
    openal->sID = openal->custom_sID;
  } else {
    ALuint sourceID;

    alGenSources (1, &sourceID);
    if (checkALError () != AL_NO_ERROR) {
      GST_ELEMENT_ERROR (openal, RESOURCE, NO_SPACE_LEFT, (NULL),
          ("Unable to generate source"));
      goto fail;
    }
    openal->sID = sourceID;
  }

  gst_openal_sink_parse_spec (openal, spec);
  if (openal->format == AL_NONE) {
    GST_ELEMENT_ERROR (openal, RESOURCE, SETTINGS, (NULL),
        ("Unable to get type %d, format %d, and %d channels",
            spec->type, spec->format, spec->channels));
    goto fail;
  }

  openal->bIDs = g_malloc (openal->bID_count * sizeof (*openal->bIDs));
  if (!openal->bIDs) {
    GST_ELEMENT_ERROR (openal, RESOURCE, FAILED, ("Out of memory."),
        ("Unable to allocate buffer IDs"));
    goto fail;
  }

  alGenBuffers (openal->bID_count, openal->bIDs);
  if (checkALError () != AL_NO_ERROR) {
    GST_ELEMENT_ERROR (openal, RESOURCE, NO_SPACE_LEFT, (NULL),
        ("Unable to generate %d buffers", openal->bID_count));
    goto fail;
  }
  openal->bID_idx = 0;

  popContext (old, ctx);
  openal->context = ctx;
  return TRUE;

fail:
  if (!openal->custom_sID && openal->sID)
    alDeleteSources (1, &openal->sID);
  openal->sID = 0;

  g_free (openal->bIDs);
  openal->bIDs = NULL;
  openal->bID_count = 0;
  openal->bID_length = 0;

  popContext (old, ctx);
  if (!openal->custom_ctx)
    alcDestroyContext (ctx);
  return FALSE;
}

static gboolean
gst_openal_sink_unprepare (GstAudioSink * asink)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);
  ALCcontext *old;

  if (!openal->context)
    return TRUE;

  old = pushContext (openal->context);

  alSourceStop (openal->sID);
  alSourcei (openal->sID, AL_BUFFER, 0);

  if (!openal->custom_sID)
    alDeleteSources (1, &openal->sID);
  openal->sID = 0;

  alDeleteBuffers (openal->bID_count, openal->bIDs);
  g_free (openal->bIDs);
  openal->bIDs = NULL;
  openal->bID_idx = 0;
  openal->bID_count = 0;
  openal->bID_length = 0;

  checkALError ();
  popContext (old, openal->context);
  if (!openal->custom_ctx)
    alcDestroyContext (openal->context);
  openal->context = NULL;

  return TRUE;
}

static guint
gst_openal_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);
  ALint processed, queued, state;
  ALCcontext *old;
  gulong rest_us;

  g_assert (length == openal->bID_length);

  old = pushContext (openal->context);

  rest_us = (guint64) (openal->bID_length / openal->bytes_per_sample) *
      G_USEC_PER_SEC / openal->srate / 2;
  do {
    alGetSourcei (openal->sID, AL_SOURCE_STATE, &state);
    alGetSourcei (openal->sID, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei (openal->sID, AL_BUFFERS_PROCESSED, &processed);
    if (checkALError () != AL_NO_ERROR) {
      GST_ELEMENT_ERROR (openal, RESOURCE, WRITE, (NULL),
          ("Source state error detected"));
      length = 0;
      goto out_nolock;
    }

    if (processed > 0 || queued < openal->bID_count)
      break;
    if (state != AL_PLAYING)
      alSourcePlay (openal->sID);
    g_usleep (rest_us);
  } while (1);

  GST_OPENAL_SINK_LOCK (openal);
  if (openal->write_reset != AL_FALSE) {
    openal->write_reset = AL_FALSE;
    length = 0;
    goto out;
  }

  queued -= processed;
  while (processed-- > 0) {
    ALuint bid;
    alSourceUnqueueBuffers (openal->sID, 1, &bid);
  }
  if (state == AL_STOPPED) {
    /* "Restore" from underruns (not actually needed, but it keeps delay
     * calculations correct while rebuffering) */
    alSourceRewind (openal->sID);
  }

  alBufferData (openal->bIDs[openal->bID_idx], openal->format,
      data, openal->bID_length, openal->srate);
  alSourceQueueBuffers (openal->sID, 1, &openal->bIDs[openal->bID_idx]);
  openal->bID_idx = (openal->bID_idx + 1) % openal->bID_count;
  queued++;

  if (state != AL_PLAYING && queued == openal->bID_count)
    alSourcePlay (openal->sID);

  if (checkALError () != ALC_NO_ERROR) {
    GST_ELEMENT_ERROR (openal, RESOURCE, WRITE, (NULL),
        ("Source queue error detected"));
    goto out;
  }

out:
  GST_OPENAL_SINK_UNLOCK (openal);
out_nolock:
  popContext (old, openal->context);
  return length;
}

static guint
gst_openal_sink_delay (GstAudioSink * asink)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);
  ALint queued, state, offset, delay;
  ALCcontext *old;

  if (!openal->context)
    return 0;

  GST_OPENAL_SINK_LOCK (openal);
  old = pushContext (openal->context);

  delay = 0;
  alGetSourcei (openal->sID, AL_BUFFERS_QUEUED, &queued);
  /* Order here is important. If the offset is queried after the state and an
   * underrun occurs in between the two calls, it can end up with a 0 offset
   * in a playing state, incorrectly reporting a len*queued/bps delay. */
  alGetSourcei (openal->sID, AL_BYTE_OFFSET, &offset);
  alGetSourcei (openal->sID, AL_SOURCE_STATE, &state);

  /* Note: state=stopped is an underrun, meaning all buffers are processed
   * and there's no delay when writing the next buffer. Pre-buffering is
   * state=initial, which will introduce a delay while writing. */
  if (checkALError () == AL_NO_ERROR && state != AL_STOPPED)
    delay = ((queued * openal->bID_length) - offset) / openal->bytes_per_sample;

  popContext (old, openal->context);
  GST_OPENAL_SINK_UNLOCK (openal);

  return delay;
}

static void
gst_openal_sink_reset (GstAudioSink * asink)
{
  GstOpenALSink *openal = GST_OPENAL_SINK (asink);
  ALCcontext *old;

  GST_OPENAL_SINK_LOCK (openal);
  old = pushContext (openal->context);

  openal->write_reset = AL_TRUE;
  alSourceStop (openal->sID);
  alSourceRewind (openal->sID);
  alSourcei (openal->sID, AL_BUFFER, 0);
  checkALError ();

  popContext (old, openal->context);
  GST_OPENAL_SINK_UNLOCK (openal);
}
