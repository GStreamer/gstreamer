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
static gboolean gst_sinesrc_unlock (GstBaseSrc * bsrc);

static gboolean gst_sinesrc_setcaps (GstBaseSrc * basesrc, GstCaps * caps);
static void gst_sinesrc_src_fixate (GstPad * pad, GstCaps * caps);

static const GstQueryType *gst_sinesrc_get_query_types (GstPad * pad);
static gboolean gst_sinesrc_src_query (GstPad * pad, GstQuery * query);

static GstFlowReturn gst_sinesrc_create (GstBaseSrc * basesrc, guint64 offset,
    guint length, GstBuffer ** buffer);
static gboolean gst_sinesrc_start (GstBaseSrc * basesrc);


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
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_sinesrc_unlock);
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

  gst_caps_structure_fixate_field_nearest_int (structure, "rate", 44100);
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

      gst_query_parse_position (query, &format, NULL, NULL);

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
        gst_query_set_position (query, format, current, -1);
      }
      break;
    }
    default:
      break;
  }

  return res;
}

/* with STREAM_LOCK */
static GstClockReturn
gst_sinesrc_wait (GstSineSrc * src, GstClockTime time)
{
  GstClockReturn ret;
  GstClockTime base_time;

  GST_LOCK (src);
  /* clock_id should be NULL outside of this function */
  g_assert (src->clock_id == NULL);
  g_assert (GST_CLOCK_TIME_IS_VALID (time));
  base_time = GST_ELEMENT (src)->base_time;
  src->clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (src),
      time + base_time);
  GST_UNLOCK (src);

  ret = gst_clock_id_wait (src->clock_id, NULL);

  GST_LOCK (src);
  gst_clock_id_unref (src->clock_id);
  src->clock_id = NULL;
  GST_UNLOCK (src);

  return ret;
}

static gboolean
gst_sinesrc_unlock (GstBaseSrc * bsrc)
{
  GstSineSrc *src = GST_SINESRC (bsrc);

  GST_LOCK (src);
  if (src->clock_id)
    gst_clock_id_unschedule (src->clock_id);
  GST_UNLOCK (src);

  return TRUE;
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

  if (gst_base_src_is_live (basesrc)) {
    GstClockReturn ret;

    ret = gst_sinesrc_wait (src, src->timestamp + src->timestamp_offset);
    if (ret == GST_CLOCK_UNSCHEDULED)
      goto unscheduled;
  }

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

unscheduled:
  {
    GST_DEBUG_OBJECT (src, "Unscheduled while waiting for clock");
    return GST_FLOW_WRONG_STATE;        /* is this the right return? */
  }
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
