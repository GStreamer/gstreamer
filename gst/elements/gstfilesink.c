/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilesink.c: 
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

#include "../gst-i18n-lib.h"

#include <gst/gst.h>
#include <errno.h>
#include "gstfilesink.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


GST_DEBUG_CATEGORY_STATIC (gst_filesink_debug);
#define GST_CAT_DEFAULT gst_filesink_debug

GstElementDetails gst_filesink_details = GST_ELEMENT_DETAILS (
  "File Sink",
  "Sink/File",
  "Write stream to a file",
  "Thomas <thomas@apestaart.org>"
);


/* FileSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION
};

GST_PAD_QUERY_TYPE_FUNCTION (gst_filesink_get_query_types,
  GST_QUERY_TOTAL,
  GST_QUERY_POSITION
)

GST_PAD_FORMATS_FUNCTION (gst_filesink_get_formats,
  GST_FORMAT_BYTES
)


static void	gst_filesink_dispose		(GObject *object);

static void	gst_filesink_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_filesink_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static gboolean gst_filesink_open_file 		(GstFileSink *sink);
static void 	gst_filesink_close_file 	(GstFileSink *sink);

static gboolean gst_filesink_handle_event       (GstPad *pad, GstEvent *event);
static gboolean	gst_filesink_pad_query		(GstPad *pad, GstQueryType type,
						 GstFormat *format, gint64 *value);
static void	gst_filesink_chain		(GstPad *pad,GstData *_data);

static void	gst_filesink_uri_handler_init	(gpointer g_iface, gpointer iface_data);
  
static GstElementStateReturn gst_filesink_change_state (GstElement *element);

static guint gst_filesink_signals[LAST_SIGNAL] = { 0 };

static void
_do_init (GType filesink_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_filesink_uri_handler_init,
    NULL,
    NULL
  };
  g_type_add_interface_static (filesink_type, GST_TYPE_URI_HANDLER, &urihandler_info);
  GST_DEBUG_CATEGORY_INIT (gst_filesink_debug, "filesink", 0, "filesink element");
}

GST_BOILERPLATE_FULL (GstFileSink, gst_filesink, GstElement, GST_TYPE_ELEMENT, _do_init);
    

static void
gst_filesink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gstelement_class->change_state = gst_filesink_change_state;
  gst_element_class_set_details (gstelement_class, &gst_filesink_details);
}
static void
gst_filesink_class_init (GstFileSinkClass *klass) 
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);


  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION,
    g_param_spec_string ("location", "File Location", "Location of the file to write",
                         NULL, G_PARAM_READWRITE));

  gst_filesink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstFileSinkClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->set_property = gst_filesink_set_property;
  gobject_class->get_property = gst_filesink_get_property;
  gobject_class->dispose      = gst_filesink_dispose;
}
static void 
gst_filesink_init (GstFileSink *filesink) 
{
  GstPad *pad;

  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (filesink), pad);
  gst_pad_set_chain_function (pad, gst_filesink_chain);

  GST_FLAG_SET (GST_ELEMENT(filesink), GST_ELEMENT_EVENT_AWARE);

  gst_pad_set_query_function (pad, gst_filesink_pad_query);
  gst_pad_set_query_type_function (pad, gst_filesink_get_query_types);
  gst_pad_set_formats_function (pad, gst_filesink_get_formats);

  filesink->filename = NULL;
  filesink->file = NULL;
}
static void
gst_filesink_dispose (GObject *object)
{
  GstFileSink *sink = GST_FILESINK (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);
  
  g_free (sink->uri);
  sink->uri = NULL;
  g_free (sink->filename);
  sink->filename = NULL;
}
static gboolean
gst_filesink_set_location (GstFileSink *sink, const gchar *location)
{
  /* the element must be stopped or paused in order to do this */
  if (GST_STATE (sink) > GST_STATE_PAUSED)
    return FALSE;
  if (GST_STATE (sink) == GST_STATE_PAUSED &&
      GST_FLAG_IS_SET (sink, GST_FILESINK_OPEN))
    return FALSE;

  g_free (sink->filename);
  g_free (sink->uri);
  if (location != NULL) {
    sink->filename = g_strdup (location);
    sink->uri = gst_uri_construct ("file", location);
  } else {
    sink->filename = NULL;
    sink->uri = NULL;
  }
  
  if (GST_STATE (sink) == GST_STATE_PAUSED)
    gst_filesink_open_file (sink);

  return TRUE;
}
static void
gst_filesink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFileSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_FILESINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_filesink_set_location (sink, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void   
gst_filesink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFileSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FILESINK (object));
 
  sink = GST_FILESINK (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_filesink_open_file (GstFileSink *sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_FILESINK_OPEN), FALSE);

  /* open the file */
  if (!sink->filename)
  {
    gst_element_error (sink, RESOURCE, NOT_FOUND,
		         (_("No filename specified")), NULL);
    return FALSE;
  }

  sink->file = fopen (sink->filename, "w");
  if (sink->file == NULL) {
    gst_element_error (sink, RESOURCE, OPEN_WRITE,
                         (_("Could not open file \"%s\" for writing"), sink->filename),
                         GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_FILESINK_OPEN);

  sink->data_written = 0;

  return TRUE;
}

static void
gst_filesink_close_file (GstFileSink *sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_FILESINK_OPEN));

  if (fclose (sink->file) != 0)
  {
    gst_element_error (sink, RESOURCE, CLOSE,
		       (_("Error closing file \"%s\""), sink->filename),
                       GST_ERROR_SYSTEM);
  }
  else {
    GST_FLAG_UNSET (sink, GST_FILESINK_OPEN);
  }
}

static gboolean
gst_filesink_pad_query (GstPad *pad, GstQueryType type,
		        GstFormat *format, gint64 *value)
{
  GstFileSink *sink = GST_FILESINK (GST_PAD_PARENT (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
	case GST_FORMAT_BYTES:
          if (GST_FLAG_IS_SET (GST_ELEMENT(sink), GST_FILESINK_OPEN)) {
            *value = sink->data_written; /* FIXME - doesn't the kernel provide
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
          if (GST_FLAG_IS_SET (GST_ELEMENT(sink), GST_FILESINK_OPEN)) {
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
gst_filesink_handle_event (GstPad *pad, GstEvent *event)
{
  GstEventType type;
  GstFileSink *filesink;

  filesink = GST_FILESINK (gst_pad_get_parent (pad));

  g_return_val_if_fail (GST_FLAG_IS_SET (filesink, GST_FILESINK_OPEN),
		        FALSE);

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      g_return_val_if_fail (GST_EVENT_SEEK_FORMAT (event) == GST_FORMAT_BYTES,
			    FALSE);

      if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH)
        if (fflush (filesink->file))
          gst_element_error (filesink, RESOURCE, WRITE,
			     (_("Error while writing to file \"%s\""), filesink->filename),
			     GST_ERROR_SYSTEM);

      switch (GST_EVENT_SEEK_METHOD(event))
      {
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
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 offset;
      
      if (gst_event_discont_get_value (event, GST_FORMAT_BYTES, &offset))
        fseek (filesink->file, offset, SEEK_SET);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH:
      if (fflush (filesink->file)) {
          gst_element_error (filesink, RESOURCE, WRITE,
			     (_("Error while writing to file \"%s\""), filesink->filename),
			     GST_ERROR_SYSTEM);
      }
      break;
    case GST_EVENT_EOS:
      gst_filesink_close_file (filesink);
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
gst_filesink_chain (GstPad *pad, GstData *_data) 
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstFileSink *filesink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filesink = GST_FILESINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT(buf))
  {
    gst_filesink_handle_event(pad, GST_EVENT(buf));
    return;
  }

  if (GST_FLAG_IS_SET (filesink, GST_FILESINK_OPEN))
  {
    guint bytes_written = 0, back_pending = 0;
    if (ftell(filesink->file) < filesink->data_written)
      back_pending = filesink->data_written - ftell(filesink->file);
    while (bytes_written < GST_BUFFER_SIZE (buf)) {
      size_t wrote = fwrite (GST_BUFFER_DATA (buf) + bytes_written, 1,
			     GST_BUFFER_SIZE (buf) - bytes_written,
			     filesink->file);
      if (wrote <= 0) {
        gst_element_error (filesink, RESOURCE, WRITE,
			     (_("Error while writing to file \"%s\""), filesink->filename),
			      ("Only %d of %d bytes written: %s",
			       bytes_written, GST_BUFFER_SIZE (buf),
			       strerror (errno)));
	break;
      }
      bytes_written += wrote;
    }

    filesink->data_written += bytes_written - back_pending;
  }

  gst_buffer_unref (buf);

  g_signal_emit (G_OBJECT (filesink),
                 gst_filesink_signals[SIGNAL_HANDOFF], 0,
	         filesink);
}

static GstElementStateReturn
gst_filesink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_FILESINK (element), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_FILESINK_OPEN))
        gst_filesink_close_file (GST_FILESINK (element));
      break;

    case GST_STATE_READY_TO_PAUSED:
      if (!GST_FLAG_IS_SET (element, GST_FILESINK_OPEN)) {
        if (!gst_filesink_open_file (GST_FILESINK (element)))
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
gst_filesink_uri_get_type (void)
{
  return GST_URI_SINK;
}
static gchar **
gst_filesink_uri_get_protocols(void)
{
  static gchar *protocols[] = {"file", NULL};
  return protocols;
}
static const gchar *
gst_filesink_uri_get_uri (GstURIHandler *handler)
{
  GstFileSink *sink = GST_FILESINK (handler);
  
  return sink->uri;
}
static gboolean
gst_filesink_uri_set_uri (GstURIHandler *handler, const gchar *uri)
{
  gchar *protocol, *location;
  gboolean ret;
  GstFileSink *sink = GST_FILESINK (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "file") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  location = gst_uri_get_location (uri);
  ret = gst_filesink_set_location (sink, location);
  g_free (location);

  return ret;
}

static void
gst_filesink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_filesink_uri_get_type;
  iface->get_protocols = gst_filesink_uri_get_protocols;
  iface->get_uri = gst_filesink_uri_get_uri;
  iface->set_uri = gst_filesink_uri_set_uri;
}
