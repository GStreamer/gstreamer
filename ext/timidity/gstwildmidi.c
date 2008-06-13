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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-wildmidi
 * @see_also: timidity
 *
 * This element renders midi-files as audio streams using
 * <ulink url="http://wildmidi.sourceforge.net//">Wildmidi</ulink>.
 * It offers better sound quality compared to the timidity element. Wildmidi
 * uses the same sound-patches as timidity (it tries the path in $WILDMIDI_CFG,
 * $HOME/.wildmidirc and /etc/wildmidi.cfg)
 * 
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch filesrc location=song.mid ! wildmidi ! alsasink
 * ]| This example pipeline will parse the midi and render to raw audio which is
 * played via alsa.
 * </refsect2>
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

static const GstElementDetails gst_wildmidi_details =
GST_ELEMENT_DETAILS ("WildMidi",
    "Codec/Decoder/Audio",
    "Midi Synthesizer Element",
    "Wouter Paesen <wouter@blue-gate.be>");

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LINEAR_VOLUME,
  ARG_HIGH_QUALITY,
  /* FILL ME */
};

static void gst_wildmidi_base_init (gpointer g_class);

static void gst_wildmidi_class_init (GstWildmidiClass * klass);

static gboolean gst_wildmidi_src_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn gst_wildmidi_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_wildmidi_activate (GstPad * pad);

static gboolean gst_wildmidi_activatepull (GstPad * pad, gboolean active);

static void gst_wildmidi_loop (GstPad * sinkpad);

static gboolean gst_wildmidi_src_query (GstPad * pad, GstQuery * query);

static void gst_wildmidi_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wildmidi_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/midi")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) 44100, "
        "channels = (int) 2, "
        "endianness = (int) LITTLE_ENDIAN, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true"));

GST_BOILERPLATE (GstWildmidi, gst_wildmidi, GstElement, GST_TYPE_ELEMENT);

static void
gst_wildmidi_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &gst_wildmidi_details);
}

static gboolean
wildmidi_open_config ()
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
    path = g_build_path (G_DIR_SEPARATOR_S, "/etc", "wildmidi.cfg", NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, "/etc", "wildmidi", "wildmidi.cfg",
        NULL);
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
    path = g_build_path (G_DIR_SEPARATOR_S, "/etc", "timidity.cfg", NULL);
    GST_DEBUG ("trying %s", path);
    if (path && (g_access (path, R_OK) == -1)) {
      g_free (path);
      path = NULL;
    }
  }

  if (path == NULL) {
    path =
        g_build_path (G_DIR_SEPARATOR_S, "/etc", "timidity", "timidity.cfg",
        NULL);
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

  gstelement_class->change_state = gst_wildmidi_change_state;
  gobject_class->set_property = gst_wildmidi_set_property;
  gobject_class->get_property = gst_wildmidi_get_property;

  g_object_class_install_property (gobject_class, ARG_LINEAR_VOLUME,
      g_param_spec_boolean ("linear-volume", "Linear volume",
          "Linear volume", TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, ARG_HIGH_QUALITY,
      g_param_spec_boolean ("high-quality", "High Quality",
          "High Quality", TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_wildmidi_init (GstWildmidi * filter, GstWildmidiClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  gst_pad_set_activatepull_function (filter->sinkpad,
      gst_wildmidi_activatepull);
  gst_pad_set_activate_function (filter->sinkpad, gst_wildmidi_activate);
  gst_pad_set_setcaps_function (filter->sinkpad, gst_pad_set_caps);
  gst_pad_use_fixed_caps (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");

  gst_pad_set_query_function (filter->srcpad, gst_wildmidi_src_query);
  gst_pad_set_event_function (filter->srcpad, gst_wildmidi_src_event);
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_pad_set_setcaps_function (filter->srcpad, gst_pad_set_caps);

  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  gst_segment_init (filter->o_segment, GST_FORMAT_DEFAULT);

  filter->bytes_per_frame = WILDMIDI_BPS;
  filter->time_per_frame = GST_SECOND / WILDMIDI_RATE;
}

static gboolean
gst_wildmidi_src_convert (GstWildmidi * wildmidi,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  gint64 frames;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_TIME:
      frames = src_value / wildmidi->time_per_frame;
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
      *dest_value = frames * wildmidi->time_per_frame;
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
gst_wildmidi_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;

  GstWildmidi *wildmidi = GST_WILDMIDI (gst_pad_get_parent (pad));

  GstFormat src_format, dst_format;

  gint64 src_value, dst_value;

  if (!wildmidi->song) {
    gst_object_unref (wildmidi);
    return FALSE;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      gst_query_set_duration (query, GST_FORMAT_TIME,
          wildmidi->o_len * wildmidi->time_per_frame);
      break;
    case GST_QUERY_POSITION:
      gst_query_set_position (query, GST_FORMAT_TIME,
          wildmidi->o_segment->last_stop * wildmidi->time_per_frame);
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
    case GST_QUERY_SEGMENT:
      gst_query_set_segment (query, wildmidi->o_segment->rate,
          wildmidi->o_segment->format, wildmidi->o_segment->start,
          wildmidi->o_segment->stop);
      break;
    case GST_QUERY_SEEKING:
      gst_query_set_seeking (query, wildmidi->o_segment->format,
          TRUE, 0, wildmidi->o_len);
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (wildmidi);
  return res;
}

static gboolean
gst_wildmidi_get_upstream_size (GstWildmidi * wildmidi, gint64 * size)
{
  GstFormat format = GST_FORMAT_BYTES;

  gboolean res = FALSE;

  GstPad *peer = gst_pad_get_peer (wildmidi->sinkpad);

  if (peer != NULL)
    res = gst_pad_query_duration (peer, &format, size) && *size >= 0;

  gst_object_unref (peer);
  return res;
}

static GstSegment *
gst_wildmidi_get_segment (GstWildmidi * wildmidi, GstFormat format,
    gboolean update)
{
  gint64 start, stop, time;

  GstSegment *segment = gst_segment_new ();

  gst_wildmidi_src_convert (wildmidi,
      wildmidi->o_segment->format, wildmidi->o_segment->start, &format, &start);

  if (wildmidi->o_segment->stop == GST_CLOCK_TIME_NONE) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    gst_wildmidi_src_convert (wildmidi,
        wildmidi->o_segment->format, wildmidi->o_segment->stop, &format, &stop);
  }

  gst_wildmidi_src_convert (wildmidi,
      wildmidi->o_segment->format, wildmidi->o_segment->time, &format, &time);

  gst_segment_set_newsegment_full (segment, update,
      wildmidi->o_segment->rate, wildmidi->o_segment->applied_rate,
      format, start, stop, time);

  segment->last_stop = time;

  return segment;
}

static GstEvent *
gst_wildmidi_get_new_segment_event (GstWildmidi * wildmidi, GstFormat format,
    gboolean update)
{
  GstSegment *segment;

  GstEvent *event;

  segment = gst_wildmidi_get_segment (wildmidi, format, update);

  event = gst_event_new_new_segment_full (update,
      segment->rate, segment->applied_rate, segment->format,
      segment->start, segment->stop, segment->time);

  gst_segment_free (segment);

  return event;
}

static gboolean
gst_wildmidi_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;

  GstWildmidi *wildmidi = GST_WILDMIDI (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "%s event received", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;

      GstFormat src_format, dst_format;

      GstSeekFlags flags;

      GstSeekType start_type, stop_type;

      gint64 orig_start, start, stop;

      gboolean flush, update;

      if (!wildmidi->song)
        break;

      gst_event_parse_seek (event, &rate, &src_format, &flags,
          &start_type, &orig_start, &stop_type, &stop);

      dst_format = GST_FORMAT_DEFAULT;

      gst_wildmidi_src_convert (wildmidi, src_format, orig_start,
          &dst_format, &start);
      gst_wildmidi_src_convert (wildmidi, src_format, stop, &dst_format, &stop);

      flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

      if (flush) {
        GST_DEBUG ("performing flush");
        gst_pad_push_event (wildmidi->srcpad, gst_event_new_flush_start ());
      } else {
        gst_pad_stop_task (wildmidi->sinkpad);
      }

      GST_PAD_STREAM_LOCK (wildmidi->sinkpad);

      if (flush) {
        gst_pad_push_event (wildmidi->srcpad, gst_event_new_flush_stop ());
      }

      gst_segment_set_seek (wildmidi->o_segment, rate, dst_format, flags,
          start_type, start, stop_type, stop, &update);

      if ((flags && GST_SEEK_FLAG_SEGMENT) == GST_SEEK_FLAG_SEGMENT) {
        GST_DEBUG_OBJECT (wildmidi, "received segment seek %d, %d",
            (gint) start_type, (gint) stop_type);
      } else {
        GST_DEBUG_OBJECT (wildmidi, "received normal seek %d",
            (gint) start_type);
        update = FALSE;
      }

      gst_pad_push_event (wildmidi->srcpad,
          gst_wildmidi_get_new_segment_event (wildmidi, GST_FORMAT_TIME,
              update));

      wildmidi->o_seek = TRUE;

      gst_pad_start_task (wildmidi->sinkpad,
          (GstTaskFunction) gst_wildmidi_loop, wildmidi->sinkpad);

      GST_PAD_STREAM_UNLOCK (wildmidi->sinkpad);
      GST_DEBUG ("seek done");
    }
      res = TRUE;
      break;
    default:
      break;
  }

  g_object_unref (wildmidi);
  return res;
}

static gboolean
gst_wildmidi_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
gst_wildmidi_activatepull (GstPad * pad, gboolean active)
{
  if (active) {
    return gst_pad_start_task (pad, (GstTaskFunction) gst_wildmidi_loop, pad);
  } else {
    return gst_pad_stop_task (pad);
  }
}

static GstBuffer *
gst_wildmidi_allocate_buffer (GstWildmidi * wildmidi, gint64 samples)
{
  return gst_buffer_new_and_alloc (samples * wildmidi->bytes_per_frame);
}

static GstBuffer *
gst_wildmidi_clip_buffer (GstWildmidi * wildmidi, GstBuffer * buffer)
{
  gint64 new_start, new_stop;

  gint64 offset, length;

  GstBuffer *out;

  return buffer;

  if (!gst_segment_clip (wildmidi->o_segment, GST_FORMAT_DEFAULT,
          GST_BUFFER_OFFSET (buffer), GST_BUFFER_OFFSET_END (buffer),
          &new_start, &new_stop)) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  if (GST_BUFFER_OFFSET (buffer) == new_start &&
      GST_BUFFER_OFFSET_END (buffer) == new_stop)
    return buffer;

  offset = new_start - GST_BUFFER_OFFSET (buffer);
  length = new_stop - new_start;

  out = gst_buffer_create_sub (buffer, offset * wildmidi->bytes_per_frame,
      length * wildmidi->bytes_per_frame);

  GST_BUFFER_OFFSET (out) = new_start;
  GST_BUFFER_OFFSET_END (out) = new_stop;
  GST_BUFFER_TIMESTAMP (out) = new_start * wildmidi->time_per_frame;
  GST_BUFFER_DURATION (out) = (new_stop - new_start) * wildmidi->time_per_frame;

  gst_buffer_unref (buffer);

  return out;
}

/* generate audio data and advance internal timers */
static GstBuffer *
gst_wildmidi_fill_buffer (GstWildmidi * wildmidi, GstBuffer * buffer)
{
  size_t bytes_read;

  gint64 samples;

  bytes_read =
      WildMidi_GetOutput (wildmidi->song, (char *) GST_BUFFER_DATA (buffer),
      (unsigned long int) GST_BUFFER_SIZE (buffer));

  if (bytes_read == 0) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  GST_BUFFER_OFFSET (buffer) =
      wildmidi->o_segment->last_stop * wildmidi->bytes_per_frame;
  GST_BUFFER_TIMESTAMP (buffer) =
      wildmidi->o_segment->last_stop * wildmidi->time_per_frame;

  if (bytes_read < GST_BUFFER_SIZE (buffer)) {
    GstBuffer *old = buffer;

    buffer = gst_buffer_create_sub (buffer, 0, bytes_read);
    gst_buffer_unref (old);
  }

  samples = GST_BUFFER_SIZE (buffer) / wildmidi->bytes_per_frame;

  wildmidi->o_segment->last_stop += samples;

  GST_BUFFER_OFFSET_END (buffer) =
      wildmidi->o_segment->last_stop * wildmidi->bytes_per_frame;
  GST_BUFFER_DURATION (buffer) = samples * wildmidi->time_per_frame;

  GST_DEBUG_OBJECT (wildmidi,
      "generated buffer %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT
      " (%d samples)",
      GST_TIME_ARGS ((guint64) GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (((guint64) (GST_BUFFER_TIMESTAMP (buffer) +
                  GST_BUFFER_DURATION (buffer)))), samples);

  return buffer;
}

static GstBuffer *
gst_wildmidi_get_buffer (GstWildmidi * wildmidi)
{
  GstBuffer *out;

  out =
      gst_wildmidi_fill_buffer (wildmidi,
      gst_wildmidi_allocate_buffer (wildmidi, 256));

  if (!out)
    return NULL;

  return gst_wildmidi_clip_buffer (wildmidi, out);
}

static void
gst_wildmidi_loop (GstPad * sinkpad)
{
  GstWildmidi *wildmidi = GST_WILDMIDI (GST_PAD_PARENT (sinkpad));

  GstBuffer *out;

  GstFlowReturn ret;

  if (wildmidi->mididata_size == 0) {
    if (!gst_wildmidi_get_upstream_size (wildmidi, &wildmidi->mididata_size)) {
      GST_ELEMENT_ERROR (wildmidi, STREAM, DECODE, (NULL),
          ("Unable to get song length"));
      goto paused;
    }

    if (wildmidi->mididata)
      free (wildmidi->mididata);

    wildmidi->mididata = malloc (wildmidi->mididata_size);
    wildmidi->mididata_offset = 0;
    return;
  }

  if (wildmidi->mididata_offset < wildmidi->mididata_size) {
    GstBuffer *buffer;

    gint64 size;

    GST_DEBUG_OBJECT (wildmidi, "loading song");

    ret =
        gst_pad_pull_range (wildmidi->sinkpad, wildmidi->mididata_offset,
        -1, &buffer);
    if (ret != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (wildmidi, STREAM, DECODE, (NULL),
          ("Unable to load song"));
      goto paused;
    }

    size = wildmidi->mididata_size - wildmidi->mididata_offset;
    if (GST_BUFFER_SIZE (buffer) < size)
      size = GST_BUFFER_SIZE (buffer);

    memmove (wildmidi->mididata + wildmidi->mididata_offset,
        GST_BUFFER_DATA (buffer), size);
    gst_buffer_unref (buffer);

    wildmidi->mididata_offset += size;
    GST_DEBUG_OBJECT (wildmidi, "Song loaded");
    return;
  }

  if (!wildmidi->song) {
    struct _WM_Info *info;

    GST_DEBUG_OBJECT (wildmidi, "Parsing song");

    /* this method takes our memory block */
    wildmidi->song =
        WildMidi_OpenBuffer ((unsigned char *) wildmidi->mididata,
        wildmidi->mididata_size);
    wildmidi->mididata_size = 0;
    wildmidi->mididata = NULL;

    if (!wildmidi->song) {
      GST_ELEMENT_ERROR (wildmidi, STREAM, DECODE, (NULL),
          ("Unable to parse midi"));
      goto paused;
    }

    WildMidi_LoadSamples (wildmidi->song);

    WildMidi_SetOption (wildmidi->song, WM_MO_LINEAR_VOLUME,
        wildmidi->linear_volume);
    WildMidi_SetOption (wildmidi->song, WM_MO_EXPENSIVE_INTERPOLATION,
        wildmidi->high_quality);

    info = WildMidi_GetInfo (wildmidi->song);
    wildmidi->o_len = info->approx_total_samples;

    gst_segment_set_newsegment (wildmidi->o_segment, FALSE, 1.0,
        GST_FORMAT_DEFAULT, 0, GST_CLOCK_TIME_NONE, 0);

    gst_pad_push_event (wildmidi->srcpad,
        gst_wildmidi_get_new_segment_event (wildmidi, GST_FORMAT_TIME, FALSE));

    GST_DEBUG_OBJECT (wildmidi, "Parsing song done");
    return;
  }

  if (wildmidi->o_segment_changed) {
    GST_DEBUG_OBJECT (wildmidi, "segment changed");

    GstSegment *segment = gst_wildmidi_get_segment (wildmidi, GST_FORMAT_TIME,
        !wildmidi->o_new_segment);

    GST_LOG_OBJECT (wildmidi,
        "sending newsegment from %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT
        ", pos=%" GST_TIME_FORMAT, GST_TIME_ARGS ((guint64) segment->start),
        GST_TIME_ARGS ((guint64) segment->stop),
        GST_TIME_ARGS ((guint64) segment->time));

    if (wildmidi->o_segment->flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT (wildmidi),
          gst_message_new_segment_start (GST_OBJECT (wildmidi),
              segment->format, segment->start));
    }

    gst_segment_free (segment);
    wildmidi->o_segment_changed = FALSE;
    return;
  }

  if (wildmidi->o_seek) {
    unsigned long int sample;

    /* perform a seek internally */
    sample = wildmidi->o_segment->time;

    if (wildmidi->accurate_seek) {
      WildMidi_SampledSeek (wildmidi->song, &sample);
    } else {
      WildMidi_FastSeek (wildmidi->song, &sample);
    }

    wildmidi->o_segment->last_stop = wildmidi->o_segment->time = sample;
  }

  out = gst_wildmidi_get_buffer (wildmidi);
  if (!out) {
    GST_LOG_OBJECT (wildmidi, "Song ended, generating eos");
    gst_pad_push_event (wildmidi->srcpad, gst_event_new_eos ());
    wildmidi->o_seek = FALSE;
    goto paused;
  }

  if (wildmidi->o_seek) {
    GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
    wildmidi->o_seek = FALSE;
  }

  gst_buffer_set_caps (out, wildmidi->out_caps);
  ret = gst_pad_push (wildmidi->srcpad, out);

  if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED)
    goto error;

  return;

paused:
  {
    GST_DEBUG_OBJECT (wildmidi, "pausing task");
    gst_pad_pause_task (wildmidi->sinkpad);
    return;
  }
error:
  {
    GST_ELEMENT_ERROR (wildmidi, STREAM, FAILED,
        ("Internal data stream error"),
        ("Streaming stopped, reason %s", gst_flow_get_name (ret)));
    gst_pad_push_event (wildmidi->srcpad, gst_event_new_eos ());
    goto paused;
  }
}

static GstStateChangeReturn
gst_wildmidi_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GstWildmidi *wildmidi = GST_WILDMIDI (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      wildmidi->out_caps =
          gst_caps_copy (gst_pad_get_pad_template_caps (wildmidi->srcpad));
      wildmidi->mididata = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      wildmidi->mididata_size = 0;
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
      if (wildmidi->song)
        WildMidi_Close (wildmidi->song);
      wildmidi->song = NULL;
      if (wildmidi->mididata)
        free (wildmidi->mididata);
      wildmidi->mididata = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_caps_unref (wildmidi->out_caps);
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
    case ARG_LINEAR_VOLUME:
      wildmidi->linear_volume = g_value_get_boolean (value);
      if (wildmidi->song)
        WildMidi_SetOption (wildmidi->song, WM_MO_LINEAR_VOLUME,
            wildmidi->linear_volume);
      break;
    case ARG_HIGH_QUALITY:
      wildmidi->high_quality = g_value_get_boolean (value);
      if (wildmidi->song)
        WildMidi_SetOption (wildmidi->song, WM_MO_EXPENSIVE_INTERPOLATION,
            wildmidi->high_quality);
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
    case ARG_LINEAR_VOLUME:
      g_value_set_boolean (value, wildmidi->linear_volume);
      break;
    case ARG_HIGH_QUALITY:
      g_value_set_boolean (value, wildmidi->high_quality);
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
    "wildmidi",
    "Wildmidi Plugin",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
