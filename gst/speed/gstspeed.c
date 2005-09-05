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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstspeed.h"

/* elementfactory information */
static GstElementDetails speed_details = GST_ELEMENT_DETAILS ("Speed",
    "Filter/Effect/Audio",
    "Set speed/pitch on audio/raw streams (resampler)",
    "Andy Wingo <apwingo@eos.ncsu.edu>, "
    "Tim-Philipp Müller <tim@centricular.net>");


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
    "width = (int) 32, " \
    "buffer-frames = (int) 0; " \
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

static void speed_chain (GstPad * pad, GstData * data);

static GstStateChangeReturn speed_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class;   /* NULL */

static GstPadLinkReturn
speed_link (GstPad * pad, const GstCaps * caps)
{
  GstSpeed *filter;
  GstPad *otherpad;

  filter = GST_SPEED (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_IS_SPEED (filter), GST_PAD_LINK_REFUSED);
  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  if (!speed_parse_caps (filter, caps))
    return GST_PAD_LINK_REFUSED;

  return gst_pad_try_set_caps (otherpad, caps);
}

static gboolean
speed_parse_caps (GstSpeed * filter, const GstCaps * caps)
{
  const gchar *mimetype;
  GstStructure *structure;
  gboolean ret;

  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);
  if (strcmp (mimetype, "audio/x-raw-float") == 0)
    filter->format = GST_SPEED_FORMAT_FLOAT;
  else if (strcmp (mimetype, "audio/x-raw-int") == 0)
    filter->format = GST_SPEED_FORMAT_INT;
  else
    return FALSE;

  ret = gst_structure_get_int (structure, "rate", &filter->rate);
  ret &= gst_structure_get_int (structure, "channels", &filter->channels);
  ret &= gst_structure_get_int (structure, "width", &filter->width);

  filter->buffer_frames = 0;
  gst_structure_get_int (structure, "buffer-frames", &filter->buffer_frames);

  if (filter->format == GST_SPEED_FORMAT_FLOAT) {
    filter->sample_size = filter->channels * filter->width / 8;
  } else {
    /* our caps only allow width == depth for now */
    filter->sample_size = filter->channels * filter->width / 8;
  }

  return ret;
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
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return src_query_types;
}

static gboolean
speed_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * val)
{
  gboolean res = TRUE;
  GstSpeed *filter;

  filter = GST_SPEED (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_POSITION:
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_BYTES:
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_TIME:
        {
          gint64 peer_value;
          const GstFormat *peer_formats;

          res = FALSE;

          peer_formats = gst_pad_get_formats (GST_PAD_PEER (filter->sinkpad));

          while (peer_formats && *peer_formats && !res) {

            GstFormat peer_format = *peer_formats;

            /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (filter->sinkpad), type,
                    &peer_format, &peer_value)) {
              GstFormat conv_format;

              /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
              res = gst_pad_convert (filter->sinkpad,
                  peer_format, peer_value, &conv_format, val);

              /* adjust for speed factor */
              *val = (gint64) (((gdouble) * val) / filter->speed);

              /* and to final format */
              res &= gst_pad_convert (pad, GST_FORMAT_TIME, *val, format, val);
            }
            peer_formats++;
          }
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}

static void
speed_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &speed_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_speed_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_speed_sink_template));
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
          0.1, 40.0, 1.0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
speed_init (GstSpeed * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_speed_sink_template), "sink");
  gst_pad_set_link_function (filter->sinkpad, speed_link);
  gst_pad_set_chain_function (filter->sinkpad, speed_chain);
  gst_pad_set_getcaps_function (filter->sinkpad, gst_pad_proxy_getcaps);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_speed_src_template), "src");
  gst_pad_set_link_function (filter->srcpad, speed_link);
  gst_pad_set_getcaps_function (filter->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_query_type_function (filter->srcpad, speed_get_query_types);
  gst_pad_set_query_function (filter->srcpad, speed_src_query);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

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

static void
speed_chain (GstPad * pad, GstData * data)
{
  GstBuffer *in_buf, *out_buf;
  GstSpeed *filter;
  guint c, in_samples, out_samples, out_size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (data != NULL);

  filter = GST_SPEED (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_IS_SPEED (filter));

  if (GST_IS_EVENT (data)) {
    switch (GST_EVENT_TYPE (GST_EVENT (data))) {
      case GST_EVENT_DISCONTINUOUS:
      {
        gint64 timestamp, offset;

        if (gst_event_discont_get_value (GST_EVENT (data), GST_FORMAT_TIME,
                &timestamp)) {
          filter->timestamp = timestamp;
          filter->offset = timestamp * filter->rate / GST_SECOND;
        }
        if (gst_event_discont_get_value (GST_EVENT (data), GST_FORMAT_BYTES,
                &offset)) {
          filter->offset = offset;
          filter->timestamp = offset * GST_SECOND / filter->rate;
        }
        break;
      }
      default:
        break;
    }
    gst_pad_event_default (pad, GST_EVENT (data));
    return;
  }

  in_buf = GST_BUFFER (data);

  out_size = ceil ((gfloat) GST_BUFFER_SIZE (in_buf) / filter->speed);
  out_buf = gst_pad_alloc_buffer (filter->srcpad, -1, out_size);

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
  filter->timestamp = filter->offset * GST_SECOND / filter->rate;

  GST_BUFFER_DURATION (out_buf) =
      filter->timestamp - GST_BUFFER_TIMESTAMP (out_buf);

  gst_pad_push (filter->srcpad, GST_DATA (out_buf));

  gst_buffer_unref (in_buf);
}

static void
speed_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSpeed *filter = (GstSpeed *) object;

  g_return_if_fail (GST_IS_SPEED (object));

  switch (prop_id) {
    case ARG_SPEED:
      filter->speed = g_value_get_float (value);
      break;
    default:
      break;
  }
}

static void
speed_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSpeed *filter = (GstSpeed *) object;

  g_return_if_fail (GST_IS_SPEED (object));

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
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      speed->offset = 0;
      speed->timestamp = 0;
      speed->sample_size = 0;
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "speed", GST_RANK_NONE, GST_TYPE_SPEED);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "speed",
    "Set speed/pitch on audio/raw streams (resampler)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
