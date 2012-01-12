/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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
 * SECTION:element-gsty4mdec
 *
 * The gsty4mdec element decodes uncompressed video in YUV4MPEG format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=file.y4m ! y4mdec ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsty4mdec.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 32768

GST_DEBUG_CATEGORY (y4mdec_debug);
#define GST_CAT_DEFAULT y4mdec_debug

/* prototypes */


static void gst_y4m_dec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_y4m_dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_y4m_dec_dispose (GObject * object);
static void gst_y4m_dec_finalize (GObject * object);

static GstFlowReturn gst_y4m_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_y4m_dec_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_y4m_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_y4m_dec_src_query (GstPad * pad, GstQuery * query);

static GstStateChangeReturn
gst_y4m_dec_change_state (GstElement * element, GstStateChange transition);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_y4m_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-yuv4mpeg, y4mversion=2")
    );

static GstStaticPadTemplate gst_y4m_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{I420,Y42B,Y444}"))
    );

/* class initialization */

GST_BOILERPLATE (GstY4mDec, gst_y4m_dec, GstElement, GST_TYPE_ELEMENT);

static void
gst_y4m_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_y4m_dec_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_y4m_dec_sink_template);

  gst_element_class_set_details_simple (element_class,
      "YUV4MPEG demuxer/decoder", "Codec/Demuxer",
      "Demuxes/decodes YUV4MPEG streams", "David Schleef <ds@schleef.org>");
}

static void
gst_y4m_dec_class_init (GstY4mDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_y4m_dec_set_property;
  gobject_class->get_property = gst_y4m_dec_get_property;
  gobject_class->dispose = gst_y4m_dec_dispose;
  gobject_class->finalize = gst_y4m_dec_finalize;
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_y4m_dec_change_state);

}

static void
gst_y4m_dec_init (GstY4mDec * y4mdec, GstY4mDecClass * y4mdec_class)
{
  y4mdec->adapter = gst_adapter_new ();

  y4mdec->sinkpad =
      gst_pad_new_from_static_template (&gst_y4m_dec_sink_template, "sink");
  gst_pad_set_event_function (y4mdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_sink_event));
  gst_pad_set_chain_function (y4mdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_chain));
  gst_element_add_pad (GST_ELEMENT (y4mdec), y4mdec->sinkpad);

  y4mdec->srcpad = gst_pad_new_from_static_template (&gst_y4m_dec_src_template,
      "src");
  gst_pad_set_event_function (y4mdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_src_event));
  gst_pad_set_query_function (y4mdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_src_query));
  gst_pad_use_fixed_caps (y4mdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (y4mdec), y4mdec->srcpad);

}

void
gst_y4m_dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_Y4M_DEC (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_y4m_dec_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_Y4M_DEC (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_y4m_dec_dispose (GObject * object)
{
  GstY4mDec *y4mdec;

  g_return_if_fail (GST_IS_Y4M_DEC (object));
  y4mdec = GST_Y4M_DEC (object);

  /* clean up as possible.  may be called multiple times */
  if (y4mdec->adapter) {
    g_object_unref (y4mdec->adapter);
    y4mdec->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_y4m_dec_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_Y4M_DEC (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_y4m_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_Y4M_DEC (element), GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstClockTime
gst_y4m_dec_frames_to_timestamp (GstY4mDec * y4mdec, int frame_index)
{
  return gst_util_uint64_scale (frame_index, GST_SECOND * y4mdec->fps_d,
      y4mdec->fps_n);
}

static int
gst_y4m_dec_timestamp_to_frames (GstY4mDec * y4mdec, GstClockTime timestamp)
{
  return gst_util_uint64_scale (timestamp, y4mdec->fps_n,
      GST_SECOND * y4mdec->fps_d);
}

static int
gst_y4m_dec_bytes_to_frames (GstY4mDec * y4mdec, gint64 bytes)
{
  if (bytes < y4mdec->header_size)
    return 0;
  return (bytes - y4mdec->header_size) / (y4mdec->frame_size + 6);
}

static gint64
gst_y4m_dec_frames_to_bytes (GstY4mDec * y4mdec, int frame_index)
{
  return y4mdec->header_size + (y4mdec->frame_size + 6) * frame_index;
}

static GstClockTime
gst_y4m_dec_bytes_to_timestamp (GstY4mDec * y4mdec, gint64 bytes)
{
  return gst_y4m_dec_frames_to_timestamp (y4mdec,
      gst_y4m_dec_bytes_to_frames (y4mdec, bytes));
}


static gboolean
gst_y4m_dec_parse_header (GstY4mDec * y4mdec, char *header)
{
  char *end;
  int format = 420;
  int interlaced_char = 0;

  if (memcmp (header, "YUV4MPEG2 ", 10) != 0) {
    return FALSE;
  }

  header += 10;
  while (*header) {
    GST_DEBUG_OBJECT (y4mdec, "parsing at '%s'", header);
    switch (*header) {
      case ' ':
        header++;
        break;
      case 'C':
        header++;
        format = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'W':
        header++;
        y4mdec->width = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'H':
        header++;
        y4mdec->height = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'I':
        header++;
        if (header[0] == 0) {
          GST_WARNING_OBJECT (y4mdec, "Expecting interlaced flag");
          return FALSE;
        }
        interlaced_char = header[0];
        header++;
        break;
      case 'F':
        header++;
        y4mdec->fps_n = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        if (header[0] != ':') {
          GST_WARNING_OBJECT (y4mdec, "Expecting :");
          return FALSE;
        }
        header++;
        y4mdec->fps_d = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'A':
        header++;
        y4mdec->par_n = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        if (header[0] != ':') {
          GST_WARNING_OBJECT (y4mdec, "Expecting :");
          return FALSE;
        }
        header++;
        y4mdec->par_d = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      default:
        GST_WARNING_OBJECT (y4mdec, "Unknown y4m header field '%c', ignoring",
            *header);
        while (*header && *header != ' ')
          header++;
        break;
    }
  }

  switch (format) {
    case 420:
      y4mdec->format = GST_VIDEO_FORMAT_I420;
      break;
    case 422:
      y4mdec->format = GST_VIDEO_FORMAT_Y42B;
      break;
    case 444:
      y4mdec->format = GST_VIDEO_FORMAT_Y444;
      break;
    default:
      GST_WARNING_OBJECT (y4mdec, "unknown y4m format %d", format);
      return FALSE;
  }

  if (y4mdec->width <= 0 || y4mdec->width > MAX_SIZE ||
      y4mdec->height <= 0 || y4mdec->height > MAX_SIZE) {
    GST_WARNING_OBJECT (y4mdec, "Dimensions %dx%d out of range",
        y4mdec->width, y4mdec->height);
    return FALSE;
  }

  y4mdec->frame_size = gst_video_format_get_size (y4mdec->format,
      y4mdec->width, y4mdec->height);

  switch (interlaced_char) {
    case 0:
    case '?':
    case 'p':
      y4mdec->interlaced = FALSE;
      break;
    case 't':
    case 'b':
      y4mdec->interlaced = TRUE;
      y4mdec->tff = (interlaced_char == 't');
      break;
    default:
      GST_WARNING_OBJECT (y4mdec, "Unknown interlaced char '%c'",
          interlaced_char);
      return FALSE;
      break;
  }

  if (y4mdec->fps_n == 0)
    y4mdec->fps_n = 1;
  if (y4mdec->fps_d == 0)
    y4mdec->fps_d = 1;
  if (y4mdec->par_n == 0)
    y4mdec->par_n = 1;
  if (y4mdec->par_d == 0)
    y4mdec->par_d = 1;

  return TRUE;
error:
  GST_WARNING_OBJECT (y4mdec, "Expecting number y4m header at '%s'", header);
  return FALSE;
}

static GstFlowReturn
gst_y4m_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstY4mDec *y4mdec;
  int n_avail;
  GstFlowReturn flow_ret = GST_FLOW_OK;
#define MAX_HEADER_LENGTH 80
  char header[MAX_HEADER_LENGTH];
  int i;
  int len;

  y4mdec = GST_Y4M_DEC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (y4mdec, "chain");

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    GST_DEBUG ("got discont");
    gst_adapter_clear (y4mdec->adapter);
  }

  gst_adapter_push (y4mdec->adapter, buffer);
  n_avail = gst_adapter_available (y4mdec->adapter);

  if (!y4mdec->have_header) {
    gboolean ret;
    GstCaps *caps;

    if (n_avail < MAX_HEADER_LENGTH)
      return GST_FLOW_OK;

    gst_adapter_copy (y4mdec->adapter, (guint8 *) header, 0, MAX_HEADER_LENGTH);

    header[MAX_HEADER_LENGTH - 1] = 0;
    for (i = 0; i < MAX_HEADER_LENGTH; i++) {
      if (header[i] == 0x0a)
        header[i] = 0;
    }

    ret = gst_y4m_dec_parse_header (y4mdec, header);
    if (!ret) {
      GST_ELEMENT_ERROR (y4mdec, STREAM, DECODE,
          ("Failed to parse YUV4MPEG header"), (NULL));
      return GST_FLOW_ERROR;
    }

    y4mdec->header_size = strlen (header) + 1;
    gst_adapter_flush (y4mdec->adapter, y4mdec->header_size);

    caps = gst_video_format_new_caps_interlaced (y4mdec->format,
        y4mdec->width, y4mdec->height,
        y4mdec->fps_n, y4mdec->fps_d,
        y4mdec->par_n, y4mdec->par_d, y4mdec->interlaced);
    ret = gst_pad_set_caps (y4mdec->srcpad, caps);
    gst_caps_unref (caps);
    if (!ret) {
      GST_DEBUG_OBJECT (y4mdec, "Couldn't set caps on src pad");
      return GST_FLOW_ERROR;
    }

    y4mdec->have_header = TRUE;
  }

  if (y4mdec->have_new_segment) {
    GstEvent *event;
    GstClockTime start = gst_y4m_dec_bytes_to_timestamp (y4mdec,
        y4mdec->segment_start);
    GstClockTime stop = gst_y4m_dec_bytes_to_timestamp (y4mdec,
        y4mdec->segment_stop);
    GstClockTime position = gst_y4m_dec_bytes_to_timestamp (y4mdec,
        y4mdec->segment_position);

    event = gst_event_new_new_segment (FALSE, 1.0,
        GST_FORMAT_TIME, start, stop, position);

    gst_pad_push_event (y4mdec->srcpad, event);
    //gst_event_unref (event);

    y4mdec->have_new_segment = FALSE;
    y4mdec->frame_index = gst_y4m_dec_bytes_to_frames (y4mdec,
        y4mdec->segment_position);
    GST_DEBUG ("new frame_index %d", y4mdec->frame_index);

  }

  while (1) {
    n_avail = gst_adapter_available (y4mdec->adapter);
    if (n_avail < MAX_HEADER_LENGTH)
      break;

    gst_adapter_copy (y4mdec->adapter, (guint8 *) header, 0, MAX_HEADER_LENGTH);
    header[MAX_HEADER_LENGTH - 1] = 0;
    for (i = 0; i < MAX_HEADER_LENGTH; i++) {
      if (header[i] == 0x0a)
        header[i] = 0;
    }
    if (memcmp (header, "FRAME", 5) != 0) {
      GST_ELEMENT_ERROR (y4mdec, STREAM, DECODE,
          ("Failed to parse YUV4MPEG frame"), (NULL));
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    len = strlen (header);
    if (n_avail < y4mdec->frame_size + len + 1) {
      /* not enough data */
      GST_DEBUG ("not enough data for frame %d < %d",
          n_avail, y4mdec->frame_size + len + 1);
      break;
    }

    gst_adapter_flush (y4mdec->adapter, len + 1);

    buffer = gst_adapter_take_buffer (y4mdec->adapter, y4mdec->frame_size);

    GST_BUFFER_CAPS (buffer) = gst_caps_ref (GST_PAD_CAPS (y4mdec->srcpad));
    GST_BUFFER_TIMESTAMP (buffer) =
        gst_y4m_dec_frames_to_timestamp (y4mdec, y4mdec->frame_index);
    GST_BUFFER_DURATION (buffer) =
        gst_y4m_dec_frames_to_timestamp (y4mdec, y4mdec->frame_index + 1) -
        GST_BUFFER_TIMESTAMP (buffer);
    if (y4mdec->interlaced && y4mdec->tff) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_TFF);
    }

    y4mdec->frame_index++;

    flow_ret = gst_pad_push (y4mdec->srcpad, buffer);
    if (flow_ret != GST_FLOW_OK)
      break;
  }

  gst_object_unref (y4mdec);
  GST_DEBUG ("returning %d", flow_ret);
  return flow_ret;
}

static gboolean
gst_y4m_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstY4mDec *y4mdec;

  y4mdec = GST_Y4M_DEC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (y4mdec, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      gdouble applied_rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;

      gst_event_parse_new_segment_full (event, &update, &rate,
          &applied_rate, &format, &start, &stop, &position);

      GST_DEBUG ("new_segment: update: %d rate: %g applied_rate: %g "
          "format: %d start: %" G_GUINT64_FORMAT " stop: %" G_GUINT64_FORMAT
          " position %" G_GUINT64_FORMAT,
          update, rate, applied_rate, format, start, stop, position);

      if (format == GST_FORMAT_BYTES) {
        y4mdec->segment_start = start;
        y4mdec->segment_stop = stop;
        y4mdec->segment_position = position;
        y4mdec->have_new_segment = TRUE;
      }

      res = TRUE;
      /* not sure why it's not forwarded, but let's unref it so it
         doesn't leak, remove the unref if it gets forwarded again */
      gst_event_unref (event);
      //res = gst_pad_push_event (y4mdec->srcpad, event);
    }
      break;
    case GST_EVENT_EOS:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
  }

  gst_object_unref (y4mdec);
  return res;
}

static gboolean
gst_y4m_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstY4mDec *y4mdec;

  y4mdec = GST_Y4M_DEC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (y4mdec, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      int framenum;
      guint64 byte;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
          &start, &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        break;
      }

      framenum = gst_y4m_dec_timestamp_to_frames (y4mdec, start);
      GST_DEBUG ("seeking to frame %d", framenum);

      byte = gst_y4m_dec_frames_to_bytes (y4mdec, framenum);
      GST_DEBUG ("offset %d", (int) byte);

      gst_event_unref (event);
      event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
          start_type, byte, stop_type, -1);

      res = gst_pad_push_event (y4mdec->sinkpad, event);
    }
      break;
    default:
      res = gst_pad_push_event (y4mdec->sinkpad, event);
      break;
  }

  gst_object_unref (y4mdec);
  return res;
}

static gboolean
gst_y4m_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstY4mDec *y4mdec = GST_Y4M_DEC (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      GstPad *peer;

      GST_DEBUG ("duration query");

      gst_query_parse_duration (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        GST_DEBUG_OBJECT (y4mdec, "not handling duration query in format %d",
            format);
        break;
      }

      peer = gst_pad_get_peer (y4mdec->sinkpad);
      if (peer) {
        GstQuery *peer_query = gst_query_new_duration (GST_FORMAT_BYTES);

        res = gst_pad_query (peer, peer_query);
        if (res) {
          gint64 duration;
          int n_frames;

          gst_query_parse_duration (peer_query, &format, &duration);

          n_frames = gst_y4m_dec_bytes_to_frames (y4mdec, duration);
          GST_DEBUG ("duration in frames %d", n_frames);

          duration = gst_y4m_dec_frames_to_timestamp (y4mdec, n_frames);
          GST_DEBUG ("duration in time %" GST_TIME_FORMAT,
              GST_TIME_ARGS (duration));

          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          res = TRUE;
        }

        gst_query_unref (peer_query);
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (y4mdec);
  return res;
}


static gboolean
plugin_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "y4mdec", GST_RANK_SECONDARY,
      gst_y4m_dec_get_type ());

  GST_DEBUG_CATEGORY_INIT (y4mdec_debug, "y4mdec", 0, "y4mdec element");

  return TRUE;
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "y4mdec",
    "Demuxes/decodes YUV4MPEG streams",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
