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
 * The intervideosrc element is a video source element.  It is used
 * in connection with a intervideosink element in a different pipeline,
 * similar to interaudiosink and interaudiosrc.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v intervideosrc ! queue ! xvimagesink
 * ]|
 * 
 * The intersubsrc element cannot be used effectively with gst-launch,
 * as it requires a second pipeline in the application to send subtitles.
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
static void gst_inter_video_src_finalize (GObject * object);

static gboolean gst_inter_video_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_video_src_start (GstBaseSrc * src);
static gboolean gst_inter_video_src_stop (GstBaseSrc * src);
static void
gst_inter_video_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static GstFlowReturn
gst_inter_video_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static GstCaps *gst_inter_video_src_fixate (GstBaseSrc * src, GstCaps * caps);

enum
{
  PROP_0,
  PROP_CHANNEL
};

/* pad templates */

static GstStaticPadTemplate gst_inter_video_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );


/* class initialization */

G_DEFINE_TYPE (GstInterVideoSrc, gst_inter_video_src, GST_TYPE_BASE_SRC);

static void
gst_inter_video_src_class_init (GstInterVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_inter_video_src_debug_category, "intervideosrc",
      0, "debug category for intervideosrc element");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_inter_video_src_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Internal video source",
      "Source/Video",
      "Virtual video source for internal process communication",
      "David Schleef <ds@schleef.org>");

  gobject_class->set_property = gst_inter_video_src_set_property;
  gobject_class->get_property = gst_inter_video_src_get_property;
  gobject_class->finalize = gst_inter_video_src_finalize;
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_inter_video_src_set_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_inter_video_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_inter_video_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_inter_video_src_get_times);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_inter_video_src_create);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_inter_video_src_fixate);

  g_object_class_install_property (gobject_class, PROP_CHANNEL,
      g_param_spec_string ("channel", "Channel",
          "Channel name to match inter src and sink elements",
          "default", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_inter_video_src_init (GstInterVideoSrc * intervideosrc)
{
  gst_base_src_set_format (GST_BASE_SRC (intervideosrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (intervideosrc), TRUE);

  intervideosrc->channel = g_strdup ("default");
}

void
gst_inter_video_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_free (intervideosrc->channel);
      intervideosrc->channel = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_video_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_value_set_string (value, intervideosrc->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_video_src_finalize (GObject * object)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (object);

  /* clean up object here */
  g_free (intervideosrc->channel);

  G_OBJECT_CLASS (gst_inter_video_src_parent_class)->finalize (object);
}



static gboolean
gst_inter_video_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "set_caps");

  if (!gst_video_info_from_caps (&intervideosrc->info, caps))
    return FALSE;

  return gst_pad_set_caps (src->srcpad, caps);
}


static gboolean
gst_inter_video_src_start (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "start");

  intervideosrc->surface = gst_inter_surface_get (intervideosrc->channel);

  return TRUE;
}

static gboolean
gst_inter_video_src_stop (GstBaseSrc * src)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);

  GST_DEBUG_OBJECT (intervideosrc, "stop");

  gst_inter_surface_unref (intervideosrc->surface);
  intervideosrc->surface = NULL;

  return TRUE;
}

static void
gst_inter_video_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GST_DEBUG_OBJECT (src, "get_times");

  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (src)) {
    GstClockTime timestamp = GST_BUFFER_PTS (buffer);

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
gst_inter_video_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstInterVideoSrc *intervideosrc = GST_INTER_VIDEO_SRC (src);
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (intervideosrc, "create");

  buffer = NULL;

  g_mutex_lock (&intervideosrc->surface->mutex);
  if (intervideosrc->surface->video_buffer) {
    buffer = gst_buffer_ref (intervideosrc->surface->video_buffer);
    intervideosrc->surface->video_buffer_count++;
    if (intervideosrc->surface->video_buffer_count >= 30) {
      gst_buffer_unref (intervideosrc->surface->video_buffer);
      intervideosrc->surface->video_buffer = NULL;
    }
  }
  g_mutex_unlock (&intervideosrc->surface->mutex);

  if (buffer == NULL) {
    GstMapInfo map;

    buffer =
        gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&intervideosrc->info));

    gst_buffer_map (buffer, &map, GST_MAP_WRITE);
    memset (map.data, 16, GST_VIDEO_INFO_COMP_STRIDE (&intervideosrc->info, 0) *
        GST_VIDEO_INFO_COMP_HEIGHT (&intervideosrc->info, 0));

    memset (map.data + GST_VIDEO_INFO_COMP_OFFSET (&intervideosrc->info, 1),
        128,
        2 * GST_VIDEO_INFO_COMP_STRIDE (&intervideosrc->info, 1) *
        GST_VIDEO_INFO_COMP_HEIGHT (&intervideosrc->info, 1));
    gst_buffer_unmap (buffer, &map);
  }

  buffer = gst_buffer_make_writable (buffer);

  GST_BUFFER_PTS (buffer) =
      gst_util_uint64_scale_int (GST_SECOND * intervideosrc->n_frames,
      GST_VIDEO_INFO_FPS_D (&intervideosrc->info),
      GST_VIDEO_INFO_FPS_N (&intervideosrc->info));
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_DEBUG_OBJECT (intervideosrc, "create ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (GST_SECOND * (intervideosrc->n_frames + 1),
      GST_VIDEO_INFO_FPS_D (&intervideosrc->info),
      GST_VIDEO_INFO_FPS_N (&intervideosrc->info)) - GST_BUFFER_PTS (buffer);
  GST_BUFFER_OFFSET (buffer) = intervideosrc->n_frames;
  GST_BUFFER_OFFSET_END (buffer) = -1;
  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  if (intervideosrc->n_frames == 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  }
  intervideosrc->n_frames++;

  *buf = buffer;

  return GST_FLOW_OK;
}

static GstCaps *
gst_inter_video_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstStructure *structure;

  GST_DEBUG_OBJECT (src, "fixate");

  caps = gst_caps_make_writable (caps);

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

  return caps;
}
