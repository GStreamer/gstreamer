/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfdsink.c:
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
 * SECTION:element-fdsink
 * @see_also: #GstFdSrc
 *
 * Write data to a unix file descriptor.
 *
 * This element will synchronize on the clock before writing the data on the
 * socket. For file descriptors where this does not make sense (files, ...) the
 * #GstBaseSink:sync property can be used to disable synchronisation.
 *
 * Last reviewed on 2006-04-28 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../../gst/gst-i18n-lib.h"

#include <sys/types.h>

#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#endif

#include <sys/stat.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef _MSC_VER
#undef stat
#define stat _stat
#define fstat _fstat
#define S_ISREG(m)	(((m)&S_IFREG)==S_IFREG)
#endif
#include <errno.h>
#include <string.h>

#include "gstfdsink.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_fd_sink__debug);
#define GST_CAT_DEFAULT gst_fd_sink__debug


/* FdSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FD
};

static void gst_fd_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType gst_fd_sink_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_fd_sink_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (gst_fd_sink_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_fd_sink__debug, "fdsink", 0, "fdsink element");
}

GST_BOILERPLATE_FULL (GstFdSink, gst_fd_sink, GstBaseSink, GST_TYPE_BASE_SINK,
    _do_init);

static void gst_fd_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fd_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_fd_sink_dispose (GObject * obj);

static gboolean gst_fd_sink_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_fd_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static gboolean gst_fd_sink_start (GstBaseSink * basesink);
static gboolean gst_fd_sink_stop (GstBaseSink * basesink);
static gboolean gst_fd_sink_unlock (GstBaseSink * basesink);
static gboolean gst_fd_sink_unlock_stop (GstBaseSink * basesink);
static gboolean gst_fd_sink_event (GstBaseSink * sink, GstEvent * event);

static gboolean gst_fd_sink_do_seek (GstFdSink * fdsink, guint64 new_offset);

static void
gst_fd_sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Filedescriptor Sink",
      "Sink/File",
      "Write data to a file descriptor", "Erik Walthinsen <omega@cse.ogi.edu>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
}

static void
gst_fd_sink_class_init (GstFdSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_fd_sink_set_property;
  gobject_class->get_property = gst_fd_sink_get_property;
  gobject_class->dispose = gst_fd_sink_dispose;

  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_fd_sink_render);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_fd_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_fd_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_fd_sink_unlock);
  gstbasesink_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_fd_sink_unlock_stop);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_fd_sink_event);

  g_object_class_install_property (gobject_class, ARG_FD,
      g_param_spec_int ("fd", "fd", "An open file descriptor to write to",
          0, G_MAXINT, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_fd_sink_init (GstFdSink * fdsink, GstFdSinkClass * klass)
{
  GstPad *pad;

  pad = GST_BASE_SINK_PAD (fdsink);
  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_fd_sink_query));

  fdsink->fd = 1;
  fdsink->uri = g_strdup_printf ("fd://%d", fdsink->fd);
  fdsink->bytes_written = 0;
  fdsink->current_pos = 0;

  gst_base_sink_set_sync (GST_BASE_SINK (fdsink), FALSE);
}

static void
gst_fd_sink_dispose (GObject * obj)
{
  GstFdSink *fdsink = GST_FD_SINK (obj);

  g_free (fdsink->uri);
  fdsink->uri = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static gboolean
gst_fd_sink_query (GstPad * pad, GstQuery * query)
{
  GstFdSink *fdsink;
  GstFormat format;

  fdsink = GST_FD_SINK (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_BYTES:
          gst_query_set_position (query, GST_FORMAT_BYTES, fdsink->current_pos);
          return TRUE;
        default:
          return FALSE;
      }

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_BYTES);
      return TRUE;

    case GST_QUERY_URI:
      gst_query_set_uri (query, fdsink->uri);
      return TRUE;

    default:
      return gst_pad_query_default (pad, query);
  }
}

static GstFlowReturn
gst_fd_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstFdSink *fdsink;
  guint8 *data;
  guint size;
  gint written;

#ifndef HAVE_WIN32
  gint retval;
#endif

  fdsink = GST_FD_SINK (sink);

  g_return_val_if_fail (fdsink->fd >= 0, GST_FLOW_ERROR);

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

again:
#ifndef HAVE_WIN32
  do {
    GST_DEBUG_OBJECT (fdsink, "going into select, have %d bytes to write",
        size);
    retval = gst_poll_wait (fdsink->fdset, GST_CLOCK_TIME_NONE);
  } while (retval == -1 && (errno == EINTR || errno == EAGAIN));

  if (retval == -1) {
    if (errno == EBUSY)
      goto stopped;
    else
      goto select_error;
  }
#endif

  GST_DEBUG_OBJECT (fdsink, "writing %d bytes to file descriptor %d", size,
      fdsink->fd);

  written = write (fdsink->fd, data, size);

  /* check for errors */
  if (G_UNLIKELY (written < 0)) {
    /* try to write again on non-fatal errors */
    if (errno == EAGAIN || errno == EINTR)
      goto again;

    /* else go to our error handler */
    goto write_error;
  }

  /* all is fine when we get here */
  size -= written;
  data += written;
  fdsink->bytes_written += written;
  fdsink->current_pos += written;

  GST_DEBUG_OBJECT (fdsink, "wrote %d bytes, %d left", written, size);

  /* short write, select and try to write the remainder */
  if (G_UNLIKELY (size > 0))
    goto again;

  return GST_FLOW_OK;

#ifndef HAVE_WIN32
select_error:
  {
    GST_ELEMENT_ERROR (fdsink, RESOURCE, READ, (NULL),
        ("select on file descriptor: %s.", g_strerror (errno)));
    GST_DEBUG_OBJECT (fdsink, "Error during select");
    return GST_FLOW_ERROR;
  }
stopped:
  {
    GST_DEBUG_OBJECT (fdsink, "Select stopped");
    return GST_FLOW_WRONG_STATE;
  }
#endif

write_error:
  {
    switch (errno) {
      case ENOSPC:
        GST_ELEMENT_ERROR (fdsink, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
        break;
      default:{
        GST_ELEMENT_ERROR (fdsink, RESOURCE, WRITE, (NULL),
            ("Error while writing to file descriptor %d: %s",
                fdsink->fd, g_strerror (errno)));
      }
    }
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_fd_sink_check_fd (GstFdSink * fdsink, int fd)
{
  struct stat stat_results;
  off_t result;

  /* see that it is a valid file descriptor */
  if (fstat (fd, &stat_results) < 0)
    goto invalid;

  if (!S_ISREG (stat_results.st_mode))
    goto not_seekable;

  /* see if it is a seekable stream */
  result = lseek (fd, 0, SEEK_CUR);
  if (result == -1) {
    switch (errno) {
      case EINVAL:
      case EBADF:
        goto invalid;

      case ESPIPE:
        goto not_seekable;
    }
  } else
    GST_DEBUG_OBJECT (fdsink, "File descriptor %d is seekable", fd);

  return TRUE;

invalid:
  {
    GST_ELEMENT_ERROR (fdsink, RESOURCE, WRITE, (NULL),
        ("File descriptor %d is not valid: %s", fd, g_strerror (errno)));
    return FALSE;
  }
not_seekable:
  {
    GST_DEBUG_OBJECT (fdsink, "File descriptor %d is a pipe", fd);
    return TRUE;
  }
}

static gboolean
gst_fd_sink_start (GstBaseSink * basesink)
{
  GstFdSink *fdsink;
  GstPollFD fd = GST_POLL_FD_INIT;

  fdsink = GST_FD_SINK (basesink);
  if (!gst_fd_sink_check_fd (fdsink, fdsink->fd))
    return FALSE;

  if ((fdsink->fdset = gst_poll_new (TRUE)) == NULL)
    goto socket_pair;

  fd.fd = fdsink->fd;
  gst_poll_add_fd (fdsink->fdset, &fd);
  gst_poll_fd_ctl_write (fdsink->fdset, &fd, TRUE);

  fdsink->bytes_written = 0;
  fdsink->current_pos = 0;

  return TRUE;

  /* ERRORS */
socket_pair:
  {
    GST_ELEMENT_ERROR (fdsink, RESOURCE, OPEN_READ_WRITE, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
}

static gboolean
gst_fd_sink_stop (GstBaseSink * basesink)
{
  GstFdSink *fdsink = GST_FD_SINK (basesink);

  if (fdsink->fdset) {
    gst_poll_free (fdsink->fdset);
    fdsink->fdset = NULL;
  }

  return TRUE;
}

static gboolean
gst_fd_sink_unlock (GstBaseSink * basesink)
{
  GstFdSink *fdsink = GST_FD_SINK (basesink);

  GST_LOG_OBJECT (fdsink, "Flushing");
  GST_OBJECT_LOCK (fdsink);
  gst_poll_set_flushing (fdsink->fdset, TRUE);
  GST_OBJECT_UNLOCK (fdsink);

  return TRUE;
}

static gboolean
gst_fd_sink_unlock_stop (GstBaseSink * basesink)
{
  GstFdSink *fdsink = GST_FD_SINK (basesink);

  GST_LOG_OBJECT (fdsink, "No longer flushing");
  GST_OBJECT_LOCK (fdsink);
  gst_poll_set_flushing (fdsink->fdset, FALSE);
  GST_OBJECT_UNLOCK (fdsink);

  return TRUE;
}

static gboolean
gst_fd_sink_update_fd (GstFdSink * fdsink, int new_fd)
{
  if (new_fd < 0)
    return FALSE;

  if (!gst_fd_sink_check_fd (fdsink, new_fd))
    goto invalid;

  /* assign the fd */
  GST_OBJECT_LOCK (fdsink);
  if (fdsink->fdset) {
    GstPollFD fd = GST_POLL_FD_INIT;

    fd.fd = fdsink->fd;
    gst_poll_remove_fd (fdsink->fdset, &fd);

    fd.fd = new_fd;
    gst_poll_add_fd (fdsink->fdset, &fd);
    gst_poll_fd_ctl_write (fdsink->fdset, &fd, TRUE);
  }
  fdsink->fd = new_fd;
  g_free (fdsink->uri);
  fdsink->uri = g_strdup_printf ("fd://%d", fdsink->fd);

  GST_OBJECT_UNLOCK (fdsink);

  return TRUE;

invalid:
  {
    return FALSE;
  }
}

static void
gst_fd_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFdSink *fdsink;

  fdsink = GST_FD_SINK (object);

  switch (prop_id) {
    case ARG_FD:{
      int fd;

      fd = g_value_get_int (value);
      gst_fd_sink_update_fd (fdsink, fd);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fd_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFdSink *fdsink;

  fdsink = GST_FD_SINK (object);

  switch (prop_id) {
    case ARG_FD:
      g_value_set_int (value, fdsink->fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_fd_sink_do_seek (GstFdSink * fdsink, guint64 new_offset)
{
  off_t result;

  result = lseek (fdsink->fd, new_offset, SEEK_SET);

  if (result == -1)
    goto seek_failed;

  fdsink->current_pos = new_offset;

  GST_DEBUG_OBJECT (fdsink, "File descriptor %d to seek to position "
      "%" G_GUINT64_FORMAT, fdsink->fd, fdsink->current_pos);

  return TRUE;

  /* ERRORS */
seek_failed:
  {
    GST_DEBUG_OBJECT (fdsink, "File descriptor %d failed to seek to position "
        "%" G_GUINT64_FORMAT, fdsink->fd, new_offset);
    return FALSE;
  }
}

static gboolean
gst_fd_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstEventType type;
  GstFdSink *fdsink;

  fdsink = GST_FD_SINK (sink);

  type = GST_EVENT_TYPE (event);

  switch (type) {
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 start, stop, pos;
      GstFormat format;
      gst_event_parse_new_segment (event, NULL, NULL, &format, &start,
          &stop, &pos);

      if (format == GST_FORMAT_BYTES) {
        /* only try to seek and fail when we are going to a different
         * position */
        if (fdsink->current_pos != start) {
          /* FIXME, the seek should be performed on the pos field, start/stop are
           * just boundaries for valid bytes offsets. We should also fill the file
           * with zeroes if the new position extends the current EOF (sparse streams
           * and segment accumulation). */
          if (!gst_fd_sink_do_seek (fdsink, (guint64) start))
            goto seek_failed;
        }
      } else {
        GST_DEBUG_OBJECT (fdsink,
            "Ignored NEWSEGMENT event of format %u (%s)", (guint) format,
            gst_format_get_name (format));
      }
      break;
    }
    default:
      break;
  }

  return TRUE;

seek_failed:
  {
    GST_ELEMENT_ERROR (fdsink, RESOURCE, SEEK, (NULL),
        ("Error while seeking on file descriptor %d: %s",
            fdsink->fd, g_strerror (errno)));
    return FALSE;
  }

}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_fd_sink_uri_get_type (void)
{
  return GST_URI_SINK;
}

static gchar **
gst_fd_sink_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "fd", NULL };

  return protocols;
}

static const gchar *
gst_fd_sink_uri_get_uri (GstURIHandler * handler)
{
  GstFdSink *sink = GST_FD_SINK (handler);

  return sink->uri;
}

static gboolean
gst_fd_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol;
  GstFdSink *sink = GST_FD_SINK (handler);
  gint fd;

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "fd") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);

  if (sscanf (uri, "fd://%d", &fd) != 1)
    return FALSE;

  return gst_fd_sink_update_fd (sink, fd);
}

static void
gst_fd_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_fd_sink_uri_get_type;
  iface->get_protocols = gst_fd_sink_uri_get_protocols;
  iface->get_uri = gst_fd_sink_uri_get_uri;
  iface->set_uri = gst_fd_sink_uri_set_uri;
}
