/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstmultifilesink.c: 
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
#  include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include <gst/gst.h>
#include <errno.h>
#include "gstmultifilesink.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


GST_DEBUG_CATEGORY_STATIC (gst_multifilesink_debug);
#define GST_CAT_DEFAULT gst_multifilesink_debug

GstElementDetails gst_multifilesink_details =
GST_ELEMENT_DETAILS ("Multiple File Sink",
    "Sink/File",
    "Write stream to multiple files sequentially",
    "Zaheer Abbas Merali <zaheerabbas at merali dot org>");


/* FileSink signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  SIGNAL_NEWFILE,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION
};

static const GstFormat *
gst_multifilesink_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0,
  };

  return formats;
}

static const GstQueryType *
gst_multifilesink_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static void gst_multifilesink_dispose (GObject * object);

static void gst_multifilesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multifilesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_multifilesink_open_file (GstMultiFileSink * sink);
static void gst_multifilesink_close_file (GstMultiFileSink * sink);

static gboolean gst_multifilesink_handle_event (GstPad * pad, GstEvent * event);
static gboolean gst_multifilesink_pad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);
static void gst_multifilesink_chain (GstPad * pad, GstData * _data);

static void gst_multifilesink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static GstElementStateReturn gst_multifilesink_change_state (GstElement *
    element);

static guint gst_multifilesink_signals[LAST_SIGNAL] = { 0 };

static void
_do_init (GType filesink_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_multifilesink_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (filesink_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
  GST_DEBUG_CATEGORY_INIT (gst_multifilesink_debug, "multifilesink", 0,
      "multifilesink element");
}

GST_BOILERPLATE_FULL (GstMultiFileSink, gst_multifilesink, GstElement,
    GST_TYPE_ELEMENT, _do_init);


static void
gst_multifilesink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gstelement_class->change_state = gst_multifilesink_change_state;
  gst_element_class_set_details (gstelement_class, &gst_multifilesink_details);
}
static void
gst_multifilesink_class_init (GstMultiFileSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);


  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to write", NULL, G_PARAM_READWRITE));

  gst_multifilesink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFileSinkClass, handoff), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gst_multifilesink_signals[SIGNAL_NEWFILE] =
      g_signal_new ("newfile", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFileSinkClass, newfile), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->set_property = gst_multifilesink_set_property;
  gobject_class->get_property = gst_multifilesink_get_property;
  gobject_class->dispose = gst_multifilesink_dispose;
}
static void
gst_multifilesink_init (GstMultiFileSink * filesink)
{
  GstPad *pad;

  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (filesink), pad);
  gst_pad_set_chain_function (pad, gst_multifilesink_chain);

  GST_FLAG_SET (GST_ELEMENT (filesink), GST_ELEMENT_EVENT_AWARE);

  gst_pad_set_query_function (pad, gst_multifilesink_pad_query);
  gst_pad_set_query_type_function (pad, gst_multifilesink_get_query_types);
  gst_pad_set_formats_function (pad, gst_multifilesink_get_formats);

  filesink->filename = NULL;
  filesink->file = NULL;
  filesink->curfilename = NULL;
  filesink->curfileindex = 0;
  filesink->numfiles = 0;
}
static void
gst_multifilesink_dispose (GObject * object)
{
  GstMultiFileSink *sink = GST_MULTIFILESINK (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (sink->uri);
  sink->uri = NULL;
  g_free (sink->filename);
  sink->filename = NULL;
  if (sink->curfilename)
    g_free (sink->curfilename);
  sink->curfilename = NULL;
}

static gboolean
gst_multifilesink_set_location (GstMultiFileSink * sink, const gchar * location)
{
  GST_DEBUG ("location set is: %s", location);
  /* the element must be stopped or paused in order to do this or in newfile 
     signal */
  if (GST_STATE (sink) > GST_STATE_PAUSED &&
      !GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_NEWFILE))
    return FALSE;
  if (GST_STATE (sink) == GST_STATE_PAUSED &&
      (GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_OPEN) ||
          !GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_NEWFILE)))

    return FALSE;

  g_free (sink->filename);
  g_free (sink->uri);
  if (location != NULL) {
    sink->filename = g_strdup (location);
    sink->curfileindex = 0;
    sink->curfilename = g_strdup_printf (location, sink->curfileindex);
    sink->uri = gst_uri_construct ("file", sink->curfilename);
  } else {
    sink->filename = NULL;
    sink->uri = NULL;
  }

  if (GST_STATE (sink) == GST_STATE_PAUSED &&
      !GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_NEWFILE))
    gst_multifilesink_open_file (sink);

  return TRUE;
}
static void
gst_multifilesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_MULTIFILESINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (!gst_multifilesink_set_location (sink, g_value_get_string (value)))
        GST_DEBUG ("location not set properly");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multifilesink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMultiFileSink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULTIFILESINK (object));

  sink = GST_MULTIFILESINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->curfilename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_multifilesink_open_file (GstMultiFileSink * sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_OPEN), FALSE);

  /* open the file */
  if (sink->curfilename == NULL || sink->curfilename[0] == '\0') {
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        (_("No file name specified for writing.")), (NULL));
    return FALSE;
  }

  sink->file = fopen (sink->curfilename, "wb");
  if (sink->file == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        (_("Could not open file \"%s\" for writing."), sink->curfilename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_MULTIFILESINK_OPEN);

  sink->data_written = 0;
  sink->curfileindex++;

  return TRUE;
}

static void
gst_multifilesink_close_file (GstMultiFileSink * sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_OPEN));

  if (fclose (sink->file) != 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
        (_("Error closing file \"%s\"."), sink->curfilename), GST_ERROR_SYSTEM);
  } else {
    GST_FLAG_UNSET (sink, GST_MULTIFILESINK_OPEN);
  }
}

static gboolean
gst_multifilesink_next_file (GstMultiFileSink * sink)
{
  GST_DEBUG ("next file");
  g_return_val_if_fail (GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_OPEN), FALSE);

  if (fclose (sink->file) != 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
        (_("Error closing file \"%s\"."), sink->curfilename), GST_ERROR_SYSTEM);
  } else {
    GST_FLAG_UNSET (sink, GST_MULTIFILESINK_OPEN);
  }

  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_MULTIFILESINK_OPEN), FALSE);
  if (sink->curfilename)
    g_free (sink->curfilename);
  if (sink->uri)
    g_free (sink->uri);
  sink->curfilename = g_strdup_printf (sink->filename, sink->curfileindex);
  sink->uri = gst_uri_construct ("file", sink->curfilename);
  GST_DEBUG ("Next file is: %s", sink->curfilename);
  /* open the file */
  if (sink->curfilename == NULL || sink->curfilename[0] == '\0') {
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
        (_("No file name specified for writing.")), (NULL));
    return FALSE;
  }

  sink->file = fopen (sink->curfilename, "wb");
  if (sink->file == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
        (_("Could not open file \"%s\" for writing."), sink->curfilename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_MULTIFILESINK_OPEN);

  sink->data_written = 0;
  sink->curfileindex++;

  return TRUE;
}

static gboolean
gst_multifilesink_pad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstMultiFileSink *sink = GST_MULTIFILESINK (GST_PAD_PARENT (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_BYTES:
          if (GST_FLAG_IS_SET (GST_ELEMENT (sink), GST_MULTIFILESINK_OPEN)) {
            *value = sink->data_written;        /* FIXME - doesn't the kernel provide
                                                   such a function? */
            break;
          }
        default:
          return FALSE;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_BYTES:
          if (GST_FLAG_IS_SET (GST_ELEMENT (sink), GST_MULTIFILESINK_OPEN)) {
            *value = ftell (sink->file);
            break;
          }
        default:
          return FALSE;
      }
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

/* handle events (search) */
static gboolean
gst_multifilesink_handle_event (GstPad * pad, GstEvent * event)
{
  GstEventType type;
  GstMultiFileSink *filesink;

  filesink = GST_MULTIFILESINK (gst_pad_get_parent (pad));


  if (!(GST_FLAG_IS_SET (filesink, GST_MULTIFILESINK_OPEN))) {
    gst_event_unref (event);
    return FALSE;
  }


  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_BYTES) {
        gst_event_unref (event);
        return FALSE;
      }

      if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH) {
        if (fflush (filesink->file)) {
          gst_event_unref (event);
          GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
              (_("Error while writing to file \"%s\"."), filesink->filename),
              GST_ERROR_SYSTEM);
        }
      }

      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          fseek (filesink->file, GST_EVENT_SEEK_OFFSET (event), SEEK_SET);
          break;
        case GST_SEEK_METHOD_CUR:
          fseek (filesink->file, GST_EVENT_SEEK_OFFSET (event), SEEK_CUR);
          break;
        case GST_SEEK_METHOD_END:
          fseek (filesink->file, GST_EVENT_SEEK_OFFSET (event), SEEK_END);
          break;
        default:
          g_warning ("unknown seek method!");
          break;
      }
      gst_event_unref (event);
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 offset;

      if (GST_EVENT_DISCONT_NEW_MEDIA (event)) {
        /* do not create a new file on the first new media discont */
        if (filesink->numfiles > 0) {
          GST_FLAG_SET (filesink, GST_MULTIFILESINK_NEWFILE);
          g_signal_emit (G_OBJECT (filesink),
              gst_multifilesink_signals[SIGNAL_NEWFILE], 0);
          GST_FLAG_UNSET (filesink, GST_MULTIFILESINK_NEWFILE);
          if (!gst_multifilesink_next_file (filesink))
            GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
                (_("Error switching files to \"%s\"."),
                    filesink->curfilename), GST_ERROR_SYSTEM);
        }
        filesink->numfiles++;
        gst_event_unref (event);
        break;
      } else {

        if (gst_event_discont_get_value (event, GST_FORMAT_BYTES, &offset))
          fseek (filesink->file, offset, SEEK_SET);

        gst_event_unref (event);
        break;
      }
    }
    case GST_EVENT_FLUSH:
      if (fflush (filesink->file)) {
        gst_event_unref (event);
        GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
            (_("Error while writing to file \"%s\"."), filesink->curfilename),
            GST_ERROR_SYSTEM);
      }
      break;
    case GST_EVENT_EOS:
      gst_event_unref (event);
      gst_multifilesink_close_file (filesink);
      gst_element_set_eos (GST_ELEMENT (filesink));
      break;
    default:
      gst_pad_event_default (pad, event);
      break;
  }

  return TRUE;
}

/**
 * gst_filesink_chain:
 * @pad: the pad this filesink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and write to file if it's open
 */
static void
gst_multifilesink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMultiFileSink *filesink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filesink = GST_MULTIFILESINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    gst_multifilesink_handle_event (pad, GST_EVENT (buf));
    return;
  }

  if (GST_FLAG_IS_SET (filesink, GST_MULTIFILESINK_OPEN)) {
    guint bytes_written = 0, back_pending = 0;

    if (ftell (filesink->file) < filesink->data_written)
      back_pending = filesink->data_written - ftell (filesink->file);
    while (bytes_written < GST_BUFFER_SIZE (buf)) {
      size_t wrote = fwrite (GST_BUFFER_DATA (buf) + bytes_written, 1,
          GST_BUFFER_SIZE (buf) - bytes_written,
          filesink->file);

      if (wrote <= 0) {
        GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
            (_("Error while writing to file \"%s\"."), filesink->filename),
            ("Only %d of %d bytes written: %s",
                bytes_written, GST_BUFFER_SIZE (buf), strerror (errno)));
        break;
      }
      bytes_written += wrote;
    }

    filesink->data_written += bytes_written - back_pending;
  }

  gst_buffer_unref (buf);

  g_signal_emit (G_OBJECT (filesink),
      gst_multifilesink_signals[SIGNAL_HANDOFF], 0, filesink);
}

static GstElementStateReturn
gst_multifilesink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_MULTIFILESINK (element), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_MULTIFILESINK_OPEN))
        gst_multifilesink_close_file (GST_MULTIFILESINK (element));
      break;

    case GST_STATE_READY_TO_PAUSED:
      if (!GST_FLAG_IS_SET (element, GST_MULTIFILESINK_OPEN)) {
        if (!gst_multifilesink_open_file (GST_MULTIFILESINK (element)))
          return GST_STATE_FAILURE;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_multifilesink_uri_get_type (void)
{
  return GST_URI_SINK;
}
static gchar **
gst_multifilesink_uri_get_protocols (void)
{
  static gchar *protocols[] = { "file", NULL };

  return protocols;
}
static const gchar *
gst_multifilesink_uri_get_uri (GstURIHandler * handler)
{
  GstMultiFileSink *sink = GST_MULTIFILESINK (handler);

  return sink->uri;
}

static gboolean
gst_multifilesink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;
  gboolean ret;
  GstMultiFileSink *sink = GST_MULTIFILESINK (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "file") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  location = gst_uri_get_location (uri);
  ret = gst_multifilesink_set_location (sink, location);
  g_free (location);

  return ret;
}

static void
gst_multifilesink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_multifilesink_uri_get_type;
  iface->get_protocols = gst_multifilesink_uri_get_protocols;
  iface->get_uri = gst_multifilesink_uri_get_uri;
  iface->set_uri = gst_multifilesink_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "multifilesink", GST_RANK_NONE,
      GST_TYPE_MULTIFILESINK);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstmultifilesink",
    "multiple file sink (sequentially) after new media events",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
