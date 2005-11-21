/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstsinesrc.c: 
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gst/controller/gstcontroller.h>

#include "gstsinesrc.h"


GstElementDetails gst_sinesrc_details = {
  "Sine-wave src",
  "Source/Audio",
  "Create a sine wave of a given frequency and volume",
  "Erik Walthinsen <omega@cse.ogi.edu>"
};


enum
{
  PROP_0,
  PROP_SAMPLES_PER_BUFFER,
  PROP_FREQ,
  PROP_VOLUME,
  PROP_IS_LIVE,
  PROP_TIMESTAMP_OFFSET,
};


static GstStaticPadTemplate gst_sinesrc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, " "rate = (int) [ 1, MAX ], " "channels = (int) 1")
    );


GST_BOILERPLATE (GstSineSrc, gst_sinesrc, GstBaseSrc, GST_TYPE_BASE_SRC);


static void gst_sinesrc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_sinesrc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_sinesrc_setcaps (GstBaseSrc * basesrc, GstCaps * caps);
static void gst_sinesrc_src_fixate (GstPad * pad, GstCaps * caps);

static const GstQueryType *gst_sinesrc_get_query_types (GstPad * pad);
static gboolean gst_sinesrc_src_query (GstPad * pad, GstQuery * query);

static void gst_sinesrc_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_sinesrc_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer);
static gboolean gst_sinesrc_start (GstBaseSrc * basesrc);
static gboolean gst_sinesrc_newsegment (GstBaseSrc * basesrc);

static void
gst_sinesrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sinesrc_src_template));
  gst_element_class_set_details (element_class, &gst_sinesrc_details);
}

static void
gst_sinesrc_class_init (GstSineSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = gst_sinesrc_set_property;
  gobject_class->get_property = gst_sinesrc_get_property;

  g_object_class_install_property (gobject_class,
      PROP_SAMPLES_PER_BUFFER,
      g_param_spec_int ("samplesperbuffer", "Samples per buffer",
          "Number of samples in each outgoing buffer",
          1, G_MAXINT, 1024, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FREQ,
      g_param_spec_double ("freq", "Frequency", "Frequency of sine source",
          0.0, 20000.0, 440.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume",
          0.0, 1.0, 0.8, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET,
      g_param_spec_int64 ("timestamp-offset", "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE));

  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_sinesrc_setcaps);
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_sinesrc_start);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_sinesrc_create);
  gstbasesrc_class->get_times = GST_DEBUG_FUNCPTR (gst_sinesrc_get_times);
  gstbasesrc_class->newsegment = GST_DEBUG_FUNCPTR (gst_sinesrc_newsegment);
}

static void
gst_sinesrc_init (GstSineSrc * src, GstSineSrcClass * g_class)
{
  GstPad *pad = GST_BASE_SRC_PAD (src);

  gst_pad_set_fixatecaps_function (pad, gst_sinesrc_src_fixate);
  gst_pad_set_query_function (pad, gst_sinesrc_src_query);
  gst_pad_set_query_type_function (pad, gst_sinesrc_get_query_types);

  src->samplerate = 44100;
  src->volume = 1.0;
  src->freq = 440.0;
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);

  src->samples_per_buffer = 1024;
  src->timestamp = G_GINT64_CONSTANT (0);
  src->offset = G_GINT64_CONSTANT (0);
  src->timestamp_offset = G_GINT64_CONSTANT (0);
}

static void
gst_sinesrc_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "rate", 44100);
}

static gboolean
gst_sinesrc_setcaps (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstSineSrc *sinesrc;
  const GstStructure *structure;
  gboolean ret;

  sinesrc = GST_SINESRC (basesrc);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &sinesrc->samplerate);

  return ret;
}

static const GstQueryType *
gst_sinesrc_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0,
  };

  return query_types;
}

static gboolean
gst_sinesrc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstSineSrc *src;

  src = GST_SINESRC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          current = src->timestamp;
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:       /* samples */
          current = src->offset / 2;    /* 16bpp audio */
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          current = src->offset;
          res = TRUE;
          break;
        default:
          break;
      }
      if (res) {
        gst_query_set_position (query, format, current);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME && GST_BASE_SRC (src)->num_buffers > 0) {
        gst_query_set_duration (query, GST_FORMAT_TIME, GST_SECOND *
            GST_BASE_SRC (src)->num_buffers * src->samples_per_buffer /
            src->samplerate);
        res = TRUE;
      } else {
        gst_query_set_duration (query, format, -1);
      }
      break;
    }
    default:
      break;
  }

  return res;
}

static void
gst_sinesrc_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = timestamp + duration;
      }
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static GstFlowReturn
gst_sinesrc_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstSineSrc *src;
  GstBuffer *buf;
  guint tdiff;
  gdouble step;
  gint16 *samples;
  gint i;

  src = GST_SINESRC (basesrc);

  if (!src->tags_pushed) {
    GstTagList *taglist;
    GstEvent *event;

    taglist = gst_tag_list_new ();

    gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND,
        GST_TAG_DESCRIPTION, "sine wave", NULL);

    event = gst_event_new_tag (taglist);
    gst_pad_push_event (basesrc->srcpad, event);
    src->tags_pushed = TRUE;
  }

  tdiff = src->samples_per_buffer * GST_SECOND / src->samplerate;

  buf = gst_buffer_new_and_alloc (src->samples_per_buffer * sizeof (gint16));
  gst_buffer_set_caps (buf, GST_PAD_CAPS (basesrc->srcpad));

  GST_BUFFER_TIMESTAMP (buf) = src->timestamp + src->timestamp_offset;
  /* offset is the number of samples */
  GST_BUFFER_OFFSET (buf) = src->offset;
  GST_BUFFER_OFFSET_END (buf) = src->offset + src->samples_per_buffer;
  GST_BUFFER_DURATION (buf) = tdiff;

  gst_object_sync_values (G_OBJECT (src), src->timestamp);

  samples = (gint16 *) GST_BUFFER_DATA (buf);

  src->timestamp += tdiff;
  src->offset += src->samples_per_buffer;

  step = 2 * M_PI * src->freq / src->samplerate;

  for (i = 0; i < src->samples_per_buffer; i++) {
    src->accumulator += step;
    if (src->accumulator >= 2 * M_PI)
      src->accumulator -= 2 * M_PI;

    samples[i] = sin (src->accumulator) * src->volume * 32767.0;
  }

  *buffer = buf;

  return GST_FLOW_OK;
}

static void
gst_sinesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSineSrc *src = GST_SINESRC (object);

  switch (prop_id) {
    case PROP_SAMPLES_PER_BUFFER:
      src->samples_per_buffer = g_value_get_int (value);
      break;
    case PROP_FREQ:
      src->freq = g_value_get_double (value);
      break;
    case PROP_VOLUME:
      src->volume = g_value_get_double (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    case PROP_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sinesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSineSrc *src = GST_SINESRC (object);

  switch (prop_id) {
    case PROP_SAMPLES_PER_BUFFER:
      g_value_set_int (value, src->samples_per_buffer);
      break;
    case PROP_FREQ:
      g_value_set_double (value, src->freq);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, src->volume);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_sinesrc_newsegment (GstBaseSrc * basesrc)
{
  GstSineSrc *src = GST_SINESRC (basesrc);
  GstEvent *event;
  gint64 start, end;

  if (basesrc->num_buffers_left > 0) {
    start = src->timestamp;
    end = start + GST_SECOND * basesrc->num_buffers_left *
        src->samples_per_buffer / src->samplerate;
  } else {
    start = src->timestamp;
    end = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG_OBJECT (basesrc, "Sending newsegment from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (end));

  event = gst_event_new_newsegment (FALSE, 1.0, GST_FORMAT_TIME, start, end, 0);

  return gst_pad_push_event (basesrc->srcpad, event);
}

static gboolean
gst_sinesrc_start (GstBaseSrc * basesrc)
{
  GstSineSrc *src = GST_SINESRC (basesrc);

  src->timestamp = G_GINT64_CONSTANT (0);
  src->offset = G_GINT64_CONSTANT (0);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sinesrc",
      GST_RANK_NONE, GST_TYPE_SINESRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sine",
    "Sine audio wave generator",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
