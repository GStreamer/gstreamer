/* GStreamer
 * Copyright (C) 2019 Cesar Fabian Orccon Chipana
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
 * SECTION:element-gstimagesequencesrc
 *
 * Stream image sequences from image files.
 * <refsect2>
 *
 * |[
 * gst-launch-1.0 imagesequencesrc location=%d.jpg start-index=1 stop-index=50 framerate=24/1 ! decodebin ! videoconvert ! autovideosink
 * ]|
 *
 * </refsect2>
 */

#include <gst/gst.h>
#include <gst/base/gsttypefindhelper.h>

#include "gstimagesequencesrc.h"

static GstFlowReturn gst_image_sequence_src_create (GstPushSrc * src,
    GstBuffer ** buffer);

static void gst_image_sequence_src_dispose (GObject * object);

static void gst_image_sequence_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_image_sequence_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstCaps *gst_image_sequence_src_getcaps (GstBaseSrc * src,
    GstCaps * filter);
static gboolean gst_image_sequence_src_query (GstBaseSrc * src,
    GstQuery * query);
static void gst_image_sequence_src_set_caps (GstImageSequenceSrc * src,
    GstCaps * caps);
static void gst_image_sequence_src_set_duration (GstImageSequenceSrc * src);
static gint gst_image_sequence_src_count_frames (GstImageSequenceSrc * self);


static GstStaticPadTemplate gst_image_sequence_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_image_sequence_src_debug);
#define GST_CAT_DEFAULT gst_image_sequence_src_debug

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_INDEX,
  PROP_START_INDEX,
  PROP_STOP_INDEX,
  PROP_FRAMERATE
};

#define DEFAULT_LOCATION "%05d"
#define DEFAULT_INDEX 0

#define gst_image_sequence_src_parent_class parent_class
#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_image_sequence_src_debug, "imagesequencesrc", \
      0, "imagesequencesrc element");
G_DEFINE_TYPE_WITH_CODE (GstImageSequenceSrc, gst_image_sequence_src,
    GST_TYPE_PUSH_SRC, _do_init);

static gboolean
is_seekable (GstBaseSrc * src)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (src);

  if ((self->count_frames != 0) && (self->fps_n) && (self->fps_d))
    return TRUE;
  return FALSE;
}


static gboolean
do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstImageSequenceSrc *src;
  gboolean reverse;

  src = GST_IMAGE_SEQUENCE_SRC (bsrc);

  reverse = segment->rate < 0;

  if (reverse) {
    GST_FIXME_OBJECT (src, "Handle reverse playback");
    return FALSE;
  }

  segment->time = segment->start;
  src->index = src->start_index +
      segment->position * src->fps_n / (src->fps_d * GST_SECOND);

  GST_DEBUG_OBJECT (src, "Seek to frame at index %d.\n", src->index);

  return TRUE;
}

static void
gst_image_sequence_src_class_init (GstImageSequenceSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_image_sequence_src_set_property;
  gobject_class->get_property = gst_image_sequence_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Pattern to create file names of input files.  File names are "
          "created by calling sprintf() with the pattern and the current "
          "index.", DEFAULT_LOCATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INDEX,
      g_param_spec_int ("index", "File Index",
          "Index to use with location property to create file names.  The "
          "index is incremented by one for each buffer read.",
          0, INT_MAX, DEFAULT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_START_INDEX,
      g_param_spec_int ("start-index", "Start Index",
          "Start value of index.  The initial value of index can be set "
          "either by setting index or start-index.  When the end of the loop "
          "is reached, the index will be set to the value start-index.",
          0, INT_MAX, DEFAULT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STOP_INDEX,
      g_param_spec_int ("stop-index", "Stop Index",
          "Stop value of index.  The special value -1 means no stop.",
          -1, INT_MAX, DEFAULT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMERATE,
      gst_param_spec_fraction ("framerate", "Framerate",
          "Set the framerate to internal caps.",
          1, 1, G_MAXINT, 1, 1, 1, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->dispose = gst_image_sequence_src_dispose;

  gstbasesrc_class->get_caps = gst_image_sequence_src_getcaps;
  gstbasesrc_class->query = gst_image_sequence_src_query;
  gstbasesrc_class->is_seekable = is_seekable;
  gstbasesrc_class->do_seek = do_seek;

  gstpushsrc_class->create = gst_image_sequence_src_create;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_image_sequence_src_pad_template);
  gst_element_class_set_static_metadata (gstelement_class,
      "ImageSequenceSrc plugin", "Src/File",
      "Creates an image-sequence video stream",
      "Cesar Fabian Orccon Chipana <cfoch.fabian@gmail.com>");
}

static void
gst_image_sequence_src_init (GstImageSequenceSrc * self)
{
  GstBaseSrc *bsrc;

  GST_DEBUG_CATEGORY_INIT (gst_image_sequence_src_debug, "imagesequencesrc", 0,
      "imagesequencesrc element");

  bsrc = GST_BASE_SRC (self);
  gst_base_src_set_format (bsrc, GST_FORMAT_TIME);
  self->start_index = DEFAULT_INDEX;
  self->index = DEFAULT_INDEX;
  self->stop_index = -1;
  self->filename = g_strdup (DEFAULT_LOCATION);
  self->caps = NULL;
  self->count_frames = 0;
  self->fps_n = self->fps_d = 1;
}

static void
gst_image_sequence_src_dispose (GObject * object)
{
  GstImageSequenceSrc *src = GST_IMAGE_SEQUENCE_SRC (object);

  g_clear_pointer (&src->filename, g_free);
  if (src->caps)
    gst_clear_caps (&src->caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_image_sequence_src_getcaps (GstBaseSrc * src, GstCaps * filter)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (src);

  GST_DEBUG_OBJECT (src, "returning %" GST_PTR_FORMAT, self->caps);

  if (filter) {
    if (self->caps)
      return gst_caps_intersect_full (filter, self->caps,
          GST_CAPS_INTERSECT_FIRST);
    else
      return gst_caps_ref (filter);
  }

  return gst_caps_new_any ();
}

static gboolean
gst_image_sequence_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean ret;
  GstImageSequenceSrc *src;

  src = GST_IMAGE_SEQUENCE_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          if (src->count_frames > 0)
            gst_query_set_duration (query, format, src->duration);
          ret = TRUE;
          break;
        default:
          ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      }
      break;
    }
    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

  return ret;
}

static gboolean
gst_image_sequence_src_set_location (GstImageSequenceSrc * src,
    const gchar * location)
{
  g_free (src->filename);
  if (location != NULL)
    src->filename = g_strdup (location);
  else
    src->filename = NULL;

  return TRUE;
}

static void
gst_image_sequence_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_image_sequence_src_set_location (self, g_value_get_string (value));
      break;
    case PROP_INDEX:
      GST_OBJECT_LOCK (self);
      /* index was really meant to be read-only, but for backwards-compatibility
       * we set start_index to make it work as it used to */
      if (!GST_OBJECT_FLAG_IS_SET (self, GST_BASE_SRC_FLAG_STARTED))
        self->start_index = g_value_get_int (value);
      else
        self->index = g_value_get_int (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_START_INDEX:
      self->start_index = g_value_get_int (value);
      gst_image_sequence_src_count_frames (self);
      break;
    case PROP_STOP_INDEX:
      self->stop_index = g_value_get_int (value);
      gst_image_sequence_src_count_frames (self);
      break;
    case PROP_FRAMERATE:
      self->fps_n = gst_value_get_fraction_numerator (value);
      self->fps_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_image_sequence_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImageSequenceSrc *src = GST_IMAGE_SEQUENCE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case PROP_INDEX:
      g_value_set_int (value, src->index);
      break;
    case PROP_START_INDEX:
      g_value_set_int (value, src->start_index);
      break;
    case PROP_STOP_INDEX:
      g_value_set_int (value, src->stop_index);
      break;
    case PROP_FRAMERATE:
      src->fps_n = gst_value_get_fraction_numerator (value);
      src->fps_d = gst_value_get_fraction_denominator (value);
      GST_DEBUG_OBJECT (src, "Set (framerate) property to (%d/%d)", src->fps_n,
          src->fps_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
gst_image_sequence_src_count_frames (GstImageSequenceSrc * self)
{
  self->count_frames = self->stop_index - self->start_index + 1;
  return self->count_frames;
}

static void
gst_image_sequence_src_set_caps (GstImageSequenceSrc * self, GstCaps * caps)
{
  GstCaps *new_caps;

  g_assert (caps != NULL);
  new_caps = gst_caps_copy (caps);

  if (self->count_frames > 0) {
    GValue fps = G_VALUE_INIT;
    g_value_init (&fps, GST_TYPE_FRACTION);
    gst_value_set_fraction (&fps, self->fps_n, self->fps_d);
    gst_caps_set_value (new_caps, "framerate", &fps);
  }

  gst_caps_replace (&self->caps, new_caps);
  gst_pad_set_caps (GST_BASE_SRC_PAD (self), new_caps);

  GST_DEBUG_OBJECT (self, "Setting new caps: %s",
      gst_caps_to_string (new_caps));
}

static void
gst_image_sequence_src_set_duration (GstImageSequenceSrc * self)
{
  /* Calculate duration */
  self->duration =
      gst_util_uint64_scale (GST_SECOND * self->count_frames, self->fps_d,
      self->fps_n);
}

static gchar *
gst_image_sequence_src_get_filename (GstImageSequenceSrc * self)
{
  gchar *filename;

  GST_DEBUG ("Reading filename at index %d.", self->index);
  filename = g_strdup_printf (self->filename, self->index);

  return filename;
}

static GstFlowReturn
gst_image_sequence_src_create (GstPushSrc * src, GstBuffer ** buffer)
{
  GstImageSequenceSrc *self;
  gsize size;
  gchar *data;
  gchar *filename;
  GstBuffer *buf;
  gboolean ret;
  GError *error = NULL;

  self = GST_IMAGE_SEQUENCE_SRC (src);

  if (self->index > self->stop_index)
    return GST_FLOW_EOS;

  if (self->index < self->start_index)
    self->index = self->start_index;

  g_assert (self->start_index <= self->index &&
      self->index <= self->stop_index);

  filename = gst_image_sequence_src_get_filename (self);

  GST_DEBUG_OBJECT (self, "reading from file \"%s\".", filename);

  ret = g_file_get_contents (filename, &data, &size, &error);
  if (!ret)
    goto handle_error;

  buf = gst_buffer_new_wrapped (data, size);

  if (!self->caps) {
    GstCaps *caps;
    caps = gst_type_find_helper_for_buffer (NULL, buf, NULL);
    gst_image_sequence_src_set_caps (self, caps);
    gst_image_sequence_src_set_duration (self);
    gst_caps_unref (caps);
  }

  self->buffer_duration = GST_SECOND * self->fps_d / self->fps_n;

  GST_BUFFER_PTS (buf) =
      (self->index - self->start_index) * self->buffer_duration;
  GST_BUFFER_DURATION (buf) = self->buffer_duration;

  GST_DEBUG_OBJECT (self, "index: %d", self->index);

  GST_DEBUG_OBJECT (self, "Timestamp: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buf)));
  GST_DEBUG_OBJECT (self, "Buffer duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  GST_BUFFER_OFFSET (buf) = self->index - self->start_index;
  GST_BUFFER_OFFSET_END (buf) = self->index - self->start_index + 1;

  GST_DEBUG_OBJECT (self, "read file \"%s\".", filename);

  g_free (filename);
  *buffer = buf;

  self->index++;
  return GST_FLOW_OK;

handle_error:
  {
    if (error != NULL) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ,
          ("Error while reading from file \"%s\".", filename),
          ("%s", error->message));
      g_error_free (error);
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, READ,
          ("Error while reading from file \"%s\".", filename),
          ("%s", g_strerror (errno)));
    }
    g_free (filename);
    return GST_FLOW_ERROR;
  }
}
