/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2004> Stephane Loeuillet <gstreamer@leroutier.net>
 * Copyright (C) <2005> Owen Fraser-Green <owen@discobabe.net>
 * Copyright (C) <2005> Michael Smith <fluendo.com>
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
#  include "config.h"
#endif
#include "rmdemux.h"
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#define RMDEMUX_GUINT32_GET(a)  GST_READ_UINT32_BE(a)
#define RMDEMUX_GUINT16_GET(a)  GST_READ_UINT16_BE(a)
#define RMDEMUX_FOURCC_GET(a)   GST_READ_UINT32_LE(a)
#define HEADER_SIZE 10
#define DATA_SIZE 8

typedef struct _GstRMDemuxIndex GstRMDemuxIndex;

struct _GstRMDemuxStream
{
  guint32 subtype;
  guint32 fourcc;
  guint32 subformat;
  guint32 format;

  int id;
  GstCaps *caps;
  GstPad *pad;
  int timescale;

  int sample_index;
  GstRMDemuxIndex *index;
  int index_length;
  gint framerate_numerator;
  gint framerate_denominator;
  guint32 seek_offset;

  guint16 width;
  guint16 height;
  guint16 flavor;
  guint16 rate;                 // samplerate
  guint16 n_channels;           // channels
  guint16 sample_width;         // bits_per_sample
  guint16 leaf_size;            // subpacket_size
  guint32 packet_size;          // coded_frame_size
  guint16 version;
  guint32 extra_data_size;      // codec_data_length
  guint8 *extra_data;           // extras
};

struct _GstRMDemuxIndex
{
  guint32 offset;
  GstClockTime timestamp;
};

static GstElementDetails gst_rmdemux_details = {
  "RealMedia Demuxer",
  "Codec/Demuxer",
  "Demultiplex a RealMedia file into audio and video streams",
  "David Schleef <ds@schleef.org>"
};

enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rmdemux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.rn-realmedia")
    );

static GstStaticPadTemplate gst_rmdemux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rmdemux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (rmdemux_debug);
#define GST_CAT_DEFAULT rmdemux_debug

static GstElementClass *parent_class = NULL;

static void gst_rmdemux_class_init (GstRMDemuxClass * klass);
static void gst_rmdemux_base_init (GstRMDemuxClass * klass);
static void gst_rmdemux_init (GstRMDemux * rmdemux);
static void gst_rmdemux_dispose (GObject * object);
static GstStateChangeReturn gst_rmdemux_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_rmdemux_chain (GstPad * pad, GstBuffer * buffer);
static void gst_rmdemux_loop (GstPad * pad);
static gboolean gst_rmdemux_sink_activate (GstPad * sinkpad);
static gboolean gst_rmdemux_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_rmdemux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_rmdemux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_rmdemux_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_rmdemux_send_event (GstRMDemux * rmdemux, GstEvent * event);
static const GstQueryType *gst_rmdemux_src_query_types (GstPad * pad);
static gboolean gst_rmdemux_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_rmdemux_perform_seek (GstRMDemux * rmdemux, gboolean flush);

static void gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_prop (GstRMDemux * rmdemux, const void *data,
    int length);
static GstFlowReturn gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux,
    const void *data, int length);
static guint gst_rmdemux_parse_indx (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_data (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_cont (GstRMDemux * rmdemux, const void *data,
    int length);
static GstFlowReturn gst_rmdemux_parse_packet (GstRMDemux * rmdemux,
    const void *data, guint16 version, guint16 length);
static void gst_rmdemux_parse_indx_data (GstRMDemux * rmdemux, const void *data,
    int length);

static GstCaps *gst_rmdemux_src_getcaps (GstPad * pad);

static GstRMDemuxStream *gst_rmdemux_get_stream_by_id (GstRMDemux * rmdemux,
    int id);

static GType
gst_rmdemux_get_type (void)
{
  static GType rmdemux_type = 0;

  if (!rmdemux_type) {
    static const GTypeInfo rmdemux_info = {
      sizeof (GstRMDemuxClass),
      (GBaseInitFunc) gst_rmdemux_base_init, NULL,
      (GClassInitFunc) gst_rmdemux_class_init,
      NULL, NULL, sizeof (GstRMDemux), 0,
      (GInstanceInitFunc) gst_rmdemux_init,
    };

    rmdemux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRMDemux", &rmdemux_info,
        0);
  }
  return rmdemux_type;
}

static void
gst_rmdemux_base_init (GstRMDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_videosrc_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_audiosrc_template));
  gst_element_class_set_details (element_class, &gst_rmdemux_details);
}

static void
gst_rmdemux_class_init (GstRMDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_rmdemux_change_state;

  GST_DEBUG_CATEGORY_INIT (rmdemux_debug, "rmdemux",
      0, "Demuxer for Realmedia streams");

  gobject_class->dispose = gst_rmdemux_dispose;
}

static void
gst_rmdemux_dispose (GObject * object)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (object);

  if (rmdemux->adapter) {
    g_object_unref (rmdemux->adapter);
    rmdemux->adapter = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_rmdemux_init (GstRMDemux * rmdemux)
{
  rmdemux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rmdemux_sink_template), "sink");
  gst_pad_set_event_function (rmdemux->sinkpad, gst_rmdemux_sink_event);
  gst_pad_set_chain_function (rmdemux->sinkpad, gst_rmdemux_chain);
  gst_pad_set_activate_function (rmdemux->sinkpad, gst_rmdemux_sink_activate);
  gst_pad_set_activatepull_function (rmdemux->sinkpad,
      gst_rmdemux_sink_activate_pull);
  gst_pad_set_activatepush_function (rmdemux->sinkpad,
      gst_rmdemux_sink_activate_push);


  gst_element_add_pad (GST_ELEMENT (rmdemux), rmdemux->sinkpad);

  rmdemux->adapter = gst_adapter_new ();
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rmdemux",
      GST_RANK_PRIMARY, GST_TYPE_RMDEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rmdemux",
    "Realmedia stream demuxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)


     static gboolean gst_rmdemux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      GST_LOG_OBJECT (rmdemux, "Event on sink: NEWSEGMENT");
      gst_event_unref (event);
      break;
    default:
      GST_LOG_OBJECT (rmdemux, "Event on sink: type=%d",
          GST_EVENT_TYPE (event));
      ret = gst_pad_event_default (pad, event);
      break;
  }

  return ret;
}

static gboolean
gst_rmdemux_src_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (rmdemux, "handling src event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gboolean running;
      gboolean flush;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;

      GST_LOG_OBJECT (rmdemux, "Event on src: SEEK");
      /* can't seek if we are not seekable, FIXME could pass the
       * seek query upstream after converting it to bytes using
       * the average bitrate of the stream. */
      if (!rmdemux->seekable) {
        ret = FALSE;
        GST_DEBUG ("seek on non seekable stream");
        goto done_unref;
      }
      gst_event_parse_seek (event, NULL, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      /* we can only seek on time */
      if (format != GST_FORMAT_TIME) {
        ret = FALSE;
        GST_DEBUG ("can only seek on TIME");
        goto done_unref;
      }

      GST_OBJECT_LOCK (rmdemux);

      if (cur_type == GST_SEEK_TYPE_SET)
        rmdemux->segment_start = cur;
      else if (cur_type == GST_SEEK_TYPE_CUR)
        rmdemux->segment_start += cur;
      else if (cur_type == GST_SEEK_TYPE_END)
        rmdemux->segment_start = rmdemux->duration + cur;

      if (stop_type == GST_SEEK_TYPE_SET)
        rmdemux->segment_stop = stop;
      else if (cur_type == GST_SEEK_TYPE_CUR)
        rmdemux->segment_stop += cur;
      else if (cur_type == GST_SEEK_TYPE_END)
        rmdemux->segment_stop = rmdemux->duration + cur;

      /* Now do a sanity-check */
      if (rmdemux->segment_stop < rmdemux->segment_start) {
        GST_OBJECT_UNLOCK (rmdemux);
        ret = FALSE;
        GST_DEBUG_OBJECT (rmdemux, "Seek had stop " G_GUINT64_FORMAT " < start "
            G_GUINT64_FORMAT ", cannot perform seek",
            rmdemux->segment_stop, rmdemux->segment_start);
        goto done_unref;
      }

      rmdemux->segment_play = !!(flags & GST_SEEK_FLAG_SEGMENT);
      flush = !!(flags & GST_SEEK_FLAG_FLUSH);
      gst_event_unref (event);

      GST_DEBUG ("segment positions set to %" GST_TIME_FORMAT "-%"
          GST_TIME_FORMAT, GST_TIME_ARGS (rmdemux->segment_start),
          GST_TIME_ARGS (rmdemux->segment_stop));

      /* check if we can do the seek now */
      running = rmdemux->running;
      GST_OBJECT_UNLOCK (rmdemux);

      /* now do the seek */
      if (running) {
        ret = gst_rmdemux_perform_seek (rmdemux, flush);
      } else
        ret = TRUE;
      break;
    }
    default:
      GST_LOG_OBJECT (rmdemux, "Event on src: type=%d", GST_EVENT_TYPE (event));
      ret = gst_pad_event_default (pad, event);
      break;
  }

  return ret;

done_unref:
  GST_DEBUG ("error handling event");
  gst_event_unref (event);
  return ret;
}

/* Validate that this looks like a reasonable point to seek to */
static gboolean
gst_rmdemux_validate_offset (GstRMDemux * rmdemux)
{
  GstBuffer *buffer;
  GstFlowReturn flowret;
  guint16 version, length;
  gboolean ret = TRUE;

  flowret = gst_pad_pull_range (rmdemux->sinkpad, rmdemux->offset, 4, &buffer);

  if (flowret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (rmdemux, "Failed to pull data at offset %d",
        rmdemux->offset);
    return FALSE;
  }
  // TODO: Can we also be seeking to a 'DATA' chunk header? Check this.
  // Also, for the case we currently handle, can we check any more? It's pretty
  // sucky to not be validating a little more heavily than this...
  /* This should now be the start of a data packet header. That begins with
   * a 2-byte 'version' field, which has to be 0 or 1, then a length. I'm not
   * certain what values are valid for length, but it must always be at least
   * 4 bytes, and we can check that it won't take us past our known total size
   */

  version = RMDEMUX_GUINT16_GET (GST_BUFFER_DATA (buffer));
  if (version != 0 && version != 1) {
    GST_DEBUG_OBJECT (rmdemux, "Expected version 0 or 1, got %d",
        (int) version);
    ret = FALSE;
  }

  length = RMDEMUX_GUINT16_GET (GST_BUFFER_DATA (buffer) + 2);
  // TODO: Also check against total stream length
  if (length < 4) {
    GST_DEBUG_OBJECT (rmdemux, "Expected length >= 4, got %d", (int) length);
    ret = FALSE;
  }

  if (ret) {
    rmdemux->offset += 4;
    gst_adapter_clear (rmdemux->adapter);
    gst_adapter_push (rmdemux->adapter, buffer);
  } else {
    GST_WARNING_OBJECT (rmdemux, "Failed to validate seek offset at %d",
        rmdemux->offset);
  }

  return ret;
}

static gboolean
find_seek_offset_bytes (GstRMDemux * rmdemux, guint target)
{
  int i, n;
  gboolean ret = FALSE;

  if (target < 0)
    return FALSE;

  for (n = 0; n < rmdemux->n_streams; n++) {
    GstRMDemuxStream *stream;

    stream = rmdemux->streams[n];

    /* Search backwards through this stream's index until we find the first
     * timestamp before our target time */
    for (i = stream->index_length - 1; i >= 0; i--) {
      if (stream->index[i].offset <= target) {
        /* Set the seek_offset for the stream so we don't bother parsing it
         * until we've passed that point */
        stream->seek_offset = stream->index[i].offset;
        rmdemux->offset = stream->index[i].offset;
        ret = TRUE;
        break;
      }
    }
  }
  return ret;
}

static gboolean
find_seek_offset_time (GstRMDemux * rmdemux, GstClockTime time)
{
  int i, n;
  gboolean ret = FALSE;
  GstClockTime earliest = GST_CLOCK_TIME_NONE;

  for (n = 0; n < rmdemux->n_streams; n++) {
    GstRMDemuxStream *stream;

    stream = rmdemux->streams[n];

    /* Search backwards through this stream's index until we find the first
     * timestamp before our target time */
    for (i = stream->index_length - 1; i >= 0; i--) {
      if (stream->index[i].timestamp <= time) {
        /* Set the seek_offset for the stream so we don't bother parsing it
         * until we've passed that point */
        stream->seek_offset = stream->index[i].offset;

        /* If it's also the earliest timestamp we've seen of all streams, then
         * that's our target!
         */
        if (earliest == GST_CLOCK_TIME_NONE ||
            stream->index[i].timestamp < earliest) {
          earliest = stream->index[i].timestamp;
          rmdemux->offset = stream->index[i].offset;
          GST_DEBUG_OBJECT (rmdemux,
              "We're looking for %" GST_TIME_FORMAT
              " and we found that stream %d has the latest index at %"
              GST_TIME_FORMAT, GST_TIME_ARGS (rmdemux->segment_start), n,
              GST_TIME_ARGS (earliest));
        }

        ret = TRUE;

        break;
      }
    }
  }
  return ret;
}

static gboolean
gst_rmdemux_perform_seek (GstRMDemux * rmdemux, gboolean flush)
{
  gboolean validated;
  gboolean ret = TRUE;

  /* nothing configured, play complete file */
  if (rmdemux->segment_start == GST_CLOCK_TIME_NONE)
    rmdemux->segment_start = 0;
  if (rmdemux->segment_stop == GST_CLOCK_TIME_NONE)
    rmdemux->segment_stop = rmdemux->duration;

  rmdemux->segment_start = CLAMP (rmdemux->segment_start, 0, rmdemux->duration);
  rmdemux->segment_stop = CLAMP (rmdemux->segment_stop, 0, rmdemux->duration);

  /* first step is to unlock the streaming thread if it is
   * blocked in a chain call, we do this by starting the flush. */
  if (flush) {
    gboolean res;

    res = gst_pad_push_event (rmdemux->sinkpad, gst_event_new_flush_start ());
    if (!res) {
      GST_WARNING_OBJECT (rmdemux, "Failed to push event upstream!");
    }

    res = gst_rmdemux_send_event (rmdemux, gst_event_new_flush_start ());
    if (!res) {
      GST_WARNING_OBJECT (rmdemux, "Failed to push event downstream!");
    }
  } else {
    gst_pad_pause_task (rmdemux->sinkpad);
  }

  GST_LOG_OBJECT (rmdemux, "Done starting flushes");

  /* now grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever. */
  GST_PAD_STREAM_LOCK (rmdemux->sinkpad);

  GST_LOG_OBJECT (rmdemux, "Took streamlock");

  /* we need to stop flushing on the sinkpad as we're going to use it
   * next. We can do this as we have the STREAM lock now. */
  gst_pad_push_event (rmdemux->sinkpad, gst_event_new_flush_stop ());

  GST_LOG_OBJECT (rmdemux, "Pushed FLUSH_STOP event");

  /* For each stream, find the first index offset equal to or before our seek 
   * target. Of these, find the smallest offset. That's where we seek to.
   *
   * Then we pull 4 bytes from that offset, and validate that we've seeked to a
   * what looks like a plausible packet.
   * If that fails, restart, with the seek target set to one less than the
   * offset we just tried. If we run out of places to try, treat that as a fatal
   * error.
   */
  if (!find_seek_offset_time (rmdemux, rmdemux->segment_start)) {
    GST_LOG_OBJECT (rmdemux, "Failed to find seek offset by time");
    ret = FALSE;
    goto done;
  }

  GST_LOG_OBJECT (rmdemux, "Validating offset %u", rmdemux->offset);
  validated = gst_rmdemux_validate_offset (rmdemux);
  while (!validated) {
    GST_INFO_OBJECT (rmdemux, "Failed to validate offset at %u",
        rmdemux->offset);
    if (!find_seek_offset_bytes (rmdemux, rmdemux->offset - 1)) {
      ret = FALSE;
      goto done;
    }
    validated = gst_rmdemux_validate_offset (rmdemux);
  }

  GST_LOG_OBJECT (rmdemux, "Found final offset. Excellent!");

  /* now we have a new position, prepare for streaming again */
  {
    GstEvent *event;

    /* Reset the demuxer state */
    rmdemux->state = RMDEMUX_STATE_DATA_PACKET;
    rmdemux->cur_timestamp = GST_CLOCK_TIME_NONE;

    if (flush)
      gst_rmdemux_send_event (rmdemux, gst_event_new_flush_stop ());

    /* create the discont event we are going to send out */
    event = gst_event_new_new_segment (FALSE, 1.0,
        GST_FORMAT_TIME, (gint64) rmdemux->segment_start,
        (gint64) rmdemux->segment_stop, rmdemux->segment_start);

    GST_DEBUG_OBJECT (rmdemux,
        "sending NEWSEGMENT event to all src pads with segment_start= %"
        GST_TIME_FORMAT, GST_TIME_ARGS (rmdemux->segment_start));
    gst_rmdemux_send_event (rmdemux, event);

    /* notify start of new segment */
    if (rmdemux->segment_play) {
      gst_element_post_message (GST_ELEMENT (rmdemux),
          gst_message_new_segment_start (GST_OBJECT (rmdemux), GST_FORMAT_TIME,
              rmdemux->segment_start));
    }
    /* restart our task since it might have been stopped when we did the 
     * flush. */
    gst_pad_start_task (rmdemux->sinkpad, (GstTaskFunction) gst_rmdemux_loop,
        rmdemux->sinkpad);
  }

done:

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (rmdemux->sinkpad);

  return ret;
}


static gboolean
gst_rmdemux_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstRMDemux *rmdemux;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      GST_DEBUG_OBJECT (rmdemux, "src_query position");
      gst_query_set_position (query, GST_FORMAT_TIME, -1);      //rmdemux->cur_timestamp, 
      GST_DEBUG_OBJECT (rmdemux, "Position query: no idea from demuxer!");
      break;
    case GST_QUERY_DURATION:
      GST_DEBUG_OBJECT (rmdemux, "src_query duration");
      gst_query_set_duration (query, GST_FORMAT_TIME,   //rmdemux->cur_timestamp, 
          rmdemux->duration);
      GST_DEBUG_OBJECT (rmdemux, "Duration query: set to %lld in demuxer",
          rmdemux->duration);
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstQueryType *
gst_rmdemux_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return query_types;
}

static GstStateChangeReturn
gst_rmdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rmdemux->state = RMDEMUX_STATE_HEADER;
      rmdemux->have_pads = FALSE;
      rmdemux->segment_start = GST_CLOCK_TIME_NONE;
      rmdemux->segment_stop = GST_CLOCK_TIME_NONE;
      rmdemux->segment_play = FALSE;
      rmdemux->running = FALSE;
      rmdemux->cur_timestamp = GST_CLOCK_TIME_NONE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (rmdemux->adapter);
      GST_OBJECT_LOCK (rmdemux);
      rmdemux->running = FALSE;
      GST_OBJECT_UNLOCK (rmdemux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}

static GstCaps *
gst_rmdemux_src_getcaps (GstPad * pad)
{
  guint n;
  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rmdemux, "getcaps");

  for (n = 0; n < rmdemux->n_streams; n++) {
    if (rmdemux->streams[n] != NULL && rmdemux->streams[n]->pad == pad) {
      return gst_caps_copy (rmdemux->streams[n]->caps);
    }
  }

  /* Base case */
  return gst_caps_new_empty ();
}

/* this function is called when the pad is activated and should start
 * processing data.
 *
 * We check if we can do random access to decide if we work push or
 * pull based.
 */
static gboolean
gst_rmdemux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

/* this function gets called when we activate ourselves in push mode.
 * We cannot seek (ourselves) in the stream */
static gboolean
gst_rmdemux_sink_activate_push (GstPad * pad, gboolean active)
{
  GstRMDemux *rmdemux;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rmdemux, "activate_push");

  rmdemux->seekable = FALSE;

  return TRUE;
}

/* this function gets called when we activate ourselves in pull mode.
 * We can perform  random access to the resource and we start a task
 * to start reading */
static gboolean
gst_rmdemux_sink_activate_pull (GstPad * pad, gboolean active)
{
  GstRMDemux *rmdemux;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rmdemux, "activate_pull");

  if (active) {
    rmdemux->seekable = TRUE;
    rmdemux->offset = 0;
    rmdemux->loop_state = RMDEMUX_LOOP_STATE_HEADER;
    rmdemux->data_offset = G_MAXUINT;

    return gst_pad_start_task (pad, (GstTaskFunction) gst_rmdemux_loop, pad);
  } else {
    return gst_pad_stop_task (pad);
  }
}

/* random access mode - just pass over to our chain function */
static void
gst_rmdemux_loop (GstPad * pad)
{
  GstRMDemux *rmdemux;
  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  guint size;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rmdemux, "loop with state=%d and offset=0x%x",
      rmdemux->loop_state, rmdemux->offset);

  switch (rmdemux->state) {
    case RMDEMUX_STATE_HEADER:
      size = HEADER_SIZE;
      break;
    case RMDEMUX_STATE_HEADER_DATA:
      size = DATA_SIZE;
      break;
    case RMDEMUX_STATE_DATA_PACKET:
      size = rmdemux->avg_packet_size;
      break;
    case RMDEMUX_STATE_EOS:
      GST_LOG_OBJECT (rmdemux, "At EOS, pausing task");
      goto need_pause;
    default:
      GST_LOG_OBJECT (rmdemux, "Default: requires %d bytes (state is %d)",
          (int) rmdemux->size, rmdemux->state);
      size = rmdemux->size;
  }

  ret = gst_pad_pull_range (pad, rmdemux->offset, size, &buffer);
  if (ret != GST_FLOW_OK) {
    if (rmdemux->offset == rmdemux->index_offset) {
      /* The index isn't available so forget about it */
      rmdemux->loop_state = RMDEMUX_LOOP_STATE_DATA;
      rmdemux->offset = rmdemux->data_offset;
      GST_OBJECT_LOCK (rmdemux);
      rmdemux->running = TRUE;
      rmdemux->seekable = FALSE;
      GST_OBJECT_UNLOCK (rmdemux);
      return;
    } else {
      GST_DEBUG_OBJECT (rmdemux,
          "Unable to pull %d bytes at offset %p (pull_range returned %d, state is %d)",
          (int) size, rmdemux->offset, ret, GST_STATE (rmdemux));
      goto need_pause;
    }
  }

  size = GST_BUFFER_SIZE (buffer);

  /* Defer to the chain function */
  ret = gst_rmdemux_chain (pad, buffer);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (rmdemux, "Chain flow failed at %p", rmdemux->offset);
    goto need_pause;
  }

  rmdemux->offset += size;

  switch (rmdemux->loop_state) {
    case RMDEMUX_LOOP_STATE_HEADER:
      if (rmdemux->offset >= rmdemux->data_offset) {
        /* It's the end of the header */
        rmdemux->loop_state = RMDEMUX_LOOP_STATE_INDEX;
        rmdemux->offset = rmdemux->index_offset;
      }
      break;
    case RMDEMUX_LOOP_STATE_INDEX:
      if (rmdemux->state == RMDEMUX_STATE_HEADER) {
        if (rmdemux->index_offset == 0) {
          /* We've read the last index */
          rmdemux->loop_state = RMDEMUX_LOOP_STATE_DATA;
          rmdemux->offset = rmdemux->data_offset;
          GST_OBJECT_LOCK (rmdemux);
          rmdemux->running = TRUE;
          GST_OBJECT_UNLOCK (rmdemux);
        } else {
          /* Get the next index */
          rmdemux->offset = rmdemux->index_offset;
        }
      }
      break;
    case RMDEMUX_LOOP_STATE_DATA:
      break;
  }

  return;

need_pause:
  {
    GST_LOG_OBJECT (rmdemux, "pausing task");
    gst_pad_pause_task (pad);
    if (GST_FLOW_IS_FATAL (ret)) {
      gst_rmdemux_send_event (rmdemux, gst_event_new_eos ());
      /* FIXME: add translations */
      if (ret != GST_FLOW_UNEXPECTED) {
        GST_ELEMENT_ERROR (rmdemux, STREAM, FAILED,
            (("Internal data stream error.")),
            ("stream stopped, reason %d", ret));
      }
    }
    return;
  }
}

static gboolean
gst_rmdemux_fourcc_isplausible (guint32 fourcc)
{
  int i;

  for (i = 0; i < 4; i++) {
    if (!isprint ((int) ((unsigned char *) (&fourcc))[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

static GstFlowReturn
gst_rmdemux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data;
  guint16 version;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (rmdemux, "Chaining buffer of size %d",
      GST_BUFFER_SIZE (buffer));

  gst_adapter_push (rmdemux->adapter, buffer);

  while (TRUE) {
    GST_LOG_OBJECT (rmdemux, "looping in chain");
    switch (rmdemux->state) {
      case RMDEMUX_STATE_HEADER:
      {
        if (gst_adapter_available (rmdemux->adapter) < HEADER_SIZE)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, HEADER_SIZE);

        rmdemux->object_id = RMDEMUX_FOURCC_GET (data + 0);
        rmdemux->size = RMDEMUX_GUINT32_GET (data + 4) - HEADER_SIZE;
        rmdemux->object_version = RMDEMUX_GUINT16_GET (data + 8);

        /* Sanity-check. We assume that the FOURCC is printable ASCII */
        if (!gst_rmdemux_fourcc_isplausible (rmdemux->object_id)) {
          /* Failed. Remain in HEADER state, try again... We flush only 
           * the actual FOURCC, not the entire header, because we could 
           * need to resync anywhere at all... really, this should never 
           * happen. */
          GST_WARNING_OBJECT (rmdemux, "Bogus looking header, unprintable "
              "FOURCC");
          gst_adapter_flush (rmdemux->adapter, 4);

          break;
        }

        GST_LOG_OBJECT (rmdemux, "header found with object_id=%"
            GST_FOURCC_FORMAT
            " size=%08x object_version=%d",
            GST_FOURCC_ARGS (rmdemux->object_id), rmdemux->size,
            rmdemux->object_version);

        gst_adapter_flush (rmdemux->adapter, HEADER_SIZE);

        switch (rmdemux->object_id) {
          case GST_MAKE_FOURCC ('.', 'R', 'M', 'F'):
            rmdemux->state = RMDEMUX_STATE_HEADER_RMF;
            break;
          case GST_MAKE_FOURCC ('P', 'R', 'O', 'P'):
            rmdemux->state = RMDEMUX_STATE_HEADER_PROP;
            break;
          case GST_MAKE_FOURCC ('M', 'D', 'P', 'R'):
            rmdemux->state = RMDEMUX_STATE_HEADER_MDPR;
            break;
          case GST_MAKE_FOURCC ('I', 'N', 'D', 'X'):
            rmdemux->state = RMDEMUX_STATE_HEADER_INDX;
            break;
          case GST_MAKE_FOURCC ('D', 'A', 'T', 'A'):
            rmdemux->state = RMDEMUX_STATE_HEADER_DATA;
            break;
          case GST_MAKE_FOURCC ('C', 'O', 'N', 'T'):
            rmdemux->state = RMDEMUX_STATE_HEADER_CONT;
            break;
          default:
            rmdemux->state = RMDEMUX_STATE_HEADER_UNKNOWN;
            break;
        }
        break;
      }
      case RMDEMUX_STATE_HEADER_UNKNOWN:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        GST_WARNING_OBJECT (rmdemux, "Unknown object_id %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (rmdemux->object_id));

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_RMF:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        if ((rmdemux->object_version == 0) || (rmdemux->object_version == 1)) {
          data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

          gst_rmdemux_parse__rmf (rmdemux, data, rmdemux->size);
        }

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_PROP:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_prop (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_MDPR:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        ret = gst_rmdemux_parse_mdpr (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_CONT:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_cont (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_DATA:
      {
        /* If we haven't already done so then signal there are no more pads */
        if (!rmdemux->have_pads) {
          gst_element_no_more_pads (GST_ELEMENT (rmdemux));
          rmdemux->have_pads = TRUE;

          GST_LOG_OBJECT (rmdemux, "no more pads.");
          gst_rmdemux_send_event (rmdemux,
              gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                  (gint64) 0, (gint64) - 1, 0));
        }

        /* The actual header is only 8 bytes */
        rmdemux->size = DATA_SIZE;
        GST_DEBUG_OBJECT (rmdemux, "data available %d",
            gst_adapter_available (rmdemux->adapter));
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_data (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);

        rmdemux->state = RMDEMUX_STATE_DATA_PACKET;
        break;
      }
      case RMDEMUX_STATE_HEADER_INDX:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        rmdemux->size = gst_rmdemux_parse_indx (rmdemux, data, rmdemux->size);

        /* Only flush the header */
        gst_adapter_flush (rmdemux->adapter, HEADER_SIZE);

        rmdemux->state = RMDEMUX_STATE_INDX_DATA;
        break;
      }
      case RMDEMUX_STATE_INDX_DATA:
      {
        /* There's not always an data to get... */
        if (rmdemux->size > 0) {
          if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
            goto unlock;

          data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

          gst_rmdemux_parse_indx_data (rmdemux, data, rmdemux->size);

          gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        }

        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_DATA_PACKET:
      {
        if (gst_adapter_available (rmdemux->adapter) < 2)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, 2);
        version = RMDEMUX_GUINT16_GET (data);
        GST_DEBUG_OBJECT (rmdemux, "Data packet with version=%d", version);

        if (version == 0 || version == 1) {
          guint16 length;

          if (gst_adapter_available (rmdemux->adapter) < 4)
            goto unlock;
          data = gst_adapter_peek (rmdemux->adapter, 4);

          length = RMDEMUX_GUINT16_GET (data + 2);
          if (length < 4) {
            /* Invalid, just drop it */
            gst_adapter_flush (rmdemux->adapter, 4);
          } else {
            if (gst_adapter_available (rmdemux->adapter) < length)
              goto unlock;
            data = gst_adapter_peek (rmdemux->adapter, length);

            gst_rmdemux_parse_packet (rmdemux, data + 4, version, length - 4);
            rmdemux->chunk_index++;

            gst_adapter_flush (rmdemux->adapter, length);
          }

          if (rmdemux->chunk_index == rmdemux->n_chunks || length == 0)
            rmdemux->state = RMDEMUX_STATE_HEADER;
        } else {
          /* Stream done */
          gst_adapter_flush (rmdemux->adapter, 2);

          if (rmdemux->data_offset == 0) {
            GST_LOG_OBJECT (rmdemux,
                "No further data, internal demux state EOS");
            rmdemux->state = RMDEMUX_STATE_EOS;
          } else
            rmdemux->state = RMDEMUX_STATE_HEADER;
        }
        break;
      }
      case RMDEMUX_STATE_EOS:
        gst_rmdemux_send_event (rmdemux, gst_event_new_eos ());
        goto unlock;
      default:
        GST_WARNING_OBJECT (rmdemux, "Unhandled state %d", rmdemux->state);
        goto unlock;
    }


  }

unlock:
  return ret;
}

static GstRMDemuxStream *
gst_rmdemux_get_stream_by_id (GstRMDemux * rmdemux, int id)
{
  int i;

  for (i = 0; i < rmdemux->n_streams; i++) {
    GstRMDemuxStream *stream;

    stream = rmdemux->streams[i];
    if (stream->id == id) {
      return stream;
    }
  }

  return NULL;
}

static gboolean
gst_rmdemux_send_event (GstRMDemux * rmdemux, GstEvent * event)
{
  int i;
  gboolean ret = TRUE;

  for (i = 0; i < rmdemux->n_streams; i++) {
    GstRMDemuxStream *stream;

    stream = rmdemux->streams[i];

    GST_DEBUG_OBJECT (rmdemux, "Pushing event to stream %d", i);

    gst_event_ref (event);
    ret = gst_pad_push_event (stream->pad, event);
    if (!ret)
      break;
  }
  gst_event_unref (event);

  return ret;
}

GstFlowReturn
gst_rmdemux_add_stream (GstRMDemux * rmdemux, GstRMDemuxStream * stream)
{
  GstFlowReturn ret = GST_FLOW_OK;
  int version = 0;

  if (stream->subtype == GST_RMDEMUX_STREAM_VIDEO) {
    char *name = g_strdup_printf ("video_%02d", rmdemux->n_video_streams);

    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_rmdemux_videosrc_template), name);
    g_free (name);

    switch (stream->fourcc) {
      case GST_RM_VDO_RV10:
        version = 1;
        break;
      case GST_RM_VDO_RV20:
        version = 2;
        break;
      case GST_RM_VDO_RV30:
        version = 3;
        break;
      case GST_RM_VDO_RV40:
        version = 4;
        break;
      default:
        GST_WARNING_OBJECT (rmdemux, "Unknown video FOURCC code");
    }

    if (version) {
      stream->caps =
          gst_caps_new_simple ("video/x-pn-realvideo", "rmversion", G_TYPE_INT,
          (int) version,
          "format", G_TYPE_INT,
          (int) stream->format,
          "subformat", G_TYPE_INT, (int) stream->subformat, NULL);
    }

    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height,
          "framerate", GST_TYPE_FRACTION, stream->framerate_numerator,
          stream->framerate_denominator, NULL);
    }
    rmdemux->n_video_streams++;

  } else if (stream->subtype == GST_RMDEMUX_STREAM_AUDIO) {
    char *name = g_strdup_printf ("audio_%02d", rmdemux->n_audio_streams);

    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_rmdemux_audiosrc_template), name);
    GST_LOG_OBJECT (rmdemux, "Created audio pad \"%s\"", name);
    g_free (name);
    switch (stream->fourcc) {
        /* Older RealAudio Codecs */
      case GST_RM_AUD_14_4:
        version = 1;
        break;

      case GST_RM_AUD_28_8:
        version = 2;
        break;

        /* DolbyNet (Dolby AC3, low bitrate) */
      case GST_RM_AUD_DNET:
        stream->caps =
            gst_caps_new_simple ("audio/x-ac3", "rate", G_TYPE_INT,
            (int) stream->rate, NULL);
        break;

        /* RealAudio 10 (AAC) */
      case GST_RM_AUD_RAAC:
        version = 10;
        break;

        /* MPEG-4 based */
      case GST_RM_AUD_RACP:
        stream->caps =
            gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT,
            (int) 4, NULL);
        break;

        /* Sony ATRAC3 */
      case GST_RM_AUD_ATRC:
        stream->caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
        break;

        /* RealAudio G2 audio */
      case GST_RM_AUD_COOK:
        version = 8;
        break;

        /* RALF is lossless */
      case GST_RM_AUD_RALF:
        GST_DEBUG_OBJECT (rmdemux, "RALF");
        stream->caps = gst_caps_new_simple ("audio/x-ralf-mpeg4-generic", NULL);
        break;

        /* Sipro/ACELP.NET Voice Codec (MIME unknown) */
      case GST_RM_AUD_SIPR:
        stream->caps = gst_caps_new_simple ("audio/x-sipro", NULL);
        break;

      default:
        GST_WARNING_OBJECT (rmdemux,
            "Unknown audio FOURCC code \"%" GST_FOURCC_FORMAT "\"",
            stream->fourcc);
        break;
    }

    if (version) {
      stream->caps =
          gst_caps_new_simple ("audio/x-pn-realaudio", "raversion", G_TYPE_INT,
          (int) version, NULL);
    }

    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "flavor", G_TYPE_INT, (int) stream->flavor,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, (int) stream->n_channels,
          "width", G_TYPE_INT, (int) stream->sample_width,
          "leaf_size", G_TYPE_INT, (int) stream->leaf_size,
          "packet_size", G_TYPE_INT, (int) stream->packet_size,
          "height", G_TYPE_INT, (int) stream->height, NULL);
    }
    rmdemux->n_audio_streams++;
  } else {
    GST_WARNING_OBJECT (rmdemux, "not adding stream of type %d, freeing it",
        stream->subtype);
    g_free (stream);
    goto beach;
  }

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  rmdemux->streams[rmdemux->n_streams] = stream;
  rmdemux->n_streams++;
  GST_LOG_OBJECT (rmdemux, "n_streams is now %d", rmdemux->n_streams);

  if (stream->pad && stream->caps) {
    GST_DEBUG_OBJECT (rmdemux, "setting caps: %p", stream->caps);

    gst_pad_set_caps (stream->pad, stream->caps);
    gst_caps_unref (stream->caps);

    gst_pad_set_getcaps_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_getcaps));
    gst_pad_set_event_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_event));
    gst_pad_set_query_type_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_query_types));
    gst_pad_set_query_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_query));

    GST_DEBUG_OBJECT (rmdemux, "adding pad %p to rmdemux %p", stream->pad,
        rmdemux);
    gst_element_add_pad (GST_ELEMENT (rmdemux), stream->pad);

    gst_pad_push_event (stream->pad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, (gint64) 0,
            (gint64) - 1, 0));

    /* If there's some extra data then send it as the first packet */
    if (stream->extra_data_size > 0) {
      GstBuffer *buffer;

      if ((ret = gst_pad_alloc_buffer_and_set_caps
              (stream->pad, GST_BUFFER_OFFSET_NONE, stream->extra_data_size,
                  stream->caps, &buffer))
          != GST_FLOW_OK) {
        GST_WARNING_OBJECT (rmdemux, "failed to alloc extra_data src "
            "buffer for stream %d", stream->id);
        goto beach;
      }

      memcpy (GST_BUFFER_DATA (buffer), stream->extra_data,
          stream->extra_data_size);

      GST_DEBUG_OBJECT (rmdemux, "Pushing extra_data of size %d to pad",
          stream->extra_data_size);
      ret = gst_pad_push (stream->pad, buffer);
    }
  }

beach:
  return ret;
}

G_GNUC_UNUSED static void
re_hexdump_bytes (guint8 * ptr, int len, int offset)
{
  guint8 *end = ptr + len;
  int i;

  while (1) {
    if (ptr >= end)
      return;
    g_print ("%08x: ", offset);
    for (i = 0; i < 16; i++) {
      if (ptr + i >= end) {
        g_print ("   ");
      } else {
        g_print ("%02x ", ptr[i]);
      }
    }
    for (i = 0; i < 16; i++) {
      if (ptr + i >= end) {
        g_print (" ");
      } else {
        g_print ("%c", g_ascii_isprint (ptr[i]) ? ptr[i] : '.');
      }
    }
    g_print ("\n");
    ptr += 16;
    offset += 16;
  }
}

static char *
re_get_pascal_string (const guint8 * ptr)
{
  int length;

  length = ptr[0];
  return g_strndup ((char *) ptr + 1, length);
}

static int
re_skip_pascal_string (const guint8 * ptr)
{
  int length;

  length = ptr[0];

  return length + 1;
}

static void
gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, const void *data, int length)
{
  GST_LOG_OBJECT (rmdemux, "file_version: %d", RMDEMUX_GUINT32_GET (data));
  GST_LOG_OBJECT (rmdemux, "num_headers: %d", RMDEMUX_GUINT32_GET (data + 4));
}

static void
gst_rmdemux_parse_prop (GstRMDemux * rmdemux, const void *data, int length)
{
  GST_LOG_OBJECT (rmdemux, "max bitrate: %d", RMDEMUX_GUINT32_GET (data));
  GST_LOG_OBJECT (rmdemux, "avg bitrate: %d", RMDEMUX_GUINT32_GET (data + 4));
  GST_LOG_OBJECT (rmdemux, "max packet size: %d",
      RMDEMUX_GUINT32_GET (data + 8));
  rmdemux->avg_packet_size = RMDEMUX_GUINT32_GET (data + 12);
  GST_LOG_OBJECT (rmdemux, "avg packet size: %d", rmdemux->avg_packet_size);
  rmdemux->num_packets = RMDEMUX_GUINT32_GET (data + 16);
  GST_LOG_OBJECT (rmdemux, "number of packets: %d", rmdemux->num_packets);

  GST_LOG_OBJECT (rmdemux, "duration: %d", RMDEMUX_GUINT32_GET (data + 20));
  rmdemux->duration = RMDEMUX_GUINT32_GET (data + 20) * GST_MSECOND;

  GST_LOG_OBJECT (rmdemux, "preroll: %d", RMDEMUX_GUINT32_GET (data + 24));
  rmdemux->index_offset = RMDEMUX_GUINT32_GET (data + 28);
  GST_LOG_OBJECT (rmdemux, "offset of INDX section: 0x%08x",
      rmdemux->index_offset);
  rmdemux->data_offset = RMDEMUX_GUINT32_GET (data + 32);
  GST_LOG_OBJECT (rmdemux, "offset of DATA section: 0x%08x",
      rmdemux->data_offset);
  GST_LOG_OBJECT (rmdemux, "n streams: %d", RMDEMUX_GUINT16_GET (data + 36));
  GST_LOG_OBJECT (rmdemux, "flags: 0x%04x", RMDEMUX_GUINT16_GET (data + 38));
}

static GstFlowReturn
gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux, const void *data, int length)
{
  GstRMDemuxStream *stream;
  char *stream1_type_string;
  char *stream2_type_string;
  int stream_type;
  int offset;

  //re_hexdump_bytes ((guint8 *) data, length, 0);

  stream = g_new0 (GstRMDemuxStream, 1);

  stream->id = RMDEMUX_GUINT16_GET (data);
  stream->index = NULL;
  stream->seek_offset = 0;
  GST_LOG_OBJECT (rmdemux, "stream_number=%d", stream->id);

  offset = 30;
  stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
  stream1_type_string = re_get_pascal_string (data + offset);
  offset += re_skip_pascal_string (data + offset);
  stream2_type_string = re_get_pascal_string (data + offset);
  offset += re_skip_pascal_string (data + offset);

  /* stream1_type_string for audio and video stream is a "put_whatever_you_want" field :
     observed values :
     - "[The ]Video/Audio Stream" (File produced by an official Real encoder)
     - "RealVideoPremierePlugIn-VIDEO/AUDIO" (File produced by Abobe Premiere)

     so, we should not rely on it to know which stream type it is
   */

  GST_LOG_OBJECT (rmdemux, "stream type: %s", stream1_type_string);
  GST_LOG_OBJECT (rmdemux, "MIME type=%s", stream2_type_string);

  if (strcmp (stream2_type_string, "video/x-pn-realvideo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_VIDEO;
  } else if (strcmp (stream2_type_string, "audio/x-pn-realaudio") == 0) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream2_type_string, "audio/x-ralf-mpeg4-generic") == 0) {
    /* Another audio type found in the real testsuite */
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream1_type_string, "") == 0 &&
      strcmp (stream2_type_string, "logical-fileinfo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_FILEINFO;
  } else {
    stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
    GST_WARNING_OBJECT (rmdemux, "unknown stream type \"%s\",\"%s\"",
        stream1_type_string, stream2_type_string);
  }
  g_free (stream1_type_string);
  g_free (stream2_type_string);

  offset += 4;

  stream->subtype = stream_type;
  switch (stream_type) {

    case GST_RMDEMUX_STREAM_VIDEO:
      /* RV10/RV20/RV30/RV40 => video/x-pn-realvideo, version=1,2,3,4 */
      stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);
      stream->width = RMDEMUX_GUINT16_GET (data + offset + 12);
      stream->height = RMDEMUX_GUINT16_GET (data + offset + 14);
      stream->rate = RMDEMUX_GUINT16_GET (data + offset + 16);
      stream->subformat = RMDEMUX_GUINT32_GET (data + offset + 26);
      stream->format = RMDEMUX_GUINT32_GET (data + offset + 30);
      stream->extra_data_size = length - (offset + 34);
      stream->extra_data = (guint8 *) data + offset + 34;
      /* Natural way to represent framerates here requires unsigned 32 bit
       * numerator, which we don't have. For the nasty case, approximate...
       */
      {
        guint32 numerator = RMDEMUX_GUINT16_GET (data + offset + 22) * 65536 +
            RMDEMUX_GUINT16_GET (data + offset + 24);
        if (numerator > G_MAXINT) {
          stream->framerate_numerator = (gint) (numerator >> 1);
          stream->framerate_denominator = 32768;
        } else {
          stream->framerate_numerator = (gint) numerator;
          stream->framerate_denominator = 65536;
        }
      }

      GST_DEBUG_OBJECT (rmdemux,
          "Video stream with fourcc=%" GST_FOURCC_FORMAT
          " width=%d height=%d rate=%d framerate=%d/%d subformat=%x format=%x extra_data_size=%d",
          GST_FOURCC_ARGS (stream->fourcc), stream->width, stream->height,
          stream->rate, stream->framerate_numerator,
          stream->framerate_denominator, stream->subformat, stream->format,
          stream->extra_data_size);
      break;
    case GST_RMDEMUX_STREAM_AUDIO:{
      stream->version = RMDEMUX_GUINT16_GET (data + offset + 4);
      stream->flavor = RMDEMUX_GUINT16_GET (data + offset + 22);
      stream->packet_size = RMDEMUX_GUINT32_GET (data + offset + 24);
      stream->leaf_size = RMDEMUX_GUINT16_GET (data + offset + 44);
      stream->height = RMDEMUX_GUINT16_GET (data + offset + 40);

      switch (stream->version) {
        case 4:
          stream->rate = RMDEMUX_GUINT16_GET (data + offset + 48);
          stream->sample_width = RMDEMUX_GUINT16_GET (data + offset + 52);
          stream->n_channels = RMDEMUX_GUINT16_GET (data + offset + 54);
          stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 62);
          stream->extra_data_size = 16;
          stream->extra_data = (guint8 *) data + offset + 71;
          break;
        case 5:
          stream->rate = RMDEMUX_GUINT16_GET (data + offset + 54);
          stream->sample_width = RMDEMUX_GUINT16_GET (data + offset + 58);
          stream->n_channels = RMDEMUX_GUINT16_GET (data + offset + 60);
          stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 66);
          stream->extra_data_size = RMDEMUX_GUINT32_GET (data + offset + 74);
          stream->extra_data = (guint8 *) data + offset + 78;
          break;
      }

      /*  14_4, 28_8, cook, dnet, sipr, raac, racp, ralf, atrc */
      GST_DEBUG_OBJECT (rmdemux,
          "Audio stream with rate=%d sample_width=%d n_channels=%d",
          stream->rate, stream->sample_width, stream->n_channels);

      break;
    }
    case GST_RMDEMUX_STREAM_FILEINFO:
    {
      int element_nb;

      /* Length of this section */
      GST_DEBUG_OBJECT (rmdemux, "length2: 0x%08x",
          RMDEMUX_GUINT32_GET (data + offset));
      offset += 4;

      /* Unknown : 00 00 00 00 */
      offset += 4;

      /* Number of variables that would follow (loop iterations) */
      element_nb = RMDEMUX_GUINT32_GET (data + offset);
      offset += 4;

      while (element_nb) {
        /* Category Id : 00 00 00 XX 00 00 */
        offset += 6;

        /* Variable Name */
        offset += re_skip_pascal_string (data + offset);

        /* Variable Value Type */
        /*   00 00 00 00 00 => integer/boolean, preceded by length */
        /*   00 00 00 02 00 => pascal string, preceded by length, no trailing \0 */
        offset += 5;

        /* Variable Value */
        offset += re_skip_pascal_string (data + offset);

        element_nb--;
      }
    }
      break;
    case GST_RMDEMUX_STREAM_UNKNOWN:
    default:
      break;
  }

  return gst_rmdemux_add_stream (rmdemux, stream);
}

static guint
gst_rmdemux_parse_indx (GstRMDemux * rmdemux, const void *data, int length)
{
  int n;
  int id;

  n = RMDEMUX_GUINT32_GET (data);
  id = RMDEMUX_GUINT16_GET (data + 4);
  rmdemux->index_offset = RMDEMUX_GUINT32_GET (data + 6);

  GST_DEBUG_OBJECT (rmdemux, "Number of indices=%d Stream ID=%d length=%d", n,
      id, length);

  /* Point to the next index_stream */
  rmdemux->index_stream = gst_rmdemux_get_stream_by_id (rmdemux, id);

  /* Return the length of the index */
  return 14 * n;
}

static void
gst_rmdemux_parse_indx_data (GstRMDemux * rmdemux, const void *data, int length)
{
  int i;
  int n;
  GstRMDemuxIndex *index;

  /* The number of index records */
  n = length / 14;

  if (rmdemux->index_stream == NULL)
    return;

  index = g_malloc (sizeof (GstRMDemuxIndex) * n);
  rmdemux->index_stream->index = index;
  rmdemux->index_stream->index_length = n;

  for (i = 0; i < n; i++) {
    index[i].timestamp = RMDEMUX_GUINT32_GET (data + 2) * GST_MSECOND;
    index[i].offset = RMDEMUX_GUINT32_GET (data + 6);

    GST_DEBUG_OBJECT (rmdemux, "Index found for timestamp=%f (at offset=%x)",
        (float) index[i].timestamp / GST_SECOND, index[i].offset);
    data += 14;
  }
}

static void
gst_rmdemux_parse_data (GstRMDemux * rmdemux, const void *data, int length)
{
  rmdemux->n_chunks = RMDEMUX_GUINT32_GET (data);
  rmdemux->data_offset = RMDEMUX_GUINT32_GET (data + 4);
  rmdemux->chunk_index = 0;
  GST_DEBUG_OBJECT (rmdemux,
      "Data chunk found with %d packets (next data at %p)", rmdemux->n_chunks,
      rmdemux->data_offset);
}

static void
gst_rmdemux_parse_cont (GstRMDemux * rmdemux, const void *data, int length)
{
  gchar *title = (gchar *) re_get_pascal_string (data);

  GST_DEBUG_OBJECT (rmdemux, "File Content : (CONT) %s", title);
  g_free (title);
}

static GstFlowReturn
gst_rmdemux_parse_packet (GstRMDemux * rmdemux, const void *data,
    guint16 version, guint16 length)
{
  guint16 id;
  GstRMDemuxStream *stream;
  GstBuffer *buffer;
  guint16 packet_size;
  GstFlowReturn ret = GST_FLOW_OK;

  id = RMDEMUX_GUINT16_GET (data);
  rmdemux->cur_timestamp = RMDEMUX_GUINT32_GET (data + 2) * GST_MSECOND;

  GST_DEBUG_OBJECT (rmdemux,
      "Parsing a packet for stream=%d, timestamp=%" GST_TIME_FORMAT
      ", version=%d", id, GST_TIME_ARGS (rmdemux->cur_timestamp), version);

  // TODO: We read 6 bytes previously; this is skipping over either 2 or 3 
  // bytes (version dependent) // without even reading it. What are these for?
  if (version == 0) {
    data += 8;
    packet_size = length - 8;
  } else {
    data += 9;
    packet_size = length - 9;
  }

  stream = gst_rmdemux_get_stream_by_id (rmdemux, id);

  if (!stream) {
    GST_WARNING_OBJECT (rmdemux, "No stream for stream id %d in parsing "
        "data packet", id);
    goto beach;
  }

  if ((rmdemux->offset + packet_size) > stream->seek_offset &&
      stream && stream->pad) {
    if ((ret =
            gst_pad_alloc_buffer_and_set_caps (stream->pad,
                GST_BUFFER_OFFSET_NONE, packet_size, stream->caps,
                &buffer)) != GST_FLOW_OK) {
      GST_WARNING_OBJECT (rmdemux, "failed to alloc src buffer for stream %d",
          id);
      return ret;
    }

    memcpy (GST_BUFFER_DATA (buffer), (guint8 *) data, packet_size);
    GST_BUFFER_TIMESTAMP (buffer) = rmdemux->cur_timestamp;

    GST_DEBUG_OBJECT (rmdemux, "Pushing buffer of size %d to pad", packet_size);
    ret = gst_pad_push (stream->pad, buffer);
  } else {
    GST_DEBUG_OBJECT (rmdemux,
        "Stream %d is skipping: seek_offset=%d, offset=%d, packet_size",
        stream->id, stream->seek_offset, rmdemux->offset, packet_size);
  }

beach:
  return ret;
}
