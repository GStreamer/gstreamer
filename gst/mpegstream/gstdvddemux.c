/* GStreamer
 * Copyright (C) 2005 Martin Soto <martinsoto@users.sourceforge.net>
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

#include "gstdvddemux.h"

/* 
 * Move PTM discont back by 0.3 seconds to allow for strange audio
 * timestamps when audio crosses a VOBU 
 */
#define PTM_DISCONT_ADJUST (0.3 * GST_SECOND)
#define INITIAL_END_PTM (-1)
#define MAX_GAP ( 3 * GST_SECOND / 2 )
#define MAX_GAP_TOLERANCE ( GST_SECOND / 20 )

GST_DEBUG_CATEGORY_STATIC (gstdvddemux_debug);
#define GST_CAT_DEFAULT (gstdvddemux_debug)


#define PARSE_CLASS(o)  GST_MPEG_PARSE_CLASS (G_OBJECT_GET_CLASS (o))
#define DEMUX_CLASS(o)  GST_MPEG_DEMUX_CLASS (G_OBJECT_GET_CLASS (o))
#define CLASS(o)  GST_DVD_DEMUX_CLASS (G_OBJECT_GET_CLASS (o))

/* DVDDemux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};


/* Define the capabilities separately, to be able to reuse them. */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 2, " "systemstream = (boolean) TRUE")
    );

#define VIDEO_CAPS \
  GST_STATIC_CAPS ("video/mpeg, " \
    "mpegversion = (int) { 1, 2 }, " \
    "systemstream = (boolean) FALSE" \
  )

#define AUDIO_CAPS \
  GST_STATIC_CAPS ( \
    "audio/mpeg, " \
      "mpegversion = (int) 1;" \
    "audio/x-lpcm, " \
      "width = (int) { 16, 20, 24 }, " \
      "rate = (int) { 48000, 96000 }, " \
      "channels = (int) [ 1, 8 ], " \
      "dynamic_range = (int) [ 0, 255 ], " \
      "emphasis = (boolean) { FALSE, TRUE }, " \
      "mute = (boolean) { FALSE, TRUE }; " \
    "audio/x-ac3;" \
    "audio/x-dts" \
  )

#define SUBPICTURE_CAPS \
  GST_STATIC_CAPS ("video/x-dvd-subpicture")

static GstStaticPadTemplate cur_video_template =
GST_STATIC_PAD_TEMPLATE ("current_video",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    VIDEO_CAPS);

static GstStaticPadTemplate audio_template =
GST_STATIC_PAD_TEMPLATE ("dvd_audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    AUDIO_CAPS);

static GstStaticPadTemplate cur_audio_template =
GST_STATIC_PAD_TEMPLATE ("current_audio",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    AUDIO_CAPS);

static GstStaticPadTemplate subpicture_template =
GST_STATIC_PAD_TEMPLATE ("subpicture_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    SUBPICTURE_CAPS);

static GstStaticPadTemplate cur_subpicture_template =
GST_STATIC_PAD_TEMPLATE ("current_subpicture",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    SUBPICTURE_CAPS);

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gstdvddemux_debug, "dvddemux", 0, \
        "DVD (VOB) demultiplexer element");

GST_BOILERPLATE_FULL (GstDVDDemux, gst_dvd_demux, GstMPEGDemux,
    GST_TYPE_MPEG_DEMUX, _do_init);

static gboolean gst_dvd_demux_process_event (GstMPEGParse * mpeg_parse,
    GstEvent * event);
static gboolean gst_dvd_demux_parse_packhead (GstMPEGParse * mpeg_parse,
    GstBuffer * buffer);

static gboolean gst_dvd_demux_handle_dvd_event
    (GstDVDDemux * dvd_demux, GstEvent * event);

static GstMPEGStream *gst_dvd_demux_get_video_stream
    (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info);
static GstMPEGStream *gst_dvd_demux_get_audio_stream
    (GstMPEGDemux * dvd_demux,
    guint8 stream_nr, gint type, const gpointer info);
static GstMPEGStream *gst_dvd_demux_get_subpicture_stream
    (GstMPEGDemux * dvd_demux,
    guint8 stream_nr, gint type, const gpointer info);

static GstFlowReturn gst_dvd_demux_process_private
    (GstMPEGDemux * mpeg_demux,
    GstBuffer * buffer,
    guint stream_nr, GstClockTime timestamp, guint headerlen, guint datalen);

static GstFlowReturn gst_dvd_demux_send_subbuffer
    (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * outstream,
    GstBuffer * buffer, GstClockTime timestamp, guint offset, guint size);

static GstFlowReturn gst_dvd_demux_combine_flows (GstMPEGDemux * mpegdemux,
    GstMPEGStream * stream, GstFlowReturn flow);

static void gst_dvd_demux_set_cur_audio
    (GstDVDDemux * dvd_demux, gint stream_nr);
static void gst_dvd_demux_set_cur_subpicture
    (GstDVDDemux * dvd_demux, gint stream_nr);

static void gst_dvd_demux_reset (GstDVDDemux * dvd_demux);
static void gst_dvd_demux_synchronise_pads (GstMPEGDemux * mpeg_demux,
    GstClockTime threshold, GstClockTime new_ts);
static void gst_dvd_demux_sync_stream_to_time (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * stream, GstClockTime last_ts);

static GstStateChangeReturn gst_dvd_demux_change_state (GstElement * element,
    GstStateChange transition);

/*static guint gst_dvd_demux_signals[LAST_SIGNAL] = { 0 };*/


static void
gst_dvd_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstMPEGParseClass *mpeg_parse_class = GST_MPEG_PARSE_CLASS (klass);
  GstMPEGDemuxClass *demux_class = GST_MPEG_DEMUX_CLASS (klass);
  GstDVDDemuxClass *dvd_demux_class = GST_DVD_DEMUX_CLASS (klass);

  mpeg_parse_class->send_buffer = NULL;
  mpeg_parse_class->process_event = gst_dvd_demux_process_event;
  mpeg_parse_class->parse_packhead = gst_dvd_demux_parse_packhead;

  /* sink pad */
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);

  demux_class->audio_template = gst_static_pad_template_get (&audio_template);

  dvd_demux_class->cur_video_template =
      gst_static_pad_template_get (&cur_video_template);
  dvd_demux_class->cur_audio_template =
      gst_static_pad_template_get (&cur_audio_template);
  dvd_demux_class->subpicture_template =
      gst_static_pad_template_get (&subpicture_template);
  dvd_demux_class->cur_subpicture_template =
      gst_static_pad_template_get (&cur_subpicture_template);

  gst_element_class_add_pad_template (element_class,
      demux_class->audio_template);

  gst_element_class_add_pad_template (element_class,
      dvd_demux_class->cur_video_template);
  gst_element_class_add_pad_template (element_class,
      dvd_demux_class->cur_audio_template);

  gst_element_class_add_pad_template (element_class,
      dvd_demux_class->subpicture_template);
  gst_element_class_add_pad_template (element_class,
      dvd_demux_class->cur_subpicture_template);

  gst_element_class_set_details_simple (element_class, "DVD Demuxer",
      "Codec/Demuxer",
      "Demultiplexes DVD (VOB) MPEG2 streams",
      "Martin Soto <martinsoto@users.sourceforge.net>");
}


static void
gst_dvd_demux_class_init (GstDVDDemuxClass * klass)
{
  GstElementClass *gstelement_class;
  GstMPEGDemuxClass *mpeg_demux_class;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class = (GstElementClass *) klass;
  mpeg_demux_class = (GstMPEGDemuxClass *) klass;

  gstelement_class->change_state = gst_dvd_demux_change_state;

  mpeg_demux_class->get_audio_stream = gst_dvd_demux_get_audio_stream;
  mpeg_demux_class->get_video_stream = gst_dvd_demux_get_video_stream;
  mpeg_demux_class->send_subbuffer = gst_dvd_demux_send_subbuffer;
  mpeg_demux_class->combine_flows = gst_dvd_demux_combine_flows;
  mpeg_demux_class->process_private = gst_dvd_demux_process_private;
  mpeg_demux_class->synchronise_pads = gst_dvd_demux_synchronise_pads;
  mpeg_demux_class->sync_stream_to_time = gst_dvd_demux_sync_stream_to_time;

  klass->get_subpicture_stream = gst_dvd_demux_get_subpicture_stream;
}


static void
gst_dvd_demux_init (GstDVDDemux * dvd_demux, GstDVDDemuxClass * klass)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);
  gint i;

  /* Create the pads for the current streams. */
  dvd_demux->cur_video =
      DEMUX_CLASS (dvd_demux)->new_output_pad (mpeg_demux, "current_video",
      CLASS (dvd_demux)->cur_video_template);
  gst_element_add_pad (GST_ELEMENT (mpeg_demux), dvd_demux->cur_video);
  dvd_demux->cur_audio =
      DEMUX_CLASS (dvd_demux)->new_output_pad (mpeg_demux, "current_audio",
      CLASS (dvd_demux)->cur_audio_template);
  gst_element_add_pad (GST_ELEMENT (mpeg_demux), dvd_demux->cur_audio);
  dvd_demux->cur_subpicture =
      DEMUX_CLASS (dvd_demux)->new_output_pad (mpeg_demux, "current_subpicture",
      CLASS (dvd_demux)->cur_subpicture_template);
  gst_element_add_pad (GST_ELEMENT (mpeg_demux), dvd_demux->cur_subpicture);

  dvd_demux->mpeg_version = 0;
  dvd_demux->cur_video_nr = 0;
  dvd_demux->cur_audio_nr = 0;
  dvd_demux->cur_subpicture_nr = 0;

  for (i = 0; i < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; i++) {
    dvd_demux->subpicture_stream[i] = NULL;
  }

  /* Directly after starting we operate as if we had just flushed. */
  dvd_demux->segment_filter = TRUE;

  dvd_demux->langcodes = NULL;
}

static gboolean
gst_dvd_demux_process_event (GstMPEGParse * mpeg_parse, GstEvent * event)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_parse);
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;

      gst_event_parse_new_segment (event, &update, NULL, NULL,
          NULL, NULL, NULL);

      if (!update) {
        /* This is a discontinuity in the timestamp sequence. which
           may mean that we find some audio blocks lying outside the
           segment. Filter them. */
        dvd_demux->segment_filter = TRUE;

        /* reset stream synchronization; parent handles other streams */
        gst_mpeg_streams_reset_cur_ts (dvd_demux->subpicture_stream,
            GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS, 0);
      }

      ret = GST_MPEG_PARSE_CLASS (parent_class)->process_event (mpeg_parse,
          event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      dvd_demux->segment_filter = TRUE;
      ret = GST_MPEG_PARSE_CLASS (parent_class)->process_event (mpeg_parse,
          event);

      /* parent class will have reset the other streams */
      gst_mpeg_streams_reset_last_flow (dvd_demux->subpicture_stream,
          GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS);
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
      if (event->structure != NULL &&
          gst_structure_has_name (event->structure, "application/x-gst-dvd")) {
        ret = gst_dvd_demux_handle_dvd_event (dvd_demux, event);
      } else {
        ret = GST_MPEG_PARSE_CLASS (parent_class)->process_event (mpeg_parse,
            event);
      }
      break;
    default:
      ret = GST_MPEG_PARSE_CLASS (parent_class)->process_event (mpeg_parse,
          event);
      break;
  }

  return ret;
}

static gboolean
gst_dvd_demux_handle_dvd_event (GstDVDDemux * dvd_demux, GstEvent * event)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (dvd_demux);
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);
  const GstStructure *structure = gst_event_get_structure (event);
  const char *event_type = gst_structure_get_string (structure, "event");

  g_return_val_if_fail (event != NULL, FALSE);

  GST_LOG_OBJECT (dvd_demux, "dvd event %" GST_PTR_FORMAT, structure);

  if (strcmp (event_type, "dvd-audio-stream-change") == 0) {
    gint stream_nr;

    gst_structure_get_int (structure, "physical", &stream_nr);
    if (stream_nr < -1 || stream_nr >= GST_MPEG_DEMUX_NUM_AUDIO_STREAMS) {
      GST_ERROR_OBJECT (dvd_demux,
          "GstDVDDemux: Invalid audio stream %02d", stream_nr);
      return FALSE;
    }
    gst_dvd_demux_set_cur_audio (dvd_demux, stream_nr);
    gst_event_unref (event);
  } else if (strcmp (event_type, "dvd-spu-stream-change") == 0) {
    gint stream_nr;

    gst_structure_get_int (structure, "physical", &stream_nr);
    if (stream_nr < -1 || stream_nr >= GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS) {
      GST_ERROR_OBJECT (dvd_demux,
          "GstDVDDemux: Invalid subpicture stream %02d", stream_nr);
      return FALSE;
    }
    gst_dvd_demux_set_cur_subpicture (dvd_demux, stream_nr);
    gst_event_unref (event);
  } else if (!strcmp (event_type, "dvd-lang-codes")) {
    gint num_substreams = 0, num_audstreams = 0, n;
    gchar *t;

    /* reset */
    if (dvd_demux->langcodes)
      gst_event_unref (dvd_demux->langcodes);

    /* see what kind of streams we have */
    dvd_demux->langcodes = event;

    /* now create pads for each; first video */
    n = 2;
    DEMUX_CLASS (dvd_demux)->get_video_stream (mpeg_demux,
        0, GST_MPEG_DEMUX_VIDEO_MPEG, &n);

    /* audio */
    for (n = 0;; n++) {
      gint fmt, ifo = 0;

      t = g_strdup_printf ("audio-%d-format", num_audstreams);
      if (!gst_structure_get_int (structure, t, &fmt)) {
        g_free (t);
        break;
      }
      g_free (t);
      switch (fmt) {
        case 0x0:              /* AC-3 */
          fmt = GST_DVD_DEMUX_AUDIO_AC3;
          break;
        case 0x2:
        case 0x3:              /* MPEG */
          fmt = GST_MPEG_DEMUX_AUDIO_MPEG;
          break;
        case 0x4:
          fmt = GST_DVD_DEMUX_AUDIO_LPCM;
          break;
        case 0x6:
          fmt = GST_DVD_DEMUX_AUDIO_DTS;
          break;
        default:
          fmt = GST_MPEG_DEMUX_AUDIO_UNKNOWN;
          break;
      }
      DEMUX_CLASS (dvd_demux)->get_audio_stream (mpeg_demux,
          num_audstreams++, fmt, &ifo);
    }

    /* subtitle */
    for (n = 0; n < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; n++) {
      t = g_strdup_printf ("subtitle-%d-language", n);
      if (!gst_structure_get_value (structure, t)) {
        g_free (t);
        continue;
      }
      g_free (t);
      num_substreams = n + 1;
    }

    /* now we have a maximum,
     * and can also fill empty slots in case of cranky DVD */
    for (n = 0; n < num_substreams; n++)
      CLASS (dvd_demux)->get_subpicture_stream (mpeg_demux,
          n, GST_DVD_DEMUX_SUBP_DVD, NULL);

    GST_DEBUG_OBJECT (dvd_demux,
        "Created 1 video stream, %d audio streams and %d subpicture streams "
        "based on DVD lang codes event; now signalling no-more-pads",
        num_audstreams, num_substreams);

    /* we know this will be all */
    gst_element_no_more_pads (GST_ELEMENT (dvd_demux));

    /* Keep video/audio/subtitle pads within 1/2 sec of the SCR */
    mpeg_demux->max_gap = MAX_GAP;
    mpeg_demux->max_gap_tolerance = MAX_GAP_TOLERANCE;
  } else {
    GST_DEBUG_OBJECT (dvd_demux, "dvddemux Forwarding DVD event %s to all pads",
        event_type);

    PARSE_CLASS (dvd_demux)->send_event (mpeg_parse, event);
  }

  return TRUE;
}

static GstMPEGStream *
gst_dvd_demux_get_video_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  GstMPEGStream *str =
      parent_class->get_video_stream (mpeg_demux, stream_nr, type, info);
  gint mpeg_version = *((gint *) info);

  if (dvd_demux->mpeg_version != mpeg_version) {
    if (str->caps)
      gst_caps_unref (str->caps);
    str->caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, mpeg_version,
        "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);

    if (!gst_pad_set_caps (dvd_demux->cur_video, str->caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
          CORE, NEGOTIATION, (NULL), ("failed to set caps"));
      gst_caps_unref (str->caps);
      str->caps = NULL;
      return str;
    } else {
      dvd_demux->mpeg_version = mpeg_version;
    }
  }

  dvd_demux->mpeg_version = mpeg_version;
  return str;
}

static GstMPEGStream *
gst_dvd_demux_get_audio_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  guint32 sample_info = 0;
  GstMPEGStream *str;
  GstDVDLPCMStream *lpcm_str = NULL;
  gboolean add_pad = FALSE;
  const gchar *codec = NULL, *lang_code = NULL;

  g_return_val_if_fail (stream_nr < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS, NULL);
  g_return_val_if_fail (type > GST_MPEG_DEMUX_AUDIO_UNKNOWN &&
      type < GST_DVD_DEMUX_AUDIO_LAST, NULL);

  if (type < GST_MPEG_DEMUX_AUDIO_LAST) {
    /* FIXME: language codes on MPEG audio streams */
    return parent_class->get_audio_stream (mpeg_demux, stream_nr, type, info);
  }

  if (type == GST_DVD_DEMUX_AUDIO_LPCM) {
    sample_info = *((guint32 *) info);
  }

  str = mpeg_demux->audio_stream[stream_nr];

  /* If the stream type is changing, recreate the pad */
  if (str && str->type != type) {
    gst_element_remove_pad (GST_ELEMENT (mpeg_demux), str->pad);
    g_free (str);
    str = mpeg_demux->audio_stream[stream_nr] = NULL;
  }

  if (str == NULL) {
    gchar *name;

    if (type != GST_DVD_DEMUX_AUDIO_LPCM) {
      str = g_new0 (GstMPEGStream, 1);
    } else {
      lpcm_str = g_new0 (GstDVDLPCMStream, 1);
      str = (GstMPEGStream *) lpcm_str;
    }

    name = g_strdup_printf ("audio_%02d", stream_nr);
    DEMUX_CLASS (dvd_demux)->init_stream (mpeg_demux, type, str, stream_nr,
        name, DEMUX_CLASS (dvd_demux)->audio_template);
    /* update caps */
    str->type = GST_MPEG_DEMUX_AUDIO_UNKNOWN;
    g_free (name);
    add_pad = TRUE;
  } else {
    /* Stream size may have changed, reset it. */
    if (type != GST_DVD_DEMUX_AUDIO_LPCM) {
      str = g_renew (GstMPEGStream, str, 1);
    } else {
      lpcm_str = g_renew (GstDVDLPCMStream, str, 1);
      str = (GstMPEGStream *) lpcm_str;
    }
  }

  mpeg_demux->audio_stream[stream_nr] = str;

  if (type != str->type ||
      (type == GST_DVD_DEMUX_AUDIO_LPCM &&
          sample_info != lpcm_str->sample_info)) {
    gint width, rate, channels, dynamic_range;
    gboolean emphasis, mute;

    /* We need to set new caps for this pad. */
    switch (type) {
      case GST_DVD_DEMUX_AUDIO_LPCM:
        /* Dynamic range in the lower byte */
        dynamic_range = sample_info & 0xff;

        /* Determine the sample width. */
        switch (sample_info & 0xC000) {
          case 0x8000:
            width = 24;
            break;
          case 0x4000:
            width = 20;
            break;
          default:
            width = 16;
            break;
        }

        /* Determine the rate. */
        if (sample_info & 0x1000) {
          rate = 96000;
        } else {
          rate = 48000;
        }

        mute = ((sample_info & 0x400000) != 0);
        emphasis = ((sample_info & 0x800000) != 0);

        /* Determine the number of channels. */
        channels = ((sample_info >> 8) & 0x7) + 1;

        if (str->caps)
          gst_caps_unref (str->caps);
        str->caps = gst_caps_new_simple ("audio/x-lpcm",
            "width", G_TYPE_INT, width,
            "rate", G_TYPE_INT, rate,
            "channels", G_TYPE_INT, channels,
            "dynamic_range", G_TYPE_INT, dynamic_range,
            "emphasis", G_TYPE_BOOLEAN, emphasis,
            "mute", G_TYPE_BOOLEAN, mute, NULL);

        lpcm_str->sample_info = sample_info;
        lpcm_str->width = width;
        lpcm_str->rate = rate;
        lpcm_str->channels = channels;
        lpcm_str->dynamic_range = dynamic_range;
        lpcm_str->mute = mute;
        lpcm_str->emphasis = emphasis;
        codec = "LPCM audio";
        break;

      case GST_DVD_DEMUX_AUDIO_AC3:
        if (str->caps)
          gst_caps_unref (str->caps);
        str->caps = gst_caps_new_simple ("audio/x-ac3", NULL);
        codec = "AC-3 audio";
        break;

      case GST_DVD_DEMUX_AUDIO_DTS:
        if (str->caps)
          gst_caps_unref (str->caps);
        str->caps = gst_caps_new_simple ("audio/x-dts", NULL);
        codec = "DTS audio";
        break;

      default:
        g_return_val_if_reached (NULL);
        break;
    }

    if (!gst_pad_set_caps (str->pad, str->caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
          CORE, NEGOTIATION, (NULL),
          ("failed to set caps on pad %s:%s", GST_DEBUG_PAD_NAME (str->pad)));
      gst_caps_unref (str->caps);
      str->caps = NULL;
      return str;
    }

    if (str->number == dvd_demux->cur_audio_nr) {
      /* This is the current audio stream.  Use the same caps. */
      if (!gst_pad_set_caps (dvd_demux->cur_audio, str->caps)) {
        GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
            CORE, NEGOTIATION, (NULL), ("failed to set caps on pad %s:%s",
                GST_DEBUG_PAD_NAME (dvd_demux->cur_audio)));
      }
    }

    if (add_pad) {
      if (dvd_demux->langcodes) {
        gchar *t;

        t = g_strdup_printf ("audio-%d-language", stream_nr);
        lang_code =
            gst_structure_get_string (gst_event_get_structure
            (dvd_demux->langcodes), t);
        g_free (t);
      }

      GST_DEBUG_OBJECT (mpeg_demux, "adding pad %s with language = %s",
          GST_PAD_NAME (str->pad), (lang_code) ? lang_code : "(unknown)");

      gst_pad_set_active (str->pad, TRUE);
      gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);

      if (codec || lang_code) {
        GstTagList *list = gst_tag_list_new ();

        if (codec) {
          gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
              GST_TAG_AUDIO_CODEC, codec, NULL);
        }
        if (lang_code) {
          gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
              GST_TAG_LANGUAGE_CODE, lang_code, NULL);
        }
        str->tags = gst_tag_list_copy (list);
        gst_element_found_tags_for_pad (GST_ELEMENT (mpeg_demux),
            str->pad, list);
      }
    }

    str->type = type;
  }

  return str;
}


static GstMPEGStream *
gst_dvd_demux_get_subpicture_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  GstMPEGStream *str;
  gchar *name;
  gboolean add_pad = FALSE;
  const gchar *lang_code = NULL;

  g_return_val_if_fail (stream_nr < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS, NULL);
  g_return_val_if_fail (type > GST_DVD_DEMUX_SUBP_UNKNOWN &&
      type < GST_DVD_DEMUX_SUBP_LAST, NULL);

  str = dvd_demux->subpicture_stream[stream_nr];

  if (str == NULL) {
    str = g_new0 (GstMPEGStream, 1);

    name = g_strdup_printf ("subpicture_%02d", stream_nr);
    DEMUX_CLASS (dvd_demux)->init_stream (mpeg_demux, type, str, stream_nr,
        name, CLASS (dvd_demux)->subpicture_template);
    str->type = GST_DVD_DEMUX_SUBP_UNKNOWN;
    g_free (name);
    add_pad = TRUE;
  } else {
    /* This stream may have been created by a derived class, reset the
       size. */
    str = g_renew (GstMPEGStream, str, 1);
  }

  dvd_demux->subpicture_stream[stream_nr] = str;

  if (str->type != GST_DVD_DEMUX_SUBP_DVD) {
    /* We need to set new caps for this pad. */
    if (str->caps)
      gst_caps_unref (str->caps);
    str->caps = gst_caps_new_simple ("video/x-dvd-subpicture", NULL);

    if (!gst_pad_set_caps (str->pad, str->caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
          CORE, NEGOTIATION, (NULL),
          ("failed to set caps on pad %s:%s", GST_DEBUG_PAD_NAME (str->pad)));
      gst_caps_unref (str->caps);
      str->caps = NULL;
      return str;
    }

    if (str->number == dvd_demux->cur_subpicture_nr) {
      /* This is the current subpicture stream.  Use the same caps. */
      if (!gst_pad_set_caps (dvd_demux->cur_subpicture, str->caps)) {
        GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
            CORE, NEGOTIATION, (NULL),
            ("failed to set caps on pad %s:%s", GST_DEBUG_PAD_NAME (str->pad)));
      }
    }

    if (add_pad) {
      if (dvd_demux->langcodes) {
        gchar *t;

        t = g_strdup_printf ("subtitle-%d-language", stream_nr);
        lang_code =
            gst_structure_get_string (gst_event_get_structure
            (dvd_demux->langcodes), t);
        g_free (t);
      }

      GST_DEBUG_OBJECT (mpeg_demux, "adding pad %s with language = %s",
          GST_PAD_NAME (str->pad), (lang_code) ? lang_code : "(unknown)");

      gst_pad_set_active (str->pad, TRUE);
      gst_element_add_pad (GST_ELEMENT (mpeg_demux), str->pad);

      if (lang_code) {
        GstTagList *list = gst_tag_list_new ();

        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_LANGUAGE_CODE, lang_code, NULL);
        str->tags = gst_tag_list_copy (list);
        gst_element_found_tags_for_pad (GST_ELEMENT (mpeg_demux),
            str->pad, list);
      }
    }
    str->type = GST_DVD_DEMUX_SUBP_DVD;
  }

  return str;
}

static gboolean
gst_dvd_demux_parse_packhead (GstMPEGParse * mpeg_parse, GstBuffer * buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_parse);
  gboolean pending_tags = mpeg_demux->pending_tags;

  GST_MPEG_PARSE_CLASS (parent_class)->parse_packhead (mpeg_parse, buffer);

  if (pending_tags) {
    GstMPEGStream **streams;
    guint i, num;

    streams = dvd_demux->subpicture_stream;
    num = GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS;
    for (i = 0; i < num; ++i) {
      if (streams[i] != NULL && streams[i]->tags != NULL)
        gst_pad_push_event (streams[i]->pad,
            gst_event_new_tag (gst_tag_list_copy (streams[i]->tags)));
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_dvd_demux_process_private (GstMPEGDemux * mpeg_demux,
    GstBuffer * buffer,
    guint stream_nr, GstClockTime timestamp, guint headerlen, guint datalen)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *basebuf;
  guint8 ps_id_code;
  GstMPEGStream *outstream = NULL;
  guint first_access = 0;
  gint align = 1, len, off;

  basebuf = GST_BUFFER_DATA (buffer);

  /* Determine the substream number. */
  ps_id_code = basebuf[headerlen + 4];

  /* In the following, the "first access" refers to the location in a
     buffer the time stamp is associated to.  DVDs include this
     information explicitely. */
  switch (stream_nr) {
    case 0:
      /* Private stream 1. */

      if (ps_id_code >= 0x80 && ps_id_code <= 0x87) {
        GST_LOG_OBJECT (dvd_demux,
            "we have an audio (AC3) packet, track %d", ps_id_code - 0x80);
        outstream = DEMUX_CLASS (dvd_demux)->get_audio_stream (mpeg_demux,
            ps_id_code - 0x80, GST_DVD_DEMUX_AUDIO_AC3, NULL);

        /* Determine the position of the "first access".  This
           should always be the beginning of an AC3 frame. */
        first_access = (basebuf[headerlen + 6] << 8) | basebuf[headerlen + 7];

        headerlen += 4;
        datalen -= 4;
      } else if (ps_id_code >= 0x88 && ps_id_code <= 0x8f) {
        GST_LOG_OBJECT (dvd_demux,
            "we have an audio (DTS) packet, track %d", ps_id_code - 0x88);
        outstream = DEMUX_CLASS (dvd_demux)->get_audio_stream (mpeg_demux,
            ps_id_code - 0x88, GST_DVD_DEMUX_AUDIO_DTS, NULL);

        /* Determine the position of the "first access".  This
           should always be the beginning of a DTS frame. */
        first_access = (basebuf[headerlen + 6] << 8) | basebuf[headerlen + 7];

        headerlen += 4;
        datalen -= 4;
      } else if (ps_id_code >= 0xA0 && ps_id_code <= 0xA7) {
        GstDVDLPCMStream *lpcm_str;
        guint32 lpcm_sample_info;

        GST_LOG_OBJECT (dvd_demux,
            "we have an audio (LPCM) packet, track %d", ps_id_code - 0xA0);

        /* Compose the sample info from the LPCM header, masking out the frame_num */
        lpcm_sample_info =
            basebuf[headerlen + 10] | (basebuf[headerlen +
                9] << 8) | ((basebuf[headerlen + 8] & 0xc0) << 16);

        outstream = DEMUX_CLASS (dvd_demux)->get_audio_stream (mpeg_demux,
            ps_id_code - 0xA0, GST_DVD_DEMUX_AUDIO_LPCM, &lpcm_sample_info);
        lpcm_str = (GstDVDLPCMStream *) outstream;

        /* Determine the position of the "first access". */
        first_access = (basebuf[headerlen + 6] << 8) | basebuf[headerlen + 7];

        /* Get rid of the LPCM header. */
        headerlen += 7;
        datalen -= 7;

        /* align by frame round up to nearest byte */
        align = (lpcm_str->width * lpcm_str->channels + 7) / 8;
      } else if (ps_id_code >= 0x20 && ps_id_code <= 0x3F) {
        GST_LOG_OBJECT (dvd_demux,
            "we have a subpicture packet, track %d", ps_id_code - 0x20);
        outstream = CLASS (dvd_demux)->get_subpicture_stream (mpeg_demux,
            ps_id_code - 0x20, GST_DVD_DEMUX_SUBP_DVD, NULL);

        headerlen += 1;
        datalen -= 1;
      } else {
        GST_WARNING_OBJECT (dvd_demux,
            "unknown DVD (private 1) id 0x%02x", ps_id_code);
      }
      break;

    case 1:
      /* Private stream 2 */

      switch (ps_id_code) {
        case 0:
          GST_LOG_OBJECT (dvd_demux, "we have a PCI nav packet");

          outstream = DEMUX_CLASS (mpeg_demux)->get_private_stream (mpeg_demux,
              1, GST_MPEG_DEMUX_PRIVATE_UNKNOWN, NULL);
          break;

        case 1:
          GST_LOG_OBJECT (dvd_demux, "we have a DSI nav packet");

          outstream = DEMUX_CLASS (mpeg_demux)->get_private_stream (mpeg_demux,
              1, GST_MPEG_DEMUX_PRIVATE_UNKNOWN, NULL);
          break;

        default:
          GST_WARNING_OBJECT (dvd_demux,
              "unknown DVD (private 2) id 0x%02x", ps_id_code);
          break;
      }
      break;

    default:
      g_return_val_if_reached (GST_FLOW_UNEXPECTED);
      break;
  }

  if (outstream == NULL) {
    return GST_FLOW_OK;
  }

  if (timestamp != GST_CLOCK_TIME_NONE && first_access > 1) {
    /* We have a first access location.  Since GStreamer doesn't have
       a means to associate a timestamp to the middle of a buffer, we
       send two separate buffers and put the timestamp in the second
       one. */
    off = headerlen + 4;
    len = first_access - 1;
    len -= len % align;
    if (len > 0) {
      ret = DEMUX_CLASS (dvd_demux)->send_subbuffer (mpeg_demux, outstream,
          buffer, GST_CLOCK_TIME_NONE, off, len);
    }
    off += len;
    len = datalen - len;
    len -= len % align;
    if (len > 0) {
      ret = DEMUX_CLASS (dvd_demux)->send_subbuffer (mpeg_demux, outstream,
          buffer, timestamp, off, len);
    }
  } else {
    off = headerlen + 4;
    len = datalen;
    len -= len % align;
    if (len > 0) {
      ret = DEMUX_CLASS (dvd_demux)->send_subbuffer (mpeg_demux, outstream,
          buffer, timestamp, off, len);
    }
  }

  return ret;
}

/* random magic value */
#define MIN_BUFS_FOR_NO_MORE_PADS 100

static GstFlowReturn
gst_dvd_demux_combine_flows (GstMPEGDemux * mpegdemux, GstMPEGStream * stream,
    GstFlowReturn flow)
{
  GstDVDDemux *demux = (GstDVDDemux *) mpegdemux;
  gint i;

  /* store the value */
  stream->last_flow = flow;

  /* if it's success we can return the value right away */
  if (flow == GST_FLOW_OK)
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (flow != GST_FLOW_NOT_LINKED) {
    GST_DEBUG_OBJECT (demux, "flow %s on pad %" GST_PTR_FORMAT,
        gst_flow_get_name (flow), stream->pad);
    goto done;
  }

  /* let parent class check for anything non-not-linked for its streams */
  flow =
      GST_MPEG_DEMUX_CLASS (parent_class)->combine_flows (mpegdemux, stream,
      flow);
  if (flow != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (i = 0; i < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; i++) {
    if (demux->subpicture_stream[i] != NULL) {
      flow = demux->subpicture_stream[i]->last_flow;
      /* some other return value (must be SUCCESS but we can return
       * other values as well) */
      if (flow != GST_FLOW_NOT_LINKED)
        goto done;
      if (demux->subpicture_stream[i]->buffers_sent < MIN_BUFS_FOR_NO_MORE_PADS) {
        flow = GST_FLOW_OK;
        goto done;
      }
    }
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
  GST_DEBUG_OBJECT (demux, "all pads combined have not-linked flow");

done:
  return flow;
}

static GstFlowReturn
gst_dvd_demux_send_subbuffer (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * outstream, GstBuffer * buffer,
    GstClockTime timestamp, guint offset, guint size)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  GstFlowReturn ret;
  GstPad *outpad;
  gint cur_nr;

  if (dvd_demux->segment_filter &&
      GST_MPEG_DEMUX_STREAM_KIND (outstream->type) ==
      GST_MPEG_DEMUX_STREAM_AUDIO) {
    /* We are in segment_filter mode and have an audio buffer. */
    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* This is the first valid audio buffer after the flush. */
      dvd_demux->segment_filter = FALSE;
    } else {
      /* Discard the buffer. */
      return GST_FLOW_OK;
    }
  }

  /* You never know what happens to a buffer when you send it.  Just
     in case, we keep a reference to the buffer during the execution
     of this function. */
  gst_buffer_ref (buffer);

  /* Send the buffer to the standard output pad. */
  ret = parent_class->send_subbuffer (mpeg_demux, outstream, buffer,
      timestamp, offset, size);

  /* Determine the current output pad and stream number for the given
     type of stream. */
  switch (GST_MPEG_DEMUX_STREAM_KIND (outstream->type)) {
    case GST_MPEG_DEMUX_STREAM_VIDEO:
      outpad = dvd_demux->cur_video;
      cur_nr = dvd_demux->cur_video_nr;
      break;
    case GST_MPEG_DEMUX_STREAM_AUDIO:
      outpad = dvd_demux->cur_audio;
      cur_nr = dvd_demux->cur_audio_nr;
      break;
    case GST_MPEG_DEMUX_STREAM_PRIVATE:
      outpad = NULL;
      cur_nr = 0;
      break;
    case GST_DVD_DEMUX_STREAM_SUBPICTURE:
      outpad = dvd_demux->cur_subpicture;
      cur_nr = dvd_demux->cur_subpicture_nr;
      break;
    default:
      g_return_val_if_reached (GST_FLOW_UNEXPECTED);
      break;
  }

  if (outpad != NULL && cur_nr == outstream->number && size > 0) {
    GstBuffer *outbuf;

    /* We have a packet of the current stream. Send it to the
       corresponding pad as well. */
    outbuf = gst_buffer_create_sub (buffer, offset, size);
    g_return_val_if_fail (outbuf != NULL, GST_FLOW_UNEXPECTED);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buffer) + offset;
    gst_buffer_set_caps (outbuf, outstream->caps);

    ret = gst_pad_push (outpad, outbuf);

    /* this was one of the current_foo pads, which is shadowing one of the
     * foo_%02d pads, so since we keep track of the last flow value in the
     * stream structure we need to combine the OK/NOT_LINKED flows here
     * (because both pads share the same stream structure) */
    if ((ret == GST_FLOW_NOT_LINKED && outstream->last_flow == GST_FLOW_OK) ||
        (ret == GST_FLOW_OK && outstream->last_flow == GST_FLOW_NOT_LINKED)) {
      outstream->last_flow = GST_FLOW_OK;
      ret = GST_FLOW_OK;
    }
  }

  gst_buffer_unref (buffer);

  ret = DEMUX_CLASS (dvd_demux)->combine_flows (mpeg_demux, outstream, ret);

  return ret;
}

static void
gst_dvd_demux_set_cur_audio (GstDVDDemux * dvd_demux, gint stream_nr)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);
  GstMPEGStream *str;
  GstCaps *caps;

  g_return_if_fail (stream_nr >= -1 &&
      stream_nr < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS);

  GST_DEBUG_OBJECT (dvd_demux, "changing current audio to %02d", stream_nr);

  dvd_demux->cur_audio_nr = stream_nr;

  if (stream_nr == -1) {
    return;
  }

  str = mpeg_demux->audio_stream[stream_nr];
  if (str != NULL) {
    /* (Re)set the caps in the "current" pad. */
    caps = GST_PAD_CAPS (str->pad);
    if (caps != NULL) {
      gst_pad_set_caps (dvd_demux->cur_audio, caps);
    }
  }
}

static void
gst_dvd_demux_set_cur_subpicture (GstDVDDemux * dvd_demux, gint stream_nr)
{
  GstMPEGStream *str;

  g_return_if_fail (stream_nr >= -1 &&
      stream_nr < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS);

  GST_DEBUG_OBJECT (dvd_demux, "changing current subpicture to %02d",
      stream_nr);

  dvd_demux->cur_subpicture_nr = stream_nr;

  if (stream_nr == -1) {
    return;
  }

  str = dvd_demux->subpicture_stream[stream_nr];
  if (str != NULL) {
    GstCaps *caps = NULL;

    /* (Re)set the caps in the "current" pad. */
    caps = GST_PAD_CAPS (str->pad);
    gst_pad_set_caps (dvd_demux->cur_subpicture, caps);
  }
}

static void
gst_dvd_demux_reset (GstDVDDemux * dvd_demux)
{
  int i;

  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);

  GST_INFO ("Resetting the dvd demuxer");
  for (i = 0; i < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; i++) {
    if (dvd_demux->subpicture_stream[i]) {
      gst_pad_push_event (dvd_demux->subpicture_stream[i]->pad,
          gst_event_new_eos ());

      gst_element_remove_pad (GST_ELEMENT (dvd_demux),
          dvd_demux->subpicture_stream[i]->pad);
      if (dvd_demux->subpicture_stream[i]->caps)
        gst_caps_unref (dvd_demux->subpicture_stream[i]->caps);
      if (dvd_demux->subpicture_stream[i]->tags)
        gst_tag_list_free (dvd_demux->subpicture_stream[i]->tags);
      g_free (dvd_demux->subpicture_stream[i]);
      dvd_demux->subpicture_stream[i] = NULL;
    }
  }
  gst_pad_set_caps (dvd_demux->cur_video, NULL);
  gst_pad_set_caps (dvd_demux->cur_audio, NULL);
  gst_pad_set_caps (dvd_demux->cur_subpicture, NULL);

  dvd_demux->cur_video_nr = 0;
  dvd_demux->cur_audio_nr = 0;
  dvd_demux->cur_subpicture_nr = 0;
  dvd_demux->mpeg_version = 0;

  /* Reset max_gap handling */
  mpeg_demux->max_gap = MAX_GAP;
  mpeg_demux->max_gap_tolerance = MAX_GAP_TOLERANCE;
}

static void
gst_dvd_demux_synchronise_pads (GstMPEGDemux * mpeg_demux,
    GstClockTime threshold, GstClockTime new_ts)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  int i;

  parent_class->synchronise_pads (mpeg_demux, threshold, new_ts);

  for (i = 0; i < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; i++) {
    if (dvd_demux->subpicture_stream[i]) {
      GST_LOG_OBJECT (mpeg_demux, "stream: %d, current: %" GST_TIME_FORMAT
          ", threshold %" GST_TIME_FORMAT, i,
          GST_TIME_ARGS (dvd_demux->subpicture_stream[i]->cur_ts),
          GST_TIME_ARGS (threshold));
      if (dvd_demux->subpicture_stream[i]->cur_ts < threshold) {
        DEMUX_CLASS (mpeg_demux)->sync_stream_to_time (mpeg_demux,
            dvd_demux->subpicture_stream[i], new_ts);
        dvd_demux->subpicture_stream[i]->cur_ts = new_ts;
      }
    }
  }
}

static void
gst_dvd_demux_sync_stream_to_time (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * stream, GstClockTime last_ts)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (mpeg_demux);
  GstPad *outpad;
  gint cur_nr;

  parent_class->sync_stream_to_time (mpeg_demux, stream, last_ts);

  switch (GST_MPEG_DEMUX_STREAM_KIND (stream->type)) {
    case GST_MPEG_DEMUX_STREAM_VIDEO:
      outpad = dvd_demux->cur_video;
      cur_nr = dvd_demux->cur_video_nr;
      break;
    case GST_MPEG_DEMUX_STREAM_AUDIO:
      outpad = dvd_demux->cur_audio;
      cur_nr = dvd_demux->cur_audio_nr;
      break;
    case GST_DVD_DEMUX_STREAM_SUBPICTURE:
      outpad = dvd_demux->cur_subpicture;
      cur_nr = dvd_demux->cur_subpicture_nr;
      break;
    default:
      return;
  }

  if (outpad && (cur_nr == stream->number)) {
    guint64 update_time;

    update_time =
        MIN ((guint64) last_ts, (guint64) mpeg_parse->current_segment.stop);
    gst_pad_push_event (outpad, gst_event_new_new_segment (TRUE,
            mpeg_parse->current_segment.rate, GST_FORMAT_TIME,
            update_time, mpeg_parse->current_segment.stop, update_time));
  }
}

static GstStateChangeReturn
gst_dvd_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_dvd_demux_reset (dvd_demux);
      if (dvd_demux->langcodes) {
        gst_event_unref (dvd_demux->langcodes);
        dvd_demux->langcodes = NULL;
      }
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_dvd_demux_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dvddemux",
      GST_RANK_SECONDARY + 1, GST_TYPE_DVD_DEMUX);
}
