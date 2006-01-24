/* GStreamer Musepack decoder plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmusepackdec.h"
#include "gstmusepackreader.h"

GST_DEBUG_CATEGORY (musepackdec_debug);
#define GST_CAT_DEFAULT musepackdec_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-musepack")
    );

#ifdef MPC_FIXED_POINT
#define BASE_CAPS \
  "audio/x-raw-int, " \
    "signed = (bool) TRUE, " \
    "width = (int) 32, " \
    "depth = (int) 32"
#else
#define BASE_CAPS \
  "audio/x-raw-float, " \
    "width = (int) 32"
#endif

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (BASE_CAPS ", "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_musepackdec_dispose (GObject * obj);

static gboolean gst_musepackdec_src_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_musepackdec_get_src_query_types (GstPad * pad);
static gboolean gst_musepackdec_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_musepackdec_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);
static gboolean gst_musepackdec_sink_activate (GstPad * sinkpad);
static gboolean
gst_musepackdec_sink_activate_pull (GstPad * sinkpad, gboolean active);

static void gst_musepackdec_loop (GstPad * sinkpad);
static GstStateChangeReturn
gst_musepackdec_change_state (GstElement * element, GstStateChange transition);

GST_BOILERPLATE (GstMusepackDec, gst_musepackdec, GstElement, GST_TYPE_ELEMENT)

     static void gst_musepackdec_base_init (gpointer klass)
{
  static GstElementDetails gst_musepackdec_details =
      GST_ELEMENT_DETAILS ("Musepack decoder",
      "Codec/Decoder/Audio",
      "Musepack decoder",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_musepackdec_details);

  GST_DEBUG_CATEGORY_INIT (musepackdec_debug, "musepackdec", 0, "mpc decoder");
}

static void
gst_musepackdec_class_init (GstMusepackDecClass * klass)
{
  GST_ELEMENT_CLASS (klass)->change_state =
      GST_DEBUG_FUNCPTR (gst_musepackdec_change_state);

  G_OBJECT_CLASS (klass)->dispose = GST_DEBUG_FUNCPTR (gst_musepackdec_dispose);
}

static void
gst_musepackdec_init (GstMusepackDec * musepackdec, GstMusepackDecClass * klass)
{
  musepackdec->offset = 0;

  musepackdec->r = g_new (mpc_reader, 1);
  musepackdec->d = g_new (mpc_decoder, 1);
  musepackdec->init = FALSE;

  musepackdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (musepackdec), musepackdec->sinkpad);

  gst_pad_set_activate_function (musepackdec->sinkpad,
      gst_musepackdec_sink_activate);
  gst_pad_set_activatepull_function (musepackdec->sinkpad,
      gst_musepackdec_sink_activate_pull);

  musepackdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_pad_set_event_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_event));

  gst_pad_set_query_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_src_query));
  gst_pad_set_query_type_function (musepackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_musepackdec_get_src_query_types));
  gst_pad_use_fixed_caps (musepackdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (musepackdec), musepackdec->srcpad);
}

static void
gst_musepackdec_dispose (GObject * obj)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (obj);

  g_free (musepackdec->r);
  musepackdec->r = NULL;
  g_free (musepackdec->d);
  musepackdec->d = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_musepackdec_send_newsegment (GstMusepackDec * dec, gboolean update)
{
  GstSegment *s = &dec->segment;
  GstFormat target_format = GST_FORMAT_TIME;
  gint64 stop_time = GST_CLOCK_TIME_NONE;
  gint64 start_time = 0;

  /* segment is in DEFAULT format, but we want to send a TIME newsegment */
  if (!gst_musepackdec_src_convert (dec->srcpad, GST_FORMAT_DEFAULT,
          s->start, &target_format, &start_time)) {
    GST_WARNING_OBJECT (dec, "failed to convert segment start %"
        G_GINT64_FORMAT " to TIME", s->start);
    return;
  }

  if (s->stop != -1 && !gst_musepackdec_src_convert (dec->srcpad,
          GST_FORMAT_DEFAULT, s->stop, &target_format, &stop_time)) {
    GST_WARNING_OBJECT (dec, "failed to convert segment stop to TIME");
    return;
  }

  GST_DEBUG_OBJECT (dec, "sending newsegment from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT, GST_TIME_ARGS (start_time),
      GST_TIME_ARGS (stop_time));

  gst_pad_push_event (dec->srcpad,
      gst_event_new_new_segment (update, s->rate, GST_FORMAT_TIME,
          start_time, stop_time, start_time));
}

static gboolean
gst_musepackdec_handle_seek_event (GstMusepackDec * dec, GstEvent * event)
{
  GstSeekType start_type, stop_type;
  GstSeekFlags flags;
  GstSegment segment;
  GstFormat format;
  gboolean only_update;
  gboolean seek_ok;
  gboolean flush;
  gdouble rate;
  gint64 start, stop;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT) {
    GST_DEBUG_OBJECT (dec, "seek failed: only TIME or DEFAULT format allowed");
    return FALSE;
  }

  GST_OBJECT_LOCK (dec);

  if (format == GST_FORMAT_TIME) {
    format = GST_FORMAT_DEFAULT;
    if (start_type != GST_SEEK_TYPE_NONE &&
        !gst_musepackdec_src_convert (dec->srcpad, GST_FORMAT_TIME,
            start, &format, &start)) {
      GST_DEBUG_OBJECT (dec, "failed to convert start to to DEFAULT format");
      goto failed;
    }
    if (stop_type != GST_SEEK_TYPE_NONE &&
        !gst_musepackdec_src_convert (dec->srcpad, GST_FORMAT_TIME,
            stop, &format, &stop)) {
      GST_DEBUG_OBJECT (dec, "failed to convert stop to to DEFAULT format");
      goto failed;
    }
  }

  flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

  /* operate on segment copy until we know the seek worked */
  segment = dec->segment;
  GST_OBJECT_UNLOCK (dec);

  gst_segment_set_seek (&segment, rate, GST_FORMAT_DEFAULT,
      flags, start_type, start, stop_type, stop, &only_update);

  if (flush) {
    gst_pad_push_event (dec->srcpad, gst_event_new_flush_start ());
  } else {
    gst_pad_stop_task (dec->sinkpad);
  }

  gst_pad_push_event (dec->sinkpad, gst_event_new_flush_start ());
  GST_PAD_STREAM_LOCK (dec->sinkpad);
  gst_pad_push_event (dec->sinkpad, gst_event_new_flush_stop ());

  GST_OBJECT_LOCK (dec);

#if 0
  if (only_update) {
    dec->segment = segment;
    gst_musepackdec_send_newsegment (dec, TRUE);
    goto done;
  }
#endif

  GST_DEBUG_OBJECT (dec, "segment: [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
      "] = [%" GST_TIME_FORMAT "-%" GST_TIME_FORMAT "]",
      segment.start, segment.stop,
      GST_TIME_ARGS (segment.start * GST_SECOND / dec->rate),
      GST_TIME_ARGS (segment.stop * GST_SECOND / dec->rate));

  GST_DEBUG_OBJECT (dec, "performing seek to sample %" G_GINT64_FORMAT,
      segment.start);

  if (flush) {
    gst_pad_push_event (dec->srcpad, gst_event_new_flush_stop ());
  }

  if (segment.start < 0 || segment.start >= segment.duration) {
    GST_WARNING_OBJECT (dec, "seek out of bounds");
    GST_PAD_STREAM_UNLOCK (dec->sinkpad);
    goto failed;
  }

  seek_ok = mpc_decoder_seek_sample (dec->d, segment.start);
  if (!seek_ok) {
    GST_PAD_STREAM_UNLOCK (dec->sinkpad);
    goto failed;
  }

  /* FIXME: support segment seeks
     if ((seek_flags & GST_SEEK_FLAG_SEGMENT) != 0) {
     GST_DEBUG_OBJECT (dec, "posting SEGMENT_START message");
     GST_OBJECT_UNLOCK (dec);
     gst_element_post_message (GST_ELEMENT (dec),
     gst_message_new_segment_start (GST_OBJECT (dec),
     GST_FORMAT_DEFAULT, dec->segment.start));
     GST_OBJECT_LOCK (dec);
     }
   */

  gst_segment_set_last_stop (&segment, GST_FORMAT_DEFAULT, segment.start);
  dec->segment = segment;
  gst_musepackdec_send_newsegment (dec, FALSE);

  GST_DEBUG_OBJECT (dec, "seek successful");

#if 0
done:
#endif

  GST_PAD_STREAM_UNLOCK (dec->sinkpad);

  gst_pad_start_task (dec->sinkpad,
      (GstTaskFunction) gst_musepackdec_loop, dec->sinkpad);

  GST_OBJECT_UNLOCK (dec);
  return TRUE;

failed:
  {
    GST_WARNING_OBJECT (dec, "seek failed");
    GST_OBJECT_UNLOCK (dec);
    return FALSE;
  }
}

static gboolean
gst_musepackdec_src_event (GstPad * pad, GstEvent * event)
{
  GstMusepackDec *dec;
  gboolean res;

  dec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (dec, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_musepackdec_handle_seek_event (dec, event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (dec);
  return res;
}

static const GstQueryType *
gst_musepackdec_get_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    (GstQueryType) 0
  };

  return query_types;
}

static gboolean
gst_musepackdec_src_query (GstPad * pad, GstQuery * query)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  GstFormat format = GST_FORMAT_DEFAULT;
  GstFormat dest_format;
  gint64 value, dest_value;
  gboolean res = TRUE;

  GST_OBJECT_LOCK (musepackdec);

  if (!musepackdec->init) {
    res = FALSE;
    goto done;
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &dest_format, NULL);
      if (!gst_musepackdec_src_convert (pad, format,
              musepackdec->segment.last_stop, &dest_format, &dest_value)) {
        res = FALSE;
      }
      gst_query_set_position (query, dest_format, dest_value);
      break;
    case GST_QUERY_DURATION:
      gst_query_parse_duration (query, &dest_format, NULL);
      if (!gst_musepackdec_src_convert (pad, format,
              musepackdec->segment.duration, &dest_format, &dest_value)) {
        res = FALSE;
        break;
      }
      gst_query_set_duration (query, dest_format, dest_value);
      break;
    case GST_QUERY_CONVERT:
      gst_query_parse_convert (query, &format, &value, &dest_format,
          &dest_value);
      if (!gst_musepackdec_src_convert (pad, format, value, &dest_format,
              &dest_value)) {
        res = FALSE;
      }
      gst_query_set_convert (query, format, value, dest_format, dest_value);
      break;
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

done:
  GST_OBJECT_UNLOCK (musepackdec);
  gst_object_unref (musepackdec);
  return res;
}

static gboolean
gst_musepackdec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  if (!musepackdec->init) {
    GST_DEBUG_OBJECT (musepackdec, "conversion failed: not initialiased yet");
    gst_object_unref (musepackdec);
    return FALSE;
  }

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value,
              GST_SECOND, musepackdec->rate);
          break;
        case GST_FORMAT_BYTES:
          *dest_value = src_value * musepackdec->bps;
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value,
              musepackdec->rate, GST_SECOND);
          break;
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale_int (src_value,
              musepackdec->rate * musepackdec->bps, GST_SECOND);
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / musepackdec->bps;
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value,
              GST_SECOND, musepackdec->bps * musepackdec->rate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;

    default:
      res = FALSE;
      break;
  }

  gst_object_unref (musepackdec);
  return res;
}

static gboolean
gst_musepack_stream_init (GstMusepackDec * musepackdec)
{
  mpc_streaminfo i;
  GstTagList *tags;
  GstCaps *caps;

  /* set up reading */
  gst_musepack_init_reader (musepackdec->r, musepackdec);

  /* streaminfo */
  mpc_streaminfo_init (&i);
  if (mpc_streaminfo_read (&i, musepackdec->r) < 0) {
    GST_ELEMENT_ERROR (musepackdec, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  /* decoding */
  mpc_decoder_setup (musepackdec->d, musepackdec->r);
  mpc_decoder_scale_output (musepackdec->d, 1.0);
  if (!mpc_decoder_initialize (musepackdec->d, &i)) {
    GST_ELEMENT_ERROR (musepackdec, STREAM, WRONG_TYPE, (NULL), (NULL));
    return FALSE;
  }

  /* capsnego */
  caps = gst_caps_from_string (BASE_CAPS);
  gst_caps_set_simple (caps,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "channels", G_TYPE_INT, i.channels,
      "rate", G_TYPE_INT, i.sample_freq, NULL);
  gst_pad_use_fixed_caps (musepackdec->srcpad);
  if (!gst_pad_set_caps (musepackdec->srcpad, caps)) {
    GST_ELEMENT_ERROR (musepackdec, CORE, NEGOTIATION, (NULL), (NULL));
    return FALSE;
  }

  musepackdec->bps = 4 * i.channels;;
  musepackdec->rate = i.sample_freq;

  gst_segment_set_last_stop (&musepackdec->segment, GST_FORMAT_DEFAULT, 0);
  gst_segment_set_duration (&musepackdec->segment, GST_FORMAT_DEFAULT,
      mpc_streaminfo_get_length_samples (&i));

  /* send basic tags */
  tags = gst_tag_list_new ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "Musepack", NULL);

  if (i.encoder[0] != '\0' && i.encoder_version > 0) {
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, i.encoder,
        GST_TAG_ENCODER_VERSION, i.encoder_version, NULL);
  }

  if (i.bitrate > 0) {
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, i.bitrate, NULL);
  } else if (i.average_bitrate > 0.0) {
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) i.average_bitrate, NULL);
  }

  /* FIXME: are these values correct in the end? */
  if (i.gain_title != 0 || i.gain_album != 0) {
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_TRACK_GAIN, (gdouble) i.gain_title / 100.0,
        GST_TAG_ALBUM_GAIN, (gdouble) i.gain_album / 100.0, NULL);
  }

  /* FIXME: are these values correct in the end? */
  if (i.peak_title != 0 && i.peak_album != 0) {
    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_TRACK_PEAK, (gdouble) i.peak_title,
        GST_TAG_ALBUM_PEAK, (gdouble) i.peak_album, NULL);
  }

  GST_LOG_OBJECT (musepackdec, "Posting tags: %" GST_PTR_FORMAT, tags);
  gst_element_found_tags (GST_ELEMENT (musepackdec), tags);

  return TRUE;
}

static gboolean
gst_musepackdec_sink_activate (GstPad * sinkpad)
{

  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return FALSE;
  }
}

static gboolean
gst_musepackdec_sink_activate_pull (GstPad * sinkpad, gboolean active)
{

  gboolean result;

  if (active) {
    result = gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_musepackdec_loop, sinkpad);
  } else {
    result = gst_pad_stop_task (sinkpad);
  }

  return result;
}

static void
gst_musepackdec_loop (GstPad * sinkpad)
{
  GstMusepackDec *musepackdec;
  GstFlowReturn flow;
  GstBuffer *out;
  guint32 update_acc, update_bits;
  gint num_samples;

  musepackdec = GST_MUSEPACK_DEC (GST_PAD_PARENT (sinkpad));

  if (!musepackdec->init) {
    if (!gst_musepack_stream_init (musepackdec))
      goto pause_task;

    musepackdec->init = TRUE;
    gst_musepackdec_send_newsegment (musepackdec, FALSE);
  }

  out = gst_buffer_new_and_alloc (MPC_DECODER_BUFFER_LENGTH * 4);

  num_samples = mpc_decoder_decode (musepackdec->d,
      (MPC_SAMPLE_FORMAT *) GST_BUFFER_DATA (out), &update_acc, &update_bits);

  if (num_samples < 0) {
    GST_ERROR_OBJECT (musepackdec, "Failed to decode sample");
    GST_ELEMENT_ERROR (musepackdec, STREAM, DECODE, (NULL), (NULL));
    goto pause_task;
  } else if (num_samples == 0) {
    GST_DEBUG_OBJECT (musepackdec, "EOS");
    gst_pad_push_event (musepackdec->srcpad, gst_event_new_eos ());
    goto pause_task;
  }

  GST_BUFFER_SIZE (out) = num_samples * musepackdec->bps;

  GST_BUFFER_OFFSET (out) = musepackdec->segment.last_stop;
  GST_BUFFER_TIMESTAMP (out) =
      gst_util_uint64_scale_int (musepackdec->segment.last_stop,
      GST_SECOND, musepackdec->rate);
  GST_BUFFER_DURATION (out) =
      gst_util_uint64_scale_int (num_samples, GST_SECOND, musepackdec->rate);

  gst_buffer_set_caps (out, GST_PAD_CAPS (musepackdec->srcpad));

  musepackdec->segment.last_stop += num_samples;

  GST_LOG_OBJECT (musepackdec, "Pushing buffer, timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out)));

  flow = gst_pad_push (musepackdec->srcpad, out);
  if (flow != GST_FLOW_OK && flow != GST_FLOW_NOT_LINKED) {
    GST_DEBUG_OBJECT (musepackdec, "Flow: %s", gst_flow_get_name (flow));
    goto pause_task;
  }

  return;

pause_task:
  {
    GST_DEBUG_OBJECT (musepackdec, "Pausing task");
    gst_pad_pause_task (sinkpad);
    return;
  }
}

static GstStateChangeReturn
gst_musepackdec_change_state (GstElement * element, GstStateChange transition)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&musepackdec->segment, GST_FORMAT_DEFAULT);
      gst_segment_set_last_stop (&musepackdec->segment, GST_FORMAT_DEFAULT, 0);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_segment_init (&musepackdec->segment, GST_FORMAT_UNDEFINED);
      musepackdec->init = FALSE;
      musepackdec->offset = 0;
      break;
    default:
      break;
  }

  return ret;

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "musepackdec",
      GST_RANK_PRIMARY, GST_TYPE_MUSEPACK_DEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "musepack",
    "Musepack decoder", plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
