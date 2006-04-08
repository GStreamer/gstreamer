/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>,
 *               <2006> Edward Hervey <bilboed@bilboed.com>
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
#include <avi.h>
#else
#include <ffmpeg/avformat.h>
#include <ffmpeg/avi.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegDemux GstFFMpegDemux;

struct _GstFFMpegDemux
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *sinkpad;

  AVFormatContext *context;
  gboolean opened;

  GstPad *srcpads[MAX_STREAMS];
  gboolean handled[MAX_STREAMS];
  guint64 last_ts[MAX_STREAMS];
  gint videopads, audiopads;

  /* Id of the first video stream */
  gint	videostreamid;

  /* time of the first media frame */
  gint64	timeoffset;

  /* segment stuff */
  /* TODO : replace with GstSegment */
  gdouble	segment_rate;
  GstSeekFlags	segment_flags;
  /* GST_FORMAT_TIME */
  gint64	segment_start;
  gint64	segment_stop;

  GstEvent	*seek_event;
  gint64	seek_start;
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

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void gst_ffmpegdemux_class_init (GstFFMpegDemuxClass * klass);
static void gst_ffmpegdemux_base_init (GstFFMpegDemuxClass * klass);
static void gst_ffmpegdemux_init (GstFFMpegDemux * demux);

static void gst_ffmpegdemux_loop (GstPad * pad);
static gboolean
gst_ffmpegdemux_sink_activate (GstPad * sinkpad);
static gboolean
gst_ffmpegdemux_sink_activate_pull (GstPad * sinkpad, gboolean active);

static gboolean
gst_ffmpegdemux_src_convert (GstPad * pad,
			     GstFormat src_fmt,
			     gint64 src_value, GstFormat * dest_fmt, gint64 * dest_value);
static GstStateChangeReturn
gst_ffmpegdemux_change_state (GstElement * element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static const gchar *
gst_ffmpegdemux_averror (gint av_errno)
{
  const gchar *message = NULL;

  switch (av_errno) {
    default:
    case AVERROR_UNKNOWN:
      message = "Unknown error";
      break;
    case AVERROR_IO:
      message = "Input/output error";
      break;
    case AVERROR_NUMEXPECTED:
      message = "Number syntax expected in filename";
      break;
    case AVERROR_INVALIDDATA:
      message = "Invalid data found";
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
  }

  return message;
}

static void
gst_ffmpegdemux_base_init (GstFFMpegDemuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegDemuxClassParams *params;
  GstElementDetails details;
  GstPadTemplate *sinktempl, *audiosrctempl, *videosrctempl;

  params = g_hash_table_lookup (global_plugins,
      GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  if (!params)
    params = g_hash_table_lookup (global_plugins, GINT_TO_POINTER (0));
  g_assert (params);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s demuxer",
      params->in_plugin->long_name);
  details.klass = "Codec/Demuxer";
  details.description = g_strdup_printf ("FFMPEG %s demuxer",
      params->in_plugin->long_name);
  details.author = "Wim Taymans <wim.taymans@chello.be>, "
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
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->opened = FALSE;
  demux->context = NULL;

  for (n = 0; n < MAX_STREAMS; n++) {
    demux->srcpads[n] = NULL;
    demux->handled[n] = FALSE;
    demux->last_ts[n] = GST_CLOCK_TIME_NONE;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  demux->videostreamid = -1;
  demux->timeoffset = 0;

  demux->segment_rate = 1.0;
  demux->segment_flags = 0;
  demux->segment_start = -1;
  demux->segment_stop = -1;
  demux->seek_event = NULL;
  demux->seek_start = 0;
}

static void
gst_ffmpegdemux_close (GstFFMpegDemux * demux)
{
  gint n;

  if (!demux->opened)
    return;

  /* remove pads from ourselves */
  for (n = 0; n < MAX_STREAMS; n++) {
    if (demux->srcpads[n]) {
      gst_element_remove_pad (GST_ELEMENT (demux), demux->srcpads[n]);
      demux->srcpads[n] = NULL;
    }
    demux->handled[n] = FALSE;
    demux->last_ts[n] = GST_CLOCK_TIME_NONE;
  }
  demux->videopads = 0;
  demux->audiopads = 0;

  demux->videostreamid = -1;
  demux->timeoffset = 0;

  demux->segment_rate = 1.0;
  demux->segment_flags = 0;
  demux->segment_start = -1;
  demux->segment_stop = -1;
  demux->seek_event = NULL;
  demux->seek_start = 0;

  /* close demuxer context from ffmpeg */
  av_close_input_file (demux->context);
  demux->context = NULL;

  demux->opened = FALSE;
}

static AVStream *
gst_ffmpegdemux_stream_from_pad (GstPad * pad)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);
  AVStream *stream = NULL;
  gint n;

  for (n = 0; n < MAX_STREAMS; n++) {
    if (demux->srcpads[n] == pad) {
      stream = demux->context->streams[n];
      break;
    }
  }

  gst_object_unref (demux);
  return stream;
}

static gboolean
gst_ffmpegdemux_handle_seek (GstFFMpegDemux * demux, gboolean update)
{
  gboolean	flush, keyframe;
  guint		stream;

  GST_DEBUG_OBJECT (demux, "update:%d", update);

  flush = demux->segment_flags & GST_SEEK_FLAG_FLUSH;
  keyframe = demux->segment_flags & GST_SEEK_FLAG_KEY_UNIT;

  if (flush) {
    GST_LOG_OBJECT (demux, "sending flush_start");
    for (stream = 0; stream < demux->context->nb_streams; stream++) {
      gst_pad_push_event (demux->srcpads[stream],
			  gst_event_new_flush_start());
    }
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_start ());
  } else {
    GST_LOG_OBJECT (demux, "pausing task");
    gst_pad_pause_task (demux->sinkpad);
  }

  GST_PAD_STREAM_LOCK (demux->sinkpad);

  GST_DEBUG_OBJECT (demux, "after PAD_STREAM_LOCK");

  /* by default, the seek position is the segment_start */
  demux->seek_start = demux->segment_start;

  /* if index is available, find previous keyframe */
  if ((demux->videostreamid != -1) && (demux->context->index_built)) {
    gint keyframeidx;

    GST_LOG_OBJECT (demux, "looking for keyframe in ffmpeg for time %lld",
		    demux->segment_start / (GST_SECOND / AV_TIME_BASE));
    keyframeidx = av_index_search_timestamp 
      (demux->context->streams[demux->videostreamid],
       demux->segment_start / (GST_SECOND / AV_TIME_BASE),
       AVSEEK_FLAG_BACKWARD);
    GST_LOG_OBJECT (demux, "keyframeidx:%d", keyframeidx);
    if (keyframeidx >= 0) {
      gint64 idxtimestamp = demux->context->streams[demux->videostreamid]->index_entries[keyframeidx].timestamp;
      GST_LOG_OBJECT (demux, "Found a keyframe at ffmpeg idx:%d timestamp :%lld",
		      keyframeidx, idxtimestamp);
      demux->seek_start = idxtimestamp * (GST_SECOND / AV_TIME_BASE);
    }
  } else {
    GST_LOG_OBJECT (demux, "no videostream or index not built");
  }
  if (keyframe)
    demux->segment_start = demux->seek_start;

  GST_DEBUG_OBJECT (demux, "Creating new segment (%"GST_TIME_FORMAT" / %"GST_TIME_FORMAT,
		    GST_TIME_ARGS (demux->segment_start),
		    GST_TIME_ARGS (demux->segment_stop));

  demux->seek_event = gst_event_new_new_segment (!update, demux->segment_rate, GST_FORMAT_TIME,
						 demux->segment_start, demux->segment_stop,
						 demux->segment_start);
  
  if (flush) {
    for (stream = 0; stream < demux->context->nb_streams; stream++)
      gst_pad_push_event (demux->srcpads[stream],
			  gst_event_new_flush_stop ());
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop ());
  }

  if (demux->segment_flags & GST_SEEK_FLAG_SEGMENT)
    gst_element_post_message (GST_ELEMENT (demux),
			      gst_message_new_segment_start (GST_OBJECT (demux),
							     GST_FORMAT_TIME,
							     demux->segment_start));
  
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_ffmpegdemux_loop,
		      demux->sinkpad);

  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;
}

static gboolean
gst_ffmpegdemux_src_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  gboolean res = TRUE;

  if (!stream)
    return FALSE;

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_SEEK:
    {
      GstFormat format;
      GstSeekFlags flags;
      gdouble rate;
      gint64 start, stop;
      gint64 tstart, tstop;
      gint64 duration;
      GstFormat tformat = GST_FORMAT_TIME;
      GstSeekType start_type, stop_type;
      gboolean update_start = TRUE;
      gboolean update_stop = TRUE;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      GST_DEBUG_OBJECT (demux,
          "seek format %d, flags:%d, start:%lld, stop:%lld", format,
          flags, start, stop);
      
      if (format != GST_FORMAT_TIME) {
	res &= gst_ffmpegdemux_src_convert (pad, format, start, &tformat, &tstart);
	res &= gst_ffmpegdemux_src_convert (pad, format, stop, &tformat, &tstop);
      } else {
	tstart = start;
	tstop = stop;
      }

      duration = gst_ffmpeg_time_ff_to_gst (stream->duration, stream->time_base);

      switch (start_type) {
        case GST_SEEK_TYPE_CUR:
          tstart = demux->segment_start + tstart;
          break;
        case GST_SEEK_TYPE_END:
          tstart = duration + tstart;
          break;
        case GST_SEEK_TYPE_NONE:
          tstart = demux->segment_start;
          update_start = FALSE;
          break;
        case GST_SEEK_TYPE_SET:
          break;
      }
      tstart = CLAMP (tstart, 0, duration);

      switch (stop_type) {
        case GST_SEEK_TYPE_CUR:
          tstop = demux->segment_stop + tstop;
          break;
        case GST_SEEK_TYPE_END:
          tstop = duration + tstop;
          break;
        case GST_SEEK_TYPE_NONE:
          tstop = demux->segment_stop;
          update_stop = FALSE;
          break;
        case GST_SEEK_TYPE_SET:
          break;
      }
      tstop = CLAMP (tstop, 0, duration);

      /* now store the values */
      demux->segment_rate = rate;
      demux->segment_flags = flags;
      demux->segment_start = tstart;
      demux->segment_stop = tstop;

      gst_ffmpegdemux_handle_seek (demux, update_start || update_stop);
      break;
    }
    break;
  default:
    res = FALSE;
    break;
  }
  
  
  gst_event_unref (event);	       
  return res;
}

static const GstQueryType *
gst_ffmpegdemux_src_query_list (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    0
  };

  return src_types;
}

static gboolean
gst_ffmpegdemux_src_query (GstPad * pad, GstQuery *query)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) GST_PAD_PARENT (pad);
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  gboolean res = FALSE;

  if (stream == NULL)
    return FALSE;

  switch (GST_QUERY_TYPE (query)) {
  case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64	timeposition;
      gst_query_parse_position (query, &format, NULL);
      timeposition = demux->last_ts[stream->index];
      if (!(GST_CLOCK_TIME_IS_VALID (timeposition)))
	break;

      switch (format) {
      case GST_FORMAT_TIME:
	gst_query_set_position (query, GST_FORMAT_TIME, timeposition);
	res = TRUE;
	break;
      case GST_FORMAT_DEFAULT:
	gst_query_set_position (query, GST_FORMAT_DEFAULT,
				timeposition * stream->r_frame_rate.num /
				(GST_SECOND * stream->r_frame_rate.den));
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
      gint64	timeduration;
      gst_query_parse_duration (query, &format, NULL);
      timeduration = gst_ffmpeg_time_ff_to_gst (stream->duration, stream->time_base);
      if (!(GST_CLOCK_TIME_IS_VALID (timeduration)))
	break;

      switch (format) {
      case GST_FORMAT_TIME:
	gst_query_set_duration (query, GST_FORMAT_TIME, timeduration);
	res = TRUE;
	break;
      case GST_FORMAT_DEFAULT:
	gst_query_set_duration (query, GST_FORMAT_DEFAULT,
				timeduration * stream->r_frame_rate.num /
				(GST_SECOND * stream->r_frame_rate.den));
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
  default:
    /* FIXME : ADD GST_QUERY_CONVERT */
    res = gst_pad_query_default (pad, query);
    break;
  }

  return res;
}

static gboolean
gst_ffmpegdemux_src_convert (GstPad * pad,
    GstFormat src_fmt,
    gint64 src_value, GstFormat * dest_fmt, gint64 * dest_value)
{
  /*GstFFMpegDemux *demux = (GstFFMpegDemux *) gst_pad_get_parent (pad);*/
  AVStream *stream = gst_ffmpegdemux_stream_from_pad (pad);
  gboolean res = TRUE;

  if (!stream || stream->codec->codec_type != CODEC_TYPE_VIDEO)
    return FALSE;

  switch (src_fmt) {
    case GST_FORMAT_TIME:
      switch (*dest_fmt) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * stream->r_frame_rate.num /
              (GST_SECOND * stream->r_frame_rate.den);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND * stream->r_frame_rate.num /
              stream->r_frame_rate.den;
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

static gboolean
gst_ffmpegdemux_add (GstFFMpegDemux * demux, AVStream * stream)
{
  GstFFMpegDemuxClass *oclass =
      (GstFFMpegDemuxClass *) G_OBJECT_GET_CLASS (demux);
  GstPadTemplate *templ = NULL;
  GstPad *pad;
  GstCaps *caps;
  gint num;
  gchar *padname;
  const gchar *codec;

  switch (stream->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
      templ = oclass->videosrctempl;
      num = demux->videopads++;
      demux->videostreamid = stream->index;
      break;
    case CODEC_TYPE_AUDIO:
      templ = oclass->audiosrctempl;
      num = demux->audiopads++;
      break;
    default:
      GST_WARNING ("Unknown pad type %d", stream->codec->codec_type);
      break;
  }
  if (!templ)
    return FALSE;

  /* create new pad for this stream */
  padname = g_strdup_printf (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ), num);
  pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  gst_pad_use_fixed_caps (pad);
  
  gst_pad_set_query_type_function (pad, gst_ffmpegdemux_src_query_list);
  gst_pad_set_query_function (pad, gst_ffmpegdemux_src_query);
  gst_pad_set_event_function (pad, gst_ffmpegdemux_src_event);

  /* store pad internally */
  demux->srcpads[stream->index] = pad;

  /* get caps that belongs to this stream */
  caps =
      gst_ffmpeg_codecid_to_caps (stream->codec->codec_id, stream->codec, TRUE);
  gst_pad_set_caps (pad, caps);

  gst_element_add_pad (GST_ELEMENT (demux), pad);

  /* metadata */
  if ((codec = gst_ffmpeg_get_codecid_longname (stream->codec->codec_id))) {
    GstTagList *list = gst_tag_list_new ();

    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        (stream->codec->codec_type == CODEC_TYPE_VIDEO) ?
        GST_TAG_VIDEO_CODEC : GST_TAG_AUDIO_CODEC, codec, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (demux), pad, list);
  }

  return TRUE;
}

static gchar *
my_safe_copy (gchar * input)
{
  gchar	* output;

  if (!(g_utf8_validate (input, -1, NULL))) {
    output = g_convert (input, strlen(input),
			"UTF-8", "ISO-8859-1",
			NULL, NULL, NULL);
  } else {
    output = g_strdup (input);
  }

  return output;
}

static GstTagList *
gst_ffmpegdemux_read_tags (GstFFMpegDemux * demux)
{
  GstTagList	*tlist;
  gboolean	hastag = FALSE;

  tlist = gst_tag_list_new ();

  if (*demux->context->title) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_TITLE,
		      my_safe_copy (demux->context->title),
		      NULL);
    hastag = TRUE;
  }
  if (*demux->context->author) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_ARTIST,
		      my_safe_copy (demux->context->author),
		      NULL);
    hastag = TRUE;
  }
  if (*demux->context->copyright) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_COPYRIGHT,
		      my_safe_copy (demux->context->copyright),
		      NULL);
    hastag = TRUE;
  }
  if (*demux->context->comment) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_COMMENT,
		      my_safe_copy (demux->context->comment),
		      NULL);
    hastag = TRUE;
  }
  if (*demux->context->album) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_ALBUM,
		      my_safe_copy (demux->context->album),
		      NULL);
    hastag = TRUE;
  }
  if (demux->context->track) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_TRACK_NUMBER,
		      demux->context->track,
		      NULL);
    hastag = TRUE;
  }
  if (*demux->context->genre) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_GENRE,
		      my_safe_copy (demux->context->genre),
		      NULL);
    hastag = TRUE;    
  }
  if (demux->context->year) {
    gst_tag_list_add (tlist, GST_TAG_MERGE_REPLACE,
		      GST_TAG_DATE,
		      g_date_new_dmy(1, 1, demux->context->year),
		      NULL);
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
  gint res;
  GstTagList *tags;

  /* to be sure... */
  gst_ffmpegdemux_close (demux);

  /* open via our input protocol hack */
  location = g_strdup_printf ("gstreamer://%p", demux->sinkpad);
  GST_DEBUG_OBJECT (demux, "about to call av_open_input_file %s",
		    location);
  res = av_open_input_file (&demux->context, location,
      oclass->in_plugin, 0, NULL);
  GST_DEBUG_OBJECT (demux, "av_open_input returned %d", res);
  if (res >= 0)
    res = av_find_stream_info (demux->context);
  g_free (location);
  if (res < 0) {
    GST_ELEMENT_ERROR (demux, LIBRARY, FAILED, (NULL),
        (gst_ffmpegdemux_averror (res)));
    return FALSE;
  }

  /* open_input_file() automatically reads the header. We can now map each
   * created AVStream to a GstPad to make GStreamer handle it. */
  for (res = 0; res < demux->context->nb_streams; res++) {
    gst_ffmpegdemux_add (demux, demux->context->streams[res]);
    demux->handled[res] = TRUE;
  }

  gst_element_no_more_pads (GST_ELEMENT (demux));
  

  /* grab the tags */
  tags = gst_ffmpegdemux_read_tags(demux);
  if (tags) {
    gst_element_post_message (GST_ELEMENT (demux),
			      gst_message_new_tag (GST_OBJECT (demux),
						   tags));
  }

  /* remember initial start position and shift start/stop */
  demux->timeoffset = demux->context->start_time * (GST_SECOND / AV_TIME_BASE );
  demux->segment_start = 0;
  if (demux->context->duration > 0)
    demux->segment_stop = demux->context->duration * (GST_SECOND / AV_TIME_BASE );
  else
    demux->segment_stop = GST_CLOCK_TIME_NONE;
  
  /* Send newsegment on all src pads */
  for (res = 0; res < demux->context->nb_streams; res++) {
    
    GST_DEBUG_OBJECT (demux, "sending newsegment start:%" GST_TIME_FORMAT
      " duration:%"  GST_TIME_FORMAT,
		      GST_TIME_ARGS (demux->segment_start),
		      GST_TIME_ARGS (demux->segment_stop));
    
    gst_pad_push_event(demux->srcpads[res],
		       gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
						  demux->segment_start, demux->segment_stop,
						  demux->segment_start));
  }

  demux->opened = TRUE;

  return TRUE;
}

#define GST_FFMPEG_TYPE_FIND_SIZE 4096
static void
gst_ffmpegdemux_type_find (GstTypeFind * tf, gpointer priv)
{
  guint8 *data;
  GstFFMpegDemuxClassParams *params = (GstFFMpegDemuxClassParams *) priv;
  AVInputFormat *in_plugin = params->in_plugin;
  gint res = 0;

  if (in_plugin->read_probe &&
      (data = gst_type_find_peek (tf, 0, GST_FFMPEG_TYPE_FIND_SIZE)) != NULL) {
    AVProbeData probe_data;

    probe_data.filename = "";
    probe_data.buf = data;
    probe_data.buf_size = GST_FFMPEG_TYPE_FIND_SIZE;

    res = in_plugin->read_probe (&probe_data);
    if (res > 0) {
      res = MAX (1, res * GST_TYPE_FIND_MAXIMUM / AVPROBE_SCORE_MAX);
      gst_type_find_suggest (tf, res, params->sinkcaps);
    }
  }
}

/* Task */
static void
gst_ffmpegdemux_loop (GstPad * pad)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux*) (GST_PAD_PARENT (pad));
  GstFlowReturn	ret = GST_FLOW_OK;
  gint res;
  AVPacket pkt;
  GstPad *srcpad;

  /* open file if we didn't so already */
  if (!demux->opened) {
    if (!gst_ffmpegdemux_open (demux)) {
      ret = GST_FLOW_ERROR;
      goto pause;
    }
  }

  /* pending seek */
  if (demux->seek_event) {
    gint seekret;

    GST_DEBUG_OBJECT (demux, "About to call av_seek_frame (context, %d, %lld, 0) for time %"GST_TIME_FORMAT,
		      -1,
		      gst_ffmpeg_time_gst_to_ff (demux->seek_start + demux->timeoffset, demux->context->streams[0]->time_base),
		      GST_TIME_ARGS (demux->seek_start + demux->timeoffset));
    if (((seekret = av_seek_frame 
	  (demux->context, -1,
	   gst_ffmpeg_time_gst_to_ff (demux->seek_start + demux->timeoffset, demux->context->streams[0]->time_base), 0))) < 0) {
      GST_WARNING_OBJECT (demux, "Call to av_seek_frame failed : %d", seekret);
      ret = GST_FLOW_ERROR;
      goto pause;
    }
  }

  /* read a package */
  res = av_read_frame (demux->context, &pkt);
  if (res < 0) {
    /* something went wrong... */
    GST_WARNING_OBJECT (demux, "av_read_frame returned %d", res);
    ret = GST_FLOW_ERROR;
    goto pause;
  }

  if (demux->seek_event) {
    GST_DEBUG_OBJECT (demux, "About to send pending newsegment event (%"GST_TIME_FORMAT"/%"GST_TIME_FORMAT")",
		      GST_TIME_ARGS (demux->segment_start),
		      GST_TIME_ARGS (demux->segment_stop));
    for (res = 0; res < demux->context->nb_streams; res++) {
      gst_event_ref (demux->seek_event);
      gst_pad_push_event (demux->srcpads[res], demux->seek_event);
    }
    gst_event_unref (demux->seek_event);
    demux->seek_event = NULL;
  }

  GST_DEBUG_OBJECT (demux, "pkt pts:%lld / dts:%lld / size:%d / stream_index:%d / flags:%d / duration:%d / pos:%lld",
		    pkt.pts, pkt.dts, pkt.size, pkt.stream_index, pkt.flags, pkt.duration, pkt.pos);

  /* for stream-generation-while-playing */
  if (!demux->handled[pkt.stream_index]) {
    gst_ffmpegdemux_add (demux, demux->context->streams[pkt.stream_index]);
    demux->handled[pkt.stream_index] = TRUE;
  }

  /* shortcut to pad belonging to this stream */
  srcpad = demux->srcpads[pkt.stream_index];

  if (srcpad) {
    AVStream *stream = gst_ffmpegdemux_stream_from_pad (srcpad);
    GstBuffer *outbuf;

    ret = gst_pad_alloc_buffer_and_set_caps (srcpad,
					     GST_CLOCK_TIME_NONE,
					     pkt.size,
					     GST_PAD_CAPS (srcpad),
					     &outbuf);
    if (ret != GST_FLOW_OK) {
      pkt.destruct (&pkt);
      goto pause;
    }

    memcpy (GST_BUFFER_DATA (outbuf), pkt.data, pkt.size);
    GST_BUFFER_TIMESTAMP (outbuf) =
      gst_ffmpeg_time_ff_to_gst (pkt.pts,
          demux->context->streams[pkt.stream_index]->time_base) - demux->timeoffset;
    if (GST_BUFFER_TIMESTAMP_IS_VALID (outbuf))
      demux->last_ts[stream->index] = GST_BUFFER_TIMESTAMP (outbuf);
    if (!(pkt.flags & PKT_FLAG_KEY))
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

    

    GST_DEBUG_OBJECT (demux, "Sending out buffer time:%"GST_TIME_FORMAT" size:%d offset:%lld",
		      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
		      GST_BUFFER_SIZE (outbuf),
		      GST_BUFFER_OFFSET (outbuf));

    ret = gst_pad_push (srcpad, outbuf);
    
  } else {
    GST_WARNING_OBJECT (demux, "No pad from stream %d",
			pkt.stream_index);
  }
  
  pkt.destruct (&pkt);

  return;
 pause:
  GST_LOG_OBJECT (demux, "pausing task");
  gst_pad_pause_task (demux->sinkpad);
  if (GST_FLOW_IS_FATAL (ret)) {
    int i;
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        ("Internal data stream error."),
        ("streaming stopped, reason %s", gst_flow_get_name (ret)));
    for (i = 0; i < MAX_STREAMS; i++)
      if (demux->srcpads[i])
	gst_pad_push_event (demux->srcpads[i], gst_event_new_eos());
  }
}


static gboolean
gst_ffmpegdemux_sink_activate (GstPad * sinkpad)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux*) (GST_PAD_PARENT (sinkpad));
  
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  GST_ELEMENT_ERROR (demux, STREAM, NOT_IMPLEMENTED,
        (NULL), ("failed to activate sinkpad in pull mode, push mode not implemented yet"));
  return FALSE;
}

static gboolean
gst_ffmpegdemux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_ffmpegdemux_loop, sinkpad);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

static GstStateChangeReturn
gst_ffmpegdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegDemux *demux = (GstFFMpegDemux *) (element);
  GstStateChangeReturn	ret;

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

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name, *typefind_name;
    gchar *p, *name = NULL;
    GstCaps *sinkcaps, *audiosrccaps, *videosrccaps;
    gint rank = GST_RANK_MARGINAL;
    gboolean register_typefind_func = TRUE;

    /* no emulators */
    if (!strncmp (in_plugin->long_name, "raw ", 4) ||
        !strncmp (in_plugin->long_name, "pcm ", 4) ||
        !strcmp (in_plugin->name, "audio_device") ||
        !strncmp (in_plugin->name, "image", 5) ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mjpeg"))
      goto next;

    /* these are known to be buggy or broken or not
     * tested enough to let them be autoplugged */
    if (!strcmp (in_plugin->name, "mp3") || /* = application/x-id3 */
        !strcmp (in_plugin->name, "avi") ||
        !strcmp (in_plugin->name, "ogg")) {
      rank = GST_RANK_NONE;
    }

    if (!strcmp (in_plugin->name, "mov,mp4,m4a,3gp,3g2") ||
        !strcmp (in_plugin->name, "avi") ||
        !strcmp (in_plugin->name, "asf") ||
        !strcmp (in_plugin->name, "mpegvideo") ||
        !strcmp (in_plugin->name, "mp3") ||
        !strcmp (in_plugin->name, "matroska") ||
        !strcmp (in_plugin->name, "mpeg") ||
        !strcmp (in_plugin->name, "wav") ||
        !strcmp (in_plugin->name, "au") ||
        !strcmp (in_plugin->name, "tta") ||
        !strcmp (in_plugin->name, "rm"))
      register_typefind_func = FALSE;

    p = name = g_strdup (in_plugin->name);
    while (*p) {
      if (*p == '.' || *p == ',')
        *p = '_';
      p++;
    }

    /* Try to find the caps that belongs here */
    sinkcaps = gst_ffmpeg_formatid_to_caps (name);
    if (!sinkcaps) {
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

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (0), (gpointer) params);

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (type), (gpointer) params);

    if (in_plugin->extensions)
      extensions = g_strsplit (in_plugin->extensions, " ", 0);
    else
      extensions = NULL;

    if (!gst_element_register (plugin, type_name, rank, type) ||
        (register_typefind_func == TRUE &&
         !gst_type_find_register (plugin, typefind_name, rank,
             gst_ffmpegdemux_type_find, extensions, sinkcaps, params, NULL))) {
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
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}
