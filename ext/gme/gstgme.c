/* Copyright (C) 2004-2005,2009 Michael Pyne <mpyne at kde org>
 * Copyright (C) 2004-2006 Chris Lee <clee at kde org>
 * Copyright (C) 2007 Brian Koropoff <bkoropoff at gmail com>
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

#include "gstgme.h"

#include <string.h>
#include <glib/gprintf.h>
#include <glib.h>

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ay; "
        "audio/x-gbs; "
        "audio/x-gym; "
        "audio/x-hes; "
        "audio/x-kss; "
        "audio/x-nsf; " "audio/x-sap; " "audio/x-spc; " "audio/x-vgm"));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 32000, " "channels = (int) 2"));

GST_BOILERPLATE (GstGmeDec, gst_gme_dec, GstElement, GST_TYPE_ELEMENT);

static GstFlowReturn gst_gme_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_gme_dec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_gme_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_gme_dec_src_query (GstPad * pad, GstQuery * query);
static GstStateChangeReturn gst_gme_dec_change_state (GstElement * element,
    GstStateChange transition);
static void gst_gme_play (GstPad * pad);
static void gst_gme_dec_dispose (GObject * object);
static gboolean gme_setup (GstGmeDec * gme);

static gboolean
gme_negotiate (GstGmeDec * gme)
{
  GstCaps *allowed, *caps;
  GstStructure *structure;
  gint width = 16, depth = 16;
  gboolean sign;
  int rate = 32000;
  int channels = 2;

  allowed = gst_pad_get_allowed_caps (gme->srcpad);
  if (!allowed) {
    GST_DEBUG_OBJECT (gme, "couldn't get allowed caps");
    return FALSE;
  }

  GST_DEBUG_OBJECT (gme, "allowed caps: %" GST_PTR_FORMAT, allowed);

  structure = gst_caps_get_structure (allowed, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "depth", &depth);

  if (width && depth && width != depth) {
    GST_DEBUG_OBJECT (gme, "width %d and depth %d are different", width, depth);
    gst_caps_unref (allowed);
    return FALSE;
  }

  gst_structure_get_boolean (structure, "signed", &sign);
  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, depth,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);
  gst_pad_set_caps (gme->srcpad, caps);

  gst_caps_unref (caps);
  gst_caps_unref (allowed);

  return TRUE;
}

static void
gst_gme_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Gaming console music file decoder", "Codec/Audio/Decoder",
      "Uses libgme to emulate a gaming console sound processors",
      "Chris Lee <clee@kde.org>, Brian Koropoff <bkoropoff@gmail.com>, "
      "Michael Pyne <mpyne@kde.org>, Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_gme_dec_class_init (GstGmeDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_gme_dec_change_state);
  gobject_class->dispose = gst_gme_dec_dispose;
}

static const GstQueryType *
gst_gme_dec_src_query_type (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    (GstQueryType) 0
  };

  return query_types;
}


static void
gst_gme_dec_init (GstGmeDec * gme, GstGmeDecClass * klass)
{
  gme->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  /* gst_pad_set_query_function (gme->sinkpad, NULL); */
  gst_pad_set_event_function (gme->sinkpad, gst_gme_dec_sink_event);
  gst_pad_set_chain_function (gme->sinkpad, gst_gme_dec_chain);
  gst_element_add_pad (GST_ELEMENT (gme), gme->sinkpad);

  gme->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (gme->srcpad, gst_gme_dec_src_event);
  gst_pad_set_query_function (gme->srcpad, gst_gme_dec_src_query);
  gst_pad_set_query_type_function (gme->srcpad, gst_gme_dec_src_query_type);
  gst_pad_use_fixed_caps (gme->srcpad);
  gst_element_add_pad (GST_ELEMENT (gme), gme->srcpad);

  gme->adapter = gst_adapter_new ();
  gme->player = NULL;
  gme->total_duration = GST_CLOCK_TIME_NONE;
  gme->initialized = FALSE;
}

static void
gst_gme_dec_dispose (GObject * object)
{
  GstGmeDec *gme = GST_GME_DEC (object);

  if (gme->adapter) {
    g_object_unref (gme->adapter);
    gme->adapter = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static GstFlowReturn
gst_gme_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstGmeDec *gme = GST_GME_DEC (gst_pad_get_parent (pad));

  /* Accumulate GME data until end-of-stream, then commence playback. */
  gst_adapter_push (gme->adapter, buffer);

  gst_object_unref (gme);

  return GST_FLOW_OK;
}

static gboolean
gst_gme_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstGmeDec *gme = GST_GME_DEC (gst_pad_get_parent (pad));
  gboolean result = TRUE;
  gboolean forward = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* we get EOS when we loaded the complete file, now try to initialize the
       * decoding */
      if (!(result = gme_setup (gme))) {
        /* can't start, post an ERROR and push EOS downstream */
        GST_ELEMENT_ERROR (gme, STREAM, DEMUX, (NULL),
            ("can't start playback"));
        forward = TRUE;
      }
      break;
    default:
      break;
  }
  if (forward)
    result = gst_pad_push_event (gme->srcpad, event);
  else
    gst_event_unref (event);

  gst_object_unref (gme);

  return result;
}

static gboolean
gst_gme_dec_src_event (GstPad * pad, GstEvent * event)
{
  GstGmeDec *gme = GST_GME_DEC (gst_pad_get_parent (pad));
  gboolean result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      gboolean flush;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      gst_event_unref (event);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (gme, "seeking is only supported in TIME format");
        break;
      }

      if (start_type != GST_SEEK_TYPE_SET || stop_type != GST_SEEK_TYPE_NONE) {
        GST_DEBUG_OBJECT (gme, "unsupported seek type");
        break;
      }

      if (stop_type == GST_SEEK_TYPE_NONE)
        stop = GST_CLOCK_TIME_NONE;

      if (start_type == GST_SEEK_TYPE_SET) {
        guint64 cur = gme_tell (gme->player) * GST_MSECOND;
        guint64 dest = (guint64) start;

        if (gme->total_duration != GST_CLOCK_TIME_NONE)
          dest = CLAMP (dest, 0, gme->total_duration);
        else
          dest = MAX (0, dest);

        if (dest == cur)
          break;

        flush = (flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH;

        if (flush) {
          gst_pad_push_event (gme->srcpad, gst_event_new_flush_start ());
        } else {
          gst_pad_stop_task (gme->srcpad);
        }

        GST_PAD_STREAM_LOCK (gme->srcpad);

        if (flags & GST_SEEK_FLAG_SEGMENT) {
          gst_element_post_message (GST_ELEMENT (gme),
              gst_message_new_segment_start (GST_OBJECT (gme), format, cur));
        }

        if (flush) {
          gst_pad_push_event (gme->srcpad, gst_event_new_flush_stop ());
        }

        if (stop == GST_CLOCK_TIME_NONE
            && gme->total_duration != GST_CLOCK_TIME_NONE)
          stop = gme->total_duration;

        gst_pad_push_event (gme->srcpad, gst_event_new_new_segment (FALSE, rate,
                GST_FORMAT_TIME, dest, stop, dest));

        gme->seekpoint = dest / GST_MSECOND;    /* nsecs to msecs */
        gme->seeking = TRUE;

        gst_pad_start_task (gme->srcpad, (GstTaskFunction) gst_gme_play,
            gme->srcpad);

        GST_PAD_STREAM_UNLOCK (gme->srcpad);
        result = TRUE;
      }
      break;
    }
    default:
      result = gst_pad_push_event (gme->sinkpad, event);
      break;
  }

  gst_object_unref (gme);

  return result;
}

static gboolean
gst_gme_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstGmeDec *gme = GST_GME_DEC (gst_pad_get_parent (pad));
  gboolean result = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      if (!gme->initialized || format != GST_FORMAT_TIME
          || gme->total_duration == GST_CLOCK_TIME_NONE) {
        result = FALSE;
        break;
      }
      gst_query_set_duration (query, GST_FORMAT_TIME, gme->total_duration);
      break;
    }
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      if (!gme->initialized || format != GST_FORMAT_TIME) {
        result = FALSE;
        break;
      }
      gst_query_set_position (query, GST_FORMAT_TIME,
          (gint64) gme_tell (gme->player) * GST_MSECOND);
      break;
    }
    default:
      result = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (gme);

  return result;
}

static void
gst_gme_play (GstPad * pad)
{
  GstGmeDec *gme = GST_GME_DEC (gst_pad_get_parent (pad));
  GstFlowReturn flow_return;
  GstBuffer *out;
  gboolean seeking = gme->seeking;
  gme_err_t gme_err = NULL;
  const int NUM_SAMPLES = 1600; /* 4 bytes (stereo 16-bit) per sample */

  if (!seeking) {
    out = gst_buffer_new_and_alloc (NUM_SAMPLES * 4);
    gst_buffer_set_caps (out, GST_PAD_CAPS (pad));
    GST_BUFFER_TIMESTAMP (out) = gme_tell (gme->player) * GST_MSECOND;

    gme_err =
        gme_play (gme->player, NUM_SAMPLES * 2,
        (short *) GST_BUFFER_DATA (out));
    if (gme_err) {
      GST_ELEMENT_ERROR (gme, STREAM, DEMUX, (NULL), ("%s", gme_err));
      gst_pad_pause_task (pad);
      gst_pad_push_event (pad, gst_event_new_eos ());
      gst_object_unref (gme);
      return;
    }
  } else {
    gme_seek (gme->player, gme->seekpoint);
    gme->seeking = FALSE;

    out = gst_buffer_new ();
    gst_buffer_set_caps (out, GST_PAD_CAPS (pad));
  }

  if ((flow_return = gst_pad_push (gme->srcpad, out)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (gme, "pausing task, reason %s",
        gst_flow_get_name (flow_return));

    gst_pad_pause_task (pad);

    if (flow_return == GST_FLOW_UNEXPECTED) {
      gst_pad_push_event (pad, gst_event_new_eos ());
    } else if (flow_return < GST_FLOW_UNEXPECTED
        || flow_return == GST_FLOW_NOT_LINKED) {
      GST_ELEMENT_ERROR (gme, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_return)));

      gst_pad_push_event (pad, gst_event_new_eos ());
    }
  }

  if (gme_tell (gme->player) * GST_MSECOND > gme->total_duration) {
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new_eos ());
  }

  gst_object_unref (gme);

  return;
}

static gboolean
gme_setup (GstGmeDec * gme)
{
  gme_info_t *info;
  gme_err_t gme_err = NULL;
  GstTagList *taglist;
  guint64 total_duration;
  guint64 fade_time;
  GstBuffer *buffer;

  if (!gst_adapter_available (gme->adapter) || !gme_negotiate (gme)) {
    return FALSE;
  }

  buffer =
      gst_adapter_take_buffer (gme->adapter,
      gst_adapter_available (gme->adapter));

  gme_err =
      gme_open_data (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer),
      &gme->player, 32000);
  gst_buffer_unref (buffer);

  if (gme_err || !gme->player) {
    if (gme->player) {
      gme_delete (gme->player);
      gme->player = NULL;
    }

    GST_ELEMENT_ERROR (gme, STREAM, DEMUX, (NULL), ("%s", gme_err));

    return FALSE;
  }

  gme_err = gme_track_info (gme->player, &info, 0);

  taglist = gst_tag_list_new ();

  if (info->song && *info->song)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE,
        info->song, NULL);
  if (info->author && *info->author)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST,
        info->author, NULL);
  /* Prefer the name of the official soundtrack over the name of the game (since this is
   * how track numbers are derived)
   */
  if (info->game && *info->game)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ALBUM, info->game,
        NULL);

  if (info->comment && *info->comment)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_COMMENT,
        info->comment, NULL);
  if (info->dumper && *info->dumper)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_CONTACT,
        info->dumper, NULL);
  if (info->copyright && *info->copyright)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_COPYRIGHT,
        info->copyright, NULL);
  if (info->system && *info->system)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
        info->system, NULL);

  gme->total_duration = total_duration =
      gst_util_uint64_scale_int (info->play_length + (info->loop_length >
          0 ? 8000 : 0), GST_MSECOND, 1);
  fade_time = info->loop_length > 0 ? info->play_length : 0;

  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_DURATION, total_duration, NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (gme), gme->srcpad, taglist);

  g_free (info);

#ifdef HAVE_LIBGME_ACCURACY
  /* TODO: Is it worth it to make this optional? */
  gme_enable_accuracy (gme->player, 1);
#endif
  gme_start_track (gme->player, 0);
  if (fade_time)
    gme_set_fade (gme->player, fade_time);

  gst_pad_push_event (gme->srcpad, gst_event_new_new_segment (FALSE, 1.0,
          GST_FORMAT_TIME, 0, -1, 0));

  gst_pad_start_task (gme->srcpad, (GstTaskFunction) gst_gme_play, gme->srcpad);

  gme->initialized = TRUE;
  gme->seeking = FALSE;
  gme->seekpoint = 0;
  return gme->initialized;
}

static GstStateChangeReturn
gst_gme_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result;
  GstGmeDec *dec;

  dec = GST_GME_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dec->total_duration = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result == GST_STATE_CHANGE_FAILURE)
    return result;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (dec->adapter);
      break;
    default:
      break;
  }

  return result;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "gmedec", GST_RANK_PRIMARY,
      GST_TYPE_GME_DEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gmedec",
    "GME Audio Decoder",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
