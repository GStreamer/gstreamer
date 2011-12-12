/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2006-2007> Jan Schmidt <thaytan@mad.scientist.com>
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
 * SECTION:element-mp3parse
 *
 * Parses and frames mpeg1 audio streams. Provides seeking.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.mp3 ! mp3parse ! mad ! autoaudiosink
 * ]|
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpegaudioparse.h"

#include <gst/glib-compat-private.h>

GST_DEBUG_CATEGORY_STATIC (mp3parse_debug);
#define GST_CAT_DEFAULT mp3parse_debug

#define MP3_CHANNEL_MODE_UNKNOWN -1
#define MP3_CHANNEL_MODE_STEREO 0
#define MP3_CHANNEL_MODE_JOINT_STEREO 1
#define MP3_CHANNEL_MODE_DUAL_CHANNEL 2
#define MP3_CHANNEL_MODE_MONO 3

#define CRC_UNKNOWN -1
#define CRC_PROTECTED 0
#define CRC_NOT_PROTECTED 1

#define XING_FRAMES_FLAG     0x0001
#define XING_BYTES_FLAG      0x0002
#define XING_TOC_FLAG        0x0004
#define XING_VBR_SCALE_FLAG  0x0008

#ifndef GST_READ_UINT24_BE
#define GST_READ_UINT24_BE(p) (p[2] | (p[1] << 8) | (p[0] << 16))
#endif

/* Minimum number of consecutive, valid-looking frames to consider
   for resyncing */
#define MIN_RESYNC_FRAMES 3

static inline MPEGAudioSeekEntry *
mpeg_audio_seek_entry_new (void)
{
  return g_slice_new (MPEGAudioSeekEntry);
}

static inline void
mpeg_audio_seek_entry_free (MPEGAudioSeekEntry * entry)
{
  g_slice_free (MPEGAudioSeekEntry, entry);
}

static GstStaticPadTemplate mp3_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) [ 8000, 48000 ], channels = (int) [ 1, 2 ],"
        "parsed=(boolean) true")
    );

static GstStaticPadTemplate mp3_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) 1, parsed=(boolean)false")
    );

/* GstMPEGAudioParse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SKIP,
  ARG_BIT_RATE
      /* FILL ME */
};


static gboolean gst_mp3parse_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mp3parse_chain (GstPad * pad, GstBuffer * buffer);
static gboolean mp3parse_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *mp3parse_get_query_types (GstPad * pad);
static gboolean mp3parse_src_event (GstPad * pad, GstEvent * event);

static int head_check (GstMPEGAudioParse * mp3parse, unsigned long head);

static void gst_mp3parse_dispose (GObject * object);
static void gst_mp3parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mp3parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_mp3parse_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn
gst_mp3parse_handle_data (GstMPEGAudioParse * mp3parse, gboolean at_eos);

static gboolean mp3parse_bytepos_to_time (GstMPEGAudioParse * mp3parse,
    gint64 bytepos, GstClockTime * ts, gboolean from_total_time);
static gboolean
mp3parse_total_bytes (GstMPEGAudioParse * mp3parse, gint64 * total);
static gboolean
mp3parse_total_time (GstMPEGAudioParse * mp3parse, GstClockTime * total);

GST_BOILERPLATE (GstMPEGAudioParse, gst_mp3parse, GstElement, GST_TYPE_ELEMENT);

#define GST_TYPE_MP3_CHANNEL_MODE (gst_mp3_channel_mode_get_type())

static const GEnumValue mp3_channel_mode[] = {
  {MP3_CHANNEL_MODE_UNKNOWN, "Unknown", "unknown"},
  {MP3_CHANNEL_MODE_MONO, "Mono", "mono"},
  {MP3_CHANNEL_MODE_DUAL_CHANNEL, "Dual Channel", "dual-channel"},
  {MP3_CHANNEL_MODE_JOINT_STEREO, "Joint Stereo", "joint-stereo"},
  {MP3_CHANNEL_MODE_STEREO, "Stereo", "stereo"},
  {0, NULL, NULL},
};

static GType
gst_mp3_channel_mode_get_type (void)
{
  static GType mp3_channel_mode_type = 0;

  if (!mp3_channel_mode_type) {
    mp3_channel_mode_type =
        g_enum_register_static ("GstMp3ChannelMode", mp3_channel_mode);
  }
  return mp3_channel_mode_type;
}

static const gchar *
gst_mp3_channel_mode_get_nick (gint mode)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS (mp3_channel_mode); i++) {
    if (mp3_channel_mode[i].value == mode)
      return mp3_channel_mode[i].value_nick;
  }
  return NULL;
}

static const guint mp3types_bitrates[2][3][16] = {
  {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}
      },
  {
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}
      },
};

static const guint mp3types_freqs[3][3] = { {44100, 48000, 32000},
{22050, 24000, 16000},
{11025, 12000, 8000}
};

static inline guint
mp3_type_frame_length_from_header (GstMPEGAudioParse * mp3parse, guint32 header,
    guint * put_version, guint * put_layer, guint * put_channels,
    guint * put_bitrate, guint * put_samplerate, guint * put_mode,
    guint * put_crc)
{
  guint length;
  gulong mode, samplerate, bitrate, layer, channels, padding, crc;
  gulong version;
  gint lsf, mpg25;

  if (header & (1 << 20)) {
    lsf = (header & (1 << 19)) ? 0 : 1;
    mpg25 = 0;
  } else {
    lsf = 1;
    mpg25 = 1;
  }

  version = 1 + lsf + mpg25;

  layer = 4 - ((header >> 17) & 0x3);

  crc = (header >> 16) & 0x1;

  bitrate = (header >> 12) & 0xF;
  bitrate = mp3types_bitrates[lsf][layer - 1][bitrate] * 1000;
  /* The caller has ensured we have a valid header, so bitrate can't be
     zero here. */
  g_assert (bitrate != 0);

  samplerate = (header >> 10) & 0x3;
  samplerate = mp3types_freqs[lsf + mpg25][samplerate];

  padding = (header >> 9) & 0x1;

  mode = (header >> 6) & 0x3;
  channels = (mode == 3) ? 1 : 2;

  switch (layer) {
    case 1:
      length = 4 * ((bitrate * 12) / samplerate + padding);
      break;
    case 2:
      length = (bitrate * 144) / samplerate + padding;
      break;
    default:
    case 3:
      length = (bitrate * 144) / (samplerate << lsf) + padding;
      break;
  }

  GST_DEBUG_OBJECT (mp3parse, "Calculated mp3 frame length of %u bytes",
      length);
  GST_DEBUG_OBJECT (mp3parse, "samplerate = %lu, bitrate = %lu, version = %lu, "
      "layer = %lu, channels = %lu, mode = %s", samplerate, bitrate, version,
      layer, channels, gst_mp3_channel_mode_get_nick (mode));

  if (put_version)
    *put_version = version;
  if (put_layer)
    *put_layer = layer;
  if (put_channels)
    *put_channels = channels;
  if (put_bitrate)
    *put_bitrate = bitrate;
  if (put_samplerate)
    *put_samplerate = samplerate;
  if (put_mode)
    *put_mode = mode;
  if (put_crc)
    *put_crc = crc;

  return length;
}

static GstCaps *
mp3_caps_create (guint version, guint layer, guint channels, guint samplerate)
{
  GstCaps *new;

  g_assert (version);
  g_assert (layer);
  g_assert (samplerate);
  g_assert (channels);

  new = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1,
      "mpegaudioversion", G_TYPE_INT, version,
      "layer", G_TYPE_INT, layer,
      "rate", G_TYPE_INT, samplerate,
      "channels", G_TYPE_INT, channels, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  return new;
}

static void
gst_mp3parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &mp3_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &mp3_src_template);

  GST_DEBUG_CATEGORY_INIT (mp3parse_debug, "mp3parse", 0, "MPEG Audio Parser");

  gst_element_class_set_details_simple (element_class, "MPEG1 Audio Parser",
      "Codec/Parser/Audio",
      "Parses and frames mpeg1 audio streams (levels 1-3), provides seek",
      "Jan Schmidt <thaytan@mad.scientist.com>,"
      "Erik Walthinsen <omega@cse.ogi.edu>");
}

static void
gst_mp3parse_class_init (GstMPEGAudioParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_mp3parse_set_property;
  gobject_class->get_property = gst_mp3parse_get_property;
  gobject_class->dispose = gst_mp3parse_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SKIP,
      g_param_spec_int ("skip", "skip", "skip",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_int ("bitrate", "Bitrate", "Bit Rate",
          G_MININT, G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_mp3parse_change_state;

/* register tags */
#define GST_TAG_CRC    "has-crc"
#define GST_TAG_MODE     "channel-mode"

  gst_tag_register (GST_TAG_CRC, GST_TAG_FLAG_META, G_TYPE_BOOLEAN,
      "has crc", "Using CRC", NULL);
  gst_tag_register (GST_TAG_MODE, GST_TAG_FLAG_ENCODED, G_TYPE_STRING,
      "channel mode", "MPEG audio channel mode", NULL);

  g_type_class_ref (GST_TYPE_MP3_CHANNEL_MODE);
}

static void
gst_mp3parse_reset (GstMPEGAudioParse * mp3parse)
{
  mp3parse->skip = 0;
  mp3parse->resyncing = TRUE;
  mp3parse->next_ts = GST_CLOCK_TIME_NONE;
  mp3parse->cur_offset = -1;

  mp3parse->sync_offset = 0;
  mp3parse->tracked_offset = 0;
  mp3parse->pending_ts = GST_CLOCK_TIME_NONE;
  mp3parse->pending_offset = -1;

  gst_adapter_clear (mp3parse->adapter);

  mp3parse->rate = mp3parse->channels = mp3parse->layer = -1;
  mp3parse->version = 1;
  mp3parse->max_bitreservoir = GST_CLOCK_TIME_NONE;

  mp3parse->avg_bitrate = 0;
  mp3parse->bitrate_sum = 0;
  mp3parse->last_posted_bitrate = 0;
  mp3parse->frame_count = 0;
  mp3parse->sent_codec_tag = FALSE;

  mp3parse->last_posted_crc = CRC_UNKNOWN;
  mp3parse->last_posted_channel_mode = MP3_CHANNEL_MODE_UNKNOWN;

  mp3parse->xing_flags = 0;
  mp3parse->xing_bitrate = 0;
  mp3parse->xing_frames = 0;
  mp3parse->xing_total_time = 0;
  mp3parse->xing_bytes = 0;
  mp3parse->xing_vbr_scale = 0;
  memset (mp3parse->xing_seek_table, 0, 100);
  memset (mp3parse->xing_seek_table_inverse, 0, 256);

  mp3parse->vbri_bitrate = 0;
  mp3parse->vbri_frames = 0;
  mp3parse->vbri_total_time = 0;
  mp3parse->vbri_bytes = 0;
  mp3parse->vbri_seek_points = 0;
  g_free (mp3parse->vbri_seek_table);
  mp3parse->vbri_seek_table = NULL;

  if (mp3parse->seek_table) {
    g_list_foreach (mp3parse->seek_table, (GFunc) mpeg_audio_seek_entry_free,
        NULL);
    g_list_free (mp3parse->seek_table);
    mp3parse->seek_table = NULL;
  }

  g_mutex_lock (mp3parse->pending_seeks_lock);
  if (mp3parse->pending_accurate_seeks) {
    g_slist_foreach (mp3parse->pending_accurate_seeks, (GFunc) g_free, NULL);
    g_slist_free (mp3parse->pending_accurate_seeks);
    mp3parse->pending_accurate_seeks = NULL;
  }
  if (mp3parse->pending_nonaccurate_seeks) {
    g_slist_foreach (mp3parse->pending_nonaccurate_seeks, (GFunc) g_free, NULL);
    g_slist_free (mp3parse->pending_nonaccurate_seeks);
    mp3parse->pending_nonaccurate_seeks = NULL;
  }
  g_mutex_unlock (mp3parse->pending_seeks_lock);

  if (mp3parse->pending_segment) {
    GstEvent **eventp = &mp3parse->pending_segment;

    gst_event_replace (eventp, NULL);
  }

  mp3parse->exact_position = FALSE;
  gst_segment_init (&mp3parse->segment, GST_FORMAT_TIME);
}

static void
gst_mp3parse_init (GstMPEGAudioParse * mp3parse, GstMPEGAudioParseClass * klass)
{
  mp3parse->sinkpad =
      gst_pad_new_from_static_template (&mp3_sink_template, "sink");
  gst_pad_set_event_function (mp3parse->sinkpad, gst_mp3parse_sink_event);
  gst_pad_set_chain_function (mp3parse->sinkpad, gst_mp3parse_chain);
  gst_element_add_pad (GST_ELEMENT (mp3parse), mp3parse->sinkpad);

  mp3parse->srcpad =
      gst_pad_new_from_static_template (&mp3_src_template, "src");
  gst_pad_use_fixed_caps (mp3parse->srcpad);
  gst_pad_set_event_function (mp3parse->srcpad, mp3parse_src_event);
  gst_pad_set_query_function (mp3parse->srcpad, mp3parse_src_query);
  gst_pad_set_query_type_function (mp3parse->srcpad, mp3parse_get_query_types);
  gst_element_add_pad (GST_ELEMENT (mp3parse), mp3parse->srcpad);

  mp3parse->adapter = gst_adapter_new ();
  mp3parse->pending_seeks_lock = g_mutex_new ();

  gst_mp3parse_reset (mp3parse);
}

static void
gst_mp3parse_dispose (GObject * object)
{
  GstMPEGAudioParse *mp3parse = GST_MP3PARSE (object);

  gst_mp3parse_reset (mp3parse);

  if (mp3parse->adapter) {
    g_object_unref (mp3parse->adapter);
    mp3parse->adapter = NULL;
  }
  g_mutex_free (mp3parse->pending_seeks_lock);
  mp3parse->pending_seeks_lock = NULL;

  g_list_foreach (mp3parse->pending_events, (GFunc) gst_mini_object_unref,
      NULL);
  g_list_free (mp3parse->pending_events);
  mp3parse->pending_events = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_mp3parse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMPEGAudioParse *mp3parse;
  GstEvent **eventp;

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, pos;
      gboolean update;
      MPEGAudioPendingAccurateSeek *seek = NULL;
      GSList *node;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);

      g_mutex_lock (mp3parse->pending_seeks_lock);
      if (format == GST_FORMAT_BYTES && mp3parse->pending_accurate_seeks) {

        for (node = mp3parse->pending_accurate_seeks; node; node = node->next) {
          MPEGAudioPendingAccurateSeek *tmp = node->data;

          if (tmp->upstream_start == pos) {
            seek = tmp;
            break;
          }
        }
        if (seek) {
          GstSegment *s = &seek->segment;

          event =
              gst_event_new_new_segment_full (FALSE, s->rate, s->applied_rate,
              GST_FORMAT_TIME, s->start, s->stop, s->last_stop);

          mp3parse->segment = seek->segment;

          mp3parse->resyncing = FALSE;
          mp3parse->cur_offset = pos;
          mp3parse->next_ts = seek->timestamp_start;
          mp3parse->pending_ts = GST_CLOCK_TIME_NONE;
          mp3parse->tracked_offset = 0;
          mp3parse->sync_offset = 0;

          gst_event_parse_new_segment_full (event, &update, &rate,
              &applied_rate, &format, &start, &stop, &pos);

          GST_DEBUG_OBJECT (mp3parse,
              "Pushing accurate newseg rate %g, applied rate %g, "
              "format %d, start %" G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT
              ", pos %" G_GINT64_FORMAT, rate, applied_rate, format, start,
              stop, pos);

          g_free (seek);
          mp3parse->pending_accurate_seeks =
              g_slist_delete_link (mp3parse->pending_accurate_seeks, node);

          g_mutex_unlock (mp3parse->pending_seeks_lock);
          res = gst_pad_push_event (mp3parse->srcpad, event);

          return res;
        } else {
          GST_WARNING_OBJECT (mp3parse,
              "Accurate seek not possible, didn't get an appropiate upstream segment");
        }
      }
      g_mutex_unlock (mp3parse->pending_seeks_lock);

      mp3parse->exact_position = FALSE;

      if (format == GST_FORMAT_BYTES) {
        GstClockTime seg_start, seg_stop, seg_pos;

        /* stop time is allowed to be open-ended, but not start & pos */
        if (!mp3parse_bytepos_to_time (mp3parse, stop, &seg_stop, FALSE))
          seg_stop = GST_CLOCK_TIME_NONE;
        if (mp3parse_bytepos_to_time (mp3parse, start, &seg_start, FALSE) &&
            mp3parse_bytepos_to_time (mp3parse, pos, &seg_pos, FALSE)) {
          gst_event_unref (event);

          /* search the pending nonaccurate seeks */
          g_mutex_lock (mp3parse->pending_seeks_lock);
          seek = NULL;
          for (node = mp3parse->pending_nonaccurate_seeks; node;
              node = node->next) {
            MPEGAudioPendingAccurateSeek *tmp = node->data;

            if (tmp->upstream_start == pos) {
              seek = tmp;
              break;
            }
          }

          if (seek) {
            if (seek->segment.stop == -1) {
              /* corrent the segment end, because non-accurate seeks might make
               * our streaming end earlier (see bug #603695) */
              seg_stop = -1;
            }
            g_free (seek);
            mp3parse->pending_nonaccurate_seeks =
                g_slist_delete_link (mp3parse->pending_nonaccurate_seeks, node);
          }
          g_mutex_unlock (mp3parse->pending_seeks_lock);

          event = gst_event_new_new_segment_full (update, rate, applied_rate,
              GST_FORMAT_TIME, seg_start, seg_stop, seg_pos);
          format = GST_FORMAT_TIME;
          GST_DEBUG_OBJECT (mp3parse, "Converted incoming segment to TIME. "
              "start = %" GST_TIME_FORMAT ", stop = %" GST_TIME_FORMAT
              ", pos = %" GST_TIME_FORMAT, GST_TIME_ARGS (seg_start),
              GST_TIME_ARGS (seg_stop), GST_TIME_ARGS (seg_pos));
        }
      }

      if (format != GST_FORMAT_TIME) {
        /* Unknown incoming segment format. Output a default open-ended 
         * TIME segment */
        gst_event_unref (event);
        event = gst_event_new_new_segment_full (update, rate, applied_rate,
            GST_FORMAT_TIME, 0, GST_CLOCK_TIME_NONE, 0);
      }

      mp3parse->resyncing = TRUE;
      mp3parse->cur_offset = -1;
      mp3parse->next_ts = GST_CLOCK_TIME_NONE;
      mp3parse->pending_ts = GST_CLOCK_TIME_NONE;
      mp3parse->tracked_offset = 0;
      mp3parse->sync_offset = 0;
      /* also clear leftover data if clearing so much state */
      gst_adapter_clear (mp3parse->adapter);

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);
      GST_DEBUG_OBJECT (mp3parse, "Pushing newseg rate %g, applied rate %g, "
          "format %d, start %" G_GINT64_FORMAT ", stop %" G_GINT64_FORMAT
          ", pos %" G_GINT64_FORMAT, rate, applied_rate, format, start, stop,
          pos);

      gst_segment_set_newsegment_full (&mp3parse->segment, update, rate,
          applied_rate, format, start, stop, pos);

      /* save the segment for later, right before we push a new buffer so that
       * the caps are fixed and the next linked element can receive the segment. */
      eventp = &mp3parse->pending_segment;
      gst_event_replace (eventp, event);
      gst_event_unref (event);
      res = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      /* Clear our adapter and set up for a new position */
      gst_adapter_clear (mp3parse->adapter);
      eventp = &mp3parse->pending_segment;
      gst_event_replace (eventp, NULL);
      res = gst_pad_push_event (mp3parse->srcpad, event);
      break;
    case GST_EVENT_EOS:
      /* If we haven't processed any frames yet, then make sure we process
         at least whatever's in our adapter */
      if (mp3parse->frame_count == 0) {
        gst_mp3parse_handle_data (mp3parse, TRUE);

        /* If we STILL have zero frames processed, fire an error */
        if (mp3parse->frame_count == 0) {
          GST_ELEMENT_ERROR (mp3parse, STREAM, WRONG_TYPE,
              ("No valid frames found before end of stream"), (NULL));
        }
      }
      /* fall through */
    default:
      if (mp3parse->pending_segment &&
          (GST_EVENT_TYPE (event) != GST_EVENT_EOS) &&
          (GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_START)) {
        /* Cache all events except EOS and the ones above if we have
         * a pending segment */
        mp3parse->pending_events =
            g_list_append (mp3parse->pending_events, event);
      } else {
        res = gst_pad_push_event (mp3parse->srcpad, event);
      }
      break;
  }

  gst_object_unref (mp3parse);

  return res;
}

static void
gst_mp3parse_add_index_entry (GstMPEGAudioParse * mp3parse, guint64 offset,
    GstClockTime ts)
{
  MPEGAudioSeekEntry *entry, *last;

  if (G_LIKELY (mp3parse->seek_table != NULL)) {
    last = mp3parse->seek_table->data;

    if (last->byte >= offset)
      return;

    if (GST_CLOCK_DIFF (last->timestamp, ts) < mp3parse->idx_interval)
      return;
  }

  entry = mpeg_audio_seek_entry_new ();
  entry->byte = offset;
  entry->timestamp = ts;
  mp3parse->seek_table = g_list_prepend (mp3parse->seek_table, entry);

  GST_LOG_OBJECT (mp3parse, "Adding index entry %" GST_TIME_FORMAT " @ offset "
      "0x%08" G_GINT64_MODIFIER "x", GST_TIME_ARGS (ts), offset);
}

/* Prepare a buffer of the indicated size, timestamp it and output */
static GstFlowReturn
gst_mp3parse_emit_frame (GstMPEGAudioParse * mp3parse, guint size,
    guint mode, guint crc)
{
  GstBuffer *outbuf;
  guint bitrate;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime push_start;
  GstTagList *taglist;

  outbuf = gst_adapter_take_buffer (mp3parse->adapter, size);

  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale (GST_SECOND, mp3parse->spf, mp3parse->rate);

  GST_BUFFER_OFFSET (outbuf) = mp3parse->cur_offset;

  /* Check if we have a pending timestamp from an incoming buffer to apply
   * here */
  if (GST_CLOCK_TIME_IS_VALID (mp3parse->pending_ts)) {
    if (mp3parse->tracked_offset >= mp3parse->pending_offset) {
      /* If the incoming timestamp differs from our expected by more than 
       * half a frame, then take it instead of our calculated timestamp.
       * This avoids creating imperfect streams just because of 
       * quantization in the container timestamping */
      GstClockTimeDiff diff = mp3parse->next_ts - mp3parse->pending_ts;
      GstClockTimeDiff thresh = GST_BUFFER_DURATION (outbuf) / 2;

      if (diff < -thresh || diff > thresh) {
        GST_DEBUG_OBJECT (mp3parse, "Updating next_ts from %" GST_TIME_FORMAT
            " to pending ts %" GST_TIME_FORMAT
            " at offset %" G_GINT64_FORMAT " (pending offset was %"
            G_GINT64_FORMAT ")", GST_TIME_ARGS (mp3parse->next_ts),
            GST_TIME_ARGS (mp3parse->pending_ts), mp3parse->tracked_offset,
            mp3parse->pending_offset);
        mp3parse->next_ts = mp3parse->pending_ts;
      }
      mp3parse->pending_ts = GST_CLOCK_TIME_NONE;
    }
  }

  /* Decide what timestamp we're going to apply */
  if (GST_CLOCK_TIME_IS_VALID (mp3parse->next_ts)) {
    GST_BUFFER_TIMESTAMP (outbuf) = mp3parse->next_ts;
  } else {
    GstClockTime ts;

    /* No timestamp yet, convert our offset to a timestamp if we can, or
     * start at 0 */
    if (mp3parse_bytepos_to_time (mp3parse, mp3parse->cur_offset, &ts, FALSE) &&
        GST_CLOCK_TIME_IS_VALID (ts))
      GST_BUFFER_TIMESTAMP (outbuf) = ts;
    else {
      GST_BUFFER_TIMESTAMP (outbuf) = 0;
    }
  }

  if (GST_BUFFER_TIMESTAMP (outbuf) == 0)
    mp3parse->exact_position = TRUE;

  if (mp3parse->seekable &&
      mp3parse->exact_position && GST_BUFFER_TIMESTAMP_IS_VALID (outbuf) &&
      mp3parse->cur_offset != GST_BUFFER_OFFSET_NONE) {
    gst_mp3parse_add_index_entry (mp3parse, mp3parse->cur_offset,
        GST_BUFFER_TIMESTAMP (outbuf));
  }

  /* Update our byte offset tracking */
  if (mp3parse->cur_offset != -1) {
    mp3parse->cur_offset += size;
  }
  mp3parse->tracked_offset += size;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (outbuf))
    mp3parse->next_ts =
        GST_BUFFER_TIMESTAMP (outbuf) + GST_BUFFER_DURATION (outbuf);

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (mp3parse->srcpad));

  /* Post a bitrate tag if we need to before pushing the buffer */
  if (mp3parse->xing_bitrate != 0)
    bitrate = mp3parse->xing_bitrate;
  else if (mp3parse->vbri_bitrate != 0)
    bitrate = mp3parse->vbri_bitrate;
  else
    bitrate = mp3parse->avg_bitrate;

  /* we will create a taglist (if any of the parameters has changed)
   * to add the tags that changed */
  taglist = NULL;
  if ((mp3parse->last_posted_bitrate / 10000) != (bitrate / 10000)) {
    taglist = gst_tag_list_new ();
    mp3parse->last_posted_bitrate = bitrate;
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_BITRATE,
        mp3parse->last_posted_bitrate, NULL);

    /* Post a new duration message if the average bitrate changes that much
     * so applications can update their cached values
     */
    if ((mp3parse->xing_flags & XING_TOC_FLAG) == 0
        && mp3parse->vbri_total_time == 0) {
      gst_element_post_message (GST_ELEMENT (mp3parse),
          gst_message_new_duration (GST_OBJECT (mp3parse), GST_FORMAT_TIME,
              -1));
    }
  }

  if (mp3parse->last_posted_crc != crc) {
    gboolean using_crc;

    if (!taglist) {
      taglist = gst_tag_list_new ();
    }
    mp3parse->last_posted_crc = crc;
    if (mp3parse->last_posted_crc == CRC_PROTECTED) {
      using_crc = TRUE;
    } else {
      using_crc = FALSE;
    }
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_CRC,
        using_crc, NULL);
  }

  if (mp3parse->last_posted_channel_mode != mode) {
    if (!taglist) {
      taglist = gst_tag_list_new ();
    }
    mp3parse->last_posted_channel_mode = mode;

    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_MODE,
        gst_mp3_channel_mode_get_nick (mode), NULL);
  }

  /* if the taglist exists, we need to send it */
  if (taglist) {
    gst_element_found_tags_for_pad (GST_ELEMENT (mp3parse),
        mp3parse->srcpad, taglist);
  }

  /* We start pushing 9 frames earlier (29 frames for MPEG2) than
   * segment start to be able to decode the first frame we want.
   * 9 (29) frames are the theoretical maximum of frames that contain
   * data for the current frame (bit reservoir).
   */
  if (mp3parse->segment.start == 0) {
    push_start = 0;
  } else if (GST_CLOCK_TIME_IS_VALID (mp3parse->max_bitreservoir)) {
    if (GST_CLOCK_TIME_IS_VALID (mp3parse->segment.start) &&
        mp3parse->segment.start > mp3parse->max_bitreservoir)
      push_start = mp3parse->segment.start - mp3parse->max_bitreservoir;
    else
      push_start = 0;
  } else {
    push_start = mp3parse->segment.start;
  }

  if (G_UNLIKELY ((GST_CLOCK_TIME_IS_VALID (push_start) &&
              GST_BUFFER_TIMESTAMP_IS_VALID (outbuf) &&
              GST_BUFFER_TIMESTAMP (outbuf) + GST_BUFFER_DURATION (outbuf)
              < push_start))) {
    GST_DEBUG_OBJECT (mp3parse,
        "Buffer before configured segment range %" GST_TIME_FORMAT
        " to %" GST_TIME_FORMAT ", dropping, timestamp %"
        GST_TIME_FORMAT " duration %" GST_TIME_FORMAT
        ", offset 0x%08" G_GINT64_MODIFIER "x", GST_TIME_ARGS (push_start),
        GST_TIME_ARGS (mp3parse->segment.stop),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
        GST_BUFFER_OFFSET (outbuf));

    gst_buffer_unref (outbuf);
    ret = GST_FLOW_OK;
  } else if (G_UNLIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (outbuf) &&
          GST_CLOCK_TIME_IS_VALID (mp3parse->segment.stop) &&
          GST_BUFFER_TIMESTAMP (outbuf) >=
          mp3parse->segment.stop + GST_BUFFER_DURATION (outbuf))) {
    /* Some mp3 streams have an offset in the timestamps, for which we have to
     * push the frame *after* the end position in order for the decoder to be
     * able to decode everything up until the segment.stop position.
     * That is the reason of the calculated offset */
    GST_DEBUG_OBJECT (mp3parse,
        "Buffer after configured segment range %" GST_TIME_FORMAT " to %"
        GST_TIME_FORMAT ", returning GST_FLOW_UNEXPECTED, timestamp %"
        GST_TIME_FORMAT " duration %" GST_TIME_FORMAT ", offset 0x%08"
        G_GINT64_MODIFIER "x", GST_TIME_ARGS (push_start),
        GST_TIME_ARGS (mp3parse->segment.stop),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
        GST_BUFFER_OFFSET (outbuf));

    gst_buffer_unref (outbuf);
    ret = GST_FLOW_UNEXPECTED;
  } else {
    GST_DEBUG_OBJECT (mp3parse,
        "pushing buffer of %d bytes, timestamp %" GST_TIME_FORMAT
        ", offset 0x%08" G_GINT64_MODIFIER "x", size,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_BUFFER_OFFSET (outbuf));
    mp3parse->segment.last_stop = GST_BUFFER_TIMESTAMP (outbuf);
    /* push any pending segment now */
    if (mp3parse->pending_segment) {
      gst_pad_push_event (mp3parse->srcpad, mp3parse->pending_segment);
      mp3parse->pending_segment = NULL;
    }
    if (mp3parse->pending_events) {
      GList *l;

      for (l = mp3parse->pending_events; l != NULL; l = l->next) {
        gst_pad_push_event (mp3parse->srcpad, GST_EVENT (l->data));
      }
      g_list_free (mp3parse->pending_events);
      mp3parse->pending_events = NULL;
    }

    /* set discont if needed */
    if (mp3parse->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      mp3parse->discont = FALSE;
    }

    ret = gst_pad_push (mp3parse->srcpad, outbuf);
  }

  return ret;
}

static void
gst_mp3parse_handle_first_frame (GstMPEGAudioParse * mp3parse)
{
  GstTagList *taglist;
  gchar *codec;
  const guint32 xing_id = 0x58696e67;   /* 'Xing' in hex */
  const guint32 info_id = 0x496e666f;   /* 'Info' in hex - found in LAME CBR files */
  const guint32 vbri_id = 0x56425249;   /* 'VBRI' in hex */

  gint offset;

  guint64 avail;
  gint64 upstream_total_bytes = 0;
  guint32 read_id;
  const guint8 *data;

  /* Output codec tag */
  if (!mp3parse->sent_codec_tag) {
    if (mp3parse->layer == 3) {
      codec = g_strdup_printf ("MPEG %d Audio, Layer %d (MP3)",
          mp3parse->version, mp3parse->layer);
    } else {
      codec = g_strdup_printf ("MPEG %d Audio, Layer %d",
          mp3parse->version, mp3parse->layer);
    }

    taglist = gst_tag_list_new ();
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_AUDIO_CODEC, codec, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (mp3parse),
        mp3parse->srcpad, taglist);
    g_free (codec);

    mp3parse->sent_codec_tag = TRUE;
  }
  /* end setting the tag */

  /* Check first frame for Xing info */
  if (mp3parse->version == 1) { /* MPEG-1 file */
    if (mp3parse->channels == 1)
      offset = 0x11;
    else
      offset = 0x20;
  } else {                      /* MPEG-2 header */
    if (mp3parse->channels == 1)
      offset = 0x09;
    else
      offset = 0x11;
  }
  /* Skip the 4 bytes of the MP3 header too */
  offset += 4;

  /* Check if we have enough data to read the Xing header */
  avail = gst_adapter_available (mp3parse->adapter);

  if (avail < offset + 8)
    return;

  data = gst_adapter_peek (mp3parse->adapter, offset + 8);
  if (data == NULL)
    return;
  /* The header starts at the provided offset */
  data += offset;

  /* obtain real upstream total bytes */
  mp3parse_total_bytes (mp3parse, &upstream_total_bytes);

  read_id = GST_READ_UINT32_BE (data);
  if (read_id == xing_id || read_id == info_id) {
    guint32 xing_flags;
    guint bytes_needed = offset + 8;
    gint64 total_bytes;
    GstClockTime total_time;

    GST_DEBUG_OBJECT (mp3parse, "Found Xing header marker 0x%x", xing_id);

    /* Read 4 base bytes of flags, big-endian */
    xing_flags = GST_READ_UINT32_BE (data + 4);
    if (xing_flags & XING_FRAMES_FLAG)
      bytes_needed += 4;
    if (xing_flags & XING_BYTES_FLAG)
      bytes_needed += 4;
    if (xing_flags & XING_TOC_FLAG)
      bytes_needed += 100;
    if (xing_flags & XING_VBR_SCALE_FLAG)
      bytes_needed += 4;
    if (avail < bytes_needed) {
      GST_DEBUG_OBJECT (mp3parse,
          "Not enough data to read Xing header (need %d)", bytes_needed);
      return;
    }

    GST_DEBUG_OBJECT (mp3parse, "Reading Xing header");
    mp3parse->xing_flags = xing_flags;
    data = gst_adapter_peek (mp3parse->adapter, bytes_needed);
    data += offset + 8;

    if (xing_flags & XING_FRAMES_FLAG) {
      mp3parse->xing_frames = GST_READ_UINT32_BE (data);
      if (mp3parse->xing_frames == 0) {
        GST_WARNING_OBJECT (mp3parse,
            "Invalid number of frames in Xing header");
        mp3parse->xing_flags &= ~XING_FRAMES_FLAG;
      } else {
        mp3parse->xing_total_time = gst_util_uint64_scale (GST_SECOND,
            (guint64) (mp3parse->xing_frames) * (mp3parse->spf),
            mp3parse->rate);
      }

      data += 4;
    } else {
      mp3parse->xing_frames = 0;
      mp3parse->xing_total_time = 0;
    }

    if (xing_flags & XING_BYTES_FLAG) {
      mp3parse->xing_bytes = GST_READ_UINT32_BE (data);
      if (mp3parse->xing_bytes == 0) {
        GST_WARNING_OBJECT (mp3parse, "Invalid number of bytes in Xing header");
        mp3parse->xing_flags &= ~XING_BYTES_FLAG;
      }

      data += 4;
    } else {
      mp3parse->xing_bytes = 0;
    }

    /* If we know the upstream size and duration, compute the
     * total bitrate, rounded up to the nearest kbit/sec */
    if ((total_time = mp3parse->xing_total_time) &&
        (total_bytes = mp3parse->xing_bytes)) {
      mp3parse->xing_bitrate = gst_util_uint64_scale (total_bytes,
          8 * GST_SECOND, total_time);
      mp3parse->xing_bitrate += 500;
      mp3parse->xing_bitrate -= mp3parse->xing_bitrate % 1000;
    }

    if (xing_flags & XING_TOC_FLAG) {
      int i, percent = 0;
      guchar *table = mp3parse->xing_seek_table;
      guchar old = 0, new;
      guint first;

      first = data[0];
      GST_DEBUG_OBJECT (mp3parse,
          "Subtracting initial offset of %d bytes from Xing TOC", first);

      /* xing seek table: percent time -> 1/256 bytepos */
      for (i = 0; i < 100; i++) {
        new = data[i] - first;
        if (old > new) {
          GST_WARNING_OBJECT (mp3parse, "Skipping broken Xing TOC");
          mp3parse->xing_flags &= ~XING_TOC_FLAG;
          goto skip_toc;
        }
        mp3parse->xing_seek_table[i] = old = new;
      }

      /* build inverse table: 1/256 bytepos -> 1/100 percent time */
      for (i = 0; i < 256; i++) {
        while (percent < 99 && table[percent + 1] <= i)
          percent++;

        if (table[percent] == i) {
          mp3parse->xing_seek_table_inverse[i] = percent * 100;
        } else if (table[percent] < i && percent < 99) {
          gdouble fa, fb, fx;
          gint a = percent, b = percent + 1;

          fa = table[a];
          fb = table[b];
          fx = (b - a) / (fb - fa) * (i - fa) + a;
          mp3parse->xing_seek_table_inverse[i] = (guint16) (fx * 100);
        } else if (percent == 99) {
          gdouble fa, fb, fx;
          gint a = percent, b = 100;

          fa = table[a];
          fb = 256.0;
          fx = (b - a) / (fb - fa) * (i - fa) + a;
          mp3parse->xing_seek_table_inverse[i] = (guint16) (fx * 100);
        }
      }
    skip_toc:
      data += 100;
    } else {
      memset (mp3parse->xing_seek_table, 0, 100);
      memset (mp3parse->xing_seek_table_inverse, 0, 256);
    }

    if (xing_flags & XING_VBR_SCALE_FLAG) {
      mp3parse->xing_vbr_scale = GST_READ_UINT32_BE (data);
    } else
      mp3parse->xing_vbr_scale = 0;

    GST_DEBUG_OBJECT (mp3parse, "Xing header reported %u frames, time %"
        GST_TIME_FORMAT ", %u bytes, vbr scale %u", mp3parse->xing_frames,
        GST_TIME_ARGS (mp3parse->xing_total_time), mp3parse->xing_bytes,
        mp3parse->xing_vbr_scale);

    /* check for truncated file */
    if (upstream_total_bytes && mp3parse->xing_bytes &&
        mp3parse->xing_bytes * 0.8 > upstream_total_bytes) {
      GST_WARNING_OBJECT (mp3parse, "File appears to have been truncated; "
          "invalidating Xing header duration and size");
      mp3parse->xing_flags &= ~XING_BYTES_FLAG;
      mp3parse->xing_flags &= ~XING_FRAMES_FLAG;
    }
  } else if (read_id == vbri_id) {
    gint64 total_bytes, total_frames;
    GstClockTime total_time;
    guint16 nseek_points;

    GST_DEBUG_OBJECT (mp3parse, "Found VBRI header marker 0x%x", vbri_id);
    if (avail < offset + 26) {
      GST_DEBUG_OBJECT (mp3parse,
          "Not enough data to read VBRI header (need %d)", offset + 26);
      return;
    }

    GST_DEBUG_OBJECT (mp3parse, "Reading VBRI header");
    data = gst_adapter_peek (mp3parse->adapter, offset + 26);
    data += offset + 4;

    if (GST_READ_UINT16_BE (data) != 0x0001) {
      GST_WARNING_OBJECT (mp3parse,
          "Unsupported VBRI version 0x%x", GST_READ_UINT16_BE (data));
      return;
    }
    data += 2;

    /* Skip encoder delay */
    data += 2;

    /* Skip quality */
    data += 2;

    total_bytes = GST_READ_UINT32_BE (data);
    if (total_bytes != 0)
      mp3parse->vbri_bytes = total_bytes;
    data += 4;

    total_frames = GST_READ_UINT32_BE (data);
    if (total_frames != 0) {
      mp3parse->vbri_frames = total_frames;
      mp3parse->vbri_total_time = gst_util_uint64_scale (GST_SECOND,
          (guint64) (mp3parse->vbri_frames) * (mp3parse->spf), mp3parse->rate);
    }
    data += 4;

    /* If we know the upstream size and duration, compute the 
     * total bitrate, rounded up to the nearest kbit/sec */
    if ((total_time = mp3parse->vbri_total_time) &&
        (total_bytes = mp3parse->vbri_bytes)) {
      mp3parse->vbri_bitrate = gst_util_uint64_scale (total_bytes,
          8 * GST_SECOND, total_time);
      mp3parse->vbri_bitrate += 500;
      mp3parse->vbri_bitrate -= mp3parse->vbri_bitrate % 1000;
    }

    nseek_points = GST_READ_UINT16_BE (data);
    data += 2;

    if (nseek_points > 0) {
      guint scale, seek_bytes, seek_frames;
      gint i;

      mp3parse->vbri_seek_points = nseek_points;

      scale = GST_READ_UINT16_BE (data);
      data += 2;

      seek_bytes = GST_READ_UINT16_BE (data);
      data += 2;

      seek_frames = GST_READ_UINT16_BE (data);

      if (scale == 0 || seek_bytes == 0 || seek_bytes > 4 || seek_frames == 0) {
        GST_WARNING_OBJECT (mp3parse, "Unsupported VBRI seek table");
        goto out_vbri;
      }

      if (avail < offset + 26 + nseek_points * seek_bytes) {
        GST_WARNING_OBJECT (mp3parse,
            "Not enough data to read VBRI seek table (need %d)",
            offset + 26 + nseek_points * seek_bytes);
        goto out_vbri;
      }

      if (seek_frames * nseek_points < total_frames - seek_frames ||
          seek_frames * nseek_points > total_frames + seek_frames) {
        GST_WARNING_OBJECT (mp3parse,
            "VBRI seek table doesn't cover the complete file");
        goto out_vbri;
      }

      data =
          gst_adapter_peek (mp3parse->adapter,
          offset + 26 + nseek_points * seek_bytes);
      data += offset + 26;


      /* VBRI seek table: frame/seek_frames -> byte */
      mp3parse->vbri_seek_table = g_new (guint32, nseek_points);
      if (seek_bytes == 4)
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT32_BE (data) * scale;
          data += 4;
      } else if (seek_bytes == 3)
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT24_BE (data) * scale;
          data += 3;
      } else if (seek_bytes == 2)
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT16_BE (data) * scale;
          data += 2;
      } else                    /* seek_bytes == 1 */
        for (i = 0; i < nseek_points; i++) {
          mp3parse->vbri_seek_table[i] = GST_READ_UINT8 (data) * scale;
          data += 1;
        }
    }
  out_vbri:

    GST_DEBUG_OBJECT (mp3parse, "VBRI header reported %u frames, time %"
        GST_TIME_FORMAT ", bytes %u", mp3parse->vbri_frames,
        GST_TIME_ARGS (mp3parse->vbri_total_time), mp3parse->vbri_bytes);

    /* check for truncated file */
    if (upstream_total_bytes && mp3parse->vbri_bytes &&
        mp3parse->vbri_bytes * 0.8 > upstream_total_bytes) {
      GST_WARNING_OBJECT (mp3parse, "File appears to have been truncated; "
          "invalidating VBRI header duration and size");
      mp3parse->vbri_valid = FALSE;
    } else {
      mp3parse->vbri_valid = TRUE;
    }
  } else {
    GST_DEBUG_OBJECT (mp3parse,
        "Xing, LAME or VBRI header not found in first frame");
  }
}

static void
gst_mp3parse_check_seekability (GstMPEGAudioParse * mp3parse)
{
  GstQuery *query;
  gboolean seekable = FALSE;
  gint64 start = -1, stop = -1;
  guint idx_interval = 0;

  query = gst_query_new_seeking (GST_FORMAT_BYTES);
  if (!gst_pad_peer_query (mp3parse->sinkpad, query)) {
    GST_DEBUG_OBJECT (mp3parse, "seeking query failed");
    goto done;
  }

  gst_query_parse_seeking (query, NULL, &seekable, &start, &stop);

  /* try harder to query upstream size if we didn't get it the first time */
  if (seekable && stop == -1) {
    GstFormat fmt = GST_FORMAT_BYTES;

    GST_DEBUG_OBJECT (mp3parse, "doing duration query to fix up unset stop");
    gst_pad_query_peer_duration (mp3parse->sinkpad, &fmt, &stop);
  }

  /* if upstream doesn't know the size, it's likely that it's not seekable in
   * practice even if it technically may be seekable */
  if (seekable && (start != 0 || stop <= start)) {
    GST_DEBUG_OBJECT (mp3parse, "seekable but unknown start/stop -> disable");
    seekable = FALSE;
  }

  /* let's not put every single frame into our index */
  if (seekable) {
    if (stop < 10 * 1024 * 1024)
      idx_interval = 100;
    else if (stop < 100 * 1024 * 1024)
      idx_interval = 500;
    else
      idx_interval = 1000;
  }

done:

  GST_INFO_OBJECT (mp3parse, "seekable: %d (%" G_GUINT64_FORMAT " - %"
      G_GUINT64_FORMAT ")", seekable, start, stop);
  mp3parse->seekable = seekable;

  GST_INFO_OBJECT (mp3parse, "idx_interval: %ums", idx_interval);
  mp3parse->idx_interval = idx_interval * GST_MSECOND;

  gst_query_unref (query);
}

/* Flush some number of bytes and update tracked offsets */
static void
gst_mp3parse_flush_bytes (GstMPEGAudioParse * mp3parse, int bytes)
{
  gst_adapter_flush (mp3parse->adapter, bytes);
  if (mp3parse->cur_offset != -1)
    mp3parse->cur_offset += bytes;
  mp3parse->tracked_offset += bytes;
}

/* Perform extended validation to check that subsequent headers match
   the first header given here in important characteristics, to avoid
   false sync. We look for a minimum of MIN_RESYNC_FRAMES consecutive
   frames to match their major characteristics.

   If at_eos is set to TRUE, we just check that we don't find any invalid
   frames in whatever data is available, rather than requiring a full
   MIN_RESYNC_FRAMES of data.

   Returns TRUE if we've seen enough data to validate or reject the frame.
   If TRUE is returned, then *valid contains TRUE if it validated, or false
   if we decided it was false sync.
 */
static gboolean
gst_mp3parse_validate_extended (GstMPEGAudioParse * mp3parse, guint32 header,
    int bpf, gboolean at_eos, gboolean * valid)
{
  guint32 next_header;
  const guint8 *data;
  guint available;
  int frames_found = 1;
  int offset = bpf;

  while (frames_found < MIN_RESYNC_FRAMES) {
    /* Check if we have enough data for all these frames, plus the next
       frame header. */
    available = gst_adapter_available (mp3parse->adapter);
    if (available < offset + 4) {
      if (at_eos) {
        /* Running out of data at EOS is fine; just accept it */
        *valid = TRUE;
        return TRUE;
      } else {
        return FALSE;
      }
    }

    data = gst_adapter_peek (mp3parse->adapter, offset + 4);
    next_header = GST_READ_UINT32_BE (data + offset);
    GST_DEBUG_OBJECT (mp3parse, "At %d: header=%08X, header2=%08X, bpf=%d",
        offset, (unsigned int) header, (unsigned int) next_header, bpf);

/* mask the bits which are allowed to differ between frames */
#define HDRMASK ~((0xF << 12)  /* bitrate */ | \
                  (0x1 <<  9)  /* padding */ | \
                  (0xf <<  4)  /* mode|mode extension */ | \
                  (0xf))        /* copyright|emphasis */

    if ((next_header & HDRMASK) != (header & HDRMASK)) {
      /* If any of the unmasked bits don't match, then it's not valid */
      GST_DEBUG_OBJECT (mp3parse, "next header doesn't match "
          "(header=%08X (%08X), header2=%08X (%08X), bpf=%d)",
          (guint) header, (guint) header & HDRMASK, (guint) next_header,
          (guint) next_header & HDRMASK, bpf);
      *valid = FALSE;
      return TRUE;
    } else if ((((next_header >> 12) & 0xf) == 0) ||
        (((next_header >> 12) & 0xf) == 0xf)) {
      /* The essential parts were the same, but the bitrate held an
         invalid value - also reject */
      GST_DEBUG_OBJECT (mp3parse, "next header invalid (bitrate)");
      *valid = FALSE;
      return TRUE;
    }

    bpf = mp3_type_frame_length_from_header (mp3parse, next_header,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    offset += bpf;
    frames_found++;
  }

  *valid = TRUE;
  return TRUE;
}

static GstFlowReturn
gst_mp3parse_handle_data (GstMPEGAudioParse * mp3parse, gboolean at_eos)
{
  GstFlowReturn flow = GST_FLOW_OK;
  const guchar *data;
  guint32 header;
  int bpf;
  guint available;
  guint bitrate, layer, rate, channels, version, mode, crc;
  gboolean caps_change;

  /* while we still have at least 4 bytes (for the header) available */
  while (gst_adapter_available (mp3parse->adapter) >= 4) {
    /* Get the header bytes, check if they're potentially valid */
    data = gst_adapter_peek (mp3parse->adapter, 4);
    header = GST_READ_UINT32_BE (data);

    if (!head_check (mp3parse, header)) {
      /* Not a valid MP3 header; we start looking forward byte-by-byte trying to
         find a place to resync */
      if (!mp3parse->resyncing)
        mp3parse->sync_offset = mp3parse->tracked_offset;
      mp3parse->resyncing = TRUE;
      gst_mp3parse_flush_bytes (mp3parse, 1);
      GST_DEBUG_OBJECT (mp3parse, "wrong header, skipping byte");
      continue;
    }

    /* We have a potentially valid header.
       If this is just a normal 'next frame', we go ahead and output it.

       However, sometimes, we do additional validation to ensure we haven't
       got false sync (common with mp3 due to the short sync word).
       The additional validation requires that we find several consecutive mp3
       frames with the same major parameters, or reach EOS with a smaller
       number of valid-looking frames.

       We do this if:
       - This is the very first frame we've processed
       - We're resyncing after a non-accurate seek, or after losing sync
       due to invalid data.
       - The format of the stream changes in a major way (number of channels,
       sample rate, layer, or mpeg version).
     */
    available = gst_adapter_available (mp3parse->adapter);

    if (G_UNLIKELY (mp3parse->resyncing &&
            mp3parse->tracked_offset - mp3parse->sync_offset > 2 * 1024 * 1024))
      goto sync_failure;

    bpf = mp3_type_frame_length_from_header (mp3parse, header,
        &version, &layer, &channels, &bitrate, &rate, &mode, &crc);
    g_assert (bpf != 0);

    if (channels != mp3parse->channels ||
        rate != mp3parse->rate || layer != mp3parse->layer ||
        version != mp3parse->version)
      caps_change = TRUE;
    else
      caps_change = FALSE;

    if (mp3parse->resyncing || caps_change) {
      gboolean valid;
      if (!gst_mp3parse_validate_extended (mp3parse, header, bpf, at_eos,
              &valid)) {
        /* Not enough data to validate; wait for more */
        break;
      }

      if (!valid) {
        /* Extended validation failed; we probably got false sync.
           Continue searching from the next byte in the stream */
        if (!mp3parse->resyncing)
          mp3parse->sync_offset = mp3parse->tracked_offset;
        mp3parse->resyncing = TRUE;
        gst_mp3parse_flush_bytes (mp3parse, 1);
        continue;
      }
    }

    /* if we don't have the whole frame... */
    if (available < bpf) {
      GST_DEBUG_OBJECT (mp3parse, "insufficient data available, need "
          "%d bytes, have %d", bpf, available);
      break;
    }

    if (caps_change) {
      GstCaps *caps;

      caps = mp3_caps_create (version, layer, channels, rate);
      gst_pad_set_caps (mp3parse->srcpad, caps);
      gst_caps_unref (caps);

      mp3parse->channels = channels;
      mp3parse->rate = rate;

      mp3parse->layer = layer;
      mp3parse->version = version;

      /* see http://www.codeproject.com/audio/MPEGAudioInfo.asp */
      if (mp3parse->layer == 1)
        mp3parse->spf = 384;
      else if (mp3parse->layer == 2)
        mp3parse->spf = 1152;
      else if (mp3parse->version == 1) {
        mp3parse->spf = 1152;
      } else {
        /* MPEG-2 or "2.5" */
        mp3parse->spf = 576;
      }

      mp3parse->max_bitreservoir = gst_util_uint64_scale (GST_SECOND,
          ((version == 1) ? 10 : 30) * mp3parse->spf, mp3parse->rate);
    }

    mp3parse->bit_rate = bitrate;

    /* Check the first frame for a Xing header to get our total length */
    if (mp3parse->frame_count == 0) {
      /* For the first frame in the file, look for a Xing frame after 
       * the header, and output a codec tag */
      gst_mp3parse_handle_first_frame (mp3parse);

      /* Check if we're seekable */
      gst_mp3parse_check_seekability (mp3parse);
    }

    /* Update VBR stats */
    mp3parse->bitrate_sum += mp3parse->bit_rate;
    mp3parse->frame_count++;
    /* Compute the average bitrate, rounded up to the nearest 1000 bits */
    mp3parse->avg_bitrate =
        (mp3parse->bitrate_sum / mp3parse->frame_count + 500);
    mp3parse->avg_bitrate -= mp3parse->avg_bitrate % 1000;

    if (!mp3parse->skip) {
      mp3parse->resyncing = FALSE;
      flow = gst_mp3parse_emit_frame (mp3parse, bpf, mode, crc);
      if (flow != GST_FLOW_OK)
        break;
    } else {
      GST_DEBUG_OBJECT (mp3parse, "skipping buffer of %d bytes", bpf);
      gst_mp3parse_flush_bytes (mp3parse, bpf);
      mp3parse->skip--;
    }
  }

  return flow;

  /* ERRORS */
sync_failure:
  {
    GST_ELEMENT_ERROR (mp3parse, STREAM, DECODE,
        ("Failed to parse stream"), (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_mp3parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstMPEGAudioParse *mp3parse;
  GstClockTime timestamp;

  mp3parse = GST_MP3PARSE (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (mp3parse, "buffer of %d bytes", GST_BUFFER_SIZE (buf));

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  mp3parse->discont |= GST_BUFFER_IS_DISCONT (buf);

  /* If we don't yet have a next timestamp, save it and the incoming offset
   * so we can apply it to the right outgoing buffer */
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    gint64 avail = gst_adapter_available (mp3parse->adapter);

    mp3parse->pending_ts = timestamp;
    mp3parse->pending_offset = mp3parse->tracked_offset + avail;

    /* If we have no data pending and the next timestamp is
     * invalid we can use the upstream timestamp for the next frame.
     *
     * This will give us a timestamp if we're resyncing and upstream
     * gave us -1 as offset. */
    if (avail == 0 && !GST_CLOCK_TIME_IS_VALID (mp3parse->next_ts))
      mp3parse->next_ts = timestamp;

    GST_LOG_OBJECT (mp3parse, "Have pending ts %" GST_TIME_FORMAT
        " to apply in %" G_GINT64_FORMAT " bytes (@ off %" G_GINT64_FORMAT ")",
        GST_TIME_ARGS (mp3parse->pending_ts), avail, mp3parse->pending_offset);
  }

  /* Update the cur_offset we'll apply to outgoing buffers */
  if (mp3parse->cur_offset == -1 && GST_BUFFER_OFFSET (buf) != -1)
    mp3parse->cur_offset = GST_BUFFER_OFFSET (buf);

  /* And add the data to the pool */
  gst_adapter_push (mp3parse->adapter, buf);

  return gst_mp3parse_handle_data (mp3parse, FALSE);
}

static gboolean
head_check (GstMPEGAudioParse * mp3parse, unsigned long head)
{
  GST_DEBUG_OBJECT (mp3parse, "checking mp3 header 0x%08lx", head);
  /* if it's not a valid sync */
  if ((head & 0xffe00000) != 0xffe00000) {
    GST_WARNING_OBJECT (mp3parse, "invalid sync");
    return FALSE;
  }
  /* if it's an invalid MPEG version */
  if (((head >> 19) & 3) == 0x1) {
    GST_WARNING_OBJECT (mp3parse, "invalid MPEG version: 0x%lx",
        (head >> 19) & 3);
    return FALSE;
  }
  /* if it's an invalid layer */
  if (!((head >> 17) & 3)) {
    GST_WARNING_OBJECT (mp3parse, "invalid layer: 0x%lx", (head >> 17) & 3);
    return FALSE;
  }
  /* if it's an invalid bitrate */
  if (((head >> 12) & 0xf) == 0x0) {
    GST_WARNING_OBJECT (mp3parse, "invalid bitrate: 0x%lx."
        "Free format files are not supported yet", (head >> 12) & 0xf);
    return FALSE;
  }
  if (((head >> 12) & 0xf) == 0xf) {
    GST_WARNING_OBJECT (mp3parse, "invalid bitrate: 0x%lx", (head >> 12) & 0xf);
    return FALSE;
  }
  /* if it's an invalid samplerate */
  if (((head >> 10) & 0x3) == 0x3) {
    GST_WARNING_OBJECT (mp3parse, "invalid samplerate: 0x%lx",
        (head >> 10) & 0x3);
    return FALSE;
  }

  if ((head & 0x3) == 0x2) {
    /* Ignore this as there are some files with emphasis 0x2 that can
     * be played fine. See BGO #537235 */
    GST_WARNING_OBJECT (mp3parse, "invalid emphasis: 0x%lx", head & 0x3);
  }

  return TRUE;
}

static void
gst_mp3parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMPEGAudioParse *src;

  src = GST_MP3PARSE (object);

  switch (prop_id) {
    case ARG_SKIP:
      src->skip = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_mp3parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMPEGAudioParse *src;

  src = GST_MP3PARSE (object);

  switch (prop_id) {
    case ARG_SKIP:
      g_value_set_int (value, src->skip);
      break;
    case ARG_BIT_RATE:
      g_value_set_int (value, src->bit_rate * 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_mp3parse_change_state (GstElement * element, GstStateChange transition)
{
  GstMPEGAudioParse *mp3parse;
  GstStateChangeReturn result;

  mp3parse = GST_MP3PARSE (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mp3parse_reset (mp3parse);
      break;
    default:
      break;
  }

  return result;
}

static gboolean
mp3parse_total_bytes (GstMPEGAudioParse * mp3parse, gint64 * total)
{
  GstFormat fmt = GST_FORMAT_BYTES;

  if (gst_pad_query_peer_duration (mp3parse->sinkpad, &fmt, total))
    return TRUE;

  if (mp3parse->xing_flags & XING_BYTES_FLAG) {
    *total = mp3parse->xing_bytes;
    return TRUE;
  }

  if (mp3parse->vbri_bytes != 0 && mp3parse->vbri_valid) {
    *total = mp3parse->vbri_bytes;
    return TRUE;
  }

  return FALSE;
}

static gboolean
mp3parse_total_time (GstMPEGAudioParse * mp3parse, GstClockTime * total)
{
  gint64 total_bytes;

  *total = GST_CLOCK_TIME_NONE;

  if (mp3parse->xing_flags & XING_FRAMES_FLAG) {
    *total = mp3parse->xing_total_time;
    return TRUE;
  }

  if (mp3parse->vbri_total_time != 0 && mp3parse->vbri_valid) {
    *total = mp3parse->vbri_total_time;
    return TRUE;
  }

  /* Calculate time from the measured bitrate */
  if (!mp3parse_total_bytes (mp3parse, &total_bytes))
    return FALSE;

  if (total_bytes != -1
      && !mp3parse_bytepos_to_time (mp3parse, total_bytes, total, TRUE))
    return FALSE;

  return TRUE;
}

/* Convert a timestamp to the file position required to start decoding that
 * timestamp. For now, this just uses the avg bitrate. Later, use an 
 * incrementally accumulated seek table */
static gboolean
mp3parse_time_to_bytepos (GstMPEGAudioParse * mp3parse, GstClockTime ts,
    gint64 * bytepos)
{
  gint64 total_bytes;
  GstClockTime total_time;

  /* -1 always maps to -1 */
  if (ts == -1) {
    *bytepos = -1;
    return TRUE;
  }

  /* If XING seek table exists use this for time->byte conversion */
  if ((mp3parse->xing_flags & XING_TOC_FLAG) &&
      (total_bytes = mp3parse->xing_bytes) &&
      (total_time = mp3parse->xing_total_time)) {
    gdouble fa, fb, fx;
    gdouble percent =
        CLAMP ((100.0 * gst_util_guint64_to_gdouble (ts)) /
        gst_util_guint64_to_gdouble (total_time), 0.0, 100.0);
    gint index = CLAMP (percent, 0, 99);

    fa = mp3parse->xing_seek_table[index];
    if (index < 99)
      fb = mp3parse->xing_seek_table[index + 1];
    else
      fb = 256.0;

    fx = fa + (fb - fa) * (percent - index);

    *bytepos = (1.0 / 256.0) * fx * total_bytes;

    return TRUE;
  }

  if (mp3parse->vbri_seek_table && (total_bytes = mp3parse->vbri_bytes) &&
      (total_time = mp3parse->vbri_total_time)) {
    gint i, j;
    gdouble a, b, fa, fb;

    i = gst_util_uint64_scale (ts, mp3parse->vbri_seek_points - 1, total_time);
    i = CLAMP (i, 0, mp3parse->vbri_seek_points - 1);

    a = gst_guint64_to_gdouble (gst_util_uint64_scale (i, total_time,
            mp3parse->vbri_seek_points));
    fa = 0.0;
    for (j = i; j >= 0; j--)
      fa += mp3parse->vbri_seek_table[j];

    if (i + 1 < mp3parse->vbri_seek_points) {
      b = gst_guint64_to_gdouble (gst_util_uint64_scale (i + 1, total_time,
              mp3parse->vbri_seek_points));
      fb = fa + mp3parse->vbri_seek_table[i + 1];
    } else {
      b = gst_guint64_to_gdouble (total_time);
      fb = total_bytes;
    }

    *bytepos = fa + ((fb - fa) / (b - a)) * (gst_guint64_to_gdouble (ts) - a);

    return TRUE;
  }

  if (mp3parse->avg_bitrate == 0)
    goto no_bitrate;

  *bytepos =
      gst_util_uint64_scale (ts, mp3parse->avg_bitrate, (8 * GST_SECOND));
  return TRUE;
no_bitrate:
  GST_DEBUG_OBJECT (mp3parse, "Cannot seek yet - no average bitrate");
  return FALSE;
}

static gboolean
mp3parse_bytepos_to_time (GstMPEGAudioParse * mp3parse,
    gint64 bytepos, GstClockTime * ts, gboolean from_total_time)
{
  gint64 total_bytes;
  GstClockTime total_time;

  if (bytepos == -1) {
    *ts = GST_CLOCK_TIME_NONE;
    return TRUE;
  }

  if (bytepos == 0) {
    *ts = 0;
    return TRUE;
  }

  /* If XING seek table exists use this for byte->time conversion */
  if (!from_total_time && (mp3parse->xing_flags & XING_TOC_FLAG) &&
      (total_bytes = mp3parse->xing_bytes) &&
      (total_time = mp3parse->xing_total_time)) {
    gdouble fa, fb, fx;
    gdouble pos;
    gint index;

    pos = CLAMP ((bytepos * 256.0) / total_bytes, 0.0, 256.0);
    index = CLAMP (pos, 0, 255);
    fa = mp3parse->xing_seek_table_inverse[index];
    if (index < 255)
      fb = mp3parse->xing_seek_table_inverse[index + 1];
    else
      fb = 10000.0;

    fx = fa + (fb - fa) * (pos - index);

    *ts = (1.0 / 10000.0) * fx * gst_util_guint64_to_gdouble (total_time);

    return TRUE;
  }

  if (!from_total_time && mp3parse->vbri_seek_table &&
      (total_bytes = mp3parse->vbri_bytes) &&
      (total_time = mp3parse->vbri_total_time)) {
    gint i = 0;
    guint64 sum = 0;
    gdouble a, b, fa, fb;

    do {
      sum += mp3parse->vbri_seek_table[i];
      i++;
    } while (i + 1 < mp3parse->vbri_seek_points
        && sum + mp3parse->vbri_seek_table[i] < bytepos);
    i--;

    a = gst_guint64_to_gdouble (sum);
    fa = gst_guint64_to_gdouble (gst_util_uint64_scale (i, total_time,
            mp3parse->vbri_seek_points));

    if (i + 1 < mp3parse->vbri_seek_points) {
      b = a + mp3parse->vbri_seek_table[i + 1];
      fb = gst_guint64_to_gdouble (gst_util_uint64_scale (i + 1, total_time,
              mp3parse->vbri_seek_points));
    } else {
      b = total_bytes;
      fb = gst_guint64_to_gdouble (total_time);
    }

    *ts = gst_gdouble_to_guint64 (fa + ((fb - fa) / (b - a)) * (bytepos - a));

    return TRUE;
  }

  /* Cannot convert anything except 0 if we don't have a bitrate yet */
  if (mp3parse->avg_bitrate == 0)
    return FALSE;

  *ts = (GstClockTime) gst_util_uint64_scale (GST_SECOND, bytepos * 8,
      mp3parse->avg_bitrate);
  return TRUE;
}

static gboolean
mp3parse_handle_seek (GstMPEGAudioParse * mp3parse, GstEvent * event)
{
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 byte_cur, byte_stop;
  MPEGAudioPendingAccurateSeek *seek;
  GstClockTime start;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  GST_DEBUG_OBJECT (mp3parse, "Performing seek to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (cur));

  /* For any format other than TIME, see if upstream handles
   * it directly or fail. For TIME, try upstream, but do it ourselves if
   * it fails upstream */
  if (format != GST_FORMAT_TIME) {
    gst_event_ref (event);
    return gst_pad_push_event (mp3parse->sinkpad, event);
  } else {
    gst_event_ref (event);
    if (gst_pad_push_event (mp3parse->sinkpad, event))
      return TRUE;
  }

  seek = g_new0 (MPEGAudioPendingAccurateSeek, 1);

  seek->segment = mp3parse->segment;

  gst_segment_set_seek (&seek->segment, rate, GST_FORMAT_TIME,
      flags, cur_type, cur, stop_type, stop, NULL);

  /* Handle TIME based seeks by converting to a BYTE position */

  /* For accurate seeking get the frame 9 (MPEG1) or 29 (MPEG2) frames
   * before the one we want to seek to and push them all to the decoder.
   *
   * This is necessary because of the bit reservoir. See
   * http://www.mars.org/mailman/public/mad-dev/2002-May/000634.html
   *
   */

  if (flags & GST_SEEK_FLAG_ACCURATE) {
    if (!mp3parse->seek_table) {
      byte_cur = 0;
      byte_stop = -1;
      start = 0;
    } else {
      MPEGAudioSeekEntry *entry = NULL, *start_entry = NULL, *stop_entry = NULL;
      GList *start_node, *stop_node;
      gint64 seek_ts = (cur > mp3parse->max_bitreservoir) ?
          (cur - mp3parse->max_bitreservoir) : 0;

      for (start_node = mp3parse->seek_table; start_node;
          start_node = start_node->next) {
        entry = start_node->data;

        if (seek_ts >= entry->timestamp) {
          start_entry = entry;
          break;
        }
      }

      if (!start_entry) {
        start_entry = mp3parse->seek_table->data;
        start = start_entry->timestamp;
        byte_cur = start_entry->byte;
      } else {
        start = start_entry->timestamp;
        byte_cur = start_entry->byte;
      }

      for (stop_node = mp3parse->seek_table; stop_node;
          stop_node = stop_node->next) {
        entry = stop_node->data;

        if (stop >= entry->timestamp) {
          stop_node = stop_node->prev;
          stop_entry = (stop_node) ? stop_node->data : NULL;
          break;
        }
      }

      if (!stop_entry) {
        byte_stop = -1;
      } else {
        byte_stop = stop_entry->byte;
      }

    }
    event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type,
        byte_cur, stop_type, byte_stop);
    g_mutex_lock (mp3parse->pending_seeks_lock);
    seek->upstream_start = byte_cur;
    seek->timestamp_start = start;
    mp3parse->pending_accurate_seeks =
        g_slist_prepend (mp3parse->pending_accurate_seeks, seek);
    g_mutex_unlock (mp3parse->pending_seeks_lock);
    if (gst_pad_push_event (mp3parse->sinkpad, event)) {
      mp3parse->exact_position = TRUE;
      return TRUE;
    } else {
      mp3parse->exact_position = TRUE;
      g_mutex_lock (mp3parse->pending_seeks_lock);
      mp3parse->pending_accurate_seeks =
          g_slist_remove (mp3parse->pending_accurate_seeks, seek);
      g_mutex_unlock (mp3parse->pending_seeks_lock);
      g_free (seek);
      return FALSE;
    }
  }

  mp3parse->exact_position = FALSE;

  /* Convert the TIME to the appropriate BYTE position at which to resume
   * decoding. */
  if (!mp3parse_time_to_bytepos (mp3parse, (GstClockTime) cur, &byte_cur))
    goto no_pos;
  if (!mp3parse_time_to_bytepos (mp3parse, (GstClockTime) stop, &byte_stop))
    goto no_pos;

  GST_DEBUG_OBJECT (mp3parse, "Seeking to byte range %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, byte_cur, byte_stop);

  /* Send BYTE based seek upstream */
  event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type,
      byte_cur, stop_type, byte_stop);

  GST_LOG_OBJECT (mp3parse, "Storing pending seek");
  g_mutex_lock (mp3parse->pending_seeks_lock);
  seek->upstream_start = byte_cur;
  seek->timestamp_start = cur;
  mp3parse->pending_nonaccurate_seeks =
      g_slist_prepend (mp3parse->pending_nonaccurate_seeks, seek);
  g_mutex_unlock (mp3parse->pending_seeks_lock);
  if (gst_pad_push_event (mp3parse->sinkpad, event)) {
    return TRUE;
  } else {
    g_mutex_lock (mp3parse->pending_seeks_lock);
    mp3parse->pending_nonaccurate_seeks =
        g_slist_remove (mp3parse->pending_nonaccurate_seeks, seek);
    g_mutex_unlock (mp3parse->pending_seeks_lock);
    g_free (seek);
    return FALSE;
  }

no_pos:
  GST_DEBUG_OBJECT (mp3parse,
      "Could not determine byte position for desired time");
  return FALSE;
}

static gboolean
mp3parse_src_event (GstPad * pad, GstEvent * event)
{
  GstMPEGAudioParse *mp3parse;
  gboolean res = FALSE;

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = mp3parse_handle_seek (mp3parse, event);
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (mp3parse);
  return res;
}

static gboolean
mp3parse_src_query (GstPad * pad, GstQuery * query)
{
  GstFormat format;
  GstClockTime total;
  GstMPEGAudioParse *mp3parse;
  gboolean res = FALSE;
  GstPad *peer;

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_BYTES || format == GST_FORMAT_DEFAULT) {
        if (mp3parse->cur_offset != -1) {
          gst_query_set_position (query, GST_FORMAT_BYTES,
              mp3parse->cur_offset);
          res = TRUE;
        }
      } else if (format == GST_FORMAT_TIME) {
        if (mp3parse->next_ts == GST_CLOCK_TIME_NONE)
          goto out;
        gst_query_set_position (query, GST_FORMAT_TIME, mp3parse->next_ts);
        res = TRUE;
      }

      /* If no answer above, see if upstream knows */
      if (!res) {
        if ((peer = gst_pad_get_peer (mp3parse->sinkpad)) != NULL) {
          res = gst_pad_query (peer, query);
          gst_object_unref (peer);
          if (res)
            goto out;
        }
      }
      break;
    case GST_QUERY_DURATION:
      gst_query_parse_duration (query, &format, NULL);

      /* First, see if upstream knows */
      if ((peer = gst_pad_get_peer (mp3parse->sinkpad)) != NULL) {
        res = gst_pad_query (peer, query);
        gst_object_unref (peer);
        if (res)
          goto out;
      }

      if (format == GST_FORMAT_TIME) {
        if (!mp3parse_total_time (mp3parse, &total) || total == -1)
          goto out;
        gst_query_set_duration (query, format, total);
        res = TRUE;
      }
      break;
    case GST_QUERY_SEEKING:
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);

      /* does upstream handle ? */
      if ((peer = gst_pad_get_peer (mp3parse->sinkpad)) != NULL) {
        res = gst_pad_query (peer, query);
        gst_object_unref (peer);
      }
      /* we may be able to help if in TIME */
      if (format == GST_FORMAT_TIME) {
        gboolean seekable;

        gst_query_parse_seeking (query, &format, &seekable, NULL, NULL);
        /* already OK if upstream takes care */
        if (!(res && seekable)) {
          gint64 pos;

          seekable = TRUE;
          if (!mp3parse_total_time (mp3parse, &total) || total == -1) {
            seekable = FALSE;
          } else if (!mp3parse_time_to_bytepos (mp3parse, 0, &pos)) {
            seekable = FALSE;
          } else {
            GstQuery *q;

            q = gst_query_new_seeking (GST_FORMAT_BYTES);
            if (!gst_pad_peer_query (mp3parse->sinkpad, q)) {
              seekable = FALSE;
            } else {
              gst_query_parse_seeking (q, &format, &seekable, NULL, NULL);
            }
            gst_query_unref (q);
          }
          gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0, total);
          res = TRUE;
        }
      }
      break;
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

out:
  gst_object_unref (mp3parse);
  return res;
}

static const GstQueryType *
mp3parse_get_query_types (GstPad * pad G_GNUC_UNUSED)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return query_types;
}
