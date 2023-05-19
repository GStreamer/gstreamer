/* GStreamer
 * Copyright (C) 2006 David A. Schleef ds@schleef.org
 * Copyright (C) 2019 Cesar Fabian Orccon Chipana
 * Copyright (C) 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:element-imagesequencesrc
 *
 * Stream image sequences from image files.
 *
 * ```
 * gst-launch-1.0 imagesequencesrc location=image-%05d.jpg start-index=1 stop-index=50 framerate=24/1 ! decodebin ! videoconvert ! autovideosink
 * ```
 *
 * This elements implements the #GstURIHandler interface meaning that you can use it with playbin,
 * (make sure to quote the URI for the filename pattern, like: `%2505d` instead of the `%05d` you would use
 * when dealing with the location).
 *
 * Note that you can pass the #imagesequencesrc:framerate, #imagesequencesrc:start-index and #imagesequencesrc:stop-index
 * properties directly in the URI using its 'query' component, for example:
 *
 * ```
 * gst-launch-1.0 playbin uri="imagesequence://path/to/image-%2505d.jpeg?start-index=0&framerate=30/1"
 * ```
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gsttypefindhelper.h>
#include <glib/gi18n-lib.h>

#include "gstimagesequencesrc.h"

#define LOCK(self) (g_rec_mutex_lock (&self->fields_lock))
#define UNLOCK(self) (g_rec_mutex_unlock (&self->fields_lock))

static GstFlowReturn gst_image_sequence_src_create (GstPushSrc * src,
    GstBuffer ** buffer);


static void gst_image_sequence_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_image_sequence_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstCaps *gst_image_sequence_src_getcaps (GstBaseSrc * src,
    GstCaps * filter);
static gboolean gst_image_sequence_src_query (GstBaseSrc * src,
    GstQuery * query);
static void gst_image_sequence_src_set_caps (GstImageSequenceSrc * self,
    GstCaps * caps);
static void gst_image_sequence_src_set_duration (GstImageSequenceSrc * self);
static gint gst_image_sequence_src_count_frames (GstImageSequenceSrc * self,
    gboolean can_read);


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
  PROP_START_INDEX,
  PROP_STOP_INDEX,
  PROP_FRAMERATE
};

#define DEFAULT_LOCATION "%05d"
#define DEFAULT_START_INDEX 0
#define DEFAULT_STOP_INDEX -1
#define DEFAULT_FRAMERATE 30

/* Call with LOCK taken */
static gboolean
gst_image_sequence_src_set_location (GstImageSequenceSrc * self,
    const gchar * location)
{
  g_free (self->path);
  if (location != NULL)
    self->path = g_strdup (location);
  else
    self->path = NULL;

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_image_sequence_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_image_sequence_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "imagesequence", NULL };

  return protocols;
}

static gchar *
gst_image_sequence_src_uri_get_uri (GstURIHandler * handler)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (handler);
  gchar *uri = NULL;

  LOCK (self);
  if (self->uri)
    uri = gst_uri_to_string (self->uri);
  else if (self->path)
    uri = gst_uri_construct ("imagesequence", self->path);
  UNLOCK (self);

  return uri;
}

static gboolean
gst_image_sequence_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** err)
{
  gchar *hostname = NULL, *location = NULL, *path, *tmp;
  gboolean ret = FALSE;
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (handler);
  GstUri *ruri = gst_uri_from_string (uri);
  GHashTable *query = NULL;

  if (!ruri) {
    g_set_error (err, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "imagesequencesrc URI is invalid: '%s'", uri);
    goto beach;
  }


  LOCK (self);
  g_clear_pointer (&self->uri, gst_uri_unref);
  self->uri = ruri;
  path = gst_uri_get_path (ruri);
  tmp = gst_filename_to_uri (path, err);
  location = g_filename_from_uri (tmp, &hostname, err);
  g_free (tmp);
  g_free (path);
  query = gst_uri_get_query_table (ruri);
  if (!location || (err != NULL && *err != NULL)) {
    GST_WARNING_OBJECT (self, "Invalid URI '%s' for imagesequencesrc: %s", uri,
        (err != NULL && *err != NULL) ? (*err)->message : "unknown error");
    goto beach;
  }

  if (hostname && strcmp (hostname, "localhost")) {
    /* Only 'localhost' is permitted */
    GST_WARNING_OBJECT (self, "Invalid hostname '%s' for filesrc", hostname);
    g_set_error (err, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "File URI with invalid hostname '%s'", hostname);
    goto beach;
  }
#ifdef G_OS_WIN32
  /* Unfortunately, g_filename_from_uri() doesn't handle some UNC paths
   * correctly on windows, it leaves them with an extra backslash
   * at the start if they're of the mozilla-style file://///host/path/file
   * form. Correct this.
   */
  if (location[0] == '\\' && location[1] == '\\' && location[2] == '\\')
    memmove (location, location + 1, strlen (location + 1) + 1);
#endif

  ret = gst_image_sequence_src_set_location (self, location);

  if (query) {
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, query);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      GST_INFO_OBJECT (self, "Setting property from URI: %s=%s", (gchar *) key,
          (gchar *) value);
      gst_util_set_object_arg (G_OBJECT (self), key, value);
    }
  }

beach:
  UNLOCK (self);

  g_free (location);
  g_free (hostname);
  g_clear_pointer (&query, g_hash_table_unref);

  return ret;
}

static void
gst_image_sequence_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_image_sequence_src_uri_get_type;
  iface->get_protocols = gst_image_sequence_src_uri_get_protocols;
  iface->get_uri = gst_image_sequence_src_uri_get_uri;
  iface->set_uri = gst_image_sequence_src_uri_set_uri;
}

#define gst_image_sequence_src_parent_class parent_class
#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_image_sequence_src_uri_handler_init); \
  GST_DEBUG_CATEGORY_INIT (gst_image_sequence_src_debug, "imagesequencesrc", \
      0, "imagesequencesrc element");
G_DEFINE_TYPE_WITH_CODE (GstImageSequenceSrc, gst_image_sequence_src,
    GST_TYPE_PUSH_SRC, _do_init);
GST_ELEMENT_REGISTER_DEFINE (imagesequencesrc, "imagesequencesrc",
    GST_RANK_NONE, gst_image_sequence_src_get_type ());

static gboolean
is_seekable (GstBaseSrc * src)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (src);

  if ((self->n_frames != 0) && (self->fps_n) && (self->fps_d))
    return TRUE;
  return FALSE;
}


static gboolean
do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstImageSequenceSrc *self;

  self = GST_IMAGE_SEQUENCE_SRC (bsrc);

  self->reverse = segment->rate < 0;
  if (self->reverse) {
    segment->time = segment->start;
  }

  self->index =
      self->start_index +
      segment->position * self->fps_n / (self->fps_d * GST_SECOND);

  return TRUE;
}

static void
gst_image_sequence_src_finalize (GObject * object)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (object);

  g_clear_pointer (&self->path, g_free);
  g_rec_mutex_clear (&self->fields_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_image_sequence_src_dispose (GObject * object)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (object);

  gst_clear_caps (&self->caps);
  g_clear_pointer (&self->uri, gst_uri_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
  g_object_class_install_property (gobject_class, PROP_START_INDEX,
      g_param_spec_int ("start-index", "Start Index",
          "Start value of index.  The initial value of index can be set "
          "either by setting index or start-index.  When the end of the loop "
          "is reached, the index will be set to the value start-index.",
          0, INT_MAX, DEFAULT_START_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STOP_INDEX,
      g_param_spec_int ("stop-index", "Stop Index",
          "Stop value of index.  The special value -1 means no stop.",
          -1, INT_MAX, DEFAULT_STOP_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMERATE,
      gst_param_spec_fraction ("framerate", "Framerate",
          "The output framerate.",
          1, 1, G_MAXINT, 1, DEFAULT_FRAMERATE, 1,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_image_sequence_src_finalize;
  gobject_class->dispose = gst_image_sequence_src_dispose;

  gstbasesrc_class->get_caps = gst_image_sequence_src_getcaps;
  gstbasesrc_class->query = gst_image_sequence_src_query;
  gstbasesrc_class->is_seekable = is_seekable;
  gstbasesrc_class->do_seek = do_seek;

  gstpushsrc_class->create = gst_image_sequence_src_create;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_image_sequence_src_pad_template);
  gst_element_class_set_static_metadata (gstelement_class,
      "Image Sequence Source", "Source/File/Video",
      "Create a video stream from a sequence of image files",
      "Cesar Fabian Orccon Chipana <cfoch.fabian@gmail.com>, "
      "Thibault Saunier <tsaunier@igalia.com>");
}

static void
gst_image_sequence_src_init (GstImageSequenceSrc * self)
{
  GstBaseSrc *bsrc;

  GST_DEBUG_CATEGORY_INIT (gst_image_sequence_src_debug, "imagesequencesrc", 0,
      "imagesequencesrc element");

  bsrc = GST_BASE_SRC (self);
  gst_base_src_set_format (bsrc, GST_FORMAT_TIME);

  g_rec_mutex_init (&self->fields_lock);
  self->start_index = DEFAULT_START_INDEX;
  self->index = 0;
  self->stop_index = DEFAULT_STOP_INDEX;
  self->path = g_strdup (DEFAULT_LOCATION);
  self->caps = NULL;
  self->n_frames = 0;
  self->fps_n = 30;
  self->fps_d = 1;
}

static GstCaps *
gst_image_sequence_src_getcaps (GstBaseSrc * src, GstCaps * filter)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (src);

  GST_DEBUG_OBJECT (self, "returning %" GST_PTR_FORMAT, self->caps);

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
  GstImageSequenceSrc *self;

  self = GST_IMAGE_SEQUENCE_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          LOCK (self);
          if (self->n_frames <= 0) {
            gst_image_sequence_src_count_frames (self, FALSE);
            gst_image_sequence_src_set_duration (self);
          }

          if (self->n_frames > 0)
            gst_query_set_duration (query, format, self->duration);
          UNLOCK (self);

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

static void
gst_image_sequence_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (object);

  LOCK (self);
  switch (prop_id) {
    case PROP_LOCATION:
      gst_image_sequence_src_set_location (self, g_value_get_string (value));
      break;
    case PROP_START_INDEX:
      self->start_index = g_value_get_int (value);
      gst_image_sequence_src_count_frames (self, FALSE);
      break;
    case PROP_STOP_INDEX:
      self->stop_index = g_value_get_int (value);
      gst_image_sequence_src_count_frames (self, FALSE);
      break;
    case PROP_FRAMERATE:
      self->fps_n = gst_value_get_fraction_numerator (value);
      self->fps_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  UNLOCK (self);
}

static void
gst_image_sequence_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImageSequenceSrc *self = GST_IMAGE_SEQUENCE_SRC (object);

  LOCK (self);
  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, self->path);
      break;
    case PROP_START_INDEX:
      g_value_set_int (value, self->start_index);
      break;
    case PROP_STOP_INDEX:
      g_value_set_int (value, self->stop_index);
      break;
    case PROP_FRAMERATE:
      self->fps_n = gst_value_get_fraction_numerator (value);
      self->fps_d = gst_value_get_fraction_denominator (value);
      GST_DEBUG_OBJECT (self, "Set (framerate) property to (%d/%d)",
          self->fps_n, self->fps_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  UNLOCK (self);
}

/* Call with LOCK */
static gint
gst_image_sequence_src_count_frames (GstImageSequenceSrc * self,
    gboolean can_read)
{
  if (can_read && self->stop_index < 0 && self->path) {
    gint i;

    for (i = self->start_index;; i++) {
      gchar *filename = g_strdup_printf (self->path, i);

      if (!g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
        i--;
        g_free (filename);
        break;
      }

      g_free (filename);
    }
    if (i > self->start_index)
      self->stop_index = i;
  }

  if (self->stop_index >= self->start_index)
    self->n_frames = self->stop_index - self->start_index + 1;
  return self->n_frames;
}

static void
gst_image_sequence_src_set_caps (GstImageSequenceSrc * self, GstCaps * caps)
{
  GstCaps *new_caps;

  g_assert (caps != NULL);
  new_caps = gst_caps_copy (caps);

  if (self->n_frames > 0) {
    GValue fps = G_VALUE_INIT;
    g_value_init (&fps, GST_TYPE_FRACTION);
    gst_value_set_fraction (&fps, self->fps_n, self->fps_d);
    gst_caps_set_value (new_caps, "framerate", &fps);
    g_value_unset (&fps);
  }

  gst_caps_replace (&self->caps, new_caps);
  gst_pad_set_caps (GST_BASE_SRC_PAD (self), new_caps);

  GST_DEBUG_OBJECT (self, "Setting new caps: %" GST_PTR_FORMAT, new_caps);

  gst_caps_unref (new_caps);
}

/* Call with LOCK */
static void
gst_image_sequence_src_set_duration (GstImageSequenceSrc * self)
{
  GstClockTime old_duration = self->duration;

  if (self->n_frames <= 0)
    return;

  /* Calculate duration */
  self->duration =
      gst_util_uint64_scale (GST_SECOND * self->n_frames, self->fps_d,
      self->fps_n);

  if (self->duration != old_duration) {
    UNLOCK (self);
    gst_element_post_message (GST_ELEMENT (self),
        gst_message_new_duration_changed (GST_OBJECT (self)));
    LOCK (self);
  }
}

/* Call with LOCK */
static gchar *
gst_image_sequence_src_get_filename (GstImageSequenceSrc * self)
{
  gchar *filename;

  GST_DEBUG ("Reading filename at index %d.", self->index);
  if (self->path != NULL) {
    filename = g_strdup_printf (self->path, self->index);
  } else {
    GST_WARNING_OBJECT (self, "No filename location set!");
    filename = NULL;
  }

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
  gint fps_n, fps_d, start_index, stop_index;

  self = GST_IMAGE_SEQUENCE_SRC (src);

  LOCK (self);
  start_index = self->start_index;
  stop_index = self->stop_index;
  if (self->index > stop_index && stop_index > 0) {
    UNLOCK (self);

    return GST_FLOW_EOS;
  }

  if (self->index < self->start_index)
    self->index = self->start_index;

  g_assert (start_index <= self->index &&
      (self->index <= stop_index || stop_index <= 0));

  filename = gst_image_sequence_src_get_filename (self);
  fps_n = self->fps_n;
  fps_d = self->fps_d;
  UNLOCK (self);

  if (!filename)
    goto error_no_filename;

  ret = g_file_get_contents (filename, &data, &size, &error);
  if (!ret)
    goto handle_error;

  buf = gst_buffer_new_wrapped (data, size);

  if (!self->caps) {
    GstCaps *caps;
    caps = gst_type_find_helper_for_buffer (NULL, buf, NULL);
    if (!caps) {
      GST_ELEMENT_ERROR (self, STREAM, TYPE_NOT_FOUND, (NULL),
          ("Could not determine image type."));

      return GST_FLOW_NOT_SUPPORTED;
    }

    LOCK (self);
    gst_image_sequence_src_count_frames (self, TRUE);
    gst_image_sequence_src_set_duration (self);
    UNLOCK (self);

    gst_image_sequence_src_set_caps (self, caps);
    gst_caps_unref (caps);
  }

  GST_BUFFER_PTS (buf) =
      gst_util_uint64_scale_ceil ((self->index - start_index) * GST_SECOND,
      fps_d, fps_n);
  GST_BUFFER_DURATION (buf) = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
  GST_BUFFER_OFFSET (buf) = self->index - start_index;
  GST_LOG_OBJECT (self, "index: %d, %s - %" GST_PTR_FORMAT, self->index,
      filename, buf);

  g_free (filename);
  *buffer = buf;

  self->index += self->reverse ? -1 : 1;
  return GST_FLOW_OK;

error_no_filename:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        (_("No file name specified for reading.")), (NULL));
    return GST_FLOW_ERROR;
  }
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
