/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>,
 *               <2006> Edward Hervey <bilboed@bilboed.com>
 *               <2006> Wim Taymans <wim@fluendo.com>
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

#include <string.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#ifdef HAVE_AVI_H
#include <avi.h>
#endif
#else
#include <ffmpeg/avformat.h>
#ifdef HAVE_AVI_H
#include <ffmpeg/avi.h>
#endif
#endif
#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegDemux GstFFMpegDemux;
typedef struct _GstFFStream GstFFStream;

struct _GstFFStream
{
  GstPad *pad;

  AVStream *avstream;

  gboolean unknown;
  GstClockTime last_ts;
  gboolean discont;
  gboolean eos;
  GstFlowReturn last_flow;
};

struct _GstFFMpegDemux
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *sinkpad;

  AVFormatContext *context;
  gboolean opened;

  GstFFStream *streams[MAX_STREAMS];

  gint videopads, audiopads;

  GstClockTime start_time;
  GstClockTime duration;

  gboolean seekable;

  gboolean flushing;

  /* segment stuff */
  GstSegment segment;
  gboolean running;

  /* cached seek in READY */
  GstEvent *seek_event;
};

typedef struct _GstFFMpegDemuxClassParams
{
  AVInputFormat *in_plugin;
  GstCaps *sinkcaps, *videosrccaps, *audiosrccaps;
} GstFFMpegDemuxClassParams;

typedef struct _GstFFMpegDemuxClass GstFFMpegDemuxClass;

struct _GstFFMpegDemuxClass
{
  GstElementClass parent_class;

  AVInputFormat *in_plugin;
  GstPadTemplate *sinktempl;
  GstPadTemplate *videosrctempl;
  GstPadTemplate *audiosrctempl;
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegdemux_class_init (GstFFMpegDemuxClass * klass);
static void gst_ffmpegdemux_base_init (GstFFMpegDemuxClass * klass);
static void gst_ffmpegdemux_init (GstFFMpegDemux * demux);

static void gst_ffmpegdemux_loop (GstPad * pad);
static gboolean gst_ffmpegdemux_sink_activate (GstPad * sinkpad);
static gboolean
gst_ffmpegdemux_sink_activate_pull (GstPad * sinkpad, gboolean active);
static gboolean
gst_ffmpegdemux_sink_activate_push (GstPad * sinkpad, gboolean active);

#if 0
static gboolean
gst_ffmpegdemux_src_convert (GstPad * pad,
    GstFormat src_fmt,
    gint64 src_value, GstFormat * dest_fmt, gint64 * dest_value);
#endif
static gboolean
gst_ffmpegdemux_send_event (GstElement * element, GstEvent * event);
static GstStateChangeReturn
gst_ffmpegdemux_change_state (GstElement * element, GstStateChange transition);

#define GST_FFDEMUX_PARAMS_QDATA g_quark_from_static_string("ffdemux-params")

static GstElementClass *parent_class = NULL;

static const gchar *
gst_ffmpegdemux_averror (gint av_errno)
{
  const gchar *message = NULL;

  switch (av_errno) {
    case AVERROR_UNKNOWN:
      message = "Unknown error";
      break;
    case AVERROR_IO:
      message = "Input/output error";
      break;
    case AVERROR_NUMEXPECTED:
      message = "Number syntax expected in filename";
      break;
    case AVERROR_NOMEM:
      message = "Not enough memory";
      break;
    case AVERROR_NOFMT:
      message = "Unknown format";
      break;
    case AVERROR_NOTSUPP:
      message = "Operation not supported";
      break;
    default:
      message = "Unhandled error code received";
      break;
  }

  return message;
}

static void
gst_ffmpegdemux_base_init (GstFFMpegDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegDemuxClassParams *params;
  GstElementDetails details;
  GstPadTemplate *sinktempl, *audiosrctempl, *videosrctempl;

  params = (GstFFMpegDemuxClassParams *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), GST_FFDEMUX_PARAMS_QDATA);
  g_assert (params != NULL);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s demuxer",
      params->in_plugin->long_name);
  details.klass = "Codec/Demuxer";
  details.description = g_strdup_printf ("FFMPEG %s demuxer",
      params->in_plugin->long_name);
  details.author = "Wim Taymans <wim@fluendo.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edward Hervey <bilboed@bilboed.com>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.description);

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, params->sinkcaps);
  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, params->videosrccaps);
  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, params->audiosrccaps);

  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->in_plugin = params->in_plugin;
  klass->videosrctempl = videosrctempl;
  klass->audiosrctempl = audiosrctempl;
  klass->sinktempl = sinktempl;
}

static void
gst_ffmpegdemux_class_init (GstFFMpegDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_ffmpegdemux_change_state;
  gstelement_class->send_event = gst_ffmpegdemux_send_event;
}

static void
gst_ffmpegdemux_init (GstFFMpegDemux * demux)
{
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) (G_OBJECT_GET_CLASS (demux));
  gint n;

  demux->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_activate_function (demux->sinkpad, gst_ffmpegdemux_sink_activate);
  gst_pad_set_activatepull_function (demux->sinkpad,
      gst_ffmpegdemux_sink_activate_pull);
  gst_pad_set_activatepush_function (demux->sinkpad,
      gst_ffmpegdemux_sink_activate_push);
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->opened = FALSE;
  demux->context = NULL;

  for (n = 0; n < MAX_STREAMS; n++) {
    demux->streams[n] = NULL;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  demux->seek_event = NULL;
  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
}

static void
gst_ffmpegdemux_close (GstFFMpegDemux * demux)
{
  gint n;
  GstEvent **event_p;

  if (!demux->opened)
    return;

  /* remove pads from ourselves */
  for (n = 0; n < MAX_STREAMS; n++) {
    GstFFStream *stream;

    stream = demux->streams[n];
    if (stream && stream->pad) {
      gst_element_remove_pad (GST_ELEMENT (demux), stream->pad);
      g_free (stream);
    }
    demux->streams[n] = NULL;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  /* close demuxer context from ffmpeg */
  av_close_input_file (demux->context);
  demux->context = NULL;

  GST_OBJECT_LOCK (demux);
  demux->opened = FALSE;
  event_p = &demux->seek_event;
  gst_event_replace (event_p, NULL);
  GST_OBJECT_UNLOCK (demux);

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
}

/* send an event to all the source pads .
 * Takes ownership of the event.
 *
 * Returns FALSE if none of the source pads handled the event.
 */
static gboolean
gst_ffmpegdemux_push_event (GstFFMpegDemux * demux, GstEvent * event)
{
  gboolean res;
  gint n;

  res = TRUE;

  for (n = 0; n < MAX_STREAMS; n++) {
    GstFFStream *s = demux->streams[n];

    if (s && s->pad) {
      gst_event_ref (event);
      res &= gst_pad_push_event (s->pad, event);
    }
  }
  gst_event_unref (event);

  return res;
}

/* set flags on all streams */
static void
gst_ffmpegdemux_set_flags (GstFFMpegDemux * demux, gboolean discont,
    gboolean eos)
{
  GstFFStream *s;
  gint n;

  for (n = 0; n < MAX_STREAMS; n++) {
    if ((s = demux->streams[n])) {
      s->discont = discont;
      s->eos = eos;
    }
  }
}

/* check if all streams are eos */
static gboolean
gst_ffmpegdemux_is_eos (GstFFMpegDemux * demux)
{
  GstFFStream *s;
  gint n;

  for (n = 0; n < MAX_STREAMS; n++) {
    if ((s = demux->streams[n])) {
      if (!s->eos)
        return FALSE;
    }
  }
  return TRUE;
}

static gboolean
gst_ffmpegdemux_do_seek (GstFFMpegDemux * demux, GstSegment * segment)
{
  gboolean ret;
  gint seekret;
  gint64 target;
  gint64 fftarget;
  AVStream *stream;
  gint index;

  /* find default index and fail if none is present */
  index = av_find_default_stream_index (demux->context);
  GST_LOG_OBJECT (demux, "default stream index %d", index);
  if (index < 0)
    return FALSE;

  ret = TRUE;

  /* get the stream for seeking */
  stream = demux->context->streams[index];
  /* initial seek position */
  target = segment->last_stop;
  /* convert target to ffmpeg time */
  fftarget = gst_ffmpeg_time_gst_to_ff (target, stream->time_base);

  GST_LOG_OBJECT (demux, "do seek to time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (target));

  /* if we need to land on a keyframe, try to do so, we don't try to do a 
   * keyframe seek if we are not absolutely sure we have an index.*/
  if (segment->flags & GST_SEEK_FLAG_KEY_UNIT && demux->context->index_built) {
    gint keyframeidx;

    GST_LOG_OBJECT (demux, "looking for keyframe in ffmpeg for time %"
        GST_TIME_FORMAT, GST_TIME_ARGS (target));

    /* search in the index for the previous keyframe */
    keyframeidx =
        av_index_search_timestamp (stream, fftarget, AVSEEK_FLAG_BACKWARD);

    GST_LOG_OBJECT (demux, "keyframeidx: %d", keyframeidx);

    if (keyframeidx >= 0) {
      fftarget = stream->index_entries[keyframeidx].timestamp;
      target = gst_ffmpeg_time_ff_to_gst (fftarget, stream->time_base);

      GST_LOG_OBJECT (demux,
          "Found a keyframe at ffmpeg idx: %d timestamp :%" GST_TIME_FORMAT,
          keyframeidx, GST_TIME_ARGS (target));
    }
  }

  GST_DEBUG_OBJECT (demux,
      "About to call av_seek_frame (context, %d, %lld, 0) for time %"
      GST_TIME_FORMAT, index, fftarget, GST_TIME_ARGS (target));

  if ((seekret =
          av_seek_frame (demux->context, index, fftarget,
              AVSEEK_FLAG_BACKWARD)) < 0)
    goto seek_failed;

  GST_DEBUG_OBJECT (demux, "seek success, returned %d", seekret);

  segment->last_stop = target;
  segment->time = target;
  segment->start = target;

  return ret;

  /* ERRORS */
seek_failed:
  {
    GST_WARNING_OBJECT (demux, "Call to av_seek_frame failed : %d", seekret);
    return FALSE;
  }
}

static gboolean
gst_ffmpegdemux_perform_seek (GstFFMpegDemux * demux, GstEvent * event)
{
  gboolean res;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean update;
  GstSegment seeksegment;

  GST_DEBUG_OBJECT (demux, "starting seek");

  if (event) {
    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we have to have a format as the segment format. Try to convert
     * if not. */
    if (demux->segment.format != format) {
      GstFormat fmt;

      fmt = demux->segment.format;
      res = TRUE;
      /* FIXME, use source pad */
      if (cur_type != GST_SEEK_TYPE_NONE && cur != -1)
        res = gst_pad_query_convert (demux->sinkpad, format, cur, &fmt, &cur);
      if (res && stop_type != GST_SEEK_TYPE_NONE && stop != -1)
        res = gst_pad_query_convert (demux->sinkpad, format, stop, &fmt, &stop);
      if (!res)
        goto no_format;

      format = fmt;
    }
  } else {
    flags = 0;
  }

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* send flush start */
  if (flush) {
    /* mark flushing so that the streaming thread can react on it */
    GST_OBJECT_LOCK (demux);
    demux->flushing = TRUE;
    GST_OBJECT_UNLOCK (demux);
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_start ());
    gst_ffmpegdemux_push_event (demux, gst_event_new_flush_start ());
  } else {
    gst_pad_pause_task (demux->sinkpad);
  }

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or our streaming thread stopped
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  /* make copy into temp structure, we can only update the main one
   * when we actually could do the seek. */
  memcpy (&seeksegment, &demux->segment, sizeof (GstSegment));

  /* now configure the seek segment */
  if (event) {
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (demux, "segment configured from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT ", position %" G_GINT64_FORMAT,
      seeksegment.start, seeksegment.stop, seeksegment.last_stop);

  /* make the sinkpad available for data passing since we might need
   * it when doing the seek */
  if (flush) {
    GST_OBJECT_LOCK (demux);
    demux->flushing = FALSE;
    GST_OBJECT_UNLOCK (demux);
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop ());
  }

  /* do the seek, segment.last_stop contains new position. */
  res = gst_ffmpegdemux_do_seek (demux, &seeksegment);

  /* and prepare to continue streaming */
  if (flush) {
    gint n;

    /* send flush stop, peer will accept data and events again. We
     * are not yet providing data as we still have the STREAM_LOCK. */
    gst_ffmpegdemux_push_event (demux, gst_event_new_flush_stop ());
    for (n = 0; n < MAX_STREAMS; ++n) {
      if (demux->streams[n])
        demux->streams[n]->last_flow = GST_FLOW_OK;
    }
  } else if (res && demux->running) {
    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (demux, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, demux->segment.start,
        demux->segment.last_stop);

    gst_ffmpegdemux_push_event (demux,
        gst_event_new_new_segment (TRUE,
            demux->segment.rate, demux->segment.format,
            demux->segment.start, demux->segment.last_stop,
            demux->segment.time));
  }
  /* if successfull seek, we update our real segment and push
   * out the new segment. */
  if (res) {
    memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));

    if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_segment_start (GST_OBJECT (demux),
              demux->segment.format, demux->segment.last_stop));
    }

    /* now send the newsegment */
    GST_DEBUG_OBJECT (demux, "Sending newsegment from %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, demux->segment.last_stop, demux->segment.stop);

    gst_ffmpegdemux_push_event (demux,
        gst_event_new_new_segment (FALSE,
            demux->segment.rate, demux->segment.format,
            demux->segment.last_stop, demux->segment.stop,
            demux->segment.time));
  }

  /* Mark discont on all srcpads and remove eos */
  gst_ffmpegdemux_set_flags (demux, TRUE, FALSE);

  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  demux->running = TRUE;
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_ffmpegdemux_loop,
      demux->sinkpad);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return res;

  /* ERROR */
no_format:
  {
    GST_DEBUG_OBJECT (demux, "undefined format given, seek aborted.");
    return FALSE;
  }
}

static gboolean
gst_ffmpegdemux_src_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDemux *demux;
  AVStream *avstream;
  GstFFStream *stream;
  gboolean res = TRUE;

  if (!(stream = gst_pad_get_element_private (pad)))
    return FALSE;

  avstream = stream->avstream;
  demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_ffmpegdemux_perform_seek (demux, event);
      gst_event_unref (event);
      break;
    case GST_EVENT_LATENCY:
      res = gst_pad_push_event (demux->sinkpad, event);
      break;
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_QOS:
    default:
      res = FALSE;
      gst_event_unref (event);
      break;
  }

  gst_object_unref (demux);

  return res;
}

static const GstQueryType *
gst_ffmpegdemux_src_query_list (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    GST_QUERY_SEEKING,
    0
  };

  return src_types;
}

static gboolean
gst_ffmpegdemux_send_event (GstElement * element, GstEvent * event)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) (element);
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_OBJECT_LOCK (demux);
      if (!demux->opened) {
        GstEvent **event_p;

        GST_DEBUG_OBJECT (demux, "caching seek event");
        event_p = &demux->seek_event;
        gst_event_replace (event_p, event);
        GST_OBJECT_UNLOCK (demux);

        res = TRUE;
      } else {
        GST_OBJECT_UNLOCK (demux);
        res = gst_ffmpegdemux_perform_seek (demux, event);
        gst_event_unref (event);
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static gboolean
gst_ffmpegdemux_src_query (GstPad * pad, GstQuery * query)
{
  GstFFMpegDemux *demux;
  GstFFStream *stream;
  AVStream *avstream;
  gboolean res = FALSE;

  if (!(stream = gst_pad_get_element_private (pad)))
    return FALSE;

  avstream = stream->avstream;

  demux = (GstFFMpegDemux *) GST_PAD_PARENT (pad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 timeposition;

      gst_query_parse_position (query, &format, NULL);

      timeposition = stream->last_ts;
      if (!(GST_CLOCK_TIME_IS_VALID (timeposition)))
        break;

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, GST_FORMAT_TIME, timeposition);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, GST_FORMAT_DEFAULT,
              gst_util_uint64_scale (timeposition, avstream->r_frame_rate.num,
                  GST_SECOND * avstream->r_frame_rate.den));
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          if (demux->videopads + demux->audiopads == 1 &&
              GST_PAD_PEER (demux->sinkpad) != NULL)
            res = gst_pad_query_default (pad, query);
          break;
        default:
          break;
      }
    }
      break;
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 timeduration;

      gst_query_parse_duration (query, &format, NULL);

      timeduration =
          gst_ffmpeg_time_ff_to_gst (avstream->duration, avstream->time_base);
      if (!(GST_CLOCK_TIME_IS_VALID (timeduration))) {
        /* use duration of complete file if the stream duration is not known */
        timeduration = demux->duration;
        if (!(GST_CLOCK_TIME_IS_VALID (timeduration)))
          break;
      }

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_duration (query, GST_FORMAT_TIME, timeduration);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_duration (query, GST_FORMAT_DEFAULT,
              gst_util_uint64_scale (timeduration, avstream->r_frame_rate.num,
                  GST_SECOND * avstream->r_frame_rate.den));
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          if (demux->videopads + demux->audiopads == 1 &&
              GST_PAD_PEER (demux->sinkpad) != NULL)
            res = gst_pad_query_default (pad, query);
          break;
        default:
          break;
      }
    }
      break;
    case GST_QUERY_SEEKING: {
      GstFormat format;
      gboolean seekable;
      gint64 dur = -1;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      seekable = demux->seekable;
      if (!gst_pad_query_duration (pad, &format, &dur)) {
        /* unlikely that we don't know duration but can seek */
        seekable = FALSE;
        dur = -1;
      }
      gst_query_set_seeking (query, format, seekable, 0, dur);
      res = TRUE;
      break;
    }
    default:
      /* FIXME : ADD GST_QUERY_CONVERT */
      res = gst_pad_query_default (pad, query);
      break;
  }

  return res;
}

#if 0
/* FIXME, reenable me */
static gboolean
gst_ffmpegdemux_src_convert (GstPad * pad,
    GstFormat src_fmt,
    gint64 src_value, GstFormat * dest_fmt, gint64 * dest_value)
{
  GstFFStream *stream;
  gboolean res = TRUE;
  AVStream *avstream;

  if (!(stream = gst_pad_get_element_private (pad)))
    return FALSE;

  avstream = stream->avstream;
  if (avstream->codec->codec_type != CODEC_TYPE_VIDEO)
    return FALSE;

  switch (src_fmt) {
    case GST_FORMAT_TIME:
      switch (*dest_fmt) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale (src_value,
              avstream->r_frame_rate.num,
              GST_SECOND * avstream->r_frame_rate.den);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              GST_SECOND * avstream->r_frame_rate.num,
              avstream->r_frame_rate.den);
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

  return res;
}
#endif

static GstFlowReturn
gst_ffmpegdemux_aggregated_flow (GstFFMpegDemux * demux)
{
  gint n;
  GstFlowReturn res = GST_FLOW_OK;
  gboolean have_ok = FALSE;

  for (n = 0; n < MAX_STREAMS; n++) {
    GstFFStream *s = demux->streams[n];

    if (s) {
      res = MIN (res, s->last_flow);

      if (GST_FLOW_IS_SUCCESS (s->last_flow))
        have_ok = TRUE;
    }
  }

  /* NOT_LINKED is OK, if at least one pad is linked */
  if (res == GST_FLOW_NOT_LINKED && have_ok)
    res = GST_FLOW_OK;

  GST_DEBUG_OBJECT (demux, "Returning aggregated value of %s",
      gst_flow_get_name (res));

  return res;
}

static GstFFStream *
gst_ffmpegdemux_get_stream (GstFFMpegDemux * demux, AVStream * avstream)
{
  GstFFMpegDemuxClass *oclass;
  GstPadTemplate *templ = NULL;
  GstPad *pad;
  GstCaps *caps;
  gint num;
  gchar *padname;
  const gchar *codec;
  AVCodecContext *ctx;
  GstFFStream *stream;

  ctx = avstream->codec;

  oclass = (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);

  if (demux->streams[avstream->index] != NULL)
    goto exists;

  /* create new stream */
  stream = g_new0 (GstFFStream, 1);
  demux->streams[avstream->index] = stream;

  /* mark stream as unknown */
  stream->unknown = TRUE;
  stream->discont = TRUE;
  stream->avstream = avstream;
  stream->last_ts = GST_CLOCK_TIME_NONE;
  stream->last_flow = GST_FLOW_OK;

  switch (ctx->codec_type) {
    case CODEC_TYPE_VIDEO:
      templ = oclass->videosrctempl;
      num = demux->videopads++;
      break;
    case CODEC_TYPE_AUDIO:
      templ = oclass->audiosrctempl;
      num = demux->audiopads++;
      break;
    default:
      goto unknown_type;
  }

  /* get caps that belongs to this stream */
  caps = gst_ffmpeg_codecid_to_caps (ctx->codec_id, ctx, TRUE);
  if (caps == NULL)
    goto unknown_caps;

  /* stream is known now */
  stream->unknown = FALSE;

  /* create new pad for this stream */
  padname = g_strdup_printf (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ), num);
  pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  gst_pad_use_fixed_caps (pad);
  gst_pad_set_caps (pad, caps);
  gst_caps_unref (caps);

  gst_pad_set_query_type_function (pad, gst_ffmpegdemux_src_query_list);
  gst_pad_set_query_function (pad, gst_ffmpegdemux_src_query);
  gst_pad_set_event_function (pad, gst_ffmpegdemux_src_event);

  /* store pad internally */
  stream->pad = pad;
  gst_pad_set_element_private (pad, stream);

  /* transform some useful info to GstClockTime and remember */
  {
    GstClockTime tmp;

    /* FIXME, actually use the start_time in some way */
    tmp = gst_ffmpeg_time_ff_to_gst (avstream->start_time, avstream->time_base);
    GST_DEBUG_OBJECT (demux, "stream %d: start time: %" GST_TIME_FORMAT,
        avstream->index, GST_TIME_ARGS (tmp));

    tmp = gst_ffmpeg_time_ff_to_gst (avstream->duration, avstream->time_base);
    GST_DEBUG_OBJECT (demux, "stream %d: duration: %" GST_TIME_FORMAT,
        avstream->index, GST_TIME_ARGS (tmp));
  }

  demux->streams[avstream->index] = stream;

  /* activate and add */
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (demux), pad);

  /* metadata */
  if ((codec = gst_ffmpeg_get_codecid_longname (ctx->codec_id))) {
    GstTagList *list = gst_tag_list_new ();

    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        (ctx->codec_type == CODEC_TYPE_VIDEO) ?
        GST_TAG_VIDEO_CODEC : GST_TAG_AUDIO_CODEC, codec, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (demux), pad, list);
  }

  return stream;

  /* ERRORS */
exists:
  {
    GST_DEBUG_OBJECT (demux, "Pad existed (stream %d)", avstream->index);
    return demux->streams[avstream->index];
  }
unknown_type:
  {
    GST_WARNING_OBJECT (demux, "Unknown pad type %d", ctx->codec_type);
    return stream;
  }
unknown_caps:
  {
    GST_WARNING_OBJECT (demux, "Unknown caps for codec %d", ctx->codec_id);
    return stream;
  }
}

static gchar *
my_safe_copy (gchar * input)
{
  gchar *output;

  if (!(g_utf8_validate (input, -1, NULL))) {
    output = g_convert (input, strlen (input),
        "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
  } else {
    output = g_strdup (input);
  }

  return output;
}

static GstTagList *
gst_ffmpegdemux_read_tags (GstFFMpegDemux * demux)
{
  GstTagList *tlist;
  gboolean hastag = FALSE;

  tlist = gst_tag_list_new ();

  if (*demux->context->title) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_TITLE, my_safe_copy (demux->context->title), NULL);
    hastag = TRUE;
  }
  if (*demux->context->author) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_ARTIST, my_safe_copy (demux->context->author), NULL);
    hastag = TRUE;
  }
  if (*demux->context->copyright) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_COPYRIGHT, my_safe_copy (demux->context->copyright), NULL);
    hastag = TRUE;
  }
  if (*demux->context->comment) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_COMMENT, my_safe_copy (demux->context->comment), NULL);
    hastag = TRUE;
  }
  if (*demux->context->album) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_ALBUM, my_safe_copy (demux->context->album), NULL);
    hastag = TRUE;
  }
  if (demux->context->track) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_TRACK_NUMBER, demux->context->track, NULL);
    hastag = TRUE;
  }
  if (*demux->context->genre) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_GENRE, my_safe_copy (demux->context->genre), NULL);
    hastag = TRUE;
  }
  if (demux->context->year) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
        GST_TAG_DATE, g_date_new_dmy (1, 1, demux->context->year), NULL);
    hastag = TRUE;
  }

  if (!hastag) {
    gst_tag_list_free (tlist);
    tlist = NULL;
  }
  return tlist;
}

static gboolean
gst_ffmpegdemux_open (GstFFMpegDemux * demux)
{
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);
  gchar *location;
  gint res, n_streams;
  GstTagList *tags;
  GstEvent *event;

  /* to be sure... */
  gst_ffmpegdemux_close (demux);

  /* open via our input protocol hack */
  location = g_strdup_printf ("gstreamer://%p", demux->sinkpad);
  GST_DEBUG_OBJECT (demux, "about to call av_open_input_file %s", location);

  res = av_open_input_file (&demux->context, location,
      oclass->in_plugin, 0, NULL);

  g_free (location);
  GST_DEBUG_OBJECT (demux, "av_open_input returned %d", res);
  if (res < 0)
    goto open_failed;

  res = gst_ffmpeg_av_find_stream_info (demux->context);
  GST_DEBUG_OBJECT (demux, "av_find_stream_info returned %d", res);
  if (res < 0)
    goto no_info;

  n_streams = demux->context->nb_streams;
  GST_DEBUG_OBJECT (demux, "we have %d streams", n_streams);

  /* open_input_file() automatically reads the header. We can now map each
   * created AVStream to a GstPad to make GStreamer handle it. */
  for (res = 0; res < n_streams; res++) {
    gst_ffmpegdemux_get_stream (demux, demux->context->streams[res]);
  }

  gst_element_no_more_pads (GST_ELEMENT (demux));

  /* grab the tags */
  tags = gst_ffmpegdemux_read_tags (demux);
  if (tags) {
    gst_element_post_message (GST_ELEMENT (demux),
        gst_message_new_tag (GST_OBJECT (demux), tags));
  }

  /* transform some useful info to GstClockTime and remember */
  demux->start_time = gst_util_uint64_scale_int (demux->context->start_time,
      GST_SECOND, AV_TIME_BASE);
  GST_DEBUG_OBJECT (demux, "start time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->start_time));
  if (demux->context->duration > 0)
    demux->duration = gst_util_uint64_scale_int (demux->context->duration,
        GST_SECOND, AV_TIME_BASE);
  else
    demux->duration = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (demux, "duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->duration));

  /* store duration in the segment as well */
  gst_segment_set_duration (&demux->segment, GST_FORMAT_TIME, demux->duration);

  GST_OBJECT_LOCK (demux);
  demux->opened = TRUE;
  event = demux->seek_event;
  demux->seek_event = NULL;
  GST_OBJECT_UNLOCK (demux);

  if (event) {
    gst_ffmpegdemux_perform_seek (demux, event);
    gst_event_unref (event);
  } else {
    gst_ffmpegdemux_push_event (demux,
        gst_event_new_new_segment (FALSE,
            demux->segment.rate, demux->segment.format,
            demux->segment.start, demux->segment.stop, demux->segment.time));
  }

  return TRUE;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (demux, LIBRARY, FAILED, (NULL),
        (gst_ffmpegdemux_averror (res)));
    return FALSE;
  }
no_info:
  {
    GST_ELEMENT_ERROR (demux, LIBRARY, FAILED, (NULL),
        (gst_ffmpegdemux_averror (res)));
    return FALSE;
  }
}

#define GST_FFMPEG_TYPE_FIND_SIZE 4096
static void
gst_ffmpegdemux_type_find (GstTypeFind * tf, gpointer priv)
{
  guint8 *data;
  GstFFMpegDemuxClassParams *params = (GstFFMpegDemuxClassParams *) priv;
  AVInputFormat *in_plugin = params->in_plugin;
  gint res = 0;
  guint64 length;

  /* We want GST_FFMPEG_TYPE_FIND_SIZE bytes, but if the file is shorter than
   * that we'll give it a try... */
  length = gst_type_find_get_length (tf);
  if (length == 0 || length > GST_FFMPEG_TYPE_FIND_SIZE)
    length = GST_FFMPEG_TYPE_FIND_SIZE;

  if (in_plugin->read_probe &&
      (data = gst_type_find_peek (tf, 0, length)) != NULL) {
    AVProbeData probe_data;

    probe_data.filename = "";
    probe_data.buf = data;
    probe_data.buf_size = length;

    res = in_plugin->read_probe (&probe_data);
    if (res > 0) {
      res = MAX (1, res * GST_TYPE_FIND_MAXIMUM / AVPROBE_SCORE_MAX);
      /* Restrict the probability for MPEG-TS streams, because there is
       * probably a better version in plugins-base, if the user has a recent
       * plugins-base (in fact we shouldn't even get here for ffmpeg mpegts or
       * mpegtsraw typefinders, since we blacklist them) */
      if (g_str_has_prefix (in_plugin->name, "mpegts"))
        res = MIN (res, GST_TYPE_FIND_POSSIBLE);

      GST_LOG ("ffmpeg typefinder '%s' suggests %" GST_PTR_FORMAT ", p=%u%%",
          in_plugin->name, params->sinkcaps, res);

      gst_type_find_suggest (tf, res, params->sinkcaps);
    }
  }
}

/* Task */
static void
gst_ffmpegdemux_loop (GstPad * pad)
{
  GstFFMpegDemux *demux;
  GstFlowReturn ret;
  gint res;
  AVPacket pkt;
  GstPad *srcpad;
  GstFFStream *stream;
  AVStream *avstream;
  GstBuffer *outbuf = NULL;
  GstClockTime timestamp, duration;
  gint outsize;
  gboolean rawvideo;

  demux = (GstFFMpegDemux *) (GST_PAD_PARENT (pad));

  /* open file if we didn't so already */
  if (!demux->opened)
    if (!gst_ffmpegdemux_open (demux))
      goto open_failed;

  GST_DEBUG_OBJECT (demux, "about to read a frame");

  /* read a frame */
  res = av_read_frame (demux->context, &pkt);
  if (res < 0)
    goto read_failed;

  /* get the stream */
  stream =
      gst_ffmpegdemux_get_stream (demux,
      demux->context->streams[pkt.stream_index]);

  /* check if we know the stream */
  if (stream->unknown)
    goto done;

  /* get more stuff belonging to this stream */
  avstream = stream->avstream;

  /* do timestamps, we do this first so that we can know when we
   * stepped over the segment stop position. */
  timestamp = gst_ffmpeg_time_ff_to_gst (pkt.pts, avstream->time_base);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    stream->last_ts = timestamp;
  }
  duration = gst_ffmpeg_time_ff_to_gst (pkt.duration, avstream->time_base);
  if (G_UNLIKELY (!duration)) {
    GST_WARNING_OBJECT (demux, "invalid buffer duration, setting to NONE");
    duration = GST_CLOCK_TIME_NONE;
  }


  GST_DEBUG_OBJECT (demux,
      "pkt pts:%" GST_TIME_FORMAT
      " / size:%d / stream_index:%d / flags:%d / duration:%" GST_TIME_FORMAT
      " / pos:%lld", GST_TIME_ARGS (timestamp), pkt.size, pkt.stream_index,
      pkt.flags, GST_TIME_ARGS (duration), pkt.pos);

  /* check start_time */
  if (demux->start_time != -1 && demux->start_time > timestamp)
    goto drop;

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    timestamp -= demux->start_time;

  /* check if we ran outside of the segment */
  if (demux->segment.stop != -1 && timestamp > demux->segment.stop)
    goto drop;

  /* prepare to push packet to peer */
  srcpad = stream->pad;

  rawvideo = (avstream->codec->codec_type == CODEC_TYPE_VIDEO &&
      avstream->codec->codec_id == CODEC_ID_RAWVIDEO);

  if (rawvideo)
    outsize = gst_ffmpeg_avpicture_get_size (avstream->codec->pix_fmt,
        avstream->codec->width, avstream->codec->height);
  else
    outsize = pkt.size;

  stream->last_flow = gst_pad_alloc_buffer_and_set_caps (srcpad,
      GST_CLOCK_TIME_NONE, outsize, GST_PAD_CAPS (srcpad), &outbuf);

  if ((ret = gst_ffmpegdemux_aggregated_flow (demux)) != GST_FLOW_OK)
    goto no_buffer;

  /* If the buffer allocation failed, don't try sending it ! */
  if (stream->last_flow != GST_FLOW_OK)
    goto done;

  /* copy the data from packet into the target buffer
   * and do conversions for raw video packets */
  if (rawvideo) {
    AVPicture src, dst;
    const gchar *plugin_name =
        ((GstFFMpegDemuxClass *) (G_OBJECT_GET_CLASS (demux)))->in_plugin->name;

    if (strcmp (plugin_name, "gif") == 0) {
      src.data[0] = pkt.data;
      src.data[1] = NULL;
      src.data[2] = NULL;
      src.linesize[0] = avstream->codec->width * 3;;
    } else {
      GST_WARNING ("Unknown demuxer %s, no idea what to do", plugin_name);
      gst_ffmpeg_avpicture_fill (&src, pkt.data,
          avstream->codec->pix_fmt, avstream->codec->width,
          avstream->codec->height);
    }

    gst_ffmpeg_avpicture_fill (&dst, GST_BUFFER_DATA (outbuf),
        avstream->codec->pix_fmt, avstream->codec->width,
        avstream->codec->height);

    gst_ffmpeg_img_convert (&dst, avstream->codec->pix_fmt,
        &src, avstream->codec->pix_fmt, avstream->codec->width,
        avstream->codec->height);
  } else {
    memcpy (GST_BUFFER_DATA (outbuf), pkt.data, outsize);
  }

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;

  /* mark keyframes */
  if (!(pkt.flags & PKT_FLAG_KEY)) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  /* Mark discont */
  if (stream->discont) {
    GST_DEBUG_OBJECT (demux, "marking DISCONT");
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }

  GST_DEBUG_OBJECT (demux,
      "Sending out buffer time:%" GST_TIME_FORMAT " size:%d",
      GST_TIME_ARGS (timestamp), GST_BUFFER_SIZE (outbuf));

  ret = stream->last_flow = gst_pad_push (srcpad, outbuf);

  /* if a pad is in e.g. WRONG_STATE, we want to pause to unlock the STREAM_LOCK */
  if ((ret != GST_FLOW_OK)
      && ((ret = gst_ffmpegdemux_aggregated_flow (demux)) != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux, "stream_movi flow: %s / %s",
        gst_flow_get_name (stream->last_flow), gst_flow_get_name (ret));
    goto pause;
  }

done:
  /* can destroy the packet now */
  pkt.destruct (&pkt);

  return;

  /* ERRORS */
pause:
  {
    GST_LOG_OBJECT (demux, "pausing task, reason %d (%s)", ret,
        gst_flow_get_name (ret));
    demux->running = FALSE;
    gst_pad_pause_task (demux->sinkpad);

    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;

          if ((stop = demux->segment.stop) == -1)
            stop = demux->segment.duration;

          GST_LOG_OBJECT (demux, "posting segment done");
          gst_element_post_message (GST_ELEMENT (demux),
              gst_message_new_segment_done (GST_OBJECT (demux),
                  demux->segment.format, stop));
        } else {
          GST_LOG_OBJECT (demux, "pushing eos");
          gst_ffmpegdemux_push_event (demux, gst_event_new_eos ());
        }
      } else {
        GST_ELEMENT_ERROR (demux, STREAM, FAILED,
            ("Internal data stream error."),
            ("streaming stopped, reason %s", gst_flow_get_name (ret)));
        gst_ffmpegdemux_push_event (demux, gst_event_new_eos ());
      }
    }
    return;
  }
open_failed:
  {
    ret = GST_FLOW_ERROR;
    goto pause;
  }
read_failed:
  {
    /* something went wrong... */
    GST_WARNING_OBJECT (demux, "av_read_frame returned %d", res);

    GST_OBJECT_LOCK (demux);
    /* pause appropriatly based on if we are flushing or not */
    if (demux->flushing)
      ret = GST_FLOW_WRONG_STATE;
    else
      ret = GST_FLOW_ERROR;
    GST_OBJECT_UNLOCK (demux);

    goto pause;
  }
drop:
  {
    GST_DEBUG_OBJECT (demux, "dropping buffer out of segment, stream eos");
    stream->eos = TRUE;
    if (gst_ffmpegdemux_is_eos (demux)) {
      pkt.destruct (&pkt);
      GST_DEBUG_OBJECT (demux, "we are eos");
      ret = GST_FLOW_UNEXPECTED;
      goto pause;
    } else {
      GST_DEBUG_OBJECT (demux, "some streams are not yet eos");
      goto done;
    }
  }
no_buffer:
  {
    pkt.destruct (&pkt);
    goto pause;
  }
}


static gboolean
gst_ffmpegdemux_sink_activate (GstPad * sinkpad)
{
  GstFFMpegDemux *demux;
  gboolean res;

  demux = (GstFFMpegDemux *) (gst_pad_get_parent (sinkpad));

  res = FALSE;

  if (gst_pad_check_pull_range (sinkpad))
    res = gst_pad_activate_pull (sinkpad, TRUE);
  else {
    res = gst_pad_activate_push (sinkpad, TRUE);
  }
  gst_object_unref (demux);
  return res;
}

static gboolean
gst_ffmpegdemux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstFFMpegDemux *demux;

  demux = (GstFFMpegDemux *) (gst_pad_get_parent (sinkpad));

  GST_ELEMENT_ERROR (demux, STREAM, NOT_IMPLEMENTED,
      (NULL),
      ("failed to activate sinkpad in pull mode, push mode not implemented yet"));

  demux->seekable = FALSE;
  gst_object_unref (demux);

  return FALSE;
}

static gboolean
gst_ffmpegdemux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstFFMpegDemux *demux;
  gboolean res;

  demux = (GstFFMpegDemux *) (gst_pad_get_parent (sinkpad));

  if (active) {
    demux->running = TRUE;
    demux->seekable = TRUE;
    res = gst_pad_start_task (sinkpad, (GstTaskFunction) gst_ffmpegdemux_loop,
        sinkpad);
  } else {
    demux->running = FALSE;
    res = gst_pad_stop_task (sinkpad);
    demux->seekable = FALSE;
  }

  gst_object_unref (demux);

  return res;
}

static GstStateChangeReturn
gst_ffmpegdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
#if 0
      /* test seek in READY here */
      gst_element_send_event (element, gst_event_new_seek (1.0,
              GST_FORMAT_TIME, GST_SEEK_FLAG_NONE,
              GST_SEEK_TYPE_SET, 10 * GST_SECOND,
              GST_SEEK_TYPE_SET, 13 * GST_SECOND));
#endif
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ffmpegdemux_close (demux);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_ffmpegdemux_register (GstPlugin * plugin)
{
  GType type;
  AVInputFormat *in_plugin;
  GstFFMpegDemuxClassParams *params;
  gchar **extensions;
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegDemuxClass),
    (GBaseInitFunc) gst_ffmpegdemux_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegdemux_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegDemux),
    0,
    (GInstanceInitFunc) gst_ffmpegdemux_init,
  };

  in_plugin = first_iformat;

  GST_LOG ("Registering demuxers");

  while (in_plugin) {
    gchar *type_name, *typefind_name;
    gchar *p, *name = NULL;
    GstCaps *sinkcaps, *audiosrccaps, *videosrccaps;
    gint rank;
    gboolean register_typefind_func = TRUE;

    GST_LOG ("Attempting to handle ffmpeg demuxer plugin %s [%s]",
        in_plugin->name, in_plugin->long_name);

    /* no emulators */
    if (!strncmp (in_plugin->long_name, "raw ", 4) ||
        !strncmp (in_plugin->long_name, "pcm ", 4) ||
        !strcmp (in_plugin->name, "audio_device") ||
        !strncmp (in_plugin->name, "image", 5) ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mjpeg") ||
        !strcmp (in_plugin->name, "redir"))
      goto next;

    /* no network demuxers */
    if (!strcmp (in_plugin->name, "sdp") || !strcmp (in_plugin->name, "rtsp"))
      goto next;

    /* these don't do what one would expect or
     * are only partially functional/useful */
    if (!strcmp (in_plugin->name, "aac") || !strcmp (in_plugin->name, "wv"))
      goto next;

    /* Don't use the typefind functions of formats for which we already have
     * better typefind functions */
    if (!strcmp (in_plugin->name, "mov,mp4,m4a,3gp,3g2,mj2") ||
        !strcmp (in_plugin->name, "avi") ||
        !strcmp (in_plugin->name, "asf") ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mp3") ||
        !strcmp (in_plugin->name, "matroska") ||
        !strcmp (in_plugin->name, "mpeg") ||
        !strcmp (in_plugin->name, "wav") ||
        !strcmp (in_plugin->name, "au") ||
        !strcmp (in_plugin->name, "tta") ||
        !strcmp (in_plugin->name, "rm") ||
        !strcmp (in_plugin->name, "amr") ||
        !strcmp (in_plugin->name, "ogg") ||
        !strcmp (in_plugin->name, "aiff") ||
        !strcmp (in_plugin->name, "ape") ||
        !strcmp (in_plugin->name, "dv") ||
        !strcmp (in_plugin->name, "flv") ||
        !strcmp (in_plugin->name, "mpc") ||
        !strcmp (in_plugin->name, "mpc8") ||
        !strcmp (in_plugin->name, "mpegts") ||
        !strcmp (in_plugin->name, "mpegtsraw") ||
        !strcmp (in_plugin->name, "nuv") ||
        !strcmp (in_plugin->name, "swf") ||
        !strcmp (in_plugin->name, "voc") || !strcmp (in_plugin->name, "gif"))
      register_typefind_func = FALSE;

    /* Set the rank of demuxers know to work to MARGINAL.
     * Set demuxers for which we already have another implementation to NONE
     * Set All others to NONE*/
    if (!strcmp (in_plugin->name, "flv") ||
        !strcmp (in_plugin->name, "wsvqa") ||
        !strcmp (in_plugin->name, "wsaud") ||
        !strcmp (in_plugin->name, "wc3movie") ||
        !strcmp (in_plugin->name, "voc") ||
        !strcmp (in_plugin->name, "tta") ||
        !strcmp (in_plugin->name, "swf") ||
        !strcmp (in_plugin->name, "sol") ||
        !strcmp (in_plugin->name, "smk") ||
        !strcmp (in_plugin->name, "vmd") ||
        !strcmp (in_plugin->name, "film_cpk") ||
        !strcmp (in_plugin->name, "ingenient") ||
        !strcmp (in_plugin->name, "psxstr") ||
        !strcmp (in_plugin->name, "nuv") ||
        !strcmp (in_plugin->name, "nut") ||
        !strcmp (in_plugin->name, "nsv") ||
        !strcmp (in_plugin->name, "mxf") ||
        !strcmp (in_plugin->name, "mmf") ||
        !strcmp (in_plugin->name, "mm") ||
        !strcmp (in_plugin->name, "ipmovie") ||
        !strcmp (in_plugin->name, "ape") ||
        !strcmp (in_plugin->name, "RoQ") ||
        !strcmp (in_plugin->name, "idcin") ||
        !strcmp (in_plugin->name, "gxf") ||
        !strcmp (in_plugin->name, "ffm") ||
        !strcmp (in_plugin->name, "ea") ||
        !strcmp (in_plugin->name, "daud") ||
        !strcmp (in_plugin->name, "avs") ||
        !strcmp (in_plugin->name, "aiff") ||
        !strcmp (in_plugin->name, "4xm") ||
        !strcmp (in_plugin->name, "yuv4mpegpipe") ||
        !strcmp (in_plugin->name, "mpc") || !strcmp (in_plugin->name, "gif"))
      rank = GST_RANK_MARGINAL;
    else
      rank = GST_RANK_NONE;

    p = name = g_strdup (in_plugin->name);
    while (*p) {
      if (*p == '.' || *p == ',')
        *p = '_';
      p++;
    }

    /* Try to find the caps that belongs here */
    sinkcaps = gst_ffmpeg_formatid_to_caps (name);
    if (!sinkcaps) {
      GST_WARNING ("Couldn't get sinkcaps for demuxer %s", in_plugin->name);
      goto next;
    }
    /* This is a bit ugly, but we just take all formats
     * for the pad template. We'll get an exact match
     * when we open the stream */
    audiosrccaps = gst_caps_new_any ();
    videosrccaps = gst_caps_new_any ();

    /* construct the type */
    type_name = g_strdup_printf ("ffdemux_%s", name);

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      gst_caps_unref (videosrccaps);
      gst_caps_unref (audiosrccaps);
      g_free (type_name);
      goto next;
    }

    typefind_name = g_strdup_printf ("fftype_%s", name);

    /* create a cache for these properties */
    params = g_new0 (GstFFMpegDemuxClassParams, 1);
    params->in_plugin = in_plugin;
    params->sinkcaps = sinkcaps;
    params->videosrccaps = videosrccaps;
    params->audiosrccaps = audiosrccaps;

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
    g_type_set_qdata (type, GST_FFDEMUX_PARAMS_QDATA, (gpointer) params);

    if (in_plugin->extensions)
      extensions = g_strsplit (in_plugin->extensions, " ", 0);
    else
      extensions = NULL;

    if (!gst_element_register (plugin, type_name, rank, type) ||
        (register_typefind_func == TRUE &&
            !gst_type_find_register (plugin, typefind_name, rank,
                gst_ffmpegdemux_type_find, extensions, sinkcaps, params,
                NULL))) {
      g_warning ("Register of type ffdemux_%s failed", name);
      g_free (type_name);
      g_free (typefind_name);
      return FALSE;
    }

    g_free (type_name);
    g_free (typefind_name);
    if (extensions)
      g_strfreev (extensions);

  next:
    g_free (name);
    in_plugin = in_plugin->next;
  }

  GST_LOG ("Finished registering demuxers");

  return TRUE;
}
