/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-giosrc
 * @see_also: #GstFileSrc, #GstGnomeVFSSrc, #GstGioSink
 *
 * This plugin reads data from a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GIO library or it's VFS backends. Common protocols are 'file', 'http',
 * 'ftp', or 'smb'.
 *
 * If an URI or #GFile is not mounted giosrc will post a message of type
 * %GST_MESSAGE_ELEMENT with name "not-mounted" on the bus. The message
 * also contains the #GFile and the corresponding URI.
 * Applications can use the "not-mounted" message to mount the #GFile
 * by calling g_file_mount_enclosing_volume() and then restart the
 * pipeline after the mounting has succeeded. Note that right after the
 * "not-mounted" message a normal error message is posted on the bus which
 * should be ignored if "not-mounted" is handled by the application, for
 * example by calling gst_bus_set_flushing(bus, TRUE) after the "not-mounted"
 * message was received and gst_bus_set_flushing(bus, FALSE) after the
 * mounting was successful.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v giosrc location=file:///home/joe/foo.xyz ! fakesink
 * ]| The above pipeline will simply read a local file and do nothing with the
 * data read. Instead of giosrc, we could just as well have used the
 * filesrc element here.
 * |[
 * gst-launch -v giosrc location=smb://othercomputer/foo.xyz ! filesink location=/home/joe/foo.xyz
 * ]| The above pipeline will copy a file from a remote host to the local file
 * system using the Samba protocol.
 * |[
 * gst-launch -v giosrc location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert ! audioresample ! alsasink
 * ]| The above pipeline will read and decode and play an mp3 file from a
 * web server using the http protocol.
 * </refsect2>
 */

/* FIXME: We would like to mount the enclosing volume of an URL
 *        if it isn't mounted yet but this is possible async-only.
 *        Unfortunately this requires a running main loop from the
 *        default context and we can't guarantuee this!
 *
 *        We would also like to do authentication while mounting.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiosrc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_gio_src_debug);
#define GST_CAT_DEFAULT gst_gio_src_debug

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_FILE
};

#define gst_gio_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGioSrc, gst_gio_src,
    GST_TYPE_GIO_BASE_SRC, gst_gio_uri_handler_do_init (g_define_type_id));

static void gst_gio_src_finalize (GObject * object);

static void gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GInputStream *gst_gio_src_get_stream (GstGioBaseSrc * bsrc);

static gboolean gst_gio_src_query (GstBaseSrc * base_src, GstQuery * query);

static void
gst_gio_src_class_init (GstGioSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseSrcClass *gstbasesrc_class = (GstBaseSrcClass *) klass;
  GstGioBaseSrcClass *gstgiobasesrc_class = (GstGioBaseSrcClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_gio_src_debug, "gio_src", 0, "GIO source");

  gobject_class->finalize = gst_gio_src_finalize;
  gobject_class->set_property = gst_gio_src_set_property;
  gobject_class->get_property = gst_gio_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location", "URI location to read from",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstGioSrc:file:
   * 
   * %GFile to read from.
   */
  g_object_class_install_property (gobject_class, PROP_FILE,
      g_param_spec_object ("file", "File", "GFile to read from",
          G_TYPE_FILE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "GIO source",
      "Source/File",
      "Read from any GIO-supported location",
      "Ren\xc3\xa9 Stadler <mail@renestadler.de>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_gio_src_query);

  gstgiobasesrc_class->get_stream = GST_DEBUG_FUNCPTR (gst_gio_src_get_stream);
  gstgiobasesrc_class->close_on_stop = TRUE;
}

static void
gst_gio_src_init (GstGioSrc * src)
{
}

static void
gst_gio_src_finalize (GObject * object)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  if (src->file) {
    g_object_unref (src->file);
    src->file = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:{
      const gchar *uri = NULL;

      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED) {
        GST_WARNING
            ("Setting a new location or GFile not supported in PLAYING or PAUSED state");
        break;
      }

      GST_OBJECT_LOCK (GST_OBJECT (src));
      if (src->file)
        g_object_unref (src->file);

      uri = g_value_get_string (value);

      if (uri) {
        src->file = g_file_new_for_uri (uri);

        if (!src->file) {
          GST_ERROR ("Could not create GFile for URI '%s'", uri);
        }
      } else {
        src->file = NULL;
      }
      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    }
    case PROP_FILE:
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED) {
        GST_WARNING
            ("Setting a new location or GFile not supported in PLAYING or PAUSED state");
        break;
      }

      GST_OBJECT_LOCK (GST_OBJECT (src));
      if (src->file)
        g_object_unref (src->file);

      src->file = g_value_dup_object (value);

      GST_OBJECT_UNLOCK (GST_OBJECT (src));
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
    case PROP_LOCATION:{
      gchar *uri;

      GST_OBJECT_LOCK (GST_OBJECT (src));
      if (src->file) {
        uri = g_file_get_uri (src->file);
        g_value_set_string (value, uri);
        g_free (uri);
      } else {
        g_value_set_string (value, NULL);
      }
      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    }
    case PROP_FILE:
      GST_OBJECT_LOCK (GST_OBJECT (src));
      g_value_set_object (value, src->file);
      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gio_src_query (GstBaseSrc * base_src, GstQuery * query)
{
  gboolean res;
  GstGioSrc *src = GST_GIO_SRC (base_src);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SCHEDULING:
    {
      gchar *scheme;
      GstSchedulingFlags flags;

      flags = 0;
      if (src->file == NULL)
        goto forward_parent;

      scheme = g_file_get_uri_scheme (src->file);
      if (scheme == NULL)
        goto forward_parent;

      if (strcmp (scheme, "file") == 0) {
        GST_LOG_OBJECT (src, "local URI, assuming random access is possible");
        flags |= GST_SCHEDULING_FLAG_SEEKABLE;
      } else if (strcmp (scheme, "http") == 0 || strcmp (scheme, "https") == 0) {
        GST_LOG_OBJECT (src, "blacklisted protocol '%s', "
            "no random access possible", scheme);
      } else {
        GST_LOG_OBJECT (src, "unhandled protocol '%s', asking parent", scheme);
        goto forward_parent;
      }
      g_free (scheme);

      gst_query_set_scheduling (query, flags, 1, -1, 0);
      gst_query_add_scheduling_mode (query, GST_PAD_MODE_PUSH);
      if (flags & GST_SCHEDULING_FLAG_SEEKABLE)
        gst_query_add_scheduling_mode (query, GST_PAD_MODE_PULL);

      res = TRUE;
      break;
    }
    default:
    forward_parent:
      res = GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS,
          query, (base_src, query), FALSE);
      break;
  }

  return res;
}

static GInputStream *
gst_gio_src_get_stream (GstGioBaseSrc * bsrc)
{
  GstGioSrc *src = GST_GIO_SRC (bsrc);
  GError *err = NULL;
  GInputStream *stream;
  GCancellable *cancel = bsrc->cancel;
  gchar *uri = NULL;

  if (src->file == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("No location or GFile given"));
    return NULL;
  }

  uri = g_file_get_uri (src->file);
  if (!uri)
    uri = g_strdup ("(null)");

  stream = G_INPUT_STREAM (g_file_read (src->file, cancel, &err));

  if (stream == NULL && !gst_gio_error (src, "g_file_read", &err, NULL)) {
    if (GST_GIO_ERROR_MATCHES (err, NOT_FOUND)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open location %s for reading: %s", uri, err->message));
    } else if (GST_GIO_ERROR_MATCHES (err, NOT_MOUNTED)) {
      gst_element_post_message (GST_ELEMENT_CAST (src),
          gst_message_new_element (GST_OBJECT_CAST (src),
              gst_structure_new ("not-mounted", "file", G_TYPE_FILE, src->file,
                  "uri", G_TYPE_STRING, uri, NULL)));

      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Location %s not mounted: %s", uri, err->message));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open location %s for reading: %s", uri, err->message));
    }

    g_free (uri);
    g_clear_error (&err);
    return NULL;
  } else if (stream == NULL) {
    g_free (uri);
    return NULL;
  }

  GST_DEBUG_OBJECT (src, "opened location %s", uri);
  g_free (uri);

  return stream;
}
