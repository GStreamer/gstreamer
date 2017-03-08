/*
 * gstwildmidi - wildmidi plugin for gstreamer
 *
 * Copyright 2007 Wouter Paesen <wouter@blue-gate.be>
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
 * SECTION:element-wildmidi
 * @title: wildmidi
 * @see_also: timidity
 *
 * This element renders midi-files as audio streams using
 * <ulink url="http://wildmidi.sourceforge.net//">Wildmidi</ulink>.
 * It offers better sound quality compared to the timidity element. Wildmidi
 * uses the same sound-patches as timidity (it tries the path in $WILDMIDI_CFG,
 * $HOME/.wildmidirc and /etc/wildmidi.cfg)
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 filesrc location=song.mid ! wildmidi ! alsasink
 * ]| This example pipeline will parse the midi and render to raw audio which is
 * played via alsa.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define WILDMIDI_RATE 44100
#define WILDMIDI_BPS  (2 * 2)

#include <gst/gst.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gstwildmidi.h"

#ifndef WILDMIDI_CFG
#define WILDMIDI_CFG "/etc/timidity.cfg"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_wildmidi_debug);
#define GST_CAT_DEFAULT gst_wildmidi_debug

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LINEAR_VOLUME,
  PROP_HIGH_QUALITY,
  /* FILL ME */
};

#define DEFAULT_LINEAR_VOLUME    TRUE
#define DEFAULT_HIGH_QUALITY     TRUE

static void gst_wildmidi_finalize (GObject * object);

static gboolean gst_wildmidi_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_wildmidi_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_wildmidi_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_wildmidi_activate (GstPad * pad, GstObject * parent);
static gboolean gst_wildmidi_activatemode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active);

static void gst_wildmidi_loop (GstPad * sinkpad);
static GstFlowReturn gst_wildmidi_chain (GstPad * sinkpad, GstObject * parent,
    GstBuffer * buffer);

static gboolean gst_wildmidi_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void gst_wildmidi_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wildmidi_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/midi; audio/riff-midi")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) S16LE, "
        "rate = (int) 44100, "
        "channels = (int) 2, " "layout = (string) interleaved"));

#define parent_class gst_wildmidi_parent_class
G_DEFINE_TYPE (GstWildmidi, gst_wildmidi, GST_TYPE_ELEMENT);

static gboolean
wildmidi_open_config (void)
{
  gchar *path = g_strdup (g_getenv ("WILDMIDI_CFG"));
  gint ret;

  GST_DEBUG ("trying %s", GST_STR_NULL (path));
  if (path && (g_access (path, R_OK) == -1)) {
    g_free (path);
    path = NULL;
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, g_get_home_dir (), ".wildmidirc",
        NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, G_DIR_SEPARATOR_S "etc",
        "wildmidi.cfg", NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, G_DIR_SEPARATOR_S "etc", "wildmidi",
        "wildmidi.cfg", NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path = g_strdup (WILDMIDI_CFG);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, G_DIR_SEPARATOR_S "etc",
        "timidity.cfg", NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, G_DIR_SEPARATOR_S "etc", "timidity",
        "timidity.cfg", NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    /* I've created a symlink to get it playing
     * ln -s /usr/share/timidity/timidity.cfg /etc/wildmidi.cfg
     * we could make it use : WILDMIDI_CFG
     * but unfortunately it fails to create a proper filename if the config
     * has a redirect
     * http://sourceforge.net/tracker/index.php?func=detail&aid=1657358&group_id=42635&atid=433744
     */
    GST_WARNING ("no config file, can't initialise");
    return FALSE;
  }

  /* this also initializes a some filter and stuff and thus is slow */
  ret = WildMidi_Init (path, WILDMIDI_RATE, 0);
  g_free (path);

  return (ret == 0);
}

/* initialize the plugin's class */
static void
gst_wildmidi_class_init (GstWildmidiClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_wildmidi_finalize;
  gobject_class->set_property = gst_wildmidi_set_property;
  gobject_class->get_property = gst_wildmidi_get_property;

  g_object_class_install_property (gobject_class, PROP_LINEAR_VOLUME,
      g_param_spec_boolean ("linear-volume", "Linear volume",
          "Linear volume", DEFAULT_LINEAR_VOLUME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HIGH_QUALITY,
      g_param_spec_boolean ("high-quality", "High Quality",
          "High Quality", DEFAULT_HIGH_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
  gst_element_class_set_static_metadata (gstelement_class, "WildMidi",
      "Codec/Decoder/Audio",
      "Midi Synthesizer Element", "Wouter Paesen <wouter@blue-gate.be>");

  gstelement_class->change_state = gst_wildmidi_change_state;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_wildmidi_init (GstWildmidi * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");

  gst_pad_set_activatemode_function (filter->sinkpad,
      gst_wildmidi_activatemode);
  gst_pad_set_activate_function (filter->sinkpad, gst_wildmidi_activate);
  gst_pad_set_event_function (filter->sinkpad, gst_wildmidi_sink_event);
  gst_pad_set_chain_function (filter->sinkpad, gst_wildmidi_chain);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

  gst_pad_set_query_function (filter->srcpad, gst_wildmidi_src_query);
  gst_pad_set_event_function (filter->srcpad, gst_wildmidi_src_event);
  gst_pad_use_fixed_caps (filter->srcpad);

  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  gst_segment_init (filter->o_segment, GST_FORMAT_DEFAULT);

  filter->adapter = gst_adapter_new ();

  filter->bytes_per_frame = WILDMIDI_BPS;

  filter->high_quality = DEFAULT_HIGH_QUALITY;
  filter->linear_volume = DEFAULT_LINEAR_VOLUME;
}

static void
gst_wildmidi_finalize (GObject * object)
{
  GstWildmidi *wildmidi;

  wildmidi = GST_WILDMIDI (object);

  g_object_unref (wildmidi->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_wildmidi_src_convert (GstWildmidi * wildmidi,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  gint64 frames;

  if (src_format == *dest_format || src_value == -1) {
    *dest_value = src_value;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_TIME:
      frames = gst_util_uint64_scale_int (src_value, WILDMIDI_RATE, GST_SECOND);
      break;
    case GST_FORMAT_BYTES:
      frames = src_value / (wildmidi->bytes_per_frame);
      break;
    case GST_FORMAT_DEFAULT:
      frames = src_value;
      break;
    default:
      res = FALSE;
      goto done;
  }

  switch (*dest_format) {
    case GST_FORMAT_TIME:
      *dest_value =
          gst_util_uint64_scale_int (frames, GST_SECOND, WILDMIDI_RATE);
      break;
    case GST_FORMAT_BYTES:
      *dest_value = frames * wildmidi->bytes_per_frame;
      break;
    case GST_FORMAT_DEFAULT:
      *dest_value = frames;
      break;
    default:
      res = FALSE;
      break;
  }

done:
  return res;
}

static gboolean
gst_wildmidi_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstWildmidi *wildmidi = GST_WILDMIDI (parent);
  GstFormat src_format, dst_format;
  gint64 src_value, dst_value;

  if (!wildmidi->song)
    return FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      gst_query_set_duration (query, GST_FORMAT_TIME,
          gst_util_uint64_scale_int (wildmidi->o_len, GST_SECOND,
              WILDMIDI_RATE));
      break;
    case GST_QUERY_POSITION:
      gst_query_set_position (query, GST_FORMAT_TIME,
          gst_util_uint64_scale_int (wildmidi->o_segment->position, GST_SECOND,
              WILDMIDI_RATE));
      break;
    case GST_QUERY_CONVERT:
      gst_query_parse_convert (query, &src_format, &src_value,
          &dst_format, NULL);

      res =
          gst_wildmidi_src_convert (wildmidi, src_format, src_value,
          &dst_format, &dst_value);
      if (res)
        gst_query_set_convert (query, src_format, src_value, dst_format,
            dst_value);

      break;
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 3,
          GST_FORMAT_TIME, GST_FORMAT_BYTES, GST_FORMAT_DEFAULT);
      break;
    case GST_QUERY_SEGMENT:{
      GstFormat format;
      gint64 start, stop;

      format = wildmidi->o_segment->format;

      start =
          gst_segment_to_stream_time (wildmidi->o_segment, format,
          wildmidi->o_segment->start);
      if ((stop = wildmidi->o_segment->stop) == -1)
        stop = wildmidi->o_segment->duration;
      else
        stop = gst_segment_to_stream_time (wildmidi->o_segment, format, stop);

      gst_query_set_segment (query, wildmidi->o_segment->rate, format, start,
          stop);
      res = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:
      gst_query_set_seeking (query, wildmidi->o_segment->format,
          TRUE, 0, wildmidi->o_len);
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static GstEvent *
gst_wildmidi_get_new_segment_event (GstWildmidi * wildmidi, GstFormat format)
{
  gint64 start = 0, stop = -1, time = 0;
  GstSegment *segment, newseg;
  GstEvent *event;
  GstFormat src_format;

  segment = wildmidi->o_segment;
  src_format = segment->format;
  newseg = *segment;

  /* convert the segment values to the target format */
  gst_wildmidi_src_convert (wildmidi, src_format, segment->start, &format,
      &start);
  gst_wildmidi_src_convert (wildmidi, src_format, segment->stop, &format,
      &stop);
  gst_wildmidi_src_convert (wildmidi, src_format, segment->time, &format,
      &time);

  newseg.format = format;
  newseg.start = start;
  newseg.stop = stop;
  newseg.time = time;

  event = gst_event_new_segment (&newseg);

  return event;
}

static gboolean
gst_wildmidi_do_seek (GstWildmidi * wildmidi, GstEvent * event)
{
  gdouble rate;
  GstFormat src_format, dst_format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gboolean flush, update;
#ifdef HAVE_WILDMIDI_0_2_2
  gboolean accurate;
#endif
  gboolean res;
  unsigned long int sample;
  GstSegment *segment;

  if (!wildmidi->song)
    return FALSE;

  gst_event_parse_seek (event, &rate, &src_format, &flags,
      &start_type, &start, &stop_type, &stop);

  /* convert the input format to samples */
  dst_format = GST_FORMAT_DEFAULT;
  res = TRUE;
  if (start_type != GST_SEEK_TYPE_NONE) {
    res =
        gst_wildmidi_src_convert (wildmidi, src_format, start, &dst_format,
        &start);
  }
  if (res && stop_type != GST_SEEK_TYPE_NONE) {
    res =
        gst_wildmidi_src_convert (wildmidi, src_format, stop, &dst_format,
        &stop);
  }
  /* unsupported format */
  if (!res)
    return res;

  flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);
#ifdef HAVE_WILDMIDI_0_2_2
  accurate = ((flags & GST_SEEK_FLAG_ACCURATE) == GST_SEEK_FLAG_ACCURATE);
#endif

  if (flush) {
    GST_DEBUG ("performing flush");
    gst_pad_push_event (wildmidi->srcpad, gst_event_new_flush_start ());
  } else {
    gst_pad_stop_task (wildmidi->sinkpad);
  }

  segment = wildmidi->o_segment;

  GST_PAD_STREAM_LOCK (wildmidi->sinkpad);

  if (flush) {
    gst_pad_push_event (wildmidi->srcpad, gst_event_new_flush_stop (TRUE));
  }

  /* update the segment now */
  gst_segment_do_seek (segment, rate, dst_format, flags,
      start_type, start, stop_type, stop, &update);

  /* we need to seek to position in the segment now, sample will be updated */
  sample = segment->position;

  GST_OBJECT_LOCK (wildmidi);
#ifdef HAVE_WILDMIDI_0_2_2
  if (accurate) {
    WildMidi_SampledSeek (wildmidi->song, &sample);
  } else {
    WildMidi_FastSeek (wildmidi->song, &sample);
  }
#else
  WildMidi_FastSeek (wildmidi->song, &sample);
#endif

  GST_OBJECT_UNLOCK (wildmidi);

  segment->start = segment->time = segment->position = sample;

  gst_pad_push_event (wildmidi->srcpad,
      gst_wildmidi_get_new_segment_event (wildmidi, GST_FORMAT_TIME));

  gst_pad_start_task (wildmidi->sinkpad,
      (GstTaskFunction) gst_wildmidi_loop, wildmidi->sinkpad, NULL);

  wildmidi->discont = TRUE;
  GST_PAD_STREAM_UNLOCK (wildmidi->sinkpad);
  GST_DEBUG ("seek done");

  return TRUE;
}

static gboolean
gst_wildmidi_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = FALSE;
  GstWildmidi *wildmidi = GST_WILDMIDI (parent);

  GST_DEBUG_OBJECT (pad, "%s event received", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_wildmidi_do_seek (wildmidi, event);
      break;
    default:
      break;
  }
  gst_event_unref (event);

  return res;
}


static gboolean
gst_wildmidi_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
  gst_query_unref (query);

  if (!pull_mode)
    goto activate_push;

  GST_DEBUG_OBJECT (sinkpad, "activating pull");
  return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);

activate_push:
  {
    GST_DEBUG_OBJECT (sinkpad, "activating push");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }
}

static gboolean
gst_wildmidi_activatemode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      res = TRUE;
      break;
    case GST_PAD_MODE_PULL:
      if (active) {
        res = gst_pad_start_task (pad, (GstTaskFunction) gst_wildmidi_loop,
            pad, NULL);
      } else {
        res = gst_pad_stop_task (pad);
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static GstBuffer *
gst_wildmidi_clip_buffer (GstWildmidi * wildmidi, GstBuffer * buffer)
{
  guint64 start, stop;
  guint64 new_start, new_stop;
  gint64 offset, length;
  guint64 bpf;

  /* clipping disabled for now */
  return buffer;

  start = GST_BUFFER_OFFSET (buffer);
  stop = GST_BUFFER_OFFSET_END (buffer);

  if (!gst_segment_clip (wildmidi->o_segment, GST_FORMAT_DEFAULT,
          start, stop, &new_start, &new_stop)) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  if (start == new_start && stop == new_stop)
    return buffer;


  offset = new_start - start;
  length = new_stop - new_start;

  bpf = wildmidi->bytes_per_frame;
  buffer = gst_buffer_make_writable (buffer);
  gst_buffer_resize (buffer, offset * bpf, length * bpf);

  GST_BUFFER_OFFSET (buffer) = new_start;
  GST_BUFFER_OFFSET_END (buffer) = new_stop;
  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (new_start, GST_SECOND, WILDMIDI_RATE);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (new_stop, GST_SECOND, WILDMIDI_RATE) -
      GST_BUFFER_TIMESTAMP (buffer);

  return buffer;
}

/* generate audio data and advance internal timers */
static GstBuffer *
gst_wildmidi_get_buffer (GstWildmidi * wildmidi)
{
  size_t size;
  gint64 samples;
  GstBuffer *buffer;
  GstSegment *segment;
  GstMapInfo info;
  guint bpf;

  bpf = wildmidi->bytes_per_frame;

  buffer = gst_buffer_new_and_alloc (256 * bpf);

  gst_buffer_map (buffer, &info, GST_MAP_READWRITE);

  GST_OBJECT_LOCK (wildmidi);
  size = WildMidi_GetOutput (wildmidi->song, (gpointer) info.data,
      (unsigned long int) info.size);
  GST_OBJECT_UNLOCK (wildmidi);

  gst_buffer_unmap (buffer, &info);

  if (size == 0) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  /* adjust buffer size */
  gst_buffer_resize (buffer, 0, size);

  segment = wildmidi->o_segment;

  GST_BUFFER_OFFSET (buffer) = segment->position;
  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (segment->position, GST_SECOND, WILDMIDI_RATE);

  samples = size / bpf;
  segment->position += samples;

  GST_BUFFER_OFFSET_END (buffer) = segment->position;
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (segment->position, GST_SECOND,
      WILDMIDI_RATE) - GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG_OBJECT (wildmidi, "buffer ts: %" GST_TIME_FORMAT ", "
      "duration: %" GST_TIME_FORMAT " (%" G_GINT64_FORMAT " samples)",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)), samples);

  return gst_wildmidi_clip_buffer (wildmidi, buffer);
}

static GstFlowReturn
gst_wildmidi_parse_song (GstWildmidi * wildmidi)
{
  struct _WM_Info *info;
  GstCaps *outcaps;
  guint8 *data;
  guint size;

  GST_DEBUG_OBJECT (wildmidi, "Parsing song");

  size = gst_adapter_available (wildmidi->adapter);
  data = gst_adapter_take (wildmidi->adapter, size);

  /* this method takes our memory block */
  GST_OBJECT_LOCK (wildmidi);
  wildmidi->song = WildMidi_OpenBuffer (data, size);

  if (!wildmidi->song)
    goto open_failed;

#ifdef HAVE_WILDMIDI_0_2_2
  WildMidi_LoadSamples (wildmidi->song);
#endif

#ifdef HAVE_WILDMIDI_0_2_2
  WildMidi_SetOption (wildmidi->song, WM_MO_LINEAR_VOLUME,
      wildmidi->linear_volume);
  WildMidi_SetOption (wildmidi->song, WM_MO_EXPENSIVE_INTERPOLATION,
      wildmidi->high_quality);
#else
  WildMidi_SetOption (wildmidi->song, WM_MO_LOG_VOLUME,
      !wildmidi->linear_volume);
  WildMidi_SetOption (wildmidi->song, WM_MO_ENHANCED_RESAMPLING,
      wildmidi->high_quality);
#endif

  info = WildMidi_GetInfo (wildmidi->song);
  GST_OBJECT_UNLOCK (wildmidi);

  wildmidi->o_len = info->approx_total_samples;

  outcaps = gst_caps_copy (gst_pad_get_pad_template_caps (wildmidi->srcpad));
  gst_pad_set_caps (wildmidi->srcpad, outcaps);
  gst_caps_unref (outcaps);

  /* we keep an internal segment in samples */
  gst_segment_init (wildmidi->o_segment, GST_FORMAT_DEFAULT);

  gst_pad_push_event (wildmidi->srcpad,
      gst_wildmidi_get_new_segment_event (wildmidi, GST_FORMAT_TIME));

  GST_DEBUG_OBJECT (wildmidi, "Parsing song done");

  return GST_FLOW_OK;

  /* ERRORS */
open_failed:
  {
    GST_OBJECT_UNLOCK (wildmidi);
    GST_ELEMENT_ERROR (wildmidi, STREAM, DECODE, (NULL),
        ("Unable to parse midi data"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_wildmidi_do_play (GstWildmidi * wildmidi)
{
  GstBuffer *out;
  GstFlowReturn ret;

  if (!(out = gst_wildmidi_get_buffer (wildmidi)))
    goto eos;

  if (wildmidi->discont) {
    GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
    wildmidi->discont = FALSE;
  }

  ret = gst_pad_push (wildmidi->srcpad, out);

  return ret;

  /* ERRORS */
eos:
  {
    GST_LOG_OBJECT (wildmidi, "Song ended");
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_wildmidi_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstWildmidi *wildmidi = GST_WILDMIDI (parent);

  GST_DEBUG_OBJECT (pad, "%s event received", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      wildmidi->state = GST_WILDMIDI_STATE_PARSE;
      /* now start the parsing task */
      res = gst_pad_start_task (wildmidi->sinkpad,
          (GstTaskFunction) gst_wildmidi_loop, wildmidi->sinkpad, NULL);
      /* don't forward the event */
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_wildmidi_chain (GstPad * sinkpad, GstObject * parent, GstBuffer * buffer)
{
  GstWildmidi *wildmidi;

  wildmidi = GST_WILDMIDI (parent);

  /* push stuff in the adapter, we will start doing something in the sink event
   * handler when we get EOS */
  gst_adapter_push (wildmidi->adapter, buffer);

  return GST_FLOW_OK;
}

static void
gst_wildmidi_loop (GstPad * sinkpad)
{
  GstWildmidi *wildmidi = GST_WILDMIDI (GST_PAD_PARENT (sinkpad));
  GstFlowReturn ret;

  switch (wildmidi->state) {
    case GST_WILDMIDI_STATE_LOAD:
    {
      GstBuffer *buffer = NULL;

      GST_DEBUG_OBJECT (wildmidi, "loading song");

      ret =
          gst_pad_pull_range (wildmidi->sinkpad, wildmidi->offset, -1, &buffer);

      if (ret == GST_FLOW_EOS) {
        GST_DEBUG_OBJECT (wildmidi, "Song loaded");
        wildmidi->state = GST_WILDMIDI_STATE_PARSE;
      } else if (ret != GST_FLOW_OK) {
        GST_ELEMENT_ERROR (wildmidi, STREAM, DECODE, (NULL),
            ("Unable to read song"));
        goto pause;
      } else {
        GST_DEBUG_OBJECT (wildmidi, "pushing buffer");
        gst_adapter_push (wildmidi->adapter, buffer);
        wildmidi->offset += gst_buffer_get_size (buffer);
      }
      break;
    }
    case GST_WILDMIDI_STATE_PARSE:
      ret = gst_wildmidi_parse_song (wildmidi);
      if (ret != GST_FLOW_OK)
        goto pause;
      wildmidi->state = GST_WILDMIDI_STATE_PLAY;
      break;
    case GST_WILDMIDI_STATE_PLAY:
      ret = gst_wildmidi_do_play (wildmidi);
      if (ret != GST_FLOW_OK)
        goto pause;
      break;
    default:
      break;
  }
  return;

pause:
  {
    const gchar *reason = gst_flow_get_name (ret);
    GstEvent *event;

    GST_DEBUG_OBJECT (wildmidi, "pausing task, reason %s", reason);
    gst_pad_pause_task (sinkpad);
    if (ret == GST_FLOW_EOS) {
      /* perform EOS logic */
      event = gst_event_new_eos ();
      gst_pad_push_event (wildmidi->srcpad, event);
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      event = gst_event_new_eos ();
      /* for fatal errors we post an error message, post the error
       * first so the app knows about the error first. */
      GST_ELEMENT_FLOW_ERROR (wildmidi, ret);
      gst_pad_push_event (wildmidi->srcpad, event);
    }
  }
}

static GstStateChangeReturn
gst_wildmidi_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWildmidi *wildmidi = GST_WILDMIDI (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      wildmidi->offset = 0;
      wildmidi->state = GST_WILDMIDI_STATE_LOAD;
      wildmidi->discont = FALSE;
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
      GST_OBJECT_LOCK (wildmidi);
      if (wildmidi->song)
        WildMidi_Close (wildmidi->song);
      wildmidi->song = NULL;
      GST_OBJECT_UNLOCK (wildmidi);
      gst_adapter_clear (wildmidi->adapter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_wildmidi_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWildmidi *wildmidi;

  g_return_if_fail (GST_IS_WILDMIDI (object));

  wildmidi = GST_WILDMIDI (object);

  switch (prop_id) {
    case PROP_LINEAR_VOLUME:
      GST_OBJECT_LOCK (object);
      wildmidi->linear_volume = g_value_get_boolean (value);
      if (wildmidi->song)
#ifdef HAVE_WILDMIDI_0_2_2
        WildMidi_SetOption (wildmidi->song, WM_MO_LINEAR_VOLUME,
            wildmidi->linear_volume);
#else
        WildMidi_SetOption (wildmidi->song, WM_MO_LOG_VOLUME,
            !wildmidi->linear_volume);
#endif
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_HIGH_QUALITY:
      GST_OBJECT_LOCK (object);
      wildmidi->high_quality = g_value_get_boolean (value);
      if (wildmidi->song)
#ifdef HAVE_WILDMIDI_0_2_2
        WildMidi_SetOption (wildmidi->song, WM_MO_EXPENSIVE_INTERPOLATION,
            wildmidi->high_quality);
#else
        WildMidi_SetOption (wildmidi->song, WM_MO_ENHANCED_RESAMPLING,
            wildmidi->high_quality);
#endif
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wildmidi_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWildmidi *wildmidi;

  g_return_if_fail (GST_IS_WILDMIDI (object));

  wildmidi = GST_WILDMIDI (object);

  switch (prop_id) {
    case PROP_LINEAR_VOLUME:
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, wildmidi->linear_volume);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_HIGH_QUALITY:
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, wildmidi->high_quality);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_wildmidi_debug, "wildmidi",
      0, "Wildmidi plugin");

  if (!wildmidi_open_config ()) {
    GST_WARNING ("Can't initialize wildmidi");
    return FALSE;
  }

  return gst_element_register (plugin, "wildmidi",
      GST_RANK_SECONDARY, GST_TYPE_WILDMIDI);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    wildmidi,
    "Wildmidi Plugin",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
