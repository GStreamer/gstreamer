/*
 * gstfluidsynth - fluidsynth plugin for gstreamer
 *
 * Copyright 2013 Wim Taymans <wim.taymans@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-fluidsynth
 * @see_also: timidity, wildmidi
 *
 * This element renders midi-events as audio streams using
 * <ulink url="http://fluidsynth.sourceforge.net//">Fluidsynth</ulink>.
 * It offers better sound quality compared to the timidity or wildmidi element.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 filesrc location=song.mid ! midiparse ! fluidsynth ! pulsesink
 * ]| This example pipeline will parse the midi and render to raw audio which is
 * played via pulseaudio.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define FLUIDSYNTH_RATE 44100
#define FLUIDSYNTH_BPS  (4 * 2)

#include <gst/gst.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gst/audio/audio.h>

#include "gstfluidsynth.h"

GST_DEBUG_CATEGORY_STATIC (gst_fluidsynth_debug);
#define GST_CAT_DEFAULT gst_fluidsynth_debug

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define SOUNDFONT_PATH "/usr/share/sounds/sf2/"

#define DEFAULT_SOUNDFONT       NULL
#define DEFAULT_SYNTH_CHORUS    TRUE
#define DEFAULT_SYNTH_REVERB    TRUE
#define DEFAULT_SYNTH_GAIN      0.2
#define DEFAULT_SYNTH_POLYPHONY 256

enum
{
  PROP_0,
  PROP_SOUNDFONT,
  PROP_SYNTH_CHORUS,
  PROP_SYNTH_REVERB,
  PROP_SYNTH_GAIN,
  PROP_SYNTH_POLYPHONY
};

static void gst_fluidsynth_finalize (GObject * object);

static gboolean gst_fluidsynth_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_fluidsynth_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_fluidsynth_chain (GstPad * sinkpad, GstObject * parent,
    GstBuffer * buffer);

static void gst_fluidsynth_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fluidsynth_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-midi-event")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (F32) ", "
        "rate = (int) " G_STRINGIFY (FLUIDSYNTH_RATE) ", "
        "channels = (int) 2, " "layout = (string) interleaved"));

#define parent_class gst_fluidsynth_parent_class
G_DEFINE_TYPE (GstFluidsynth, gst_fluidsynth, GST_TYPE_ELEMENT);

/* fluid_synth log handler */
static void
gst_fluid_synth_error_log_function (int level, char *message, void *data)
{
  GST_ERROR ("%s", message);
}

static void
gst_fluid_synth_warning_log_function (int level, char *message, void *data)
{
  GST_WARNING ("%s", message);
}

static void
gst_fluid_synth_info_log_function (int level, char *message, void *data)
{
  GST_INFO ("%s", message);
}

static void
gst_fluid_synth_debug_log_function (int level, char *message, void *data)
{
  GST_DEBUG ("%s", message);
}


/* initialize the plugin's class */
static void
gst_fluidsynth_class_init (GstFluidsynthClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_fluidsynth_finalize;
  gobject_class->set_property = gst_fluidsynth_set_property;
  gobject_class->get_property = gst_fluidsynth_get_property;

  g_object_class_install_property (gobject_class, PROP_SOUNDFONT,
      g_param_spec_string ("soundfont",
          "Soundfont", "the filename of a soundfont (NULL for default)",
          DEFAULT_SOUNDFONT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SYNTH_CHORUS,
      g_param_spec_boolean ("synth-chorus",
          "Synth Chorus", "Turn the chorus on or off",
          DEFAULT_SYNTH_CHORUS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SYNTH_REVERB,
      g_param_spec_boolean ("synth-reverb",
          "Synth Reverb", "Turn the reverb on or off",
          DEFAULT_SYNTH_REVERB, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SYNTH_GAIN,
      g_param_spec_double ("synth-gain",
          "Synth Gain", "Set the master gain", 0.0, 10.0,
          DEFAULT_SYNTH_GAIN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SYNTH_POLYPHONY,
      g_param_spec_int ("synth-polyphony",
          "Synth Polyphony", "The number of simultaneous voices", 1, 65535,
          DEFAULT_SYNTH_POLYPHONY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "Fluidsynth",
      "Codec/Decoder/Audio",
      "Midi Synthesizer Element", "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_class->change_state = gst_fluidsynth_change_state;

#ifndef GST_DISABLE_GST_DEBUG
  fluid_set_log_function (FLUID_PANIC, gst_fluid_synth_error_log_function,
      NULL);
  fluid_set_log_function (FLUID_ERR, gst_fluid_synth_warning_log_function,
      NULL);
  fluid_set_log_function (FLUID_WARN, gst_fluid_synth_warning_log_function,
      NULL);
  fluid_set_log_function (FLUID_INFO, gst_fluid_synth_info_log_function, NULL);
  fluid_set_log_function (FLUID_DBG, gst_fluid_synth_debug_log_function, NULL);
#else
  fluid_set_log_function (FLUID_PANIC, NULL, NULL);
  fluid_set_log_function (FLUID_ERR, NULL, NULL);
  fluid_set_log_function (FLUID_WARN, NULL, NULL);
  fluid_set_log_function (FLUID_INFO, NULL, NULL);
  fluid_set_log_function (FLUID_DBG, NULL, NULL);
#endif
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_fluidsynth_init (GstFluidsynth * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad, gst_fluidsynth_sink_event);
  gst_pad_set_chain_function (filter->sinkpad, gst_fluidsynth_chain);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->soundfont = g_strdup (DEFAULT_SOUNDFONT);
  filter->synth_chorus = DEFAULT_SYNTH_CHORUS;
  filter->synth_reverb = DEFAULT_SYNTH_REVERB;
  filter->synth_gain = DEFAULT_SYNTH_GAIN;
  filter->synth_polyphony = DEFAULT_SYNTH_POLYPHONY;

  filter->settings = new_fluid_settings ();
  filter->synth = new_fluid_synth (filter->settings);
  filter->sf = -1;

  fluid_synth_set_chorus_on (filter->synth, filter->synth_chorus);
  fluid_synth_set_reverb_on (filter->synth, filter->synth_reverb);
  fluid_synth_set_gain (filter->synth, filter->synth_gain);
  fluid_synth_set_polyphony (filter->synth, filter->synth_polyphony);
}

static void
gst_fluidsynth_finalize (GObject * object)
{
  GstFluidsynth *fluidsynth = GST_FLUIDSYNTH (object);

  delete_fluid_synth (fluidsynth->synth);
  delete_fluid_settings (fluidsynth->settings);
  g_free (fluidsynth->soundfont);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#if 0
static GstBuffer *
gst_fluidsynth_clip_buffer (GstFluidsynth * fluidsynth, GstBuffer * buffer)
{
  guint64 start, stop;
  guint64 new_start, new_stop;
  gint64 offset, length;

  /* clipping disabled for now */
  return buffer;

  start = GST_BUFFER_OFFSET (buffer);
  stop = GST_BUFFER_OFFSET_END (buffer);

  if (!gst_segment_clip (&fluidsynth->segment, GST_FORMAT_DEFAULT,
          start, stop, &new_start, &new_stop)) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  if (start == new_start && stop == new_stop)
    return buffer;

  offset = new_start - start;
  length = new_stop - new_start;

  buffer = gst_buffer_make_writable (buffer);
  gst_buffer_resize (buffer, offset, length);

  GST_BUFFER_OFFSET (buffer) = new_start;
  GST_BUFFER_OFFSET_END (buffer) = new_stop;
  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (new_start, GST_SECOND, FLUIDSYNTH_RATE);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (new_stop, GST_SECOND, FLUIDSYNTH_RATE) -
      GST_BUFFER_TIMESTAMP (buffer);

  return buffer;
}
#endif

static gboolean
gst_fluidsynth_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstFluidsynth *fluidsynth = GST_FLUIDSYNTH (parent);

  GST_DEBUG_OBJECT (pad, "%s event received", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      caps = gst_caps_new_simple ("audio/x-raw",
          "format", G_TYPE_STRING, GST_AUDIO_NE (F32),
          "rate", G_TYPE_INT, FLUIDSYNTH_RATE,
          "channels", G_TYPE_INT, 2,
          "layout", G_TYPE_STRING, "interleaved", NULL);

      fluid_synth_set_sample_rate (fluidsynth->synth, FLUIDSYNTH_RATE);

      res = gst_pad_push_event (fluidsynth->srcpad, gst_event_new_caps (caps));
      gst_caps_unref (caps);
      break;
    }
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &fluidsynth->segment);
      GST_DEBUG_OBJECT (fluidsynth, "configured segment %" GST_SEGMENT_FORMAT,
          fluidsynth->segment);
      res = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_EOS:
      /* FIXME, push last samples */
      res = gst_pad_event_default (pad, parent, event);
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static GstFlowReturn
produce_samples (GstFluidsynth * fluidsynth, GstClockTime pts, guint64 sample)
{
  GstClockTime duration, timestamp;
  guint64 samples, offset;
  GstMapInfo info;
  GstBuffer *outbuf;

  samples = sample - fluidsynth->last_sample;
  duration = pts - fluidsynth->last_pts;
  offset = fluidsynth->last_sample;
  timestamp = fluidsynth->last_pts;

  fluidsynth->last_pts = pts;
  fluidsynth->last_sample = sample;

  if (samples == 0)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (fluidsynth, "duration %" GST_TIME_FORMAT
      ", samples %u", GST_TIME_ARGS (duration), samples);

  outbuf = gst_buffer_new_allocate (NULL, samples * FLUIDSYNTH_BPS, NULL);

  gst_buffer_map (outbuf, &info, GST_MAP_WRITE);
  fluid_synth_write_float (fluidsynth->synth, samples, info.data, 0, 2,
      info.data, 1, 2);
  gst_buffer_unmap (outbuf, &info);

  GST_BUFFER_DTS (outbuf) = timestamp;
  GST_BUFFER_PTS (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;
  GST_BUFFER_OFFSET (outbuf) = offset;
  GST_BUFFER_OFFSET_END (outbuf) = offset + samples;

  return gst_pad_push (fluidsynth->srcpad, outbuf);
}

static void
handle_buffer (GstFluidsynth * fluidsynth, GstBuffer * buffer)
{
  GstMapInfo info;
  guint8 event;

  gst_buffer_map (buffer, &info, GST_MAP_READ);

  event = info.data[0];

  switch (event & 0xf0) {
    case 0xf0:
      switch (event) {
        case 0xff:
          GST_DEBUG_OBJECT (fluidsynth, "system reset");
          fluid_synth_system_reset (fluidsynth->synth);
          break;
        case 0xf0:
        case 0xf7:
          GST_DEBUG_OBJECT (fluidsynth, "sysex 0x%02x", event);
          GST_MEMDUMP_OBJECT (fluidsynth, "bytes ", info.data + 1,
              info.size - 1);
          fluid_synth_sysex (fluidsynth->synth, (char *) info.data + 1,
              info.size - 1, NULL, NULL, NULL, 0);

          break;
        case 0xf9:
          GST_LOG_OBJECT (fluidsynth, "midi tick");
          break;
        default:
          GST_WARNING_OBJECT (fluidsynth, "unhandled event 0x%02x", event);
          break;
      }
      break;
    default:
    {
      guint8 channel, p1, p2;

      channel = event & 0x0f;

      p1 = info.size > 1 ? info.data[1] & 0x7f : 0;
      p2 = info.size > 2 ? info.data[2] & 0x7f : 0;

      GST_DEBUG_OBJECT (fluidsynth, "event 0x%02x channel %d, 0x%02x 0x%02x",
          event, channel, p1, p2);

      switch (event & 0xf0) {
        case 0x80:
          fluid_synth_noteoff (fluidsynth->synth, channel, p1);
          break;
        case 0x90:
          fluid_synth_noteon (fluidsynth->synth, channel, p1, p2);
          break;
        case 0xA0:
          /* aftertouch */
          break;
        case 0xB0:
          fluid_synth_cc (fluidsynth->synth, channel, p1, p2);
          break;
        case 0xC0:
          fluid_synth_program_change (fluidsynth->synth, channel, p1);
          break;
        case 0xD0:
          fluid_synth_channel_pressure (fluidsynth->synth, channel, p1);
          break;
        case 0xE0:
          fluid_synth_pitch_bend (fluidsynth->synth, channel, (p2 << 7) | p1);
          break;
        default:
          break;
      }
      break;
    }
  }
  gst_buffer_unmap (buffer, &info);
}

static GstFlowReturn
gst_fluidsynth_chain (GstPad * sinkpad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstFluidsynth *fluidsynth;
  GstClockTime pts;

  fluidsynth = GST_FLUIDSYNTH (parent);

  pts = GST_BUFFER_PTS (buffer);

  if (pts != GST_CLOCK_TIME_NONE) {
    guint64 sample =
        gst_util_uint64_scale_int (pts, FLUIDSYNTH_RATE, GST_SECOND);

    if (fluidsynth->last_pts == GST_CLOCK_TIME_NONE) {
      fluidsynth->last_pts = pts;
      fluidsynth->last_sample = sample;
    } else if (fluidsynth->last_pts < pts) {
      /* generate samples for the elapsed time */
      res = produce_samples (fluidsynth, pts, sample);
    }
  }

  if (res == GST_FLOW_OK) {
    handle_buffer (fluidsynth, buffer);
  }
  gst_buffer_unref (buffer);

  return res;
}

static gboolean
gst_fluidsynth_open (GstFluidsynth * fluidsynth)
{
  GDir *dir;
  GError *error = NULL;

  if (fluidsynth->sf != -1)
    return TRUE;

  if (fluidsynth->soundfont) {
    GST_DEBUG_OBJECT (fluidsynth, "loading soundfont file %s",
        fluidsynth->soundfont);

    fluidsynth->sf = fluid_synth_sfload (fluidsynth->synth,
        fluidsynth->soundfont, 1);
    if (fluidsynth->sf == -1)
      goto load_failed;

    GST_DEBUG_OBJECT (fluidsynth, "loaded soundfont file %s",
        fluidsynth->soundfont);
  } else {

    dir = g_dir_open (SOUNDFONT_PATH, 0, &error);
    if (dir == NULL)
      goto open_dir_failed;

    while (TRUE) {
      const gchar *name;
      gchar *filename;

      if ((name = g_dir_read_name (dir)) == NULL)
        break;

      filename = g_build_filename (SOUNDFONT_PATH, name, NULL);

      GST_DEBUG_OBJECT (fluidsynth, "loading soundfont file %s", filename);
      fluidsynth->sf = fluid_synth_sfload (fluidsynth->synth, filename, 1);
      if (fluidsynth->sf != -1) {
        GST_DEBUG_OBJECT (fluidsynth, "loaded soundfont file %s", filename);
        break;
      }
      GST_DEBUG_OBJECT (fluidsynth, "could not load soundfont file %s",
          filename);
    }
    g_dir_close (dir);

    if (fluidsynth->sf == -1)
      goto no_soundfont;
  }
  return TRUE;

  /* ERRORS */
load_failed:
  {
    GST_ELEMENT_ERROR (fluidsynth, RESOURCE, OPEN_READ,
        ("Can't open soundfont %s", fluidsynth->soundfont),
        ("failed to open soundfont file %s for reading",
            fluidsynth->soundfont));
    return FALSE;
  }
open_dir_failed:
  {
    GST_ELEMENT_ERROR (fluidsynth, RESOURCE, OPEN_READ,
        ("Can't open directory %s", SOUNDFONT_PATH),
        ("failed to open directory %s for reading: %s", SOUNDFONT_PATH,
            error->message));
    g_error_free (error);
    return FALSE;
  }
no_soundfont:
  {
    GST_ELEMENT_ERROR (fluidsynth, RESOURCE, OPEN_READ,
        ("Can't find soundfont file in directory %s", SOUNDFONT_PATH),
        ("No usable soundfont files found in %s", SOUNDFONT_PATH));
    return FALSE;
  }
}

static gboolean
gst_fluidsynth_close (GstFluidsynth * fluidsynth)
{
  if (fluidsynth->sf) {
    fluid_synth_sfunload (fluidsynth->synth, fluidsynth->sf, 1);
    fluidsynth->sf = -1;
  }
  return TRUE;
}

static GstStateChangeReturn
gst_fluidsynth_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFluidsynth *fluidsynth = GST_FLUIDSYNTH (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_fluidsynth_open (fluidsynth))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_fluidsynth_close (fluidsynth);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_ERROR_OBJECT (fluidsynth, "could not open");
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
gst_fluidsynth_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFluidsynth *fluidsynth = GST_FLUIDSYNTH (object);

  switch (prop_id) {
    case PROP_SOUNDFONT:
      g_free (fluidsynth->soundfont);
      fluidsynth->soundfont = g_value_dup_string (value);
      break;
    case PROP_SYNTH_CHORUS:
      fluidsynth->synth_chorus = g_value_get_boolean (value);
      fluid_synth_set_chorus_on (fluidsynth->synth, fluidsynth->synth_chorus);
      break;
    case PROP_SYNTH_REVERB:
      fluidsynth->synth_reverb = g_value_get_boolean (value);
      fluid_synth_set_reverb_on (fluidsynth->synth, fluidsynth->synth_reverb);
      break;
    case PROP_SYNTH_GAIN:
      fluidsynth->synth_gain = g_value_get_double (value);
      fluid_synth_set_gain (fluidsynth->synth, fluidsynth->synth_gain);
      break;
    case PROP_SYNTH_POLYPHONY:
      fluidsynth->synth_polyphony = g_value_get_int (value);
      fluid_synth_set_polyphony (fluidsynth->synth,
          fluidsynth->synth_polyphony);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fluidsynth_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFluidsynth *fluidsynth = GST_FLUIDSYNTH (object);

  switch (prop_id) {
    case PROP_SOUNDFONT:
      g_value_set_string (value, fluidsynth->soundfont);
      break;
    case PROP_SYNTH_CHORUS:
      g_value_set_boolean (value, fluidsynth->synth_chorus);
      break;
    case PROP_SYNTH_REVERB:
      g_value_set_boolean (value, fluidsynth->synth_reverb);
      break;
    case PROP_SYNTH_GAIN:
      g_value_set_double (value, fluidsynth->synth_gain);
      break;
    case PROP_SYNTH_POLYPHONY:
      g_value_set_int (value, fluidsynth->synth_polyphony);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_fluidsynth_debug, "fluidsynth",
      0, "Fluidsynth plugin");

  return gst_element_register (plugin, "fluidsynth",
      GST_RANK_SECONDARY, GST_TYPE_FLUIDSYNTH);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    fluidsynth,
    "Fluidsynth Plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
