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
 * The interaudiosrc element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! interaudiosrc ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include "gstinteraudiosrc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_inter_audio_src_debug_category);
#define GST_CAT_DEFAULT gst_inter_audio_src_debug_category

/* prototypes */


static void gst_inter_audio_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inter_audio_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_inter_audio_src_dispose (GObject * object);
static void gst_inter_audio_src_finalize (GObject * object);

static GstCaps *gst_inter_audio_src_get_caps (GstBaseSrc * src);
static gboolean gst_inter_audio_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_audio_src_negotiate (GstBaseSrc * src);
static gboolean gst_inter_audio_src_newsegment (GstBaseSrc * src);
static gboolean gst_inter_audio_src_start (GstBaseSrc * src);
static gboolean gst_inter_audio_src_stop (GstBaseSrc * src);
static void
gst_inter_audio_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_inter_audio_src_is_seekable (GstBaseSrc * src);
static gboolean gst_inter_audio_src_unlock (GstBaseSrc * src);
static gboolean gst_inter_audio_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn
gst_inter_audio_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_inter_audio_src_do_seek (GstBaseSrc * src,
    GstSegment * segment);
static gboolean gst_inter_audio_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_inter_audio_src_check_get_range (GstBaseSrc * src);
static void gst_inter_audio_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_audio_src_unlock_stop (GstBaseSrc * src);
static gboolean
gst_inter_audio_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_inter_audio_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_inter_audio_src_debug_category, "interaudiosrc", 0, \
      "debug category for interaudiosrc element");

GST_BOILERPLATE_FULL (GstInterAudioSrc, gst_inter_audio_src, GstBaseSrc,
    GST_TYPE_BASE_SRC, DEBUG_INIT);

static void
gst_inter_audio_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_inter_audio_src_src_template);

  gst_element_class_set_details_simple (element_class, "FIXME Long name",
      "Generic", "FIXME Description", "FIXME <fixme@example.com>");
}

static void
gst_inter_audio_src_class_init (GstInterAudioSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_inter_audio_src_set_property;
  gobject_class->get_property = gst_inter_audio_src_get_property;
  gobject_class->dispose = gst_inter_audio_src_dispose;
  gobject_class->finalize = gst_inter_audio_src_finalize;
  base_src_class->get_caps = GST_DEBUG_FUNCPTR (gst_inter_audio_src_get_caps);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_inter_audio_src_set_caps);
  if (0)
    base_src_class->negotiate =
        GST_DEBUG_FUNCPTR (gst_inter_audio_src_negotiate);
  base_src_class->newsegment =
      GST_DEBUG_FUNCPTR (gst_inter_audio_src_newsegment);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_inter_audio_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_inter_audio_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_inter_audio_src_get_times);
  if (0)
    base_src_class->is_seekable =
        GST_DEBUG_FUNCPTR (gst_inter_audio_src_is_seekable);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_inter_audio_src_unlock);
  base_src_class->event = GST_DEBUG_FUNCPTR (gst_inter_audio_src_event);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_inter_audio_src_create);
  if (0)
    base_src_class->do_seek = GST_DEBUG_FUNCPTR (gst_inter_audio_src_do_seek);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_inter_audio_src_query);
  if (0)
    base_src_class->check_get_range =
        GST_DEBUG_FUNCPTR (gst_inter_audio_src_check_get_range);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_inter_audio_src_fixate);
  if (0)
    base_src_class->unlock_stop =
        GST_DEBUG_FUNCPTR (gst_inter_audio_src_unlock_stop);
  if (0)
    base_src_class->prepare_seek_segment =
        GST_DEBUG_FUNCPTR (gst_inter_audio_src_prepare_seek_segment);


}

static void
gst_inter_audio_src_init (GstInterAudioSrc * interaudiosrc,
    GstInterAudioSrcClass * interaudiosrc_class)
{

  gst_base_src_set_live (GST_BASE_SRC (interaudiosrc), TRUE);
  gst_base_src_set_blocksize (GST_BASE_SRC (interaudiosrc), -1);

  interaudiosrc->surface = gst_inter_surface_get ("default");
}

void
gst_inter_audio_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_audio_src_dispose (GObject * object)
{
  /* GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_inter_audio_src_finalize (GObject * object)
{
  /* GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_inter_audio_src_get_caps (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "get_caps");

  return NULL;
}

static gboolean
gst_inter_audio_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);
  const GstStructure *structure;
  gboolean ret;
  int sample_rate;

  GST_DEBUG_OBJECT (interaudiosrc, "set_caps");

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "rate", &sample_rate);
  if (ret) {
    interaudiosrc->sample_rate = sample_rate;
  }

  return ret;
}

static gboolean
gst_inter_audio_src_negotiate (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "negotiate");

  return TRUE;
}

static gboolean
gst_inter_audio_src_newsegment (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "newsegment");

  return TRUE;
}

static gboolean
gst_inter_audio_src_start (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "start");

  return TRUE;
}

static gboolean
gst_inter_audio_src_stop (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "stop");

  return TRUE;
}

static void
gst_inter_audio_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "get_times");

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

static gboolean
gst_inter_audio_src_is_seekable (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "is_seekable");

  return FALSE;
}

static gboolean
gst_inter_audio_src_unlock (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "unlock");

  return TRUE;
}

static gboolean
gst_inter_audio_src_event (GstBaseSrc * src, GstEvent * event)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "event");

  return TRUE;
}

static GstFlowReturn
gst_inter_audio_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);
  GstBuffer *buffer;
  int n;

  GST_DEBUG_OBJECT (interaudiosrc, "create");

  buffer = NULL;

  g_mutex_lock (interaudiosrc->surface->mutex);
  n = gst_adapter_available (interaudiosrc->surface->audio_adapter) / 4;
  if (n > 1600 * 2) {
    GST_DEBUG ("flushing %d samples", 800);
    gst_adapter_flush (interaudiosrc->surface->audio_adapter, 800 * 4);
    n -= 800;
  }
  if (n > 1600)
    n = 1600;
  if (n > 0) {
    buffer = gst_adapter_take_buffer (interaudiosrc->surface->audio_adapter,
        n * 4);
  }
  g_mutex_unlock (interaudiosrc->surface->mutex);

  if (n < 1600) {
    GstBuffer *newbuf = gst_buffer_new_and_alloc (1600 * 4);

    GST_DEBUG ("creating %d samples of silence", 1600 - n);
    memset (GST_BUFFER_DATA (newbuf) + n * 4, 0, 1600 * 4 - n * 4);
    if (buffer) {
      memcpy (GST_BUFFER_DATA (newbuf), GST_BUFFER_DATA (buffer), n * 4);
      gst_buffer_unref (buffer);
    }
    buffer = newbuf;
  }
  n = 1600;

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
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (GST_BASE_SRC_PAD (interaudiosrc)));
  interaudiosrc->n_samples += n;

  *buf = buffer;

  return GST_FLOW_OK;
}

static gboolean
gst_inter_audio_src_do_seek (GstBaseSrc * src, GstSegment * segment)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "do_seek");

  return FALSE;
}

static gboolean
gst_inter_audio_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "query");

  return TRUE;
}

static gboolean
gst_inter_audio_src_check_get_range (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "get_range");

  return FALSE;
}

static void
gst_inter_audio_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (interaudiosrc, "fixate");

  gst_structure_fixate_field_nearest_int (structure, "channels", 2);
  gst_structure_fixate_field_nearest_int (structure, "rate", 48000);

}

static gboolean
gst_inter_audio_src_unlock_stop (GstBaseSrc * src)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "stop");

  return TRUE;
}

static gboolean
gst_inter_audio_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment)
{
  GstInterAudioSrc *interaudiosrc = GST_INTER_AUDIO_SRC (src);

  GST_DEBUG_OBJECT (interaudiosrc, "seek_segment");

  return FALSE;
}
