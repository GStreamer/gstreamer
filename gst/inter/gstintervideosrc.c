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
 * SECTION:element-gstintervideosrc
 *
 * The intervideosrc element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! intervideosrc ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/video/video.h>
#include "gstintervideosrc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_inter_video_src_debug_category);
#define GST_CAT_DEFAULT gst_inter_video_src_debug_category

/* prototypes */


static void gst_inter_video_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inter_video_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_inter_video_src_dispose (GObject * object);
static void gst_inter_video_src_finalize (GObject * object);

static GstCaps *gst_inter_video_src_get_caps (GstBaseSrc * src);
static gboolean gst_inter_video_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_video_src_negotiate (GstBaseSrc * src);
static gboolean gst_inter_video_src_newsegment (GstBaseSrc * src);
static gboolean gst_inter_video_src_start (GstBaseSrc * src);
static gboolean gst_inter_video_src_stop (GstBaseSrc * src);
static void
gst_inter_video_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_inter_video_src_is_seekable (GstBaseSrc * src);
static gboolean gst_inter_video_src_unlock (GstBaseSrc * src);
static gboolean gst_inter_video_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn
gst_inter_video_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_inter_video_src_do_seek (GstBaseSrc * src,
    GstSegment * segment);
static gboolean gst_inter_video_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_inter_video_src_check_get_range (GstBaseSrc * src);
static void gst_inter_video_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_video_src_unlock_stop (GstBaseSrc * src);
static gboolean
gst_inter_video_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_inter_video_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_inter_video_src_debug_category, "intervideosrc", 0, \
      "debug category for intervideosrc element");

GST_BOILERPLATE_FULL (GstInterVideoSrc, gst_inter_video_src, GstBaseSrc,
    GST_TYPE_BASE_SRC, DEBUG_INIT);

static void
gst_inter_video_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_inter_video_src_src_template);

  gst_element_class_set_details_simple (element_class, "FIXME Long name",
      "Generic", "FIXME Description", "FIXME <fixme@example.com>");
}

static void
gst_inter_video_src_class_init (GstInterVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_inter_video_src_set_property;
  gobject_class->get_property = gst_inter_video_src_get_property;
  gobject_class->dispose = gst_inter_video_src_dispose;
  gobject_class->finalize = gst_inter_video_src_finalize;
  if (0)
    base_src_class->get_caps = GST_DEBUG_FUNCPTR (gst_inter_video_src_get_caps);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_inter_video_src_set_caps);
  if (0)
    base_src_class->negotiate =
        GST_DEBUG_FUNCPTR (gst_inter_video_src_negotiate);
  if (0)
    base_src_class->newsegment =
        GST_DEBUG_FUNCPTR (gst_inter_video_src_newsegment);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_inter_video_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_inter_video_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_inter_video_src_get_times);
  if (0)
    base_src_class->is_seekable =
        GST_DEBUG_FUNCPTR (gst_inter_video_src_is_seekable);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_inter_video_src_unlock);
  base_src_class->event = GST_DEBUG_FUNCPTR (gst_inter_video_src_event);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_inter_video_src_create);
  if (0)
    base_src_class->do_seek = GST_DEBUG_FUNCPTR (gst_inter_video_src_do_seek);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_inter_video_src_query);
  if (0)
    base_src_class->check_get_range =
        GST_DEBUG_FUNCPTR (gst_inter_video_src_check_get_range);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_inter_video_src_fixate);
  if (0)
    base_src_class->unlock_stop =
        GST_DEBUG_FUNCPTR (gst_inter_video_src_unlock_stop);
  if (0)
    base_src_class->prepare_seek_segment =
        GST_DEBUG_FUNCPTR (gst_inter_video_src_prepare_seek_segment);


}

static void
gst_inter_video_src_init (GstInterVideoSrc * intervideosrc,
    GstInterVideoSrcClass * intervideosrc_class)
{
  gst_base_src_set_format (GST_BASE_SRC (intervideosrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (intervideosrc), TRUE);

  intervideosrc->surface = gst_inter_surface_get ("default");
}

void
gst_inter_video_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_video_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_video_src_dispose (GObject * object)
{
  /* GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_inter_video_src_finalize (GObject * object)
{
  /* GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_inter_video_src_get_caps (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "get_caps");

  return NULL;
}

static gboolean
gst_inter_video_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);
  gboolean ret;
  GstVideoFormat format;
  int width, height;
  int fps_n, fps_d;

  GST_DEBUG_OBJECT (intervideosrc, "set_caps");

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  ret &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);

  if (ret) {
    intervideosrc->format = format;
    intervideosrc->width = width;
    intervideosrc->height = height;
    intervideosrc->fps_n = fps_n;
    intervideosrc->fps_d = fps_d;
    GST_DEBUG ("fps %d/%d", fps_n, fps_d);
  }

  return ret;
}

static gboolean
gst_inter_video_src_negotiate (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "negotiate");

  return TRUE;
}

static gboolean
gst_inter_video_src_newsegment (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "newsegment");

  return TRUE;
}

static gboolean
gst_inter_video_src_start (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "start");

  return TRUE;
}

static gboolean
gst_inter_video_src_stop (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "stop");

  return TRUE;
}

static void
gst_inter_video_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "get_times");

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
gst_inter_video_src_is_seekable (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "is_seekable");

  return FALSE;
}

static gboolean
gst_inter_video_src_unlock (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "unlock");

  return TRUE;
}

static gboolean
gst_inter_video_src_event (GstBaseSrc * src, GstEvent * event)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "event");

  return TRUE;
}

static GstFlowReturn
gst_inter_video_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);
  GstBuffer *buffer;
  guint8 *data;

  GST_DEBUG_OBJECT (intervideosrc, "create");

  buffer = NULL;

  g_mutex_lock (intervideosrc->surface->mutex);
  if (intervideosrc->surface->video_buffer) {
    buffer = gst_buffer_ref (intervideosrc->surface->video_buffer);
    intervideosrc->surface->video_buffer_count++;
    if (intervideosrc->surface->video_buffer_count >= 30) {
      gst_buffer_unref (intervideosrc->surface->video_buffer);
      intervideosrc->surface->video_buffer = NULL;
    }
  }
  g_mutex_unlock (intervideosrc->surface->mutex);

  if (buffer == NULL) {
    buffer =
        gst_buffer_new_and_alloc (gst_video_format_get_size
        (intervideosrc->format, intervideosrc->width, intervideosrc->height));

    data = GST_BUFFER_DATA (buffer);
    memset (data, 16,
        gst_video_format_get_row_stride (intervideosrc->format, 0,
            intervideosrc->width) *
        gst_video_format_get_component_height (intervideosrc->format, 0,
            intervideosrc->height));

    memset (data + gst_video_format_get_component_offset (intervideosrc->format,
            1, intervideosrc->width, intervideosrc->height),
        128,
        2 * gst_video_format_get_row_stride (intervideosrc->format, 1,
            intervideosrc->width) *
        gst_video_format_get_component_height (intervideosrc->format, 1,
            intervideosrc->height));

#if 0
    {
      int i;
      for (i = 0; i < 10000; i++) {
        data[i] = g_random_int () & 0xff;
      }
    }
#endif
  }

  buffer = gst_buffer_make_metadata_writable (buffer);

  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (GST_SECOND * intervideosrc->n_frames,
      intervideosrc->fps_d, intervideosrc->fps_n);
  GST_DEBUG_OBJECT (intervideosrc, "create ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (GST_SECOND * (intervideosrc->n_frames + 1),
      intervideosrc->fps_d,
      intervideosrc->fps_n) - GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_OFFSET (buffer) = intervideosrc->n_frames;
  GST_BUFFER_OFFSET_END (buffer) = -1;
  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  if (intervideosrc->n_frames == 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  }
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (GST_BASE_SRC_PAD (intervideosrc)));
  intervideosrc->n_frames++;

  *buf = buffer;

  return GST_FLOW_OK;
}

static gboolean
gst_inter_video_src_do_seek (GstBaseSrc * src, GstSegment * segment)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "do_seek");

  return FALSE;
}

static gboolean
gst_inter_video_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "query");

  return TRUE;
}

static gboolean
gst_inter_video_src_check_get_range (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "get_range");

  return FALSE;
}

static void
gst_inter_video_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);
  GstStructure *structure;

  GST_DEBUG_OBJECT (intervideosrc, "fixate");

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
  if (gst_structure_has_field (structure, "pixel-aspect-ratio"))
    gst_structure_fixate_field_nearest_fraction (structure,
        "pixel-aspect-ratio", 1, 1);
  if (gst_structure_has_field (structure, "color-matrix"))
    gst_structure_fixate_field_string (structure, "color-matrix", "sdtv");
  if (gst_structure_has_field (structure, "chroma-site"))
    gst_structure_fixate_field_string (structure, "chroma-site", "mpeg2");

  if (gst_structure_has_field (structure, "interlaced"))
    gst_structure_fixate_field_boolean (structure, "interlaced", FALSE);

}

static gboolean
gst_inter_video_src_unlock_stop (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "stop");

  return TRUE;
}

static gboolean
gst_inter_video_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "seek_segment");

  return FALSE;
}
