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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* TODO: - Add locking where appropiate
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>

#include "gstrawparse.h"

static void gst_raw_parse_dispose (GObject * object);

static GstFlowReturn gst_raw_parse_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_raw_parse_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_raw_parse_src_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_raw_parse_src_query_type (GstPad * pad);
static gboolean gst_raw_parse_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_raw_parse_convert (GstRawParse * rp,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value);

static GstStaticPadTemplate gst_raw_parse_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_raw_parse_debug);
#define GST_CAT_DEFAULT gst_raw_parse_debug

static const GstElementDetails raw_parse_details =
GST_ELEMENT_DETAILS ("Raw parser base class",
    "Filter/Raw",
    "Parses byte streams into raw frames",
    "Sebastian Dröge <slomo@circular-chaos.org>");

GST_BOILERPLATE (GstRawParse, gst_raw_parse, GstElement, GST_TYPE_ELEMENT);

static void
gst_raw_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_raw_parse_debug, "rawparse", 0,
      "rawparse element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_raw_parse_sink_pad_template));
  gst_element_class_set_details (gstelement_class, &raw_parse_details);
}

static void
gst_raw_parse_class_init (GstRawParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_raw_parse_dispose;
}

static void
gst_raw_parse_init (GstRawParse * rp, GstRawParseClass * g_class)
{
  GstPadTemplate *src_pad_template;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  rp->sinkpad =
      gst_pad_new_from_static_template (&gst_raw_parse_sink_pad_template,
      "sink");
  gst_element_add_pad (GST_ELEMENT (rp), rp->sinkpad);

  gst_pad_set_chain_function (rp->sinkpad, gst_raw_parse_chain);
  gst_pad_set_event_function (rp->sinkpad, gst_raw_parse_sink_event);

  src_pad_template = gst_element_class_get_pad_template (element_class, "src");

  if (src_pad_template) {
    rp->srcpad = gst_pad_new_from_template (src_pad_template, "src");
  } else {
    GstCaps *caps;

    GST_WARNING ("Subclass didn't specify a src pad template, using ANY caps");
    rp->srcpad = gst_pad_new ("src", GST_PAD_SRC);
    caps = gst_caps_new_any ();
    gst_pad_set_caps (rp->srcpad, caps);
    gst_caps_unref (caps);
  }

  gst_element_add_pad (GST_ELEMENT (rp), rp->srcpad);
  g_object_unref (src_pad_template);

  gst_pad_set_event_function (rp->srcpad, gst_raw_parse_src_event);

  gst_pad_set_query_type_function (rp->srcpad, gst_raw_parse_src_query_type);
  gst_pad_set_query_function (rp->srcpad, gst_raw_parse_src_query);

  rp->adapter = gst_adapter_new ();

  rp->fps_n = 1;
  rp->fps_d = 0;
  rp->framesize = 1;
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

  gst_segment_init (&rp->segment, GST_FORMAT_TIME);
  gst_adapter_clear (rp->adapter);
}

static GstFlowReturn
gst_raw_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRawParse *rp = GST_RAW_PARSE (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  GstRawParseClass *rp_class = GST_RAW_PARSE_GET_CLASS (rp);
  guint buffersize, nframes;

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (rp, "received DISCONT buffer");

    rp->discont = TRUE;
  }

  if (!rp->negotiated) {
    GstCaps *caps;

    if (rp_class->get_caps) {
      caps = rp_class->get_caps (rp);
    } else {
      GST_WARNING
          ("Subclass doesn't implement get_caps() method, using ANY caps");
      caps = gst_caps_new_any ();
    }

    rp->negotiated = gst_pad_set_caps (rp->srcpad, caps);
  }

  g_return_val_if_fail (rp->negotiated, GST_FLOW_ERROR);

  gst_adapter_push (rp->adapter, buffer);

  if (rp_class->multiple_frames_per_buffer) {
    buffersize = gst_adapter_available (rp->adapter);
    buffersize -= buffersize % rp->framesize;
    nframes = buffersize / rp->framesize;
  } else {
    buffersize = rp->framesize;
    nframes = 1;
  }

  while (gst_adapter_available (rp->adapter) >= buffersize) {
    buffer = gst_adapter_take_buffer (rp->adapter, buffersize);

    if (rp->fps_n) {
      GST_BUFFER_TIMESTAMP (buffer) = rp->segment.start +
          gst_util_uint64_scale (rp->n_frames, GST_SECOND * rp->fps_d,
          rp->fps_n);
      GST_BUFFER_DURATION (buffer) =
          gst_util_uint64_scale (nframes * GST_SECOND, rp->fps_d, rp->fps_n);
    } else {
      GST_BUFFER_TIMESTAMP (buffer) = rp->segment.start;
      GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
    }
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (rp->srcpad));
    if (rp->discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      rp->discont = FALSE;
    }

    rp->n_frames += nframes;

    ret = gst_pad_push (rp->srcpad, buffer);
    if (ret != GST_FLOW_OK)
      break;
  }

  gst_object_unref (rp);
  return ret;
}

static gboolean
gst_raw_parse_convert (GstRawParse * rp,
    GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean ret = FALSE;

  GST_DEBUG ("converting value %" G_GINT64_FORMAT " from %s to %s",
      src_value, gst_format_get_name (src_format),
      gst_format_get_name (dest_format));

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
gst_raw_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstRawParse *rp = GST_RAW_PARSE (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_raw_parse_reset (rp);
      ret = gst_pad_push_event (rp->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstClockTimeDiff start, stop, time;
      gdouble rate, arate;
      gboolean update;
      GstFormat format;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format == GST_FORMAT_TIME) {
        ret = gst_pad_push_event (rp->srcpad, event);
        gst_segment_set_newsegment_full (&rp->segment, update, rate, arate,
            GST_FORMAT_TIME, start, stop, time);
      } else {

        gst_event_unref (event);

        ret =
            gst_raw_parse_convert (rp, format, start, GST_FORMAT_TIME, &start);
        ret &= gst_raw_parse_convert (rp, format, time, GST_FORMAT_TIME, &time);
        ret &= gst_raw_parse_convert (rp, format, stop, GST_FORMAT_TIME, &stop);
        if (!ret) {
          GST_ERROR_OBJECT (rp,
              "Failed converting to GST_FORMAT_TIME format (%d)", format);
          break;
        }

        gst_segment_set_newsegment_full (&rp->segment, update, rate, arate,
            GST_FORMAT_TIME, start, stop, time);
        event = gst_event_new_new_segment (FALSE, rp->segment.rate,
            GST_FORMAT_TIME, start, stop, time);

        ret = gst_pad_push_event (rp->srcpad, event);
      }

      if (ret) {
        rp->n_frames = 0;
        rp->discont = TRUE;
        gst_adapter_clear (rp->adapter);
      }
      break;
    }
    default:
      ret = gst_pad_event_default (rp->sinkpad, event);
      break;
  }

  gst_object_unref (rp);

  return ret;
}

static gboolean
gst_raw_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstRawParse *rp = GST_RAW_PARSE (gst_pad_get_parent (pad));
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstFormat format;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      /* First try if upstream handles the seek */
      ret = gst_pad_push_event (rp->sinkpad, event);
      if (ret)
        goto done;

      /* Otherwise convert to bytes and push upstream */
      if (format == GST_FORMAT_TIME || format == GST_FORMAT_DEFAULT) {
        gst_event_unref (event);

        ret =
            gst_raw_parse_convert (rp, format, start, GST_FORMAT_BYTES, &start);
        ret &=
            gst_raw_parse_convert (rp, format, stop, GST_FORMAT_BYTES, &stop);

        if (ret) {
          /* Seek on a frame boundary */
          start -= start % rp->framesize;
          if (stop != -1)
            stop += rp->framesize - stop % rp->framesize;

          event =
              gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, start_type,
              start, stop_type, stop);

          ret = gst_pad_push_event (rp->sinkpad, event);
        }
      }
      break;
    }
    default:
      ret = gst_pad_event_default (rp->srcpad, event);
      break;
  }

done:
  gst_object_unref (rp);

  return ret;
}

static const GstQueryType *
gst_raw_parse_src_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return types;
}

static gboolean
gst_raw_parse_src_query (GstPad * pad, GstQuery * query)
{
  GstRawParse *rp = GST_RAW_PARSE (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  GST_DEBUG ("src_query %s", gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      GST_LOG ("query position");

      gst_query_parse_position (query, &format, NULL);

      time = gst_util_uint64_scale (rp->n_frames,
          GST_SECOND * rp->fps_d, rp->fps_n);
      ret = gst_raw_parse_convert (rp, GST_FORMAT_TIME, time, format, &value);

      gst_query_set_position (query, format, value);

      break;
    }
    case GST_QUERY_DURATION:{
      gint64 duration;
      GstFormat format;
      GstQuery *bquery;

      GST_LOG ("query duration");
      ret = gst_pad_peer_query (rp->srcpad, query);
      if (ret)
        goto done;

      gst_query_parse_duration (query, &format, NULL);
      /* We only handle TIME and DEFAULT format */
      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT)
        goto error;

      bquery = gst_query_new_duration (GST_FORMAT_BYTES);
      ret = gst_pad_peer_query (rp->srcpad, bquery);

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
    default:
      /* else forward upstream */
      ret = gst_pad_peer_query (rp->sinkpad, query);
      break;
  }

done:
  gst_object_unref (rp);
  return ret;
error:
  GST_DEBUG_OBJECT (rp, "query failed");
  goto done;
}

void
gst_raw_parse_set_framesize (GstRawParse * rp, int framesize)
{
  g_return_if_fail (GST_IS_RAW_PARSE (rp));
  g_return_if_fail (!rp->negotiated);

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

  *fps_n = rp->fps_n;
  *fps_d = rp->fps_d;
}

gboolean
gst_raw_parse_is_negotiated (GstRawParse * rp)
{
  g_return_val_if_fail (GST_IS_RAW_PARSE (rp), FALSE);

  return rp->negotiated;
}
