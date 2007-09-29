/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
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

/**
 * SECTION:element-giosrc
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch giosrc location=file:///home/foo/bar.ext ! fakesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiosrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_src_debug);
#define GST_CAT_DEFAULT gst_gio_src_debug

enum
{
  ARG_0,
  ARG_LOCATION
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE_FULL (GstGioSrc, gst_gio_src, GstBaseSrc, GST_TYPE_BASE_SRC,
    gst_gio_uri_handler_do_init);

static void gst_gio_src_finalize (GObject * object);
static void gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_gio_src_start (GstBaseSrc * base_src);
static gboolean gst_gio_src_stop (GstBaseSrc * base_src);
static gboolean gst_gio_src_get_size (GstBaseSrc * base_src, guint64 * size);
static gboolean gst_gio_src_is_seekable (GstBaseSrc * base_src);
static gboolean gst_gio_src_unlock (GstBaseSrc * base_src);
static gboolean gst_gio_src_unlock_stop (GstBaseSrc * base_src);
static gboolean gst_gio_src_check_get_range (GstBaseSrc * base_src);
static GstFlowReturn gst_gio_src_create (GstBaseSrc * base_src, guint64 offset,
    guint size, GstBuffer ** buf);

static void
gst_gio_src_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "GIO source",
    "Source/File",
    "Read from any GIO-supported location",
    "Ren\xc3\xa9 Stadler <mail@renestadler.de>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_src_debug, "giosrc", 0, "GIO source");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gio_src_class_init (GstGioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->finalize = gst_gio_src_finalize;
  gobject_class->set_property = gst_gio_src_set_property;
  gobject_class->get_property = gst_gio_src_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location", "URI location to read from",
          NULL, G_PARAM_READWRITE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gio_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_gio_src_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_gio_src_get_size);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_gio_src_is_seekable);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_gio_src_unlock);
  gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_gio_src_unlock_stop);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_gio_src_check_get_range);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_gio_src_create);
}

static void
gst_gio_src_init (GstGioSrc * src, GstGioSrcClass * gclass)
{
  src->cancel = g_cancellable_new ();
}

static void
gst_gio_src_finalize (GObject * object)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  g_object_unref (src->cancel);

  if (src->file)
    g_object_unref (src->file);

  g_free (src->location);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED)
        break;

      g_free (src->location);
      src->location = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gio_src_start (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);
  GError *err = NULL;

  if (src->location == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No location given"));
    return FALSE;
  }

  src->file = g_file_new_for_uri (src->location);

  if (src->file == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Malformed URI or protocol not supported (%s)", src->location));
    return FALSE;
  }

  src->stream = g_file_read (src->file, src->cancel, &err);

  if (src->stream == NULL && !gst_gio_error (src, "g_file_read", &err, NULL)) {

    if (GST_GIO_ERROR_MATCHES (err, NOT_FOUND))
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open location %s for reading: %s",
              src->location, err->message));
    else
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open location %s for reading: %s",
              src->location, err->message));

    g_clear_error (&err);

    g_object_unref (src->file);
    src->file = NULL;

    return FALSE;

  } else if (src->stream == NULL) {
    g_object_unref (src->file);
    return FALSE;
  }

  src->position = 0;

  GST_DEBUG_OBJECT (src, "opened location %s", src->location);

  return TRUE;
}

static gboolean
gst_gio_src_stop (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);
  gboolean success = TRUE;
  GError *err = NULL;

  if (src->stream != NULL) {
    /* FIXME: In case that the call below would block, there is no one to
     * trigger the cancellation! */

    success = g_input_stream_close (G_INPUT_STREAM (src->stream), src->cancel,
        &err);

    if (!success && !gst_gio_error (src, "g_input_stream_close", &err, NULL)) {
      GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, (NULL),
          ("g_input_stream_close failed: %s", err->message));
      g_clear_error (&err);
    }

    g_object_unref (src->stream);
    src->stream = NULL;
  }

  if (src->file != NULL) {
    g_object_unref (src->file);
    src->file = NULL;
  }

  GST_DEBUG_OBJECT (src, "closed location %s", src->location);

  return success;
}

static gboolean
gst_gio_src_get_size (GstBaseSrc * base_src, guint64 * size)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);
  GFileInfo *info;
  GError *err = NULL;

  info = g_file_input_stream_query_info (src->stream,
      G_FILE_ATTRIBUTE_STD_SIZE, src->cancel, &err);

  if (info != NULL) {
    *size = g_file_info_get_size (info);
    g_object_unref (info);
    GST_DEBUG_OBJECT (src, "found size: %" G_GUINT64_FORMAT, *size);
    return TRUE;
  }

  if (!gst_gio_error (src, "g_file_input_stream_query_info", &err, NULL)) {

    if (GST_GIO_ERROR_MATCHES (err, NOT_SUPPORTED))
      GST_DEBUG_OBJECT (src, "size information not available");
    else
      GST_WARNING_OBJECT (src, "size information retrieval failed: %s",
          err->message);

    g_clear_error (&err);
  }

  return FALSE;
}

static gboolean
gst_gio_src_is_seekable (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);
  gboolean seekable;

  seekable = g_seekable_can_seek (G_SEEKABLE (src->stream));

  GST_DEBUG_OBJECT (src, "can seek: %d", seekable);

  return seekable;
}

static gboolean
gst_gio_src_unlock (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);

  GST_LOG_OBJECT (src, "triggering cancellation");

  g_cancellable_cancel (src->cancel);

  return TRUE;
}

static gboolean
gst_gio_src_unlock_stop (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);

  GST_LOG_OBJECT (src, "restoring cancellable");

  g_object_unref (src->cancel);
  src->cancel = g_cancellable_new ();

  return TRUE;
}

static gboolean
gst_gio_src_check_get_range (GstBaseSrc * base_src)
{
  /* FIXME: Implement dry-run variant using guesswork like gnomevfssrc? */

  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS,
      check_get_range, (base_src), FALSE);
}

static GstFlowReturn
gst_gio_src_create (GstBaseSrc * base_src, guint64 offset, guint size,
    GstBuffer ** buf_return)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);
  GstBuffer *buf;
  gssize read;
  gboolean success, eos;
  GstFlowReturn ret = GST_FLOW_OK;
  GError *err = NULL;

  if (G_UNLIKELY (offset != src->position)) {

    ret = gst_gio_seek (src, G_SEEKABLE (src->stream), offset, src->cancel);

    if (ret == GST_FLOW_OK)
      src->position = offset;
    else
      return ret;
  }

  buf = gst_buffer_new_and_alloc (size);

  GST_LOG_OBJECT (src, "reading %u bytes from offset %" G_GUINT64_FORMAT,
      size, offset);

  read =
      g_input_stream_read (G_INPUT_STREAM (src->stream), GST_BUFFER_DATA (buf),
      size, src->cancel, &err);

  success = (read >= 0);
  eos = (size > 0 && read == 0);

  if (!success && !gst_gio_error (src, "g_input_stream_read", &err, &ret)) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not read from location %s: %s", src->location, err->message));
    g_clear_error (&err);
  }

  if (success && !eos) {
    src->position += read;
    GST_BUFFER_OFFSET (buf) = offset;
    GST_BUFFER_SIZE (buf) = read;
    *buf_return = buf;
  } else {
    /* !success || eos */
    gst_buffer_unref (buf);
  }

  if (eos)
    ret = GST_FLOW_UNEXPECTED;

  return ret;
}
