/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 *                 Jan Schmidt <thaytan@noraisin.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpegdefs.h"
#include "gstmpegdemux.h"

#define SEGMENT_THRESHOLD (300*GST_MSECOND)
#define VIDEO_SEGMENT_THRESHOLD (500*GST_MSECOND)

/* The SCR_MUNGE value is used to offset the scr_adjust value, to avoid
 * ever generating a negative timestamp */
#define SCR_MUNGE (10 * GST_SECOND)

/* We clamp scr delta with 0 so negative bytes won't be possible */
#define GSTTIME_TO_BYTES(time) \
  ((time != -1) ? gst_util_uint64_scale (MAX(0,(gint64) (GSTTIME_TO_MPEGTIME(time) - demux->first_scr)), demux->scr_rate_n, demux->scr_rate_d) : -1)
#define BYTES_TO_GSTTIME(bytes) ((bytes != -1) ? MPEGTIME_TO_GSTTIME(gst_util_uint64_scale (bytes, demux->scr_rate_d, demux->scr_rate_n)) : -1)

#define ADAPTER_OFFSET_FLUSH(_bytes_) demux->adapter_offset += (_bytes_)

GST_DEBUG_CATEGORY_STATIC (gstflupsdemux_debug);
#define GST_CAT_DEFAULT (gstflupsdemux_debug)

GST_DEBUG_CATEGORY_EXTERN (gstflupesfilter_debug);

/* MPEG2Demux signals and args */
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

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-resin-dvd")
    );

static GstStaticPadTemplate video_template =
    GST_STATIC_PAD_TEMPLATE ("video_%02x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2, 4 }, " "systemstream = (boolean) FALSE;"
        "video/x-h264")
    );

static GstStaticPadTemplate audio_template =
    GST_STATIC_PAD_TEMPLATE ("audio_%02x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1;"
        "audio/x-private1-lpcm; "
        "audio/x-private1-ac3;" "audio/x-private1-dts;" "audio/ac3")
    );

static GstStaticPadTemplate subpicture_template =
GST_STATIC_PAD_TEMPLATE ("subpicture_%02x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-dvd-subpicture")
    );

static GstStaticPadTemplate private_template =
GST_STATIC_PAD_TEMPLATE ("private_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static void gst_flups_demux_base_init (GstFluPSDemuxClass * klass);
static void gst_flups_demux_class_init (GstFluPSDemuxClass * klass);
static void gst_flups_demux_init (GstFluPSDemux * demux);
static void gst_flups_demux_finalize (GstFluPSDemux * demux);
static void gst_flups_demux_reset (GstFluPSDemux * demux);

static gboolean gst_flups_demux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_flups_demux_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_flups_demux_src_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_flups_demux_chain (GstPad * pad, GstBuffer * buffer);

static GstStateChangeReturn gst_flups_demux_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_flups_demux_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_flups_demux_get_type (void)
{
  static GType flups_demux_type = 0;

  if (!flups_demux_type) {
    static const GTypeInfo flups_demux_info = {
      sizeof (GstFluPSDemuxClass),
      (GBaseInitFunc) gst_flups_demux_base_init,
      NULL,
      (GClassInitFunc) gst_flups_demux_class_init,
      NULL,
      NULL,
      sizeof (GstFluPSDemux),
      0,
      (GInstanceInitFunc) gst_flups_demux_init,
    };

    flups_demux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "RsnDVDDemux",
        &flups_demux_info, 0);

    GST_DEBUG_CATEGORY_INIT (gstflupsdemux_debug, "rsndvddemux", 0,
        "MPEG program stream demultiplexer element");
  }

  return flups_demux_type;
}

static void
gst_flups_demux_base_init (GstFluPSDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  klass->sink_template = gst_static_pad_template_get (&sink_template);
  klass->video_template = gst_static_pad_template_get (&video_template);
  klass->audio_template = gst_static_pad_template_get (&audio_template);
  klass->subpicture_template =
      gst_static_pad_template_get (&subpicture_template);
  klass->private_template = gst_static_pad_template_get (&private_template);

  gst_element_class_add_pad_template (element_class, klass->video_template);
  gst_element_class_add_pad_template (element_class, klass->audio_template);
  gst_element_class_add_pad_template (element_class, klass->private_template);
  gst_element_class_add_pad_template (element_class,
      klass->subpicture_template);
  gst_element_class_add_pad_template (element_class, klass->sink_template);

  gst_element_class_set_details_simple (element_class, "MPEG Program Demuxer",
      "Codec/Demuxer",
      "Demultiplexes MPEG Program Streams", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_flups_demux_class_init (GstFluPSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = (GObjectFinalizeFunc) gst_flups_demux_finalize;

  gstelement_class->change_state = gst_flups_demux_change_state;
}

static void
gst_flups_demux_init (GstFluPSDemux * demux)
{
  GstFluPSDemuxClass *klass = GST_FLUPS_DEMUX_GET_CLASS (demux);

  demux->sinkpad = gst_pad_new_from_template (klass->sink_template, "sink");
  gst_pad_set_event_function (demux->sinkpad, gst_flups_demux_sink_event);
  gst_pad_set_chain_function (demux->sinkpad, gst_flups_demux_chain);
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->streams =
      g_malloc0 (sizeof (GstFluPSStream *) * (GST_FLUPS_DEMUX_MAX_STREAMS));

  demux->scr_adjust = GSTTIME_TO_MPEGTIME (SCR_MUNGE);
}

static void
gst_flups_demux_finalize (GstFluPSDemux * demux)
{
  gst_flups_demux_reset (demux);
  g_free (demux->streams);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (demux));
}

static void
gst_flups_demux_reset (GstFluPSDemux * demux)
{
  /* Clean up the streams and pads we allocated */
  gint i;
  GstEvent **p_ev;

  for (i = 0; i < GST_FLUPS_DEMUX_MAX_STREAMS; i++) {
    GstFluPSStream *stream = demux->streams[i];

    if (stream != NULL) {
      if (stream->pad)
        gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->pad);

      g_free (stream);
      demux->streams[i] = NULL;
    }
  }

  p_ev = &demux->lang_codes;
  gst_event_replace (p_ev, NULL);

  demux->scr_adjust = GSTTIME_TO_MPEGTIME (SCR_MUNGE);
}

static GstFluPSStream *
gst_flups_demux_create_stream (GstFluPSDemux * demux, gint id, gint stream_type)
{
  GstFluPSStream *stream;
  GstPadTemplate *template;
  gchar *name;
  GstFluPSDemuxClass *klass = GST_FLUPS_DEMUX_GET_CLASS (demux);
  GstCaps *caps;
  GstClockTime threshold = SEGMENT_THRESHOLD;

  name = NULL;
  template = NULL;
  caps = NULL;

  GST_DEBUG_OBJECT (demux, "create stream id 0x%02x, type 0x%02x", id,
      stream_type);

  switch (stream_type) {
    case ST_VIDEO_MPEG1:
    case ST_VIDEO_MPEG2:
    case ST_VIDEO_MPEG4:
    case ST_GST_VIDEO_MPEG1_OR_2:
    {
      gint mpeg_version = 1;

      if (stream_type == ST_VIDEO_MPEG2 ||
          (stream_type == ST_GST_VIDEO_MPEG1_OR_2 && demux->is_mpeg2_pack)) {
        mpeg_version = 2;
      }
      if (stream_type == ST_VIDEO_MPEG4) {
        mpeg_version = 4;
      }

      template = klass->video_template;
      name = g_strdup_printf ("video_%02x", id);
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, mpeg_version,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      threshold = VIDEO_SEGMENT_THRESHOLD;
      break;
    }
    case ST_AUDIO_MPEG1:
    case ST_AUDIO_MPEG2:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%02x", id);
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, NULL);
      break;
    case ST_PRIVATE_SECTIONS:
    case ST_PRIVATE_DATA:
    case ST_MHEG:
    case ST_DSMCC:
    case ST_AUDIO_AAC:
      break;
    case ST_VIDEO_H264:
      template = klass->video_template;
      name = g_strdup_printf ("video_%02x", id);
      caps = gst_caps_new_simple ("video/x-h264", NULL);
      threshold = VIDEO_SEGMENT_THRESHOLD;
      break;
    case ST_PS_AUDIO_AC3:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%02x", id);
      caps = gst_caps_new_simple ("audio/x-private1-ac3", NULL);
      break;
    case ST_PS_AUDIO_DTS:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%02x", id);
      caps = gst_caps_new_simple ("audio/x-private1-dts", NULL);
      break;
    case ST_PS_AUDIO_LPCM:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%02x", id);
      caps = gst_caps_new_simple ("audio/x-private1-lpcm", NULL);
      break;
    case ST_PS_DVD_SUBPICTURE:
      template = klass->subpicture_template;
      name = g_strdup_printf ("subpicture_%02x", id);
      caps = gst_caps_new_simple ("video/x-dvd-subpicture", NULL);
      break;
    case ST_GST_AUDIO_RAWA52:
      template = klass->audio_template;
      name = g_strdup_printf ("audio_%02x", id);
      caps = gst_caps_new_simple ("audio/ac3", NULL);
      break;
    default:
      break;
  }

  if (name == NULL || template == NULL || caps == NULL)
    return NULL;

  stream = g_new0 (GstFluPSStream, 1);
  stream->id = id;
  stream->discont = TRUE;
  stream->notlinked = FALSE;
  stream->type = stream_type;
  stream->pad = gst_pad_new_from_template (template, name);
  stream->segment_thresh = threshold;
  gst_pad_set_event_function (stream->pad, gst_flups_demux_src_event);
  gst_pad_set_query_function (stream->pad, gst_flups_demux_src_query);
  gst_pad_use_fixed_caps (stream->pad);
  gst_pad_set_caps (stream->pad, caps);
  gst_caps_unref (caps);
  GST_DEBUG_OBJECT (demux, "create pad %s, caps %" GST_PTR_FORMAT, name, caps);
  g_free (name);

  return stream;
}

static GstFluPSStream *
gst_flups_demux_get_stream (GstFluPSDemux * demux, gint id, gint type)
{
  GstFluPSStream *stream = demux->streams[id];

  if (stream == NULL && !demux->disable_stream_creation) {
    if (!(stream = gst_flups_demux_create_stream (demux, id, type)))
      goto unknown_stream;

    GST_DEBUG_OBJECT (demux, "adding pad for stream id 0x%02x type 0x%02x", id,
        type);

    gst_pad_set_active (stream->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (demux), stream->pad);

    demux->streams[id] = stream;
  }
  return stream;

  /* ERROR */
unknown_stream:
  {
    GST_DEBUG_OBJECT (demux, "unknown stream id 0x%02x type 0x%02x", id, type);
    return NULL;
  }
}

static GstFlowReturn
gst_flups_demux_send_data (GstFluPSDemux * demux, GstFluPSStream * stream,
    GstBuffer * buf)
{
  GstFlowReturn result;
  guint64 timestamp;
  guint size;

  if (stream == NULL)
    goto no_stream;

  /* timestamps */
  if (demux->next_pts != G_MAXUINT64)
    timestamp = MPEGTIME_TO_GSTTIME (demux->next_pts);
  else
    timestamp = GST_CLOCK_TIME_NONE;

  if (demux->current_scr != G_MAXUINT64) {
    GstClockTime cur_scr_time = MPEGTIME_TO_GSTTIME (demux->current_scr);

    if (stream->last_ts == GST_CLOCK_TIME_NONE ||
        stream->last_ts < cur_scr_time) {
#if 0
      g_print ("last_ts update on pad %s to time %" GST_TIME_FORMAT "\n",
          GST_PAD_NAME (stream->pad), GST_TIME_ARGS (cur_scr_time));
#endif
      stream->last_ts = cur_scr_time;
    }
  }

  /* OK, sent new segment now prepare the buffer for sending */
  /* caps */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));
  GST_BUFFER_TIMESTAMP (buf) = timestamp;

  /* Set the buffer discont flag, and clear discont state on the stream */
  if (stream->discont) {
    GST_DEBUG_OBJECT (demux, "discont buffer to pad %" GST_PTR_FORMAT
        " with TS %" GST_TIME_FORMAT, stream->pad, GST_TIME_ARGS (timestamp));

    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }
  size = GST_BUFFER_SIZE (buf);

  demux->next_pts = G_MAXUINT64;
  demux->next_dts = G_MAXUINT64;

  result = gst_pad_push (stream->pad, buf);
  GST_DEBUG_OBJECT (demux, "pushed stream id 0x%02x type 0x%02x, time: %"
      GST_TIME_FORMAT ", size %d. result: %s",
      stream->id, stream->type, GST_TIME_ARGS (timestamp),
      size, gst_flow_get_name (result));

  return result;

  /* ERROR */
no_stream:
  {
    GST_DEBUG_OBJECT (demux, "no stream given");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }
}

static void
gst_flups_demux_mark_discont (GstFluPSDemux * demux)
{
  gint id;

  /* mark discont on all streams */
  for (id = 0; id < GST_FLUPS_DEMUX_MAX_STREAMS; id++) {
    GstFluPSStream *stream = demux->streams[id];

    if (stream) {
      stream->discont = TRUE;
      GST_DEBUG_OBJECT (demux, "marked stream as discont %d", stream->discont);
    }
  }
}

static void
gst_flups_demux_clear_times (GstFluPSDemux * demux)
{
  gint id;

  /* Clear the last ts for all streams */
  for (id = 0; id < GST_FLUPS_DEMUX_MAX_STREAMS; id++) {
    GstFluPSStream *stream = demux->streams[id];

    if (stream) {
      stream->last_seg_start = stream->last_ts = GST_CLOCK_TIME_NONE;
    }
  }
}

static void
gst_flups_demux_send_segment_updates (GstFluPSDemux * demux,
    GstClockTime new_time)
{
  /* Advance all lagging streams by sending a segment update */
  gint id;
  GstEvent *event = NULL;

  if (new_time > demux->src_segment.stop)
    return;

  for (id = 0; id < GST_FLUPS_DEMUX_MAX_STREAMS; id++) {
    GstFluPSStream *stream = demux->streams[id];

    if (stream) {
      if (stream->last_ts == GST_CLOCK_TIME_NONE ||
          stream->last_ts < demux->src_segment.start)
        stream->last_ts = demux->src_segment.start;
      if (stream->last_ts + stream->segment_thresh < new_time) {
#if 0
        g_print ("Segment update to pad %s time %" GST_TIME_FORMAT " stop now %"
            GST_TIME_FORMAT " last_stop %" GST_TIME_FORMAT "\n",
            GST_PAD_NAME (stream->pad), GST_TIME_ARGS (new_time),
            GST_TIME_ARGS (demux->src_segment.stop),
            GST_TIME_ARGS (demux->src_segment.last_stop));
#endif
        GST_DEBUG_OBJECT (demux,
            "Segment update to pad %s time %" GST_TIME_FORMAT,
            GST_PAD_NAME (stream->pad), GST_TIME_ARGS (new_time));
        if (event == NULL) {
          event = gst_event_new_new_segment_full (TRUE,
              demux->src_segment.rate, demux->src_segment.applied_rate,
              GST_FORMAT_TIME, new_time,
              demux->src_segment.stop,
              demux->src_segment.time + (new_time - demux->src_segment.start));
        }
        gst_event_ref (event);
        gst_pad_push_event (stream->pad, event);
        stream->last_seg_start = stream->last_ts = new_time;
      }
    }
  }

  if (event)
    gst_event_unref (event);
}

static void
gst_flups_demux_send_segment_close (GstFluPSDemux * demux)
{
  gint id;
  GstEvent *event = NULL;
  GstClockTime stop = demux->src_segment.stop;

  if (demux->src_segment.last_stop != -1 && demux->src_segment.last_stop > stop)
    stop = demux->src_segment.last_stop;

  for (id = 0; id < GST_FLUPS_DEMUX_MAX_STREAMS; id++) {
    GstFluPSStream *stream = demux->streams[id];

    if (stream) {
      GstClockTime start = demux->src_segment.start;

      if (stream->last_seg_start != GST_CLOCK_TIME_NONE &&
          stream->last_seg_start > start)
        start = stream->last_seg_start;

#if 0
      g_print ("Segment close to pad %s start %" GST_TIME_FORMAT
          " stop %" GST_TIME_FORMAT "\n",
          GST_PAD_NAME (stream->pad), GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));
#endif
      if (start > stop) {
        g_print ("Problem on pad %s with start %" GST_TIME_FORMAT " > stop %"
            GST_TIME_FORMAT "\n",
            gst_object_get_name (GST_OBJECT (stream->pad)),
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
      }
      event = gst_event_new_new_segment_full (TRUE,
          demux->src_segment.rate, demux->src_segment.applied_rate,
          GST_FORMAT_TIME, start,
          stop, demux->src_segment.time + (start - demux->src_segment.start));
      if (event)
        gst_pad_push_event (stream->pad, event);
    }
  }
}

static gboolean
gst_flups_demux_send_event (GstFluPSDemux * demux, GstEvent * event)
{
  gint id;
  gboolean ret = FALSE;

  for (id = 0; id < GST_FLUPS_DEMUX_MAX_STREAMS; id++) {
    GstFluPSStream *stream = demux->streams[id];

    if (stream) {
      (void) gst_event_ref (event);

      if (!gst_pad_push_event (stream->pad, event)) {
        GST_DEBUG_OBJECT (stream, "event %s was not handled correctly by pad %"
            GST_PTR_FORMAT, GST_EVENT_TYPE_NAME (event), stream->pad);
      } else {
        /* If at least one push returns TRUE, then we return TRUE. */
        GST_DEBUG_OBJECT (stream, "event %s was handled correctly by pad %"
            GST_PTR_FORMAT, GST_EVENT_TYPE_NAME (event), stream->pad);
        ret = TRUE;
      }
    }
  }

  gst_event_unref (event);
  return ret;
}

static gboolean
gst_flups_demux_handle_dvd_event (GstFluPSDemux * demux, GstEvent * event)
{
  const GstStructure *structure = gst_event_get_structure (event);
  const char *type = gst_structure_get_string (structure, "event");
  gint i;
  gchar cur_stream_name[32];
  GstFluPSStream *temp;
  gboolean ret = TRUE;

  if (strcmp (type, "dvd-lang-codes") == 0) {
    GstEvent **p_ev;

    /* Store the language codes event on the element, then iterate over the 
     * streams it specifies and retrieve them. The stream creation code then 
     * creates the pad appropriately and sends tag events as needed */
    p_ev = &demux->lang_codes;
    gst_event_replace (p_ev, event);

    GST_DEBUG_OBJECT (demux, "Handling language codes event");

    demux->disable_stream_creation = FALSE;

    /* Create a video pad to ensure it exists before emitting no more pads */
    temp = gst_flups_demux_get_stream (demux, 0xe0, ST_VIDEO_MPEG2);
    /* Send a video format event downstream */
    {
      gboolean is_widescreen, is_pal;

      if (gst_structure_get_boolean (structure,
              "video-widescreen", &is_widescreen) &&
          gst_structure_get_boolean (structure, "video-pal-format", &is_pal)) {
        GstEvent *v_format;
        GstStructure *v_struct;

        v_struct = gst_structure_new ("application/x-gst-dvd",
            "event", G_TYPE_STRING, "dvd-video-format",
            "video-widescreen", G_TYPE_BOOLEAN, is_widescreen,
            "video-pal-format", G_TYPE_BOOLEAN, is_pal, NULL);
        v_format = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, v_struct);
        gst_pad_push_event (temp->pad, v_format);
      }
    }

    /* Read out the languages for audio streams and request each one that 
     * is present */
    for (i = 0; i < MAX_DVD_AUDIO_STREAMS; i++) {
      gint stream_format;
      gint stream_id;

      g_snprintf (cur_stream_name, 32, "audio-%d-format", i);
      if (!gst_structure_get_int (structure, cur_stream_name, &stream_format))
        continue;

      g_snprintf (cur_stream_name, 32, "audio-%d-stream", i);
      if (!gst_structure_get_int (structure, cur_stream_name, &stream_id))
        continue;
      if (stream_id < 0 || stream_id >= MAX_DVD_AUDIO_STREAMS)
        continue;

      demux->audio_stream_types[i] = stream_format;
      switch (stream_format) {
        case 0x0:
          /* AC3 */
          stream_id += 0x80;
          GST_DEBUG_OBJECT (demux,
              "Audio stream %d format %d ID 0x%02x - AC3", i,
              stream_format, stream_id);
          temp = gst_flups_demux_get_stream (demux, stream_id, ST_PS_AUDIO_AC3);
          break;
        case 0x2:
        case 0x3:
          /* MPEG audio without and with extension stream are 
           * treated the same */
          stream_id += 0xC0;
          GST_DEBUG_OBJECT (demux,
              "Audio stream %d format %d ID 0x%02x - MPEG audio", i,
              stream_format, stream_id);
          temp = gst_flups_demux_get_stream (demux, stream_id, ST_AUDIO_MPEG1);
          break;
        case 0x4:
          /* LPCM */
          stream_id += 0xA0;
          GST_DEBUG_OBJECT (demux,
              "Audio stream %d format %d ID 0x%02x - DVD LPCM", i,
              stream_format, stream_id);
          temp =
              gst_flups_demux_get_stream (demux, stream_id, ST_PS_AUDIO_LPCM);
          break;
        case 0x6:
          /* DTS */
          stream_id += 0x88;
          GST_DEBUG_OBJECT (demux,
              "Audio stream %d format %d ID 0x%02x - DTS", i,
              stream_format, stream_id);
          temp = gst_flups_demux_get_stream (demux, stream_id, ST_PS_AUDIO_DTS);
          break;
        case 0x7:
          /* FIXME: What range is SDDS? */
          break;
        default:
          GST_WARNING_OBJECT (demux,
              "Unknown audio stream format in language code event: %d",
              stream_format);
          break;
      }
    }

    /* And subtitle streams */
    for (i = 0; i < MAX_DVD_SUBPICTURE_STREAMS; i++) {
      gint stream_format;
      gint stream_id;

      g_snprintf (cur_stream_name, 32, "subpicture-%d-format", i);
      if (!gst_structure_get_int (structure, cur_stream_name, &stream_format))
        continue;

      g_snprintf (cur_stream_name, 32, "subpicture-%d-stream", i);
      if (!gst_structure_get_int (structure, cur_stream_name, &stream_id))
        continue;
      if (stream_id < 0 || stream_id >= MAX_DVD_SUBPICTURE_STREAMS)
        continue;

      GST_DEBUG_OBJECT (demux, "Subpicture stream %d ID 0x%02x", i, 0x20 + i);

      /* Retrieve the subpicture stream to force pad creation */
      temp = gst_flups_demux_get_stream (demux, 0x20 + stream_id,
          ST_PS_DVD_SUBPICTURE);
    }

    demux->disable_stream_creation = TRUE;

    GST_DEBUG_OBJECT (demux, "Created all pads from Language Codes event, "
        "signalling no-more-pads");

    gst_element_no_more_pads (GST_ELEMENT (demux));
    demux->need_no_more_pads = FALSE;
    gst_event_unref (event);
  } else if (strcmp (type, "dvd-set-subpicture-track") == 0) {
    gint stream_id;
    gboolean forced_only;

    gst_structure_get_boolean (structure, "forced-only", &forced_only);

    if (gst_structure_get_int (structure, "physical-id", &stream_id)) {
      temp = demux->streams[0x20 + stream_id];
      if (temp != NULL && temp->pad != NULL) {
        /* Send event to the selector to activate the desired pad */
        GstStructure *s = gst_structure_new ("application/x-gst-dvd",
            "event", G_TYPE_STRING, "select-pad", NULL);
        GstEvent *sel_event =
            gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

        temp->notlinked = FALSE;
        gst_pad_push_event (temp->pad, sel_event);

        gst_event_ref (event);
        ret = gst_pad_push_event (temp->pad, event);
        GST_INFO_OBJECT (demux,
            "Subpicture physical ID change to %d, forced %d", stream_id,
            forced_only);
      }
    }
    gst_event_unref (event);
  } else if (strcmp (type, "dvd-set-audio-track") == 0) {
    gint stream_id;

    if (gst_structure_get_int (structure, "physical-id", &stream_id)) {
      gint aud_type;

      stream_id %= MAX_DVD_AUDIO_STREAMS;

      aud_type = demux->audio_stream_types[stream_id];

      switch (aud_type) {
        case 0x0:
          /* AC3 */
          stream_id += 0x80;
          temp = demux->streams[stream_id];
          break;
        case 0x2:
        case 0x3:
          /* MPEG audio without and with extension stream are 
           * treated the same */
          stream_id += 0xC0;
          temp = demux->streams[stream_id];
          break;
        case 0x4:
          /* LPCM */
          stream_id += 0xA0;
          temp = demux->streams[stream_id];
          break;
        case 0x6:
          /* DTS */
          stream_id += 0x88;
          temp = demux->streams[stream_id];
          break;
        case 0x7:
          /* FIXME: What range is SDDS? */
          temp = NULL;
          break;
        default:
          temp = NULL;
          break;
      }
      GST_INFO_OBJECT (demux, "Have DVD audio stream select event: "
          "stream 0x%02x", stream_id);
      if (temp != NULL && temp->pad != NULL) {
        /* Send event to the selector to activate the desired pad */
        GstStructure *s = gst_structure_new ("application/x-gst-dvd",
            "event", G_TYPE_STRING, "select-pad", NULL);
        GstEvent *sel_event =
            gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);
        gst_pad_push_event (temp->pad, sel_event);

        gst_event_ref (event);
        ret = gst_pad_push_event (temp->pad, event);
      }
    }
    ret = gst_flups_demux_send_event (demux, event);
  } else {
    ret = gst_flups_demux_send_event (demux, event);
  }
  return ret;
}

static gboolean
gst_flups_demux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstFluPSDemux *demux;

  demux = GST_FLUPS_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_flups_demux_send_event (demux, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_flups_demux_send_event (demux, event);

      gst_segment_init (&demux->sink_segment, GST_FORMAT_UNDEFINED);
      gst_segment_init (&demux->src_segment, GST_FORMAT_TIME);
      gst_adapter_clear (demux->adapter);
      gst_adapter_clear (demux->rev_adapter);
      demux->adapter_offset = G_MAXUINT64;
      gst_pes_filter_drain (&demux->filter);
      demux->current_scr = G_MAXUINT64;
      demux->bytes_since_scr = 0;
      demux->scr_adjust = GSTTIME_TO_MPEGTIME (SCR_MUNGE);
      gst_flups_demux_clear_times (demux);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start, stop, time;
      gint64 accum, dur;
      gdouble arate;
      GstClockTimeDiff adjust;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format != GST_FORMAT_TIME)
        return FALSE;

      dur = stop - start;

      demux->first_scr = GSTTIME_TO_MPEGTIME (start);
      demux->current_scr = demux->first_scr + demux->scr_adjust;
      demux->base_time = time;
      demux->bytes_since_scr = 0;

      gst_segment_set_newsegment_full (&demux->sink_segment, update, rate,
          arate, format, start, stop, time);

      GST_DEBUG_OBJECT (demux,
          "demux: got segment update %d start %" G_GINT64_FORMAT " stop %"
          G_GINT64_FORMAT " time %" G_GINT64_FORMAT, update, start, stop, time);

      accum = demux->sink_segment.accum;
      start = demux->sink_segment.start;
      stop = demux->sink_segment.stop;

      adjust = accum - start + SCR_MUNGE;
      start = accum + SCR_MUNGE;

      if (adjust >= 0)
        demux->scr_adjust = GSTTIME_TO_MPEGTIME (adjust);
      else
        demux->scr_adjust = -GSTTIME_TO_MPEGTIME (-adjust);

      if (stop != -1) {
        stop = start + dur;
        if (demux->src_segment.last_stop != -1
            && demux->src_segment.last_stop > stop)
          stop = demux->src_segment.last_stop;
      }

      GST_DEBUG_OBJECT (demux,
          "sending new segment: update %d rate %g format %d, start: %"
          G_GINT64_FORMAT ", stop: %" G_GINT64_FORMAT ", time: %"
          G_GINT64_FORMAT " scr_adjust: %" G_GINT64_FORMAT "(%" GST_TIME_FORMAT
          ")", update, rate, format, start, stop, time, demux->scr_adjust,
          GST_TIME_ARGS (MPEGTIME_TO_GSTTIME (demux->scr_adjust)));

      gst_segment_set_newsegment_full (&demux->src_segment, update,
          rate, arate, format, start, stop, time);

      gst_event_unref (event);
      if (update) {
        /* Segment closing, send it as per-pad updates to manage the accum
         * properly */
        gst_flups_demux_send_segment_close (demux);
      } else {
        event = gst_event_new_new_segment_full (update,
            rate, arate, GST_FORMAT_TIME, start, stop, time);
        gst_flups_demux_send_event (demux, event);
      }

      break;
    }
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (demux, "Received EOS");
      if (!gst_flups_demux_send_event (demux, event)) {
        GST_WARNING_OBJECT (demux, "failed pushing EOS on streams");
        GST_ELEMENT_ERROR (demux, STREAM, FAILED,
            ("Internal data stream error."), ("Can't push EOS downstream"));
      }
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      const GstStructure *structure = gst_event_get_structure (event);

      if (structure != NULL
          && gst_structure_has_name (structure, "application/x-gst-dvd")) {
        res = gst_flups_demux_handle_dvd_event (demux, event);
      } else {
        gst_flups_demux_send_event (demux, event);
      }
      break;
    }
    default:
      gst_flups_demux_send_event (demux, event);
      break;
  }

  gst_object_unref (demux);

  return res;
}

static gboolean
gst_flups_demux_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstFluPSDemux *demux;

  demux = GST_FLUPS_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      gint64 bstart, bstop;
      GstEvent *bevent;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      GST_DEBUG_OBJECT (demux, "seek event, rate: %f start: %" GST_TIME_FORMAT
          " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
          GST_TIME_ARGS (stop));

      if (format == GST_FORMAT_BYTES) {
        GST_DEBUG_OBJECT (demux, "seek not supported on format %d", format);
        goto not_supported;
      }

      GST_DEBUG_OBJECT (demux, "seek - trying directly upstream first");

      /* first try original format seek */
      (void) gst_event_ref (event);
      if ((res = gst_pad_push_event (demux->sinkpad, event)))
        goto done;

      if (format != GST_FORMAT_TIME) {
        /* From here down, we only support time based seeks */
        GST_DEBUG_OBJECT (demux, "seek not supported on format %d", format);
        goto not_supported;
      }

      /* We need to convert to byte based seek and we need a scr_rate for that. */
      if (demux->scr_rate_n == G_MAXUINT64 || demux->scr_rate_d == G_MAXUINT64) {
        GST_DEBUG_OBJECT (demux, "seek not possible, no scr_rate");
        goto not_supported;
      }

      GST_DEBUG_OBJECT (demux, "try with scr_rate interpolation");

      bstart = GSTTIME_TO_BYTES (start);
      bstop = GSTTIME_TO_BYTES (stop);

      GST_DEBUG_OBJECT (demux, "in bytes bstart %" G_GINT64_FORMAT " bstop %"
          G_GINT64_FORMAT, bstart, bstop);
      bevent = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, start_type,
          bstart, stop_type, bstop);

      res = gst_pad_push_event (demux->sinkpad, bevent);

    done:
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_push_event (demux->sinkpad, event);
      break;
  }

  gst_object_unref (demux);

  return res;

not_supported:
  {
    gst_object_unref (demux);
    gst_event_unref (event);

    return FALSE;
  }
}

static gboolean
gst_flups_demux_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstFluPSDemux *demux;

  demux = GST_FLUPS_DEMUX (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (demux, "Have query of type %d on pad %" GST_PTR_FORMAT,
      GST_QUERY_TYPE (query), pad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstPad *peer;
      GstFormat format;
      gint64 position;

      gst_query_parse_position (query, &format, NULL);

      if ((peer = gst_pad_get_peer (demux->sinkpad)) != NULL) {
        res = gst_pad_query (peer, query);
        gst_object_unref (peer);
        if (res)
          break;
      }

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (demux, "position not supported for format %d",
            format);
        goto not_supported;
      }

      position = demux->base_time;
      if (demux->current_scr != G_MAXUINT64 && demux->first_scr != G_MAXUINT64) {
        position +=
            MPEGTIME_TO_GSTTIME (demux->current_scr - demux->scr_adjust -
            demux->first_scr);
      }

      GST_LOG_OBJECT (demux, "Position at GStreamer Time:%" GST_TIME_FORMAT,
          GST_TIME_ARGS (position));

      gst_query_set_position (query, format, position);
      res = TRUE;
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 duration;
      GstPad *peer;

      gst_query_parse_duration (query, &format, NULL);

      if ((peer = gst_pad_get_peer (demux->sinkpad)) == NULL) {
        GST_DEBUG_OBJECT (demux, "duration not possible, no peer");
        goto not_supported;
      }

      /* For any format other than bytes, see if upstream knows first */
      if (format == GST_FORMAT_BYTES) {
        GST_DEBUG_OBJECT (demux, "duration not supported for format %d",
            format);
        gst_object_unref (peer);
        goto not_supported;
      }

      if (gst_pad_query (peer, query)) {
        gst_object_unref (peer);
        res = TRUE;
        break;
      }

      /* Upstream didn't know, so we can only answer TIME queries from 
       * here on */
      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (demux, "duration not supported for format %d",
            format);
        gst_object_unref (peer);
        goto not_supported;
      }

      if (demux->mux_rate == -1) {
        GST_DEBUG_OBJECT (demux, "duration not possible, no mux_rate");
        gst_object_unref (peer);
        goto not_supported;
      }

      gst_query_set_duration (query, GST_FORMAT_BYTES, -1);

      if (!gst_pad_query (peer, query)) {
        GST_LOG_OBJECT (demux, "query on peer pad failed");
        gst_object_unref (peer);
        goto not_supported;
      }
      gst_object_unref (peer);

      gst_query_parse_duration (query, &format, &duration);

      duration = BYTES_TO_GSTTIME (duration);

      gst_query_set_duration (query, GST_FORMAT_TIME, duration);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (demux);

  return res;

not_supported:
  {
    gst_object_unref (demux);

    return FALSE;
  }
}

static void
gst_flups_demux_reset_psm (GstFluPSDemux * demux)
{
  gint i;

#define FILL_TYPE(start, stop, type)	\
  for (i=start; i <= stop; i++)			\
    demux->psm[i] = type;

  FILL_TYPE (0x00, 0x1f, -1);
  FILL_TYPE (0x20, 0x3f, ST_PS_DVD_SUBPICTURE);
  FILL_TYPE (0x40, 0x7f, -1);
  FILL_TYPE (0x80, 0x87, ST_PS_AUDIO_AC3);
  FILL_TYPE (0x88, 0x9f, ST_PS_AUDIO_DTS);
  FILL_TYPE (0xa0, 0xaf, ST_PS_AUDIO_LPCM);
  FILL_TYPE (0xbd, 0xbd, -1);
  FILL_TYPE (0xc0, 0xdf, ST_AUDIO_MPEG1);
  FILL_TYPE (0xe0, 0xef, ST_GST_VIDEO_MPEG1_OR_2);
  FILL_TYPE (0xf0, 0xff, -1);

#undef FILL_TYPE
}

static GstFlowReturn
gst_flups_demux_parse_pack_start (GstFluPSDemux * demux)
{
  const guint8 *data;
  guint length;
  guint32 scr1, scr2;
  guint64 scr, scr_adjusted, new_rate;
  GstClockTime new_time;

  GST_DEBUG ("parsing pack start");

  /* fixed length to begin with, start code and two scr values */
  length = 8 + 4;

  if (!(data = gst_adapter_peek (demux->adapter, length)))
    goto need_more_data;

  /* skip start code */
  data += 4;

  scr1 = GST_READ_UINT32_BE (data);
  scr2 = GST_READ_UINT32_BE (data + 4);

  /* start parsing the stream */
  if ((*data & 0xc0) == 0x40) {
    guint32 scr_ext;
    guint32 next32;
    guint8 stuffing_bytes;

    GST_DEBUG ("Found MPEG2 stream");
    demux->is_mpeg2_pack = TRUE;

    /* mpeg2 has more data */
    length += 2;
    if (gst_adapter_available (demux->adapter) < length)
      goto need_more_data;

    /* :2=01 ! scr:3 ! marker:1==1 ! scr:15 ! marker:1==1 ! scr:15 */

    /* check markers */
    if ((scr1 & 0xc4000400) != 0x44000400)
      goto lost_sync;

    scr = ((guint64) scr1 & 0x38000000) << 3;
    scr |= ((guint64) scr1 & 0x03fff800) << 4;
    scr |= ((guint64) scr1 & 0x000003ff) << 5;
    scr |= ((guint64) scr2 & 0xf8000000) >> 27;

    /* marker:1==1 ! scr_ext:9 ! marker:1==1 */
    if ((scr2 & 0x04010000) != 0x04010000)
      goto lost_sync;

    scr_ext = (scr2 & 0x03fe0000) >> 17;
    /* We keep the offset of this scr */
    demux->last_scr_offset = demux->adapter_offset + 12;

    GST_DEBUG_OBJECT (demux, "SCR: 0x%08x SCRE: 0x%08x", (guint) scr, scr_ext);

    if (scr_ext) {
      scr = (scr * 300 + scr_ext % 300) / 300;
    }
    /* SCR has been converted into units of 90Khz ticks to make it comparable
       to DTS/PTS, that also implies 1 tick rounding error */
    data += 6;
    /* PMR:22 ! :2==11 ! reserved:5 ! stuffing_len:3 */
    next32 = GST_READ_UINT32_BE (data);
    if ((next32 & 0x00000300) != 0x00000300)
      goto lost_sync;

    new_rate = (next32 & 0xfffffc00) >> 10;

    stuffing_bytes = (next32 & 0x07);
    GST_DEBUG_OBJECT (demux, "stuffing bytes: %d", stuffing_bytes);

    data += 4;
    while (stuffing_bytes--) {
      if (*data++ != 0xff)
        goto lost_sync;
    }
  } else {
    GST_DEBUG ("Found MPEG1 stream");
    demux->is_mpeg2_pack = FALSE;

    /* check markers */
    if ((scr1 & 0xf1000100) != 0x21000100)
      goto lost_sync;

    if ((scr2 & 0x01800001) != 0x01800001)
      goto lost_sync;

    /* :4=0010 ! scr:3 ! marker:1==1 ! scr:15 ! marker:1==1 ! scr:15 ! marker:1==1 */
    scr = ((guint64) scr1 & 0x0e000000) << 5;
    scr |= ((guint64) scr1 & 0x00fffe00) << 6;
    scr |= ((guint64) scr1 & 0x000000ff) << 7;
    scr |= ((guint64) scr2 & 0xfe000000) >> 25;

    /* We keep the offset of this scr */
    demux->last_scr_offset = demux->adapter_offset + 8;

    /* marker:1==1 ! mux_rate:22 ! marker:1==1 */
    new_rate = (scr2 & 0x007ffffe) >> 1;

    data += 8;
  }
  new_rate *= MPEG_MUX_RATE_MULT;

  /* scr adjusted is the new scr found + the colected adjustment */
  scr_adjusted = scr + demux->scr_adjust;

  /* keep the first src in order to calculate delta time */
  if (demux->first_scr == G_MAXUINT64) {
    demux->first_scr = scr;
    demux->first_scr_offset = demux->last_scr_offset;

    if (demux->sink_segment.format == GST_FORMAT_TIME) {
      demux->base_time = demux->sink_segment.time;
    } else {
      demux->base_time = MPEGTIME_TO_GSTTIME (demux->first_scr);
    }
    /* at begin consider the new_rate as the scr rate, bytes/clock ticks */
    demux->scr_rate_n = new_rate;
    demux->scr_rate_d = CLOCK_FREQ;
  } else if (demux->first_scr_offset != demux->last_scr_offset) {
    /* estimate byte rate related to the SCR */
    demux->scr_rate_n = demux->last_scr_offset - demux->first_scr_offset;
    demux->scr_rate_d = scr - demux->first_scr;
  }

  GST_DEBUG_OBJECT (demux,
      "SCR: %" G_GINT64_FORMAT " (%" G_GINT64_FORMAT "), mux_rate %"
      G_GINT64_FORMAT ", GStreamer Time:%" GST_TIME_FORMAT,
      scr, scr_adjusted, new_rate,
      GST_TIME_ARGS (MPEGTIME_TO_GSTTIME ((guint64) scr - demux->first_scr)));

  GST_DEBUG_OBJECT (demux, "%s mode scr: %" G_GUINT64_FORMAT " at %"
      G_GUINT64_FORMAT ", first scr: %" G_GUINT64_FORMAT
      " at %" G_GUINT64_FORMAT ", scr rate: %" G_GUINT64_FORMAT
      "/%" G_GUINT64_FORMAT "(%f)",
      ((demux->sink_segment.rate >= 0.0) ? "forward" : "backward"),
      scr, demux->last_scr_offset,
      demux->first_scr, demux->first_scr_offset,
      demux->scr_rate_n, demux->scr_rate_d,
      (float) demux->scr_rate_n / demux->scr_rate_d);

  /* adjustment of the SCR */
  if (demux->current_scr != G_MAXUINT64) {
    gint64 diff;
    guint64 old_scr, old_mux_rate, bss, adjust;

    /* keep SCR of the previous packet */
    old_scr = demux->current_scr;
    old_mux_rate = demux->mux_rate;

    /* Bytes since SCR is the amount we placed in the adapter since then
     * (demux->bytes_since_scr) minus the amount remaining in the adapter,
     * clamped to >= 0 */
    bss = MAX (0, (gint) (demux->bytes_since_scr -
            gst_adapter_available (demux->adapter)));

    /* estimate the new SCR using the previous one according the notes
       on point 2.5.2.2 of the ISO/IEC 13818-1 document */
    adjust = (bss * CLOCK_FREQ) / old_mux_rate;
    if (demux->sink_segment.rate >= 0.0)
      demux->next_scr = old_scr + adjust;
    else
      demux->next_scr = old_scr - adjust;

    GST_DEBUG_OBJECT (demux,
        "bss: %" G_GUINT64_FORMAT ", next_scr: %" G_GUINT64_FORMAT
        ", old_scr: %" G_GUINT64_FORMAT ", scr: %" G_GUINT64_FORMAT,
        bss, demux->next_scr, old_scr, scr_adjusted);

    /* calculate the absolute deference between the last scr and
       the new one */
    if (old_scr > scr_adjusted)
      diff = old_scr - scr_adjusted;
    else
      diff = scr_adjusted - old_scr;

    /* if the difference is more than 1 second we need to reconfigure
       adjustment */
    if (diff > CLOCK_FREQ) {
#if 0
      demux->scr_adjust = demux->next_scr - scr;
      GST_DEBUG_OBJECT (demux, "discont found, diff: %" G_GINT64_FORMAT
          ", adjust %" G_GINT64_FORMAT, diff, demux->scr_adjust);
      scr_adjusted = demux->next_scr;
#else
      GST_WARNING_OBJECT (demux, "Unexpected SCR diff of %" G_GINT64_FORMAT,
          diff);
#endif
    } else {
      demux->next_scr = scr_adjusted;
    }
  }

  /* update the current_scr and rate members */
  demux->mux_rate = new_rate;
  demux->current_scr = scr_adjusted;

  new_time = MPEGTIME_TO_GSTTIME (scr_adjusted);
  if (new_time != GST_CLOCK_TIME_NONE) {
    // g_print ("SCR now %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (new_time));
    gst_segment_set_last_stop (&demux->src_segment, GST_FORMAT_TIME, new_time);
    gst_flups_demux_send_segment_updates (demux, new_time);
  }

  /* Reset the bytes_since_scr value to count the data remaining in the
   * adapter */
  demux->bytes_since_scr = gst_adapter_available (demux->adapter);

  gst_adapter_flush (demux->adapter, length);
  ADAPTER_OFFSET_FLUSH (length);
  return GST_FLOW_OK;

lost_sync:
  {
    GST_DEBUG_OBJECT (demux, "lost sync");
    return GST_FLOW_LOST_SYNC;
  }
need_more_data:
  {
    GST_DEBUG_OBJECT (demux, "need more data");
    return GST_FLOW_NEED_MORE_DATA;
  }
}

static GstFlowReturn
gst_flups_demux_parse_sys_head (GstFluPSDemux * demux)
{
  guint16 length;
  const guint8 *data;
  gboolean csps;

  /* start code + length */
  if (!(data = gst_adapter_peek (demux->adapter, 6)))
    goto need_more_data;

  /* skip start code */
  data += 4;

  length = GST_READ_UINT16_BE (data);
  GST_DEBUG_OBJECT (demux, "length %d", length);

  length += 6;

  if (!(data = gst_adapter_peek (demux->adapter, length)))
    goto need_more_data;

  /* skip start code and length */
  data += 6;

  /* marker:1==1 ! rate_bound:22 | marker:1==1 */
  if ((*data & 0x80) != 0x80)
    goto marker_expected;

  {
    guint32 rate_bound;

    if ((data[2] & 0x01) != 0x01)
      goto marker_expected;

    rate_bound = ((guint32) data[0] & 0x7f) << 15;
    rate_bound |= ((guint32) data[1]) << 7;
    rate_bound |= ((guint32) data[2] & 0xfe) >> 1;
    rate_bound *= MPEG_MUX_RATE_MULT;

    GST_DEBUG_OBJECT (demux, "rate bound %u", rate_bound);

    data += 3;
  }

  /* audio_bound:6==1 ! fixed:1 | constrained:1 */
  {
    guint8 audio_bound;
    gboolean fixed;

    /* max number of simultaneous audio streams active */
    audio_bound = (data[0] & 0xfc) >> 2;
    /* fixed or variable bitrate */
    fixed = (data[0] & 0x02) == 0x02;
    /* meeting constraints */
    csps = (data[0] & 0x01) == 0x01;

    GST_DEBUG_OBJECT (demux, "audio_bound %d, fixed %d, constrained %d",
        audio_bound, fixed, csps);
    data += 1;
  }

  /* audio_lock:1 | video_lock:1 | marker:1==1 | video_bound:5 */
  {
    gboolean audio_lock;
    gboolean video_lock;
    guint8 video_bound;

    audio_lock = (data[0] & 0x80) == 0x80;
    video_lock = (data[0] & 0x40) == 0x40;

    if ((data[0] & 0x20) != 0x20)
      goto marker_expected;

    /* max number of simultaneous video streams active */
    video_bound = (data[0] & 0x1f);

    GST_DEBUG_OBJECT (demux, "audio_lock %d, video_lock %d, video_bound %d",
        audio_lock, video_lock, video_bound);
    data += 1;
  }

  /* packet_rate_restriction:1 | reserved:7==0x7F */
  {
    gboolean packet_rate_restriction;

    if ((data[0] & 0x7f) != 0x7f)
      goto marker_expected;

    /* only valid if csps is set */
    if (csps) {
      packet_rate_restriction = (data[0] & 0x80) == 0x80;

      GST_DEBUG_OBJECT (demux, "packet_rate_restriction %d",
          packet_rate_restriction);
    }
  }
  data += 1;

  {
    gint stream_count = (length - 12) / 3;
    gint i;

    GST_DEBUG_OBJECT (demux, "number of streams: %d ", stream_count);

    for (i = 0; i < stream_count; i++) {
      guint8 stream_id;
      gboolean STD_buffer_bound_scale;
      guint16 STD_buffer_size_bound;
      guint32 buf_byte_size_bound;

      stream_id = *data++;
      if (!(stream_id & 0x80))
        goto sys_len_error;

      /* check marker bits */
      if ((*data & 0xC0) != 0xC0)
        goto no_placeholder_bits;

      STD_buffer_bound_scale = *data & 0x20;
      STD_buffer_size_bound = ((guint16) (*data++ & 0x1F)) << 8;
      STD_buffer_size_bound |= *data++;

      if (STD_buffer_bound_scale == 0) {
        buf_byte_size_bound = STD_buffer_size_bound * 128;
      } else {
        buf_byte_size_bound = STD_buffer_size_bound * 1024;
      }

      GST_DEBUG_OBJECT (demux, "STD_buffer_bound_scale %d",
          STD_buffer_bound_scale);
      GST_DEBUG_OBJECT (demux, "STD_buffer_size_bound %d or %d bytes",
          STD_buffer_size_bound, buf_byte_size_bound);
    }
  }

  gst_adapter_flush (demux->adapter, length);
  ADAPTER_OFFSET_FLUSH (length);
  return GST_FLOW_OK;

  /* ERRORS */
marker_expected:
  {
    GST_DEBUG_OBJECT (demux, "expecting marker");
    return GST_FLOW_LOST_SYNC;
  }
no_placeholder_bits:
  {
    GST_DEBUG_OBJECT (demux, "expecting placeholder bit values"
        " '11' after stream id");
    return GST_FLOW_LOST_SYNC;
  }
sys_len_error:
  {
    GST_DEBUG_OBJECT (demux, "error in system header length");
    return GST_FLOW_LOST_SYNC;
  }
need_more_data:
  {
    GST_DEBUG_OBJECT (demux, "need more data");
    return GST_FLOW_NEED_MORE_DATA;
  }
}

static GstFlowReturn
gst_flups_demux_parse_psm (GstFluPSDemux * demux)
{
  guint16 length = 0, info_length = 0, es_map_length = 0;
  guint8 psm_version = 0;
  const guint8 *data, *es_map_base;
  gboolean applicable;

  /* start code + length */
  if (!(data = gst_adapter_peek (demux->adapter, 6)))
    goto need_more_data;

  /* skip start code */
  data += 4;

  length = GST_READ_UINT16_BE (data);
  GST_DEBUG_OBJECT (demux, "length %u", length);

  if (G_UNLIKELY (length > 0x3FA))
    goto psm_len_error;

  length += 6;

  if (!(data = gst_adapter_peek (demux->adapter, length)))
    goto need_more_data;

  /* skip start code and length */
  data += 6;

  /* Read PSM applicable bit together with version */
  psm_version = GST_READ_UINT8 (data);
  applicable = (psm_version & 0x80) >> 7;
  psm_version &= 0x1F;
  GST_DEBUG_OBJECT (demux, "PSM version %u (applicable now %u)", psm_version,
      applicable);

  /* Jump over version and marker bit */
  data += 2;

  /* Read PS info length */
  info_length = GST_READ_UINT16_BE (data);
  /* Cap it to PSM length - needed bytes for ES map length and CRC */
  info_length = MIN (length - 16, info_length);
  GST_DEBUG_OBJECT (demux, "PS info length %u bytes", info_length);

  /* Jump over that section */
  data += (2 + info_length);

  /* Read ES map length */
  es_map_length = GST_READ_UINT16_BE (data);
  /* Cap it to PSM remaining length -  CRC */
  es_map_length = MIN (length - (16 + info_length), es_map_length);
  GST_DEBUG_OBJECT (demux, "ES map length %u bytes", es_map_length);

  /* Jump over the size */
  data += 2;

  /* Now read the ES map */
  es_map_base = data;
  while (es_map_base + 4 <= data + es_map_length) {
    guint8 stream_type = 0, stream_id = 0;
    guint16 stream_info_length = 0;

    stream_type = GST_READ_UINT8 (es_map_base);
    es_map_base++;
    stream_id = GST_READ_UINT8 (es_map_base);
    es_map_base++;
    stream_info_length = GST_READ_UINT16_BE (es_map_base);
    es_map_base += 2;
    /* Cap stream_info_length */
    stream_info_length = MIN (data + es_map_length - es_map_base,
        stream_info_length);

    GST_DEBUG_OBJECT (demux, "Stream type %02X with id %02X and %u bytes info",
        stream_type, stream_id, stream_info_length);
    demux->psm[stream_id] = stream_type;
    es_map_base += stream_info_length;
  }

  gst_adapter_flush (demux->adapter, length);
  ADAPTER_OFFSET_FLUSH (length);
  return GST_FLOW_OK;

psm_len_error:
  {
    GST_DEBUG_OBJECT (demux, "error in PSM length");
    return GST_FLOW_LOST_SYNC;
  }
need_more_data:
  {
    GST_DEBUG_OBJECT (demux, "need more data");
    return GST_FLOW_NEED_MORE_DATA;
  }
}

static void
gst_flups_demux_resync_cb (GstPESFilter * filter, GstFluPSDemux * demux)
{
}

static GstFlowReturn
gst_flups_demux_data_cb (GstPESFilter * filter, gboolean first,
    GstBuffer * buffer, GstFluPSDemux * demux)
{
  GstBuffer *out_buf;
  GstFlowReturn ret = GST_FLOW_OK;
  gint stream_type;
  guint32 start_code;
  guint8 id;
  guint8 *data;
  guint datalen;
  guint offset = 0;

  data = GST_BUFFER_DATA (buffer);
  datalen = GST_BUFFER_SIZE (buffer);

  start_code = filter->start_code;
  id = filter->id;

  if (first) {
    /* find the stream type */
    stream_type = demux->psm[id];
    if (stream_type == -1) {
      /* no stream type, if PS1, get the new id */
      if (start_code == ID_PRIVATE_STREAM_1 && datalen >= 2) {
        guint8 nframes;

        /* VDR writes A52 streams without any header bytes 
         * (see ftp://ftp.mplayerhq.hu/MPlayer/samples/MPEG-VOB/vdr-AC3) */
        if (datalen >= 4) {
          guint hdr = GST_READ_UINT32_BE (data);

          if (G_UNLIKELY ((hdr & 0xffff0000) == AC3_SYNC_WORD)) {
            id = 0x80;
            stream_type = demux->psm[id] = ST_GST_AUDIO_RAWA52;
            GST_DEBUG_OBJECT (demux, "Found VDR raw A52 stream");
          }
        }

        if (G_LIKELY (stream_type == -1)) {
          /* new id is in the first byte */
          id = data[offset++];
          datalen--;

          /* and remap */
          stream_type = demux->psm[id];

          /* Now, if it's a subpicture stream - no more, otherwise
           * take the first byte too, since it's the frame count in audio
           * streams and our backwards compat convention is to strip it off */
          if (stream_type != ST_PS_DVD_SUBPICTURE) {
            /* Number of audio frames in this packet */
            nframes = data[offset++];
            datalen--;
            GST_DEBUG_OBJECT (demux, "private type 0x%02x, %d frames", id,
                nframes);
          } else {
            GST_DEBUG_OBJECT (demux, "private type 0x%02x, stream type %d", id,
                stream_type);
          }
        }
      }
      if (stream_type == -1)
        goto unknown_stream_type;
    }
    if (filter->pts != -1) {
      demux->next_pts = filter->pts + demux->scr_adjust;
      GST_DEBUG_OBJECT (demux, "PTS = %" G_GUINT64_FORMAT
          "(%" G_GUINT64_FORMAT ")", filter->pts, demux->next_pts);
    } else
      demux->next_pts = G_MAXUINT64;

    if (filter->dts != -1) {
      demux->next_dts = filter->dts + demux->scr_adjust;
    } else {
      demux->next_dts = demux->next_pts;
    }
    GST_DEBUG_OBJECT (demux, "DTS = orig %" G_GUINT64_FORMAT
        " (%" G_GUINT64_FORMAT ")", filter->dts, demux->next_dts);

    demux->current_stream = gst_flups_demux_get_stream (demux, id, stream_type);
  }

  if (demux->current_stream == NULL) {
    GST_DEBUG_OBJECT (demux, "Dropping buffer for unknown stream id 0x%02x",
        id);
    goto done;
  }

  /* After 2 seconds of bitstream emit no more pads */
  if (demux->need_no_more_pads
      && (demux->current_scr - demux->first_scr - demux->scr_adjust) >
      2 * CLOCK_FREQ) {
    GST_DEBUG_OBJECT (demux, "no more pads, notifying");
    gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
    demux->need_no_more_pads = FALSE;
  }

  /* If the stream is not-linked, don't bother creating a sub-buffer
   * to send to it, unless we're processing a discont (which resets
   * the not-linked status and tries again */
  if (demux->current_stream->discont) {
    GST_DEBUG_OBJECT (demux, "stream is discont");
    demux->current_stream->notlinked = FALSE;
  }

  if (demux->current_stream->notlinked == FALSE) {
    out_buf = gst_buffer_create_sub (buffer, offset, datalen);

    ret = gst_flups_demux_send_data (demux, demux->current_stream, out_buf);
    if (ret == GST_FLOW_NOT_LINKED) {
      demux->current_stream->notlinked = TRUE;
      ret = GST_FLOW_OK;
    }
  }

done:
  gst_buffer_unref (buffer);

  return ret;

  /* ERRORS */
unknown_stream_type:
  {
    GST_DEBUG_OBJECT (demux, "unknown stream type %02x", id);
    ret = GST_FLOW_OK;
    goto done;
  }
}

static gboolean
gst_flups_demux_resync (GstFluPSDemux * demux, gboolean save)
{
  const guint8 *data;
  gint avail;
  guint32 code;
  gint offset;
  gboolean found;

  avail = gst_adapter_available (demux->adapter);
  if (avail < 4)
    goto need_data;

  /* Common case, read 4 bytes an check it */
  data = gst_adapter_peek (demux->adapter, 4);

  /* read currect code */
  code = GST_READ_UINT32_BE (data);

  /* The common case is that the sync code is at 0 bytes offset */
  if (G_LIKELY ((code & 0xffffff00) == 0x100L)) {
    GST_LOG_OBJECT (demux, "Found resync code %08x after 0 bytes", code);
    demux->last_sync_code = code;
    return TRUE;
  }

  /* Otherwise, we are starting at byte 4 and we need to search 
     the sync code in all available data in the adapter */
  offset = 4;
  if (offset >= avail)
    goto need_data;             /* Not enough data to find sync */

  data = gst_adapter_peek (demux->adapter, avail);

  do {
    code = (code << 8) | data[offset++];
    found = (code & 0xffffff00) == 0x100L;
  } while (offset < avail && !found);

  if (!save || demux->sink_segment.rate >= 0.0) {
    GST_LOG_OBJECT (demux, "flushing %d bytes", offset - 4);
    /* forward playback, we can discard and flush the skipped bytes */
    gst_adapter_flush (demux->adapter, offset - 4);
    ADAPTER_OFFSET_FLUSH (offset - 4);
  } else {
    if (found) {
      GST_LOG_OBJECT (demux, "reverse saving %d bytes", offset - 4);
      /* reverse playback, we keep the flushed bytes and we will append them to
       * the next buffer in the chain function, which is the previous buffer in
       * the stream. */
      gst_adapter_push (demux->rev_adapter,
          gst_adapter_take_buffer (demux->adapter, offset - 4));
    } else {
      GST_LOG_OBJECT (demux, "reverse saving %d bytes", avail);
      /* nothing found, keep all bytes */
      gst_adapter_push (demux->rev_adapter,
          gst_adapter_take_buffer (demux->adapter, avail));
    }
  }

  if (found) {
    GST_LOG_OBJECT (demux, "Found resync code %08x after %d bytes",
        code, offset - 4);
    demux->last_sync_code = code;
  } else {
    GST_LOG_OBJECT (demux, "No resync after skipping %d", offset);
  }

  return found;

need_data:
  {
    GST_LOG_OBJECT (demux, "we need more data for resync %d", avail);
    return FALSE;
  }
}

static inline gboolean
gst_flups_demux_is_pes_sync (guint32 sync)
{
  return ((sync & 0xfc) == 0xbc) ||
      ((sync & 0xe0) == 0xc0) || ((sync & 0xf0) == 0xe0);
}

static GstFlowReturn
gst_flups_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFluPSDemux *demux = GST_FLUPS_DEMUX (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 avail;
  gboolean save, discont;

  discont = GST_BUFFER_IS_DISCONT (buffer);

  if (discont) {
    GST_LOG_OBJECT (demux, "Received buffer with discont flag and"
        " offset %" G_GUINT64_FORMAT, GST_BUFFER_OFFSET (buffer));

    gst_pes_filter_drain (&demux->filter);
    gst_flups_demux_mark_discont (demux);

    /* mark discont on all streams */
    if (demux->sink_segment.rate >= 0.0) {
      demux->current_scr = G_MAXUINT64;
      demux->bytes_since_scr = 0;
    }
  } else {
    GST_LOG_OBJECT (demux, "Received buffer with offset %" G_GUINT64_FORMAT,
        GST_BUFFER_OFFSET (buffer));
  }

  /* We keep the offset to interpolate SCR */
  demux->adapter_offset = GST_BUFFER_OFFSET (buffer);

  gst_adapter_push (demux->adapter, buffer);
  demux->bytes_since_scr += GST_BUFFER_SIZE (buffer);

  avail = gst_adapter_available (demux->rev_adapter);
  if (avail > 0) {
    GST_LOG_OBJECT (demux, "appending %u saved bytes", avail);
    /* if we have a previous reverse chunk, append this now */
    /* FIXME this code assumes we receive discont buffers all thei
     * time */
    gst_adapter_push (demux->adapter,
        gst_adapter_take_buffer (demux->rev_adapter, avail));
  }

  avail = gst_adapter_available (demux->adapter);
  GST_LOG_OBJECT (demux, "avail now: %d, state %d", avail, demux->filter.state);

  switch (demux->filter.state) {
    case STATE_DATA_SKIP:
    case STATE_DATA_PUSH:
      ret = gst_pes_filter_process (&demux->filter);
      break;
    case STATE_HEADER_PARSE:
      break;
    default:
      break;
  }

  switch (ret) {
    case GST_FLOW_NEED_MORE_DATA:
      /* Go and get more data */
      ret = GST_FLOW_OK;
      goto done;
    case GST_FLOW_LOST_SYNC:
      /* for FLOW_OK or lost-sync, carry onto resync */
      ret = GST_FLOW_OK;
      break;
    case GST_FLOW_OK:
      break;
    default:
      /* Any other return value should be sent upstream immediately */
      goto done;
  }

  /* align adapter data to sync boundary, we keep the data up to the next sync
   * point. */
  save = TRUE;
  while (gst_flups_demux_resync (demux, save)) {
    gboolean ps_sync = TRUE;

    /* now switch on last synced byte */
    switch (demux->last_sync_code) {
      case ID_PS_PACK_START_CODE:
        ret = gst_flups_demux_parse_pack_start (demux);
        break;
      case ID_PS_SYSTEM_HEADER_START_CODE:
        ret = gst_flups_demux_parse_sys_head (demux);
        break;
      case ID_PS_END_CODE:
        ret = GST_FLOW_OK;
        goto done;
      case ID_PS_PROGRAM_STREAM_MAP:
        ret = gst_flups_demux_parse_psm (demux);
        break;
      default:
        if (gst_flups_demux_is_pes_sync (demux->last_sync_code)) {
          ret = gst_pes_filter_process (&demux->filter);
        } else {
          GST_DEBUG_OBJECT (demux, "sync_code=%08x, non PES sync found"
              ", continuing", demux->last_sync_code);
          ps_sync = FALSE;
          ret = GST_FLOW_LOST_SYNC;
        }
        break;
    }
    /* if we found a ps sync, we stop saving the data, any non-ps sync gets
     * saved up to the next ps sync. */
    if (ps_sync)
      save = FALSE;

    switch (ret) {
      case GST_FLOW_NEED_MORE_DATA:
        GST_DEBUG_OBJECT (demux, "need more data");
        ret = GST_FLOW_OK;
        goto done;
      case GST_FLOW_LOST_SYNC:
        if (!save || demux->sink_segment.rate >= 0.0) {
          GST_DEBUG_OBJECT (demux, "flushing 3 bytes");
          gst_adapter_flush (demux->adapter, 3);
          ADAPTER_OFFSET_FLUSH (3);
        } else {
          GST_DEBUG_OBJECT (demux, "saving 3 bytes");
          gst_adapter_push (demux->rev_adapter,
              gst_adapter_take_buffer (demux->adapter, 3));
        }
        ret = GST_FLOW_OK;
        break;
      default:
        break;
    }
  }
done:
  gst_object_unref (demux);

  return ret;
}

static GstStateChangeReturn
gst_flups_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstFluPSDemux *demux = GST_FLUPS_DEMUX (element);
  GstStateChangeReturn result;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      demux->adapter = gst_adapter_new ();
      demux->rev_adapter = gst_adapter_new ();
      demux->adapter_offset = G_MAXUINT64;
      gst_pes_filter_init (&demux->filter, demux->adapter,
          &demux->adapter_offset);
      gst_pes_filter_set_callbacks (&demux->filter,
          (GstPESFilterData) gst_flups_demux_data_cb,
          (GstPESFilterResync) gst_flups_demux_resync_cb, demux);
      demux->filter.gather_pes = TRUE;
      demux->first_scr = G_MAXUINT64;
      demux->bytes_since_scr = 0;
      demux->current_scr = G_MAXUINT64;
      demux->base_time = G_MAXUINT64;
      demux->scr_rate_n = G_MAXUINT64;
      demux->scr_rate_d = G_MAXUINT64;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      demux->current_scr = G_MAXUINT64;
      demux->mux_rate = G_MAXUINT64;
      demux->next_pts = G_MAXUINT64;
      demux->next_dts = G_MAXUINT64;
      demux->first_scr = G_MAXUINT64;
      demux->bytes_since_scr = 0;
      demux->base_time = G_MAXUINT64;
      demux->scr_rate_n = G_MAXUINT64;
      demux->scr_rate_d = G_MAXUINT64;
      demux->need_no_more_pads = TRUE;

      gst_flups_demux_reset_psm (demux);
      gst_segment_init (&demux->sink_segment, GST_FORMAT_UNDEFINED);
      gst_segment_init (&demux->src_segment, GST_FORMAT_TIME);
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_flups_demux_reset (demux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_pes_filter_uninit (&demux->filter);
      g_object_unref (demux->adapter);
      demux->adapter = NULL;
      g_object_unref (demux->rev_adapter);
      demux->rev_adapter = NULL;
      break;
    default:
      break;
  }

  return result;
}

gboolean
gst_flups_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstflupesfilter_debug, "rsnpesfilter", 0,
      "MPEG program stream PES filter debug");

  GST_DEBUG_CATEGORY_INIT (gstflupsdemux_debug, "rsndvddemux", 0,
      "MPEG program stream demuxer debug");

  return TRUE;
}
