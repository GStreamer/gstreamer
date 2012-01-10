/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/* 
   Code based on modplugxmms
   XMMS plugin:
     Kenton Varda <temporal@gauge3d.org>
   Sound Engine:
     Olivier Lapicque <olivierl@jps.net>  
*/

/**
 * SECTION:element-modplug
 * 
 * Modplug uses the <ulink url="http://modplug-xmms.sourceforge.net/">modplug</ulink>
 * library to decode tracked music in the MOD/S3M/XM/IT and related formats.
 * 
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v filesrc location=1990s-nostalgia.xm ! modplug ! audioconvert ! alsasink
 * ]| Play a FastTracker xm file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Required to not get an undefined warning
 * https://bugzilla.gnome.org/show_bug.cgi?id=613795
 */
#ifndef WORDS_BIGENDIAN
#define WORDS_BIGENDIAN 0
#endif

#include <stdafx.h>
#include <libmodplug/sndfile.h>

#include "gstmodplug.h"

#include <gst/gst.h>
#include <stdlib.h>
#include <gst/audio/audio.h>

GST_DEBUG_CATEGORY_STATIC (modplug_debug);
#define GST_CAT_DEFAULT modplug_debug

enum
{
  ARG_0,
  ARG_SONGNAME,
  ARG_REVERB,
  ARG_REVERB_DEPTH,
  ARG_REVERB_DELAY,
  ARG_MEGABASS,
  ARG_MEGABASS_AMOUNT,
  ARG_MEGABASS_RANGE,
  ARG_NOISE_REDUCTION,
  ARG_SURROUND,
  ARG_SURROUND_DEPTH,
  ARG_SURROUND_DELAY,
  ARG_OVERSAMP
};

#define DEFAULT_REVERB           FALSE
#define DEFAULT_REVERB_DEPTH     30
#define DEFAULT_REVERB_DELAY     100
#define DEFAULT_MEGABASS         FALSE
#define DEFAULT_MEGABASS_AMOUNT  40
#define DEFAULT_MEGABASS_RANGE   30
#define DEFAULT_SURROUND         TRUE
#define DEFAULT_SURROUND_DEPTH   20
#define DEFAULT_SURROUND_DELAY   20
#define DEFAULT_OVERSAMP         TRUE
#define DEFAULT_NOISE_REDUCTION  TRUE

static GstStaticPadTemplate modplug_src_template_factory =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int,"
        " endianness = (int) BYTE_ORDER,"
        " signed = (boolean) true,"
        " width = (int) 32,"
        " depth = (int) 32,"
        " rate = (int) { 8000, 11025, 22050, 44100 },"
        " channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int,"
        " endianness = (int) BYTE_ORDER,"
        " signed = (boolean) true,"
        " width = (int) 16,"
        " depth = (int) 16,"
        " rate = (int) { 8000, 11025, 22050, 44100 },"
        " channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int,"
        " endianness = (int) BYTE_ORDER,"
        " signed = (boolean) false,"
        " width = (int) 8,"
        " depth = (int) 8,"
        " rate = (int) { 8000, 11025, 22050, 44100 }, "
        " channels = (int) [ 1, 2 ]"));

static GstStaticPadTemplate modplug_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mod; audio/x-xm; audio/x-it; audio/x-s3m; "
        "audio/x-stm"));

static void gst_modplug_dispose (GObject * object);
static void gst_modplug_set_property (GObject * object,
    guint id, const GValue * value, GParamSpec * pspec);
static void gst_modplug_get_property (GObject * object,
    guint id, GValue * value, GParamSpec * pspec);

static void gst_modplug_fixate (GstPad * pad, GstCaps * caps);
static const GstQueryType *gst_modplug_get_query_types (GstPad * pad);
static gboolean gst_modplug_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_modplug_src_query (GstPad * pad, GstQuery * query);
static GstStateChangeReturn gst_modplug_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_modplug_sinkpad_activate (GstPad * pad);
static gboolean gst_modplug_sinkpad_activate_pull (GstPad * pad,
    gboolean active);
static void gst_modplug_loop (GstModPlug * element);

GST_BOILERPLATE (GstModPlug, gst_modplug, GstElement, GST_TYPE_ELEMENT);

static void
gst_modplug_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &modplug_sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &modplug_src_template_factory);

  gst_element_class_set_details_simple (element_class, "ModPlug",
      "Codec/Decoder/Audio", "Module decoder based on modplug engine",
      "Jeremy SIMON <jsimon13@yahoo.fr>");

  GST_DEBUG_CATEGORY_INIT (modplug_debug, "modplug", 0, "ModPlug element");
}

static void
gst_modplug_class_init (GstModPlugClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_modplug_set_property;
  gobject_class->get_property = gst_modplug_get_property;
  gobject_class->dispose = gst_modplug_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SONGNAME,
      g_param_spec_string ("songname", "Songname", "The song name",
          NULL, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_REVERB,
      g_param_spec_boolean ("reverb", "reverb", "Reverb",
          DEFAULT_REVERB,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_REVERB_DEPTH,
      g_param_spec_int ("reverb-depth", "reverb depth", "Reverb depth",
          0, 100, DEFAULT_REVERB_DEPTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_REVERB_DELAY,
      g_param_spec_int ("reverb-delay", "reverb delay", "Reverb delay",
          0, 200, DEFAULT_REVERB_DELAY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEGABASS,
      g_param_spec_boolean ("megabass", "megabass", "Megabass",
          DEFAULT_MEGABASS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEGABASS_AMOUNT,
      g_param_spec_int ("megabass-amount", "megabass amount", "Megabass amount",
          0, 100, DEFAULT_MEGABASS_AMOUNT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEGABASS_RANGE,
      g_param_spec_int ("megabass-range", "megabass range", "Megabass range",
          0, 100, DEFAULT_MEGABASS_RANGE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SURROUND,
      g_param_spec_boolean ("surround", "surround", "Surround",
          DEFAULT_SURROUND,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SURROUND_DEPTH,
      g_param_spec_int ("surround-depth", "surround depth", "Surround depth",
          0, 100, DEFAULT_SURROUND_DEPTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SURROUND_DELAY,
      g_param_spec_int ("surround-delay", "surround delay", "Surround delay",
          0, 40, DEFAULT_SURROUND_DELAY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_OVERSAMP,
      g_param_spec_boolean ("oversamp", "oversamp", "oversamp",
          DEFAULT_OVERSAMP,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NOISE_REDUCTION,
      g_param_spec_boolean ("noise-reduction", "noise reduction",
          "noise reduction", DEFAULT_NOISE_REDUCTION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gstelement_class->change_state = gst_modplug_change_state;
}

static void
gst_modplug_init (GstModPlug * modplug, GstModPlugClass * klass)
{
  /* create the sink and src pads */
  modplug->sinkpad =
      gst_pad_new_from_static_template (&modplug_sink_template_factory, "sink");
  gst_pad_set_activate_function (modplug->sinkpad,
      GST_DEBUG_FUNCPTR (gst_modplug_sinkpad_activate));
  gst_pad_set_activatepull_function (modplug->sinkpad,
      GST_DEBUG_FUNCPTR (gst_modplug_sinkpad_activate_pull));
  gst_element_add_pad (GST_ELEMENT (modplug), modplug->sinkpad);

  modplug->srcpad =
      gst_pad_new_from_static_template (&modplug_src_template_factory, "src");
  gst_pad_set_fixatecaps_function (modplug->srcpad,
      GST_DEBUG_FUNCPTR (gst_modplug_fixate));
  gst_pad_set_event_function (modplug->srcpad,
      GST_DEBUG_FUNCPTR (gst_modplug_src_event));
  gst_pad_set_query_function (modplug->srcpad,
      GST_DEBUG_FUNCPTR (gst_modplug_src_query));
  gst_pad_set_query_type_function (modplug->srcpad,
      GST_DEBUG_FUNCPTR (gst_modplug_get_query_types));
  gst_element_add_pad (GST_ELEMENT (modplug), modplug->srcpad);

  modplug->reverb = DEFAULT_REVERB;
  modplug->reverb_depth = DEFAULT_REVERB_DEPTH;
  modplug->reverb_delay = DEFAULT_REVERB_DELAY;
  modplug->megabass = DEFAULT_MEGABASS;
  modplug->megabass_amount = DEFAULT_MEGABASS_AMOUNT;
  modplug->megabass_range = DEFAULT_MEGABASS_RANGE;
  modplug->surround = DEFAULT_SURROUND;
  modplug->surround_depth = DEFAULT_SURROUND_DEPTH;
  modplug->surround_delay = DEFAULT_SURROUND_DELAY;
  modplug->oversamp = DEFAULT_OVERSAMP;
  modplug->noise_reduction = DEFAULT_NOISE_REDUCTION;

  modplug->bits = 16;
  modplug->channel = 2;
  modplug->frequency = 44100;
}


static void
gst_modplug_dispose (GObject * object)
{
  GstModPlug *modplug = GST_MODPLUG (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  if (modplug->buffer) {
    gst_buffer_unref (modplug->buffer);
    modplug->buffer = NULL;
  }
}

static const GstQueryType *
gst_modplug_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_modplug_src_query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    (GstQueryType) 0
  };

  return gst_modplug_src_query_types;
}


static gboolean
gst_modplug_src_query (GstPad * pad, GstQuery * query)
{
  GstModPlug *modplug;
  gboolean res = FALSE;

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  if (!modplug->mSoundFile)
    goto done;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        gst_query_set_duration (query, format, modplug->song_length);
        res = TRUE;
      }
    }
      break;
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        gint64 pos;

        pos = (modplug->song_length * modplug->mSoundFile->GetCurrentPos ());
        pos /= modplug->mSoundFile->GetMaxPosition ();
        gst_query_set_position (query, format, pos);
        res = TRUE;
      }
    }
      break;
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

done:
  gst_object_unref (modplug);

  return res;
}

static gboolean
gst_modplug_src_event (GstPad * pad, GstEvent * event)
{
  GstModPlug *modplug;
  gboolean res = FALSE;

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gboolean flush;
      gint64 cur, stop;
/* FIXME timestamp is set but not used */
#if 0
      guint64 timestamp;
#endif

      if (modplug->frequency == 0) {
        GST_DEBUG_OBJECT (modplug, "no song loaded yet");
        break;
      }
#if 0
      timestamp = gst_util_uint64_scale_int (modplug->offset, GST_SECOND,
          modplug->frequency);
#endif

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (modplug, "seeking is only supported in TIME format");
        gst_event_unref (event);
        break;
      }

      /* FIXME: we should be using GstSegment for all this */
      if (cur_type != GST_SEEK_TYPE_SET || stop_type != GST_SEEK_TYPE_NONE) {
        GST_DEBUG_OBJECT (modplug, "unsupported seek type");
        gst_event_unref (event);
        break;
      }

      if (stop_type == GST_SEEK_TYPE_NONE)
        stop = GST_CLOCK_TIME_NONE;

      cur = CLAMP (cur, 0, modplug->song_length);

      GST_DEBUG_OBJECT (modplug, "seek to %" GST_TIME_FORMAT,
          GST_TIME_ARGS ((guint64) cur));

      modplug->seek_at = cur;

      flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);

      if (flush) {
        gst_pad_push_event (modplug->srcpad, gst_event_new_flush_start ());
      } else {
        gst_pad_stop_task (modplug->sinkpad);
      }

      GST_PAD_STREAM_LOCK (modplug->sinkpad);

      if (flags & GST_SEEK_FLAG_SEGMENT) {
        gst_element_post_message (GST_ELEMENT (modplug),
            gst_message_new_segment_start (GST_OBJECT (modplug), format, cur));
      }
      if (stop == -1 && modplug->song_length > 0)
        stop = modplug->song_length;

      if (flush) {
        gst_pad_push_event (modplug->srcpad, gst_event_new_flush_stop ());
      }

      GST_LOG_OBJECT (modplug, "sending newsegment from %" GST_TIME_FORMAT "-%"
          GST_TIME_FORMAT ", pos=%" GST_TIME_FORMAT,
          GST_TIME_ARGS ((guint64) cur), GST_TIME_ARGS ((guint64) stop),
          GST_TIME_ARGS ((guint64) cur));

      gst_pad_push_event (modplug->srcpad,
          gst_event_new_new_segment (FALSE, rate,
              GST_FORMAT_TIME, cur, stop, cur));

      modplug->offset =
          gst_util_uint64_scale_int (cur, modplug->frequency, GST_SECOND);

      gst_pad_start_task (modplug->sinkpad,
          (GstTaskFunction) gst_modplug_loop, modplug);

      GST_PAD_STREAM_UNLOCK (modplug->sinkpad);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (modplug);
  return res;
}

static void
gst_modplug_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_fixate_field_nearest_int (structure, "rate", 44100))
    GST_WARNING_OBJECT (pad, "Failed to fixate rate to 44100");
  if (!gst_structure_fixate_field_nearest_int (structure, "channels", 2))
    GST_WARNING_OBJECT (pad, "Failed to fixate number of channels to stereo");
}

static gboolean
gst_modplug_load_song (GstModPlug * modplug)
{
  GstCaps *newcaps, *othercaps;
  GstStructure *structure;

  GST_DEBUG_OBJECT (modplug, "Setting caps");

  /* negotiate srcpad caps */
  if ((othercaps = gst_pad_get_allowed_caps (modplug->srcpad))) {
    newcaps = gst_caps_copy_nth (othercaps, 0);
    gst_caps_unref (othercaps);
  } else {
    GST_WARNING ("no allowed caps on srcpad, no peer linked");
    /* FIXME: this can be done in a better way */
    newcaps =
        gst_caps_copy_nth (gst_pad_get_pad_template_caps (modplug->srcpad), 0);
  }
  gst_pad_fixate_caps (modplug->srcpad, newcaps);

  /* set up modplug to output the negotiated format */
  structure = gst_caps_get_structure (newcaps, 0);
  gst_structure_get_int (structure, "depth", &modplug->bits);
  gst_structure_get_int (structure, "channels", &modplug->channel);
  gst_structure_get_int (structure, "rate", &modplug->frequency);

  gst_pad_set_caps (modplug->srcpad, newcaps);
  gst_caps_unref (newcaps);

  modplug->read_samples = 1152;
  modplug->read_bytes =
      modplug->read_samples * modplug->channel * modplug->bits / 8;

  GST_DEBUG_OBJECT (modplug, "Loading song");

  modplug->mSoundFile = new CSoundFile;

  modplug->mSoundFile->SetWaveConfig (modplug->frequency, modplug->bits,
      modplug->channel);

  modplug->mSoundFile->SetWaveConfigEx (modplug->surround, !modplug->oversamp,
      modplug->reverb, true, modplug->megabass, modplug->noise_reduction, true);
  modplug->mSoundFile->SetResamplingMode (SRCMODE_POLYPHASE);

  if (modplug->surround)
    modplug->mSoundFile->SetSurroundParameters (modplug->surround_depth,
        modplug->surround_delay);

  if (modplug->megabass)
    modplug->mSoundFile->SetXBassParameters (modplug->megabass_amount,
        modplug->megabass_range);

  if (modplug->reverb)
    modplug->mSoundFile->SetReverbParameters (modplug->reverb_depth,
        modplug->reverb_delay);

  if (!modplug->mSoundFile->Create (GST_BUFFER_DATA (modplug->buffer),
          modplug->song_size)) {
    GST_ELEMENT_ERROR (modplug, STREAM, DECODE, (NULL),
        ("Unable to load song"));
    return FALSE;
  }

  modplug->song_length = modplug->mSoundFile->GetSongTime () * GST_SECOND;
  modplug->seek_at = -1;

  GST_INFO_OBJECT (modplug, "Song length: %" GST_TIME_FORMAT,
      GST_TIME_ARGS ((guint64) modplug->song_length));

  return TRUE;
}

static gboolean
gst_modplug_sinkpad_activate (GstPad * pad)
{
  if (!gst_pad_check_pull_range (pad))
    return FALSE;

  return gst_pad_activate_pull (pad, TRUE);
}

static gboolean
gst_modplug_sinkpad_activate_pull (GstPad * pad, gboolean active)
{
  GstModPlug *modplug = GST_MODPLUG (GST_OBJECT_PARENT (pad));

  if (active) {
    return gst_pad_start_task (pad, (GstTaskFunction) gst_modplug_loop,
        modplug);
  } else {
    return gst_pad_stop_task (pad);
  }
}

static gboolean
gst_modplug_get_upstream_size (GstModPlug * modplug, gint64 * length)
{
  GstFormat format = GST_FORMAT_BYTES;
  gboolean res = FALSE;
  GstPad *peer;

  peer = gst_pad_get_peer (modplug->sinkpad);
  if (peer == NULL)
    return FALSE;

  if (gst_pad_query_duration (peer, &format, length) && *length >= 0) {
    res = TRUE;
  }

  gst_object_unref (peer);
  return res;
}

static void
gst_modplug_loop (GstModPlug * modplug)
{
  GstFlowReturn flow;
  GstBuffer *out = NULL;

  g_assert (GST_IS_MODPLUG (modplug));

  /* first, get the size of the song */
  if (!modplug->song_size) {
    if (!gst_modplug_get_upstream_size (modplug, &modplug->song_size)) {
      GST_ELEMENT_ERROR (modplug, STREAM, DECODE, (NULL),
          ("Unable to load song"));
      goto pause;
    }

    if (modplug->buffer) {
      gst_buffer_unref (modplug->buffer);
    }
    modplug->buffer = gst_buffer_new_and_alloc (modplug->song_size);
    modplug->offset = 0;
  }

  /* read in the song data */
  if (!modplug->mSoundFile) {
    GstBuffer *buffer = NULL;
    guint64 read_size = modplug->song_size - modplug->offset;

    if (read_size > 4096)
      read_size = 4096;

    flow =
        gst_pad_pull_range (modplug->sinkpad, modplug->offset, read_size,
        &buffer);
    if (flow != GST_FLOW_OK) {
      GST_ELEMENT_ERROR (modplug, STREAM, DECODE, (NULL),
          ("Unable to load song"));
      goto pause;
    }

    /* GST_LOG_OBJECT (modplug, "Read %u bytes", GST_BUFFER_SIZE (buffer)); */
    g_memmove (GST_BUFFER_DATA (modplug->buffer) + modplug->offset,
        GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
    gst_buffer_unref (buffer);

    modplug->offset += read_size;

    /* actually load it */
    if (modplug->offset == modplug->song_size) {
      GstEvent *newsegment;
      GstTagList *tags;
      gboolean ok;
      gchar comment[16384];

      ok = gst_modplug_load_song (modplug);
      gst_buffer_unref (modplug->buffer);
      modplug->buffer = NULL;
      modplug->offset = 0;

      if (!ok) {
        goto pause;
      }

      newsegment = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
          0, modplug->song_length, 0);
      gst_pad_push_event (modplug->srcpad, newsegment);

      /* get and send metadata */
      tags = gst_tag_list_new ();
      gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
          GST_TAG_TITLE, modplug->mSoundFile->GetTitle (),
          GST_TAG_BEATS_PER_MINUTE,
          (gdouble) modplug->mSoundFile->GetMusicTempo (), NULL);

      if (modplug->mSoundFile->GetSongComments ((gchar *) & comment, 16384, 32)) {
        gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
            GST_TAG_COMMENT, comment, NULL);
      }


      gst_element_found_tags (GST_ELEMENT (modplug), tags);
    } else {
      /* not fully loaded yet */
      return;
    }
  }

  /* could move this to gst_modplug_src_event 
   * if libmodplug was definitely thread safe.. */
  if (modplug->seek_at != -1) {
    gint seek_to_pos;
    gfloat temp;

    temp = (gfloat) modplug->song_length / modplug->seek_at;
    seek_to_pos = (int) (modplug->mSoundFile->GetMaxPosition () / temp);

    GST_DEBUG_OBJECT (modplug, "Seeking to row %d", seek_to_pos);

    modplug->mSoundFile->SetCurrentPos (seek_to_pos);
    modplug->seek_at = -1;
  }

  /* read and output a buffer */
  flow = gst_pad_alloc_buffer_and_set_caps (modplug->srcpad,
      GST_BUFFER_OFFSET_NONE, modplug->read_bytes,
      GST_PAD_CAPS (modplug->srcpad), &out);

  if (flow != GST_FLOW_OK) {
    GST_LOG_OBJECT (modplug, "pad alloc flow: %s", gst_flow_get_name (flow));
    goto pause;
  }

  if (!modplug->mSoundFile->Read (GST_BUFFER_DATA (out), modplug->read_bytes))
    goto eos;

  GST_BUFFER_SIZE (out) = modplug->read_bytes;
  GST_BUFFER_DURATION (out) =
      gst_util_uint64_scale_int (modplug->read_samples, GST_SECOND,
      modplug->frequency);
  GST_BUFFER_OFFSET (out) = modplug->offset;
  GST_BUFFER_TIMESTAMP (out) =
      gst_util_uint64_scale_int (modplug->offset, GST_SECOND,
      modplug->frequency);

  modplug->offset += modplug->read_samples;

  flow = gst_pad_push (modplug->srcpad, out);

  if (flow != GST_FLOW_OK) {
    GST_LOG_OBJECT (modplug, "pad push flow: %s", gst_flow_get_name (flow));
    goto pause;
  }

  return;

eos:
  {
    gst_buffer_unref (out);
    GST_INFO_OBJECT (modplug, "EOS");
    gst_pad_push_event (modplug->srcpad, gst_event_new_eos ());
    goto pause;
  }

pause:
  {
    GST_INFO_OBJECT (modplug, "Pausing");
    gst_pad_pause_task (modplug->sinkpad);
  }
}


static GstStateChangeReturn
gst_modplug_change_state (GstElement * element, GstStateChange transition)
{
  GstModPlug *modplug;
  GstStateChangeReturn ret;

  modplug = GST_MODPLUG (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      modplug->buffer = NULL;
      modplug->offset = 0;
      modplug->song_size = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (modplug->buffer) {
        gst_buffer_unref (modplug->buffer);
        modplug->buffer = NULL;
      }
      if (modplug->mSoundFile) {
        modplug->mSoundFile->Destroy ();
        modplug->mSoundFile = NULL;
      }
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}


static void
gst_modplug_set_property (GObject * object, guint id, const GValue * value,
    GParamSpec * pspec)
{
  GstModPlug *modplug;

  g_return_if_fail (GST_IS_MODPLUG (object));
  modplug = GST_MODPLUG (object);

  switch (id) {
    case ARG_REVERB:
      modplug->reverb = g_value_get_boolean (value);
      break;
    case ARG_REVERB_DEPTH:
      modplug->reverb_depth = g_value_get_int (value);
      break;
    case ARG_REVERB_DELAY:
      modplug->reverb_delay = g_value_get_int (value);
      break;
    case ARG_MEGABASS:
      modplug->megabass = g_value_get_boolean (value);
      break;
    case ARG_MEGABASS_AMOUNT:
      modplug->megabass_amount = g_value_get_int (value);
      break;
    case ARG_MEGABASS_RANGE:
      modplug->megabass_range = g_value_get_int (value);
      break;
    case ARG_NOISE_REDUCTION:
      modplug->noise_reduction = g_value_get_boolean (value);
      break;
    case ARG_SURROUND:
      modplug->surround = g_value_get_boolean (value);
      break;
    case ARG_SURROUND_DEPTH:
      modplug->surround_depth = g_value_get_int (value);
      break;
    case ARG_SURROUND_DELAY:
      modplug->surround_delay = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_modplug_get_property (GObject * object, guint id, GValue * value,
    GParamSpec * pspec)
{
  GstModPlug *modplug;

  g_return_if_fail (GST_IS_MODPLUG (object));
  modplug = GST_MODPLUG (object);

  switch (id) {
    case ARG_REVERB:
      g_value_set_boolean (value, modplug->reverb);
      break;
    case ARG_REVERB_DEPTH:
      g_value_set_int (value, modplug->reverb_depth);
      break;
    case ARG_REVERB_DELAY:
      g_value_set_int (value, modplug->reverb_delay);
      break;
    case ARG_MEGABASS:
      g_value_set_boolean (value, modplug->megabass);
      break;
    case ARG_MEGABASS_AMOUNT:
      g_value_set_int (value, modplug->megabass_amount);
      break;
    case ARG_MEGABASS_RANGE:
      g_value_set_int (value, modplug->megabass_range);
      break;
    case ARG_SURROUND:
      g_value_set_boolean (value, modplug->surround);
      break;
    case ARG_SURROUND_DEPTH:
      g_value_set_int (value, modplug->surround_depth);
      break;
    case ARG_SURROUND_DELAY:
      g_value_set_int (value, modplug->surround_delay);
      break;
    case ARG_NOISE_REDUCTION:
      g_value_set_boolean (value, modplug->noise_reduction);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "modplug",
      GST_RANK_PRIMARY, GST_TYPE_MODPLUG);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "modplug",
    ".MOD audio decoding",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
