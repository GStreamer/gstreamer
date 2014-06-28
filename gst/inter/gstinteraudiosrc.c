/* GStreamer
 * Copyright (C) 2011 David A. Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstinteraudiosrc
 *
 * The interaudiosrc element is an audio source element.  It is used
 * in connection with a interaudiosink element in a different pipeline.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v interaudiosrc ! queue ! audiosink
 * ]|
 * 
 * The interaudiosrc element cannot be used effectively with gst-launch,
 * as it requires a second pipeline in the application to send audio.
 * See the gstintertest.c example in the gst-plugins-bad source code for
 * more details.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstinteraudiosrc.h"

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/audio/audio.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_inter_audio_src_debug_category);
#define GST_CAT_DEFAULT gst_inter_audio_src_debug_category

/* prototypes */


static void gst_inter_audio_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inter_audio_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_inter_audio_src_finalize (GObject * object);

static gboolean gst_inter_audio_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_audio_src_start (GstBaseSrc * src);
static gboolean gst_inter_audio_src_stop (GstBaseSrc * src);
static void
gst_inter_audio_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn
gst_inter_audio_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_inter_audio_src_query (GstBaseSrc * src, GstQuery * query);
static GstCaps *gst_inter_audio_src_fixate (GstBaseSrc * src, GstCaps * caps);

enum
{
  PROP_0,
  PROP_CHANNEL
};

/* pad templates */

static GstStaticPadTemplate gst_inter_audio_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "
        "rate = (int) 48000, channels = (int) 2")
    );


/* class initialization */

G_DEFINE_TYPE (GstInterAudioSrc, gst_inter_audio_src, GST_TYPE_BASE_SRC);

static void
gst_inter_audio_src_class_init (GstInterAudioSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_inter_audio_src_debug_category, "interaudiosrc",
      0, "debug category for interaudiosrc element");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_inter_audio_src_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Internal audio source",
      "Source/Audio",
      "Virtual audio source for internal process communication",
      "David Schleef <ds@schleef.org>");

  gobject_class->set_property = gst_inter_audio_src_set_property;
  gobject_class->get_property = gst_inter_audio_src_get_property;
  gobject_class->finalize = gst_inter_audio_src_finalize;
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_inter_audio_src_set_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_inter_audio_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_inter_audio_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_inter_audio_src_get_times);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_inter_audio_src_create);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_inter_audio_src_query);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_inter_audio_src_fixate);

  g_object_class_install_property (gobject_class, PROP_CHANNEL,
      g_param_spec_string ("channel", "Channel",
          "Channel name to match inter src and sink elements",
          "default", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_inter_audio_src_init (GstInterAudioSrc * interaudiosrc)
{
  gst_base_src_set_format (GST_BASE_SRC (interaudiosrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (interaudiosrc), TRUE);
  gst_base_src_set_blocksize (GST_BASE_SRC (interaudiosrc), -1);

  interaudiosrc->channel = g_strdup ("default");
}

void
gst_inter_audio_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_free (interaudiosrc->channel);
      interaudiosrc->channel = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_value_set_string (value, interaudiosrc->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_src_finalize (GObject * object)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object);

  /* clean up object here */
  g_free (interaudiosrc->channel);

  G_OBJECT_CLASS (gst_inter_audio_src_parent_class)->finalize (object);
}

static gboolean
gst_inter_audio_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);
  const GstStructure *structure;
  GstAudioInfo info;
  gboolean ret;
  int sample_rate;

  GST_DEBUG_OBJECT (interaudiosrc, "set_caps");

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &sample_rate)) {
    GST_ERROR_OBJECT (src, "Audio caps without rate");
    return FALSE;
  }

  interaudiosrc->sample_rate = sample_rate;

  if (!gst_audio_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (src, "Can't parse audio caps");
    return FALSE;
  }

  interaudiosrc->finfo = info.finfo;

  ret = gst_pad_set_caps (src->srcpad, caps);

  return ret;
}


static gboolean
gst_inter_audio_src_start (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "start");

  interaudiosrc->surface = gst_inter_surface_get (interaudiosrc->channel);

  return TRUE;
}

static gboolean
gst_inter_audio_src_stop (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "stop");

  gst_inter_surface_unref (interaudiosrc->surface);
  interaudiosrc->surface = NULL;
  interaudiosrc->finfo = NULL;

  return TRUE;
}

static void
gst_inter_audio_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GST_DEBUG_OBJECT (src, "get_times");

  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (src)) {
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


#define SIZE 1600

static GstFlowReturn
gst_inter_audio_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);
  GstBuffer *buffer;
  int n;

  GST_DEBUG_OBJECT (interaudiosrc, "create");

  buffer = NULL;

  g_mutex_lock (&interaudiosrc->surface->mutex);
  n = gst_adapter_available (interaudiosrc->surface->audio_adapter) / 4;
  if (n > SIZE * 3) {
    GST_WARNING ("flushing %d samples", SIZE / 2);
    gst_adapter_flush (interaudiosrc->surface->audio_adapter, (SIZE / 2) * 4);
    n -= (SIZE / 2);
  }
  if (n > SIZE)
    n = SIZE;
  if (n > 0) {
    buffer = gst_adapter_take_buffer (interaudiosrc->surface->audio_adapter,
        n * 4);
  } else {
    buffer = gst_buffer_new ();
  }
  g_mutex_unlock (&interaudiosrc->surface->mutex);

  if (n < SIZE) {
    GstMapInfo map;
    GstMemory *mem;

    GST_WARNING ("creating %d samples of silence", SIZE - n);
    mem = gst_allocator_alloc (NULL, (SIZE - n) * 4, NULL);
    if (gst_memory_map (mem, &map, GST_MAP_WRITE)) {
      gst_audio_format_fill_silence (interaudiosrc->finfo, map.data, map.size);
      gst_memory_unmap (mem, &map);
    }
    buffer = gst_buffer_make_writable (buffer);
    gst_buffer_prepend_memory (buffer, mem);
  }
  n = SIZE;

  GST_BUFFER_OFFSET (buffer) = interaudiosrc->n_samples;
  GST_BUFFER_OFFSET_END (buffer) = interaudiosrc->n_samples + n;
  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (interaudiosrc->n_samples, GST_SECOND,
      interaudiosrc->sample_rate);
  GST_DEBUG_OBJECT (interaudiosrc, "create ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (interaudiosrc->n_samples + n, GST_SECOND,
      interaudiosrc->sample_rate) - GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_OFFSET (buffer) = interaudiosrc->n_samples;
  GST_BUFFER_OFFSET_END (buffer) = -1;
  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  if (interaudiosrc->n_samples == 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  }
  interaudiosrc->n_samples += n;

  *buf = buffer;

  return GST_FLOW_OK;
}


static gboolean
gst_inter_audio_src_query (GstBaseSrc * src, GstQuery * query)
{
  gboolean ret;

  GST_DEBUG_OBJECT (src, "query");

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;

      min_latency = 30 * gst_util_uint64_scale_int (GST_SECOND, SIZE, 48000);

      max_latency = min_latency;

      GST_ERROR_OBJECT (src,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      gst_query_set_latency (query,
          gst_base_src_is_live (src), min_latency, max_latency);

      ret = TRUE;
      break;
    }
    default:
      ret = GST_BASE_SRC_CLASS (gst_inter_audio_src_parent_class)->query (src,
          query);
      break;
  }

  return ret;
}

static GstCaps *
gst_inter_audio_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstStructure *structure;

  GST_DEBUG_OBJECT (src, "fixate");

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "channels", 2);
  gst_structure_fixate_field_nearest_int (structure, "rate", 48000);

  return caps;
}
