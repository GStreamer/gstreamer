/* GStreamer
 * Copyright (C) 2004 Martin Soto <martinsoto@users.sourceforge.net>
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


GST_DEBUG_CATEGORY_STATIC (gstdvddemux_debug);
#define GST_CAT_DEFAULT (gstdvddemux_debug)


#define PARSE_CLASS(o)  GST_MPEG_PARSE_CLASS (G_OBJECT_GET_CLASS (o))
#define DEMUX_CLASS(o)  GST_MPEG_DEMUX_CLASS (G_OBJECT_GET_CLASS (o))
#define CLASS(o)  GST_DVD_DEMUX_CLASS (G_OBJECT_GET_CLASS (o))


/* Element factory information */
static GstElementDetails dvd_demux_details = {
  "DVD Demuxer",
  "Codec/Demuxer",
  "Demultiplexes DVD (VOB) MPEG2 streams",
  "Martin Soto <soto@informatik.uni-kl.de>"
};

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

#define VIDEO_CAPS \
  GST_STATIC_CAPS ("video/mpeg, " \
    "mpegversion = (int) { 1, 2 }, " \
    "systemstream = (boolean) FALSE" \
  )

#define AUDIO_CAPS \
  GST_STATIC_CAPS ( \
    "audio/mpeg, " \
      "mpegversion = (int) 1;" \
    "audio/x-raw-int, " \
      "endianness = (int) BIG_ENDIAN, " \
      "signed = (boolean) TRUE, " \
      "width = (int) { 16, 20, 24 }, " \
      "depth = (int) { 16, 20, 24 }, " \
      "rate = (int) { 48000, 96000 }, " \
      "channels = (int) [ 1, 8 ];" \
    "audio/x-ac3" \
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


static void gst_dvd_demux_class_init (GstDVDDemuxClass * klass);
static void gst_dvd_demux_base_init (GstDVDDemuxClass * klass);
static void gst_dvd_demux_init (GstDVDDemux * dvd_demux);

static void gst_dvd_demux_send_data (GstMPEGParse * mpeg_parse,
    GstData * data, GstClockTime time);

static void gst_dvd_demux_send_discont
    (GstMPEGParse * mpeg_parse, GstClockTime time);
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

static void gst_dvd_demux_process_private
    (GstMPEGDemux * mpeg_demux,
    GstBuffer * buffer,
    guint stream_nr, GstClockTime timestamp, guint headerlen, guint datalen);

static void gst_dvd_demux_send_subbuffer
    (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * outstream,
    GstBuffer * buffer, GstClockTime timestamp, guint offset, guint size);

static void gst_dvd_demux_set_cur_audio
    (GstDVDDemux * dvd_demux, gint stream_nr);
static void gst_dvd_demux_set_cur_subpicture
    (GstDVDDemux * dvd_demux, gint stream_nr);


static GstMPEGDemuxClass *parent_class = NULL;

/*static guint gst_dvd_demux_signals[LAST_SIGNAL] = { 0 };*/


GType
gst_dvd_demux_get_type (void)
{
  static GType dvd_demux_type = 0;

  if (!dvd_demux_type) {
    static const GTypeInfo dvd_demux_info = {
      sizeof (GstDVDDemuxClass),
      (GBaseInitFunc) gst_dvd_demux_base_init,
      NULL,
      (GClassInitFunc) gst_dvd_demux_class_init,
      NULL,
      NULL,
      sizeof (GstDVDDemux),
      0,
      (GInstanceInitFunc) gst_dvd_demux_init,
    };

    dvd_demux_type = g_type_register_static (GST_TYPE_MPEG_DEMUX,
        "GstDVDDemux", &dvd_demux_info, 0);

    GST_DEBUG_CATEGORY_INIT (gstdvddemux_debug, "dvddemux", 0,
        "DVD (VOB) demultiplexer element");
  }

  return dvd_demux_type;
}


static void
gst_dvd_demux_base_init (GstDVDDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstMPEGDemuxClass *demux_class = GST_MPEG_DEMUX_CLASS (klass);
  GstMPEGParseClass *mpeg_parse_class = (GstMPEGParseClass *) klass;

  mpeg_parse_class->send_data = gst_dvd_demux_send_data;

  demux_class->audio_template = gst_static_pad_template_get (&audio_template);

  klass->cur_video_template = gst_static_pad_template_get (&cur_video_template);
  klass->cur_audio_template = gst_static_pad_template_get (&cur_audio_template);
  klass->subpicture_template =
      gst_static_pad_template_get (&subpicture_template);
  klass->cur_subpicture_template =
      gst_static_pad_template_get (&cur_subpicture_template);

  gst_element_class_add_pad_template (element_class,
      demux_class->audio_template);

  gst_element_class_add_pad_template (element_class, klass->cur_video_template);
  gst_element_class_add_pad_template (element_class, klass->cur_audio_template);
  gst_element_class_add_pad_template (element_class,
      klass->subpicture_template);
  gst_element_class_add_pad_template (element_class,
      klass->cur_subpicture_template);

  gst_element_class_set_details (element_class, &dvd_demux_details);
}


static void
gst_dvd_demux_class_init (GstDVDDemuxClass * klass)
{
  GstElementClass *gstelement_class;
  GstMPEGParseClass *mpeg_parse_class;
  GstMPEGDemuxClass *mpeg_demux_class;

  parent_class = g_type_class_ref (GST_TYPE_MPEG_DEMUX);

  gstelement_class = (GstElementClass *) klass;
  mpeg_parse_class = (GstMPEGParseClass *) klass;
  mpeg_demux_class = (GstMPEGDemuxClass *) klass;

  mpeg_parse_class->send_discont = gst_dvd_demux_send_discont;

  mpeg_demux_class->get_audio_stream = gst_dvd_demux_get_audio_stream;
  mpeg_demux_class->get_video_stream = gst_dvd_demux_get_video_stream;
  mpeg_demux_class->send_subbuffer = gst_dvd_demux_send_subbuffer;
  mpeg_demux_class->process_private = gst_dvd_demux_process_private;

  klass->get_subpicture_stream = gst_dvd_demux_get_subpicture_stream;
}


static void
gst_dvd_demux_init (GstDVDDemux * dvd_demux)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (dvd_demux);
  gint i;

  GST_FLAG_SET (dvd_demux, GST_ELEMENT_EVENT_AWARE);

  /* Create the pads for the current streams. */
  dvd_demux->cur_video =
      DEMUX_CLASS (dvd_demux)->new_output_pad (mpeg_demux, "current_video",
      CLASS (dvd_demux)->cur_video_template);
  dvd_demux->cur_audio =
      DEMUX_CLASS (dvd_demux)->new_output_pad (mpeg_demux, "current_audio",
      CLASS (dvd_demux)->cur_audio_template);
  dvd_demux->cur_subpicture =
      DEMUX_CLASS (dvd_demux)->new_output_pad (mpeg_demux, "current_subpicture",
      CLASS (dvd_demux)->cur_subpicture_template);

  dvd_demux->mpeg_version = 0;
  dvd_demux->cur_video_nr = 0;
  dvd_demux->cur_audio_nr = 0;
  dvd_demux->cur_subpicture_nr = 0;

  /* Start the timestamp sequence in 0. */
  dvd_demux->last_end_ptm = 0;

  /* Try to prevent the mpegparse infrastructure from doing timestamp
     adjustment. */
  mpeg_parse->do_adjust = FALSE;
  mpeg_parse->adjust = 0;

  dvd_demux->just_flushed = FALSE;
  dvd_demux->discont_time = GST_CLOCK_TIME_NONE;

  for (i = 0; i < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; i++) {
    dvd_demux->subpicture_stream[i] = NULL;
  }
}


static void
gst_dvd_demux_send_data (GstMPEGParse * mpeg_parse, GstData * data,
    GstClockTime time)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_parse);

  if (GST_IS_BUFFER (data)) {
    gst_buffer_unref (GST_BUFFER (data));
  } else {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_ANY:
        gst_dvd_demux_handle_dvd_event (dvd_demux, event);
        break;

      case GST_EVENT_FLUSH:
        GST_DEBUG_OBJECT (dvd_demux, "flush received");

        dvd_demux->just_flushed = TRUE;

        /* Propagate the event normally. */
        gst_pad_event_default (mpeg_parse->sinkpad, event);
        break;

      default:
        gst_pad_event_default (mpeg_parse->sinkpad, event);
        break;
    }
  }
}


static gboolean
gst_dvd_demux_handle_dvd_event (GstDVDDemux * dvd_demux, GstEvent * event)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (dvd_demux);
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);
  GstStructure *structure = event->event_data.structure.structure;
  const char *event_type = gst_structure_get_string (structure, "event");

  g_return_val_if_fail (event != NULL, FALSE);

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *text = gst_structure_to_string (structure);

    GST_LOG_OBJECT (dvd_demux, "processing event \"%s\"", text);
    g_free (text);
  }
#endif

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
  }

  else if (strcmp (event_type, "dvd-spu-stream-change") == 0) {
    gint stream_nr;

    gst_structure_get_int (structure, "physical", &stream_nr);
    if (stream_nr < -1 || stream_nr >= GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS) {
      GST_ERROR_OBJECT (dvd_demux,
          "GstDVDDemux: Invalid subpicture stream %02d", stream_nr);
      return FALSE;
    }
    gst_dvd_demux_set_cur_subpicture (dvd_demux, stream_nr);
    gst_event_unref (event);
  }

  else if (strcmp (event_type, "dvd-nav-packet") == 0) {
    GstStructure *structure = event->event_data.structure.structure;
    GstClockTime start_ptm =
        g_value_get_uint64 (gst_structure_get_value (structure, "start_ptm"));
    GstClockTime end_ptm =
        g_value_get_uint64 (gst_structure_get_value (structure, "end_ptm"));

    if (start_ptm != dvd_demux->last_end_ptm) {
      /* Set the adjust value to gap the discontinuity. */
      mpeg_demux->adjust += GST_CLOCK_DIFF (dvd_demux->last_end_ptm, start_ptm);

      GST_DEBUG_OBJECT (dvd_demux,
          "PTM sequence discontinuity: from %0.3fs to "
          "%0.3fs, new adjust %0.3fs",
          (double) dvd_demux->last_end_ptm / GST_SECOND,
          (double) start_ptm / GST_SECOND,
          (double) mpeg_demux->adjust / GST_SECOND);
    }
    dvd_demux->last_end_ptm = end_ptm;

    if (dvd_demux->just_flushed) {
      /* The pipeline was just flushed, schedule a discontinuity with
         the next sequence time. We don't do it here to reduce the
         time gap between the discontinuity and the subsequent data
         blocks. */
      dvd_demux->discont_time = start_ptm + mpeg_demux->adjust;
      dvd_demux->just_flushed = FALSE;
    }

    gst_event_unref (event);
  }

  else {
    if (GST_EVENT_TIMESTAMP (event) != GST_CLOCK_TIME_NONE) {
      GST_EVENT_TIMESTAMP (event) += mpeg_demux->adjust;
    }
    gst_pad_event_default (mpeg_parse->sinkpad, event);
  }

  return TRUE;
}


static void
gst_dvd_demux_send_discont (GstMPEGParse * mpeg_parse, GstClockTime time)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_parse);
  GstEvent *discont;
  gint i;

  GST_DEBUG_OBJECT (dvd_demux, "sending discontinuity: %0.3fs",
      (double) time / GST_SECOND);

  GST_MPEG_PARSE_CLASS (parent_class)->send_discont (mpeg_parse, time);

  for (i = 0; i < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS; i++) {
    if (dvd_demux->subpicture_stream[i] &&
        GST_PAD_IS_USABLE (dvd_demux->subpicture_stream[i]->pad)) {
      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
          time, NULL);

      gst_pad_push (dvd_demux->subpicture_stream[i]->pad, GST_DATA (discont));
    }
  }

  /* Distribute the event to the "current" pads. */
  if (GST_PAD_IS_USABLE (dvd_demux->cur_video)) {
    discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, NULL);

    gst_pad_push (dvd_demux->cur_video, GST_DATA (discont));
  }

  if (GST_PAD_IS_USABLE (dvd_demux->cur_audio)) {
    discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, NULL);

    gst_pad_push (dvd_demux->cur_audio, GST_DATA (discont));
  }

  if (GST_PAD_IS_USABLE (dvd_demux->cur_subpicture)) {
    discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, NULL);

    gst_pad_push (dvd_demux->cur_subpicture, GST_DATA (discont));
  }
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
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, mpeg_version,
        "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);

    if (!gst_pad_set_explicit_caps (dvd_demux->cur_video, caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (mpeg_demux),
          CORE, NEGOTIATION, (NULL), ("failed to set caps"));
    } else {
      dvd_demux->mpeg_version = mpeg_version;
    }
    gst_caps_free (caps);
  }

  return str;
}

static GstMPEGStream *
gst_dvd_demux_get_audio_stream (GstMPEGDemux * mpeg_demux,
    guint8 stream_nr, gint type, const gpointer info)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  guint8 sample_info = 0;
  GstMPEGStream *str;
  GstDVDLPCMStream *lpcm_str = NULL;
  gchar *name;
  GstCaps *caps;
  gint width, rate, channels;

  g_return_val_if_fail (stream_nr < GST_MPEG_DEMUX_NUM_AUDIO_STREAMS, NULL);
  g_return_val_if_fail (type > GST_MPEG_DEMUX_AUDIO_UNKNOWN &&
      type < GST_DVD_DEMUX_AUDIO_LAST, NULL);

  if (type < GST_MPEG_DEMUX_AUDIO_LAST) {
    return parent_class->get_audio_stream (mpeg_demux, stream_nr, type, info);
  }

  if (type == GST_DVD_DEMUX_AUDIO_LPCM) {
    sample_info = *((guint8 *) info);
  }

  str = mpeg_demux->audio_stream[stream_nr];

  if (str == NULL) {
    if (type != GST_DVD_DEMUX_AUDIO_LPCM) {
      str = g_new0 (GstMPEGStream, 1);
    } else {
      lpcm_str = g_new0 (GstDVDLPCMStream, 1);
      str = (GstMPEGStream *) lpcm_str;
    }
    str->type = GST_MPEG_DEMUX_AUDIO_UNKNOWN;

    name = g_strdup_printf ("audio_%02d", stream_nr);
    DEMUX_CLASS (dvd_demux)->init_stream (mpeg_demux, type, str, stream_nr,
        name, DEMUX_CLASS (dvd_demux)->audio_template);
    g_free (name);

    mpeg_demux->audio_stream[stream_nr] = str;
  } else {
    /* This stream may have been created by a derived class, reset the
       size. */
    if (type != GST_DVD_DEMUX_AUDIO_LPCM) {
      str = g_renew (GstMPEGStream, str, 1);
    } else {
      lpcm_str = g_renew (GstDVDLPCMStream, str, 1);
      str = (GstMPEGStream *) lpcm_str;
    }
  }

  if (type != str->type ||
      (type == GST_DVD_DEMUX_AUDIO_LPCM &&
          sample_info != lpcm_str->sample_info)) {
    /* We need to set new caps for this pad. */
    switch (type) {
      case GST_DVD_DEMUX_AUDIO_LPCM:
        /* Determine the sample width. */
        switch (sample_info & 0xC0) {
          case 0x80:
            width = 24;
            break;
          case 0x40:
            width = 20;
            break;
          default:
            width = 16;
            break;
        }

        /* Determine the rate. */
        if (sample_info & 0x10) {
          rate = 96000;
        } else {
          rate = 48000;
        }

        /* Determine the number of channels. */
        channels = (sample_info & 0x7) + 1;

        caps = gst_caps_new_simple ("audio/x-raw-int",
            "endianness", G_TYPE_INT, G_BIG_ENDIAN,
            "signed", G_TYPE_BOOLEAN, TRUE,
            "width", G_TYPE_INT, width,
            "depth", G_TYPE_INT, width,
            "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);


        lpcm_str->sample_info = sample_info;
        break;

      case GST_DVD_DEMUX_AUDIO_AC3:
        caps = gst_caps_new_simple ("audio/x-ac3", NULL);
        break;

      case GST_DVD_DEMUX_AUDIO_DTS:
        caps = gst_caps_new_simple ("audio/x-dts", NULL);
        break;

      default:
        g_return_val_if_reached (NULL);
        break;
    }

    gst_pad_set_explicit_caps (str->pad, caps);

    if (str->number == dvd_demux->cur_audio_nr) {
      /* This is the current audio stream.  Use the same caps. */
      gst_pad_set_explicit_caps (dvd_demux->cur_audio, gst_caps_copy (caps));
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
  GstCaps *caps;

  g_return_val_if_fail (stream_nr < GST_DVD_DEMUX_NUM_SUBPICTURE_STREAMS, NULL);
  g_return_val_if_fail (type > GST_DVD_DEMUX_SUBP_UNKNOWN &&
      type < GST_DVD_DEMUX_SUBP_LAST, NULL);

  str = dvd_demux->subpicture_stream[stream_nr];

  if (str == NULL) {
    str = g_new0 (GstMPEGStream, 1);
    str->type = GST_DVD_DEMUX_SUBP_UNKNOWN;

    name = g_strdup_printf ("subpicture_%02d", stream_nr);
    DEMUX_CLASS (dvd_demux)->init_stream (mpeg_demux, type, str, stream_nr,
        name, CLASS (dvd_demux)->subpicture_template);
    g_free (name);

    dvd_demux->subpicture_stream[stream_nr] = str;
  } else {
    /* This stream may have been created by a derived class, reset the
       size. */
    str = g_renew (GstMPEGStream, str, 1);
  }

  if (str->type != GST_DVD_DEMUX_SUBP_DVD) {
    /* We need to set new caps for this pad. */
    caps = gst_caps_new_simple ("video/x-dvd-subpicture", NULL);
    gst_pad_set_explicit_caps (str->pad, caps);

    if (str->number == dvd_demux->cur_subpicture_nr) {
      /* This is the current subpicture stream.  Use the same caps. */
      gst_pad_set_explicit_caps (dvd_demux->cur_subpicture, caps);
    }

    gst_caps_free (caps);
    str->type = GST_DVD_DEMUX_SUBP_DVD;
  }

  return str;
}


static void
gst_dvd_demux_process_private (GstMPEGDemux * mpeg_demux,
    GstBuffer * buffer,
    guint stream_nr, GstClockTime timestamp, guint headerlen, guint datalen)
{
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  guint8 *basebuf;
  guint8 ps_id_code, lpcm_sample_info;
  GstMPEGStream *outstream = NULL;
  guint first_access = 0;

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
        first_access = *(basebuf + headerlen + 6) * 256 +
            *(basebuf + headerlen + 7);

        headerlen += 4;
        datalen -= 4;
      } else if (ps_id_code >= 0xA0 && ps_id_code <= 0xA7) {
        GST_LOG_OBJECT (dvd_demux,
            "we have an audio (LPCM) packet, track %d", ps_id_code - 0xA0);
        lpcm_sample_info = basebuf[headerlen + 9];
        outstream = DEMUX_CLASS (dvd_demux)->get_audio_stream (mpeg_demux,
            ps_id_code - 0xA0, GST_DVD_DEMUX_AUDIO_LPCM, &lpcm_sample_info);

        /* Determine the position of the "first access". */
        first_access = *(basebuf + headerlen + 6) * 256 +
            *(basebuf + headerlen + 7);

        /* Get rid of the LPCM header. */
        headerlen += 7;
        datalen -= 7;
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
      g_return_if_reached ();
      break;
  }

  if (outstream == NULL) {
    return;
  }

  if (timestamp != GST_CLOCK_TIME_NONE && first_access > 1) {
    /* We have a first access location.  Since GStreamer doesn't have
       a means to associate a timestamp to the middle of a buffer, we
       send two separate buffers and put the timestamp in the second
       one. */
    DEMUX_CLASS (dvd_demux)->send_subbuffer (mpeg_demux, outstream,
        buffer, GST_CLOCK_TIME_NONE, headerlen + 4, first_access - 1);
    DEMUX_CLASS (dvd_demux)->send_subbuffer (mpeg_demux, outstream,
        buffer, timestamp,
        headerlen + 3 + first_access, datalen - (first_access - 1));
  } else {
    DEMUX_CLASS (dvd_demux)->send_subbuffer (mpeg_demux, outstream,
        buffer, timestamp, headerlen + 4, datalen);
  }
}


static void
gst_dvd_demux_send_subbuffer (GstMPEGDemux * mpeg_demux,
    GstMPEGStream * outstream, GstBuffer * buffer,
    GstClockTime timestamp, guint offset, guint size)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (mpeg_demux);
  GstDVDDemux *dvd_demux = GST_DVD_DEMUX (mpeg_demux);
  GstPad *outpad;
  gint cur_nr;

  /* If there's a pending discontinuity, send it now. The idea is to
     minimize the time interval between the discontinuity and the data
     buffers following it. */
  if (dvd_demux->discont_time != GST_CLOCK_TIME_NONE) {
    PARSE_CLASS (mpeg_demux)->send_discont (mpeg_parse,
        dvd_demux->discont_time - 200 * GST_MSECOND);
    dvd_demux->discont_time = GST_CLOCK_TIME_NONE;
  }

  /* You never know what happens to a buffer when you send it.  Just
     in case, we keep a reference to the buffer during the execution
     of this function. */
  gst_buffer_ref (buffer);

  /* Send the buffer to the standard output pad. */
  parent_class->send_subbuffer (mpeg_demux, outstream, buffer,
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
      g_return_if_reached ();
      break;
  }

  if ((outpad != NULL) && (cur_nr == outstream->number) && (size > 0)) {
    GstBuffer *outbuf;

    /* We have a packet of the current stream. Send it to the
       corresponding pad as well. */
    outbuf = gst_buffer_create_sub (buffer, offset, size);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buffer) + offset;

    gst_pad_push (outpad, GST_DATA (outbuf));
  }

  gst_buffer_unref (buffer);
}


static void
gst_dvd_demux_set_cur_audio (GstDVDDemux * dvd_demux, gint stream_nr)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (dvd_demux);
  GstMPEGStream *str;
  const GstCaps *caps;

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
    caps = gst_pad_get_negotiated_caps (str->pad);
    if (caps != NULL) {
      gst_pad_set_explicit_caps (dvd_demux->cur_audio, caps);
    }
  }
}


static void
gst_dvd_demux_set_cur_subpicture (GstDVDDemux * dvd_demux, gint stream_nr)
{
  GstMPEGStream *str;
  const GstCaps *caps = NULL;

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
    /* (Re)set the caps in the "current" pad. */
    caps = gst_pad_get_negotiated_caps (str->pad);
    if (caps != NULL) {
      gst_pad_set_explicit_caps (dvd_demux->cur_subpicture, caps);
    }
  }
}


gboolean
gst_dvd_demux_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dvddemux",
      GST_RANK_PRIMARY - 1, GST_TYPE_DVD_DEMUX);
}
