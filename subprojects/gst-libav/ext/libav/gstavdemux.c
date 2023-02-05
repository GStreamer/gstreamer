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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
/* #include <ffmpeg/avi.h> */
#include <gst/gst.h>
#include <gst/base/gstflowcombiner.h>
#include <gst/audio/gstdsd.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavprotocol.h"

#define MAX_STREAMS 20

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

  GstTagList *tags;             /* stream tags */
};

struct _GstFFMpegDemux
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *sinkpad;

  gboolean have_group_id;
  guint group_id;

  AVFormatContext *context;
  gboolean opened;

  GstFFStream *streams[MAX_STREAMS];

  GstFlowCombiner *flowcombiner;

  gint videopads, audiopads;

  GstClockTime start_time;
  GstClockTime duration;

  /* TRUE if working in pull-mode */
  gboolean seekable;

  /* TRUE if the avformat demuxer can reliably handle streaming mode */
  gboolean can_push;

  gboolean flushing;

  /* segment stuff */
  GstSegment segment;

  /* cached seek in READY */
  GstEvent *seek_event;

  /* cached upstream events */
  GList *cached_events;

  /* push mode data */
  GstFFMpegPipe ffpipe;
  GstTask *task;
  GRecMutex task_lock;
};

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
static void gst_ffmpegdemux_finalize (GObject * object);

static gboolean gst_ffmpegdemux_sink_event (GstPad * sinkpad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_ffmpegdemux_chain (GstPad * sinkpad,
    GstObject * parent, GstBuffer * buf);

static void gst_ffmpegdemux_loop (GstFFMpegDemux * demux);
static gboolean gst_ffmpegdemux_sink_activate (GstPad * sinkpad,
    GstObject * parent);
static gboolean gst_ffmpegdemux_sink_activate_mode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);
static GstTagList *gst_ffmpeg_metadata_to_tag_list (AVDictionary * metadata);

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

#define GST_FFDEMUX_PARAMS_QDATA g_quark_from_static_string("avdemux-params")

static GstElementClass *parent_class = NULL;

static const gchar *
gst_ffmpegdemux_averror (gint av_errno)
{
  const gchar *message = NULL;

  switch (av_errno) {
    case AVERROR (EINVAL):
      message = "Unknown error";
      break;
    case AVERROR (EIO):
      message = "Input/output error";
      break;
    case AVERROR (EDOM):
      message = "Number syntax expected in filename";
      break;
    case AVERROR (ENOMEM):
      message = "Not enough memory";
      break;
    case AVERROR (EILSEQ):
      message = "Unknown format";
      break;
    case AVERROR (ENOSYS):
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
  AVInputFormat *in_plugin;
  GstCaps *sinkcaps;
  GstPadTemplate *sinktempl, *audiosrctempl, *videosrctempl;
  gchar *longname, *description, *name;

  in_plugin = (AVInputFormat *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), GST_FFDEMUX_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  name = g_strdup (in_plugin->name);
  g_strdelimit (name, ".,|-<> ", '_');

  /* construct the element details struct */
  longname = g_strdup_printf ("libav %s demuxer", in_plugin->long_name);
  description = g_strdup_printf ("libav %s demuxer", in_plugin->long_name);
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Demuxer", description,
      "Wim Taymans <wim@fluendo.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edward Hervey <bilboed@bilboed.com>");
  g_free (longname);
  g_free (description);

  /* pad templates */
  sinkcaps = gst_ffmpeg_formatid_to_caps (name);
  sinktempl = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  g_free (name);
  videosrctempl = gst_pad_template_new ("video_%u",
      GST_PAD_SRC, GST_PAD_SOMETIMES, GST_CAPS_ANY);
  audiosrctempl = gst_pad_template_new ("audio_%u",
      GST_PAD_SRC, GST_PAD_SOMETIMES, GST_CAPS_ANY);

  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  gst_caps_unref (sinkcaps);

  klass->in_plugin = in_plugin;
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

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ffmpegdemux_finalize);

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
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdemux_sink_activate));
  gst_pad_set_activatemode_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdemux_sink_activate_mode));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* push based setup */
  /* the following are not used in pull-based mode, so safe to set anyway */
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdemux_sink_event));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdemux_chain));
  /* task for driving ffmpeg in loop function */
  demux->task =
      gst_task_new ((GstTaskFunction) gst_ffmpegdemux_loop, demux, NULL);
  g_rec_mutex_init (&demux->task_lock);
  gst_task_set_lock (demux->task, &demux->task_lock);

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;

  demux->opened = FALSE;
  demux->context = NULL;

  for (n = 0; n < MAX_STREAMS; n++) {
    demux->streams[n] = NULL;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  demux->seek_event = NULL;
  gst_segment_init (&demux->segment, GST_FORMAT_TIME);

  demux->flowcombiner = gst_flow_combiner_new ();

  /* push based data */
  g_mutex_init (&demux->ffpipe.tlock);
  g_cond_init (&demux->ffpipe.cond);
  demux->ffpipe.adapter = gst_adapter_new ();

  /* blacklist unreliable push-based demuxers */
  if (strcmp (oclass->in_plugin->name, "ape"))
    demux->can_push = TRUE;
  else
    demux->can_push = FALSE;
}

static void
gst_ffmpegdemux_finalize (GObject * object)
{
  GstFFMpegDemux *demux;

  demux = (GstFFMpegDemux *) object;

  gst_flow_combiner_free (demux->flowcombiner);

  g_mutex_clear (&demux->ffpipe.tlock);
  g_cond_clear (&demux->ffpipe.cond);
  gst_object_unref (demux->ffpipe.adapter);

  gst_object_unref (demux->task);
  g_rec_mutex_clear (&demux->task_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
    if (stream) {
      if (stream->pad) {
        gst_flow_combiner_remove_pad (demux->flowcombiner, stream->pad);
        gst_element_remove_pad (GST_ELEMENT (demux), stream->pad);
      }
      if (stream->tags)
        gst_tag_list_unref (stream->tags);
      g_free (stream);
    }
    demux->streams[n] = NULL;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  /* close demuxer context from ffmpeg */
  if (demux->seekable)
    gst_ffmpegdata_close (demux->context->pb);
  else
    gst_ffmpeg_pipe_close (demux->context->pb);
  demux->context->pb = NULL;
  avformat_close_input (&demux->context);
  if (demux->context)
    avformat_free_context (demux->context);
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
      GST_DEBUG ("stream %d %p eos:%d", n, s, s->eos);
      if (!s->eos)
        return FALSE;
    }
  }
  return TRUE;
}

/* Returns True if we at least outputted one buffer */
static gboolean
gst_ffmpegdemux_has_outputted (GstFFMpegDemux * demux)
{
  GstFFStream *s;
  gint n;

  for (n = 0; n < MAX_STREAMS; n++) {
    if ((s = demux->streams[n])) {
      if (GST_CLOCK_TIME_IS_VALID (s->last_ts))
        return TRUE;
    }
  }
  return FALSE;
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
  target = segment->position + demux->start_time;
  /* convert target to ffmpeg time */
  fftarget = gst_ffmpeg_time_gst_to_ff (target, stream->time_base);

  GST_LOG_OBJECT (demux, "do seek to time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (target));

  /* if we need to land on a keyframe, try to do so, we don't try to do a 
   * keyframe seek if we are not absolutely sure we have an index.*/
  if (segment->flags & GST_SEEK_FLAG_KEY_UNIT) {
    gint keyframeidx;

    GST_LOG_OBJECT (demux, "looking for keyframe in ffmpeg for time %"
        GST_TIME_FORMAT, GST_TIME_ARGS (target));

    /* search in the index for the previous keyframe */
    keyframeidx =
        av_index_search_timestamp (stream, fftarget, AVSEEK_FLAG_BACKWARD);

    GST_LOG_OBJECT (demux, "keyframeidx: %d", keyframeidx);

    if (keyframeidx >= 0) {
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(58,78,0)
      fftarget = avformat_index_get_entry (stream, keyframeidx)->timestamp;
#else
      fftarget = stream->index_entries[keyframeidx].timestamp;
#endif
      target = gst_ffmpeg_time_ff_to_gst (fftarget, stream->time_base);

      GST_LOG_OBJECT (demux,
          "Found a keyframe at ffmpeg idx: %d timestamp :%" GST_TIME_FORMAT,
          keyframeidx, GST_TIME_ARGS (target));
    }
  }

  GST_DEBUG_OBJECT (demux,
      "About to call av_seek_frame (context, %d, %" G_GINT64_FORMAT
      ", 0) for time %" GST_TIME_FORMAT, index, fftarget,
      GST_TIME_ARGS (target));

  if ((seekret =
          av_seek_frame (demux->context, index, fftarget,
              AVSEEK_FLAG_BACKWARD)) < 0)
    goto seek_failed;

  GST_DEBUG_OBJECT (demux, "seek success, returned %d", seekret);

  if (target > demux->start_time)
    target -= demux->start_time;
  else
    target = 0;

  segment->position = target;
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

  if (!demux->seekable) {
    GST_DEBUG_OBJECT (demux, "in push mode; ignoring seek");
    return FALSE;
  }

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
        res = gst_pad_query_convert (demux->sinkpad, format, cur, fmt, &cur);
      if (res && stop_type != GST_SEEK_TYPE_NONE && stop != -1)
        res = gst_pad_query_convert (demux->sinkpad, format, stop, fmt, &stop);
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
    gst_segment_do_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (demux, "segment configured from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT ", position %" G_GINT64_FORMAT,
      seeksegment.start, seeksegment.stop, seeksegment.position);

  /* make the sinkpad available for data passing since we might need
   * it when doing the seek */
  if (flush) {
    GST_OBJECT_LOCK (demux);
    demux->flushing = FALSE;
    GST_OBJECT_UNLOCK (demux);
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop (TRUE));
  }

  /* do the seek, segment.position contains new position. */
  res = gst_ffmpegdemux_do_seek (demux, &seeksegment);

  /* and prepare to continue streaming */
  if (flush) {
    /* send flush stop, peer will accept data and events again. We
     * are not yet providing data as we still have the STREAM_LOCK. */
    gst_ffmpegdemux_push_event (demux, gst_event_new_flush_stop (TRUE));
  }
  /* if successfull seek, we update our real segment and push
   * out the new segment. */
  if (res) {
    memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));

    if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_segment_start (GST_OBJECT (demux),
              demux->segment.format, demux->segment.position));
    }

    /* now send the newsegment, FIXME, do this from the streaming thread */
    GST_DEBUG_OBJECT (demux, "Sending newsegment %" GST_SEGMENT_FORMAT,
        &demux->segment);

    gst_ffmpegdemux_push_event (demux, gst_event_new_segment (&demux->segment));
  }

  /* Mark discont on all srcpads and remove eos */
  gst_ffmpegdemux_set_flags (demux, TRUE, FALSE);
  gst_flow_combiner_reset (demux->flowcombiner);

  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_ffmpegdemux_loop,
      demux->sinkpad, NULL);

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
gst_ffmpegdemux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegDemux *demux;
  gboolean res = TRUE;

  demux = (GstFFMpegDemux *) parent;

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

  return res;
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
gst_ffmpegdemux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFFMpegDemux *demux;
  GstFFStream *stream;
  AVStream *avstream;
  gboolean res = FALSE;

  if (!(stream = gst_pad_get_element_private (pad)))
    return FALSE;

  avstream = stream->avstream;

  demux = (GstFFMpegDemux *) parent;

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
              gst_util_uint64_scale (timeposition, avstream->avg_frame_rate.num,
                  GST_SECOND * avstream->avg_frame_rate.den));
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          if (demux->videopads + demux->audiopads == 1 &&
              GST_PAD_PEER (demux->sinkpad) != NULL)
            res = gst_pad_query_default (pad, parent, query);
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
              gst_util_uint64_scale (timeduration, avstream->avg_frame_rate.num,
                  GST_SECOND * avstream->avg_frame_rate.den));
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          if (demux->videopads + demux->audiopads == 1 &&
              GST_PAD_PEER (demux->sinkpad) != NULL)
            res = gst_pad_query_default (pad, parent, query);
          break;
        default:
          break;
      }
    }
      break;
    case GST_QUERY_SEEKING:{
      GstFormat format;
      gboolean seekable;
      gint64 dur = -1;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      seekable = demux->seekable;
      if (!gst_pad_query_duration (pad, format, &dur)) {
        /* unlikely that we don't know duration but can seek */
        seekable = FALSE;
        dur = -1;
      }
      gst_query_set_seeking (query, format, seekable, 0, dur);
      res = TRUE;
      break;
    }
    case GST_QUERY_SEGMENT:{
      GstFormat format;
      gint64 start, stop;

      format = demux->segment.format;

      start =
          gst_segment_to_stream_time (&demux->segment, format,
          demux->segment.start);
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;
      else
        stop = gst_segment_to_stream_time (&demux->segment, format, stop);

      gst_query_set_segment (query, demux->segment.rate, format, start, stop);
      res = TRUE;
      break;
    }
    default:
      /* FIXME : ADD GST_QUERY_CONVERT */
      res = gst_pad_query_default (pad, parent, query);
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
  if (avstream->codec->codec_type != AVMEDIA_TYPE_VIDEO)
    return FALSE;

  switch (src_fmt) {
    case GST_FORMAT_TIME:
      switch (*dest_fmt) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale (src_value,
              avstream->avg_frame_rate.num,
              GST_SECOND * avstream->avg_frame_rate.den);
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
              GST_SECOND * avstream->avg_frame_rate.num,
              avstream->avg_frame_rate.den);
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

static gchar *
gst_ffmpegdemux_create_padname (const gchar * templ, gint n)
{
  GString *string;

  /* FIXME, we just want to printf the number according to the template but
   * then the format string is not a literal and we can't check arguments and
   * this generates a compiler error */
  string = g_string_new (templ);
  g_string_truncate (string, string->len - 2);
  g_string_append_printf (string, "%u", n);

  return g_string_free (string, FALSE);
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
  AVCodecContext *ctx = NULL;
  GstFFStream *stream;
  GstEvent *event;
  gchar *stream_id;

  oclass = (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);

  if (demux->streams[avstream->index] != NULL)
    goto exists;

  ctx = avcodec_alloc_context3 (NULL);
  avcodec_parameters_to_context (ctx, avstream->codecpar);

  /* create new stream */
  stream = g_new0 (GstFFStream, 1);
  demux->streams[avstream->index] = stream;

  /* mark stream as unknown */
  stream->unknown = TRUE;
  stream->discont = TRUE;
  stream->avstream = avstream;
  stream->last_ts = GST_CLOCK_TIME_NONE;
  stream->tags = NULL;

  switch (ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      templ = oclass->videosrctempl;
      num = demux->videopads++;
      /* These are not part of the codec parameters we built the
       * context from */
      ctx->framerate.num = avstream->r_frame_rate.num;
      ctx->framerate.den = avstream->r_frame_rate.den;
      break;
    case AVMEDIA_TYPE_AUDIO:
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
  padname =
      gst_ffmpegdemux_create_padname (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ),
      num);
  pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  gst_pad_use_fixed_caps (pad);
  gst_pad_set_active (pad, TRUE);

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


  stream_id =
      gst_pad_create_stream_id_printf (pad, GST_ELEMENT_CAST (demux), "%03u",
      avstream->index);

  event = gst_pad_get_sticky_event (demux->sinkpad, GST_EVENT_STREAM_START, 0);
  if (event) {
    if (gst_event_parse_group_id (event, &demux->group_id))
      demux->have_group_id = TRUE;
    else
      demux->have_group_id = FALSE;
    gst_event_unref (event);
  } else if (!demux->have_group_id) {
    demux->have_group_id = TRUE;
    demux->group_id = gst_util_group_id_next ();
  }
  event = gst_event_new_stream_start (stream_id);
  if (demux->have_group_id)
    gst_event_set_group_id (event, demux->group_id);

  gst_pad_push_event (pad, event);
  g_free (stream_id);

  GST_INFO_OBJECT (pad, "adding pad with caps %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (pad, caps);
  gst_caps_unref (caps);

  /* activate and add */
  gst_element_add_pad (GST_ELEMENT (demux), pad);
  gst_flow_combiner_add_pad (demux->flowcombiner, pad);

  /* metadata */
  if ((codec = gst_ffmpeg_get_codecid_longname (ctx->codec_id))) {
    stream->tags = gst_ffmpeg_metadata_to_tag_list (avstream->metadata);

    if (stream->tags == NULL)
      stream->tags = gst_tag_list_new_empty ();

    gst_tag_list_add (stream->tags, GST_TAG_MERGE_REPLACE,
        (ctx->codec_type == AVMEDIA_TYPE_VIDEO) ?
        GST_TAG_VIDEO_CODEC : GST_TAG_AUDIO_CODEC, codec, NULL);
  }

done:
  if (ctx)
    avcodec_free_context (&ctx);
  return stream;

  /* ERRORS */
exists:
  {
    GST_DEBUG_OBJECT (demux, "Pad existed (stream %d)", avstream->index);
    stream = demux->streams[avstream->index];
    goto done;
  }
unknown_type:
  {
    GST_WARNING_OBJECT (demux, "Unknown pad type %d", ctx->codec_type);
    goto done;
  }
unknown_caps:
  {
    GST_WARNING_OBJECT (demux, "Unknown caps for codec %d", ctx->codec_id);
    goto done;
  }
}

static gchar *
safe_utf8_copy (gchar * input)
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

/* This is a list of standard tag keys taken from the avformat.h
 * header, without handling any variants. */
static const struct
{
  const gchar *ffmpeg_tag_name;
  const gchar *gst_tag_name;
} tagmapping[] = {
  {
      "album", GST_TAG_ALBUM}, {
      "album_artist", GST_TAG_ALBUM_ARTIST}, {
      "artist", GST_TAG_ARTIST}, {
      "comment", GST_TAG_COMMENT}, {
      "composer", GST_TAG_COMPOSER}, {
      "copyright", GST_TAG_COPYRIGHT},
  /* Need to convert ISO 8601 to GstDateTime: */
  {
      "creation_time", GST_TAG_DATE_TIME},
  /* Need to convert ISO 8601 to GDateTime: */
  {
      "date", GST_TAG_DATE_TIME}, {
      "disc", GST_TAG_ALBUM_VOLUME_NUMBER}, {
      "encoder", GST_TAG_ENCODER}, {
      "encoded_by", GST_TAG_ENCODED_BY}, {
      "genre", GST_TAG_GENRE}, {
      "language", GST_TAG_LANGUAGE_CODE}, {
      "performer", GST_TAG_PERFORMER}, {
      "publisher", GST_TAG_PUBLISHER}, {
      "title", GST_TAG_TITLE}, {
      "track", GST_TAG_TRACK_NUMBER}
};

static const gchar *
match_tag_name (gchar * ffmpeg_tag_name)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (tagmapping); i++) {
    if (!g_strcmp0 (tagmapping[i].ffmpeg_tag_name, ffmpeg_tag_name))
      return tagmapping[i].gst_tag_name;
  }
  return NULL;
}

static GstTagList *
gst_ffmpeg_metadata_to_tag_list (AVDictionary * metadata)
{
  AVDictionaryEntry *tag = NULL;
  GstTagList *list;
  list = gst_tag_list_new_empty ();

  while ((tag = av_dict_get (metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
    const gchar *gsttag = match_tag_name (tag->key);
    GType t;
    GST_LOG ("mapping tag %s=%s\n", tag->key, tag->value);
    if (gsttag == NULL) {
      GST_LOG ("Ignoring unknown metadata tag %s", tag->key);
      continue;
    }
    /* Special case, track and disc numbers may be x/n in libav, split
     * them */
    if (g_str_equal (gsttag, GST_TAG_TRACK_NUMBER)) {
      guint track, trackcount;
      if (sscanf (tag->value, "%u/%u", &track, &trackcount) == 2) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            gsttag, track, GST_TAG_TRACK_COUNT, trackcount, NULL);
        continue;
      }
      /* Fall through and handle as a single uint below */
    } else if (g_str_equal (gsttag, GST_TAG_ALBUM_VOLUME_NUMBER)) {
      guint disc, disc_count;
      if (sscanf (tag->value, "%u/%u", &disc, &disc_count) == 2) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            gsttag, disc, GST_TAG_ALBUM_VOLUME_COUNT, disc_count, NULL);
        continue;
      }
      /* Fall through and handle as a single uint below */
    }

    t = gst_tag_get_type (gsttag);
    if (t == G_TYPE_STRING) {
      gchar *s = safe_utf8_copy (tag->value);
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, gsttag, s, NULL);
      g_free (s);
    } else if (t == G_TYPE_UINT || t == G_TYPE_INT) {
      gchar *end;
      gint v = strtol (tag->value, &end, 10);
      if (end == tag->value)
        continue;               /* Failed to parse */
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, gsttag, v, NULL);
    } else if (t == G_TYPE_DATE) {
      guint year, month, day;
      GDate *date = NULL;
      if (sscanf (tag->value, "%04u-%02u-%02u", &year, &month, &day) == 3) {
        date = g_date_new_dmy (day, month, year);
      } else {
        /* Try interpreting just as a year */
        gchar *end;

        year = strtol (tag->value, &end, 10);
        if (end != tag->value)
          date = g_date_new_dmy (1, 1, year);
      }
      if (date) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, gsttag, date, NULL);
        g_date_free (date);
      }
    } else if (t == GST_TYPE_DATE_TIME) {
      gchar *s = safe_utf8_copy (tag->value);
      GstDateTime *d = gst_date_time_new_from_iso8601_string (s);

      g_free (s);
      if (d) {
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, gsttag, d, NULL);
        gst_date_time_unref (d);
      }
    } else {
      GST_FIXME ("Unhandled tag %s", gsttag);
    }
  }

  if (gst_tag_list_is_empty (list)) {
    gst_tag_list_unref (list);
    return NULL;
  }

  return list;
}

static gboolean
gst_ffmpegdemux_open (GstFFMpegDemux * demux)
{
  AVIOContext *iocontext = NULL;
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);
  gint res, n_streams, i;
  GstTagList *tags;
  GstEvent *event;
  GList *cached_events;
  GstQuery *query;
  gchar *uri = NULL;

  /* to be sure... */
  gst_ffmpegdemux_close (demux);

  /* open via our input protocol hack */
  if (demux->seekable)
    res = gst_ffmpegdata_open (demux->sinkpad, AVIO_FLAG_READ, &iocontext);
  else
    res = gst_ffmpeg_pipe_open (&demux->ffpipe, AVIO_FLAG_READ, &iocontext);

  if (res < 0)
    goto beach;

  query = gst_query_new_uri ();
  if (gst_pad_peer_query (demux->sinkpad, query)) {
    gchar *query_uri, *redirect_uri;
    gboolean permanent;

    gst_query_parse_uri (query, &query_uri);
    gst_query_parse_uri_redirection (query, &redirect_uri);
    gst_query_parse_uri_redirection_permanent (query, &permanent);

    if (permanent && redirect_uri) {
      uri = redirect_uri;
      g_free (query_uri);
    } else {
      uri = query_uri;
      g_free (redirect_uri);
    }
  }
  gst_query_unref (query);

  GST_DEBUG_OBJECT (demux, "Opening context with URI %s", GST_STR_NULL (uri));

  demux->context = avformat_alloc_context ();
  demux->context->pb = iocontext;
  res = avformat_open_input (&demux->context, uri, oclass->in_plugin, NULL);

  g_free (uri);

  GST_DEBUG_OBJECT (demux, "av_open_input returned %d", res);
  if (res < 0)
    goto beach;

  res = gst_ffmpeg_av_find_stream_info (demux->context);
  GST_DEBUG_OBJECT (demux, "av_find_stream_info returned %d", res);
  if (res < 0)
    goto beach;

  n_streams = demux->context->nb_streams;
  GST_DEBUG_OBJECT (demux, "we have %d streams", n_streams);

  /* open_input_file() automatically reads the header. We can now map each
   * created AVStream to a GstPad to make GStreamer handle it. */
  for (i = 0; i < n_streams; i++) {
    gst_ffmpegdemux_get_stream (demux, demux->context->streams[i]);
  }

  gst_element_no_more_pads (GST_ELEMENT (demux));

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
  demux->segment.duration = demux->duration;

  GST_OBJECT_LOCK (demux);
  demux->opened = TRUE;
  event = demux->seek_event;
  demux->seek_event = NULL;
  cached_events = demux->cached_events;
  demux->cached_events = NULL;
  GST_OBJECT_UNLOCK (demux);

  if (event) {
    gst_ffmpegdemux_perform_seek (demux, event);
    gst_event_unref (event);
  } else {
    GST_DEBUG_OBJECT (demux, "Sending segment %" GST_SEGMENT_FORMAT,
        &demux->segment);
    gst_ffmpegdemux_push_event (demux, gst_event_new_segment (&demux->segment));
  }

  while (cached_events) {
    event = cached_events->data;
    GST_INFO_OBJECT (demux, "pushing cached event: %" GST_PTR_FORMAT, event);
    gst_ffmpegdemux_push_event (demux, event);
    cached_events = g_list_delete_link (cached_events, cached_events);
  }

  /* grab the global tags */
  tags = gst_ffmpeg_metadata_to_tag_list (demux->context->metadata);
  if (tags) {
    GST_INFO_OBJECT (demux, "global tags: %" GST_PTR_FORMAT, tags);
  }

  /* now handle the stream tags */
  for (i = 0; i < n_streams; i++) {
    GstFFStream *stream;

    stream = gst_ffmpegdemux_get_stream (demux, demux->context->streams[i]);
    if (stream->pad != NULL) {

      /* Global tags */
      if (tags)
        gst_pad_push_event (stream->pad,
            gst_event_new_tag (gst_tag_list_ref (tags)));

      /* Per-stream tags */
      if (stream->tags != NULL) {
        GST_INFO_OBJECT (stream->pad, "stream tags: %" GST_PTR_FORMAT,
            stream->tags);
        gst_pad_push_event (stream->pad,
            gst_event_new_tag (gst_tag_list_ref (stream->tags)));
      }
    }
  }
  if (tags)
    gst_tag_list_unref (tags);
  return TRUE;

  /* ERRORS */
beach:
  {
    GST_ELEMENT_ERROR (demux, LIBRARY, FAILED, (NULL),
        ("%s", gst_ffmpegdemux_averror (res)));
    return FALSE;
  }
}

#define GST_FFMPEG_TYPE_FIND_SIZE 4096
#define GST_FFMPEG_TYPE_FIND_MIN_SIZE 256

static void
gst_ffmpegdemux_type_find (GstTypeFind * tf, gpointer priv)
{
  const guint8 *data;
  AVInputFormat *in_plugin = (AVInputFormat *) priv;
  gint res = 0;
  guint64 length;
  GstCaps *sinkcaps;

  /* We want GST_FFMPEG_TYPE_FIND_SIZE bytes, but if the file is shorter than
   * that we'll give it a try... */
  length = gst_type_find_get_length (tf);
  if (length == 0 || length > GST_FFMPEG_TYPE_FIND_SIZE)
    length = GST_FFMPEG_TYPE_FIND_SIZE;

  /* The ffmpeg typefinders assume there's a certain minimum amount of data
   * and will happily do invalid memory access if there isn't, so let's just
   * skip the ffmpeg typefinders if the data available is too short
   * (in which case it's unlikely to be a media file anyway) */
  if (length < GST_FFMPEG_TYPE_FIND_MIN_SIZE) {
    GST_LOG ("not typefinding %" G_GUINT64_FORMAT " bytes, too short", length);
    return;
  }

  GST_LOG ("typefinding %" G_GUINT64_FORMAT " bytes", length);
  if (in_plugin->read_probe &&
      (data = gst_type_find_peek (tf, 0, length)) != NULL) {
    AVProbeData probe_data;

    probe_data.filename = "";
    probe_data.buf = (guint8 *) data;
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

      sinkcaps = gst_ffmpeg_formatid_to_caps (in_plugin->name);

      GST_LOG ("libav typefinder '%s' suggests %" GST_PTR_FORMAT ", p=%u%%",
          in_plugin->name, sinkcaps, res);

      gst_type_find_suggest (tf, res, sinkcaps);
      gst_caps_unref (sinkcaps);
    }
  }
}

/* Task */
static void
gst_ffmpegdemux_loop (GstFFMpegDemux * demux)
{
  GstFlowReturn ret;
  gint res = -1;
  AVPacket pkt;
  GstPad *srcpad;
  GstFFStream *stream;
  AVStream *avstream;
  GstBuffer *outbuf = NULL;
  GstClockTime timestamp, duration;
  gint outsize;
  gboolean rawvideo;
  GstFlowReturn stream_last_flow;
  gint64 pts;

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
  pts = pkt.pts;
  if (G_UNLIKELY (pts < 0)) {
    /* some streams have pts such this:
     * 0
     * -2
     * -1
     * 1
     *
     * we reset pts to 0 since for us timestamp are unsigned
     */
    GST_WARNING_OBJECT (demux,
        "negative pts detected: %" G_GINT64_FORMAT " resetting to 0", pts);
    pts = 0;
  }
  timestamp = gst_ffmpeg_time_ff_to_gst (pts, avstream->time_base);
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
      " / pos:%" G_GINT64_FORMAT, GST_TIME_ARGS (timestamp), pkt.size,
      pkt.stream_index, pkt.flags, GST_TIME_ARGS (duration), (gint64) pkt.pos);

  /* check start_time */
#if 0
  if (demux->start_time != -1 && demux->start_time > timestamp)
    goto drop;
#endif

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    /* start_time should be the ts of the first frame but it may actually be
     * higher because of rounding when converting to gst ts. */
    if (demux->start_time >= timestamp)
      timestamp = 0;
    else
      timestamp -= demux->start_time;
  }

  /* check if we ran outside of the segment */
  if (demux->segment.stop != -1 && timestamp > demux->segment.stop)
    goto drop;

  /* prepare to push packet to peer */
  srcpad = stream->pad;

  rawvideo = (avstream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
      avstream->codecpar->codec_id == AV_CODEC_ID_RAWVIDEO);

  if (rawvideo)
    outsize = gst_ffmpeg_avpicture_get_size (avstream->codecpar->format,
        avstream->codecpar->width, avstream->codecpar->height);
  else
    outsize = pkt.size;

  outbuf = gst_buffer_new_and_alloc (outsize);

  /* copy the data from packet into the target buffer
   * and do conversions for raw video packets */
  if (rawvideo) {
    AVFrame src, dst;
    const gchar *plugin_name =
        ((GstFFMpegDemuxClass *) (G_OBJECT_GET_CLASS (demux)))->in_plugin->name;
    GstMapInfo map;

    GST_WARNING ("Unknown demuxer %s, no idea what to do", plugin_name);
    gst_ffmpeg_avpicture_fill (&src, pkt.data,
        avstream->codecpar->format, avstream->codecpar->width,
        avstream->codecpar->height);

    gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
    gst_ffmpeg_avpicture_fill (&dst, map.data,
        avstream->codecpar->format, avstream->codecpar->width,
        avstream->codecpar->height);

    av_image_copy (dst.data, dst.linesize, (const uint8_t **) src.data,
        src.linesize, avstream->codecpar->format, avstream->codecpar->width,
        avstream->codecpar->height);
    gst_buffer_unmap (outbuf, &map);
  } else {
    gst_buffer_fill (outbuf, 0, pkt.data, outsize);
  }

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;

  /* mark keyframes */
  if (!(pkt.flags & AV_PKT_FLAG_KEY)) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  /* Mark discont */
  if (stream->discont) {
    GST_DEBUG_OBJECT (demux, "marking DISCONT");
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }

  /* If we are demuxing planar DSD data, add the necessary
   * meta to inform downstream about the planar layout. */
  switch (avstream->codecpar->codec_id) {
    case AV_CODEC_ID_DSD_LSBF_PLANAR:
    case AV_CODEC_ID_DSD_MSBF_PLANAR:
    {
      int channel_idx;
      int num_channels = avstream->codecpar->channels;
      int num_bytes_per_channel = pkt.size / num_channels;
      GstDsdPlaneOffsetMeta *plane_ofs_meta;

      plane_ofs_meta = gst_buffer_add_dsd_plane_offset_meta (outbuf,
          avstream->codecpar->channels, num_bytes_per_channel, NULL);

      for (channel_idx = 0; channel_idx < num_channels; ++channel_idx) {
        plane_ofs_meta->offsets[channel_idx] =
            num_bytes_per_channel * channel_idx;
      }

      break;
    }

    default:
      break;
  }

  GST_DEBUG_OBJECT (demux,
      "Sending out buffer time:%" GST_TIME_FORMAT " size:%" G_GSIZE_FORMAT,
      GST_TIME_ARGS (timestamp), gst_buffer_get_size (outbuf));

  ret = stream_last_flow = gst_pad_push (srcpad, outbuf);

  /* if a pad is in e.g. WRONG_STATE, we want to pause to unlock the STREAM_LOCK */
  if (((ret = gst_flow_combiner_update_flow (demux->flowcombiner,
                  ret)) != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (demux, "stream_movi flow: %s / %s",
        gst_flow_get_name (stream_last_flow), gst_flow_get_name (ret));
    goto pause;
  }

done:
  /* can destroy the packet now */
  if (res == 0) {
    av_packet_unref (&pkt);
  }

  return;

  /* ERRORS */
pause:
  {
    GST_LOG_OBJECT (demux, "pausing task, reason %d (%s)", ret,
        gst_flow_get_name (ret));
    if (demux->seekable)
      gst_pad_pause_task (demux->sinkpad);
    else {
      GstFFMpegPipe *ffpipe = &demux->ffpipe;

      GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);
      /* pause task and make sure loop stops */
      gst_task_pause (demux->task);
      g_rec_mutex_lock (&demux->task_lock);
      g_rec_mutex_unlock (&demux->task_lock);
      demux->ffpipe.srcresult = ret;
      GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);
    }

    if (ret == GST_FLOW_EOS) {
      if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gint64 stop;

        if ((stop = demux->segment.stop) == -1)
          stop = demux->segment.duration;

        GST_LOG_OBJECT (demux, "posting segment done");
        gst_element_post_message (GST_ELEMENT (demux),
            gst_message_new_segment_done (GST_OBJECT (demux),
                demux->segment.format, stop));
        gst_ffmpegdemux_push_event (demux,
            gst_event_new_segment_done (demux->segment.format, stop));
      } else {
        GST_LOG_OBJECT (demux, "pushing eos");
        gst_ffmpegdemux_push_event (demux, gst_event_new_eos ());
      }
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      GST_ELEMENT_FLOW_ERROR (demux, ret);
      gst_ffmpegdemux_push_event (demux, gst_event_new_eos ());
    }
    goto done;
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
      ret = GST_FLOW_FLUSHING;
    else if (gst_ffmpegdemux_has_outputted (demux)
        || gst_ffmpegdemux_is_eos (demux)) {
      GST_DEBUG_OBJECT (demux, "We are EOS");
      ret = GST_FLOW_EOS;
    } else
      ret = GST_FLOW_ERROR;
    GST_OBJECT_UNLOCK (demux);

    goto pause;
  }
drop:
  {
    GST_DEBUG_OBJECT (demux, "dropping buffer out of segment, stream eos");
    stream->eos = TRUE;
    if (gst_ffmpegdemux_is_eos (demux)) {
      av_packet_unref (&pkt);
      GST_DEBUG_OBJECT (demux, "we are eos");
      ret = GST_FLOW_EOS;
      goto pause;
    } else {
      GST_DEBUG_OBJECT (demux, "some streams are not yet eos");
      goto done;
    }
  }
}


static gboolean
gst_ffmpegdemux_sink_event (GstPad * sinkpad, GstObject * parent,
    GstEvent * event)
{
  GstFFMpegDemux *demux;
  GstFFMpegPipe *ffpipe;
  gboolean result = TRUE;

  demux = (GstFFMpegDemux *) parent;
  ffpipe = &(demux->ffpipe);

  GST_LOG_OBJECT (demux, "event: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* forward event */
      gst_pad_event_default (sinkpad, parent, event);

      /* now unblock the chain function */
      GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);
      ffpipe->srcresult = GST_FLOW_FLUSHING;
      GST_FFMPEG_PIPE_SIGNAL (ffpipe);
      GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

      /* loop might run into WRONG_STATE and end itself,
       * but may also be waiting in a ffmpeg read
       * trying to break that would make ffmpeg believe eos,
       * so no harm to have the loop 'pausing' there ... */
      goto done;
    case GST_EVENT_FLUSH_STOP:
      /* forward event */
      gst_pad_event_default (sinkpad, parent, event);

      GST_OBJECT_LOCK (demux);
      g_list_foreach (demux->cached_events, (GFunc) gst_mini_object_unref,
          NULL);
      g_list_free (demux->cached_events);
      GST_OBJECT_UNLOCK (demux);
      GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);
      gst_adapter_clear (ffpipe->adapter);
      ffpipe->srcresult = GST_FLOW_OK;
      /* loop may have decided to end itself as a result of flush WRONG_STATE */
      gst_task_start (demux->task);
      demux->flushing = FALSE;
      GST_LOG_OBJECT (demux, "loop started");
      GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);
      goto done;
    case GST_EVENT_EOS:
      /* inform the src task that it can stop now */
      GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);
      ffpipe->eos = TRUE;
      GST_FFMPEG_PIPE_SIGNAL (ffpipe);
      GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

      /* eat this event for now, task will send eos when finished */
      gst_event_unref (event);
      goto done;
    case GST_EVENT_STREAM_START:
    case GST_EVENT_CAPS:
    case GST_EVENT_SEGMENT:
      GST_LOG_OBJECT (demux, "dropping %s event", GST_EVENT_TYPE_NAME (event));
      gst_event_unref (event);
      goto done;
    default:
      /* for a serialized event, wait until an earlier data is gone,
       * though this is no guarantee as to when task is done with it.
       *
       * If the demuxer isn't opened, push straight away, since we'll
       * be waiting against a cond that will never be signalled. */
      if (GST_EVENT_IS_SERIALIZED (event)) {
        if (demux->opened) {
          GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);
          while (!ffpipe->needed)
            GST_FFMPEG_PIPE_WAIT (ffpipe);
          GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);
        } else {
          /* queue events and send them later (esp. tag events) */
          GST_OBJECT_LOCK (demux);
          demux->cached_events = g_list_append (demux->cached_events, event);
          GST_OBJECT_UNLOCK (demux);
          goto done;
        }
      }
      break;
  }

  result = gst_pad_event_default (sinkpad, parent, event);

done:

  return result;
}

static GstFlowReturn
gst_ffmpegdemux_chain (GstPad * sinkpad, GstObject * parent, GstBuffer * buffer)
{
  GstFFMpegDemux *demux;
  GstFFMpegPipe *ffpipe;

  demux = (GstFFMpegDemux *) parent;
  ffpipe = &demux->ffpipe;

  GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);

  if (G_UNLIKELY (ffpipe->eos))
    goto eos;

  if (G_UNLIKELY (ffpipe->srcresult != GST_FLOW_OK))
    goto ignore;

  GST_DEBUG ("Giving a buffer of %" G_GSIZE_FORMAT " bytes",
      gst_buffer_get_size (buffer));
  gst_adapter_push (ffpipe->adapter, buffer);
  buffer = NULL;
  while (gst_adapter_available (ffpipe->adapter) >= ffpipe->needed) {
    GST_DEBUG ("Adapter has more that requested (ffpipe->needed:%d)",
        ffpipe->needed);
    GST_FFMPEG_PIPE_SIGNAL (ffpipe);
    GST_FFMPEG_PIPE_WAIT (ffpipe);
    /* may have become flushing */
    if (G_UNLIKELY (ffpipe->srcresult != GST_FLOW_OK))
      goto ignore;
  }

  GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

  return GST_FLOW_OK;

/* special cases */
eos:
  {
    GST_DEBUG_OBJECT (demux, "ignoring buffer at end-of-stream");
    GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }
ignore:
  {
    GST_DEBUG_OBJECT (demux, "ignoring buffer because src task encountered %s",
        gst_flow_get_name (ffpipe->srcresult));
    GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

    if (buffer)
      gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }
}

static gboolean
gst_ffmpegdemux_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode;
  GstSchedulingFlags flags;

  query = gst_query_new_scheduling ();

  if (!gst_pad_peer_query (sinkpad, query)) {
    gst_query_unref (query);
    goto activate_push;
  }

  pull_mode = gst_query_has_scheduling_mode_with_flags (query,
      GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);

  gst_query_parse_scheduling (query, &flags, NULL, NULL, NULL);
  if (flags & GST_SCHEDULING_FLAG_SEQUENTIAL)
    pull_mode = FALSE;

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

/* push mode:
 * - not seekable
 * - use gstpipe protocol, like ffmpeg's pipe protocol
 * - (independently managed) task driving ffmpeg
 */
static gboolean
gst_ffmpegdemux_sink_activate_push (GstPad * sinkpad, GstObject * parent,
    gboolean active)
{
  GstFFMpegDemux *demux;
  gboolean res = FALSE;

  demux = (GstFFMpegDemux *) (parent);

  if (active) {
    if (demux->can_push == FALSE) {
      GST_WARNING_OBJECT (demux, "Demuxer can't reliably operate in push-mode");
      goto beach;
    }
    demux->ffpipe.eos = FALSE;
    demux->ffpipe.srcresult = GST_FLOW_OK;
    demux->ffpipe.needed = 0;
    demux->seekable = FALSE;
    res = gst_task_start (demux->task);
  } else {
    GstFFMpegPipe *ffpipe = &demux->ffpipe;

    /* release chain and loop */
    GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);
    demux->ffpipe.srcresult = GST_FLOW_FLUSHING;
    /* end streaming by making ffmpeg believe eos */
    demux->ffpipe.eos = TRUE;
    GST_FFMPEG_PIPE_SIGNAL (ffpipe);
    GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

    /* make sure streaming ends */
    gst_task_stop (demux->task);
    g_rec_mutex_lock (&demux->task_lock);
    g_rec_mutex_unlock (&demux->task_lock);
    res = gst_task_join (demux->task);
    demux->seekable = FALSE;
  }

beach:
  return res;
}

/* pull mode:
 * - seekable
 * - use gstreamer protocol, like ffmpeg's file protocol
 * - task driving ffmpeg based on sink pad
 */
static gboolean
gst_ffmpegdemux_sink_activate_pull (GstPad * sinkpad, GstObject * parent,
    gboolean active)
{
  GstFFMpegDemux *demux;
  gboolean res;

  demux = (GstFFMpegDemux *) parent;

  if (active) {
    demux->seekable = TRUE;
    res = gst_pad_start_task (sinkpad, (GstTaskFunction) gst_ffmpegdemux_loop,
        demux, NULL);
  } else {
    res = gst_pad_stop_task (sinkpad);
    demux->seekable = FALSE;
  }

  return res;
}

static gboolean
gst_ffmpegdemux_sink_activate_mode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean res;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      res = gst_ffmpegdemux_sink_activate_push (sinkpad, parent, active);
      break;
    case GST_PAD_MODE_PULL:
      res = gst_ffmpegdemux_sink_activate_pull (sinkpad, parent, active);
      break;
    default:
      res = FALSE;
      break;
  }
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
      gst_adapter_clear (demux->ffpipe.adapter);
      g_list_foreach (demux->cached_events, (GFunc) gst_mini_object_unref,
          NULL);
      g_list_free (demux->cached_events);
      demux->cached_events = NULL;
      demux->have_group_id = FALSE;
      demux->group_id = G_MAXUINT;
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
  const AVInputFormat *in_plugin;
  gchar *extensions;
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

  void *i = 0;

  GST_LOG ("Registering demuxers");

  while ((in_plugin = av_demuxer_iterate (&i))) {
    gchar *type_name, *typefind_name;
    gint rank;
    gboolean register_typefind_func = TRUE;

    GST_LOG ("Attempting to handle libav demuxer plugin %s [%s]",
        in_plugin->name, in_plugin->long_name);

    /* no emulators */
    if (in_plugin->long_name != NULL) {
      if (!strncmp (in_plugin->long_name, "raw ", 4) ||
          !strncmp (in_plugin->long_name, "pcm ", 4)
          )
        continue;
    }

    if (!strcmp (in_plugin->name, "audio_device") ||
        !strncmp (in_plugin->name, "image", 5) ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mjpeg") ||
        !strcmp (in_plugin->name, "redir") ||
        !strncmp (in_plugin->name, "u8", 2) ||
        !strncmp (in_plugin->name, "u16", 3) ||
        !strncmp (in_plugin->name, "u24", 3) ||
        !strncmp (in_plugin->name, "u32", 3) ||
        !strncmp (in_plugin->name, "s8", 2) ||
        !strncmp (in_plugin->name, "s16", 3) ||
        !strncmp (in_plugin->name, "s24", 3) ||
        !strncmp (in_plugin->name, "s32", 3) ||
        !strncmp (in_plugin->name, "f32", 3) ||
        !strncmp (in_plugin->name, "f64", 3) ||
        !strcmp (in_plugin->name, "mulaw") || !strcmp (in_plugin->name, "alaw")
        )
      continue;

    /* no network demuxers */
    if (!strcmp (in_plugin->name, "sdp") ||
        !strcmp (in_plugin->name, "rtsp") ||
        !strcmp (in_plugin->name, "applehttp")
        )
      continue;

    /* these don't do what one would expect or
     * are only partially functional/useful */
    if (!strcmp (in_plugin->name, "aac") ||
        !strcmp (in_plugin->name, "wv") ||
        !strcmp (in_plugin->name, "ass") ||
        !strcmp (in_plugin->name, "ffmetadata"))
      continue;

    /* Don't use the typefind functions of formats for which we already have
     * better typefind functions */
    if (!strcmp (in_plugin->name, "mov,mp4,m4a,3gp,3g2,mj2") ||
        !strcmp (in_plugin->name, "ass") ||
        !strcmp (in_plugin->name, "avi") ||
        !strcmp (in_plugin->name, "asf") ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mp3") ||
        !strcmp (in_plugin->name, "matroska") ||
        !strcmp (in_plugin->name, "matroska_webm") ||
        !strcmp (in_plugin->name, "matroska,webm") ||
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
        !strcmp (in_plugin->name, "mxf") ||
        !strcmp (in_plugin->name, "nuv") ||
        !strcmp (in_plugin->name, "swf") ||
        !strcmp (in_plugin->name, "voc") ||
        !strcmp (in_plugin->name, "pva") ||
        !strcmp (in_plugin->name, "gif") ||
        !strcmp (in_plugin->name, "vc1test") ||
        !strcmp (in_plugin->name, "ivf"))
      register_typefind_func = FALSE;

    /* Set the rank of demuxers known to work to MARGINAL.
     * Set demuxers for which we already have another implementation to NONE
     * Set All others to NONE*/
    /**
     * element-avdemux_xwma
     *
     * Since: 1.20
     */
    if (!strcmp (in_plugin->name, "wsvqa") ||
        !strcmp (in_plugin->name, "wsaud") ||
        !strcmp (in_plugin->name, "wc3movie") ||
        !strcmp (in_plugin->name, "voc") ||
        !strcmp (in_plugin->name, "tta") ||
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
        !strcmp (in_plugin->name, "xwma") ||
        !strcmp (in_plugin->name, "4xm") ||
        !strcmp (in_plugin->name, "yuv4mpegpipe") ||
        !strcmp (in_plugin->name, "pva") ||
        !strcmp (in_plugin->name, "mpc") ||
        !strcmp (in_plugin->name, "mpc8") ||
        !strcmp (in_plugin->name, "ivf") ||
        !strcmp (in_plugin->name, "brstm") ||
        !strcmp (in_plugin->name, "bfstm") ||
        !strcmp (in_plugin->name, "gif") ||
        !strcmp (in_plugin->name, "dsf") || !strcmp (in_plugin->name, "iff"))
      rank = GST_RANK_MARGINAL;
    else {
      GST_DEBUG ("ignoring %s", in_plugin->name);
      rank = GST_RANK_NONE;
      continue;
    }

    /* construct the type */
    type_name = g_strdup_printf ("avdemux_%s", in_plugin->name);
    g_strdelimit (type_name, ".,|-<> ", '_');

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      continue;
    }

    typefind_name = g_strdup_printf ("avtype_%s", in_plugin->name);
    g_strdelimit (typefind_name, ".,|-<> ", '_');

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
    g_type_set_qdata (type, GST_FFDEMUX_PARAMS_QDATA, (gpointer) in_plugin);

    if (in_plugin->extensions)
      extensions = g_strdelimit (g_strdup (in_plugin->extensions), " ", ',');
    else
      extensions = NULL;

    if (!gst_element_register (plugin, type_name, rank, type) ||
        (register_typefind_func == TRUE &&
            !gst_type_find_register (plugin, typefind_name, rank,
                gst_ffmpegdemux_type_find, extensions, NULL,
                (gpointer) in_plugin, NULL))) {
      g_warning ("Registration of type %s failed", type_name);
      g_free (type_name);
      g_free (typefind_name);
      g_free (extensions);
      return FALSE;
    }

    g_free (type_name);
    g_free (typefind_name);
    g_free (extensions);
  }

  GST_LOG ("Finished registering demuxers");

  return TRUE;
}
