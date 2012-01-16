/* Copyright (C) 2004-2005 Michael Pyne <michael dot pyne at kdemail net>
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

#include "gstspc.h"

#include <string.h>
#include <glib/gprintf.h>
#include <glib.h>

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-spc"));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) 32000, " "channels = (int) 2"));

GST_BOILERPLATE (GstSpcDec, gst_spc_dec, GstElement, GST_TYPE_ELEMENT);

static GstFlowReturn gst_spc_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_spc_dec_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_spc_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_spc_dec_src_query (GstPad * pad, GstQuery * query);
static GstStateChangeReturn gst_spc_dec_change_state (GstElement * element,
    GstStateChange transition);
static void spc_play (GstPad * pad);
static void gst_spc_dec_dispose (GObject * object);
static gboolean spc_setup (GstSpcDec * spc);

static gboolean
spc_negotiate (GstSpcDec * spc)
{
  GstCaps *allowed, *caps;
  GstStructure *structure;
  gint width = 16, depth = 16;
  gboolean sign;
  int rate = 32000;
  int channels = 2;

  allowed = gst_pad_get_allowed_caps (spc->srcpad);
  if (!allowed) {
    GST_DEBUG_OBJECT (spc, "couldn't get allowed caps");
    return FALSE;
  }

  GST_DEBUG_OBJECT (spc, "allowed caps: %" GST_PTR_FORMAT, allowed);

  structure = gst_caps_get_structure (allowed, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "depth", &depth);

  if (width && depth && width != depth) {
    GST_DEBUG_OBJECT (spc, "width %d and depth %d are different", width, depth);
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
  gst_pad_set_caps (spc->srcpad, caps);

  gst_caps_unref (caps);
  gst_caps_unref (allowed);

  return TRUE;
}

static void
gst_spc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "OpenSPC SPC decoder",
      "Codec/Audio/Decoder",
      "Uses OpenSPC to emulate an SPC processor",
      "Chris Lee <clee@kde.org>, Brian Koropoff <bkoropoff@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_spc_dec_class_init (GstSpcDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_spc_dec_change_state);
  gobject_class->dispose = gst_spc_dec_dispose;
}

static const GstQueryType *
gst_spc_dec_src_query_type (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_DURATION,
    GST_QUERY_POSITION,
    (GstQueryType) 0
  };

  return query_types;
}


static void
gst_spc_dec_init (GstSpcDec * spc, GstSpcDecClass * klass)
{
  spc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  /* gst_pad_set_query_function (spc->sinkpad, NULL); */
  gst_pad_set_event_function (spc->sinkpad, gst_spc_dec_sink_event);
  gst_pad_set_chain_function (spc->sinkpad, gst_spc_dec_chain);
  gst_element_add_pad (GST_ELEMENT (spc), spc->sinkpad);

  spc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (spc->srcpad, gst_spc_dec_src_event);
  gst_pad_set_query_function (spc->srcpad, gst_spc_dec_src_query);
  gst_pad_set_query_type_function (spc->srcpad, gst_spc_dec_src_query_type);
  gst_pad_use_fixed_caps (spc->srcpad);
  gst_element_add_pad (GST_ELEMENT (spc), spc->srcpad);

  spc->buf = NULL;
  spc->initialized = FALSE;
  spc_tag_clear (&spc->tag_info);
}

static void
gst_spc_dec_dispose (GObject * object)
{
  GstSpcDec *spc = GST_SPC_DEC (object);

  if (spc->buf) {
    gst_buffer_unref (spc->buf);
    spc->buf = NULL;
  }

  spc_tag_free (&spc->tag_info);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static GstFlowReturn
gst_spc_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));

  if (spc->buf) {
    spc->buf = gst_buffer_join (spc->buf, buffer);
  } else {
    spc->buf = buffer;
  }

  gst_object_unref (spc);

  return GST_FLOW_OK;
}

static gboolean
gst_spc_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  gboolean result = TRUE;
  gboolean forward = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* we get EOS when we loaded the complete file, now try to initialize the
       * decoding */
      if (!(result = spc_setup (spc))) {
        /* can't start, post an ERROR and push EOS downstream */
        GST_ELEMENT_ERROR (spc, STREAM, DEMUX, (NULL),
            ("can't start playback"));
        forward = TRUE;
      }
      break;
    default:
      break;
  }
  if (forward)
    result = gst_pad_push_event (spc->srcpad, event);
  else
    gst_event_unref (event);

  gst_object_unref (spc);

  return result;
}

static gint64
gst_spc_duration (GstSpcDec * spc)
{
  gint64 total_ticks =
      spc->tag_info.time_intro +
      spc->tag_info.time_loop * spc->tag_info.loop_count +
      spc->tag_info.time_end;
  if (total_ticks) {
    return (gint64) gst_util_uint64_scale (total_ticks, GST_SECOND, 64000);
  } else if (spc->tag_info.time_seconds) {
    gint64 time = (gint64) spc->tag_info.time_seconds * GST_SECOND;

    return time;
  } else {
    return (gint64) (3 * 60) * GST_SECOND;
  }
}

static gint64
gst_spc_fadeout (GstSpcDec * spc)
{
  if (spc->tag_info.time_fade) {
    return (gint64) gst_util_uint64_scale ((guint64) spc->tag_info.time_fade,
        GST_SECOND, 64000);
  } else if (spc->tag_info.time_fade_milliseconds) {
    return (gint64) (spc->tag_info.time_fade_milliseconds * GST_MSECOND);
  } else {
    return 10 * GST_SECOND;
  }
}


static gboolean
gst_spc_dec_src_event (GstPad * pad, GstEvent * event)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
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

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (spc, "seeking is only supported in TIME format");
        break;
      }

      if (start_type != GST_SEEK_TYPE_SET || stop_type != GST_SEEK_TYPE_NONE) {
        GST_DEBUG_OBJECT (spc, "unsupported seek type");
        break;
      }

      if (stop_type == GST_SEEK_TYPE_NONE)
        stop = GST_CLOCK_TIME_NONE;

      if (start_type == GST_SEEK_TYPE_SET) {
        guint64 cur =
            gst_util_uint64_scale (spc->byte_pos, GST_SECOND, 32000 * 2 * 2);
        guint64 dest = (guint64) start;

        dest = CLAMP (dest, 0, gst_spc_duration (spc) + gst_spc_fadeout (spc));

        if (dest == cur)
          break;

        flush = (flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH;

        if (flush) {
          gst_pad_push_event (spc->srcpad, gst_event_new_flush_start ());
        } else {
          gst_pad_stop_task (spc->srcpad);
        }

        GST_PAD_STREAM_LOCK (spc->srcpad);

        if (flags & GST_SEEK_FLAG_SEGMENT) {
          gst_element_post_message (GST_ELEMENT (spc),
              gst_message_new_segment_start (GST_OBJECT (spc), format, cur));
        }

        if (flush) {
          gst_pad_push_event (spc->srcpad, gst_event_new_flush_stop ());
        }

        if (stop == GST_CLOCK_TIME_NONE)
          stop = (guint64) (gst_spc_duration (spc) + gst_spc_fadeout (spc));

        gst_pad_push_event (spc->srcpad, gst_event_new_new_segment (FALSE, rate,
                GST_FORMAT_TIME, dest, stop, dest));

        /* spc->byte_pos += OSPC_Run(-1, NULL, (unsigned int) (gst_util_uint64_scale(dest - cur, 32000*2*2, GST_SECOND))); */
        spc->seekpoint =
            gst_util_uint64_scale (dest, 32000 * 2 * 2, GST_SECOND);
        spc->seeking = TRUE;

        gst_pad_start_task (spc->srcpad, (GstTaskFunction) spc_play,
            spc->srcpad);

        GST_PAD_STREAM_UNLOCK (spc->srcpad);
        result = TRUE;
      }
      break;
    }
    default:
      break;
  }

  gst_event_unref (event);
  gst_object_unref (spc);

  return result;
}

static gboolean
gst_spc_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  gboolean result = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      if (!spc->initialized || format != GST_FORMAT_TIME) {
        result = FALSE;
        break;
      }
      gst_query_set_duration (query, GST_FORMAT_TIME,
          gst_spc_duration (spc) + gst_spc_fadeout (spc));
      break;
    }
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      if (!spc->initialized || format != GST_FORMAT_TIME) {
        result = FALSE;
        break;
      }
      gst_query_set_position (query, GST_FORMAT_TIME,
          (gint64) gst_util_uint64_scale (spc->byte_pos, GST_SECOND,
              32000 * 2 * 2));
      break;
    }
    default:
      result = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (spc);

  return result;
}

static void
spc_play (GstPad * pad)
{
  GstSpcDec *spc = GST_SPC_DEC (gst_pad_get_parent (pad));
  GstFlowReturn flow_return;
  GstBuffer *out;
  gboolean seeking = spc->seeking;
  gint64 duration, fade, end, position;

  if (!seeking) {
    out = gst_buffer_new_and_alloc (1600 * 4);
    gst_buffer_set_caps (out, GST_PAD_CAPS (pad));
    GST_BUFFER_TIMESTAMP (out) =
        (gint64) gst_util_uint64_scale ((guint64) spc->byte_pos, GST_SECOND,
        32000 * 2 * 2);
    spc->byte_pos += OSPC_Run (-1, (short *) GST_BUFFER_DATA (out), 1600 * 4);
  } else {
    if (spc->seekpoint < spc->byte_pos) {
      OSPC_Init (GST_BUFFER_DATA (spc->buf), GST_BUFFER_SIZE (spc->buf));
      spc->byte_pos = 0;
    }
    spc->byte_pos += OSPC_Run (-1, NULL, 1600 * 4);
    if (spc->byte_pos >= spc->seekpoint) {
      spc->seeking = FALSE;
    }
    out = gst_buffer_new ();
    gst_buffer_set_caps (out, GST_PAD_CAPS (pad));
  }

  duration = gst_spc_duration (spc);
  fade = gst_spc_fadeout (spc);
  end = duration + fade;
  position =
      (gint64) gst_util_uint64_scale ((guint64) spc->byte_pos, GST_SECOND,
      32000 * 2 * 2);

  if (position >= duration) {
    gint16 *data = (gint16 *) GST_BUFFER_DATA (out);
    guint32 size = GST_BUFFER_SIZE (out) / sizeof (gint16);
    unsigned int i;

    gint64 num = (fade - (position - duration));

    for (i = 0; i < size; i++) {
      /* Apply a parabolic volume envelope */
      data[i] = (gint16) (data[i] * num / fade * num / fade);
    }
  }

  if ((flow_return = gst_pad_push (spc->srcpad, out)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (spc, "pausing task, reason %s",
        gst_flow_get_name (flow_return));

    gst_pad_pause_task (pad);

    if (flow_return <= GST_FLOW_UNEXPECTED
        || flow_return == GST_FLOW_NOT_LINKED) {
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
  }

  if (position >= end) {
    gst_pad_pause_task (pad);
    gst_pad_push_event (pad, gst_event_new_eos ());
  }

  gst_object_unref (spc);

  return;
}

static gboolean
spc_setup (GstSpcDec * spc)
{
  spc_tag_info *info;
  GstTagList *taglist;
  guint64 total_duration;

  if (!spc->buf || !spc_negotiate (spc)) {
    return FALSE;
  }

  info = &(spc->tag_info);

  spc_tag_get_info (GST_BUFFER_DATA (spc->buf), GST_BUFFER_SIZE (spc->buf),
      info);

  taglist = gst_tag_list_new ();

  if (info->title)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE,
        info->title, NULL);
  if (info->artist)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST,
        info->artist, NULL);
  /* Prefer the name of the official soundtrack over the name of the game (since this is
   * how track numbers are derived)
   */
  if (info->album)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ALBUM,
        info->album, NULL);
  else if (info->game)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ALBUM, info->game,
        NULL);
  if (info->year) {
    GDate *date = g_date_new_dmy (1, 1, info->year);

    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_DATE, date, NULL);
    g_date_free (date);
  }
  if (info->track) {
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_TRACK_NUMBER,
        info->track, NULL);
  }
  if (info->comment)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_COMMENT,
        info->comment, NULL);
  if (info->disc)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_ALBUM_VOLUME_NUMBER, info->disc, NULL);
  if (info->publisher)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
        info->publisher, NULL);
  if (info->dumper)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_CONTACT,
        info->dumper, NULL);
  if (info->emulator)
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
        info->emulator == EMU_ZSNES ? "ZSNES" : "Snes9x", NULL);

  total_duration = (guint64) (gst_spc_duration (spc) + gst_spc_fadeout (spc));

  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_DURATION, total_duration,
      GST_TAG_GENRE, "Game", GST_TAG_CODEC, "SPC700", NULL);

  gst_element_found_tags_for_pad (GST_ELEMENT (spc), spc->srcpad, taglist);

  /* spc_tag_info_free(&info); */


  if (OSPC_Init (GST_BUFFER_DATA (spc->buf), GST_BUFFER_SIZE (spc->buf)) != 0) {
    return FALSE;
  }

  gst_pad_push_event (spc->srcpad, gst_event_new_new_segment (FALSE, 1.0,
          GST_FORMAT_TIME, 0, -1, 0));

  gst_pad_start_task (spc->srcpad, (GstTaskFunction) spc_play, spc->srcpad);

  /* We can't unreference this buffer because we might need to re-initialize
   * the emulator with the original data during a reverse seek
   * gst_buffer_unref (spc->buf);
   * spc->buf = NULL;
   */
  spc->initialized = TRUE;
  spc->seeking = FALSE;
  spc->seekpoint = 0;
  spc->byte_pos = 0;
  return spc->initialized;
}

static GstStateChangeReturn
gst_spc_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result;
  GstSpcDec *dec;

  dec = GST_SPC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result == GST_STATE_CHANGE_FAILURE)
    return result;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (dec->buf) {
        gst_buffer_unref (dec->buf);
        dec->buf = NULL;
      }
      break;
    default:
      break;
  }

  return result;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "spcdec", GST_RANK_SECONDARY,
      GST_TYPE_SPC_DEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "spcdec",
    "OpenSPC Audio Decoder",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
