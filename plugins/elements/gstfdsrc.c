/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Philippe Khalaf <burger@speedy.org>
 *
 * gstfdsrc.c:
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
#include "gst/gst_private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef _MSC_VER
#include <io.h>
#endif
#include <stdlib.h>
#include <errno.h>

#include "gstfdsrc.h"

/* the select call is also performed on the control sockets, that way
 * we can send special commands to unblock the select call */
#define CONTROL_STOP            'S'     /* stop the select call */
#define CONTROL_SOCKETS(src)   src->control_sock
#define WRITE_SOCKET(src)      src->control_sock[1]
#define READ_SOCKET(src)       src->control_sock[0]

#define SEND_COMMAND(src, command)          \
G_STMT_START {                              \
  unsigned char c; c = command;             \
  write (WRITE_SOCKET(src), &c, 1);         \
} G_STMT_END

#define READ_COMMAND(src, command, res)        \
G_STMT_START {                                 \
  res = read(READ_SOCKET(src), &command, 1);   \
} G_STMT_END

#define DEFAULT_BLOCKSIZE	4096

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_fd_src_debug);
#define GST_CAT_DEFAULT gst_fd_src_debug

static GstElementDetails gst_fd_src_details =
GST_ELEMENT_DETAILS ("Disk Source",
    "Source/File",
    "Synchronous read from a file",
    "Erik Walthinsen <omega@cse.ogi.edu>");

enum
{
  PROP_0,
  PROP_FD,
};

static void gst_fd_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType fd_src_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_fd_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (fd_src_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_fd_src_debug, "fdsrc", 0, "fdsrc element");
}

GST_BOILERPLATE_FULL (GstFdSrc, gst_fd_src, GstElement, GST_TYPE_PUSH_SRC,
    _do_init);

static void gst_fd_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fd_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_fd_src_dispose (GObject * obj);

static gboolean gst_fd_src_start (GstBaseSrc * bsrc);
static gboolean gst_fd_src_stop (GstBaseSrc * bsrc);
static gboolean gst_fd_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_fd_src_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_fd_src_get_size (GstBaseSrc * src, guint64 * size);

static GstFlowReturn gst_fd_src_create (GstPushSrc * psrc, GstBuffer ** outbuf);

static void
gst_fd_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details (gstelement_class, &gst_fd_src_details);
}

static void
gst_fd_src_class_init (GstFdSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstElementClass *gstelement_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_PUSH_SRC);

  gobject_class->set_property = gst_fd_src_set_property;
  gobject_class->get_property = gst_fd_src_get_property;
  gobject_class->dispose = gst_fd_src_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FD,
      g_param_spec_int ("fd", "fd", "An open file descriptor to read from",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_fd_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_fd_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_fd_src_unlock);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_fd_src_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_fd_src_get_size);

  gstpush_src_class->create = GST_DEBUG_FUNCPTR (gst_fd_src_create);
}

static void
gst_fd_src_init (GstFdSrc * fdsrc, GstFdSrcClass * klass)
{
  fdsrc->fd = 0;
  fdsrc->new_fd = 0;
  fdsrc->seekable_fd = FALSE;
  fdsrc->uri = g_strdup_printf ("fd://%d", fdsrc->fd);
  fdsrc->curoffset = 0;
}

static void
gst_fd_src_dispose (GObject * obj)
{
  GstFdSrc *src = GST_FD_SRC (obj);

  g_free (src->uri);
  src->uri = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_fd_src_update_fd (GstFdSrc * src)
{
  struct stat stat_results;

  src->fd = src->new_fd;
  g_free (src->uri);
  src->uri = g_strdup_printf ("fd://%d", src->fd);

  if (fstat (src->fd, &stat_results) < 0)
    goto not_seekable;

  if (!S_ISREG (stat_results.st_mode))
    goto not_seekable;

  /* Try a seek of 0 bytes offset to check for seekability */
  if (lseek (src->fd, SEEK_CUR, 0) < 0)
    goto not_seekable;

  src->seekable_fd = TRUE;
  return;

not_seekable:
  src->seekable_fd = FALSE;
}

static gboolean
gst_fd_src_start (GstBaseSrc * bsrc)
{
  GstFdSrc *src = GST_FD_SRC (bsrc);
  gint control_sock[2];

  src->curoffset = 0;

  gst_fd_src_update_fd (src);

  if (socketpair (PF_UNIX, SOCK_STREAM, 0, control_sock) < 0)
    goto socket_pair;

  READ_SOCKET (src) = control_sock[0];
  WRITE_SOCKET (src) = control_sock[1];

  fcntl (READ_SOCKET (src), F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (src), F_SETFL, O_NONBLOCK);

  return TRUE;

  /* ERRORS */
socket_pair:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
}

static gboolean
gst_fd_src_stop (GstBaseSrc * bsrc)
{
  GstFdSrc *src = GST_FD_SRC (bsrc);

  close (READ_SOCKET (src));
  close (WRITE_SOCKET (src));

  return TRUE;
}

static gboolean
gst_fd_src_unlock (GstBaseSrc * bsrc)
{
  GstFdSrc *src = GST_FD_SRC (bsrc);

  SEND_COMMAND (src, CONTROL_STOP);

  return TRUE;
}

static void
gst_fd_src_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstFdSrc *src = GST_FD_SRC (object);

  switch (prop_id) {
    case PROP_FD:
      src->new_fd = g_value_get_int (value);

      /* If state is ready or below, update the current fd immediately
       * so it is reflected in get_properties and uri */
      GST_OBJECT_LOCK (object);
      if (GST_STATE (GST_ELEMENT (src)) <= GST_STATE_READY) {
        gst_fd_src_update_fd (src);
      }
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fd_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFdSrc *src = GST_FD_SRC (object);

  switch (prop_id) {
    case PROP_FD:
      g_value_set_int (value, src->fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_fd_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstFdSrc *src;
  GstBuffer *buf;
  glong readbytes;
  guint blocksize;

#ifndef HAVE_WIN32
  fd_set readfds;
  gint retval;
#endif

  src = GST_FD_SRC (psrc);

#ifndef HAVE_WIN32
  FD_ZERO (&readfds);
  FD_SET (src->fd, &readfds);
  FD_SET (READ_SOCKET (src), &readfds);

  do {
    retval = select (FD_SETSIZE, &readfds, NULL, NULL, NULL);
  } while ((retval == -1 && errno == EINTR));

  if (retval == -1)
    goto select_error;

  if (FD_ISSET (READ_SOCKET (src), &readfds)) {
    /* read all stop commands */
    while (TRUE) {
      gchar command;
      int res;

      READ_COMMAND (src, command, res);
      if (res < 0) {
        GST_LOG_OBJECT (src, "no more commands");
        /* no more commands */
        break;
      }
    }
    goto stopped;
  }
#endif

  blocksize = GST_BASE_SRC (src)->blocksize;

  /* create the buffer */
  buf = gst_buffer_new_and_alloc (blocksize);

  do {
    readbytes = read (src->fd, GST_BUFFER_DATA (buf), blocksize);
  } while (readbytes == -1 && errno == EINTR);  /* retry if interrupted */

  if (readbytes < 0)
    goto read_error;

  if (readbytes == 0)
    goto eos;

  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  src->curoffset += readbytes;

  GST_DEBUG_OBJECT (psrc, "Read buffer of size %u.", readbytes);

  /* we're done, return the buffer */
  *outbuf = buf;

  return GST_FLOW_OK;

  /* ERRORS */
#ifndef HAVE_WIN32
select_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("select on file descriptor: %s.", g_strerror (errno)));
    GST_DEBUG_OBJECT (psrc, "Error during select");
    return GST_FLOW_ERROR;
  }
stopped:
  {
    GST_DEBUG_OBJECT (psrc, "Select stopped");
    return GST_FLOW_WRONG_STATE;
  }
#endif
eos:
  {
    GST_DEBUG_OBJECT (psrc, "Read 0 bytes. EOS.");
    gst_buffer_unref (buf);
    return GST_FLOW_UNEXPECTED;
  }
read_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("read on file descriptor: %s.", g_strerror (errno)));
    GST_DEBUG_OBJECT (psrc, "Error reading from fd");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

gboolean
gst_fd_src_is_seekable (GstBaseSrc * bsrc)
{
  GstFdSrc *src = GST_FD_SRC (bsrc);

  return src->seekable_fd;
}

gboolean
gst_fd_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstFdSrc *src = GST_FD_SRC (bsrc);
  struct stat stat_results;

  if (!src->seekable_fd) {
    /* If it isn't seekable, we won't know the length (but fstat will still
     * succeed, and wrongly say our length is zero. */
    return FALSE;
  }

  if (fstat (src->fd, &stat_results) < 0)
    goto could_not_stat;

  *size = stat_results.st_size;

  return TRUE;

  /* ERROR */
could_not_stat:
  {
    return FALSE;
  }

}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_fd_src_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_fd_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "fd", NULL };

  return protocols;
}
static const gchar *
gst_fd_src_uri_get_uri (GstURIHandler * handler)
{
  GstFdSrc *src = GST_FD_SRC (handler);

  return src->uri;
}

static gboolean
gst_fd_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol;
  GstFdSrc *src = GST_FD_SRC (handler);
  gint fd;

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "fd") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);

  if (sscanf (uri, "fd://%d", &fd) != 1)
    return FALSE;

  src->new_fd = fd;

  GST_OBJECT_LOCK (src);
  if (GST_STATE (GST_ELEMENT (src)) <= GST_STATE_READY) {
    gst_fd_src_update_fd (src);
  }
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static void
gst_fd_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_fd_src_uri_get_type;
  iface->get_protocols = gst_fd_src_uri_get_protocols;
  iface->get_uri = gst_fd_src_uri_get_uri;
  iface->set_uri = gst_fd_src_uri_set_uri;
}
