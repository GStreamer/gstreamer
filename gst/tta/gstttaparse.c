/* GStreamer TTA plugin
 * (c) 2004 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstttaparse.c: TTA file parser
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>

#include <math.h>

#include "gstttaparse.h"
#include "ttadec.h"
#include "crc32.h"

GST_DEBUG_CATEGORY_STATIC (gst_tta_parse_debug);
#define GST_CAT_DEFAULT gst_tta_parse_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ttafile")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-tta, "
        "width = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 8000, 96000 ]")
    );

static void gst_tta_parse_class_init (GstTtaParseClass * klass);
static void gst_tta_parse_base_init (GstTtaParseClass * klass);
static void gst_tta_parse_init (GstTtaParse * ttaparse);

static gboolean gst_tta_parse_src_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_tta_parse_get_query_types (GstPad * pad);
static gboolean gst_tta_parse_query (GstPad * pad, GstQuery * query);
static gboolean gst_tta_parse_activate (GstPad * pad);
static gboolean gst_tta_parse_activate_pull (GstPad * pad, gboolean active);
static void gst_tta_parse_loop (GstTtaParse * ttaparse);
static GstStateChangeReturn gst_tta_parse_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_tta_parse_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstTtaParseClass),
      (GBaseInitFunc) gst_tta_parse_base_init,
      NULL,
      (GClassInitFunc) gst_tta_parse_class_init,
      NULL,
      NULL,
      sizeof (GstTtaParse),
      0,
      (GInstanceInitFunc) gst_tta_parse_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstTtaParse", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_tta_parse_base_init (GstTtaParseClass * klass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_static_metadata (element_class, "TTA file parser",
      "Codec/Demuxer/Audio",
      "Parses TTA files", "Arwed v. Merkatz <v.merkatz@gmx.net>");
}

static void
gst_tta_parse_dispose (GObject * object)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (object);

  g_free (ttaparse->index);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_tta_parse_class_init (GstTtaParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_tta_parse_dispose;
  gstelement_class->change_state = gst_tta_parse_change_state;
}

static void
gst_tta_parse_reset (GstTtaParse * ttaparse)
{
  ttaparse->header_parsed = FALSE;
  ttaparse->current_frame = 0;
  ttaparse->data_length = 0;
  ttaparse->samplerate = 0;
}

static void
gst_tta_parse_init (GstTtaParse * ttaparse)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ttaparse);

  ttaparse->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");

  ttaparse->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_fixed_caps (ttaparse->srcpad);
  gst_pad_set_query_type_function (ttaparse->srcpad,
      gst_tta_parse_get_query_types);
  gst_pad_set_query_function (ttaparse->srcpad, gst_tta_parse_query);
  gst_pad_set_event_function (ttaparse->srcpad, gst_tta_parse_src_event);

  gst_element_add_pad (GST_ELEMENT (ttaparse), ttaparse->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ttaparse), ttaparse->srcpad);
  gst_pad_set_activate_function (ttaparse->sinkpad, gst_tta_parse_activate);
  gst_pad_set_activatepull_function (ttaparse->sinkpad,
      gst_tta_parse_activate_pull);

  gst_tta_parse_reset (ttaparse);
}

static gboolean
gst_tta_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (GST_PAD_PARENT (pad));

  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &start_type, &start, &stop_type, &stop);

      if (format == GST_FORMAT_TIME) {
        if (flags & GST_SEEK_FLAG_FLUSH) {
          gst_pad_push_event (ttaparse->srcpad, gst_event_new_flush_start ());
          gst_pad_push_event (ttaparse->sinkpad, gst_event_new_flush_start ());
        } else {
          gst_pad_pause_task (ttaparse->sinkpad);
        }
        GST_PAD_STREAM_LOCK (ttaparse->sinkpad);

        switch (start_type) {
          case GST_SEEK_TYPE_CUR:
            ttaparse->current_frame += (start / GST_SECOND) / FRAME_TIME;
            break;
          case GST_SEEK_TYPE_END:
            ttaparse->current_frame += (start / GST_SECOND) / FRAME_TIME;
            break;
          case GST_SEEK_TYPE_SET:
            ttaparse->current_frame = (start / GST_SECOND) / FRAME_TIME;
            break;
          case GST_SEEK_TYPE_NONE:
            break;
        }
        res = TRUE;

        if (flags & GST_SEEK_FLAG_FLUSH) {
          gst_pad_push_event (ttaparse->srcpad, gst_event_new_flush_stop ());
          gst_pad_push_event (ttaparse->sinkpad, gst_event_new_flush_stop ());
        }

        gst_pad_push_event (ttaparse->srcpad, gst_event_new_new_segment (FALSE,
                1.0, GST_FORMAT_TIME, 0,
                ttaparse->num_frames * FRAME_TIME * GST_SECOND, 0));

        gst_pad_start_task (ttaparse->sinkpad,
            (GstTaskFunction) gst_tta_parse_loop, ttaparse, NULL);

        GST_PAD_STREAM_UNLOCK (ttaparse->sinkpad);

      } else {
        res = FALSE;
      }

      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static const GstQueryType *
gst_tta_parse_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return types;
}

static gboolean
gst_tta_parse_query (GstPad * pad, GstQuery * query)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 cur;

      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_TIME:
          cur = ttaparse->index[ttaparse->current_frame].time;
          break;
        default:
          format = GST_FORMAT_BYTES;
          cur = ttaparse->index[ttaparse->current_frame].pos;
          break;
      }
      gst_query_set_position (query, format, cur);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 end;

      gst_query_parse_duration (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_TIME:
          end = ((gdouble) ttaparse->data_length /
              (gdouble) ttaparse->samplerate) * GST_SECOND;
          break;
        default:
          format = GST_FORMAT_BYTES;
          end = ttaparse->index[ttaparse->num_frames].pos +
              ttaparse->index[ttaparse->num_frames].size;
          break;
      }
      gst_query_set_duration (query, format, end);
      break;
    }
    default:
      return FALSE;
      break;
  }
  return TRUE;
}

static gboolean
gst_tta_parse_activate (GstPad * pad)
{
  if (gst_pad_check_pull_range (pad)) {
    return gst_pad_activate_pull (pad, TRUE);
  }
  return FALSE;
}

static gboolean
gst_tta_parse_activate_pull (GstPad * pad, gboolean active)
{
  GstTtaParse *ttaparse = GST_TTA_PARSE (GST_OBJECT_PARENT (pad));

  if (active) {
    gst_pad_start_task (pad, (GstTaskFunction) gst_tta_parse_loop, ttaparse,
        NULL);
  } else {
    gst_pad_stop_task (pad);
  }

  return TRUE;
}

static GstFlowReturn
gst_tta_parse_parse_header (GstTtaParse * ttaparse)
{
  guchar *data;
  GstBuffer *buf = NULL;
  guint32 crc;
  double frame_length;
  int num_frames;
  GstCaps *caps;
  int i;
  guint32 offset;
  GstEvent *discont;

  if (gst_pad_pull_range (ttaparse->sinkpad, 0, 22, &buf) != GST_FLOW_OK)
    goto pull_fail;
  data = GST_BUFFER_DATA (buf);
  ttaparse->channels = GST_READ_UINT16_LE (data + 6);
  ttaparse->bits = GST_READ_UINT16_LE (data + 8);
  ttaparse->samplerate = GST_READ_UINT32_LE (data + 10);
  ttaparse->data_length = GST_READ_UINT32_LE (data + 14);
  crc = crc32 (data, 18);
  if (crc != GST_READ_UINT32_LE (data + 18)) {
    GST_DEBUG ("Header CRC wrong!");
  }
  frame_length = FRAME_TIME * ttaparse->samplerate;
  num_frames = (ttaparse->data_length / frame_length) + 1;
  ttaparse->num_frames = num_frames;
  gst_buffer_unref (buf);

  ttaparse->index =
      (GstTtaIndex *) g_malloc (num_frames * sizeof (GstTtaIndex));
  if (gst_pad_pull_range (ttaparse->sinkpad,
          22, num_frames * 4 + 4, &buf) != GST_FLOW_OK)
    goto pull_fail;
  data = GST_BUFFER_DATA (buf);

  offset = 22 + num_frames * 4 + 4;     // header size + seektable size
  for (i = 0; i < num_frames; i++) {
    ttaparse->index[i].size = GST_READ_UINT32_LE (data + i * 4);
    ttaparse->index[i].pos = offset;
    offset += ttaparse->index[i].size;
    ttaparse->index[i].time = i * FRAME_TIME * GST_SECOND;
  }
  crc = crc32 (data, num_frames * 4);
  if (crc != GST_READ_UINT32_LE (data + num_frames * 4)) {
    GST_DEBUG ("Seektable CRC wrong!");
  }

  GST_DEBUG
      ("channels: %u, bits: %u, samplerate: %u, data_length: %u, num_frames: %u",
      ttaparse->channels, ttaparse->bits, ttaparse->samplerate,
      ttaparse->data_length, num_frames);

  ttaparse->header_parsed = TRUE;
  caps = gst_caps_new_simple ("audio/x-tta",
      "width", G_TYPE_INT, ttaparse->bits,
      "channels", G_TYPE_INT, ttaparse->channels,
      "rate", G_TYPE_INT, ttaparse->samplerate, NULL);
  gst_pad_set_caps (ttaparse->srcpad, caps);

  discont =
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0,
      num_frames * FRAME_TIME * GST_SECOND, 0);

  gst_pad_push_event (ttaparse->srcpad, discont);

  return GST_FLOW_OK;

pull_fail:
  {
    GST_ELEMENT_ERROR (ttaparse, STREAM, DEMUX, (NULL),
        ("Couldn't read header"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_tta_parse_stream_data (GstTtaParse * ttaparse)
{
  GstBuffer *buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;

  if (ttaparse->current_frame >= ttaparse->num_frames)
    goto found_eos;

  GST_DEBUG ("playing frame %u of %u", ttaparse->current_frame + 1,
      ttaparse->num_frames);
  if ((res = gst_pad_pull_range (ttaparse->sinkpad,
              ttaparse->index[ttaparse->current_frame].pos,
              ttaparse->index[ttaparse->current_frame].size,
              &buf)) != GST_FLOW_OK)
    goto pull_error;

  GST_BUFFER_OFFSET (buf) = ttaparse->index[ttaparse->current_frame].pos;
  GST_BUFFER_TIMESTAMP (buf) = ttaparse->index[ttaparse->current_frame].time;
  if (ttaparse->current_frame + 1 == ttaparse->num_frames) {
    guint32 samples =
        ttaparse->data_length % (gint64) (ttaparse->samplerate * FRAME_TIME);
    gdouble frametime = (gdouble) samples / (gdouble) ttaparse->samplerate;

    GST_BUFFER_DURATION (buf) = (guint64) (frametime * GST_SECOND);
  } else {
    GST_BUFFER_DURATION (buf) = FRAME_TIME * GST_SECOND;
  }
  gst_buffer_set_caps (buf, GST_PAD_CAPS (ttaparse->srcpad));

  if ((res = gst_pad_push (ttaparse->srcpad, buf)) != GST_FLOW_OK)
    goto push_error;
  ttaparse->current_frame++;

  return res;

found_eos:
  {
    GST_DEBUG ("found EOS");
    gst_pad_push_event (ttaparse->srcpad, gst_event_new_eos ());
    return GST_FLOW_FLUSHING;
  }
pull_error:
  {
    GST_DEBUG ("Error getting frame from the sinkpad");
    return res;
  }
push_error:
  {
    GST_DEBUG ("Error pushing on srcpad");
    return res;
  }
}

static void
gst_tta_parse_loop (GstTtaParse * ttaparse)
{
  GstFlowReturn ret;

  if (!ttaparse->header_parsed)
    if ((ret = gst_tta_parse_parse_header (ttaparse)) != GST_FLOW_OK)
      goto pause;
  if ((ret = gst_tta_parse_stream_data (ttaparse)) != GST_FLOW_OK)
    goto pause;

  return;

pause:
  GST_LOG_OBJECT (ttaparse, "pausing task, %s", gst_flow_get_name (ret));
  gst_pad_pause_task (ttaparse->sinkpad);
  if (ret == GST_FLOW_UNEXPECTED) {
    gst_pad_push_event (ttaparse->srcpad, gst_event_new_eos ());
  } else if (ret < GST_FLOW_UNEXPECTED || ret == GST_FLOW_NOT_LINKED) {
    GST_ELEMENT_ERROR (ttaparse, STREAM, FAILED,
        ("Internal data stream error."),
        ("streaming stopped, reason %s", gst_flow_get_name (ret)));
    gst_pad_push_event (ttaparse->srcpad, gst_event_new_eos ());
  }
}

static GstStateChangeReturn
gst_tta_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstTtaParse *ttaparse = GST_TTA_PARSE (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_tta_parse_reset (ttaparse);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_tta_parse_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "ttaparse",
          GST_RANK_NONE, GST_TYPE_TTA_PARSE)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (gst_tta_parse_debug, "ttaparse", 0,
      "tta file parser");

  return TRUE;
}
