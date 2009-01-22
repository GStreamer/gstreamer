/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
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
 * SECTION:element-qtdemux
 *
 * <refsect2>
 * <para>
 * Demuxes a .mov file into raw or compressed audio and/or video streams.
 * </para>
 * <para>
 * This element supports both push and pull-based scheduling, depending on the
 * capabilities of the upstream elements.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=test.mov ! qtdemux name=demux  demux.audio_00 ! decodebin ! audioconvert ! audioresample ! autoaudiosink   demux.video_00 ! queue ! decodebin ! ffmpegcolorspace ! videoscale ! autovideosink
 * </programlisting>
 * Play (parse and decode) a .mov file and try to output it to
 * an automatically detected soundcard and videosink. If the MOV file contains
 * compressed audio or video data, this will only work if you have the
 * right decoder elements/plugins installed.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-12-29 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include <gst/tag/tag.h>

#include "qtdemux_types.h"
#include "qtdemux_dump.h"
#include "qtdemux_fourcc.h"
#include "qtdemux.h"
#include "qtpalette.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ZLIB
# include <zlib.h>
#endif

GST_DEBUG_CATEGORY (qtdemux_debug);

#if 0
#define qtdemux_dump_mem(a,b)  gst_util_dump_mem(a,b)
#else
#define qtdemux_dump_mem(a,b)   /* */
#endif

typedef struct _QtNode QtNode;
typedef struct _QtDemuxSegment QtDemuxSegment;
typedef struct _QtDemuxSample QtDemuxSample;

struct _QtNode
{
  guint32 type;
  guint8 *data;
  gint len;
};

struct _QtDemuxSample
{
  guint32 chunk;
  guint32 size;
  guint64 offset;
  GstClockTimeDiff pts_offset;  /* Add this value to timestamp to get the pts */
  guint64 timestamp;            /* In GstClockTime */
  guint64 duration;             /* in GstClockTime */
  gboolean keyframe;            /* TRUE when this packet is a keyframe */
};

/*
 * Quicktime has tracks and segments. A track is a continuous piece of
 * multimedia content. The track is not always played from start to finish but
 * instead, pieces of the track are 'cut out' and played in sequence. This is
 * what the segments do.
 *
 * Inside the track we have keyframes (K) and delta frames. The track has its
 * own timing, which starts from 0 and extends to end. The position in the track
 * is called the media_time.
 *
 * The segments now describe the pieces that should be played from this track
 * and are basically tupples of media_time/duration/rate entries. We can have
 * multiple segments and they are all played after one another. An example:
 *
 * segment 1: media_time: 1 second, duration: 1 second, rate 1
 * segment 2: media_time: 3 second, duration: 2 second, rate 2
 *
 * To correctly play back this track, one must play: 1 second of media starting
 * from media_time 1 followed by 2 seconds of media starting from media_time 3
 * at a rate of 2.
 *
 * Each of the segments will be played at a specific time, the first segment at
 * time 0, the second one after the duration of the first one, etc.. Note that
 * the time in resulting playback is not identical to the media_time of the
 * track anymore.
 *
 * Visually, assuming the track has 4 second of media_time:
 *
 *                (a)                   (b)          (c)              (d)
 *         .-----------------------------------------------------------.
 * track:  | K.....K.........K........K.......K.......K...........K... |
 *         '-----------------------------------------------------------'
 *         0              1              2              3              4    
 *           .------------^              ^   .----------^              ^
 *          /              .-------------'  /       .------------------'
 *         /              /          .-----'       /
 *         .--------------.         .--------------.
 *         | segment 1    |         | segment 2    |
 *         '--------------'         '--------------'
 *       
 * The challenge here is to cut out the right pieces of the track for each of
 * the playback segments. This fortunatly can easily be done with the SEGMENT
 * events of gstreamer.
 *
 * For playback of segment 1, we need to provide the decoder with the keyframe
 * (a), in the above figure, but we must instruct it only to output the decoded
 * data between second 1 and 2. We do this with a SEGMENT event for 1 to 2, time
 * position set to the time of the segment: 0.
 *
 * We then proceed to push data from keyframe (a) to frame (b). The decoder
 * decodes but clips all before media_time 1.
 * 
 * After finishing a segment, we push out a new SEGMENT event with the clipping
 * boundaries of the new data.
 *
 * This is a good usecase for the GStreamer accumulated SEGMENT events.
 */

struct _QtDemuxSegment
{
  /* global time and duration, all gst time */
  guint64 time;
  guint64 stop_time;
  guint64 duration;
  /* media time of trak, all gst time */
  guint64 media_start;
  guint64 media_stop;
  gdouble rate;
};

struct _QtDemuxStream
{
  GstPad *pad;

  /* stream type */
  guint32 subtype;
  GstCaps *caps;
  guint32 fourcc;

  /* duration/scale */
  guint64 duration;             /* in timescale */
  guint32 timescale;

  /* our samples */
  guint32 n_samples;
  QtDemuxSample *samples;
  gboolean all_keyframe;        /* TRUE when all samples are keyframes (no stss) */
  guint32 min_duration;         /* duration in timescale of first sample, used for figuring out
                                   the framerate, in timescale units */

  /* if we use chunks or samples */
  gboolean sampled;
  guint padding;

  /* video info */
  gint width;
  gint height;
  /* Numerator/denominator framerate */
  gint fps_n;
  gint fps_d;
  guint16 bits_per_sample;
  guint16 color_table_id;

  /* audio info */
  gdouble rate;
  gint n_channels;
  guint samples_per_packet;
  guint samples_per_frame;
  guint bytes_per_packet;
  guint bytes_per_sample;
  guint bytes_per_frame;
  guint compression;

  /* when a discontinuity is pending */
  gboolean discont;

  /* list of buffers to push first */
  GSList *buffers;

  /* if we need to clip this buffer. This is only needed for uncompressed
   * data */
  gboolean need_clip;

  /* current position */
  guint32 segment_index;
  guint32 sample_index;
  guint64 time_position;        /* in gst time */

  /* the Gst segment we are processing out, used for clipping */
  GstSegment segment;

  /* last GstFlowReturn */
  GstFlowReturn last_ret;

  /* quicktime segments */
  guint32 n_segments;
  QtDemuxSegment *segments;
  guint32 from_sample;
  guint32 to_sample;
};

enum QtDemuxState
{
  QTDEMUX_STATE_INITIAL,        /* Initial state (haven't got the header yet) */
  QTDEMUX_STATE_HEADER,         /* Parsing the header */
  QTDEMUX_STATE_MOVIE,          /* Parsing/Playing the media data */
  QTDEMUX_STATE_BUFFER_MDAT     /* Buffering the mdat atom */
};

static GNode *qtdemux_tree_get_child_by_type (GNode * node, guint32 fourcc);
static GNode *qtdemux_tree_get_sibling_by_type (GNode * node, guint32 fourcc);

static const GstElementDetails gst_qtdemux_details =
GST_ELEMENT_DETAILS ("QuickTime demuxer",
    "Codec/Demuxer",
    "Demultiplex a QuickTime file into audio and video streams",
    "David Schleef <ds@schleef.org>, Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_qtdemux_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/quicktime; video/mj2; audio/x-m4a; "
        "application/x-3gp")
    );

static GstStaticPadTemplate gst_qtdemux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_qtdemux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstElementClass *parent_class = NULL;

static void gst_qtdemux_class_init (GstQTDemuxClass * klass);
static void gst_qtdemux_base_init (GstQTDemuxClass * klass);
static void gst_qtdemux_init (GstQTDemux * quicktime_demux);
static void gst_qtdemux_dispose (GObject * object);

static GstStateChangeReturn gst_qtdemux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean qtdemux_sink_activate (GstPad * sinkpad);
static gboolean qtdemux_sink_activate_pull (GstPad * sinkpad, gboolean active);
static gboolean qtdemux_sink_activate_push (GstPad * sinkpad, gboolean active);

static void gst_qtdemux_loop (GstPad * pad);
static GstFlowReturn gst_qtdemux_chain (GstPad * sinkpad, GstBuffer * inbuf);
static gboolean gst_qtdemux_handle_sink_event (GstPad * pad, GstEvent * event);

static gboolean qtdemux_parse_moov (GstQTDemux * qtdemux, guint8 * buffer,
    int length);
static gboolean qtdemux_parse_node (GstQTDemux * qtdemux, GNode * node,
    guint8 * buffer, int length);
static gboolean qtdemux_parse_tree (GstQTDemux * qtdemux);

static void gst_qtdemux_handle_esds (GstQTDemux * qtdemux,
    QtDemuxStream * stream, GNode * esds, GstTagList * list);
static GstCaps *qtdemux_video_caps (GstQTDemux * qtdemux,
    QtDemuxStream * stream, guint32 fourcc, const guint8 * stsd_data,
    gchar ** codec_name);
static GstCaps *qtdemux_audio_caps (GstQTDemux * qtdemux,
    QtDemuxStream * stream, guint32 fourcc, const guint8 * data, int len,
    gchar ** codec_name);

GType
gst_qtdemux_get_type (void)
{
  static GType qtdemux_type = 0;

  if (!qtdemux_type) {
    static const GTypeInfo qtdemux_info = {
      sizeof (GstQTDemuxClass),
      (GBaseInitFunc) gst_qtdemux_base_init, NULL,
      (GClassInitFunc) gst_qtdemux_class_init,
      NULL, NULL, sizeof (GstQTDemux), 0,
      (GInstanceInitFunc) gst_qtdemux_init,
    };

    qtdemux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstQTDemux", &qtdemux_info,
        0);
  }
  return qtdemux_type;
}

static void
gst_qtdemux_base_init (GstQTDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qtdemux_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qtdemux_videosrc_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qtdemux_audiosrc_template));
  gst_element_class_set_details (element_class, &gst_qtdemux_details);

  GST_DEBUG_CATEGORY_INIT (qtdemux_debug, "qtdemux", 0, "qtdemux plugin");
}

static void
gst_qtdemux_class_init (GstQTDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_qtdemux_dispose;

  gstelement_class->change_state = gst_qtdemux_change_state;
}

static void
gst_qtdemux_init (GstQTDemux * qtdemux)
{
  qtdemux->sinkpad =
      gst_pad_new_from_static_template (&gst_qtdemux_sink_template, "sink");
  gst_pad_set_activate_function (qtdemux->sinkpad, qtdemux_sink_activate);
  gst_pad_set_activatepull_function (qtdemux->sinkpad,
      qtdemux_sink_activate_pull);
  gst_pad_set_activatepush_function (qtdemux->sinkpad,
      qtdemux_sink_activate_push);
  gst_pad_set_chain_function (qtdemux->sinkpad, gst_qtdemux_chain);
  gst_pad_set_event_function (qtdemux->sinkpad, gst_qtdemux_handle_sink_event);
  gst_element_add_pad (GST_ELEMENT_CAST (qtdemux), qtdemux->sinkpad);

  qtdemux->state = QTDEMUX_STATE_INITIAL;
  /* FIXME, use segment last_stop for this */
  qtdemux->last_ts = GST_CLOCK_TIME_NONE;
  qtdemux->pullbased = FALSE;
  qtdemux->neededbytes = 16;
  qtdemux->todrop = 0;
  qtdemux->adapter = gst_adapter_new ();
  qtdemux->offset = 0;
  qtdemux->mdatoffset = GST_CLOCK_TIME_NONE;
  qtdemux->mdatbuffer = NULL;
  gst_segment_init (&qtdemux->segment, GST_FORMAT_TIME);
}

static void
gst_qtdemux_dispose (GObject * object)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (object);

  if (qtdemux->adapter) {
    g_object_unref (G_OBJECT (qtdemux->adapter));
    qtdemux->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

#if 0
static gboolean
gst_qtdemux_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  QtDemuxStream *stream = gst_pad_get_element_private (pad);

  if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e') &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}
#endif

static const GstQueryType *
gst_qtdemux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return src_types;
}

static gboolean
gst_qtdemux_get_duration (GstQTDemux * qtdemux, gint64 * duration)
{
  gboolean res = TRUE;

  *duration = GST_CLOCK_TIME_NONE;

  if (qtdemux->duration != 0) {
    if (qtdemux->duration != G_MAXINT32 && qtdemux->timescale != 0) {
      *duration = gst_util_uint64_scale (qtdemux->duration,
          GST_SECOND, qtdemux->timescale);
    }
  }
  return res;
}

static gboolean
gst_qtdemux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstQTDemux *qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      if (GST_CLOCK_TIME_IS_VALID (qtdemux->segment.last_stop)) {
        gst_query_set_position (query, GST_FORMAT_TIME,
            qtdemux->segment.last_stop);
        res = TRUE;
      }
      break;
    case GST_QUERY_DURATION:{
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME) {
        gint64 duration = -1;

        gst_qtdemux_get_duration (qtdemux, &duration);
        if (duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          res = TRUE;
        }
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        gint64 duration = -1;

        gst_qtdemux_get_duration (qtdemux, &duration);
        gst_query_set_seeking (query, GST_FORMAT_TIME, qtdemux->pullbased,
            0, duration);
        res = TRUE;
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (qtdemux);

  return res;
}

/* push event on all source pads; takes ownership of the event */
static void
gst_qtdemux_push_event (GstQTDemux * qtdemux, GstEvent * event)
{
  guint n;

  GST_DEBUG_OBJECT (qtdemux, "pushing %s event on all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (n = 0; n < qtdemux->n_streams; n++) {
    GstPad *pad;

    if ((pad = qtdemux->streams[n]->pad))
      gst_pad_push_event (pad, gst_event_ref (event));
  }
  gst_event_unref (event);
}

/* push a pending newsegment event, if any from the streaming thread */
static void
gst_qtdemux_push_pending_newsegment (GstQTDemux * qtdemux)
{
  if (qtdemux->pending_newsegment) {
    gst_qtdemux_push_event (qtdemux, qtdemux->pending_newsegment);
    qtdemux->pending_newsegment = NULL;
  }
}

/* find the index of the sample that includes the data for @media_time
 *
 * Returns the index of the sample or n_samples when the sample was not
 * found.
 */
/* FIXME, binary search would be nice here */
static guint32
gst_qtdemux_find_index (GstQTDemux * qtdemux, QtDemuxStream * str,
    guint64 media_time)
{
  guint32 i;

  if (str->n_samples == 0)
    return 0;

  for (i = 0; i < str->n_samples; i++) {
    if (str->samples[i].timestamp > media_time) {
      /* first sample after media_time, we need the previous one */
      return (i == 0 ? 0 : i - 1);
    }
  }
  return str->n_samples - 1;
}

/* find the index of the keyframe needed to decode the sample at @index
 * of stream @str.
 *
 * Returns the index of the keyframe.
 */
static guint32
gst_qtdemux_find_keyframe (GstQTDemux * qtdemux, QtDemuxStream * str,
    guint32 index)
{
  guint32 new_index = index;

  if (index >= str->n_samples) {
    new_index = str->n_samples;
    goto beach;
  }

  /* all keyframes, return index */
  if (str->all_keyframe) {
    new_index = index;
    goto beach;
  }

  /* else go back until we have a keyframe */
  while (TRUE) {
    if (str->samples[new_index].keyframe)
      break;

    if (new_index == 0)
      break;

    new_index--;
  }

beach:
  GST_DEBUG_OBJECT (qtdemux, "searching for keyframe index before index %u "
      "gave %u", index, new_index);

  return new_index;
}

/* find the segment for @time_position for @stream
 *
 * Returns -1 if the segment cannot be found.
 */
static guint32
gst_qtdemux_find_segment (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint64 time_position)
{
  gint i;
  guint32 seg_idx;

  GST_LOG_OBJECT (qtdemux, "finding segment for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time_position));

  /* find segment corresponding to time_position if we are looking
   * for a segment. */
  seg_idx = -1;
  for (i = 0; i < stream->n_segments; i++) {
    QtDemuxSegment *segment = &stream->segments[i];

    GST_LOG_OBJECT (qtdemux,
        "looking at segment %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
        GST_TIME_ARGS (segment->time), GST_TIME_ARGS (segment->stop_time));

    /* For the last segment we include stop_time in the last segment */
    if (i < stream->n_segments - 1) {
      if (segment->time <= time_position && time_position < segment->stop_time) {
        GST_LOG_OBJECT (qtdemux, "segment %d matches", i);
        seg_idx = i;
        break;
      }
    } else {
      if (segment->time <= time_position && time_position <= segment->stop_time) {
        GST_LOG_OBJECT (qtdemux, "segment %d matches", i);
        seg_idx = i;
        break;
      }
    }
  }
  return seg_idx;
}

/* move the stream @str to the sample position @index.
 *
 * Updates @str->sample_index and marks discontinuity if needed.
 */
static void
gst_qtdemux_move_stream (GstQTDemux * qtdemux, QtDemuxStream * str,
    guint32 index)
{
  /* no change needed */
  if (index == str->sample_index)
    return;

  GST_DEBUG_OBJECT (qtdemux, "moving to sample %u of %u", index,
      str->n_samples);

  /* position changed, we have a discont */
  str->sample_index = index;
  /* Each time we move in the stream we store the position where we are 
   * starting from */
  str->from_sample = index;
  str->discont = TRUE;
}

/* perform the seek.
 *
 * We set all segment_indexes in the streams to unknown and
 * adjust the time_position to the desired position. this is enough
 * to trigger a segment switch in the streaming thread to start
 * streaming from the desired position.
 *
 * Keyframe seeking is a little more complicated when dealing with
 * segments. Ideally we want to move to the previous keyframe in
 * the segment but there might not be a keyframe in the segment. In
 * fact, none of the segments could contain a keyframe. We take a
 * practical approach: seek to the previous keyframe in the segment,
 * if there is none, seek to the beginning of the segment.
 *
 * Called with STREAM_LOCK
 */
static gboolean
gst_qtdemux_perform_seek (GstQTDemux * qtdemux, GstSegment * segment)
{
  gint64 desired_offset;
  gint n;

  desired_offset = segment->last_stop;

  GST_DEBUG_OBJECT (qtdemux, "seeking to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (desired_offset));

  if (segment->flags & GST_SEEK_FLAG_KEY_UNIT) {
    guint64 min_offset;

    min_offset = desired_offset;

    /* for each stream, find the index of the sample in the segment
     * and move back to the previous keyframe. */
    for (n = 0; n < qtdemux->n_streams; n++) {
      QtDemuxStream *str;
      guint32 index, kindex;
      guint32 seg_idx;
      guint64 media_start;
      guint64 media_time;
      guint64 seg_time;
      QtDemuxSegment *seg;

      str = qtdemux->streams[n];

      seg_idx = gst_qtdemux_find_segment (qtdemux, str, desired_offset);
      GST_DEBUG_OBJECT (qtdemux, "align segment %d", seg_idx);

      /* segment not found, continue with normal flow */
      if (seg_idx == -1)
        continue;

      /* get segment and time in the segment */
      seg = &str->segments[seg_idx];
      seg_time = desired_offset - seg->time;

      /* get the media time in the segment */
      media_start = seg->media_start + seg_time;

      /* get the index of the sample with media time */
      index = gst_qtdemux_find_index (qtdemux, str, media_start);
      GST_DEBUG_OBJECT (qtdemux, "sample for %" GST_TIME_FORMAT " at %u",
          GST_TIME_ARGS (media_start), index);

      /* find previous keyframe */
      kindex = gst_qtdemux_find_keyframe (qtdemux, str, index);

      /* if the keyframe is at a different position, we need to update the
       * requiested seek time */
      if (index != kindex) {
        index = kindex;

        /* get timestamp of keyframe */
        media_time = str->samples[kindex].timestamp;
        GST_DEBUG_OBJECT (qtdemux, "keyframe at %u with time %" GST_TIME_FORMAT,
            kindex, GST_TIME_ARGS (media_time));

        /* keyframes in the segment get a chance to change the
         * desired_offset. keyframes out of the segment are
         * ignored. */
        if (media_time >= seg->media_start) {
          guint64 seg_time;

          /* this keyframe is inside the segment, convert back to
           * segment time */
          seg_time = (media_time - seg->media_start) + seg->time;
          if (seg_time < min_offset)
            min_offset = seg_time;
        }
      }
    }
    GST_DEBUG_OBJECT (qtdemux, "keyframe seek, align to %"
        GST_TIME_FORMAT, GST_TIME_ARGS (desired_offset));
    desired_offset = min_offset;
  }

  /* and set all streams to the final position */
  for (n = 0; n < qtdemux->n_streams; n++) {
    QtDemuxStream *stream = qtdemux->streams[n];

    stream->time_position = desired_offset;
    stream->sample_index = -1;
    stream->segment_index = -1;
    stream->last_ret = GST_FLOW_OK;
  }
  segment->last_stop = desired_offset;
  segment->time = desired_offset;

  /* we stop at the end */
  if (segment->stop == -1)
    segment->stop = segment->duration;

  return TRUE;
}

/* do a seek in pull based mode */
static gboolean
gst_qtdemux_do_seek (GstQTDemux * qtdemux, GstPad * pad, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean res;
  gboolean update;
  GstSegment seeksegment;
  int i;

  if (event) {
    GST_DEBUG_OBJECT (qtdemux, "doing seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we have to have a format as the segment format. Try to convert
     * if not. */
    if (format != GST_FORMAT_TIME) {
      GstFormat fmt;

      fmt = GST_FORMAT_TIME;
      res = TRUE;
      if (cur_type != GST_SEEK_TYPE_NONE)
        res = gst_pad_query_convert (pad, format, cur, &fmt, &cur);
      if (res && stop_type != GST_SEEK_TYPE_NONE)
        res = gst_pad_query_convert (pad, format, stop, &fmt, &stop);
      if (!res)
        goto no_format;

      format = fmt;
    }
  } else {
    GST_DEBUG_OBJECT (qtdemux, "doing seek without event");
    flags = 0;
  }

  flush = flags & GST_SEEK_FLAG_FLUSH;

  GST_DEBUG_OBJECT (qtdemux, "seek format %d", format);

  /* stop streaming, either by flushing or by pausing the task */
  if (flush) {
    /* unlock upstream pull_range */
    gst_pad_push_event (qtdemux->sinkpad, gst_event_new_flush_start ());
    /* make sure out loop function exits */
    gst_qtdemux_push_event (qtdemux, gst_event_new_flush_start ());
  } else {
    /* non flushing seek, pause the task */
    gst_pad_pause_task (qtdemux->sinkpad);
  }

  /* wait for streaming to finish */
  GST_PAD_STREAM_LOCK (qtdemux->sinkpad);

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &qtdemux->segment, sizeof (GstSegment));

  if (event) {
    /* configure the segment with the seek variables */
    GST_DEBUG_OBJECT (qtdemux, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  /* now do the seek, this actually never returns FALSE */
  res = gst_qtdemux_perform_seek (qtdemux, &seeksegment);

  /* prepare for streaming again */
  if (flush) {
    gst_pad_push_event (qtdemux->sinkpad, gst_event_new_flush_stop ());
    gst_qtdemux_push_event (qtdemux, gst_event_new_flush_stop ());
  } else if (qtdemux->segment_running) {
    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (qtdemux, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, qtdemux->segment.start,
        qtdemux->segment.last_stop);

    if (qtdemux->segment.rate >= 0) {
      /* FIXME, rate is the product of the global rate and the (quicktime)
       * segment rate. */
      qtdemux->pending_newsegment = gst_event_new_new_segment (TRUE,
          qtdemux->segment.rate, qtdemux->segment.format,
          qtdemux->segment.start, qtdemux->segment.last_stop,
          qtdemux->segment.time);
    } else {                    /* For Reverse Playback */
      guint64 stop;

      if ((stop = qtdemux->segment.stop) == -1)
        stop = qtdemux->segment.duration;
      /* for reverse playback, we played from stop to last_stop. */
      qtdemux->pending_newsegment = gst_event_new_new_segment (TRUE,
          qtdemux->segment.rate, qtdemux->segment.format,
          qtdemux->segment.last_stop, stop, qtdemux->segment.last_stop);
    }
  }

  /* commit the new segment */
  memcpy (&qtdemux->segment, &seeksegment, sizeof (GstSegment));

  if (qtdemux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT_CAST (qtdemux),
        gst_message_new_segment_start (GST_OBJECT_CAST (qtdemux),
            qtdemux->segment.format, qtdemux->segment.last_stop));
  }

  /* restart streaming, NEWSEGMENT will be sent from the streaming
   * thread. */
  qtdemux->segment_running = TRUE;
  for (i = 0; i < qtdemux->n_streams; i++)
    qtdemux->streams[i]->last_ret = GST_FLOW_OK;

  gst_pad_start_task (qtdemux->sinkpad, (GstTaskFunction) gst_qtdemux_loop,
      qtdemux->sinkpad);

  GST_PAD_STREAM_UNLOCK (qtdemux->sinkpad);

  return TRUE;

  /* ERRORS */
no_format:
  {
    GST_DEBUG_OBJECT (qtdemux, "unsupported format given, seek aborted.");
    return FALSE;
  }
}

static gboolean
gst_qtdemux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstQTDemux *qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (qtdemux->pullbased) {
        res = gst_qtdemux_do_seek (qtdemux, pad, event);
      } else {
        GST_DEBUG_OBJECT (qtdemux, "cannot seek in streaming mode");
        res = FALSE;
      }
      gst_event_unref (event);
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
      res = FALSE;
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (qtdemux);

  return res;
}

static gboolean
gst_qtdemux_handle_sink_event (GstPad * sinkpad, GstEvent * event)
{
  GstQTDemux *demux = GST_QTDEMUX (GST_PAD_PARENT (sinkpad));
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      /* We need to convert it to a GST_FORMAT_TIME new segment */
      gst_event_unref (event);
      res = TRUE;
      break;
    case GST_EVENT_EOS:
      /* If we are in push mode, and get an EOS before we've seen any streams,
       * then error out - we have nowhere to send the EOS */
      if (!demux->pullbased && demux->n_streams == 0) {
        GST_ELEMENT_ERROR (demux, STREAM, DECODE,
            (_("This file contains no playable streams.")),
            ("no known streams found"));
      }
      /* Fall through */
    default:
      res = gst_pad_event_default (demux->sinkpad, event);
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_qtdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gint n;

      qtdemux->state = QTDEMUX_STATE_INITIAL;
      qtdemux->last_ts = GST_CLOCK_TIME_NONE;
      qtdemux->neededbytes = 16;
      qtdemux->todrop = 0;
      qtdemux->pullbased = FALSE;
      qtdemux->offset = 0;
      qtdemux->mdatoffset = GST_CLOCK_TIME_NONE;
      if (qtdemux->mdatbuffer)
        gst_buffer_unref (qtdemux->mdatbuffer);
      qtdemux->mdatbuffer = NULL;
      gst_adapter_clear (qtdemux->adapter);
      for (n = 0; n < qtdemux->n_streams; n++) {
        QtDemuxStream *stream = qtdemux->streams[n];

        while (stream->buffers) {
          gst_buffer_unref (GST_BUFFER_CAST (stream->buffers->data));
          stream->buffers =
              g_slist_delete_link (stream->buffers, stream->buffers);
        }
        if (stream->pad)
          gst_element_remove_pad (element, stream->pad);
        if (stream->samples)
          g_free (stream->samples);
        if (stream->caps)
          gst_caps_unref (stream->caps);
        if (stream->segments)
          g_free (stream->segments);
        g_free (stream);
      }
      qtdemux->major_brand = 0;
      qtdemux->n_streams = 0;
      qtdemux->n_video_streams = 0;
      qtdemux->n_audio_streams = 0;
      gst_segment_init (&qtdemux->segment, GST_FORMAT_TIME);
      break;
    }
    default:
      break;
  }

  return result;
}

static void
extract_initial_length_and_fourcc (guint8 * data, guint64 * plength,
    guint32 * pfourcc)
{
  guint64 length;
  guint32 fourcc;

  length = QT_UINT32 (data);
  GST_DEBUG ("length %08" G_GINT64_MODIFIER "x", length);
  fourcc = QT_FOURCC (data + 4);
  GST_DEBUG ("atom type %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));

  if (length == 0) {
    length = G_MAXUINT32;
  } else if (length == 1) {
    /* this means we have an extended size, which is the 64 bit value of
     * the next 8 bytes */
    length = QT_UINT64 (data + 8);
    GST_DEBUG ("length %08llx", length);
  }

  if (plength)
    *plength = length;
  if (pfourcc)
    *pfourcc = fourcc;
}

static GstFlowReturn
gst_qtdemux_loop_state_header (GstQTDemux * qtdemux)
{
  guint64 length;
  guint32 fourcc;
  GstBuffer *buf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 cur_offset = qtdemux->offset;

  ret = gst_pad_pull_range (qtdemux->sinkpad, cur_offset, 16, &buf);
  if (ret != GST_FLOW_OK)
    goto beach;
  extract_initial_length_and_fourcc (GST_BUFFER_DATA (buf), &length, &fourcc);
  gst_buffer_unref (buf);

  if (length == 0) {
    GST_ELEMENT_ERROR (qtdemux, STREAM, DECODE,
        (_("This file is invalid and cannot be played.")),
        ("Header atom '%" GST_FOURCC_FORMAT "' has empty length",
            GST_FOURCC_ARGS (fourcc)));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  switch (fourcc) {
    case FOURCC_mdat:
    case FOURCC_free:
    case FOURCC_wide:
    case FOURCC_PICT:
    case FOURCC_pnot:
    {
      GST_LOG_OBJECT (qtdemux,
          "skipping atom '%" GST_FOURCC_FORMAT "' at %" G_GUINT64_FORMAT,
          GST_FOURCC_ARGS (fourcc), cur_offset);
      cur_offset += length;
      qtdemux->offset += length;
      break;
    }
    case FOURCC_moov:
    {
      GstBuffer *moov;

      ret = gst_pad_pull_range (qtdemux->sinkpad, cur_offset, length, &moov);
      if (ret != GST_FLOW_OK)
        goto beach;
      if (length != GST_BUFFER_SIZE (moov)) {
        /* Some files have a 'moov' atom at the end of the file which contains
         * a terminal 'free' atom where the body of the atom is missing.
         * Check for, and permit, this special case.
         */
        if (GST_BUFFER_SIZE (moov) >= 8) {
          guint8 *final_data = GST_BUFFER_DATA (moov) +
              (GST_BUFFER_SIZE (moov) - 8);
          guint32 final_length = QT_UINT32 (final_data);
          guint32 final_fourcc = QT_FOURCC (final_data + 4);
          if (final_fourcc == FOURCC_free &&
              GST_BUFFER_SIZE (moov) + final_length - 8 == length) {
            /* Ok, we've found that special case. Allocate a new buffer with
             * that free atom actually present. */
            GstBuffer *newmoov = gst_buffer_new_and_alloc (length);
            gst_buffer_copy_metadata (newmoov, moov,
                GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS |
                GST_BUFFER_COPY_CAPS);
            memcpy (GST_BUFFER_DATA (newmoov), GST_BUFFER_DATA (moov),
                GST_BUFFER_SIZE (moov));
            memset (GST_BUFFER_DATA (newmoov) + GST_BUFFER_SIZE (moov), 0,
                final_length - 8);
            gst_buffer_unref (moov);
            moov = newmoov;
          }
        }
      }

      if (length != GST_BUFFER_SIZE (moov)) {
        GST_ELEMENT_ERROR (qtdemux, STREAM, DECODE,
            (_("This file is incomplete and cannot be played.")),
            ("We got less than expected (received %u, wanted %u, offset %"
                G_GUINT64_FORMAT ")",
                GST_BUFFER_SIZE (moov), (guint) length, cur_offset));
        ret = GST_FLOW_ERROR;
        goto beach;
      }
      cur_offset += length;
      qtdemux->offset += length;

      qtdemux_parse_moov (qtdemux, GST_BUFFER_DATA (moov), length);
      qtdemux_node_dump (qtdemux, qtdemux->moov_node);

      qtdemux_parse_tree (qtdemux);
      g_node_destroy (qtdemux->moov_node);
      gst_buffer_unref (moov);
      qtdemux->moov_node = NULL;
      qtdemux->state = QTDEMUX_STATE_MOVIE;
      GST_DEBUG_OBJECT (qtdemux, "switching state to STATE_MOVIE (%d)",
          qtdemux->state);
      break;
    }
    case FOURCC_ftyp:
    {
      GstBuffer *ftyp;

      /* extract major brand; might come in handy for ISO vs QT issues */
      ret = gst_pad_pull_range (qtdemux->sinkpad, cur_offset, length, &ftyp);
      if (ret != GST_FLOW_OK)
        goto beach;
      cur_offset += length;
      qtdemux->offset += length;
      /* only consider at least a sufficiently complete ftyp atom */
      if (length >= 20) {
        qtdemux->major_brand = QT_FOURCC (GST_BUFFER_DATA (ftyp) + 8);
        GST_DEBUG_OBJECT (qtdemux, "major brand: %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (qtdemux->major_brand));
      }
      gst_buffer_unref (ftyp);
      break;
    }
    default:
    {
      GST_LOG_OBJECT (qtdemux,
          "unknown %08x '%" GST_FOURCC_FORMAT "' at %" G_GUINT64_FORMAT, fourcc,
          GST_FOURCC_ARGS (fourcc), cur_offset);
      cur_offset += length;
      qtdemux->offset += length;
      break;
    }
  }

beach:
  return ret;
}

/* Seeks to the previous keyframe of the indexed stream and 
 * aligns other streams with respect to the keyframe timestamp 
 * of indexed stream. Only called in case of Reverse Playback
 */
static GstFlowReturn
gst_qtdemux_seek_to_previous_keyframe (GstQTDemux * qtdemux)
{
  guint8 n = 0;
  guint32 seg_idx = 0, k_index = 0;
  guint64 k_pos = 0, last_stop = 0;
  QtDemuxSegment *seg = NULL;
  QtDemuxStream *ref_str = NULL;

  /* Now we choose an arbitrary stream, get the previous keyframe timestamp
   * and finally align all the other streams on that timestamp with their 
   * respective keyframes */
  for (n = 0; n < qtdemux->n_streams; n++) {
    QtDemuxStream *str = qtdemux->streams[n];

    seg_idx = gst_qtdemux_find_segment (qtdemux, str,
        qtdemux->segment.last_stop);

    /* segment not found, continue with normal flow */
    if (seg_idx == -1)
      continue;

    /* No candidate yet, take that one */
    if (!ref_str) {
      ref_str = str;
      continue;
    }

    /* So that stream has a segment, we prefer video streams */
    if (str->subtype == FOURCC_vide) {
      ref_str = str;
      break;
    }
  }

  if (G_UNLIKELY (!ref_str)) {
    GST_DEBUG_OBJECT (qtdemux, "couldn't find any stream");
    goto eos;
  }

  if (G_UNLIKELY (!ref_str->from_sample)) {
    GST_DEBUG_OBJECT (qtdemux, "reached the beginning of the file");
    goto eos;
  }

  /* So that stream has been playing from from_sample to to_sample. We will
   * get the timestamp of the previous sample and search for a keyframe before
   * that. For audio streams we do an arbitrary jump in the past (10 samples) */
  if (ref_str->subtype == FOURCC_vide) {
    k_index = gst_qtdemux_find_keyframe (qtdemux, ref_str,
        ref_str->from_sample - 1);
  } else {
    k_index = ref_str->from_sample - 10;
  }

  /* get current segment for that stream */
  seg = &ref_str->segments[ref_str->segment_index];
  /* Crawl back through segments to find the one containing this I frame */
  while (ref_str->samples[k_index].timestamp < seg->media_start) {
    GST_DEBUG_OBJECT (qtdemux, "keyframe position is out of segment %u",
        ref_str->segment_index);
    if (G_UNLIKELY (!ref_str->segment_index)) {
      /* Reached first segment, let's consider it's EOS */
      goto eos;
    }
    ref_str->segment_index--;
    seg = &ref_str->segments[ref_str->segment_index];
  }
  /* Calculate time position of the keyframe and where we should stop */
  k_pos = (ref_str->samples[k_index].timestamp - seg->media_start) + seg->time;
  last_stop = ref_str->samples[ref_str->from_sample].timestamp;
  last_stop = (last_stop - seg->media_start) + seg->time;

  GST_DEBUG_OBJECT (qtdemux, "preferred stream played from sample %u, "
      "now going to sample %u (pts %" GST_TIME_FORMAT ")", ref_str->from_sample,
      k_index, GST_TIME_ARGS (k_pos));

  /* Set last_stop with the keyframe timestamp we pushed of that stream */
  gst_segment_set_last_stop (&qtdemux->segment, GST_FORMAT_TIME, last_stop);
  GST_DEBUG_OBJECT (qtdemux, "last_stop now is %" GST_TIME_FORMAT,
      GST_TIME_ARGS (last_stop));

  if (G_UNLIKELY (last_stop < qtdemux->segment.start)) {
    GST_DEBUG_OBJECT (qtdemux, "reached the beginning of segment");
    goto eos;
  }

  /* Align them all on this */
  for (n = 0; n < qtdemux->n_streams; n++) {
    guint32 index = 0;
    guint64 media_start = 0, seg_time = 0;
    QtDemuxStream *str = qtdemux->streams[n];

    seg_idx = gst_qtdemux_find_segment (qtdemux, str, k_pos);
    GST_DEBUG_OBJECT (qtdemux, "align segment %d", seg_idx);

    /* segment not found, continue with normal flow */
    if (seg_idx == -1)
      continue;

    /* get segment and time in the segment */
    seg = &str->segments[seg_idx];
    seg_time = k_pos - seg->time;

    /* get the media time in the segment */
    media_start = seg->media_start + seg_time;

    /* get the index of the sample with media time */
    index = gst_qtdemux_find_index (qtdemux, str, media_start);
    GST_DEBUG_OBJECT (qtdemux, "sample for %" GST_TIME_FORMAT " at %u",
        GST_TIME_ARGS (media_start), index);

    /* find previous keyframe */
    k_index = gst_qtdemux_find_keyframe (qtdemux, str, index);

    /* Remember until where we want to go */
    str->to_sample = str->from_sample - 1;
    /* Define our time position */
    str->time_position =
        (str->samples[k_index].timestamp - seg->media_start) + seg->time;
    /* Now seek back in time */
    gst_qtdemux_move_stream (qtdemux, str, k_index);
    GST_DEBUG_OBJECT (qtdemux, "keyframe at %u, time position %"
        GST_TIME_FORMAT " playing from sample %u to %u", k_index,
        GST_TIME_ARGS (str->time_position), str->from_sample, str->to_sample);
  }

  return GST_FLOW_OK;

eos:
  return GST_FLOW_UNEXPECTED;
}

/* activate the given segment number @seg_idx of @stream at time @offset.
 * @offset is an absolute global position over all the segments.
 *
 * This will push out a NEWSEGMENT event with the right values and
 * position the stream index to the first decodable sample before
 * @offset.
 */
static gboolean
gst_qtdemux_activate_segment (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint32 seg_idx, guint64 offset)
{
  GstEvent *event;
  QtDemuxSegment *segment;
  guint32 index, kf_index;
  guint64 seg_time;
  guint64 start, stop, time;
  gdouble rate;

  GST_LOG_OBJECT (qtdemux, "activate segment %d, offset %" G_GUINT64_FORMAT,
      seg_idx, offset);

  /* update the current segment */
  stream->segment_index = seg_idx;

  /* get the segment */
  segment = &stream->segments[seg_idx];

  if (offset < segment->time) {
    GST_WARNING_OBJECT (qtdemux, "offset < segment->time %" G_GUINT64_FORMAT,
        segment->time);
    return FALSE;
  }

  /* get time in this segment */
  seg_time = offset - segment->time;

  GST_LOG_OBJECT (qtdemux, "seg_time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (seg_time));

  if (seg_time > segment->duration) {
    GST_LOG_OBJECT (qtdemux, "seg_time > segment->duration %" GST_TIME_FORMAT,
        GST_TIME_ARGS (segment->duration));
    return FALSE;
  }

  /* qtdemux->segment.stop is in outside-time-realm, whereas
   * segment->media_stop is in track-time-realm.
   * 
   * In order to compare the two, we need to bring segment.stop
   * into the track-time-realm */

  if (qtdemux->segment.stop == -1)
    stop = segment->media_stop;
  else
    stop =
        MIN (segment->media_stop,
        qtdemux->segment.stop - segment->time + segment->media_start);

  if (qtdemux->segment.rate >= 0) {
    start = MIN (segment->media_start + seg_time, stop);
    time = offset;
  } else {
    start = segment->media_start;
    stop = MIN (segment->media_start + seg_time, stop);
    time = segment->time;
  }

  GST_DEBUG_OBJECT (qtdemux, "newsegment %d from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT, seg_idx,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (time));

  /* combine global rate with that of the segment */
  rate = segment->rate * qtdemux->segment.rate;

  /* update the segment values used for clipping */
  gst_segment_init (&stream->segment, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&stream->segment, FALSE, rate, GST_FORMAT_TIME,
      start, stop, time);

  /* now prepare and send the segment */
  if (stream->pad) {
    event = gst_event_new_new_segment (FALSE, rate, GST_FORMAT_TIME,
        start, stop, time);
    gst_pad_push_event (stream->pad, event);
    /* assume we can send more data now */
    stream->last_ret = GST_FLOW_OK;
  }

  /* and move to the keyframe before the indicated media time of the
   * segment */
  if (qtdemux->segment.rate >= 0) {
    index = gst_qtdemux_find_index (qtdemux, stream, start);
    stream->to_sample = stream->n_samples;
    GST_DEBUG_OBJECT (qtdemux, "moving data pointer to %" GST_TIME_FORMAT
        ", index: %u, pts %" GST_TIME_FORMAT, GST_TIME_ARGS (start), index,
        GST_TIME_ARGS (stream->samples[index].timestamp));
  } else {
    index = gst_qtdemux_find_index (qtdemux, stream, stop);
    stream->to_sample = index;
    GST_DEBUG_OBJECT (qtdemux, "moving data pointer to %" GST_TIME_FORMAT
        ", index: %u, pts %" GST_TIME_FORMAT, GST_TIME_ARGS (stop), index,
        GST_TIME_ARGS (stream->samples[index].timestamp));
  }

  /* we're at the right spot */
  if (index == stream->sample_index) {
    GST_DEBUG_OBJECT (qtdemux, "we are at the right index");
    return TRUE;
  }

  /* find keyframe of the target index */
  kf_index = gst_qtdemux_find_keyframe (qtdemux, stream, index);

  /* if we move forwards, we don't have to go back to the previous
   * keyframe since we already sent that. We can also just jump to
   * the keyframe right before the target index if there is one. */
  if (index > stream->sample_index) {
    /* moving forwards check if we move past a keyframe */
    if (kf_index > stream->sample_index) {
      GST_DEBUG_OBJECT (qtdemux, "moving forwards to keyframe at %u (pts %"
          GST_TIME_FORMAT, kf_index,
          GST_TIME_ARGS (stream->samples[kf_index].timestamp));
      gst_qtdemux_move_stream (qtdemux, stream, kf_index);
    } else {
      GST_DEBUG_OBJECT (qtdemux, "moving forwards, keyframe at %u (pts %"
          GST_TIME_FORMAT " already sent", kf_index,
          GST_TIME_ARGS (stream->samples[kf_index].timestamp));
    }
  } else {
    GST_DEBUG_OBJECT (qtdemux, "moving backwards to keyframe at %u (pts %"
        GST_TIME_FORMAT, kf_index,
        GST_TIME_ARGS (stream->samples[kf_index].timestamp));
    gst_qtdemux_move_stream (qtdemux, stream, kf_index);
  }

  return TRUE;
}

/* prepare to get the current sample of @stream, getting essential values.
 *
 * This function will also prepare and send the segment when needed.
 *
 * Return FALSE if the stream is EOS.
 */
static gboolean
gst_qtdemux_prepare_current_sample (GstQTDemux * qtdemux,
    QtDemuxStream * stream, guint64 * offset, guint * size, guint64 * timestamp,
    guint64 * duration, gboolean * keyframe)
{
  QtDemuxSample *sample;
  guint64 time_position;
  guint32 seg_idx;

  g_return_val_if_fail (stream != NULL, FALSE);

  time_position = stream->time_position;
  if (time_position == -1)
    goto eos;

  seg_idx = stream->segment_index;
  if (seg_idx == -1) {
    /* find segment corresponding to time_position if we are looking
     * for a segment. */
    seg_idx = gst_qtdemux_find_segment (qtdemux, stream, time_position);

    /* nothing found, we're really eos */
    if (seg_idx == -1)
      goto eos;
  }

  /* different segment, activate it, sample_index will be set. */
  if (stream->segment_index != seg_idx)
    gst_qtdemux_activate_segment (qtdemux, stream, seg_idx, time_position);

  GST_LOG_OBJECT (qtdemux, "segment active, index = %u of %u",
      stream->sample_index, stream->n_samples);

  /* send out pending buffers */
  while (stream->buffers) {
    GstBuffer *buffer = (GstBuffer *) stream->buffers->data;

    if (stream->discont) {
      GST_LOG_OBJECT (qtdemux, "marking discont buffer");
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }
    gst_buffer_set_caps (buffer, stream->caps);

    gst_pad_push (stream->pad, buffer);

    stream->buffers = g_slist_delete_link (stream->buffers, stream->buffers);
  }

  if (stream->sample_index >= stream->n_samples)
    goto eos;

  /* now get the info for the sample we're at */
  sample = &stream->samples[stream->sample_index];

  *timestamp = sample->timestamp + sample->pts_offset;
  *offset = sample->offset;
  *size = sample->size;
  *duration = sample->duration;
  *keyframe = stream->all_keyframe || sample->keyframe;

  /* add padding */
  if (stream->padding) {
    *offset += stream->padding;
    *size -= stream->padding;
  }

  return TRUE;

  /* special cases */
eos:
  {
    stream->time_position = -1;
    return FALSE;
  }
}

/* move to the next sample in @stream.
 *
 * Moves to the next segment when needed.
 */
static void
gst_qtdemux_advance_sample (GstQTDemux * qtdemux, QtDemuxStream * stream)
{
  QtDemuxSample *sample;
  QtDemuxSegment *segment;

  if (stream->sample_index >= stream->to_sample) {
    /* Mark the stream as EOS */
    GST_DEBUG_OBJECT (qtdemux, "reached max allowed sample %u, mark EOS",
        stream->to_sample);
    stream->time_position = -1;
    return;
  }

  /* move to next sample */
  stream->sample_index++;

  /* get current segment */
  segment = &stream->segments[stream->segment_index];

  /* reached the last sample, we need the next segment */
  if (stream->sample_index >= stream->n_samples)
    goto next_segment;

  /* get next sample */
  sample = &stream->samples[stream->sample_index];

  /* see if we are past the segment */
  if (sample->timestamp >= segment->media_stop)
    goto next_segment;

  if (sample->timestamp >= segment->media_start) {
    /* inside the segment, update time_position, looks very familiar to
     * GStreamer segments, doesn't it? */
    stream->time_position =
        (sample->timestamp - segment->media_start) + segment->time;
  } else {
    /* not yet in segment, time does not yet increment. This means
     * that we are still prerolling keyframes to the decoder so it can
     * decode the first sample of the segment. */
    stream->time_position = segment->time;
  }
  return;

  /* move to the next segment */
next_segment:
  {
    GST_DEBUG_OBJECT (qtdemux, "segment %d ended ", stream->segment_index);

    if (stream->segment_index == stream->n_segments - 1) {
      /* are we at the end of the last segment, we're EOS */
      stream->time_position = -1;
    } else {
      /* else we're only at the end of the current segment */
      stream->time_position = segment->stop_time;
    }
    /* make sure we select a new segment */
    stream->segment_index = -1;
  }
}

/* UNEXPECTED and NOT_LINKED need to be combined. This means that we return:
 *  
 *  GST_FLOW_NOT_LINKED: when all pads NOT_LINKED.
 *  GST_FLOW_UNEXPECTED: when all pads UNEXPECTED or NOT_LINKED.
 */
static GstFlowReturn
gst_qtdemux_combine_flows (GstQTDemux * demux, QtDemuxStream * stream,
    GstFlowReturn ret)
{
  gint i;
  gboolean unexpected = FALSE, not_linked = TRUE;

  GST_LOG_OBJECT (demux, "flow return: %s", gst_flow_get_name (ret));

  /* store the value */
  stream->last_ret = ret;

  for (i = 0; i < demux->n_streams; i++) {
    QtDemuxStream *ostream = demux->streams[i];

    ret = ostream->last_ret;

    /* no unexpected or unlinked, return */
    if (ret != GST_FLOW_UNEXPECTED && ret != GST_FLOW_NOT_LINKED)
      goto done;

    /* we check to see if we have at least 1 unexpected or all unlinked */
    unexpected |= (ret == GST_FLOW_UNEXPECTED);
    not_linked &= (ret == GST_FLOW_NOT_LINKED);
  }
  /* when we get here, we all have unlinked or unexpected */
  if (not_linked)
    ret = GST_FLOW_NOT_LINKED;
  else if (unexpected)
    ret = GST_FLOW_UNEXPECTED;
done:
  GST_LOG_OBJECT (demux, "combined flow return: %s", gst_flow_get_name (ret));
  return ret;
}

/* the input buffer metadata must be writable. Returns NULL when the buffer is
 * completely cliped */
static GstBuffer *
gst_qtdemux_clip_buffer (GstQTDemux * qtdemux, QtDemuxStream * stream,
    GstBuffer * buf)
{
  gint64 start, stop, cstart, cstop, diff;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE, duration = GST_CLOCK_TIME_NONE;
  guint8 *data;
  guint size;
  gint num_rate, denom_rate;
  gint frame_size;
  gboolean clip_data;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* depending on the type, setup the clip parameters */
  if (stream->subtype == FOURCC_soun) {
    frame_size = stream->bytes_per_frame;
    num_rate = GST_SECOND;
    denom_rate = (gint) stream->rate;
    clip_data = TRUE;
  } else if (stream->subtype == FOURCC_vide) {
    frame_size = size;
    num_rate = stream->fps_n;
    denom_rate = stream->fps_d;
    clip_data = FALSE;
  } else
    goto wrong_type;

  /* we can only clip if we have a valid timestamp */
  timestamp = GST_BUFFER_TIMESTAMP (buf);
  if (!GST_CLOCK_TIME_IS_VALID (timestamp))
    goto no_timestamp;

  if (GST_BUFFER_DURATION_IS_VALID (buf)) {
    duration = GST_BUFFER_DURATION (buf);
  } else {
    duration =
        gst_util_uint64_scale_int (size / frame_size, num_rate, denom_rate);
  }

  start = timestamp;
  stop = start + duration;

  if (!gst_segment_clip (&stream->segment, GST_FORMAT_TIME,
          start, stop, &cstart, &cstop))
    goto clipped;

  /* see if some clipping happened */
  diff = cstart - start;
  if (diff > 0) {
    timestamp = cstart;
    duration -= diff;

    if (clip_data) {
      /* bring clipped time to samples and to bytes */
      diff = gst_util_uint64_scale_int (diff, denom_rate, num_rate);
      diff *= frame_size;

      GST_DEBUG_OBJECT (qtdemux, "clipping start to %" GST_TIME_FORMAT " %"
          G_GUINT64_FORMAT " bytes", GST_TIME_ARGS (cstart), diff);

      data += diff;
      size -= diff;
    }
  }
  diff = stop - cstop;
  if (diff > 0) {
    duration -= diff;

    if (clip_data) {
      /* bring clipped time to samples and then to bytes */
      diff = gst_util_uint64_scale_int (diff, denom_rate, num_rate);
      diff *= frame_size;

      GST_DEBUG_OBJECT (qtdemux, "clipping stop to %" GST_TIME_FORMAT " %"
          G_GUINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);

      size -= diff;
    }
  }

  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;
  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_DATA (buf) = data;

  return buf;

  /* dropped buffer */
wrong_type:
  {
    GST_DEBUG_OBJECT (qtdemux, "unknown stream type");
    return buf;
  }
no_timestamp:
  {
    GST_DEBUG_OBJECT (qtdemux, "no timestamp on buffer");
    return buf;
  }
clipped:
  {
    GST_DEBUG_OBJECT (qtdemux, "clipped buffer");
    gst_buffer_unref (buf);
    return NULL;
  }
}

static GstFlowReturn
gst_qtdemux_loop_state_movie (GstQTDemux * qtdemux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;
  QtDemuxStream *stream;
  guint64 min_time;
  guint64 offset;
  guint64 timestamp;
  guint64 duration;
  gboolean keyframe;
  guint size;
  gint index;
  gint i;

  gst_qtdemux_push_pending_newsegment (qtdemux);

  /* Figure out the next stream sample to output, min_time is expressed in
   * global time and runs over the edit list segments. */
  min_time = G_MAXUINT64;
  index = -1;
  for (i = 0; i < qtdemux->n_streams; i++) {
    guint64 position;

    stream = qtdemux->streams[i];
    position = stream->time_position;

    /* position of -1 is EOS */
    if (position != -1 && position < min_time) {
      min_time = position;
      index = i;
    }
  }
  /* all are EOS */
  if (index == -1) {
    GST_DEBUG_OBJECT (qtdemux, "all streams are EOS");
    goto eos;
  }

  /* check for segment end */
  if (qtdemux->segment.stop != -1 && qtdemux->segment.stop < min_time) {
    GST_DEBUG_OBJECT (qtdemux, "we reached the end of our segment.");
    goto eos;
  }

  stream = qtdemux->streams[index];

  /* fetch info for the current sample of this stream */
  if (!gst_qtdemux_prepare_current_sample (qtdemux, stream, &offset, &size,
          &timestamp, &duration, &keyframe))
    goto eos;

  GST_LOG_OBJECT (qtdemux,
      "pushing from stream %d, offset %" G_GUINT64_FORMAT
      ", size %d, timestamp=%" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      index, offset, size, GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

  /* hmm, empty sample, skip and move to next sample */
  if (G_UNLIKELY (size <= 0))
    goto next;

  /* last pushed sample was out of boundary, goto next sample */
  if (stream->last_ret == GST_FLOW_UNEXPECTED)
    goto next;

  GST_LOG_OBJECT (qtdemux, "reading %d bytes @ %" G_GUINT64_FORMAT, size,
      offset);

  ret = gst_pad_pull_range (qtdemux->sinkpad, offset, size, &buf);
  if (ret != GST_FLOW_OK)
    goto beach;

  if (stream->fourcc == FOURCC_rtsp) {
    GstMessage *m;
    gchar *url;

    url = g_strndup ((gchar *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

    /* we have RTSP redirect now */
    m = gst_message_new_element (GST_OBJECT_CAST (qtdemux),
        gst_structure_new ("redirect",
            "new-location", G_TYPE_STRING, url, NULL));
    g_free (url);

    gst_element_post_message (GST_ELEMENT_CAST (qtdemux), m);
  }

  qtdemux->last_ts = min_time;
  if (qtdemux->segment.rate >= 0) {
    gst_segment_set_last_stop (&qtdemux->segment, GST_FORMAT_TIME, min_time);
  }
  if (stream->pad) {
    /* we're going to modify the metadata */
    buf = gst_buffer_make_metadata_writable (buf);

    GST_BUFFER_TIMESTAMP (buf) = timestamp;
    GST_BUFFER_DURATION (buf) = duration;
    GST_BUFFER_OFFSET (buf) = -1;
    GST_BUFFER_OFFSET_END (buf) = -1;

    if (stream->need_clip)
      buf = gst_qtdemux_clip_buffer (qtdemux, stream, buf);

    if (buf == NULL)
      goto next;

    if (stream->discont) {
      GST_LOG_OBJECT (qtdemux, "marking discont buffer");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }

    if (!keyframe)
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    gst_buffer_set_caps (buf, stream->caps);

    GST_LOG_OBJECT (qtdemux,
        "Pushing buffer with time %" GST_TIME_FORMAT ", duration %"
        GST_TIME_FORMAT " on pad %s",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_PAD_NAME (stream->pad));

    ret = gst_pad_push (stream->pad, buf);
  } else {
    GST_DEBUG_OBJECT (qtdemux, "No output pad for stream, ignoring");
    gst_buffer_unref (buf);
    ret = GST_FLOW_OK;
  }

  /* combine flows */
  ret = gst_qtdemux_combine_flows (qtdemux, stream, ret);
  /* ignore unlinked, we will not push on the pad anymore and we will EOS when
   * we have no more data for the pad to push */
  if (ret == GST_FLOW_UNEXPECTED)
    ret = GST_FLOW_OK;

next:
  gst_qtdemux_advance_sample (qtdemux, stream);

beach:
  return ret;

  /* special cases */
eos:
  {
    GST_DEBUG_OBJECT (qtdemux, "No samples left for any streams - EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }
}

static void
gst_qtdemux_loop (GstPad * pad)
{
  GstQTDemux *qtdemux;
  guint64 cur_offset;
  GstFlowReturn ret;

  qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  cur_offset = qtdemux->offset;
  GST_LOG_OBJECT (qtdemux, "loop at position %" G_GUINT64_FORMAT ", state %d",
      cur_offset, qtdemux->state);

  switch (qtdemux->state) {
    case QTDEMUX_STATE_INITIAL:
    case QTDEMUX_STATE_HEADER:
      ret = gst_qtdemux_loop_state_header (qtdemux);
      break;
    case QTDEMUX_STATE_MOVIE:
      ret = gst_qtdemux_loop_state_movie (qtdemux);
      if (qtdemux->segment.rate < 0 && ret == GST_FLOW_UNEXPECTED) {
        ret = gst_qtdemux_seek_to_previous_keyframe (qtdemux);
      }
      break;
    default:
      /* ouch */
      goto invalid_state;
  }

  /* if something went wrong, pause */
  if (ret != GST_FLOW_OK)
    goto pause;

done:
  gst_object_unref (qtdemux);
  return;

  /* ERRORS */
invalid_state:
  {
    GST_ELEMENT_ERROR (qtdemux, STREAM, FAILED,
        (NULL), ("streaming stopped, invalid state"));
    qtdemux->segment_running = FALSE;
    gst_pad_pause_task (pad);
    gst_qtdemux_push_event (qtdemux, gst_event_new_eos ());
    goto done;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_LOG_OBJECT (qtdemux, "pausing task, reason %s", reason);

    qtdemux->segment_running = FALSE;
    gst_pad_pause_task (pad);

    /* fatal errors need special actions */
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      /* check EOS */
      if (ret == GST_FLOW_UNEXPECTED) {
        if (qtdemux->n_streams == 0) {
          /* we have no streams, post an error */
          GST_ELEMENT_ERROR (qtdemux, STREAM, DECODE,
              (_("This file contains no playable streams.")),
              ("no known streams found"));
        }
        if (qtdemux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;

          /* FIXME: I am not sure this is the right fix. If the sinks are
           * supposed to detect the segment is complete and accumulate 
           * automatically, it does not seem to work here. Need more work */
          qtdemux->segment_running = TRUE;

          if ((stop = qtdemux->segment.stop) == -1)
            stop = qtdemux->segment.duration;

          if (qtdemux->segment.rate >= 0) {
            GST_LOG_OBJECT (qtdemux, "Sending segment done, at end of segment");
            gst_element_post_message (GST_ELEMENT_CAST (qtdemux),
                gst_message_new_segment_done (GST_OBJECT_CAST (qtdemux),
                    GST_FORMAT_TIME, stop));
          } else {
            /*  For Reverse Playback */
            GST_LOG_OBJECT (qtdemux,
                "Sending segment done, at start of segment");
            gst_element_post_message (GST_ELEMENT_CAST (qtdemux),
                gst_message_new_segment_done (GST_OBJECT_CAST (qtdemux),
                    GST_FORMAT_TIME, qtdemux->segment.start));
          }
        } else {
          GST_LOG_OBJECT (qtdemux, "Sending EOS at end of segment");
          gst_qtdemux_push_event (qtdemux, gst_event_new_eos ());
        }
      } else {
        GST_ELEMENT_ERROR (qtdemux, STREAM, FAILED,
            (NULL), ("streaming stopped, reason %s", reason));
        gst_qtdemux_push_event (qtdemux, gst_event_new_eos ());
      }
    }
    goto done;
  }
}

/*
 * next_entry_size
 *
 * Returns the size of the first entry at the current offset.
 * If -1, there are none (which means EOS or empty file).
 */
static guint64
next_entry_size (GstQTDemux * demux)
{
  QtDemuxStream *stream;
  int i;
  int smallidx = -1;
  guint64 smalloffs = (guint64) - 1;

  GST_LOG_OBJECT (demux, "Finding entry at offset %lld", demux->offset);

  for (i = 0; i < demux->n_streams; i++) {
    stream = demux->streams[i];

    if (stream->sample_index == -1)
      stream->sample_index = 0;

    GST_LOG_OBJECT (demux,
        "Checking Stream %d (sample_index:%d / offset:%lld / size:%d / chunk:%d)",
        i, stream->sample_index, stream->samples[stream->sample_index].offset,
        stream->samples[stream->sample_index].size,
        stream->samples[stream->sample_index].chunk);

    if (((smalloffs == -1)
            || (stream->samples[stream->sample_index].offset < smalloffs))
        && (stream->samples[stream->sample_index].size)) {
      smallidx = i;
      smalloffs = stream->samples[stream->sample_index].offset;
    }
  }

  GST_LOG_OBJECT (demux, "stream %d offset %lld demux->offset :%lld",
      smallidx, smalloffs, demux->offset);

  if (smallidx == -1)
    return -1;
  stream = demux->streams[smallidx];

  if (stream->samples[stream->sample_index].offset >= demux->offset) {
    demux->todrop =
        stream->samples[stream->sample_index].offset - demux->offset;
    return stream->samples[stream->sample_index].size + demux->todrop;
  }

  GST_DEBUG_OBJECT (demux, "There wasn't any entry at offset %lld",
      demux->offset);
  return -1;
}

static void
gst_qtdemux_post_progress (GstQTDemux * demux, gint num, gint denom)
{
  gint perc = (gint) ((gdouble) num * 100.0 / (gdouble) denom);

  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux),
          gst_structure_new ("progress", "percent", G_TYPE_INT, perc, NULL)));
}

/* FIXME, unverified after edit list updates */
static GstFlowReturn
gst_qtdemux_chain (GstPad * sinkpad, GstBuffer * inbuf)
{
  GstQTDemux *demux;
  GstFlowReturn ret = GST_FLOW_OK;

  demux = GST_QTDEMUX (gst_pad_get_parent (sinkpad));

  gst_adapter_push (demux->adapter, inbuf);

  GST_DEBUG_OBJECT (demux, "pushing in inbuf %p, neededbytes:%u, available:%u",
      inbuf, demux->neededbytes, gst_adapter_available (demux->adapter));

  while (((gst_adapter_available (demux->adapter)) >= demux->neededbytes) &&
      (ret == GST_FLOW_OK)) {

    GST_DEBUG_OBJECT (demux,
        "state:%d , demux->neededbytes:%d, demux->offset:%lld", demux->state,
        demux->neededbytes, demux->offset);

    switch (demux->state) {
      case QTDEMUX_STATE_INITIAL:{
        const guint8 *data;
        guint32 fourcc;
        guint64 size;

        data = gst_adapter_peek (demux->adapter, demux->neededbytes);

        /* get fourcc/length, set neededbytes */
        extract_initial_length_and_fourcc ((guint8 *) data, &size, &fourcc);
        GST_DEBUG_OBJECT (demux,
            "Peeking found [%" GST_FOURCC_FORMAT "] size: %u",
            GST_FOURCC_ARGS (fourcc), (guint) size);
        if (size == 0) {
          GST_ELEMENT_ERROR (demux, STREAM, DECODE,
              (_("This file is invalid and cannot be played.")),
              ("initial atom '%" GST_FOURCC_FORMAT "' has empty length",
                  GST_FOURCC_ARGS (fourcc)));
          ret = GST_FLOW_ERROR;
          break;
        }
        if (fourcc == FOURCC_mdat) {
          if (demux->n_streams > 0) {
            demux->state = QTDEMUX_STATE_MOVIE;
            demux->neededbytes = next_entry_size (demux);
          } else {
            demux->state = QTDEMUX_STATE_BUFFER_MDAT;
            demux->neededbytes = size;
            demux->mdatoffset = demux->offset;
          }
        } else {
          demux->neededbytes = size;
          demux->state = QTDEMUX_STATE_HEADER;
        }
        break;
      }
      case QTDEMUX_STATE_HEADER:{
        guint8 *data;
        guint32 fourcc;

        GST_DEBUG_OBJECT (demux, "In header");

        data = gst_adapter_take (demux->adapter, demux->neededbytes);

        /* parse the header */
        extract_initial_length_and_fourcc (data, NULL, &fourcc);
        if (fourcc == FOURCC_moov) {
          GST_DEBUG_OBJECT (demux, "Parsing [moov]");

          qtdemux_parse_moov (demux, data, demux->neededbytes);
          qtdemux_node_dump (demux, demux->moov_node);
          qtdemux_parse_tree (demux);

          g_node_destroy (demux->moov_node);
          g_free (data);
          demux->moov_node = NULL;
        } else {
          GST_WARNING_OBJECT (demux,
              "Unknown fourcc while parsing header : %" GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (fourcc));
          /* Let's jump that one and go back to initial state */
        }

        GST_DEBUG_OBJECT (demux, "Finished parsing the header");
        if (demux->mdatbuffer && demux->n_streams) {
          /* the mdat was before the header */
          GST_DEBUG_OBJECT (demux, "We have n_streams:%d and mdatbuffer:%p",
              demux->n_streams, demux->mdatbuffer);
          gst_adapter_clear (demux->adapter);
          GST_DEBUG_OBJECT (demux, "mdatbuffer starts with %" GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (QT_UINT32 (demux->mdatbuffer)));
          gst_adapter_push (demux->adapter, demux->mdatbuffer);
          demux->mdatbuffer = NULL;
          demux->offset = demux->mdatoffset;
          demux->neededbytes = next_entry_size (demux);
          demux->state = QTDEMUX_STATE_MOVIE;
        } else {
          GST_DEBUG_OBJECT (demux, "Carrying on normally");
          demux->offset += demux->neededbytes;
          demux->neededbytes = 16;
          demux->state = QTDEMUX_STATE_INITIAL;
        }

        break;
      }
      case QTDEMUX_STATE_BUFFER_MDAT:{
        GST_DEBUG_OBJECT (demux, "Got our buffer at offset %lld",
            demux->mdatoffset);
        if (demux->mdatbuffer)
          gst_buffer_unref (demux->mdatbuffer);
        demux->mdatbuffer = gst_buffer_new ();
        gst_buffer_set_data (demux->mdatbuffer,
            gst_adapter_take (demux->adapter, demux->neededbytes),
            demux->neededbytes);
        GST_DEBUG_OBJECT (demux, "mdatbuffer starts with %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (QT_UINT32 (demux->mdatbuffer)));
        demux->offset += demux->neededbytes;
        demux->neededbytes = 16;
        demux->state = QTDEMUX_STATE_INITIAL;
        gst_qtdemux_post_progress (demux, 1, 1);

        break;
      }
      case QTDEMUX_STATE_MOVIE:{
        guint8 *data;
        GstBuffer *outbuf;
        QtDemuxStream *stream = NULL;
        int i = -1;

        GST_DEBUG_OBJECT (demux, "BEGIN // in MOVIE for offset %lld",
            demux->offset);

        if (demux->todrop) {
          gst_adapter_flush (demux->adapter, demux->todrop);
          demux->neededbytes -= demux->todrop;
          demux->offset += demux->todrop;
        }

        /* Figure out which stream this is packet belongs to */
        for (i = 0; i < demux->n_streams; i++) {
          stream = demux->streams[i];
          GST_LOG_OBJECT (demux,
              "Checking stream %d (sample_index:%d / offset:%lld / size:%d / chunk:%d)",
              i, stream->sample_index,
              stream->samples[stream->sample_index].offset,
              stream->samples[stream->sample_index].size,
              stream->samples[stream->sample_index].chunk);

          if (stream->samples[stream->sample_index].offset == demux->offset)
            break;
        }

        if (stream == NULL)
          goto unknown_stream;

        /* first buffer? */
        /* FIXME : this should be handled in sink_event */
        if (demux->last_ts == GST_CLOCK_TIME_NONE) {
          gst_qtdemux_push_event (demux,
              gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                  0, GST_CLOCK_TIME_NONE, 0));
        }

        /* get data */
        data = gst_adapter_take (demux->adapter, demux->neededbytes);

        /* Put data in a buffer, set timestamps, caps, ... */
        outbuf = gst_buffer_new ();
        gst_buffer_set_data (outbuf, data, demux->neededbytes);
        GST_DEBUG_OBJECT (demux, "stream : %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (stream->fourcc));

        if (stream->samples[stream->sample_index].pts_offset) {
          demux->last_ts = stream->samples[stream->sample_index].timestamp;
          GST_BUFFER_TIMESTAMP (outbuf) = demux->last_ts +
              stream->samples[stream->sample_index].pts_offset;
        } else {
          GST_BUFFER_TIMESTAMP (outbuf) =
              stream->samples[stream->sample_index].timestamp;
          demux->last_ts = GST_BUFFER_TIMESTAMP (outbuf);
        }
        GST_BUFFER_DURATION (outbuf) =
            stream->samples[stream->sample_index].duration;

        /* send buffer */
        if (stream->pad) {
          GST_LOG_OBJECT (demux,
              "Pushing buffer with time %" GST_TIME_FORMAT " on pad %p",
              GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)), stream->pad);
          gst_buffer_set_caps (outbuf, stream->caps);
          ret = gst_pad_push (stream->pad, outbuf);
        } else {
          gst_buffer_unref (outbuf);
          ret = GST_FLOW_OK;
        }

        /* combine flows */
        ret = gst_qtdemux_combine_flows (demux, stream, ret);

        stream->sample_index++;

        /* update current offset and figure out size of next buffer */
        GST_LOG_OBJECT (demux, "increasing offset %" G_GUINT64_FORMAT " by %u",
            demux->offset, demux->neededbytes);
        demux->offset += demux->neededbytes;
        GST_LOG_OBJECT (demux, "offset is now %lld", demux->offset);

        if ((demux->neededbytes = next_entry_size (demux)) == -1)
          goto eos;
        break;
      }
      default:
        goto invalid_state;
    }
  }

  /* when buffering movie data, at least show user something is happening */
  if (ret == GST_FLOW_OK && demux->state == QTDEMUX_STATE_BUFFER_MDAT &&
      gst_adapter_available (demux->adapter) <= demux->neededbytes) {
    gst_qtdemux_post_progress (demux, gst_adapter_available (demux->adapter),
        demux->neededbytes);
  }
done:
  gst_object_unref (demux);

  return ret;

  /* ERRORS */
unknown_stream:
  {
    GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL), ("unknown stream found"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
eos:
  {
    GST_DEBUG_OBJECT (demux, "no next entry, EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }
invalid_state:
  {
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        (NULL), ("qtdemuxer invalid state %d", demux->state));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
qtdemux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);
  else
    return gst_pad_activate_push (sinkpad, TRUE);
}

static gboolean
qtdemux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstQTDemux *demux = GST_QTDEMUX (GST_PAD_PARENT (sinkpad));

  if (active) {
    demux->pullbased = TRUE;
    demux->segment_running = TRUE;
    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_qtdemux_loop,
        sinkpad);
  } else {
    demux->segment_running = FALSE;
    return gst_pad_stop_task (sinkpad);
  }
}

static gboolean
qtdemux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstQTDemux *demux = GST_QTDEMUX (GST_PAD_PARENT (sinkpad));

  demux->pullbased = FALSE;

  return TRUE;
}

#ifdef HAVE_ZLIB
static void *
qtdemux_zalloc (void *opaque, unsigned int items, unsigned int size)
{
  return g_malloc (items * size);
}

static void
qtdemux_zfree (void *opaque, void *addr)
{
  g_free (addr);
}

static void *
qtdemux_inflate (void *z_buffer, int z_length, int length)
{
  guint8 *buffer;
  z_stream *z;
  int ret;

  z = g_new0 (z_stream, 1);
  z->zalloc = qtdemux_zalloc;
  z->zfree = qtdemux_zfree;
  z->opaque = NULL;

  z->next_in = z_buffer;
  z->avail_in = z_length;

  buffer = (guint8 *) g_malloc (length);
  ret = inflateInit (z);
  while (z->avail_in > 0) {
    if (z->avail_out == 0) {
      length += 1024;
      buffer = (guint8 *) g_realloc (buffer, length);
      z->next_out = buffer + z->total_out;
      z->avail_out = 1024;
    }
    ret = inflate (z, Z_SYNC_FLUSH);
    if (ret != Z_OK)
      break;
  }
  if (ret != Z_STREAM_END) {
    g_warning ("inflate() returned %d", ret);
  }

  g_free (z);
  return buffer;
}
#endif /* HAVE_ZLIB */

static gboolean
qtdemux_parse_moov (GstQTDemux * qtdemux, guint8 * buffer, int length)
{
  GNode *cmov;

  qtdemux->moov_node = g_node_new (buffer);

  GST_DEBUG_OBJECT (qtdemux, "parsing 'moov' atom");
  qtdemux_parse_node (qtdemux, qtdemux->moov_node, buffer, length);

  cmov = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_cmov);
  if (cmov) {
    guint32 method;
    GNode *dcom;
    GNode *cmvd;

    dcom = qtdemux_tree_get_child_by_type (cmov, FOURCC_dcom);
    cmvd = qtdemux_tree_get_child_by_type (cmov, FOURCC_cmvd);
    if (dcom == NULL || cmvd == NULL)
      goto invalid_compression;

    method = QT_FOURCC ((guint8 *) dcom->data + 8);
    switch (method) {
#ifdef HAVE_ZLIB
      case GST_MAKE_FOURCC ('z', 'l', 'i', 'b'):{
        int uncompressed_length;
        int compressed_length;
        guint8 *buf;

        uncompressed_length = QT_UINT32 ((guint8 *) cmvd->data + 8);
        compressed_length = QT_UINT32 ((guint8 *) cmvd->data + 4) - 12;
        GST_LOG ("length = %d", uncompressed_length);

        buf =
            (guint8 *) qtdemux_inflate ((guint8 *) cmvd->data + 12,
            compressed_length, uncompressed_length);

        qtdemux->moov_node_compressed = qtdemux->moov_node;
        qtdemux->moov_node = g_node_new (buf);

        qtdemux_parse_node (qtdemux, qtdemux->moov_node, buf,
            uncompressed_length);
        break;
      }
#endif /* HAVE_ZLIB */
      default:
        GST_WARNING_OBJECT (qtdemux, "unknown or unhandled header compression "
            "type %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (method));
        break;
    }
  }
  return TRUE;

  /* ERRORS */
invalid_compression:
  {
    GST_ERROR_OBJECT (qtdemux, "invalid compressed header");
    return FALSE;
  }
}

static gboolean
qtdemux_parse_container (GstQTDemux * qtdemux, GNode * node, guint8 * buf,
    guint8 * end)
{
  while (buf < end) {
    GNode *child;
    guint32 len;

    if (buf + 4 > end) {
      GST_LOG_OBJECT (qtdemux, "buffer overrun");
      break;
    }
    len = QT_UINT32 (buf);
    if (len == 0) {
      GST_LOG_OBJECT (qtdemux, "empty container");
      break;
    }
    if (len < 8) {
      GST_WARNING_OBJECT (qtdemux, "length too short (%d < 8)", len);
      break;
    }
    if (len > (end - buf)) {
      GST_WARNING_OBJECT (qtdemux, "length too long (%d > %d)", len, end - buf);
      break;
    }

    child = g_node_new (buf);
    g_node_append (node, child);
    qtdemux_parse_node (qtdemux, child, buf, len);

    buf += len;
  }
  return TRUE;
}

static gboolean
qtdemux_parse_theora_extension (GstQTDemux * qtdemux, QtDemuxStream * stream,
    GNode * xdxt)
{
  int len = QT_UINT32 (xdxt->data);
  guint8 *buf = xdxt->data;
  guint8 *end = buf + len;
  GstBuffer *buffer;

  /* skip size and type */
  buf += 8;
  end -= 8;

  while (buf < end) {
    gint size;
    guint32 type;

    size = QT_UINT32 (buf);
    type = QT_FOURCC (buf + 4);

    GST_LOG_OBJECT (qtdemux, "%p %p", buf, end);

    if (buf + size > end || size <= 0)
      break;

    buf += 8;
    size -= 8;

    GST_WARNING_OBJECT (qtdemux, "have cookie %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (type));

    switch (type) {
      case FOURCC_tCtH:
        buffer = gst_buffer_new_and_alloc (size);
        memcpy (GST_BUFFER_DATA (buffer), buf, size);
        stream->buffers = g_slist_append (stream->buffers, buffer);
        GST_LOG_OBJECT (qtdemux, "parsing theora header");
        break;
      case FOURCC_tCt_:
        buffer = gst_buffer_new_and_alloc (size);
        memcpy (GST_BUFFER_DATA (buffer), buf, size);
        stream->buffers = g_slist_append (stream->buffers, buffer);
        GST_LOG_OBJECT (qtdemux, "parsing theora comment");
        break;
      case FOURCC_tCtC:
        buffer = gst_buffer_new_and_alloc (size);
        memcpy (GST_BUFFER_DATA (buffer), buf, size);
        stream->buffers = g_slist_append (stream->buffers, buffer);
        GST_LOG_OBJECT (qtdemux, "parsing theora codebook");
        break;
      default:
        GST_WARNING_OBJECT (qtdemux,
            "unknown theora cookie %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (type));
        break;
    }
    buf += size;
  }
  return TRUE;
}

static gboolean
qtdemux_parse_node (GstQTDemux * qtdemux, GNode * node, guint8 * buffer,
    int length)
{
  guint32 fourcc;
  guint32 node_length;
  const QtNodeType *type;
  guint8 *end;

  GST_LOG_OBJECT (qtdemux, "qtdemux_parse buffer %p length %d", buffer, length);

  node_length = QT_UINT32 (buffer);
  fourcc = QT_FOURCC (buffer + 4);

  type = qtdemux_type_get (fourcc);

  /* ignore empty nodes */
  if (fourcc == 0 || node_length == 8)
    return TRUE;

  end = buffer + length;

  GST_LOG_OBJECT (qtdemux,
      "parsing '%" GST_FOURCC_FORMAT "', length=%d, name '%s'",
      GST_FOURCC_ARGS (fourcc), node_length, type->name);

  if (type->flags & QT_FLAG_CONTAINER) {
    qtdemux_parse_container (qtdemux, node, buffer + 8, end);
  } else {
    switch (fourcc) {
      case FOURCC_stsd:
      {
        if (node_length < 20) {
          GST_LOG_OBJECT (qtdemux, "skipping small stsd box");
          break;
        }
        GST_DEBUG_OBJECT (qtdemux,
            "parsing stsd (sample table, sample description) atom");
        qtdemux_parse_container (qtdemux, node, buffer + 16, end);
        break;
      }
      case FOURCC_mp4a:
      {
        guint32 version;
        guint32 offset;

        if (length < 20) {
          /* small boxes are also inside wave inside the mp4a box */
          GST_LOG_OBJECT (qtdemux, "skipping small mp4a box");
          break;
        }
        version = QT_UINT32 (buffer + 16);

        GST_WARNING_OBJECT (qtdemux, "mp4a version 0x%08x", version);

        /* parse any esds descriptors */
        switch (version) {
          case 0x00000000:
            offset = 0x24;
            break;
          case 0x00010000:
            offset = 0x34;
            break;
          case 0x00020000:
            offset = 0x58;
            break;
          default:
            GST_WARNING_OBJECT (qtdemux, "unhandled mp4a version 0x%08x",
                version);
            offset = 0;
            break;
        }
        if (offset)
          qtdemux_parse_container (qtdemux, node, buffer + offset, end);
        break;
      }
      case FOURCC_mp4v:
      {
        guint8 *buf;
        guint32 version;
        int tlen;

        GST_DEBUG_OBJECT (qtdemux, "parsing in mp4v");
        version = QT_UINT32 (buffer + 16);
        GST_DEBUG_OBJECT (qtdemux, "version %08x", version);
        if (1 || version == 0x00000000) {
          buf = buffer + 0x32;

          /* FIXME Quicktime uses PASCAL string while
           * the iso format uses C strings. Check the file
           * type before attempting to parse the string here. */
          tlen = QT_UINT8 (buf);
          GST_DEBUG_OBJECT (qtdemux, "tlen = %d", tlen);
          buf++;
          GST_DEBUG_OBJECT (qtdemux, "string = %.*s", tlen, (char *) buf);
          /* the string has a reserved space of 32 bytes so skip
           * the remaining 31 */
          buf += 31;
          buf += 4;             /* and 4 bytes reserved */

          qtdemux_dump_mem (buf, end - buf);

          qtdemux_parse_container (qtdemux, node, buf, end);
        }
        break;
      }
      case FOURCC_mjp2:
      {
        qtdemux_parse_container (qtdemux, node, buffer + 86, end);
        break;
      }
      case FOURCC_meta:
      {
        GST_DEBUG_OBJECT (qtdemux, "parsing meta atom");
        qtdemux_parse_container (qtdemux, node, buffer + 12, end);
        break;
      }
      case FOURCC_XiTh:
      {
        guint32 version;
        guint32 offset;

        version = QT_UINT32 (buffer + 12);
        GST_DEBUG_OBJECT (qtdemux, "parsing XiTh atom version 0x%08x", version);

        switch (version) {
          case 0x00000001:
            offset = 0x62;
            break;
          default:
            GST_DEBUG_OBJECT (qtdemux, "unknown version 0x%08x", version);
            offset = 0;
            break;
        }
        if (offset)
          qtdemux_parse_container (qtdemux, node, buffer + offset, end);
        break;
      }
      default:
        break;
    }
  }
  GST_LOG_OBJECT (qtdemux, "parsed '%" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (fourcc));
  return TRUE;
}

static GNode *
qtdemux_tree_get_child_by_type (GNode * node, guint32 fourcc)
{
  GNode *child;
  guint8 *buffer;
  guint32 child_fourcc;

  for (child = g_node_first_child (node); child;
      child = g_node_next_sibling (child)) {
    buffer = (guint8 *) child->data;

    child_fourcc = QT_FOURCC (buffer + 4);

    if (child_fourcc == fourcc) {
      return child;
    }
  }
  return NULL;
}

static GNode *
qtdemux_tree_get_sibling_by_type (GNode * node, guint32 fourcc)
{
  GNode *child;
  guint8 *buffer;
  guint32 child_fourcc;

  for (child = g_node_next_sibling (node); child;
      child = g_node_next_sibling (child)) {
    buffer = (guint8 *) child->data;

    child_fourcc = QT_FOURCC (buffer + 4);

    if (child_fourcc == fourcc) {
      return child;
    }
  }
  return NULL;
}

static gboolean
gst_qtdemux_add_stream (GstQTDemux * qtdemux,
    QtDemuxStream * stream, GstTagList * list)
{
  if (qtdemux->n_streams >= GST_QTDEMUX_MAX_STREAMS)
    goto too_many_streams;

  if (stream->subtype == FOURCC_vide) {
    gchar *name = g_strdup_printf ("video_%02d", qtdemux->n_video_streams);

    stream->pad =
        gst_pad_new_from_static_template (&gst_qtdemux_videosrc_template, name);
    g_free (name);

    /* fps is calculated base on the duration of the first frames since
     * qt does not have a fixed framerate. */
    if ((stream->n_samples == 1) && (stream->min_duration == 0)) {
      /* still frame */
      stream->fps_n = 0;
      stream->fps_d = 1;
    } else {
      stream->fps_n = stream->timescale;
      if (stream->min_duration == 0)
        stream->fps_d = 1;
      else
        stream->fps_d = stream->min_duration;
    }

    if (stream->caps) {
      gboolean gray;
      gint depth, palette_count;
      const guint32 *palette_data = NULL;

      gst_caps_set_simple (stream->caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height,
          "framerate", GST_TYPE_FRACTION, stream->fps_n, stream->fps_d, NULL);

      depth = stream->bits_per_sample;

      /* more than 32 bits means grayscale */
      gray = (depth > 32);
      /* low 32 bits specify the depth  */
      depth &= 0x1F;

      /* different number of palette entries is determined by depth. */
      palette_count = 0;
      if ((depth == 1) || (depth == 2) || (depth == 4) || (depth == 8))
        palette_count = (1 << depth);

      switch (palette_count) {
        case 0:
          break;
        case 2:
          palette_data = ff_qt_default_palette_2;
          break;
        case 4:
          palette_data = ff_qt_default_palette_4;
          break;
        case 16:
          if (gray)
            palette_data = ff_qt_grayscale_palette_16;
          else
            palette_data = ff_qt_default_palette_16;
          break;
        case 256:
          if (gray)
            palette_data = ff_qt_grayscale_palette_256;
          else
            palette_data = ff_qt_default_palette_256;
          break;
        default:
          GST_ELEMENT_WARNING (qtdemux, STREAM, DECODE,
              (_("The video in this file might not play correctly.")),
              ("unsupported palette depth %d", depth));
          break;
      }
      if (palette_data) {
        GstBuffer *palette;

        /* make sure it's not writable. We leave MALLOCDATA to NULL so that we
         * don't free any of the buffer data. */
        palette = gst_buffer_new ();
        GST_BUFFER_FLAG_SET (palette, GST_BUFFER_FLAG_READONLY);
        GST_BUFFER_DATA (palette) = (guint8 *) palette_data;
        GST_BUFFER_SIZE (palette) = sizeof (guint32) * palette_count;

        gst_caps_set_simple (stream->caps, "palette_data",
            GST_TYPE_BUFFER, palette, NULL);
        gst_buffer_unref (palette);
      } else if (palette_count != 0) {
        GST_ELEMENT_WARNING (qtdemux, STREAM, NOT_IMPLEMENTED,
            (NULL), ("Unsupported palette depth %d. Ignoring stream.", depth));

        gst_object_unref (stream->pad);
        stream->pad = NULL;
      }
    }
    qtdemux->n_video_streams++;
  } else if (stream->subtype == FOURCC_soun) {
    gchar *name = g_strdup_printf ("audio_%02d", qtdemux->n_audio_streams);

    stream->pad =
        gst_pad_new_from_static_template (&gst_qtdemux_audiosrc_template, name);
    g_free (name);
    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, stream->n_channels, NULL);
    }
    qtdemux->n_audio_streams++;
  } else if (stream->subtype == FOURCC_strm) {
    GST_DEBUG_OBJECT (qtdemux, "stream type, not creating pad");
  } else {
    GST_DEBUG_OBJECT (qtdemux, "unknown stream type");
    goto done;
  }

  qtdemux->streams[qtdemux->n_streams] = stream;
  qtdemux->n_streams++;
  GST_DEBUG_OBJECT (qtdemux, "n_streams is now %d", qtdemux->n_streams);

  if (stream->pad) {
    GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;

    gst_pad_use_fixed_caps (stream->pad);
    gst_pad_set_event_function (stream->pad, gst_qtdemux_handle_src_event);
    gst_pad_set_query_type_function (stream->pad,
        gst_qtdemux_get_src_query_types);
    gst_pad_set_query_function (stream->pad, gst_qtdemux_handle_src_query);

    GST_DEBUG_OBJECT (qtdemux, "setting caps %" GST_PTR_FORMAT, stream->caps);
    gst_pad_set_caps (stream->pad, stream->caps);

    GST_DEBUG_OBJECT (qtdemux, "adding pad %s %p to qtdemux %p",
        GST_OBJECT_NAME (stream->pad), stream->pad, qtdemux);
    gst_pad_set_active (stream->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (qtdemux), stream->pad);
    if (list)
      gst_element_found_tags_for_pad (GST_ELEMENT_CAST (qtdemux), stream->pad,
          list);
  }
done:
  return TRUE;

too_many_streams:
  {
    GST_ELEMENT_WARNING (qtdemux, STREAM, DECODE,
        (_("This file contains too many streams. Only playing first %d"),
            GST_QTDEMUX_MAX_STREAMS), (NULL));
    return TRUE;
  }
}

/* collect all samples for @stream by reading the info from @stbl
 */
static gboolean
qtdemux_parse_samples (GstQTDemux * qtdemux, QtDemuxStream * stream,
    GNode * stbl)
{
  int offset;
  GNode *stsc;
  GNode *stsz;
  GNode *stco;
  GNode *co64;
  GNode *stts;
  GNode *stss;
  GNode *ctts;
  const guint8 *stsc_data, *stsz_data, *stco_data;
  int sample_size;
  int sample_index;
  int n_samples;
  int n_samples_per_chunk;
  int n_sample_times;
  QtDemuxSample *samples;
  gint i, j, k;
  int index;
  guint64 timestamp, time;

  /* sample to chunk */
  if (!(stsc = qtdemux_tree_get_child_by_type (stbl, FOURCC_stsc)))
    goto corrupt_file;
  stsc_data = (const guint8 *) stsc->data;
  /* sample size */
  if (!(stsz = qtdemux_tree_get_child_by_type (stbl, FOURCC_stsz)))
    goto corrupt_file;
  stsz_data = (const guint8 *) stsz->data;
  /* chunk offsets */
  stco = qtdemux_tree_get_child_by_type (stbl, FOURCC_stco);
  co64 = qtdemux_tree_get_child_by_type (stbl, FOURCC_co64);
  if (stco) {
    stco_data = (const guint8 *) stco->data;
  } else {
    stco_data = NULL;
    if (co64 == NULL)
      goto corrupt_file;
  }
  /* sample time */
  if (!(stts = qtdemux_tree_get_child_by_type (stbl, FOURCC_stts)))
    goto corrupt_file;

  /* sample sync, can be NULL */
  stss = qtdemux_tree_get_child_by_type (stbl, FOURCC_stss);

  sample_size = QT_UINT32 (stsz_data + 12);
  if (sample_size == 0 || stream->sampled) {
    n_samples = QT_UINT32 (stsz_data + 16);
    GST_DEBUG_OBJECT (qtdemux, "stsz sample_size 0, allocating n_samples %d",
        n_samples);
    stream->n_samples = n_samples;
    samples = g_new0 (QtDemuxSample, n_samples);
    stream->samples = samples;

    for (i = 0; i < n_samples; i++) {
      if (sample_size == 0)
        samples[i].size = QT_UINT32 (stsz_data + i * 4 + 20);
      else
        samples[i].size = sample_size;

      GST_LOG_OBJECT (qtdemux, "sample %d has size %d", i, samples[i].size);
      /* init other fields to defaults for this sample */
      samples[i].keyframe = FALSE;
    }
    n_samples_per_chunk = QT_UINT32 (stsc_data + 12);
    index = 0;
    for (i = 0; i < n_samples_per_chunk; i++) {
      guint32 first_chunk, last_chunk;
      guint32 samples_per_chunk;

      first_chunk = QT_UINT32 (stsc_data + 16 + i * 12 + 0) - 1;
      if (i == n_samples_per_chunk - 1) {
        last_chunk = G_MAXUINT32;
      } else {
        last_chunk = QT_UINT32 (stsc_data + 16 + i * 12 + 12) - 1;
      }
      samples_per_chunk = QT_UINT32 (stsc_data + 16 + i * 12 + 4);

      for (j = first_chunk; j < last_chunk; j++) {
        guint64 chunk_offset;

        if (stco) {
          chunk_offset = QT_UINT32 (stco_data + 16 + j * 4);
        } else {
          chunk_offset = QT_UINT64 ((guint8 *) co64->data + 16 + j * 8);
        }
        for (k = 0; k < samples_per_chunk; k++) {
          GST_LOG_OBJECT (qtdemux, "Creating entry %d with offset %lld",
              index, chunk_offset);
          samples[index].chunk = j;
          samples[index].offset = chunk_offset;
          chunk_offset += samples[index].size;
          index++;
          if (index >= n_samples)
            goto done2;
        }
      }
    }
  done2:

    n_sample_times = QT_UINT32 ((guint8 *) stts->data + 12);
    timestamp = 0;
    stream->min_duration = 0;
    time = 0;
    index = 0;
    for (i = 0; (i < n_sample_times) && (index < stream->n_samples); i++) {
      guint32 n;
      guint32 duration;

      n = QT_UINT32 ((guint8 *) stts->data + 16 + 8 * i);
      duration = QT_UINT32 ((guint8 *) stts->data + 16 + 8 * i + 4);
      for (j = 0; (j < n) && (index < stream->n_samples); j++) {
        GST_DEBUG_OBJECT (qtdemux, "sample %d: timestamp %" GST_TIME_FORMAT,
            index, GST_TIME_ARGS (timestamp));

        samples[index].timestamp = timestamp;
        /* take first duration for fps */
        if (stream->min_duration == 0)
          stream->min_duration = duration;
        /* add non-scaled values to avoid rounding errors */
        time += duration;
        timestamp = gst_util_uint64_scale (time, GST_SECOND, stream->timescale);
        samples[index].duration = timestamp - samples[index].timestamp;

        index++;
      }
    }
    if (stss) {
      /* mark keyframes */
      guint32 n_sample_syncs;

      n_sample_syncs = QT_UINT32 ((guint8 *) stss->data + 12);
      if (n_sample_syncs == 0) {
        stream->all_keyframe = TRUE;
      } else {
        offset = 16;
        for (i = 0; i < n_sample_syncs; i++) {
          /* note that the first sample is index 1, not 0 */
          index = QT_UINT32 ((guint8 *) stss->data + offset);
          if (index > 0 && index <= stream->n_samples) {
            samples[index - 1].keyframe = TRUE;
            offset += 4;
          }
        }
      }
    } else {
      /* no stss, all samples are keyframes */
      stream->all_keyframe = TRUE;
    }
  } else {
    GST_DEBUG_OBJECT (qtdemux,
        "stsz sample_size %d != 0, treating chunks as samples", sample_size);

    /* treat chunks as samples */
    if (stco) {
      n_samples = QT_UINT32 (stco_data + 12);
    } else {
      n_samples = QT_UINT32 ((guint8 *) co64->data + 12);
    }
    stream->n_samples = n_samples;
    GST_DEBUG_OBJECT (qtdemux, "allocating n_samples %d", n_samples);
    samples = g_new0 (QtDemuxSample, n_samples);
    stream->samples = samples;

    n_samples_per_chunk = QT_UINT32 (stsc_data + 12);
    GST_DEBUG_OBJECT (qtdemux, "n_samples_per_chunk %d", n_samples_per_chunk);
    sample_index = 0;
    timestamp = 0;
    for (i = 0; i < n_samples_per_chunk; i++) {
      guint32 first_chunk, last_chunk;
      guint32 samples_per_chunk;

      first_chunk = QT_UINT32 (stsc_data + 16 + i * 12 + 0) - 1;
      /* the last chunk of each entry is calculated by taking the first chunk
       * of the next entry; except if there is no next, where we fake it with
       * INT_MAX */
      if (i == n_samples_per_chunk - 1) {
        last_chunk = G_MAXUINT32;
      } else {
        last_chunk = QT_UINT32 (stsc_data + 16 + i * 12 + 12) - 1;
      }
      samples_per_chunk = QT_UINT32 (stsc_data + 16 + i * 12 + 4);

      GST_LOG_OBJECT (qtdemux,
          "entry %d has first_chunk %d, last_chunk %d, samples_per_chunk %d", i,
          first_chunk, last_chunk, samples_per_chunk);

      for (j = first_chunk; j < last_chunk; j++) {
        guint64 chunk_offset;

        if (j >= n_samples)
          goto done;

        if (stco) {
          chunk_offset = QT_UINT32 (stco_data + 16 + j * 4);
        } else {
          chunk_offset = QT_UINT64 ((guint8 *) co64->data + 16 + j * 8);
        }
        GST_LOG_OBJECT (qtdemux,
            "Creating entry %d with offset %" G_GUINT64_FORMAT, j,
            chunk_offset);

        samples[j].chunk = j;
        samples[j].offset = chunk_offset;

        if (stream->samples_per_frame * stream->bytes_per_frame) {
          samples[j].size = (samples_per_chunk * stream->n_channels) /
              stream->samples_per_frame * stream->bytes_per_frame;
        } else {
          samples[j].size = samples_per_chunk;
        }

        GST_DEBUG_OBJECT (qtdemux, "sample %d: timestamp %" GST_TIME_FORMAT
            ", size %u", j, GST_TIME_ARGS (timestamp), samples[j].size);

        samples[j].timestamp = timestamp;
        sample_index += samples_per_chunk;

        timestamp = gst_util_uint64_scale (sample_index,
            GST_SECOND, stream->timescale);
        samples[j].duration = timestamp - samples[j].timestamp;

        samples[j].keyframe = TRUE;
      }
    }
  }

  /* composition time to sample */
  if ((ctts = qtdemux_tree_get_child_by_type (stbl, FOURCC_ctts))) {
    const guint8 *ctts_data = (const guint8 *) ctts->data;
    guint32 n_entries = QT_UINT32 (ctts_data + 12);
    guint32 count;
    gint32 soffset;

    /* Fill in the pts_offsets */
    for (i = 0, j = 0; (j < stream->n_samples) && (i < n_entries); i++) {
      count = QT_UINT32 (ctts_data + 16 + i * 8);
      soffset = QT_UINT32 (ctts_data + 20 + i * 8);
      for (k = 0; (k < count) && (j < stream->n_samples); k++, j++) {
        /* we operate with very small soffset values here, it shouldn't overflow */
        samples[j].pts_offset = soffset * GST_SECOND / stream->timescale;
      }
    }
  }
done:
  return TRUE;

/* ERRORS */
corrupt_file:
  {
    GST_ELEMENT_ERROR (qtdemux, STREAM, DECODE,
        (_("This file is corrupt and cannot be played.")), (NULL));
    return FALSE;
  }
}

/* collect all segment info for @stream.
 */
static gboolean
qtdemux_parse_segments (GstQTDemux * qtdemux, QtDemuxStream * stream,
    GNode * trak)
{
  GNode *edts;

  /* parse and prepare segment info from the edit list */
  GST_DEBUG_OBJECT (qtdemux, "looking for edit list container");
  stream->n_segments = 0;
  stream->segments = NULL;
  if ((edts = qtdemux_tree_get_child_by_type (trak, FOURCC_edts))) {
    GNode *elst;
    gint n_segments;
    gint i, count;
    guint64 time, stime;
    guint8 *buffer;

    GST_DEBUG_OBJECT (qtdemux, "looking for edit list");
    if (!(elst = qtdemux_tree_get_child_by_type (edts, FOURCC_elst)))
      goto done;

    buffer = elst->data;

    n_segments = QT_UINT32 (buffer + 12);

    /* we might allocate a bit too much, at least allocate 1 segment */
    stream->segments = g_new (QtDemuxSegment, MAX (n_segments, 1));

    /* segments always start from 0 */
    time = 0;
    stime = 0;
    count = 0;
    for (i = 0; i < n_segments; i++) {
      guint64 duration;
      guint64 media_time;
      QtDemuxSegment *segment;
      guint32 rate_int;

      media_time = QT_UINT32 (buffer + 20 + i * 12);

      /* -1 media time is an empty segment, just ignore it */
      if (media_time == G_MAXUINT32)
        continue;

      duration = QT_UINT32 (buffer + 16 + i * 12);

      segment = &stream->segments[count++];

      /* time and duration expressed in global timescale */
      segment->time = stime;
      /* add non scaled values so we don't cause roundoff errors */
      time += duration;
      stime = gst_util_uint64_scale (time, GST_SECOND, qtdemux->timescale);
      segment->stop_time = stime;
      segment->duration = stime - segment->time;
      /* media_time expressed in stream timescale */
      segment->media_start =
          gst_util_uint64_scale (media_time, GST_SECOND, stream->timescale);
      segment->media_stop = segment->media_start + segment->duration;
      rate_int = GST_READ_UINT32_BE (buffer + 24 + i * 12);

      if (rate_int <= 1) {
        /* 0 is not allowed, some programs write 1 instead of the floating point
         * value */
        GST_WARNING_OBJECT (qtdemux, "found suspicious rate %" G_GUINT32_FORMAT,
            rate_int);
        segment->rate = 1;
      } else {
        segment->rate = rate_int / 65536.0;
      }

      GST_DEBUG_OBJECT (qtdemux, "created segment %d time %" GST_TIME_FORMAT
          ", duration %" GST_TIME_FORMAT ", media_time %" GST_TIME_FORMAT
          ", rate %g, (%d)", i, GST_TIME_ARGS (segment->time),
          GST_TIME_ARGS (segment->duration),
          GST_TIME_ARGS (segment->media_start), segment->rate, rate_int);
    }
    GST_DEBUG_OBJECT (qtdemux, "found %d non-empty segments", count);
    stream->n_segments = count;
  }
done:

  /* no segments, create one to play the complete trak */
  if (stream->n_segments == 0) {
    if (stream->segments == NULL)
      stream->segments = g_new (QtDemuxSegment, 1);

    stream->segments[0].time = 0;
    stream->segments[0].stop_time = qtdemux->segment.duration;
    stream->segments[0].duration = qtdemux->segment.duration;
    stream->segments[0].media_start = 0;
    stream->segments[0].media_stop = qtdemux->segment.duration;
    stream->segments[0].rate = 1.0;

    GST_DEBUG_OBJECT (qtdemux, "created dummy segment");
    stream->n_segments = 1;
  }
  GST_DEBUG_OBJECT (qtdemux, "using %d segments", stream->n_segments);

  return TRUE;
}

/* parse the traks.
 * With each track we associate a new QtDemuxStream that contains all the info
 * about the trak.
 * traks that do not decode to something (like strm traks) will not have a pad.
 */
static gboolean
qtdemux_parse_trak (GstQTDemux * qtdemux, GNode * trak)
{
  int offset;
  GNode *tkhd;
  GNode *mdia;
  GNode *mdhd;
  GNode *hdlr;
  GNode *minf;
  GNode *stbl;
  GNode *stsd;
  GNode *mp4a;
  GNode *mp4v;
  GNode *wave;
  GNode *esds;
  QtDemuxStream *stream;
  GstTagList *list = NULL;
  gchar *codec = NULL;
  const guint8 *stsd_data;
  guint32 version;

  stream = g_new0 (QtDemuxStream, 1);
  /* new streams always need a discont */
  stream->discont = TRUE;
  /* we enable clipping for raw audio/video streams */
  stream->need_clip = FALSE;
  stream->segment_index = -1;
  stream->time_position = 0;
  stream->sample_index = -1;
  stream->last_ret = GST_FLOW_OK;

  if (!(tkhd = qtdemux_tree_get_child_by_type (trak, FOURCC_tkhd)))
    goto corrupt_file;

  GST_LOG_OBJECT (qtdemux, "track[tkhd] version/flags: 0x%08x",
      QT_UINT32 ((guint8 *) tkhd->data + 8));

  if (!(mdia = qtdemux_tree_get_child_by_type (trak, FOURCC_mdia)))
    goto corrupt_file;

  if (!(mdhd = qtdemux_tree_get_child_by_type (mdia, FOURCC_mdhd))) {
    /* be nice for some crooked mjp2 files that use mhdr for mdhd */
    if (qtdemux->major_brand != FOURCC_mjp2 ||
        !(mdhd = qtdemux_tree_get_child_by_type (mdia, FOURCC_mhdr)))
      goto corrupt_file;
  }

  version = QT_UINT32 ((guint8 *) mdhd->data + 8);
  GST_LOG_OBJECT (qtdemux, "track version/flags: %08x", version);
  if (version == 0x01000000) {
    stream->timescale = QT_UINT32 ((guint8 *) mdhd->data + 28);
    stream->duration = QT_UINT64 ((guint8 *) mdhd->data + 32);
  } else {
    stream->timescale = QT_UINT32 ((guint8 *) mdhd->data + 20);
    stream->duration = QT_UINT32 ((guint8 *) mdhd->data + 24);
  }

  GST_LOG_OBJECT (qtdemux, "track timescale: %" G_GUINT32_FORMAT,
      stream->timescale);
  GST_LOG_OBJECT (qtdemux, "track duration: %" G_GUINT64_FORMAT,
      stream->duration);

  if (qtdemux->duration != G_MAXINT32 && stream->duration != G_MAXINT32) {
    guint64 tdur1, tdur2;

    /* don't overflow */
    tdur1 = stream->timescale * (guint64) qtdemux->duration;
    tdur2 = qtdemux->timescale * (guint64) stream->duration;

    /* HACK:
     * some of those trailers, nowadays, have prologue images that are
     * themselves vide tracks as well. I haven't really found a way to
     * identify those yet, except for just looking at their duration. */
    if (tdur1 != 0 && (tdur2 * 10 / tdur1) < 2) {
      GST_WARNING_OBJECT (qtdemux,
          "Track shorter than 20%% (%" G_GUINT64_FORMAT "/%" G_GUINT32_FORMAT
          " vs. %" G_GUINT32_FORMAT "/%" G_GUINT32_FORMAT ") of the stream "
          "found, assuming preview image or something; skipping track",
          stream->duration, stream->timescale, qtdemux->duration,
          qtdemux->timescale);
      g_free (stream);
      return TRUE;
    }
  }

  if (!(hdlr = qtdemux_tree_get_child_by_type (mdia, FOURCC_hdlr)))
    goto corrupt_file;

  GST_LOG_OBJECT (qtdemux, "track type: %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (QT_FOURCC ((guint8 *) hdlr->data + 12)));

  stream->subtype = QT_FOURCC ((guint8 *) hdlr->data + 16);
  GST_LOG_OBJECT (qtdemux, "track subtype: %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (stream->subtype));

  if (!(minf = qtdemux_tree_get_child_by_type (mdia, FOURCC_minf)))
    goto corrupt_file;

  if (!(stbl = qtdemux_tree_get_child_by_type (minf, FOURCC_stbl)))
    goto corrupt_file;

  /* parse stsd */
  if (!(stsd = qtdemux_tree_get_child_by_type (stbl, FOURCC_stsd)))
    goto corrupt_file;
  stsd_data = (const guint8 *) stsd->data;

  if (stream->subtype == FOURCC_vide) {
    guint32 fourcc;

    stream->sampled = TRUE;

    offset = 16;
    stream->fourcc = fourcc = QT_FOURCC (stsd_data + offset + 4);
    GST_LOG_OBJECT (qtdemux, "st type:          %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (fourcc));

    stream->width = QT_UINT16 (stsd_data + offset + 32);
    stream->height = QT_UINT16 (stsd_data + offset + 34);
    stream->fps_n = 0;          /* this is filled in later */
    stream->fps_d = 0;          /* this is filled in later */
    stream->bits_per_sample = QT_UINT16 (stsd_data + offset + 82);
    stream->color_table_id = QT_UINT16 (stsd_data + offset + 84);

    GST_LOG_OBJECT (qtdemux, "frame count:   %u",
        QT_UINT16 (stsd_data + offset + 48));

    if (fourcc == FOURCC_drms)
      goto error_encrypted;

    stream->caps =
        qtdemux_video_caps (qtdemux, stream, fourcc, stsd_data, &codec);
    if (codec) {
      list = gst_tag_list_new ();
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_VIDEO_CODEC, codec, NULL);
      g_free (codec);
      codec = NULL;
    }

    esds = NULL;
    mp4v = qtdemux_tree_get_child_by_type (stsd, FOURCC_mp4v);
    if (mp4v)
      esds = qtdemux_tree_get_child_by_type (mp4v, FOURCC_esds);

    if (esds) {
      gst_qtdemux_handle_esds (qtdemux, stream, esds, list);
    } else {
      switch (fourcc) {
        case FOURCC_avc1:
        {
          gint len = QT_UINT32 (stsd_data) - 0x66;
          const guint8 *avc_data = stsd_data + 0x66;

          /* find avcC */
          while (len >= 0x8 &&
              QT_FOURCC (avc_data + 0x4) != FOURCC_avcC &&
              QT_UINT32 (avc_data) < len) {
            len -= QT_UINT32 (avc_data);
            avc_data += QT_UINT32 (avc_data);
          }

          /* parse, if found */
          if (len > 0x8 && QT_FOURCC (avc_data + 0x4) == FOURCC_avcC) {
            GstBuffer *buf;
            gint size;

            if (QT_UINT32 (avc_data) < len)
              size = QT_UINT32 (avc_data) - 0x8;
            else
              size = len - 0x8;

            GST_DEBUG_OBJECT (qtdemux, "found avcC codec_data in stsd");

            buf = gst_buffer_new_and_alloc (size);
            memcpy (GST_BUFFER_DATA (buf), avc_data + 0x8, size);
            gst_caps_set_simple (stream->caps,
                "codec_data", GST_TYPE_BUFFER, buf, NULL);
            gst_buffer_unref (buf);
          }
          break;
        }
        case FOURCC_mjp2:
        {
          GNode *jp2h, *colr, *mjp2, *field, *prefix;
          const guint8 *data;
          guint32 fourcc = 0;

          GST_DEBUG_OBJECT (qtdemux, "found mjp2");
          /* some required atoms */
          mjp2 = qtdemux_tree_get_child_by_type (stsd, FOURCC_mjp2);
          if (!mjp2)
            break;
          jp2h = qtdemux_tree_get_child_by_type (mjp2, FOURCC_jp2h);
          if (!jp2h)
            break;
          colr = qtdemux_tree_get_child_by_type (jp2h, FOURCC_colr);
          if (!colr)
            break;
          GST_DEBUG_OBJECT (qtdemux, "found colr");
          /* try to extract colour space info */
          if (QT_UINT8 (colr->data + 8) == 1) {
            switch (QT_UINT32 (colr->data + 11)) {
              case 16:
                fourcc = GST_MAKE_FOURCC ('s', 'R', 'G', 'B');
                break;
              case 17:
                fourcc = GST_MAKE_FOURCC ('G', 'R', 'A', 'Y');
                break;
              case 18:
                fourcc = GST_MAKE_FOURCC ('s', 'Y', 'U', 'V');
                break;
              default:
                break;
            }
          }

          if (fourcc)
            gst_caps_set_simple (stream->caps,
                "fourcc", GST_TYPE_FOURCC, fourcc, NULL);

          /* some optional atoms */
          field = qtdemux_tree_get_child_by_type (mjp2, FOURCC_fiel);
          prefix = qtdemux_tree_get_child_by_type (mjp2, FOURCC_jp2x);

          /* indicate possible fields in caps */
          if (field) {
            data = field->data + 8;
            if (*data != 1)
              gst_caps_set_simple (stream->caps, "fields", G_TYPE_INT,
                  (gint) * data, NULL);
          }
          /* add codec_data if provided */
          if (prefix) {
            GstBuffer *buf;
            gint len;

            GST_DEBUG_OBJECT (qtdemux, "found prefix data in stsd");
            data = prefix->data;
            len = QT_UINT32 (data);
            if (len > 0x8) {
              len -= 0x8;
              buf = gst_buffer_new_and_alloc (len);
              memcpy (GST_BUFFER_DATA (buf), data + 8, len);
              gst_caps_set_simple (stream->caps,
                  "codec_data", GST_TYPE_BUFFER, buf, NULL);
              gst_buffer_unref (buf);
            }
          }
          break;
        }
        case FOURCC_SVQ3:
        case FOURCC_VP31:
        {
          GstBuffer *buf;
          gint len = QT_UINT32 (stsd_data);

          GST_DEBUG_OBJECT (qtdemux, "found codec_data in stsd");

          buf = gst_buffer_new_and_alloc (len);
          memcpy (GST_BUFFER_DATA (buf), stsd_data, len);
          gst_caps_set_simple (stream->caps,
              "codec_data", GST_TYPE_BUFFER, buf, NULL);
          gst_buffer_unref (buf);
          break;
        }
        case FOURCC_rle_:
        {
          gst_caps_set_simple (stream->caps,
              "depth", G_TYPE_INT, QT_UINT16 (stsd_data + offset + 82), NULL);
          break;
        }
        case FOURCC_XiTh:
        {
          GNode *xith, *xdxt;

          GST_DEBUG_OBJECT (qtdemux, "found XiTh");
          xith = qtdemux_tree_get_child_by_type (stsd, FOURCC_XiTh);
          if (!xith)
            break;

          xdxt = qtdemux_tree_get_child_by_type (xith, FOURCC_XdxT);
          if (!xdxt)
            break;

          GST_DEBUG_OBJECT (qtdemux, "found XdxT node");
          /* collect the headers and store them in a stream list so that we can
           * send them out first */
          qtdemux_parse_theora_extension (qtdemux, stream, xdxt);
          break;
        }
        default:
          break;
      }
    }

    GST_INFO_OBJECT (qtdemux,
        "type %" GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (fourcc), stream->caps);

  } else if (stream->subtype == FOURCC_soun) {
    int version, samplesize;
    guint32 fourcc;
    int len;
    guint16 compression_id;

    len = QT_UINT32 (stsd_data + 16);
    GST_LOG_OBJECT (qtdemux, "stsd len:           %d", len);

    stream->fourcc = fourcc = QT_FOURCC (stsd_data + 16 + 4);
    GST_LOG_OBJECT (qtdemux, "stsd type:          %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (stream->fourcc));

    offset = 32;

    version = QT_UINT32 (stsd_data + offset);
    stream->n_channels = QT_UINT16 (stsd_data + offset + 8);
    samplesize = QT_UINT16 (stsd_data + offset + 10);
    compression_id = QT_UINT16 (stsd_data + offset + 12);
    stream->rate = QT_FP32 (stsd_data + offset + 16);

    GST_LOG_OBJECT (qtdemux, "version/rev:      %08x", version);
    GST_LOG_OBJECT (qtdemux, "vendor:           %08x",
        QT_UINT32 (stsd_data + offset + 4));
    GST_LOG_OBJECT (qtdemux, "n_channels:       %d", stream->n_channels);
    GST_LOG_OBJECT (qtdemux, "sample_size:      %d", samplesize);
    GST_LOG_OBJECT (qtdemux, "compression_id:   %d", compression_id);
    GST_LOG_OBJECT (qtdemux, "packet size:      %d",
        QT_UINT16 (stsd_data + offset + 14));
    GST_LOG_OBJECT (qtdemux, "sample rate:      %g", stream->rate);

    if (compression_id == 0xfffe)
      stream->sampled = TRUE;

    /* first assume uncompressed audio */
    stream->bytes_per_sample = samplesize / 8;
    stream->samples_per_frame = stream->n_channels;
    stream->bytes_per_frame = stream->n_channels * stream->bytes_per_sample;
    stream->samples_per_packet = stream->samples_per_frame;
    stream->bytes_per_packet = stream->bytes_per_sample;

    offset = 52;
    switch (fourcc) {
        /* Yes, these have to be hard-coded */
      case FOURCC_MAC6:
      {
        stream->samples_per_packet = 6;
        stream->bytes_per_packet = 1;
        stream->bytes_per_frame = 1 * stream->n_channels;
        stream->bytes_per_sample = 1;
        stream->samples_per_frame = 6 * stream->n_channels;
        break;
      }
      case FOURCC_MAC3:
      {
        stream->samples_per_packet = 3;
        stream->bytes_per_packet = 1;
        stream->bytes_per_frame = 1 * stream->n_channels;
        stream->bytes_per_sample = 1;
        stream->samples_per_frame = 3 * stream->n_channels;
        break;
      }
      case FOURCC_ima4:
      {
        stream->samples_per_packet = 64;
        stream->bytes_per_packet = 34;
        stream->bytes_per_frame = 34 * stream->n_channels;
        stream->bytes_per_sample = 2;
        stream->samples_per_frame = 64 * stream->n_channels;
        break;
      }
      case FOURCC_ulaw:
      case FOURCC_alaw:
      {
        stream->samples_per_packet = 1;
        stream->bytes_per_packet = 1;
        stream->bytes_per_frame = 1 * stream->n_channels;
        stream->bytes_per_sample = 1;
        stream->samples_per_frame = 1 * stream->n_channels;
        break;
      }
      default:
        break;
    }
    if (version == 0x00010000) {
      switch (fourcc) {
        case FOURCC_twos:
        case FOURCC_sowt:
        case FOURCC_raw_:
          break;
        default:
        {
          /* only parse extra decoding config for non-pcm audio */
          stream->samples_per_packet = QT_UINT32 (stsd_data + offset);
          stream->bytes_per_packet = QT_UINT32 (stsd_data + offset + 4);
          stream->bytes_per_frame = QT_UINT32 (stsd_data + offset + 8);
          stream->bytes_per_sample = QT_UINT32 (stsd_data + offset + 12);

          GST_LOG_OBJECT (qtdemux, "samples/packet:   %d",
              stream->samples_per_packet);
          GST_LOG_OBJECT (qtdemux, "bytes/packet:     %d",
              stream->bytes_per_packet);
          GST_LOG_OBJECT (qtdemux, "bytes/frame:      %d",
              stream->bytes_per_frame);
          GST_LOG_OBJECT (qtdemux, "bytes/sample:     %d",
              stream->bytes_per_sample);

          if (!stream->sampled && stream->bytes_per_packet) {
            stream->samples_per_frame = (stream->bytes_per_frame /
                stream->bytes_per_packet) * stream->samples_per_packet;
            GST_LOG_OBJECT (qtdemux, "samples/frame:    %d",
                stream->samples_per_frame);
          }
          break;
        }
      }
      offset = 68;
    } else if (version == 0x00020000) {
      union
      {
        gdouble fp;
        guint64 val;
      } qtfp;

      stream->samples_per_packet = QT_UINT32 (stsd_data + offset);
      qtfp.val = QT_UINT64 (stsd_data + offset + 4);
      stream->rate = qtfp.fp;
      stream->n_channels = QT_UINT32 (stsd_data + offset + 12);

      GST_LOG_OBJECT (qtdemux, "samples/packet:   %d",
          stream->samples_per_packet);
      GST_LOG_OBJECT (qtdemux, "sample rate:      %g", stream->rate);
      GST_LOG_OBJECT (qtdemux, "n_channels:       %d", stream->n_channels);

      offset = 68;
    } else {
      GST_WARNING_OBJECT (qtdemux, "unknown version %08x", version);
    }

    if (fourcc == FOURCC_drms)
      goto error_encrypted;

    stream->caps = qtdemux_audio_caps (qtdemux, stream, fourcc, NULL, 0,
        &codec);

    if (codec) {
      list = gst_tag_list_new ();
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_AUDIO_CODEC, codec, NULL);
      g_free (codec);
      codec = NULL;
    }

    mp4a = qtdemux_tree_get_child_by_type (stsd, FOURCC_mp4a);
    wave = NULL;
    esds = NULL;
    if (mp4a) {
      wave = qtdemux_tree_get_child_by_type (mp4a, FOURCC_wave);
      if (wave)
        esds = qtdemux_tree_get_child_by_type (wave, FOURCC_esds);
      if (!esds)
        esds = qtdemux_tree_get_child_by_type (mp4a, FOURCC_esds);
    }

    if (esds) {
      gst_qtdemux_handle_esds (qtdemux, stream, esds, list);
    } else {
      switch (fourcc) {
#if 0
          /* FIXME: what is in the chunk? */
        case FOURCC_QDMC:
        {
          gint len = QT_UINT32 (stsd_data);

          /* seems to be always = 116 = 0x74 */
          break;
        }
#endif
        case FOURCC_QDM2:
        {
          gint len = QT_UINT32 (stsd_data);

          if (len > 0x4C) {
            GstBuffer *buf = gst_buffer_new_and_alloc (len - 0x4C);

            memcpy (GST_BUFFER_DATA (buf), stsd_data + 0x4C, len - 0x4C);
            gst_caps_set_simple (stream->caps,
                "codec_data", GST_TYPE_BUFFER, buf, NULL);
            gst_buffer_unref (buf);
          }
          gst_caps_set_simple (stream->caps,
              "samplesize", G_TYPE_INT, samplesize, NULL);
          break;
        }
        case FOURCC_alac:
        {
          gint len = QT_UINT32 (stsd_data);

          if (len > 0x34) {
            GstBuffer *buf = gst_buffer_new_and_alloc (len - 0x34);

            memcpy (GST_BUFFER_DATA (buf), stsd_data + 0x34, len - 0x34);
            gst_caps_set_simple (stream->caps,
                "codec_data", GST_TYPE_BUFFER, buf, NULL);
            gst_buffer_unref (buf);
          }
          gst_caps_set_simple (stream->caps,
              "samplesize", G_TYPE_INT, samplesize, NULL);
          break;
        }
        case FOURCC_samr:
        {
          gint len = QT_UINT32 (stsd_data);

          if (len > 0x34) {
            GstBuffer *buf = gst_buffer_new_and_alloc (len - 0x34);

            memcpy (GST_BUFFER_DATA (buf), stsd_data + 0x34, len - 0x34);

            gst_caps_set_simple (stream->caps,
                "codec_data", GST_TYPE_BUFFER, buf, NULL);
            gst_buffer_unref (buf);
          }
          break;
        }
        default:
          break;
      }
    }
    GST_INFO_OBJECT (qtdemux,
        "type %" GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (fourcc), stream->caps);

  } else if (stream->subtype == FOURCC_strm) {
    guint32 fourcc;

    stream->fourcc = fourcc = QT_FOURCC (stsd_data + 16 + 4);
    GST_LOG_OBJECT (qtdemux, "stsd type:          %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (fourcc));

    if (fourcc != FOURCC_rtsp) {
      GST_INFO_OBJECT (qtdemux, "unhandled stream type %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (fourcc));
      goto unknown_stream;
    }
    stream->sampled = TRUE;
  } else {
    goto unknown_stream;
  }

  /* promote to sampled format */
  if (stream->fourcc == FOURCC_samr) {
    /* force mono 8000 Hz for AMR */
    stream->sampled = TRUE;
    stream->n_channels = 1;
    stream->rate = 8000;
  } else if (stream->fourcc == FOURCC_sawb) {
    /* force mono 16000 Hz for AMR-WB */
    stream->sampled = TRUE;
    stream->n_channels = 1;
    stream->rate = 16000;
  } else if (stream->fourcc == FOURCC_mp4a) {
    stream->sampled = TRUE;
  }

  /* collect sample information */
  if (!qtdemux_parse_samples (qtdemux, stream, stbl))
    goto samples_failed;

  /* configure segments */
  if (!qtdemux_parse_segments (qtdemux, stream, trak))
    goto segments_failed;

  /* now we are ready to add the stream */
  gst_qtdemux_add_stream (qtdemux, stream, list);

  return TRUE;

/* ERRORS */
corrupt_file:
  {
    GST_ELEMENT_ERROR (qtdemux, STREAM, DECODE,
        (_("This file is corrupt and cannot be played.")), (NULL));
    g_free (stream);
    return FALSE;
  }
error_encrypted:
  {
    GST_ELEMENT_ERROR (qtdemux, STREAM, DECRYPT, (NULL), (NULL));
    g_free (stream);
    return FALSE;
  }
samples_failed:
  {
    /* we posted an error already */
    g_free (stream);
    return FALSE;
  }
segments_failed:
  {
    /* we posted an error already */
    g_free (stream);
    return FALSE;
  }
unknown_stream:
  {
    GST_INFO_OBJECT (qtdemux, "unknown subtype %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (stream->subtype));
    g_free (stream);
    return TRUE;
  }
}

static void
qtdemux_tag_add_str (GstQTDemux * qtdemux, const char *tag, const char *dummy,
    GNode * node)
{
  const gchar *env_vars[] = { "GST_QT_TAG_ENCODING", "GST_TAG_ENCODING", NULL };
  GNode *data;
  char *s;
  int len;
  int type;
  int offset;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QT_UINT32 (data->data);
    type = QT_UINT32 ((guint8 *) data->data + 8);
    if (type == 0x00000001) {
      s = gst_tag_freeform_string_to_utf8 ((char *) data->data + 16, len - 16,
          env_vars);
      if (s) {
        GST_DEBUG_OBJECT (qtdemux, "adding tag %s", GST_STR_NULL (s));
        gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE, tag, s,
            NULL);
        g_free (s);
      } else {
        GST_DEBUG_OBJECT (qtdemux, "failed to convert %s tag to UTF-8", tag);
      }
    }
  } else {
    len = QT_UINT32 (node->data);
    type = QT_UINT32 ((guint8 *) node->data + 4);
    if (type & 0xa9000000) {
      /* Type starts with the (C) symbol, so the next 32 bits are
       * the language code, which we ignore */
      offset = 12;
      GST_DEBUG_OBJECT (qtdemux, "found international text tag");
    } else {
      offset = 8;
      GST_DEBUG_OBJECT (qtdemux, "found normal text tag");
    }
    s = gst_tag_freeform_string_to_utf8 ((char *) node->data + offset,
        len - offset, env_vars);
    if (s) {
      GST_DEBUG_OBJECT (qtdemux, "adding tag %s", GST_STR_NULL (s));
      gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE, tag, s, NULL);
      g_free (s);
    } else {
      GST_DEBUG_OBJECT (qtdemux, "failed to convert %s tag to UTF-8", tag);
    }
  }
}

static void
qtdemux_tag_add_num (GstQTDemux * qtdemux, const char *tag1,
    const char *tag2, GNode * node)
{
  GNode *data;
  int len;
  int type;
  int n1, n2;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QT_UINT32 (data->data);
    type = QT_UINT32 ((guint8 *) data->data + 8);
    if (type == 0x00000000 && len >= 22) {
      n1 = QT_UINT16 ((guint8 *) data->data + 18);
      n2 = QT_UINT16 ((guint8 *) data->data + 20);
      GST_DEBUG_OBJECT (qtdemux, "adding tag %d/%d", n1, n2);
      gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE,
          tag1, n1, tag2, n2, NULL);
    }
  }
}

static void
qtdemux_tag_add_tmpo (GstQTDemux * qtdemux, const char *tag1, const char *dummy,
    GNode * node)
{
  GNode *data;
  int len;
  int type;
  int n1;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QT_UINT32 (data->data);
    type = QT_UINT32 ((guint8 *) data->data + 8);
    GST_DEBUG_OBJECT (qtdemux, "have tempo tag, type=%d,len=%d", type, len);
    /* some files wrongly have a type 0x0f=15, but it should be 0x15 */
    if ((type == 0x00000015 || type == 0x0000000f) && len >= 18) {
      n1 = QT_UINT16 ((guint8 *) data->data + 16);
      if (n1) {
        /* do not add bpm=0 */
        GST_DEBUG_OBJECT (qtdemux, "adding tag %d", n1);
        gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE,
            tag1, (gdouble) n1, NULL);
      }
    }
  }
}

static void
qtdemux_tag_add_covr (GstQTDemux * qtdemux, const char *tag1, const char *dummy,
    GNode * node)
{
  GNode *data;
  int len;
  int type;
  GstBuffer *buf;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QT_UINT32 (data->data);
    type = QT_UINT32 ((guint8 *) data->data + 8);
    GST_DEBUG_OBJECT (qtdemux, "have covr tag, type=%d,len=%d", type, len);
    if ((type == 0x0000000d || type == 0x0000000e) && len > 16) {
      if ((buf = gst_tag_image_data_to_image_buffer (data->data + 16, len - 16,
                  GST_TAG_IMAGE_TYPE_NONE))) {
        GST_DEBUG_OBJECT (qtdemux, "adding tag size %d", len - 16);
        gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE,
            tag1, buf, NULL);
      }
    }
  }
}

static void
qtdemux_tag_add_date (GstQTDemux * qtdemux, const char *tag, const char *dummy,
    GNode * node)
{
  GNode *data;
  char *s;
  int len;
  int type;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QT_UINT32 (data->data);
    type = QT_UINT32 ((guint8 *) data->data + 8);
    if (type == 0x00000001) {
      guint y, m = 1, d = 1;
      gint ret;

      s = g_strndup ((char *) data->data + 16, len - 16);
      GST_DEBUG_OBJECT (qtdemux, "adding date '%s'", s);
      ret = sscanf (s, "%u-%u-%u", &y, &m, &d);
      if (ret >= 1 && y > 1500 && y < 3000) {
        GDate *date;

        date = g_date_new_dmy (d, m, y);
        gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE, tag,
            date, NULL);
        g_date_free (date);
      } else {
        GST_DEBUG_OBJECT (qtdemux, "could not parse date string '%s'", s);
      }
      g_free (s);
    }
  }
}

static void
qtdemux_tag_add_gnre (GstQTDemux * qtdemux, const char *tag, const char *dummy,
    GNode * node)
{
  static const gchar *genres[] = {
    "N/A", "Blues", "Classic Rock", "Country", "Dance", "Disco",
    "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies",
    "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno",
    "Industrial", "Alternative", "Ska", "Death Metal", "Pranks",
    "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal",
    "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental",
    "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
    "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative",
    "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
    "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk",
    "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
    "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American",
    "Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes",
    "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka",
    "Retro", "Musical", "Rock & Roll", "Hard Rock", "Folk",
    "Folk/Rock", "National Folk", "Swing", "Fast-Fusion", "Bebob",
    "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
    "Gothic Rock", "Progressive Rock", "Psychedelic Rock",
    "Symphonic Rock", "Slow Rock", "Big Band", "Chorus",
    "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson",
    "Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass",
    "Primus", "Porn Groove", "Satire", "Slow Jam", "Club", "Tango",
    "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul",
    "Freestyle", "Duet", "Punk Rock", "Drum Solo", "A capella",
    "Euro-House", "Dance Hall", "Goa", "Drum & Bass", "Club House",
    "Hardcore", "Terror", "Indie", "BritPop", "NegerPunk",
    "Polsk Punk", "Beat", "Christian Gangsta", "Heavy Metal",
    "Black Metal", "Crossover", "Contemporary C", "Christian Rock",
    "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop", "SynthPop"
  };
  GNode *data;
  int len;
  int type;
  int n;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QT_UINT32 (data->data);
    type = QT_UINT32 ((guint8 *) data->data + 8);
    if (type == 0x00000000 && len >= 18) {
      n = QT_UINT16 ((guint8 *) data->data + 16);
      if (n > 0 && n < sizeof (genres) / sizeof (char *)) {
        GST_DEBUG_OBJECT (qtdemux, "adding %d [%s]", n, genres[n]);
        gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE,
            tag, genres[n], NULL);
      }
    }
  }
}

typedef void (*GstQTDemuxAddTagFunc) (GstQTDemux * demux,
    const char *tag, const char *tag_bis, GNode * node);

static const struct
{
  guint32 fourcc;
  const gchar *gst_tag;
  const gchar *gst_tag_bis;
  const GstQTDemuxAddTagFunc func;
} add_funcs[] = {
  {
  FOURCC__nam, GST_TAG_TITLE, NULL, qtdemux_tag_add_str}, {
  FOURCC__grp, GST_TAG_ARTIST, NULL, qtdemux_tag_add_str}, {
  FOURCC__wrt, GST_TAG_ARTIST, NULL, qtdemux_tag_add_str}, {
  FOURCC__ART, GST_TAG_ARTIST, NULL, qtdemux_tag_add_str}, {
  FOURCC__alb, GST_TAG_ALBUM, NULL, qtdemux_tag_add_str}, {
  FOURCC_cprt, GST_TAG_COPYRIGHT, NULL, qtdemux_tag_add_str}, {
  FOURCC__cpy, GST_TAG_COPYRIGHT, NULL, qtdemux_tag_add_str}, {
  FOURCC__cmt, GST_TAG_COMMENT, NULL, qtdemux_tag_add_str}, {
  FOURCC__des, GST_TAG_DESCRIPTION, NULL, qtdemux_tag_add_str}, {
  FOURCC__day, GST_TAG_DATE, NULL, qtdemux_tag_add_date}, {
  FOURCC__too, GST_TAG_COMMENT, NULL, qtdemux_tag_add_str}, {
  FOURCC_trkn, GST_TAG_TRACK_NUMBER, GST_TAG_TRACK_COUNT, qtdemux_tag_add_num}, {
  FOURCC_disk, GST_TAG_ALBUM_VOLUME_NUMBER, GST_TAG_ALBUM_VOLUME_COUNT,
        qtdemux_tag_add_num}, {
  FOURCC_disc, GST_TAG_ALBUM_VOLUME_NUMBER, GST_TAG_ALBUM_VOLUME_COUNT,
        qtdemux_tag_add_num}, {
  FOURCC__gen, GST_TAG_GENRE, NULL, qtdemux_tag_add_str}, {
  FOURCC_gnre, GST_TAG_GENRE, NULL, qtdemux_tag_add_gnre}, {
  FOURCC_tmpo, GST_TAG_BEATS_PER_MINUTE, NULL, qtdemux_tag_add_tmpo}, {
  FOURCC_covr, GST_TAG_PREVIEW_IMAGE, NULL, qtdemux_tag_add_covr}, {
  FOURCC_kywd, GST_TAG_KEYWORDS, NULL, qtdemux_tag_add_str}, {
  FOURCC_keyw, GST_TAG_KEYWORDS, NULL, qtdemux_tag_add_str}
};

static void
qtdemux_tag_add_blob (GNode * node, GstQTDemux * demux)
{
  gint len;
  guint8 *data;
  GstBuffer *buf;
  gchar *media_type, *style;
  GstCaps *caps;

  data = node->data;
  len = QT_UINT32 (data);
  buf = gst_buffer_new_and_alloc (len);
  memcpy (GST_BUFFER_DATA (buf), data, len);

  /* heuristic to determine style of tag */
  if (QT_FOURCC (data + 4) == FOURCC_____ ||
      (len > 8 + 12 && QT_FOURCC (data + 12) == FOURCC_data))
    style = "itunes";
  else if (demux->major_brand == GST_MAKE_FOURCC ('q', 't', ' ', ' '))
    style = "quicktime";
  /* fall back to assuming iso/3gp tag style */
  else
    style = "iso";

  media_type = g_strdup_printf ("application/x-gst-qt-%c%c%c%c-tag",
      g_ascii_tolower (data[4]), g_ascii_tolower (data[5]),
      g_ascii_tolower (data[6]), g_ascii_tolower (data[7]));
  caps = gst_caps_new_simple (media_type, "style", G_TYPE_STRING, style, NULL);
  gst_buffer_set_caps (buf, caps);
  gst_caps_unref (caps);
  g_free (media_type);

  GST_DEBUG_OBJECT (demux, "adding private tag; size %d, caps %" GST_PTR_FORMAT,
      GST_BUFFER_SIZE (buf), caps);

  gst_tag_list_add (demux->tag_list, GST_TAG_MERGE_APPEND,
      GST_QT_DEMUX_PRIVATE_TAG, buf, NULL);
  gst_buffer_unref (buf);
}

static void
qtdemux_parse_udta (GstQTDemux * qtdemux, GNode * udta)
{
  GNode *meta;
  GNode *ilst;
  GNode *node;
  gint i;

  meta = qtdemux_tree_get_child_by_type (udta, FOURCC_meta);
  if (meta != NULL) {
    ilst = qtdemux_tree_get_child_by_type (meta, FOURCC_ilst);
    if (ilst == NULL) {
      GST_LOG_OBJECT (qtdemux, "no ilst");
      return;
    }
  } else {
    ilst = udta;
    GST_LOG_OBJECT (qtdemux, "no meta so using udta itself");
  }

  GST_DEBUG_OBJECT (qtdemux, "new tag list");
  qtdemux->tag_list = gst_tag_list_new ();

  for (i = 0; i < G_N_ELEMENTS (add_funcs); ++i) {
    node = qtdemux_tree_get_child_by_type (ilst, add_funcs[i].fourcc);
    if (node) {
      add_funcs[i].func (qtdemux, add_funcs[i].gst_tag,
          add_funcs[i].gst_tag_bis, node);
      g_node_destroy (node);
    }
  }

  /* parsed nodes have been removed, pass along remainder as blob */
  g_node_children_foreach (ilst, G_TRAVERSE_ALL,
      (GNodeForeachFunc) qtdemux_tag_add_blob, qtdemux);

}

typedef struct
{
  GstStructure *structure;      /* helper for sort function */
  gchar *location;
  guint min_req_bitrate;
  guint min_req_qt_version;
} GstQtReference;

static gint
qtdemux_redirects_sort_func (gconstpointer a, gconstpointer b)
{
  GstQtReference *ref_a = (GstQtReference *) a;
  GstQtReference *ref_b = (GstQtReference *) b;

  if (ref_b->min_req_qt_version != ref_a->min_req_qt_version)
    return ref_b->min_req_qt_version - ref_a->min_req_qt_version;

  /* known bitrates go before unknown; higher bitrates go first */
  return ref_b->min_req_bitrate - ref_a->min_req_bitrate;
}

/* sort the redirects and post a message for the application.
 */
static void
qtdemux_process_redirects (GstQTDemux * qtdemux, GList * references)
{
  GstQtReference *best;
  GstStructure *s;
  GstMessage *msg;
  GValue list_val = { 0, };
  GList *l;

  g_assert (references != NULL);

  references = g_list_sort (references, qtdemux_redirects_sort_func);

  best = (GstQtReference *) references->data;

  g_value_init (&list_val, GST_TYPE_LIST);

  for (l = references; l != NULL; l = l->next) {
    GstQtReference *ref = (GstQtReference *) l->data;
    GValue struct_val = { 0, };

    ref->structure = gst_structure_new ("redirect",
        "new-location", G_TYPE_STRING, ref->location, NULL);

    if (ref->min_req_bitrate > 0) {
      gst_structure_set (ref->structure, "minimum-bitrate", G_TYPE_INT,
          ref->min_req_bitrate, NULL);
    }

    g_value_init (&struct_val, GST_TYPE_STRUCTURE);
    g_value_set_boxed (&struct_val, ref->structure);
    gst_value_list_append_value (&list_val, &struct_val);
    g_value_unset (&struct_val);
    /* don't free anything here yet, since we need best->structure below */
  }

  g_assert (best != NULL);
  s = gst_structure_copy (best->structure);

  if (g_list_length (references) > 1) {
    gst_structure_set_value (s, "locations", &list_val);
  }

  g_value_unset (&list_val);

  for (l = references; l != NULL; l = l->next) {
    GstQtReference *ref = (GstQtReference *) l->data;

    gst_structure_free (ref->structure);
    g_free (ref->location);
    g_free (ref);
  }
  g_list_free (references);

  GST_INFO_OBJECT (qtdemux, "posting redirect message: %" GST_PTR_FORMAT, s);
  msg = gst_message_new_element (GST_OBJECT_CAST (qtdemux), s);
  gst_element_post_message (GST_ELEMENT_CAST (qtdemux), msg);
}

/* look for redirect nodes, collect all redirect information and
 * process it.
 */
static gboolean
qtdemux_parse_redirects (GstQTDemux * qtdemux)
{
  GNode *rmra, *rmda, *rdrf;

  rmra = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_rmra);
  if (rmra) {
    GList *redirects = NULL;

    rmda = qtdemux_tree_get_child_by_type (rmra, FOURCC_rmda);
    while (rmda) {
      GstQtReference ref = { NULL, NULL, 0, 0 };
      GNode *rmdr, *rmvc;

      if ((rmdr = qtdemux_tree_get_child_by_type (rmda, FOURCC_rmdr))) {
        ref.min_req_bitrate = QT_UINT32 ((guint8 *) rmdr->data + 12);
        GST_LOG_OBJECT (qtdemux, "data rate atom, required bitrate = %u",
            ref.min_req_bitrate);
      }

      if ((rmvc = qtdemux_tree_get_child_by_type (rmda, FOURCC_rmvc))) {
        guint32 package = QT_FOURCC ((guint8 *) rmvc->data + 12);
        guint version = QT_UINT32 ((guint8 *) rmvc->data + 16);

#ifndef GST_DISABLE_GST_DEBUG
        guint bitmask = QT_UINT32 ((guint8 *) rmvc->data + 20);
#endif
        guint check_type = QT_UINT16 ((guint8 *) rmvc->data + 24);

        GST_LOG_OBJECT (qtdemux,
            "version check atom [%" GST_FOURCC_FORMAT "], version=0x%08x"
            ", mask=%08x, check_type=%u", GST_FOURCC_ARGS (package), version,
            bitmask, check_type);
        if (package == FOURCC_qtim && check_type == 0) {
          ref.min_req_qt_version = version;
        }
      }

      rdrf = qtdemux_tree_get_child_by_type (rmda, FOURCC_rdrf);
      if (rdrf) {
        guint32 ref_type;
        guint8 *ref_data;

        ref_type = QT_FOURCC ((guint8 *) rdrf->data + 12);
        ref_data = (guint8 *) rdrf->data + 20;
        if (ref_type == FOURCC_alis) {
          guint record_len, record_version, fn_len;

          /* MacOSX alias record, google for alias-layout.txt */
          record_len = QT_UINT16 (ref_data + 4);
          record_version = QT_UINT16 (ref_data + 4 + 2);
          fn_len = QT_UINT8 (ref_data + 50);
          if (record_len > 50 && record_version == 2 && fn_len > 0) {
            ref.location = g_strndup ((gchar *) ref_data + 51, fn_len);
          }
        } else if (ref_type == FOURCC_url_) {
          ref.location = g_strdup ((gchar *) ref_data);
        } else {
          GST_DEBUG_OBJECT (qtdemux,
              "unknown rdrf reference type %" GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (ref_type));
        }
        if (ref.location != NULL) {
          GST_INFO_OBJECT (qtdemux, "New location: %s", ref.location);
          redirects = g_list_prepend (redirects, g_memdup (&ref, sizeof (ref)));
        } else {
          GST_WARNING_OBJECT (qtdemux,
              "Failed to extract redirect location from rdrf atom");
        }
      }

      /* look for others */
      rmda = qtdemux_tree_get_sibling_by_type (rmda, FOURCC_rmda);
    }

    if (redirects != NULL) {
      qtdemux_process_redirects (qtdemux, redirects);
    }
  }
  return TRUE;
}

/* we have read th complete moov node now.
 * This function parses all of the relevant info, creates the traks and
 * prepares all data structures for playback
 */
static gboolean
qtdemux_parse_tree (GstQTDemux * qtdemux)
{
  GNode *mvhd;
  GNode *trak;
  GNode *udta;
  gint64 duration;

  mvhd = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_mvhd);
  if (mvhd == NULL) {
    GST_LOG_OBJECT (qtdemux, "No mvhd node found, looking for redirects.");
    return qtdemux_parse_redirects (qtdemux);
  }

  qtdemux->timescale = QT_UINT32 ((guint8 *) mvhd->data + 20);
  qtdemux->duration = QT_UINT32 ((guint8 *) mvhd->data + 24);

  GST_INFO_OBJECT (qtdemux, "timescale: %u", qtdemux->timescale);
  GST_INFO_OBJECT (qtdemux, "duration: %u", qtdemux->duration);

  /* set duration in the segment info */
  gst_qtdemux_get_duration (qtdemux, &duration);
  gst_segment_set_duration (&qtdemux->segment, GST_FORMAT_TIME, duration);

  /* parse all traks */
  trak = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_trak);
  while (trak) {
    qtdemux_parse_trak (qtdemux, trak);
    /* iterate all siblings */
    trak = qtdemux_tree_get_sibling_by_type (trak, FOURCC_trak);
  }
  gst_element_no_more_pads (GST_ELEMENT_CAST (qtdemux));

  /* find and push tags, we do this after adding the pads so we can push the
   * tags downstream as well. */
  udta = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_udta);
  if (udta) {
    qtdemux_parse_udta (qtdemux, udta);

    if (qtdemux->tag_list) {
      GST_DEBUG_OBJECT (qtdemux,
          "calling gst_element_found_tags with %" GST_PTR_FORMAT,
          qtdemux->tag_list);
      gst_element_found_tags (GST_ELEMENT_CAST (qtdemux), qtdemux->tag_list);
      qtdemux->tag_list = NULL;
    }
  } else {
    GST_LOG_OBJECT (qtdemux, "No udta node found.");
  }
  return TRUE;
}

/* taken from ffmpeg */
static unsigned int
get_size (guint8 * ptr, guint8 ** end)
{
  int count = 4;
  int len = 0;

  while (count--) {
    int c = *ptr;

    ptr++;
    len = (len << 7) | (c & 0x7f);
    if (!(c & 0x80))
      break;
  }
  if (end)
    *end = ptr;
  return len;
}

/* this can change the codec originally present in @list */
static void
gst_qtdemux_handle_esds (GstQTDemux * qtdemux, QtDemuxStream * stream,
    GNode * esds, GstTagList * list)
{
  int len = QT_UINT32 (esds->data);
  guint8 *ptr = esds->data;
  guint8 *end = ptr + len;
  int tag;
  guint8 *data_ptr = NULL;
  int data_len = 0;
  guint8 object_type_id = 0;

  qtdemux_dump_mem (ptr, len);
  ptr += 8;
  GST_DEBUG_OBJECT (qtdemux, "version/flags = %08x", QT_UINT32 (ptr));
  ptr += 4;
  while (ptr < end) {
    tag = QT_UINT8 (ptr);
    GST_DEBUG_OBJECT (qtdemux, "tag = %02x", tag);
    ptr++;
    len = get_size (ptr, &ptr);
    GST_DEBUG_OBJECT (qtdemux, "len = %d", len);

    switch (tag) {
      case 0x03:
        GST_DEBUG_OBJECT (qtdemux, "ID %04x", QT_UINT16 (ptr));
        GST_DEBUG_OBJECT (qtdemux, "priority %04x", QT_UINT8 (ptr + 2));
        ptr += 3;
        break;
      case 0x04:
        object_type_id = QT_UINT8 (ptr);
        GST_DEBUG_OBJECT (qtdemux, "object_type_id %02x", object_type_id);
        GST_DEBUG_OBJECT (qtdemux, "stream_type %02x", QT_UINT8 (ptr + 1));
        GST_DEBUG_OBJECT (qtdemux, "buffer_size_db %02x", QT_UINT24 (ptr + 2));
        GST_DEBUG_OBJECT (qtdemux, "max bitrate %d", QT_UINT32 (ptr + 5));
        GST_DEBUG_OBJECT (qtdemux, "avg bitrate %d", QT_UINT32 (ptr + 9));
        ptr += 13;
        break;
      case 0x05:
        GST_DEBUG_OBJECT (qtdemux, "data:");
        qtdemux_dump_mem (ptr, len);
        data_ptr = ptr;
        data_len = len;
        ptr += len;
        break;
      case 0x06:
        GST_DEBUG_OBJECT (qtdemux, "data %02x", QT_UINT8 (ptr));
        ptr += 1;
        break;
      default:
        GST_ERROR_OBJECT (qtdemux, "parse error");
    }
  }

  if (data_ptr) {
    GstBuffer *buffer;

    buffer = gst_buffer_new_and_alloc (data_len);
    memcpy (GST_BUFFER_DATA (buffer), data_ptr, data_len);
    qtdemux_dump_mem (GST_BUFFER_DATA (buffer), data_len);

    GST_DEBUG_OBJECT (qtdemux, "setting codec_data from esds");

    gst_caps_set_simple (stream->caps, "codec_data", GST_TYPE_BUFFER,
        buffer, NULL);
    gst_buffer_unref (buffer);
  }
  /* object_type_id in the stsd atom in mp4a tells us about AAC or plain
   * MPEG audio and other formats */
  switch (object_type_id) {
    case 107:
      /* change to mpeg1 layer 3 audio */
      gst_caps_set_simple (stream->caps, "layer", G_TYPE_INT, 3,
          "mpegversion", G_TYPE_INT, 1, NULL);
      if (list)
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_AUDIO_CODEC, "MPEG-1 layer 3", NULL);
      break;
    case 0xE1:
    {
      GstStructure *structure;

      /* QCELP, the codec_data is a riff tag (little endian) with
       * more info (http://ftp.3gpp2.org/TSGC/Working/2003/2003-05-SanDiego/TSG-C-2003-05-San%20Diego/WG1/SWG12/C12-20030512-006%20=%20C12-20030217-015_Draft_Baseline%20Text%20of%20FFMS_R2.doc). */
      structure = gst_caps_get_structure (stream->caps, 0);
      gst_structure_set_name (structure, "audio/qcelp");
      gst_structure_remove_fields (structure, "mpegversion", "framed", NULL);

      if (list)
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_AUDIO_CODEC, "QCELP", NULL);
      break;
    }
    default:
      break;
  }
}

#define _codec(name) \
  do { \
    if (codec_name) { \
      *codec_name = g_strdup (name); \
    } \
  } while (0)

static GstCaps *
qtdemux_video_caps (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint32 fourcc, const guint8 * stsd_data, gchar ** codec_name)
{
  GstCaps *caps;
  const GstStructure *s;
  const gchar *name;

  switch (fourcc) {
    case GST_MAKE_FOURCC ('p', 'n', 'g', ' '):
      _codec ("PNG still images");
      caps = gst_caps_new_simple ("image/png", NULL);
      break;
    case GST_MAKE_FOURCC ('j', 'p', 'e', 'g'):
      _codec ("JPEG still images");
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'j', 'p', 'a'):
    case GST_MAKE_FOURCC ('A', 'V', 'D', 'J'):
    case GST_MAKE_FOURCC ('M', 'J', 'P', 'G'):
      _codec ("Motion-JPEG");
      caps = gst_caps_new_simple ("image/jpeg", NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'j', 'p', 'b'):
      _codec ("Motion-JPEG format B");
      caps = gst_caps_new_simple ("video/x-mjpeg-b", NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'j', 'p', '2'):
      _codec ("JPEG-2000");
      /* override to what it should be according to spec, avoid palette_data */
      stream->bits_per_sample = 24;
      caps = gst_caps_new_simple ("image/x-j2c", "fields", G_TYPE_INT, 1, NULL);
      break;
    case GST_MAKE_FOURCC ('S', 'V', 'Q', '3'):
      _codec ("Sorensen video v.3");
      caps = gst_caps_new_simple ("video/x-svq",
          "svqversion", G_TYPE_INT, 3, NULL);
      break;
    case GST_MAKE_FOURCC ('s', 'v', 'q', 'i'):
    case GST_MAKE_FOURCC ('S', 'V', 'Q', '1'):
      _codec ("Sorensen video v.1");
      caps = gst_caps_new_simple ("video/x-svq",
          "svqversion", G_TYPE_INT, 1, NULL);
      break;
    case GST_MAKE_FOURCC ('r', 'a', 'w', ' '):
    {
      guint16 bps;

      _codec ("Raw RGB video");
      bps = QT_UINT16 (stsd_data + 98);
      /* set common stuff */
      caps = gst_caps_new_simple ("video/x-raw-rgb",
          "endianness", G_TYPE_INT, G_BYTE_ORDER, "depth", G_TYPE_INT, bps,
          NULL);

      switch (bps) {
        case 15:
          gst_caps_set_simple (caps,
              "bpp", G_TYPE_INT, 16,
              "endianness", G_TYPE_INT, G_BIG_ENDIAN,
              "red_mask", G_TYPE_INT, 0x7c00,
              "green_mask", G_TYPE_INT, 0x03e0,
              "blue_mask", G_TYPE_INT, 0x001f, NULL);
          break;
        case 16:
          gst_caps_set_simple (caps,
              "bpp", G_TYPE_INT, 16,
              "endianness", G_TYPE_INT, G_BIG_ENDIAN,
              "red_mask", G_TYPE_INT, 0xf800,
              "green_mask", G_TYPE_INT, 0x07e0,
              "blue_mask", G_TYPE_INT, 0x001f, NULL);
          break;
        case 24:
          gst_caps_set_simple (caps,
              "bpp", G_TYPE_INT, 24,
              "endianness", G_TYPE_INT, G_BIG_ENDIAN,
              "red_mask", G_TYPE_INT, 0xff0000,
              "green_mask", G_TYPE_INT, 0x00ff00,
              "blue_mask", G_TYPE_INT, 0x0000ff, NULL);
          break;
        case 32:
          gst_caps_set_simple (caps,
              "bpp", G_TYPE_INT, 32,
              "endianness", G_TYPE_INT, G_BIG_ENDIAN,
              "alpha_mask", G_TYPE_INT, 0xff000000,
              "red_mask", G_TYPE_INT, 0x00ff0000,
              "green_mask", G_TYPE_INT, 0x0000ff00,
              "blue_mask", G_TYPE_INT, 0x000000ff, NULL);
          break;
        default:
          /* unknown */
          break;
      }
      break;
    }
    case GST_MAKE_FOURCC ('y', 'v', '1', '2'):
      _codec ("Raw planar YUV 4:2:0");
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
          NULL);
      break;
    case GST_MAKE_FOURCC ('y', 'u', 'v', '2'):
    case GST_MAKE_FOURCC ('Y', 'u', 'v', '2'):
      _codec ("Raw packed YUV 4:2:2");
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
          NULL);
      break;
    case GST_MAKE_FOURCC ('2', 'v', 'u', 'y'):
      _codec ("Raw packed YUV 4:2:0");
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'),
          NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'p', 'e', 'g'):
    case GST_MAKE_FOURCC ('m', 'p', 'g', '1'):
      _codec ("MPEG-1 video");
      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 1,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('h', 'd', 'v', '1'): // HDV 720p30
    case GST_MAKE_FOURCC ('h', 'd', 'v', '2'): // HDV 1080i60
    case GST_MAKE_FOURCC ('h', 'd', 'v', '3'): // HDV 1080i50
    case GST_MAKE_FOURCC ('h', 'd', 'v', '5'): // HDV 720p25
    case GST_MAKE_FOURCC ('h', 'd', 'v', '6'): // HDV 1080i60
    case GST_MAKE_FOURCC ('m', 'x', '5', 'n'): // MPEG2 IMX NTSC 525/60 50mb/s produced by FCP
    case GST_MAKE_FOURCC ('m', 'x', '5', 'p'): // MPEG2 IMX PAL 625/60 50mb/s produced by FCP
    case GST_MAKE_FOURCC ('m', 'x', '4', 'n'): // MPEG2 IMX NTSC 525/60 40mb/s produced by FCP
    case GST_MAKE_FOURCC ('m', 'x', '4', 'p'): // MPEG2 IMX PAL 625/60 40mb/s produced by FCP
    case GST_MAKE_FOURCC ('m', 'x', '3', 'n'): // MPEG2 IMX NTSC 525/60 30mb/s produced by FCP
    case GST_MAKE_FOURCC ('m', 'x', '3', 'p'): // MPEG2 IMX PAL 625/50 30mb/s produced by FCP
    case GST_MAKE_FOURCC ('x', 'd', 'v', '2'): // XDCAM HD 1080i60
    case GST_MAKE_FOURCC ('A', 'V', 'm', 'p'): // AVID IMX PAL
    case GST_MAKE_FOURCC ('m', 'p', 'g', '2'): // AVID IMX PAL
      _codec ("MPEG-2 video");
      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('g', 'i', 'f', ' '):
      _codec ("GIF still images");
      caps = gst_caps_new_simple ("image/gif", NULL);
      break;
    case GST_MAKE_FOURCC ('h', '2', '6', '3'):
    case GST_MAKE_FOURCC ('H', '2', '6', '3'):
    case GST_MAKE_FOURCC ('s', '2', '6', '3'):
    case GST_MAKE_FOURCC ('U', '2', '6', '3'):
      _codec ("H.263");
      /* ffmpeg uses the height/width props, don't know why */
      caps = gst_caps_new_simple ("video/x-h263", NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
      _codec ("MPEG-4 video");
      caps = gst_caps_new_simple ("video/mpeg", "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('3', 'i', 'v', 'd'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', 'D'):
      _codec ("Microsoft MPEG-4 4.3");  /* FIXME? */
      caps = gst_caps_new_simple ("video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, 43, NULL);
      break;
    case GST_MAKE_FOURCC ('3', 'I', 'V', '1'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', '2'):
      _codec ("3ivX video");
      caps = gst_caps_new_simple ("video/x-3ivx", NULL);
      break;
    case GST_MAKE_FOURCC ('D', 'I', 'V', '3'):
      _codec ("DivX 3");
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 3, NULL);
      break;
    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
    case GST_MAKE_FOURCC ('d', 'i', 'v', 'x'):
      _codec ("DivX 4");
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 4, NULL);
      break;
    case GST_MAKE_FOURCC ('D', 'X', '5', '0'):
      _codec ("DivX 5");
      caps = gst_caps_new_simple ("video/x-divx",
          "divxversion", G_TYPE_INT, 5, NULL);
      break;
    case GST_MAKE_FOURCC ('X', 'V', 'I', 'D'):
    case GST_MAKE_FOURCC ('x', 'v', 'i', 'd'):
      _codec ("XVID MPEG-4");
      caps = gst_caps_new_simple ("video/x-xvid", NULL);
      break;

    case GST_MAKE_FOURCC ('F', 'M', 'P', '4'):
    case GST_MAKE_FOURCC ('U', 'M', 'P', '4'):
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4, NULL);
      if (codec_name)
        *codec_name = g_strdup ("FFmpeg MPEG-4");
      break;

    case GST_MAKE_FOURCC ('c', 'v', 'i', 'd'):
      _codec ("Cinepak");
      caps = gst_caps_new_simple ("video/x-cinepak", NULL);
      break;
    case GST_MAKE_FOURCC ('q', 'd', 'r', 'w'):
      _codec ("Apple QuickDraw");
      caps = gst_caps_new_simple ("video/x-qdrw", NULL);
      break;
    case GST_MAKE_FOURCC ('r', 'p', 'z', 'a'):
      _codec ("Apple video");
      caps = gst_caps_new_simple ("video/x-apple-video", NULL);
      break;
    case GST_MAKE_FOURCC ('a', 'v', 'c', '1'):
      _codec ("H.264 / AVC");
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      break;
    case GST_MAKE_FOURCC ('r', 'l', 'e', ' '):
      _codec ("Run-length encoding");
      caps = gst_caps_new_simple ("video/x-rle",
          "layout", G_TYPE_STRING, "quicktime", NULL);
      break;
    case GST_MAKE_FOURCC ('i', 'v', '3', '2'):
      _codec ("Indeo Video 3");
      caps = gst_caps_new_simple ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 3, NULL);
      break;
    case GST_MAKE_FOURCC ('I', 'V', '4', '1'):
    case GST_MAKE_FOURCC ('i', 'v', '4', '1'):
      _codec ("Intel Video 4");
      caps = gst_caps_new_simple ("video/x-indeo",
          "indeoversion", G_TYPE_INT, 4, NULL);
      break;
    case GST_MAKE_FOURCC ('d', 'v', 'c', 'p'):
    case GST_MAKE_FOURCC ('d', 'v', 'c', ' '):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
    case GST_MAKE_FOURCC ('D', 'V', 'S', 'D'):
    case GST_MAKE_FOURCC ('d', 'v', 'c', 's'):
    case GST_MAKE_FOURCC ('D', 'V', 'C', 'S'):
    case GST_MAKE_FOURCC ('d', 'v', '2', '5'):
    case GST_MAKE_FOURCC ('d', 'v', 'p', 'p'):
      _codec ("DV Video");
      caps = gst_caps_new_simple ("video/x-dv", "dvversion", G_TYPE_INT, 25,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('d', 'v', '5', 'n'): //DVCPRO50 NTSC
    case GST_MAKE_FOURCC ('d', 'v', '5', 'p'): //DVCPRO50 PAL
      _codec ("DVCPro50 Video");
      caps = gst_caps_new_simple ("video/x-dv", "dvversion", G_TYPE_INT, 50,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('d', 'v', 'h', '5'): //DVCPRO HD 50i produced by FCP
    case GST_MAKE_FOURCC ('d', 'v', 'h', '6'): //DVCPRO HD 60i produced by FCP
      _codec ("DVCProHD Video");
      caps = gst_caps_new_simple ("video/x-dv", "dvversion", G_TYPE_INT, 100,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('s', 'm', 'c', ' '):
      _codec ("Apple Graphics (SMC)");
      caps = gst_caps_new_simple ("video/x-smc", NULL);
      break;
    case GST_MAKE_FOURCC ('V', 'P', '3', '1'):
      _codec ("VP3");
      caps = gst_caps_new_simple ("video/x-vp3", NULL);
      break;
    case GST_MAKE_FOURCC ('X', 'i', 'T', 'h'):
      _codec ("Theora");
      caps = gst_caps_new_simple ("video/x-theora", NULL);
      /* theora uses one byte of padding in the data stream because it does not
       * allow 0 sized packets while theora does */
      stream->padding = 1;
      break;
    case GST_MAKE_FOURCC ('d', 'r', 'a', 'c'):
      _codec ("Dirac");
      caps = gst_caps_new_simple ("video/x-dirac", NULL);
      break;
    case GST_MAKE_FOURCC ('t', 'i', 'f', 'f'):
      _codec ("TIFF still images");
      caps = gst_caps_new_simple ("image/tiff", NULL);
      break;
    case GST_MAKE_FOURCC ('k', 'p', 'c', 'd'):
    default:
    {
      char *s;

      s = g_strdup_printf ("video/x-gst-fourcc-%" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (fourcc));
      caps = gst_caps_new_simple (s, NULL);
      break;
    }
  }

  /* enable clipping for raw video streams */
  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);
  if (g_str_has_prefix (name, "video/x-raw-")) {
    stream->need_clip = TRUE;
  }
  return caps;
}

static GstCaps *
qtdemux_audio_caps (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint32 fourcc, const guint8 * data, int len, gchar ** codec_name)
{
  GstCaps *caps;
  const GstStructure *s;
  const gchar *name;
  gint endian = 0;

  GST_DEBUG_OBJECT (qtdemux, "resolve fourcc %08x", fourcc);

  switch (fourcc) {
    case GST_MAKE_FOURCC ('N', 'O', 'N', 'E'):
    case GST_MAKE_FOURCC ('r', 'a', 'w', ' '):
      _codec ("Raw 8-bit PCM audio");
      caps = gst_caps_new_simple ("audio/x-raw-int", "width", G_TYPE_INT, 8,
          "depth", G_TYPE_INT, 8, "signed", G_TYPE_BOOLEAN, FALSE, NULL);
      break;
    case GST_MAKE_FOURCC ('t', 'w', 'o', 's'):
      endian = G_BIG_ENDIAN;
      /* fall-through */
    case GST_MAKE_FOURCC ('s', 'o', 'w', 't'):
    {
      gchar *str;
      gint depth;

      if (!endian)
        endian = G_LITTLE_ENDIAN;

      depth = stream->bytes_per_packet * 8;
      str = g_strdup_printf ("Raw %d-bit PCM audio", depth);
      _codec (str);
      g_free (str);
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "width", G_TYPE_INT, depth, "depth", G_TYPE_INT, depth,
          "endianness", G_TYPE_INT, endian,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    }
    case GST_MAKE_FOURCC ('f', 'l', '6', '4'):
      _codec ("Raw 64-bit floating-point audio");
      caps = gst_caps_new_simple ("audio/x-raw-float", "width", G_TYPE_INT, 64,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN, NULL);
      break;
    case GST_MAKE_FOURCC ('f', 'l', '3', '2'):
      _codec ("Raw 32-bit floating-point audio");
      caps = gst_caps_new_simple ("audio/x-raw-float", "width", G_TYPE_INT, 32,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN, NULL);
      break;
    case GST_MAKE_FOURCC ('i', 'n', '2', '4'):
      _codec ("Raw 24-bit PCM audio");
      caps = gst_caps_new_simple ("audio/x-raw-int", "width", G_TYPE_INT, 24,
          "depth", G_TYPE_INT, 24,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case GST_MAKE_FOURCC ('i', 'n', '3', '2'):
      _codec ("Raw 32-bit PCM audio");
      caps = gst_caps_new_simple ("audio/x-raw-int", "width", G_TYPE_INT, 32,
          "depth", G_TYPE_INT, 32,
          "endianness", G_TYPE_INT, G_BIG_ENDIAN,
          "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case GST_MAKE_FOURCC ('u', 'l', 'a', 'w'):
      _codec ("Mu-law audio");
      caps = gst_caps_new_simple ("audio/x-mulaw", NULL);
      break;
    case GST_MAKE_FOURCC ('a', 'l', 'a', 'w'):
      _codec ("A-law audio");
      caps = gst_caps_new_simple ("audio/x-alaw", NULL);
      break;
    case 0x0200736d:
    case 0x6d730002:
      _codec ("Microsoft ADPCM");
      /* Microsoft ADPCM-ACM code 2 */
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      break;
    case 0x1100736d:
    case 0x6d730011:
      _codec ("IMA Loki SDL MJPEG ADPCM");
      /* Loki ADPCM, See #550288 for a file that only decodes
       * with the smjpeg variant of the ADPCM decoder. */
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "smjpeg", NULL);
      break;
    case 0x1700736d:
    case 0x6d730017:
      _codec ("DVI/Intel IMA ADPCM");
      /* FIXME DVI/Intel IMA ADPCM/ACM code 17 */
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "quicktime", NULL);
      break;
    case 0x5500736d:
    case 0x6d730055:
      /* MPEG layer 3, CBR only (pre QT4.1) */
    case GST_MAKE_FOURCC ('.', 'm', 'p', '3'):
      _codec ("MPEG-1 layer 3");
      /* MPEG layer 3, CBR & VBR (QT4.1 and later) */
      caps = gst_caps_new_simple ("audio/mpeg", "layer", G_TYPE_INT, 3,
          "mpegversion", G_TYPE_INT, 1, NULL);
      break;
    case 0x20736d:
      _codec ("AC-3 audio");
      caps = gst_caps_new_simple ("audio/x-ac3", NULL);
      break;
    case GST_MAKE_FOURCC ('M', 'A', 'C', '3'):
      _codec ("MACE-3");
      caps = gst_caps_new_simple ("audio/x-mace",
          "maceversion", G_TYPE_INT, 3, NULL);
      break;
    case GST_MAKE_FOURCC ('M', 'A', 'C', '6'):
      _codec ("MACE-6");
      caps = gst_caps_new_simple ("audio/x-mace",
          "maceversion", G_TYPE_INT, 6, NULL);
      break;
    case GST_MAKE_FOURCC ('O', 'g', 'g', 'V'):
      /* ogg/vorbis */
      caps = gst_caps_new_simple ("application/ogg", NULL);
      break;
    case GST_MAKE_FOURCC ('d', 'v', 'c', 'a'):
      _codec ("DV audio");
      caps = gst_caps_new_simple ("audio/x-dv", NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'p', '4', 'a'):
      _codec ("MPEG-4 AAC audio");
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, "framed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;
    case GST_MAKE_FOURCC ('Q', 'D', 'M', 'C'):
      _codec ("QDesign Music");
      caps = gst_caps_new_simple ("audio/x-qdm", NULL);
      break;
    case GST_MAKE_FOURCC ('Q', 'D', 'M', '2'):
      _codec ("QDesign Music v.2");
      /* FIXME: QDesign music version 2 (no constant) */
      if (data) {
        caps = gst_caps_new_simple ("audio/x-qdm2",
            "framesize", G_TYPE_INT, QT_UINT32 (data + 52),
            "bitrate", G_TYPE_INT, QT_UINT32 (data + 40),
            "blocksize", G_TYPE_INT, QT_UINT32 (data + 44), NULL);
      } else {
        caps = gst_caps_new_simple ("audio/x-qdm2", NULL);
      }
      break;
    case GST_MAKE_FOURCC ('a', 'g', 's', 'm'):
      _codec ("GSM audio");
      caps = gst_caps_new_simple ("audio/x-gsm", NULL);
      break;
    case GST_MAKE_FOURCC ('s', 'a', 'm', 'r'):
      _codec ("AMR audio");
      caps = gst_caps_new_simple ("audio/AMR", NULL);
      break;
    case GST_MAKE_FOURCC ('s', 'a', 'w', 'b'):
      _codec ("AMR-WB audio");
      caps = gst_caps_new_simple ("audio/AMR-WB", NULL);
      break;
    case GST_MAKE_FOURCC ('i', 'm', 'a', '4'):
      _codec ("Quicktime IMA ADPCM");
      caps = gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "quicktime", NULL);
      break;
    case GST_MAKE_FOURCC ('a', 'l', 'a', 'c'):
      _codec ("Apple lossless audio");
      caps = gst_caps_new_simple ("audio/x-alac", NULL);
      break;
    case GST_MAKE_FOURCC ('q', 't', 'v', 'r'):
      /* ? */
    case GST_MAKE_FOURCC ('Q', 'c', 'l', 'p'):
      /* QUALCOMM PureVoice */
    default:
    {
      char *s;

      s = g_strdup_printf ("audio/x-gst-fourcc-%" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (fourcc));
      caps = gst_caps_new_simple (s, NULL);
      break;
    }
  }

  /* enable clipping for raw audio streams */
  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);
  if (g_str_has_prefix (name, "audio/x-raw-")) {
    stream->need_clip = TRUE;
  }
  return caps;
}
