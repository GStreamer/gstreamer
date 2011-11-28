/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2003 Colin Walters <walters@verbum.org>
 *                    2005 Tim-Philipp Müller <tim centricular net>
 *
 * gstgnomevfssink.c: 
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
 * SECTION:element-gnomevfssink
 * @see_also: #GstFileSink, #GstGnomeVFSSrc
 *
 * This plugin writes incoming data to a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GnomeVFS library. Common protocols are 'file', 'ftp', or 'smb'.
 *
 * Applications can connect to the #GstGnomeVFSSink::allow-overwrite signal to
 * receive a callback when an existing file will be overwritten. The return
 * value of the signal will determine if gnomevfssink will overwrite the
 * resource or abort with an error.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v filesrc location=input.xyz ! gnomevfssink location=file:///home/joe/out.xyz
 * ]| The above pipeline will simply copy a local file. Instead of gnomevfssink,
 * we could just as well have used the filesink element here.
 * |[
 * gst-launch -v filesrc location=foo.mp3 ! mad ! flacenc ! gnomevfssink location=smb://othercomputer/foo.flac
 * ]| The above pipeline will re-encode an mp3 file into FLAC format and store
 * it on a remote host using the Samba protocol.
 * </refsect2>
 *
 * Last reviewed on 2006-02-28 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgnomevfssink.h"

#include "gst/gst-i18n-plugin.h"

#include <gst/gst.h>
#include <libgnomevfs/gnome-vfs.h>
#include <string.h>
#include <errno.h>

enum
{
  SIGNAL_ERASE_ASK,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_URI,
  ARG_HANDLE
};

static void gst_gnome_vfs_sink_finalize (GObject * obj);

static void gst_gnome_vfs_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_gnome_vfs_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gnome_vfs_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gnome_vfs_sink_open_file (GstGnomeVFSSink * sink);
static void gst_gnome_vfs_sink_close_file (GstGnomeVFSSink * sink);
static gboolean gst_gnome_vfs_sink_start (GstBaseSink * basesink);
static gboolean gst_gnome_vfs_sink_stop (GstBaseSink * basesink);
static GstFlowReturn gst_gnome_vfs_sink_render (GstBaseSink * basesink,
    GstBuffer * buffer);
static gboolean gst_gnome_vfs_sink_handle_event (GstBaseSink * basesink,
    GstEvent * event);
static gboolean gst_gnome_vfs_sink_query (GstPad * pad, GstQuery * query);

static guint gst_gnome_vfs_sink_signals[LAST_SIGNAL];   /* all 0 */

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_gnome_vfs_sink_debug);
#define GST_CAT_DEFAULT gst_gnome_vfs_sink_debug

static void
gst_gnome_vfs_sink_do_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_gnome_vfs_sink_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_gnome_vfs_sink_debug, "gnomevfssink", 0,
      "Gnome VFS sink element");
}

GST_BOILERPLATE_FULL (GstGnomeVFSSink, gst_gnome_vfs_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, gst_gnome_vfs_sink_do_init);

static void
gst_gnome_vfs_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_details_simple (element_class,
      "GnomeVFS Sink", "Sink/File",
      "Write a stream to a GnomeVFS URI", "Bastien Nocera <hadess@hadess.net>");
}

static gboolean
_gst_boolean_allow_overwrite_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean allow_overwrite;

  allow_overwrite = g_value_get_boolean (handler_return);
  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boolean (return_accu, allow_overwrite);

  /* stop emission if signal doesn't allow overwriting */
  return allow_overwrite;
}

static void
gst_gnome_vfs_sink_class_init (GstGnomeVFSSinkClass * klass)
{
  GstBaseSinkClass *basesink_class;
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  basesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_gnome_vfs_sink_set_property;
  gobject_class->get_property = gst_gnome_vfs_sink_get_property;
  gobject_class->finalize = gst_gnome_vfs_sink_finalize;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to write", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_URI,
      g_param_spec_boxed ("uri", "GnomeVFSURI", "URI for GnomeVFS",
          GST_TYPE_GNOME_VFS_URI, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_HANDLE,
      g_param_spec_boxed ("handle", "GnomeVFSHandle", "Handle for GnomeVFS",
          GST_TYPE_GNOME_VFS_HANDLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstGnomeVFSSink::allow-overwrite
   * @sink: the object which received the signal
   * @uri: the URI to be overwritten
   *
   * This signal is fired when gnomevfssink is about to overwrite an
   * existing resource. The application can connect to this signal and ask
   * the user if the resource may be overwritten. 
   *
   * Returns: A boolean indicating that the resource may be overwritten.
   */
  gst_gnome_vfs_sink_signals[SIGNAL_ERASE_ASK] =
      g_signal_new ("allow-overwrite", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_CLEANUP, G_STRUCT_OFFSET (GstGnomeVFSSinkClass, erase_ask),
      _gst_boolean_allow_overwrite_accumulator, NULL,
      gst_marshal_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1, GST_TYPE_GNOME_VFS_URI);

  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_gnome_vfs_sink_stop);
  basesink_class->start = GST_DEBUG_FUNCPTR (gst_gnome_vfs_sink_start);
  basesink_class->event = GST_DEBUG_FUNCPTR (gst_gnome_vfs_sink_handle_event);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_gnome_vfs_sink_render);
  basesink_class->get_times = NULL;
}

static void
gst_gnome_vfs_sink_finalize (GObject * obj)
{
  GstGnomeVFSSink *sink = GST_GNOME_VFS_SINK (obj);

  if (sink->uri) {
    gnome_vfs_uri_unref (sink->uri);
    sink->uri = NULL;
  }

  if (sink->uri_name) {
    g_free (sink->uri_name);
    sink->uri_name = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_gnome_vfs_sink_init (GstGnomeVFSSink * sink, GstGnomeVFSSinkClass * klass)
{
  gst_pad_set_query_function (GST_BASE_SINK_PAD (sink),
      GST_DEBUG_FUNCPTR (gst_gnome_vfs_sink_query));

  sink->uri = NULL;
  sink->uri_name = NULL;
  sink->handle = NULL;
  sink->own_handle = FALSE;
  sink->current_pos = 0;

  GST_BASE_SINK (sink)->sync = FALSE;
}

static void
gst_gnome_vfs_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSink *sink;
  GstState cur_state;

  sink = GST_GNOME_VFS_SINK (object);

  gst_element_get_state (GST_ELEMENT (sink), &cur_state, NULL, 0);

  if (cur_state == GST_STATE_PLAYING || cur_state == GST_STATE_PAUSED) {
    GST_WARNING_OBJECT (sink, "cannot set property when PAUSED or PLAYING");
    return;
  }

  GST_OBJECT_LOCK (sink);

  switch (prop_id) {
    case ARG_LOCATION:{
      const gchar *new_location;

      if (sink->uri) {
        gnome_vfs_uri_unref (sink->uri);
        sink->uri = NULL;
      }
      if (sink->uri_name) {
        g_free (sink->uri_name);
        sink->uri_name = NULL;
      }

      new_location = g_value_get_string (value);
      if (new_location) {
        sink->uri_name = gst_gnome_vfs_location_to_uri_string (new_location);
        sink->uri = gnome_vfs_uri_new (sink->uri_name);
      }
      break;
    }
    case ARG_URI:{
      if (sink->uri) {
        gnome_vfs_uri_unref (sink->uri);
        sink->uri = NULL;
      }
      if (sink->uri_name) {
        g_free (sink->uri_name);
        sink->uri_name = NULL;
      }
      if (g_value_get_boxed (value)) {
        sink->uri = (GnomeVFSURI *) g_value_dup_boxed (value);
        sink->uri_name = gnome_vfs_uri_to_string (sink->uri, 0);
      }
      break;
    }
    case ARG_HANDLE:{
      if (sink->uri) {
        gnome_vfs_uri_unref (sink->uri);
        sink->uri = NULL;
      }
      if (sink->uri_name) {
        g_free (sink->uri_name);
        sink->uri_name = NULL;
      }
      sink->handle = g_value_get_boxed (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (sink);
}

static void
gst_gnome_vfs_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSink *sink;

  sink = GST_GNOME_VFS_SINK (object);

  GST_OBJECT_LOCK (sink);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->uri_name);
      break;
    case ARG_URI:
      g_value_set_boxed (value, sink->uri);
      break;
    case ARG_HANDLE:
      g_value_set_boxed (value, sink->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (sink);
}

static gboolean
gst_gnome_vfs_sink_open_file (GstGnomeVFSSink * sink)
{
  GnomeVFSResult result;

  if (sink->uri) {
    /* open the file, all permissions, umask will apply */
    result = gnome_vfs_create_uri (&(sink->handle), sink->uri,
        GNOME_VFS_OPEN_WRITE, TRUE,
        GNOME_VFS_PERM_USER_READ | GNOME_VFS_PERM_USER_WRITE |
        GNOME_VFS_PERM_GROUP_READ | GNOME_VFS_PERM_GROUP_WRITE |
        GNOME_VFS_PERM_OTHER_READ | GNOME_VFS_PERM_OTHER_WRITE);

    /* if the file existed and the property says to ask, then ask! */
    if (result == GNOME_VFS_ERROR_FILE_EXISTS) {
      gboolean erase_anyway = FALSE;

      g_signal_emit (G_OBJECT (sink),
          gst_gnome_vfs_sink_signals[SIGNAL_ERASE_ASK], 0, sink->uri,
          &erase_anyway);
      if (erase_anyway) {
        result = gnome_vfs_create_uri (&(sink->handle), sink->uri,
            GNOME_VFS_OPEN_WRITE, FALSE,
            GNOME_VFS_PERM_USER_READ | GNOME_VFS_PERM_USER_WRITE |
            GNOME_VFS_PERM_GROUP_READ | GNOME_VFS_PERM_GROUP_WRITE |
            GNOME_VFS_PERM_OTHER_READ | GNOME_VFS_PERM_OTHER_WRITE);
      }
    }

    GST_DEBUG_OBJECT (sink, "open: %s", gnome_vfs_result_to_string (result));

    if (result != GNOME_VFS_OK) {
      gchar *filename = gnome_vfs_uri_to_string (sink->uri,
          GNOME_VFS_URI_HIDE_PASSWORD);

      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
          (_("Could not open vfs file \"%s\" for writing: %s."),
              filename, gnome_vfs_result_to_string (result)), GST_ERROR_SYSTEM);
      g_free (filename);
      return FALSE;
    }
    sink->own_handle = TRUE;
  } else if (!sink->handle) {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (_("No filename given")),
        (NULL));
    return FALSE;
  } else {
    sink->own_handle = FALSE;
  }

  sink->current_pos = 0;

  return TRUE;
}

static void
gst_gnome_vfs_sink_close_file (GstGnomeVFSSink * sink)
{
  GnomeVFSResult result;

  if (sink->own_handle) {
    /* close the file */
    result = gnome_vfs_close (sink->handle);

    if (result != GNOME_VFS_OK) {
      gchar *filename = gnome_vfs_uri_to_string (sink->uri,
          GNOME_VFS_URI_HIDE_PASSWORD);

      GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
          (_("Could not close vfs file \"%s\"."), filename), GST_ERROR_SYSTEM);
      g_free (filename);
    }

    sink->own_handle = FALSE;
    sink->handle = NULL;
  }
}

static gboolean
gst_gnome_vfs_sink_start (GstBaseSink * basesink)
{
  gboolean ret;

  ret = gst_gnome_vfs_sink_open_file (GST_GNOME_VFS_SINK (basesink));

  return ret;
}

static gboolean
gst_gnome_vfs_sink_stop (GstBaseSink * basesink)
{
  GST_DEBUG_OBJECT (basesink, "closing ...");
  gst_gnome_vfs_sink_close_file (GST_GNOME_VFS_SINK (basesink));
  return TRUE;
}

static gboolean
gst_gnome_vfs_sink_handle_event (GstBaseSink * basesink, GstEvent * event)
{
  GstGnomeVFSSink *sink;
  gboolean ret = TRUE;

  sink = GST_GNOME_VFS_SINK (basesink);

  GST_DEBUG_OBJECT (sink, "processing %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GnomeVFSResult res;
      GstFormat format;
      gint64 offset;

      gst_event_parse_new_segment (event, NULL, NULL, &format, &offset,
          NULL, NULL);

      if (format != GST_FORMAT_BYTES) {
        GST_WARNING_OBJECT (sink, "ignored NEWSEGMENT event in %s format",
            gst_format_get_name (format));
        break;
      }

      GST_LOG_OBJECT (sink, "seeking to offset %" G_GINT64_FORMAT, offset);
      res = gnome_vfs_seek (sink->handle, GNOME_VFS_SEEK_START, offset);

      if (res != GNOME_VFS_OK) {
        GST_ERROR_OBJECT (sink, "Failed to seek to offset %"
            G_GINT64_FORMAT ": %s", offset, gnome_vfs_result_to_string (res));
        ret = FALSE;
      } else {
        sink->current_pos = offset;
      }

      break;
    }

    case GST_EVENT_FLUSH_START:
    case GST_EVENT_EOS:{
      /* No need to flush with GnomeVfs */
      break;
    }
    default:
      break;
  }

  return ret;
}

static gboolean
gst_gnome_vfs_sink_query (GstPad * pad, GstQuery * query)
{
  GstGnomeVFSSink *sink;
  GstFormat format;

  sink = GST_GNOME_VFS_SINK (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
          gst_query_set_position (query, GST_FORMAT_BYTES, sink->current_pos);
          return TRUE;
        default:
          return FALSE;
      }

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_BYTES);
      return TRUE;

    case GST_QUERY_URI:
      gst_query_set_uri (query, sink->uri_name);
      return TRUE;

    default:
      return gst_pad_query_default (pad, query);
  }
}

static GstFlowReturn
gst_gnome_vfs_sink_render (GstBaseSink * basesink, GstBuffer * buf)
{
  GnomeVFSFileSize written, cur_pos;
  GstGnomeVFSSink *sink;
  GnomeVFSResult result;
  GstFlowReturn ret;

  sink = GST_GNOME_VFS_SINK (basesink);

  if (gnome_vfs_tell (sink->handle, &cur_pos) == GNOME_VFS_OK) {
    /* bring up to date with current position for proper reporting */
    sink->current_pos = cur_pos;
  }

  result = gnome_vfs_write (sink->handle, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf), &written);

  switch (result) {
    case GNOME_VFS_OK:{
      GST_DEBUG_OBJECT (sink, "wrote %" G_GINT64_FORMAT " bytes at %"
          G_GINT64_FORMAT, (gint64) written, (gint64) cur_pos);

      if (written < GST_BUFFER_SIZE (buf)) {
        /* FIXME: what to do here? (tpm) */
        g_warning ("%s: %d bytes should be written, only %"
            G_GUINT64_FORMAT " bytes written", G_STRLOC,
            GST_BUFFER_SIZE (buf), written);
      }

      sink->current_pos += GST_BUFFER_SIZE (buf);
      ret = GST_FLOW_OK;
      break;
    }
    case GNOME_VFS_ERROR_NO_SPACE:{
      /* TODO: emit signal/send msg on out-of-diskspace and
       * handle this gracefully (see open bug) (tpm) */
      GST_ELEMENT_ERROR (sink, RESOURCE, NO_SPACE_LEFT, (NULL),
          ("bufsize=%u, written=%u", GST_BUFFER_SIZE (buf), (guint) written));
      ret = GST_FLOW_ERROR;
      break;
    }
    default:{
      gchar *filename = gnome_vfs_uri_to_string (sink->uri,
          GNOME_VFS_URI_HIDE_PASSWORD);

      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
          (_("Error while writing to file \"%s\"."), filename),
          ("%s, bufsize=%u, written=%u", gnome_vfs_result_to_string (result),
              GST_BUFFER_SIZE (buf), (guint) written));

      g_free (filename);
      ret = GST_FLOW_ERROR;
      break;
    }
  }

  return ret;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_gnome_vfs_sink_uri_get_type (void)
{
  return GST_URI_SINK;
}

static gchar **
gst_gnome_vfs_sink_uri_get_protocols (void)
{
  return gst_gnomevfs_get_supported_uris ();
}

static const gchar *
gst_gnome_vfs_sink_uri_get_uri (GstURIHandler * handler)
{
  GstGnomeVFSSink *sink = GST_GNOME_VFS_SINK (handler);

  return sink->uri_name;
}

static gboolean
gst_gnome_vfs_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstGnomeVFSSink *sink = GST_GNOME_VFS_SINK (handler);
  GstState cur_state;

  gst_element_get_state (GST_ELEMENT (sink), &cur_state, NULL, 0);

  if (cur_state == GST_STATE_PLAYING || cur_state == GST_STATE_PAUSED) {
    GST_WARNING_OBJECT (sink, "cannot set uri when PAUSED or PLAYING");
    return FALSE;
  }

  g_object_set (sink, "location", uri, NULL);

  return TRUE;
}

static void
gst_gnome_vfs_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_gnome_vfs_sink_uri_get_type;
  iface->get_protocols = gst_gnome_vfs_sink_uri_get_protocols;
  iface->get_uri = gst_gnome_vfs_sink_uri_get_uri;
  iface->set_uri = gst_gnome_vfs_sink_uri_set_uri;
}
