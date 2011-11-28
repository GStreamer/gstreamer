/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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
 * SECTION:element-speed
 *
 * Plays an audio stream at a different speed (by resampling the audio).
 * 
 * Do not use this element. Either use the 'pitch' element, or do a seek with
 * a non-1.0 rate parameter, this will have the same effect as using the speed
 * element (but relies on the decoder/demuxer to handle this correctly, also
 * requires a fairly up-to-date gst-plugins-base, as of February 2007).
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.ogg ! decodebin ! audioconvert ! speed speed=1.5 ! audioconvert ! audioresample ! autoaudiosink
 * ]| Plays an .ogg file at 1.5x speed.
 * </refsect2>
 *
 * Last reviewed on 2007-02-26 (0.10.4.1)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstspeed.h"

GST_DEBUG_CATEGORY_STATIC (speed_debug);
#define GST_CAT_DEFAULT speed_debug

enum
{
  ARG_0,
  ARG_SPEED
};

/* assumption here: sizeof (gfloat) = 4 */
#define GST_SPEED_AUDIO_CAPS \
    "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, MAX ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32; " \
    \
    "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, MAX ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 16, " \
    "depth = (int) 16, " \
    "signed = (boolean) true"

static GstStaticPadTemplate gst_speed_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_SPEED_AUDIO_CAPS)
    );

static GstStaticPadTemplate gst_speed_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_SPEED_AUDIO_CAPS)
    );

static void speed_base_init (gpointer g_class);
static void speed_class_init (GstSpeedClass * klass);
static void speed_init (GstSpeed * filter);

static void speed_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void speed_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec);

static gboolean speed_parse_caps (GstSpeed * filter, const GstCaps * caps);

static GstFlowReturn speed_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn speed_change_state (GstElement * element,
    GstStateChange transition);
static gboolean speed_sink_event (GstPad * pad, GstEvent * event);
static gboolean speed_src_event (GstPad * pad, GstEvent * event);

static GstElementClass *parent_class;   /* NULL */

static gboolean
speed_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSpeed *filter;
  GstPad *otherpad;
  gboolean ret;

  filter = GST_SPEED (gst_pad_get_parent (pad));

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  if ((ret = speed_parse_caps (filter, caps)))
    ret = gst_pad_set_caps (otherpad, caps);

  gst_object_unref (filter);

  return ret;

}

static gboolean
speed_parse_caps (GstSpeed * filter, const GstCaps * caps)
{

  GstStructure *structure;
  gint rate, chans, width;

  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  structure = gst_caps_get_structure (caps, 0);


  if (gst_structure_has_name (structure, "audio/x-raw-float"))
    filter->format = GST_SPEED_FORMAT_FLOAT;
  else if (gst_structure_has_name (structure, "audio/x-raw-int"))
    filter->format = GST_SPEED_FORMAT_INT;
  else
    return FALSE;

  if (!gst_structure_get_int (structure, "rate", &rate)
      || !gst_structure_get_int (structure, "width", &width)
      || !gst_structure_get_int (structure, "channels", &chans)) {
    return FALSE;
  }

  filter->rate = rate;
  filter->width = width;
  filter->channels = chans;

  if (filter->format == GST_SPEED_FORMAT_FLOAT) {
    filter->sample_size = filter->channels * filter->width / 8;
  } else {
    /* our caps only allow width == depth for now */
    filter->sample_size = filter->channels * filter->width / 8;
  }

  return TRUE;
}

GType
gst_speed_get_type (void)
{
  static GType speed_type = 0;

  if (!speed_type) {
    static const GTypeInfo speed_info = {
      sizeof (GstSpeedClass),
      speed_base_init,
      NULL,
      (GClassInitFunc) speed_class_init,
      NULL,
      NULL,
      sizeof (GstSpeed),
      0,
      (GInstanceInitFunc) speed_init,
    };

    speed_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSpeed", &speed_info, 0);
  }
  return speed_type;
}

static const GstQueryType *
speed_get_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return src_query_types;
}

static gboolean
speed_src_event (GstPad * pad, GstEvent * event)
{
  GstSpeed *filter;
  gboolean ret = FALSE;

  filter = GST_SPEED (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);
      gst_event_unref (event);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (filter, "only support seeks in TIME format");
        break;
      }

      if (start_type != GST_SEEK_TYPE_NONE && start != -1) {
        start *= filter->speed;
      }

      if (stop_type != GST_SEEK_TYPE_NONE && stop != -1) {
        stop *= filter->speed;
      }

      event = gst_event_new_seek (rate, format, flags, start_type, start,
          stop_type, stop);

      GST_LOG ("sending seek event: %" GST_PTR_FORMAT, event->structure);

      ret = gst_pad_send_event (GST_PAD_PEER (filter->sinkpad), event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (filter);
  return ret;

}

static gboolean
gst_speed_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean ret = TRUE;
  guint scale = 1;
  GstSpeed *filter;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  filter = GST_SPEED (GST_PAD_PARENT (pad));

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (filter->sample_size == 0) {
            ret = FALSE;
            break;
          }
          *dest_value = src_value / filter->sample_size;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = filter->sample_size * filter->rate;

          if (byterate == 0) {
            ret = FALSE;
            break;
          }
          *dest_value = src_value * GST_SECOND / byterate;
          break;
        }
        default:
          ret = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * filter->sample_size;
          break;
        case GST_FORMAT_TIME:
          if (filter->rate == 0) {
            ret = FALSE;
            break;
          }
          *dest_value = src_value * GST_SECOND / filter->rate;
          break;
        default:
          ret = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = filter->sample_size;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * scale * filter->rate / GST_SECOND;
          break;
        default:
          ret = FALSE;
      }
      break;
    default:
      ret = FALSE;
  }

  return ret;

}

static gboolean
speed_src_query (GstPad * pad, GstQuery * query)
{
  gboolean ret = TRUE;
  GstSpeed *filter;

  filter = GST_SPEED (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      GstFormat rformat = GST_FORMAT_TIME;
      gint64 cur;
      GstFormat conv_format = GST_FORMAT_TIME;

      /* save requested format */
      gst_query_parse_position (query, &format, NULL);

      /* query peer for current position in time */
      gst_query_set_position (query, GST_FORMAT_TIME, -1);

      if (!gst_pad_query_peer_position (filter->sinkpad, &rformat, &cur)) {
        GST_LOG_OBJECT (filter, "query on peer pad failed");
        goto error;
      }

      if (rformat == GST_FORMAT_BYTES)
        GST_LOG_OBJECT (filter,
            "peer pad returned current=%" G_GINT64_FORMAT " bytes", cur);
      else if (rformat == GST_FORMAT_TIME)
        GST_LOG_OBJECT (filter, "peer pad returned time=%" G_GINT64_FORMAT,
            cur);

      /* convert to time format */
      if (!gst_speed_convert (pad, rformat, cur, &conv_format, &cur)) {
        ret = FALSE;
        break;
      }

      /* adjust for speed factor */
      cur /= filter->speed;

      /* convert to time format */
      if (!gst_speed_convert (pad, conv_format, cur, &format, &cur)) {
        ret = FALSE;
        break;
      }
      gst_query_set_position (query, format, cur);

      GST_LOG_OBJECT (filter,
          "position query: we return %" G_GUINT64_FORMAT " (format %u)", cur,
          format);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      GstFormat rformat = GST_FORMAT_TIME;
      gint64 end;
      GstFormat conv_format = GST_FORMAT_TIME;

      /* save requested format */
      gst_query_parse_duration (query, &format, NULL);

      /* query peer for total length in time */
      gst_query_set_duration (query, GST_FORMAT_TIME, -1);

      if (!gst_pad_query_peer_duration (filter->sinkpad, &rformat, &end)) {
        GST_LOG_OBJECT (filter, "query on peer pad failed");
        goto error;
      }

      if (rformat == GST_FORMAT_BYTES)
        GST_LOG_OBJECT (filter,
            "peer pad returned total=%" G_GINT64_FORMAT " bytes", end);
      else if (rformat == GST_FORMAT_TIME)
        GST_LOG_OBJECT (filter, "peer pad returned time=%" G_GINT64_FORMAT,
            end);

      /* convert to time format */
      if (!gst_speed_convert (pad, rformat, end, &conv_format, &end)) {
        ret = FALSE;
        break;
      }

      /* adjust for speed factor */
      end /= filter->speed;

      /* convert to time format */
      if (!gst_speed_convert (pad, conv_format, end, &format, &end)) {
        ret = FALSE;
        break;
      }

      gst_query_set_duration (query, format, end);

      GST_LOG_OBJECT (filter,
          "duration query: we return %" G_GUINT64_FORMAT " (format %u)", end,
          format);

      break;
    }
    default:
      ret = FALSE;
      break;
  }

  gst_object_unref (filter);
  return ret;

error:

  gst_object_unref (filter);
  GST_DEBUG ("error handling query");
  return FALSE;
}

static void
speed_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Speed",
      "Filter/Effect/Audio",
      "Set speed/pitch on audio/raw streams (resampler)",
      "Andy Wingo <apwingo@eos.ncsu.edu>, "
      "Tim-Philipp Müller <tim@centricular.net>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_speed_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_speed_sink_template);
}

static void
speed_class_init (GstSpeedClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = speed_set_property;
  gobject_class->get_property = speed_get_property;
  gstelement_class->change_state = speed_change_state;

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SPEED,
      g_param_spec_float ("speed", "speed", "speed",
          0.1, 40.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
speed_init (GstSpeed * filter)
{
  filter->sinkpad =
      gst_pad_new_from_static_template (&gst_speed_sink_template, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad, speed_setcaps);
  gst_pad_set_chain_function (filter->sinkpad, speed_chain);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_event_function (filter->sinkpad, speed_sink_event);

  filter->srcpad =
      gst_pad_new_from_static_template (&gst_speed_src_template, "src");
  gst_pad_set_setcaps_function (filter->srcpad, speed_setcaps);
  gst_pad_set_query_type_function (filter->srcpad, speed_get_query_types);
  gst_pad_set_query_function (filter->srcpad, speed_src_query);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  gst_pad_set_event_function (filter->srcpad, speed_src_event);

  filter->offset = 0;
  filter->timestamp = 0;
  filter->sample_size = 0;

}

static inline guint
speed_chain_int16 (GstSpeed * filter, GstBuffer * in_buf, GstBuffer * out_buf,
    guint c, guint in_samples)
{
  gint16 *in_data, *out_data;
  gfloat interp, lower, i_float;
  guint i, j;

  in_data = ((gint16 *) GST_BUFFER_DATA (in_buf)) + c;
  out_data = ((gint16 *) GST_BUFFER_DATA (out_buf)) + c;

  lower = in_data[0];
  i_float = 0.5 * (filter->speed - 1.0);
  i = (guint) ceil (i_float);
  j = 0;

  while (i < in_samples) {
    interp = i_float - floor (i_float);

    out_data[j * filter->channels] =
        lower * (1 - interp) + in_data[i * filter->channels] * interp;

    lower = in_data[i * filter->channels];

    i_float += filter->speed;
    i = (guint) ceil (i_float);

    ++j;
  }

  return j;
}

static inline guint
speed_chain_float32 (GstSpeed * filter, GstBuffer * in_buf, GstBuffer * out_buf,
    guint c, guint in_samples)
{
  gfloat *in_data, *out_data;
  gfloat interp, lower, i_float;
  guint i, j;

  in_data = ((gfloat *) GST_BUFFER_DATA (in_buf)) + c;
  out_data = ((gfloat *) GST_BUFFER_DATA (out_buf)) + c;

  lower = in_data[0];
  i_float = 0.5 * (filter->speed - 1.0);
  i = (guint) ceil (i_float);
  j = 0;

  while (i < in_samples) {
    interp = i_float - floor (i_float);

    out_data[j * filter->channels] =
        lower * (1 - interp) + in_data[i * filter->channels] * interp;

    lower = in_data[i * filter->channels];

    i_float += filter->speed;
    i = (guint) ceil (i_float);

    ++j;
  }

  return j;
}


static gboolean
speed_sink_event (GstPad * pad, GstEvent * event)
{
  GstSpeed *filter;
  gboolean ret = FALSE;

  filter = GST_SPEED (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      gdouble rate;
      gboolean update = FALSE;
      GstFormat format;
      gint64 start_value, stop_value, base;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start_value,
          &stop_value, &base);
      gst_event_unref (event);

      if (format != GST_FORMAT_TIME) {
        GST_WARNING_OBJECT (filter, "newsegment event not in TIME format!");
        break;
      }

      g_assert (filter->speed > 0);

      if (start_value >= 0)
        start_value /= filter->speed;
      if (stop_value >= 0)
        stop_value /= filter->speed;
      base /= filter->speed;

      /* this would only really be correct if we clipped incoming data */
      filter->timestamp = start_value;

      /* set to NONE so it gets reset later based on the timestamp when we have
       * the samplerate */
      filter->offset = GST_BUFFER_OFFSET_NONE;

      ret =
          gst_pad_event_default (pad, gst_event_new_new_segment (update, rate,
              format, start_value, stop_value, base));
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }


  gst_object_unref (filter);

  return ret;


}

static GstFlowReturn
speed_chain (GstPad * pad, GstBuffer * in_buf)
{
  GstBuffer *out_buf;
  GstSpeed *filter;
  guint c, in_samples, out_samples, out_size;
  GstFlowReturn flow;

  filter = GST_SPEED (GST_PAD_PARENT (pad));

  if (G_UNLIKELY (filter->sample_size == 0 || filter->rate == 0)) {
    flow = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

  if (G_UNLIKELY (filter->offset == GST_BUFFER_OFFSET_NONE)) {
    filter->offset =
        gst_util_uint64_scale_int (filter->timestamp, filter->rate, GST_SECOND);
  }

  /* buffersize has to be aligned by samplesize */
  out_size = ceil ((gfloat) GST_BUFFER_SIZE (in_buf) / filter->speed);
  out_size = ((out_size + filter->sample_size - 1) / filter->sample_size) *
      filter->sample_size;

  flow =
      gst_pad_alloc_buffer_and_set_caps (filter->srcpad, -1, out_size,
      GST_PAD_CAPS (filter->srcpad), &out_buf);

  if (flow != GST_FLOW_OK)
    goto done;

  in_samples = GST_BUFFER_SIZE (in_buf) / filter->sample_size;

  out_samples = 0;

  for (c = 0; c < filter->channels; ++c) {
    if (filter->format == GST_SPEED_FORMAT_INT) {
      out_samples = speed_chain_int16 (filter, in_buf, out_buf, c, in_samples);
    } else {
      out_samples =
          speed_chain_float32 (filter, in_buf, out_buf, c, in_samples);
    }
  }

  GST_BUFFER_SIZE (out_buf) = out_samples * filter->sample_size;

  GST_BUFFER_OFFSET (out_buf) = filter->offset;
  GST_BUFFER_TIMESTAMP (out_buf) = filter->timestamp;

  filter->offset += GST_BUFFER_SIZE (out_buf) / filter->sample_size;
  filter->timestamp =
      gst_util_uint64_scale_int (filter->offset, GST_SECOND, filter->rate);

  /* make sure it's at least nominally a perfect stream */
  GST_BUFFER_DURATION (out_buf) =
      filter->timestamp - GST_BUFFER_TIMESTAMP (out_buf);

  flow = gst_pad_push (filter->srcpad, out_buf);

done:

  if (G_UNLIKELY (flow != GST_FLOW_OK))
    GST_DEBUG_OBJECT (filter, "flow: %s", gst_flow_get_name (flow));

  gst_buffer_unref (in_buf);
  return flow;
}

static void
speed_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSpeed *filter = GST_SPEED (object);

  switch (prop_id) {
    case ARG_SPEED:
      filter->speed = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
speed_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSpeed *filter = GST_SPEED (object);

  switch (prop_id) {
    case ARG_SPEED:
      g_value_set_float (value, filter->speed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static GstStateChangeReturn
speed_change_state (GstElement * element, GstStateChange transition)
{
  GstSpeed *speed = GST_SPEED (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      speed->offset = GST_BUFFER_OFFSET_NONE;
      speed->timestamp = 0;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (speed_debug, "speed", 0, "speed element");

  return gst_element_register (plugin, "speed", GST_RANK_NONE, GST_TYPE_SPEED);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "speed",
    "Set speed/pitch on audio/raw streams (resampler)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
