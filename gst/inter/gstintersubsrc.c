/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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
 * SECTION:element-gstintersubsrc
 *
 * The intersubsrc element is a subtitle source element.  It is used
 * in connection with a intersubsink element in a different pipeline,
 * similar to interaudiosink and interaudiosrc.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v intersubsrc ! kateenc ! oggmux ! filesink location=out.ogv
 * ]|
 * 
 * The intersubsrc element cannot be used effectively with gst-launch,
 * as it requires a second pipeline in the application to send subtitles.
 * See the gstintertest.c example in the gst-plugins-bad source code for
 * more details.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include "gstintersubsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_inter_sub_src_debug_category);
#define GST_CAT_DEFAULT gst_inter_sub_src_debug_category

/* prototypes */


static void gst_inter_sub_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inter_sub_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_inter_sub_src_dispose (GObject * object);
static void gst_inter_sub_src_finalize (GObject * object);

static GstCaps *gst_inter_sub_src_get_caps (GstBaseSrc * src);
static gboolean gst_inter_sub_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_sub_src_negotiate (GstBaseSrc * src);
static gboolean gst_inter_sub_src_newsegment (GstBaseSrc * src);
static gboolean gst_inter_sub_src_start (GstBaseSrc * src);
static gboolean gst_inter_sub_src_stop (GstBaseSrc * src);
static void
gst_inter_sub_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_inter_sub_src_is_seekable (GstBaseSrc * src);
static gboolean gst_inter_sub_src_unlock (GstBaseSrc * src);
static gboolean gst_inter_sub_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn
gst_inter_sub_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_inter_sub_src_do_seek (GstBaseSrc * src,
    GstSegment * segment);
static gboolean gst_inter_sub_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_inter_sub_src_check_get_range (GstBaseSrc * src);
static void gst_inter_sub_src_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_inter_sub_src_unlock_stop (GstBaseSrc * src);
static gboolean
gst_inter_sub_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment);

enum
{
  PROP_0,
  PROP_CHANNEL
};

/* pad templates */

static GstStaticPadTemplate gst_inter_sub_src_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/unknown")
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_inter_sub_src_debug_category, "intersubsrc", 0, \
      "debug category for intersubsrc element");

GST_BOILERPLATE_FULL (GstInterSubSrc, gst_inter_sub_src, GstBaseSrc,
    GST_TYPE_BASE_SRC, DEBUG_INIT);

static void
gst_inter_sub_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_inter_sub_src_src_template));

  gst_element_class_set_details_simple (element_class,
      "Internal subtitle source",
      "Source/Subtitle",
      "Virtual subtitle source for internal process communication",
      "David Schleef <ds@schleef.org>");
}

static void
gst_inter_sub_src_class_init (GstInterSubSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_inter_sub_src_set_property;
  gobject_class->get_property = gst_inter_sub_src_get_property;
  gobject_class->dispose = gst_inter_sub_src_dispose;
  gobject_class->finalize = gst_inter_sub_src_finalize;
  if (0)
    base_src_class->get_caps = GST_DEBUG_FUNCPTR (gst_inter_sub_src_get_caps);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_inter_sub_src_set_caps);
  if (0)
    base_src_class->negotiate = GST_DEBUG_FUNCPTR (gst_inter_sub_src_negotiate);
  if (0)
    base_src_class->newsegment =
        GST_DEBUG_FUNCPTR (gst_inter_sub_src_newsegment);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_inter_sub_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_inter_sub_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_inter_sub_src_get_times);
  if (0)
    base_src_class->is_seekable =
        GST_DEBUG_FUNCPTR (gst_inter_sub_src_is_seekable);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_inter_sub_src_unlock);
  base_src_class->event = GST_DEBUG_FUNCPTR (gst_inter_sub_src_event);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_inter_sub_src_create);
  if (0)
    base_src_class->do_seek = GST_DEBUG_FUNCPTR (gst_inter_sub_src_do_seek);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_inter_sub_src_query);
  if (0)
    base_src_class->check_get_range =
        GST_DEBUG_FUNCPTR (gst_inter_sub_src_check_get_range);
  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_inter_sub_src_fixate);
  if (0)
    base_src_class->unlock_stop =
        GST_DEBUG_FUNCPTR (gst_inter_sub_src_unlock_stop);
  if (0)
    base_src_class->prepare_seek_segment =
        GST_DEBUG_FUNCPTR (gst_inter_sub_src_prepare_seek_segment);

  g_object_class_install_property (gobject_class, PROP_CHANNEL,
      g_param_spec_string ("channel", "Channel",
          "Channel name to match inter src and sink elements",
          "default", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_inter_sub_src_init (GstInterSubSrc * intersubsrc,
    GstInterSubSrcClass * intersubsrc_class)
{

  intersubsrc->srcpad =
      gst_pad_new_from_static_template (&gst_inter_sub_src_src_template, "src");

  gst_base_src_set_format (GST_BASE_SRC (intersubsrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (intersubsrc), TRUE);

  intersubsrc->channel = g_strdup ("default");
}

void
gst_inter_sub_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_free (intersubsrc->channel);
      intersubsrc->channel = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_sub_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (object);

  switch (property_id) {
    case PROP_CHANNEL:
      g_value_set_string (value, intersubsrc->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_inter_sub_src_dispose (GObject * object)
{
  /* GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_inter_sub_src_finalize (GObject * object)
{
  /* GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_inter_sub_src_get_caps (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "get_caps");

  return NULL;
}

static gboolean
gst_inter_sub_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "set_caps");

  return TRUE;
}

static gboolean
gst_inter_sub_src_negotiate (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "negotiate");

  return TRUE;
}

static gboolean
gst_inter_sub_src_newsegment (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "newsegment");

  return TRUE;
}

static gboolean
gst_inter_sub_src_start (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "start");

  intersubsrc->surface = gst_inter_surface_get (intersubsrc->channel);

  return TRUE;
}

static gboolean
gst_inter_sub_src_stop (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "stop");

  gst_inter_surface_unref (intersubsrc->surface);
  intersubsrc->surface = NULL;

  return TRUE;
}

static void
gst_inter_sub_src_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "get_times");

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
gst_inter_sub_src_is_seekable (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "is_seekable");

  return FALSE;
}

static gboolean
gst_inter_sub_src_unlock (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "unlock");

  return TRUE;
}

static gboolean
gst_inter_sub_src_event (GstBaseSrc * src, GstEvent * event)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);
  gboolean ret;

  GST_DEBUG_OBJECT (intersubsrc, "event");

  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->event (src, event);
  }

  return ret;
}

static GstFlowReturn
gst_inter_sub_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (intersubsrc, "create");

  buffer = NULL;

  g_mutex_lock (intersubsrc->surface->mutex);
  if (intersubsrc->surface->sub_buffer) {
    buffer = gst_buffer_ref (intersubsrc->surface->sub_buffer);
    //intersubsrc->surface->sub_buffer_count++;
    //if (intersubsrc->surface->sub_buffer_count >= 30) {
    gst_buffer_unref (intersubsrc->surface->sub_buffer);
    intersubsrc->surface->sub_buffer = NULL;
    //}
  }
  g_mutex_unlock (intersubsrc->surface->mutex);

  if (buffer == NULL) {
    guint8 *data;

    buffer = gst_buffer_new_and_alloc (1);

    data = GST_BUFFER_DATA (buffer);
    data[0] = 0;
  }

  buffer = gst_buffer_make_metadata_writable (buffer);

  GST_BUFFER_TIMESTAMP (buffer) =
      gst_util_uint64_scale_int (GST_SECOND, intersubsrc->n_frames,
      intersubsrc->rate);
  GST_DEBUG_OBJECT (intersubsrc, "create ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (GST_SECOND, (intersubsrc->n_frames + 1),
      intersubsrc->rate) - GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_OFFSET (buffer) = intersubsrc->n_frames;
  GST_BUFFER_OFFSET_END (buffer) = -1;
  GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  if (intersubsrc->n_frames == 0) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  }
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (GST_BASE_SRC_PAD (intersubsrc)));
  intersubsrc->n_frames++;

  *buf = buffer;

  return GST_FLOW_OK;
}

static gboolean
gst_inter_sub_src_do_seek (GstBaseSrc * src, GstSegment * segment)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "do_seek");

  return FALSE;
}

static gboolean
gst_inter_sub_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);
  gboolean ret;

  GST_DEBUG_OBJECT (intersubsrc, "query");

  switch (GST_QUERY_TYPE (query)) {
    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->query (src, query);
  }

  return ret;
}

static gboolean
gst_inter_sub_src_check_get_range (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "get_range");

  return FALSE;
}

static void
gst_inter_sub_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "fixate");
}

static gboolean
gst_inter_sub_src_unlock_stop (GstBaseSrc * src)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "stop");

  return TRUE;
}

static gboolean
gst_inter_sub_src_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment)
{
  GstInterSubSrc *intersubsrc = GST_INTER_SUB_SRC (src);

  GST_DEBUG_OBJECT (intersubsrc, "seek_segment");

  return FALSE;
}
