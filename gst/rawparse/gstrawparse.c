/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstrawparse.c:
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

/* TODO: - Add locking where appropiate
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>

#include "gstrawparse.h"

static void gst_raw_parse_dispose (GObject * object);

static gboolean gst_raw_parse_sink_activate (GstPad * sinkpad,
    GstObject * parent);
static gboolean gst_raw_parse_sink_activatemode (GstPad * sinkpad,
    GstObject * parent, GstPadMode mode, gboolean active);
static void gst_raw_parse_loop (GstElement * element);
static GstStateChangeReturn gst_raw_parse_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_raw_parse_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_raw_parse_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_raw_parse_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_raw_parse_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_raw_parse_convert (GstRawParse * rp, GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value);
static gboolean gst_raw_parse_handle_seek_pull (GstRawParse * rp,
    GstEvent * event);

static void gst_raw_parse_reset (GstRawParse * rp);

static GstStaticPadTemplate gst_raw_parse_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_raw_parse_debug);
#define GST_CAT_DEFAULT gst_raw_parse_debug

static void gst_raw_parse_class_init (GstRawParseClass * klass);
static void gst_raw_parse_init (GstRawParse * clip, GstRawParseClass * g_class);

static GstElementClass *parent_class;

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_raw_parse_get_type (void)
{
  static volatile gsize raw_parse_type = 0;

  if (g_once_init_enter (&raw_parse_type)) {
    GType _type;

    _type = g_type_register_static_simple (GST_TYPE_ELEMENT,
        "GstRawParse", sizeof (GstRawParseClass),
        (GClassInitFunc) gst_raw_parse_class_init, sizeof (GstRawParse),
        (GInstanceInitFunc) gst_raw_parse_init, G_TYPE_FLAG_ABSTRACT);

    g_once_init_leave (&raw_parse_type, _type);
  }
  return raw_parse_type;
}

static void
gst_raw_parse_class_init (GstRawParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_raw_parse_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_raw_parse_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_raw_parse_sink_pad_template));

  GST_DEBUG_CATEGORY_INIT (gst_raw_parse_debug, "rawparse", 0,
      "rawparse element");
}

static void
gst_raw_parse_init (GstRawParse * rp, GstRawParseClass * g_class)
{
  GstPadTemplate *src_pad_template;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  rp->sinkpad =
      gst_pad_new_from_static_template (&gst_raw_parse_sink_pad_template,
      "sink");
  gst_pad_set_chain_function (rp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_raw_parse_chain));
  gst_pad_set_event_function (rp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_raw_parse_sink_event));
  gst_pad_set_activate_function (rp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_raw_parse_sink_activate));
  gst_pad_set_activatemode_function (rp->sinkpad,
      GST_DEBUG_FUNCPTR (gst_raw_parse_sink_activatemode));
  gst_element_add_pad (GST_ELEMENT (rp), rp->sinkpad);

  src_pad_template = gst_element_class_get_pad_template (element_class, "src");

  if (src_pad_template) {
    rp->srcpad = gst_pad_new_from_template (src_pad_template, "src");
  } else {
    g_warning ("Subclass didn't specify a src pad template");
    g_assert_not_reached ();
  }

  gst_pad_set_event_function (rp->srcpad,
      GST_DEBUG_FUNCPTR (gst_raw_parse_src_event));
  gst_pad_set_query_function (rp->srcpad,
      GST_DEBUG_FUNCPTR (gst_raw_parse_src_query));
  gst_element_add_pad (GST_ELEMENT (rp), rp->srcpad);

  rp->adapter = gst_adapter_new ();

  rp->fps_n = 1;
  rp->fps_d = 0;
  rp->framesize = 1;

  gst_raw_parse_reset (rp);
}

static void
gst_raw_parse_dispose (GObject * object)
{
  GstRawParse *rp = GST_RAW_PARSE (object);

  if (rp->adapter) {
    g_object_unref (rp->adapter);
    rp->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_raw_parse_class_set_src_pad_template (GstRawParseClass * klass,
    const GstCaps * allowed_caps)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  g_return_if_fail (GST_IS_RAW_PARSE_CLASS (klass));
  g_return_if_fail (allowed_caps != NULL);
  g_return_if_fail (GST_IS_CAPS (allowed_caps));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_copy (allowed_caps)));
}

void
gst_raw_parse_class_set_multiple_frames_per_buffer (GstRawParseClass * klass,
    gboolean multiple_frames)
{
  g_return_if_fail (GST_IS_RAW_PARSE_CLASS (klass));

  klass->multiple_frames_per_buffer = multiple_frames;
}

static void
gst_raw_parse_reset (GstRawParse * rp)
{
  rp->n_frames = 0;
  rp->discont = TRUE;
  rp->negotiated = FALSE;

  gst_segment_init (&rp->segment, GST_FORMAT_TIME);
  gst_adapter_clear (rp->adapter);
}

static gboolean
gst_raw_parse_set_src_caps (GstRawParse * rp)
{
  GstRawParseClass *rp_class = GST_RAW_PARSE_GET_CLASS (rp);
  GstCaps *caps;

  if (rp->negotiated)
    return TRUE;

  if (rp_class->get_caps) {
    caps = rp_class->get_caps (rp);
  } else {
    GST_WARNING
        ("Subclass doesn't implement get_caps() method, using ANY caps");
    caps = gst_caps_new_any ();
  }

  rp->negotiated = gst_pad_set_caps (rp->srcpad, caps);
  gst_caps_unref (caps);

  return rp->negotiated;
}

static GstFlowReturn
gst_raw_parse_push_buffer (GstRawParse * rp, GstBuffer * buffer)
{
  GstFlowReturn ret;
  gint nframes;
  GstRawParseClass *rpclass;

  rpclass = GST_RAW_PARSE_GET_CLASS (rp);

  nframes = gst_buffer_get_size (buffer) / rp->framesize;

  if (rp->segment.rate < 0) {
    rp->n_frames -= nframes;
    rp->discont = TRUE;
  }

  GST_BUFFER_OFFSET (buffer) = rp->n_frames;
  GST_BUFFER_OFFSET_END (buffer) = rp->n_frames + nframes;

  if (rp->fps_n) {
    GST_BUFFER_TIMESTAMP (buffer) =
        gst_util_uint64_scale (rp->n_frames, GST_SECOND * rp->fps_d, rp->fps_n);
    GST_BUFFER_DURATION (buffer) =
        gst_util_uint64_scale ((rp->n_frames + nframes) * GST_SECOND, rp->fps_d,
        rp->fps_n) - GST_BUFFER_TIMESTAMP (buffer);
  } else {
    GST_BUFFER_TIMESTAMP (buffer) = rp->segment.start;
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  }

  if (rpclass->set_buffer_flags) {
    rpclass->set_buffer_flags (rp, buffer);
  }

  if (rp->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    rp->discont = FALSE;
  }

  if (rp->segment.rate >= 0) {
    rp->offset += gst_buffer_get_size (buffer);
    rp->n_frames += nframes;
  }

  rp->segment.position = GST_BUFFER_TIMESTAMP (buffer);

  GST_LOG_OBJECT (rp, "Pushing buffer with time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  ret = gst_pad_push (rp->srcpad, buffer);

  return ret;
}

static GstFlowReturn
gst_raw_parse_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstRawParse *rp = GST_RAW_PARSE (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstRawParseClass *rp_class = GST_RAW_PARSE_GET_CLASS (rp);
  guint buffersize;

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (rp, "received DISCONT buffer");
    gst_adapter_clear (rp->adapter);
    rp->discont = TRUE;
  }

  if (!gst_raw_parse_set_src_caps (rp))
    goto no_caps;

  if (rp->start_segment) {
    GST_DEBUG_OBJECT (rp, "sending start segment");
    gst_pad_push_event (rp->srcpad, rp->start_segment);
    rp->start_segment = NULL;
  }

  gst_adapter_push (rp->adapter, buffer);

  if (rp_class->multiple_frames_per_buffer) {
    buffersize = gst_adapter_available (rp->adapter);
    buffersize -= buffersize % rp->framesize;
  } else {
    buffersize = rp->framesize;
  }

  while (gst_adapter_available (rp->adapter) >= buffersize) {
    buffer = gst_adapter_take_buffer (rp->adapter, buffersize);

    ret = gst_raw_parse_push_buffer (rp, buffer);
    if (ret != GST_FLOW_OK)
      break;
  }
done:

  return ret;

  /* ERRORS */
no_caps:
  {
    GST_ERROR_OBJECT (rp, "could not set caps");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static void
gst_raw_parse_loop (GstElement * element)
{
  GstRawParse *rp = GST_RAW_PARSE (element);
  GstRawParseClass *rp_class = GST_RAW_PARSE_GET_CLASS (rp);
  GstFlowReturn ret;
  GstBuffer *buffer;
  gint size;

  if (G_UNLIKELY (rp->push_stream_start)) {
    gchar *stream_id;
    GstEvent *event;

    stream_id =
        gst_pad_create_stream_id (rp->srcpad, GST_ELEMENT_CAST (rp), NULL);

    event = gst_event_new_stream_start (stream_id);
    gst_event_set_group_id (event, gst_util_group_id_next ());

    GST_DEBUG_OBJECT (rp, "Pushing STREAM_START");
    gst_pad_push_event (rp->srcpad, event);
    rp->push_stream_start = FALSE;
    g_free (stream_id);
  }

  if (!gst_raw_parse_set_src_caps (rp))
    goto no_caps;

  if (rp->start_segment) {
    GST_DEBUG_OBJECT (rp, "sending start segment");
    gst_pad_push_event (rp->srcpad, rp->start_segment);
    rp->start_segment = NULL;
  }

  if (rp_class->multiple_frames_per_buffer && rp->framesize < 4096)
    size = 4096 - (4096 % rp->framesize);
  else
    size = rp->framesize;

  if (rp->segment.rate >= 0) {
    if (rp->offset + size > rp->upstream_length) {
      GstFormat fmt = GST_FORMAT_BYTES;

      if (!gst_pad_peer_query_duration (rp->sinkpad, fmt, &rp->upstream_length)) {
        GST_WARNING_OBJECT (rp,
            "Could not get upstream duration, trying to pull frame by frame");
        size = rp->framesize;
      } else if (rp->upstream_length < rp->offset + rp->framesize) {
        ret = GST_FLOW_EOS;
        goto pause;
      } else if (rp->offset + size > rp->upstream_length) {
        size = rp->upstream_length - rp->offset;
        size -= size % rp->framesize;
      }
    }
  } else {
    if (rp->offset == 0) {
      ret = GST_FLOW_EOS;
      goto pause;
    } else if (rp->offset < size) {
      size -= rp->offset;
    }
    rp->offset -= size;
  }

  buffer = NULL;
  ret = gst_pad_pull_range (rp->sinkpad, rp->offset, size, &buffer);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (rp, "pull_range (%" G_GINT64_FORMAT ", %u) "
        "failed, flow: %s", rp->offset, size, gst_flow_get_name (ret));
    buffer = NULL;
    goto pause;
  }

  if (gst_buffer_get_size (buffer) < size) {
    GST_DEBUG_OBJECT (rp, "Short read at offset %" G_GINT64_FORMAT
        ", got only %" G_GSIZE_FORMAT " of %u bytes", rp->offset,
        gst_buffer_get_size (buffer), size);

    if (size > rp->framesize) {
      gst_buffer_set_size (buffer, gst_buffer_get_size (buffer) -
          gst_buffer_get_size (buffer) % rp->framesize);
    } else {
      gst_buffer_unref (buffer);
      buffer = NULL;
      ret = GST_FLOW_EOS;
      goto pause;
    }
  }

  ret = gst_raw_parse_push_buffer (rp, buffer);
  if (ret != GST_FLOW_OK)
    goto pause;

  return;

  /* ERRORS */
no_caps:
  {
    GST_ERROR_OBJECT (rp, "could not negotiate caps");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto pause;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_LOG_OBJECT (rp, "pausing task, reason %s", reason);
    gst_pad_pause_task (rp->sinkpad);

    if (ret == GST_FLOW_EOS) {
      if (rp->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        GstClockTime stop;

        GST_LOG_OBJECT (rp, "Sending segment done");

        if ((stop = rp->segment.stop) == -1)
          stop = rp->segment.duration;

        gst_element_post_message (GST_ELEMENT_CAST (rp),
            gst_message_new_segment_done (GST_OBJECT_CAST (rp),
                rp->segment.format, stop));
        gst_pad_push_event (rp->srcpad,
            gst_event_new_segment_done (rp->segment.format, stop));
      } else {
        GST_LOG_OBJECT (rp, "Sending EOS, at end of stream");
        gst_pad_push_event (rp->srcpad, gst_event_new_eos ());
      }
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (rp, STREAM, FAILED,
          ("Internal data stream error."),
          ("stream stopped, reason %s", reason));
      gst_pad_push_event (rp->srcpad, gst_event_new_eos ());
    }
    return;
  }
}

static gboolean
gst_raw_parse_sink_activate (GstPad * sinkpad, GstObject * parent)
{
  GstQuery *query;
  gboolean pull_mode = FALSE;

  query = gst_query_new_scheduling ();

  if (gst_pad_peer_query (sinkpad, query))
    pull_mode = gst_query_has_scheduling_mode_with_flags (query,
        GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);

  gst_query_unref (query);

  if (pull_mode) {
    GST_DEBUG ("going to pull mode");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PULL, TRUE);
  } else {
    GST_DEBUG ("going to push (streaming) mode");
    return gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, TRUE);
  }
}

static gboolean
gst_raw_parse_sink_activatemode (GstPad * sinkpad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstRawParse *rp = GST_RAW_PARSE (parent);
  gboolean result;

  switch (mode) {
    case GST_PAD_MODE_PULL:
      if (active) {
        GstFormat format;
        gint64 duration;

        /* get the duration in bytes */
        format = GST_FORMAT_BYTES;
        result = gst_pad_peer_query_duration (sinkpad, format, &duration);
        if (result) {
          GST_DEBUG_OBJECT (rp, "got duration %" GST_TIME_FORMAT,
              GST_TIME_ARGS (duration));
          rp->upstream_length = duration;
          /* convert to time */
          gst_raw_parse_convert (rp, format, duration, GST_FORMAT_TIME,
              &duration);
        } else {
          rp->upstream_length = -1;
          duration = -1;
        }
        rp->segment.duration = duration;

        rp->push_stream_start = TRUE;

        result = gst_raw_parse_handle_seek_pull (rp, NULL);
        rp->mode = mode;
      } else {
        result = gst_pad_stop_task (sinkpad);
      }
      return result;
    case GST_PAD_MODE_PUSH:
      rp->mode = mode;
      return TRUE;
    default:
      return FALSE;
  }
}

static GstStateChangeReturn
gst_raw_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstRawParse *rp = GST_RAW_PARSE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&rp->segment, GST_FORMAT_TIME);
      rp->segment.position = 0;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_raw_parse_reset (rp);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_raw_parse_convert (GstRawParse * rp,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;

  GST_DEBUG ("converting value %" G_GINT64_FORMAT " from %s (%d) to %s (%d)",
      src_value, gst_format_get_name (src_format), src_format,
      gst_format_get_name (dest_format), dest_format);

  if (src_format == dest_format) {
    *dest_value = src_value;
    ret = TRUE;
    goto done;
  }

  if (src_value == -1) {
    *dest_value = -1;
    ret = TRUE;
    goto done;
  }

  /* bytes to frames */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_DEFAULT) {
    if (rp->framesize != 0) {
      *dest_value = gst_util_uint64_scale_int (src_value, 1, rp->framesize);
    } else {
      GST_ERROR ("framesize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* frames to bytes */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_BYTES) {
    *dest_value = gst_util_uint64_scale_int (src_value, rp->framesize, 1);
    ret = TRUE;
    goto done;
  }

  /* time to frames */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_DEFAULT) {
    if (rp->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          rp->fps_n, GST_SECOND * rp->fps_d);
    } else {
      GST_ERROR ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* frames to time */
  if (src_format == GST_FORMAT_DEFAULT && dest_format == GST_FORMAT_TIME) {
    if (rp->fps_n != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * rp->fps_d, rp->fps_n);
    } else {
      GST_ERROR ("framerate numerator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* time to bytes */
  if (src_format == GST_FORMAT_TIME && dest_format == GST_FORMAT_BYTES) {
    if (rp->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          rp->fps_n * rp->framesize, GST_SECOND * rp->fps_d);
    } else {
      GST_ERROR ("framerate denominator is 0");
      *dest_value = 0;
    }
    ret = TRUE;
    goto done;
  }

  /* bytes to time */
  if (src_format == GST_FORMAT_BYTES && dest_format == GST_FORMAT_TIME) {
    if (rp->fps_n != 0 && rp->framesize != 0) {
      *dest_value = gst_util_uint64_scale (src_value,
          GST_SECOND * rp->fps_d, rp->fps_n * rp->framesize);
    } else {
      GST_ERROR ("framerate denominator and/or framesize is 0");
      *dest_value = 0;
    }
    ret = TRUE;
  }

done:

  GST_DEBUG ("ret=%d result %" G_GINT64_FORMAT, ret, *dest_value);

  return ret;
}


static gboolean
gst_raw_parse_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRawParse *rp = GST_RAW_PARSE (parent);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_STOP:
      /* Only happens in push mode */
      gst_raw_parse_reset (rp);
      ret = gst_pad_push_event (rp->srcpad, event);
      break;
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      /* Only happens in push mode */

      gst_event_copy_segment (event, &segment);

      if (segment.format != GST_FORMAT_TIME) {
        gst_event_unref (event);

        segment.format = GST_FORMAT_TIME;

        ret =
            gst_raw_parse_convert (rp, segment.format, segment.start,
            GST_FORMAT_TIME, (gint64 *) & segment.start);
        ret &= gst_raw_parse_convert (rp, segment.format, segment.time,
            GST_FORMAT_TIME, (gint64 *) & segment.time);
        ret &= gst_raw_parse_convert (rp, segment.format, segment.stop,
            GST_FORMAT_TIME, (gint64 *) & segment.stop);
        if (!ret) {
          GST_ERROR_OBJECT (rp,
              "Failed converting to GST_FORMAT_TIME format (%d)",
              segment.format);
          break;
        }

        event = gst_event_new_segment (&segment);
      }

      gst_segment_copy_into (&segment, &rp->segment);

      if (rp->start_segment)
        gst_event_unref (rp->start_segment);
      rp->start_segment = event;
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}


static gboolean
gst_raw_parse_handle_seek_push (GstRawParse * rp, GstEvent * event)
{
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gboolean ret = FALSE;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  /* can't seek backwards yet */
  if (rate <= 0.0)
    goto wrong_rate;

  /* First try if upstream handles the seek */
  ret = gst_pad_push_event (rp->sinkpad, event);
  if (ret)
    return ret;

  /* Otherwise convert to bytes and push upstream */
  if (format == GST_FORMAT_TIME || format == GST_FORMAT_DEFAULT) {
    ret = gst_raw_parse_convert (rp, format, start, GST_FORMAT_BYTES, &start);
    ret &= gst_raw_parse_convert (rp, format, stop, GST_FORMAT_BYTES, &stop);

    if (ret) {
      /* Seek on a frame boundary */
      start -= start % rp->framesize;
      if (stop != -1)
        stop += rp->framesize - stop % rp->framesize;

      event =
          gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, start_type,
          start, stop_type, stop);

      ret = gst_pad_push_event (rp->sinkpad, event);
    } else {
      GST_DEBUG_OBJECT (rp, "Seek failed: couldn't convert to byte positions");
    }
  } else {
    GST_DEBUG_OBJECT (rp,
        "seeking is only supported in TIME or DEFAULT format");
  }
  return ret;

  /* ERRORS */
wrong_rate:
  {
    GST_DEBUG_OBJECT (rp, "Seek failed: negative rates not supported yet");
    return FALSE;
  }
}

static gboolean
gst_raw_parse_handle_seek_pull (GstRawParse * rp, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gint64 last_stop;
  gboolean ret = FALSE;
  gboolean flush;
  GstSegment seeksegment;

  if (event) {
    gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
        &stop_type, &stop);

    /* convert input offsets to time */
    ret = gst_raw_parse_convert (rp, format, start, GST_FORMAT_TIME, &start);
    ret &= gst_raw_parse_convert (rp, format, stop, GST_FORMAT_TIME, &stop);
    if (!ret)
      goto convert_failed;

    GST_DEBUG_OBJECT (rp, "converted start - stop to time");

    format = GST_FORMAT_TIME;

    gst_event_unref (event);
  } else {
    format = GST_FORMAT_TIME;
    flags = 0;
  }

  flush = ((flags & GST_SEEK_FLAG_FLUSH) != 0);

  /* start flushing up and downstream so that the loop function pauses and we
   * can acquire the STREAM_LOCK. */
  if (flush) {
    GST_LOG_OBJECT (rp, "flushing");
    gst_pad_push_event (rp->sinkpad, gst_event_new_flush_start ());
    gst_pad_push_event (rp->srcpad, gst_event_new_flush_start ());
  } else {
    GST_LOG_OBJECT (rp, "pause task");
    gst_pad_pause_task (rp->sinkpad);
  }

  GST_PAD_STREAM_LOCK (rp->sinkpad);

  memcpy (&seeksegment, &rp->segment, sizeof (GstSegment));

  if (event) {
    /* configure the seek values */
    gst_segment_do_seek (&seeksegment, rate, format, flags,
        start_type, start, stop_type, stop, NULL);
  }

  /* get the desired position */
  last_stop = seeksegment.position;

  GST_LOG_OBJECT (rp, "seeking to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (last_stop));

  /* convert the desired position to bytes */
  ret =
      gst_raw_parse_convert (rp, format, last_stop, GST_FORMAT_BYTES,
      &last_stop);

  /* prepare for streaming */
  if (flush) {
    GST_LOG_OBJECT (rp, "stop flush");
    gst_pad_push_event (rp->sinkpad, gst_event_new_flush_stop (TRUE));
    gst_pad_push_event (rp->srcpad, gst_event_new_flush_stop (TRUE));
  }

  if (ret) {
    /* seek done */

    /* Seek on a frame boundary */
    last_stop -= last_stop % rp->framesize;

    rp->offset = last_stop;
    rp->n_frames = last_stop / rp->framesize;

    GST_LOG_OBJECT (rp, "seeking to bytes %" G_GINT64_FORMAT, last_stop);

    memcpy (&rp->segment, &seeksegment, sizeof (GstSegment));

    if (rp->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT_CAST (rp),
          gst_message_new_segment_start (GST_OBJECT_CAST (rp),
              rp->segment.format, rp->segment.position));
    }

    /* for deriving a stop position for the playback segment from the seek
     * segment, we must take the duration when the stop is not set */
    if ((stop = rp->segment.stop) == -1)
      stop = rp->segment.duration;

    GST_DEBUG_OBJECT (rp, "preparing newsegment from %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, rp->segment.start, stop);

    /* now replace the old segment so that we send it in the stream thread the
     * next time it is scheduled. */
    if (rp->start_segment)
      gst_event_unref (rp->start_segment);
    rp->start_segment = gst_event_new_segment (&rp->segment);
  }
  rp->discont = TRUE;

  GST_LOG_OBJECT (rp, "start streaming");
  gst_pad_start_task (rp->sinkpad, (GstTaskFunction) gst_raw_parse_loop, rp,
      NULL);

  GST_PAD_STREAM_UNLOCK (rp->sinkpad);

  return ret;

  /* ERRORS */
convert_failed:
  {
    GST_DEBUG_OBJECT (rp, "Seek failed: couldn't convert to byte positions");
    return FALSE;
  }
}

static gboolean
gst_raw_parse_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRawParse *rp = GST_RAW_PARSE (parent);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (rp->mode == GST_PAD_MODE_PUSH)
        ret = gst_raw_parse_handle_seek_push (rp, event);
      else
        ret = gst_raw_parse_handle_seek_pull (rp, event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_raw_parse_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstRawParse *rp = GST_RAW_PARSE (parent);
  gboolean ret = FALSE;

  GST_DEBUG ("src_query %s", gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      GST_LOG ("query position");

      gst_query_parse_position (query, &format, NULL);

      time = rp->segment.position;
      ret = gst_raw_parse_convert (rp, GST_FORMAT_TIME, time, format, &value);

      gst_query_set_position (query, format, value);

      break;
    }
    case GST_QUERY_DURATION:{
      gint64 duration;
      GstFormat format;
      GstQuery *bquery;

      GST_LOG ("query duration");
      ret = gst_pad_peer_query (rp->sinkpad, query);
      if (ret)
        goto done;

      gst_query_parse_duration (query, &format, NULL);
      /* We only handle TIME and DEFAULT format */
      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT)
        goto error;

      bquery = gst_query_new_duration (GST_FORMAT_BYTES);
      ret = gst_pad_peer_query (rp->sinkpad, bquery);
      if (!ret) {
        gst_query_unref (bquery);
        goto error;
      }

      gst_query_parse_duration (bquery, NULL, &duration);
      gst_query_unref (bquery);

      ret =
          gst_raw_parse_convert (rp, GST_FORMAT_BYTES, duration, format,
          &duration);
      if (ret)
        gst_query_set_duration (query, format, duration);

      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_LOG ("query convert");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      ret = gst_raw_parse_convert (rp, src_fmt, src_val, dest_fmt, &dest_val);
      if (!ret)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      ret = TRUE;
      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt != GST_FORMAT_TIME && fmt != GST_FORMAT_DEFAULT
          && fmt != GST_FORMAT_BYTES) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
      } else if (rp->mode == GST_PAD_MODE_PUSH) {
        GstQuery *peerquery = gst_query_new_seeking (GST_FORMAT_BYTES);
        gboolean seekable;

        seekable = gst_pad_peer_query (rp->sinkpad, peerquery);
        if (seekable)
          gst_query_parse_seeking (peerquery, NULL, &seekable, NULL, NULL);

        gst_query_unref (peerquery);
        gst_query_set_seeking (query, fmt, seekable, seekable ? 0 : -1, -1);
      } else {
        gst_query_set_seeking (query, fmt, TRUE, 0, -1);
      }
      break;
    }
    default:
      /* else forward upstream */
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

done:
  return ret;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (rp, "query failed");
    goto done;
  }
}

void
gst_raw_parse_set_framesize (GstRawParse * rp, int framesize)
{
  g_return_if_fail (GST_IS_RAW_PARSE (rp));
  g_return_if_fail (!rp->negotiated);

  GST_DEBUG_OBJECT (rp, "framesize %d", framesize);
  rp->framesize = framesize;
}

void
gst_raw_parse_set_fps (GstRawParse * rp, int fps_n, int fps_d)
{
  g_return_if_fail (GST_IS_RAW_PARSE (rp));
  g_return_if_fail (!rp->negotiated);

  rp->fps_n = fps_n;
  rp->fps_d = fps_d;
}

void
gst_raw_parse_get_fps (GstRawParse * rp, int *fps_n, int *fps_d)
{
  g_return_if_fail (GST_IS_RAW_PARSE (rp));

  if (fps_n)
    *fps_n = rp->fps_n;
  if (fps_d)
    *fps_d = rp->fps_d;
}

gboolean
gst_raw_parse_is_negotiated (GstRawParse * rp)
{
  g_return_val_if_fail (GST_IS_RAW_PARSE (rp), FALSE);

  return rp->negotiated;
}
