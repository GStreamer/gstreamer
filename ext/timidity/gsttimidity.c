/*
 * gsttimdity - timidity plugin for gstreamer
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
 * SECTION:element-timidity
 * @see_also: wildmidi
 *
 * This element renders midi-files as audio streams using
 * <ulink url="http://timidity.sourceforge.net/">Timidity</ulink>.
 * 
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch filesrc location=song.mid ! timidity ! alsasink
 * ]| This example pipeline will parse the midi and render to raw audio which is
 * played via alsa.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "gsttimidity.h"

#ifndef TIMIDITY_CFG
#define TIMIDITY_CFG "/etc/timidity.cfg"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_timidity_debug);
#define GST_CAT_DEFAULT gst_timidity_debug

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static gboolean gst_timidity_src_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_timidity_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_timidity_activate (GstPad * pad);
static gboolean gst_timidity_activatepull (GstPad * pad, gboolean active);
static void gst_timidity_loop (GstPad * sinkpad);
static gboolean gst_timidity_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_timidity_set_song_options (GstTimidity * timidity,
    MidSongOptions * options);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/midi; audio/riff-midi")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) 44100, "
        "channels = (int) 2, "
        "endianness = (int) LITTLE_ENDIAN, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true"));

GST_BOILERPLATE (GstTimidity, gst_timidity, GstElement, GST_TYPE_ELEMENT);

static void
gst_timidity_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_static_metadata (element_class, "Timidity",
      "Codec/Decoder/Audio",
      "Midi Synthesizer Element", "Wouter Paesen <wouter@blue-gate.be>");
}

/* initialize the plugin's class */
static void
gst_timidity_class_init (GstTimidityClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;
  gstelement_class->change_state = gst_timidity_change_state;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_timidity_init (GstTimidity * filter, GstTimidityClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  /* initialise timidity library */
  if (mid_init ((char *) TIMIDITY_CFG) == 0) {
    filter->initialized = TRUE;
  } else {
    GST_WARNING ("can't initialize timidity with config: " TIMIDITY_CFG);
  }

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  gst_pad_set_activatepull_function (filter->sinkpad,
      gst_timidity_activatepull);
  gst_pad_set_activate_function (filter->sinkpad, gst_timidity_activate);
  gst_pad_set_setcaps_function (filter->sinkpad, gst_pad_set_caps);
  gst_pad_use_fixed_caps (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");

  gst_pad_set_query_function (filter->srcpad, gst_timidity_src_query);
  gst_pad_set_event_function (filter->srcpad, gst_timidity_src_event);
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_pad_set_setcaps_function (filter->srcpad, gst_pad_set_caps);

  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->song_options->buffer_size = 2048;
  filter->song_options->rate = 44100;
  filter->song_options->format = MID_AUDIO_S16LSB;
  filter->song_options->channels = 2;

  gst_timidity_set_song_options (filter, filter->song_options);

  gst_segment_init (filter->o_segment, GST_FORMAT_DEFAULT);
}

static gboolean
gst_timidity_set_song_options (GstTimidity * timidity, MidSongOptions * options)
{
  gint64 bps;

  switch (options->format) {
    case MID_AUDIO_U8:
    case MID_AUDIO_S8:
      bps = 1;
      break;
    case MID_AUDIO_U16LSB:
    case MID_AUDIO_S16LSB:
    case MID_AUDIO_U16MSB:
    case MID_AUDIO_S16MSB:
      bps = 2;
      break;
    default:
      return FALSE;
  }

  bps *= options->channels;

  if (options != timidity->song_options)
    memcpy (timidity->song_options, options, sizeof (MidSongOptions));

  timidity->bytes_per_frame = bps;
  timidity->time_per_frame = GST_SECOND / (GstClockTime) options->rate;

  return TRUE;
}

static gboolean
gst_timidity_src_convert (GstTimidity * timidity,
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
      frames = src_value / timidity->time_per_frame;
      break;
    case GST_FORMAT_BYTES:
      frames = src_value / (timidity->bytes_per_frame);
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
      *dest_value = frames * timidity->time_per_frame;
      break;
    case GST_FORMAT_BYTES:
      *dest_value = frames * timidity->bytes_per_frame;
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
gst_timidity_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstTimidity *timidity = GST_TIMIDITY (gst_pad_get_parent (pad));
  GstFormat src_format, dst_format;
  gint64 src_value, dst_value;

  if (!timidity->song) {
    gst_object_unref (timidity);
    return FALSE;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      gst_query_set_duration (query, GST_FORMAT_TIME,
          GST_MSECOND * (gint64) mid_song_get_total_time (timidity->song));
      break;
    case GST_QUERY_POSITION:
      gst_query_set_position (query, GST_FORMAT_TIME,
          timidity->o_segment->last_stop * timidity->time_per_frame);
      break;
    case GST_QUERY_CONVERT:
      gst_query_parse_convert (query, &src_format, &src_value,
          &dst_format, NULL);

      res =
          gst_timidity_src_convert (timidity, src_format, src_value,
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

      format = timidity->o_segment->format;

      start =
          gst_segment_to_stream_time (timidity->o_segment, format,
          timidity->o_segment->start);
      if ((stop = timidity->o_segment->stop) == -1)
        stop = timidity->o_segment->duration;
      else
        stop = gst_segment_to_stream_time (timidity->o_segment, format, stop);

      gst_query_set_segment (query, timidity->o_segment->rate, format, start,
          stop);
      res = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:
      gst_query_set_seeking (query, timidity->o_segment->format,
          TRUE, 0, timidity->o_len);
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (timidity);
  return res;
}

static gboolean
gst_timidity_get_upstream_size (GstTimidity * timidity, gint64 * size)
{
  GstFormat format = GST_FORMAT_BYTES;
  gboolean res = FALSE;
  GstPad *peer = gst_pad_get_peer (timidity->sinkpad);

  if (peer != NULL)
    res = gst_pad_query_duration (peer, &format, size) && *size >= 0;

  gst_object_unref (peer);
  return res;
}

static GstSegment *
gst_timidity_get_segment (GstTimidity * timidity, GstFormat format,
    gboolean update)
{
  gint64 start = 0, stop = 0, time = 0;

  GstSegment *segment = gst_segment_new ();

  gst_timidity_src_convert (timidity,
      timidity->o_segment->format, timidity->o_segment->start, &format, &start);

  if (timidity->o_segment->stop == GST_CLOCK_TIME_NONE) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    gst_timidity_src_convert (timidity,
        timidity->o_segment->format, timidity->o_segment->stop, &format, &stop);
  }

  gst_timidity_src_convert (timidity,
      timidity->o_segment->format, timidity->o_segment->time, &format, &time);

  gst_segment_set_newsegment_full (segment, update,
      timidity->o_segment->rate, timidity->o_segment->applied_rate,
      format, start, stop, time);

  segment->last_stop = time;

  return segment;
}

static GstEvent *
gst_timidity_get_new_segment_event (GstTimidity * timidity, GstFormat format,
    gboolean update)
{
  GstSegment *segment;
  GstEvent *event;

  segment = gst_timidity_get_segment (timidity, format, update);

  event = gst_event_new_new_segment_full (update,
      segment->rate, segment->applied_rate, segment->format,
      segment->start, segment->stop, segment->time);

  gst_segment_free (segment);

  return event;
}

static gboolean
gst_timidity_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstTimidity *timidity = GST_TIMIDITY (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "%s event received", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat src_format, dst_format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 orig_start, start = 0, stop = 0;
      gboolean flush, update;

      if (!timidity->song)
        break;

      gst_event_parse_seek (event, &rate, &src_format, &flags,
          &start_type, &orig_start, &stop_type, &stop);

      dst_format = GST_FORMAT_DEFAULT;

      gst_timidity_src_convert (timidity, src_format, orig_start,
          &dst_format, &start);
      gst_timidity_src_convert (timidity, src_format, stop, &dst_format, &stop);

      flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

      if (flush) {
        GST_DEBUG ("performing flush");
        gst_pad_push_event (timidity->srcpad, gst_event_new_flush_start ());
      } else {
        gst_pad_stop_task (timidity->sinkpad);
      }

      GST_PAD_STREAM_LOCK (timidity->sinkpad);

      if (flush) {
        gst_pad_push_event (timidity->srcpad, gst_event_new_flush_stop ());
      }

      gst_segment_set_seek (timidity->o_segment, rate, dst_format, flags,
          start_type, start, stop_type, stop, &update);

      if (flags & GST_SEEK_FLAG_SEGMENT) {
        GST_DEBUG_OBJECT (timidity, "received segment seek %d, %d",
            (gint) start_type, (gint) stop_type);
      } else {
        GST_DEBUG_OBJECT (timidity, "received normal seek %d",
            (gint) start_type);
        update = FALSE;
      }

      gst_pad_push_event (timidity->srcpad,
          gst_timidity_get_new_segment_event (timidity, GST_FORMAT_TIME,
              update));

      timidity->o_seek = TRUE;

      gst_pad_start_task (timidity->sinkpad,
          (GstTaskFunction) gst_timidity_loop, timidity->sinkpad, NULL);

      GST_PAD_STREAM_UNLOCK (timidity->sinkpad);
      GST_DEBUG ("seek done");
    }
      res = TRUE;
      break;
    default:
      break;
  }

  g_object_unref (timidity);
  return res;
}

static gboolean
gst_timidity_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
gst_timidity_activatepull (GstPad * pad, gboolean active)
{
  if (active) {
    return gst_pad_start_task (pad, (GstTaskFunction) gst_timidity_loop, pad,
        NULL);
  } else {
    return gst_pad_stop_task (pad);
  }
}

static GstBuffer *
gst_timidity_allocate_buffer (GstTimidity * timidity, gint64 samples)
{
  return gst_buffer_new_and_alloc (samples * timidity->bytes_per_frame);
}

static GstBuffer *
gst_timidity_clip_buffer (GstTimidity * timidity, GstBuffer * buffer)
{
  gint64 new_start, new_stop;
  gint64 offset, length;
  GstBuffer *out;

  return buffer;

  if (!gst_segment_clip (timidity->o_segment, GST_FORMAT_DEFAULT,
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

  out = gst_buffer_create_sub (buffer, offset * timidity->bytes_per_frame,
      length * timidity->bytes_per_frame);

  GST_BUFFER_OFFSET (out) = new_start;
  GST_BUFFER_OFFSET_END (out) = new_stop;
  GST_BUFFER_TIMESTAMP (out) = new_start * timidity->time_per_frame;
  GST_BUFFER_DURATION (out) = (new_stop - new_start) * timidity->time_per_frame;

  gst_buffer_unref (buffer);

  return out;
}

/* generate audio data and advance internal timers */
static GstBuffer *
gst_timidity_fill_buffer (GstTimidity * timidity, GstBuffer * buffer)
{
  size_t bytes_read;
  gint64 samples;

  bytes_read = mid_song_read_wave (timidity->song, GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));

  if (bytes_read == 0) {
    gst_buffer_unref (buffer);
    return NULL;
  }

  GST_BUFFER_OFFSET (buffer) =
      timidity->o_segment->last_stop * timidity->bytes_per_frame;
  GST_BUFFER_TIMESTAMP (buffer) =
      timidity->o_segment->last_stop * timidity->time_per_frame;

  if (bytes_read < GST_BUFFER_SIZE (buffer)) {
    GstBuffer *old = buffer;

    buffer = gst_buffer_create_sub (buffer, 0, bytes_read);
    gst_buffer_unref (old);
  }

  samples = GST_BUFFER_SIZE (buffer) / timidity->bytes_per_frame;

  timidity->o_segment->last_stop += samples;

  GST_BUFFER_OFFSET_END (buffer) =
      timidity->o_segment->last_stop * timidity->bytes_per_frame;
  GST_BUFFER_DURATION (buffer) = samples * timidity->time_per_frame;

  GST_DEBUG_OBJECT (timidity,
      "generated buffer %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT
      " (%" G_GINT64_FORMAT " samples)",
      GST_TIME_ARGS ((guint64) GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (((guint64) (GST_BUFFER_TIMESTAMP (buffer) +
                  GST_BUFFER_DURATION (buffer)))), samples);

  return buffer;
}

static GstBuffer *
gst_timidity_get_buffer (GstTimidity * timidity)
{
  GstBuffer *out;

  out =
      gst_timidity_fill_buffer (timidity,
      gst_timidity_allocate_buffer (timidity, 256));

  if (!out)
    return NULL;

  return gst_timidity_clip_buffer (timidity, out);
}

static void
gst_timidity_loop (GstPad * sinkpad)
{
  GstTimidity *timidity = GST_TIMIDITY (GST_PAD_PARENT (sinkpad));
  GstBuffer *out;
  GstFlowReturn ret;

  if (timidity->mididata_size == 0) {
    if (!gst_timidity_get_upstream_size (timidity, &timidity->mididata_size)) {
      GST_ELEMENT_ERROR (timidity, STREAM, DECODE, (NULL),
          ("Unable to get song length"));
      goto paused;
    }

    if (timidity->mididata)
      g_free (timidity->mididata);

    timidity->mididata = g_malloc (timidity->mididata_size);
    timidity->mididata_offset = 0;
    return;
  }

  if (timidity->mididata_offset < timidity->mididata_size) {
    GstBuffer *buffer = NULL;
    gint64 size;

    GST_DEBUG_OBJECT (timidity, "loading song");

    ret =
        gst_pad_pull_range (timidity->sinkpad, timidity->mididata_offset,
        -1, &buffer);
    if (ret != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (timidity, STREAM, DECODE, (NULL),
          ("Unable to load song"));
      goto paused;
    }

    size = timidity->mididata_size - timidity->mididata_offset;
    if (GST_BUFFER_SIZE (buffer) < size)
      size = GST_BUFFER_SIZE (buffer);

    memmove (timidity->mididata + timidity->mididata_offset,
        GST_BUFFER_DATA (buffer), size);
    gst_buffer_unref (buffer);

    timidity->mididata_offset += size;
    GST_DEBUG_OBJECT (timidity, "Song loaded");
    return;
  }

  if (!timidity->song) {
    MidIStream *stream;
    GstTagList *tags = NULL;
    gchar *text;

    GST_DEBUG_OBJECT (timidity, "Parsing song");

    stream =
        mid_istream_open_mem (timidity->mididata, timidity->mididata_size, 0);

    timidity->song = mid_song_load (stream, timidity->song_options);
    mid_istream_close (stream);

    if (!timidity->song) {
      GST_ELEMENT_ERROR (timidity, STREAM, DECODE, (NULL),
          ("Unable to parse midi"));
      goto paused;
    }

    mid_song_start (timidity->song);
    timidity->o_len = (GST_MSECOND *
        (GstClockTime) mid_song_get_total_time (timidity->song)) /
        timidity->time_per_frame;
    gst_segment_set_newsegment (timidity->o_segment, FALSE, 1.0,
        GST_FORMAT_DEFAULT, 0, GST_CLOCK_TIME_NONE, 0);


    gst_pad_push_event (timidity->srcpad,
        gst_timidity_get_new_segment_event (timidity, GST_FORMAT_TIME, FALSE));

    /* extract tags */
    text = mid_song_get_meta (timidity->song, MID_SONG_TEXT);
    if (text) {
      tags = gst_tag_list_new ();
      gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, text, NULL);

      //g_free (text);
    }

    text = mid_song_get_meta (timidity->song, MID_SONG_COPYRIGHT);
    if (text) {
      if (tags == NULL)
        tags = gst_tag_list_new ();
      gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
          GST_TAG_COPYRIGHT, text, NULL);

      //g_free (text);
    }

    if (tags) {
      gst_element_found_tags (GST_ELEMENT (timidity), tags);
    }

    GST_DEBUG_OBJECT (timidity, "Parsing song done");
    return;
  }

  if (timidity->o_segment_changed) {
    GstSegment *segment = gst_timidity_get_segment (timidity, GST_FORMAT_TIME,
        !timidity->o_new_segment);

    GST_LOG_OBJECT (timidity,
        "sending newsegment from %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT
        ", pos=%" GST_TIME_FORMAT, GST_TIME_ARGS ((guint64) segment->start),
        GST_TIME_ARGS ((guint64) segment->stop),
        GST_TIME_ARGS ((guint64) segment->time));

    if (timidity->o_segment->flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT (timidity),
          gst_message_new_segment_start (GST_OBJECT (timidity),
              segment->format, segment->start));
    }

    gst_segment_free (segment);
    timidity->o_segment_changed = FALSE;
    return;
  }

  if (timidity->o_seek) {
    /* perform a seek internally */
    timidity->o_segment->last_stop = timidity->o_segment->time;
    mid_song_seek (timidity->song,
        (timidity->o_segment->last_stop * timidity->time_per_frame) /
        GST_MSECOND);
  }

  out = gst_timidity_get_buffer (timidity);
  if (!out) {
    GST_LOG_OBJECT (timidity, "Song ended, generating eos");
    gst_pad_push_event (timidity->srcpad, gst_event_new_eos ());
    timidity->o_seek = FALSE;
    goto paused;
  }

  if (timidity->o_seek) {
    GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
    timidity->o_seek = FALSE;
  }

  gst_buffer_set_caps (out, timidity->out_caps);
  ret = gst_pad_push (timidity->srcpad, out);

  if (ret == GST_FLOW_UNEXPECTED)
    goto eos;
  else if (ret < GST_FLOW_UNEXPECTED || ret == GST_FLOW_NOT_LINKED)
    goto error;

  return;

paused:
  {
    GST_DEBUG_OBJECT (timidity, "pausing task");
    gst_pad_pause_task (timidity->sinkpad);
    return;
  }
eos:
  {
    gst_pad_push_event (timidity->srcpad, gst_event_new_eos ());
    goto paused;
  }
error:
  {
    GST_ELEMENT_ERROR (timidity, STREAM, FAILED,
        ("Internal data stream error"),
        ("Streaming stopped, reason %s", gst_flow_get_name (ret)));
    gst_pad_push_event (timidity->srcpad, gst_event_new_eos ());
    goto paused;
  }
}

static GstStateChangeReturn
gst_timidity_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstTimidity *timidity = GST_TIMIDITY (element);

  if (!timidity->initialized) {
    GST_WARNING ("Timidity renderer is not initialized");
    return GST_STATE_CHANGE_FAILURE;
  }

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      timidity->out_caps =
          gst_caps_copy (gst_pad_get_pad_template_caps (timidity->srcpad));
      timidity->mididata = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      timidity->mididata_size = 0;
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
      if (timidity->song)
        mid_song_free (timidity->song);
      timidity->song = NULL;
      timidity->mididata_size = 0;
      if (timidity->mididata) {
        g_free (timidity->mididata);
        timidity->mididata = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_caps_unref (timidity->out_caps);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_timidity_debug, "timidity",
      0, "Timidity plugin");

  return gst_element_register (plugin, "timidity",
      GST_RANK_PRIMARY, GST_TYPE_TIMIDITY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    timidity,
    "Timidity Plugin",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
