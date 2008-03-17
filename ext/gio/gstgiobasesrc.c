/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
#include <config.h>
#endif

#include "gstgiobasesrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_base_src_debug);
#define GST_CAT_DEFAULT gst_gio_base_src_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstGioBaseSrc, gst_gio_base_src, GstBaseSrc,
    GST_TYPE_BASE_SRC);

static void gst_gio_base_src_finalize (GObject * object);
static gboolean gst_gio_base_src_start (GstBaseSrc * base_src);
static gboolean gst_gio_base_src_stop (GstBaseSrc * base_src);
static gboolean gst_gio_base_src_get_size (GstBaseSrc * base_src,
    guint64 * size);
static gboolean gst_gio_base_src_is_seekable (GstBaseSrc * base_src);
static gboolean gst_gio_base_src_unlock (GstBaseSrc * base_src);
static gboolean gst_gio_base_src_unlock_stop (GstBaseSrc * base_src);
static gboolean gst_gio_base_src_check_get_range (GstBaseSrc * base_src);
static GstFlowReturn gst_gio_base_src_create (GstBaseSrc * base_src,
    guint64 offset, guint size, GstBuffer ** buf);

static void
gst_gio_base_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_base_src_debug, "gio_base_src", 0,
      "GIO base source");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
}

static void
gst_gio_base_src_class_init (GstGioBaseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->finalize = gst_gio_base_src_finalize;

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gio_base_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_gio_base_src_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_gio_base_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_gio_base_src_is_seekable);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_gio_base_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_gio_base_src_unlock_stop);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_gio_base_src_check_get_range);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_gio_base_src_create);
}

static void
gst_gio_base_src_init (GstGioBaseSrc * src, GstGioBaseSrcClass * gclass)
{
  src->cancel = g_cancellable_new ();
}

static void
gst_gio_base_src_finalize (GObject * object)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (object);

  if (src->cancel) {
    g_object_unref (src->cancel);
    src->cancel = NULL;
  }

  if (src->stream) {
    g_object_unref (src->stream);
    src->stream = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static gboolean
gst_gio_base_src_start (GstBaseSrc * base_src)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);

  if (!G_IS_INPUT_STREAM (src->stream)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("No stream given yet"));
    return FALSE;
  }

  src->position = 0;

  GST_DEBUG_OBJECT (src, "started stream");

  return TRUE;
}

static gboolean
gst_gio_base_src_stop (GstBaseSrc * base_src)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);
  gboolean success;
  GError *err = NULL;

  if (G_IS_INPUT_STREAM (src->stream)) {
    GST_DEBUG_OBJECT (src, "closing stream");

    /* FIXME: can block but unfortunately we can't use async operations
     * here because they require a running main loop */
    success = g_input_stream_close (src->stream, src->cancel, &err);

    if (!success && !gst_gio_error (src, "g_input_stream_close", &err, NULL)) {
      GST_ELEMENT_WARNING (src, RESOURCE, CLOSE, (NULL),
          ("g_input_stream_close failed: %s", err->message));
      g_clear_error (&err);
    } else if (!success) {
      GST_ELEMENT_WARNING (src, RESOURCE, CLOSE, (NULL),
          ("g_input_stream_close failed"));
    } else {
      GST_DEBUG_OBJECT (src, "g_input_stream_close succeeded");
    }

    g_object_unref (src->stream);
    src->stream = NULL;
  }

  return TRUE;
}

static gboolean
gst_gio_base_src_get_size (GstBaseSrc * base_src, guint64 * size)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);

  if (G_IS_FILE_INPUT_STREAM (src->stream)) {
    GFileInfo *info;
    GError *err = NULL;

    info = g_file_input_stream_query_info (G_FILE_INPUT_STREAM (src->stream),
        G_FILE_ATTRIBUTE_STANDARD_SIZE, src->cancel, &err);

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
  } else if (GST_GIO_STREAM_IS_SEEKABLE (src->stream)) {
    goffset old;
    goffset stream_size;
    gboolean ret;
    GSeekable *seekable = G_SEEKABLE (src->stream);
    GError *err = NULL;

    old = g_seekable_tell (seekable);

    ret = g_seekable_seek (seekable, 0, G_SEEK_END, src->cancel, &err);
    if (!ret) {
      if (!gst_gio_error (src, "g_seekable_seek", &err, NULL)) {
        if (GST_GIO_ERROR_MATCHES (err, NOT_SUPPORTED))
          GST_DEBUG_OBJECT (src,
              "Seeking to the end of stream is not supported");
        else
          GST_WARNING_OBJECT (src, "Seeking to end of stream failed: %s",
              err->message);
        g_clear_error (&err);
      } else {
        GST_WARNING_OBJECT (src, "Seeking to end of stream failed");
      }

      return FALSE;
    }

    stream_size = g_seekable_tell (seekable);

    ret = g_seekable_seek (seekable, old, G_SEEK_SET, src->cancel, &err);
    if (!ret) {
      if (!gst_gio_error (src, "g_seekable_seek", &err, NULL)) {
        if (GST_GIO_ERROR_MATCHES (err, NOT_SUPPORTED))
          GST_ERROR_OBJECT (src, "Seeking to the old position not supported");
        else
          GST_ERROR_OBJECT (src, "Seeking to the old position failed: %s",
              err->message);
        g_clear_error (&err);
      } else {
        GST_ERROR_OBJECT (src, "Seeking to the old position faile");
      }

      return FALSE;
    }

    if (stream_size >= 0) {
      *size = stream_size;
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_gio_base_src_is_seekable (GstBaseSrc * base_src)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);
  gboolean seekable;

  seekable = GST_GIO_STREAM_IS_SEEKABLE (src->stream);

  GST_DEBUG_OBJECT (src, "can seek: %d", seekable);

  return seekable;
}

static gboolean
gst_gio_base_src_unlock (GstBaseSrc * base_src)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);

  GST_LOG_OBJECT (src, "triggering cancellation");

  g_cancellable_cancel (src->cancel);

  return TRUE;
}

static gboolean
gst_gio_base_src_unlock_stop (GstBaseSrc * base_src)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);

  GST_LOG_OBJECT (src, "resetting cancellable");

  g_cancellable_reset (src->cancel);

  return TRUE;
}

static gboolean
gst_gio_base_src_check_get_range (GstBaseSrc * base_src)
{
  /* FIXME: Implement dry-run variant using guesswork like gnomevfssrc? */

  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS,
      check_get_range, (base_src), FALSE);
}

static GstFlowReturn
gst_gio_base_src_create (GstBaseSrc * base_src, guint64 offset, guint size,
    GstBuffer ** buf_return)
{
  GstGioBaseSrc *src = GST_GIO_BASE_SRC (base_src);
  GstBuffer *buf;
  gssize read;
  gboolean success, eos;
  GstFlowReturn ret = GST_FLOW_OK;
  GError *err = NULL;

  g_return_val_if_fail (G_IS_INPUT_STREAM (src->stream), GST_FLOW_ERROR);

  if (G_UNLIKELY (offset != src->position)) {
    if (!GST_GIO_STREAM_IS_SEEKABLE (src->stream))
      return GST_FLOW_NOT_SUPPORTED;

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
        ("Could not read from stream: %s", err->message));
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

void
gst_gio_base_src_set_stream (GstGioBaseSrc * src, GInputStream * stream)
{
  gboolean success;
  GError *err = NULL;

  g_return_if_fail (G_IS_INPUT_STREAM (stream));
  g_return_if_fail ((GST_STATE (src) != GST_STATE_PLAYING &&
          GST_STATE (src) != GST_STATE_PAUSED));

  if (G_IS_INPUT_STREAM (src->stream)) {
    GST_DEBUG_OBJECT (src, "closing old stream");

    /* FIXME: can block but unfortunately we can't use async operations
     * here because they require a running main loop */
    success = g_input_stream_close (src->stream, src->cancel, &err);

    if (!success && !gst_gio_error (src, "g_input_stream_close", &err, NULL)) {
      GST_ELEMENT_WARNING (src, RESOURCE, CLOSE, (NULL),
          ("g_input_stream_close failed: %s", err->message));
      g_clear_error (&err);
    } else if (!success) {
      GST_ELEMENT_WARNING (src, RESOURCE, CLOSE, (NULL),
          ("g_input_stream_close failed"));
    } else {
      GST_DEBUG_OBJECT (src, "g_input_stream_close succeeded");
    }

    g_object_unref (src->stream);
    src->stream = NULL;
  }

  src->stream = stream;
}
