/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
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

#ifndef HAVE_WIN32
#include <sys/time.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
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

#define DEFAULT_BLOCKSIZE	4096

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_fdsrc_debug);
#define GST_CAT_DEFAULT gst_fdsrc_debug

GstElementDetails gst_fdsrc_details = GST_ELEMENT_DETAILS ("Disk Source",
    "Source/File",
    "Synchronous read from a file",
    "Erik Walthinsen <omega@cse.ogi.edu>");


/* FdSrc signals and args */
enum
{
  SIGNAL_TIMEOUT,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FD,
  ARG_BLOCKSIZE,
  ARG_TIMEOUT
};

static guint gst_fdsrc_signals[LAST_SIGNAL] = { 0 };

static void gst_fdsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType fdsrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_fdsrc_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (fdsrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (gst_fdsrc_debug, "fdsrc", 0, "fdsrc element");
}

GST_BOILERPLATE_FULL (GstFdSrc, gst_fdsrc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_fdsrc_dispose (GObject * obj);
static void gst_fdsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fdsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_fdsrc_change_state (GstElement * element);
static gboolean gst_fdsrc_release_locks (GstElement * element);
static GstData *gst_fdsrc_get (GstPad * pad);


static void
gst_fdsrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details (gstelement_class, &gst_fdsrc_details);
}
static void
gst_fdsrc_class_init (GstFdSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class = G_OBJECT_CLASS (klass);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FD,
      g_param_spec_int ("fd", "fd", "An open file descriptor to read from",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to read per buffer", 1, G_MAXULONG, DEFAULT_BLOCKSIZE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout", "Read timeout in nanoseconds",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE));

  gst_fdsrc_signals[SIGNAL_TIMEOUT] =
      g_signal_new ("timeout", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstFdSrcClass, timeout), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->set_property = gst_fdsrc_set_property;
  gobject_class->get_property = gst_fdsrc_get_property;
  gobject_class->dispose = gst_fdsrc_dispose;

  gstelement_class->change_state = gst_fdsrc_change_state;
  gstelement_class->release_locks = gst_fdsrc_release_locks;
}

static void
gst_fdsrc_dispose (GObject * obj)
{
  GstFdSrc *src = GST_FDSRC (obj);

  g_free (src->uri);
  src->uri = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_fdsrc_init (GstFdSrc * fdsrc)
{
  fdsrc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");

  gst_pad_set_get_function (fdsrc->srcpad, gst_fdsrc_get);
  gst_element_add_pad (GST_ELEMENT (fdsrc), fdsrc->srcpad);

  fdsrc->fd = 0;
  fdsrc->uri = g_strdup_printf ("fd://%d", fdsrc->fd);
  fdsrc->curoffset = 0;
  fdsrc->blocksize = DEFAULT_BLOCKSIZE;
  fdsrc->timeout = 0;
  fdsrc->seq = 0;
}

static GstElementStateReturn
gst_fdsrc_change_state (GstElement * element)
{
  GstFdSrc *src = GST_FDSRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      src->curoffset = 0;
      break;
    default:
      break;
  }

  /* in any case, an interrupt succeeds if we get here */
  src->interrupted = FALSE;

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static void
gst_fdsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));

  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_FD:
      src->fd = g_value_get_int (value);
      g_free (src->uri);
      src->uri = g_strdup_printf ("fd://%d", src->fd);
      break;
    case ARG_BLOCKSIZE:
      src->blocksize = g_value_get_ulong (value);
      break;
    case ARG_TIMEOUT:
      src->timeout = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fdsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFdSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSRC (object));

  src = GST_FDSRC (object);

  switch (prop_id) {
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, src->blocksize);
      break;
    case ARG_FD:
      g_value_set_int (value, src->fd);
      break;
    case ARG_TIMEOUT:
      g_value_set_uint64 (value, src->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_fdsrc_release_locks (GstElement * element)
{
  GstFdSrc *src = GST_FDSRC (element);

  src->interrupted = TRUE;

  return TRUE;
}

static GstData *
gst_fdsrc_get (GstPad * pad)
{
  GstFdSrc *src;
  GstBuffer *buf;
  glong readbytes;

#ifndef HAVE_WIN32
  fd_set readfds;
  struct timeval t;
  gint retval;
#endif

  src = GST_FDSRC (gst_pad_get_parent (pad));

  /* create the buffer */
  buf = gst_buffer_new_and_alloc (src->blocksize);

#ifndef HAVE_WIN32
  FD_ZERO (&readfds);
  FD_SET (src->fd, &readfds);

  /* loop until data is available, or a timeout is set. Re-enter
   * loop if we got a timeout without a timeout set, or if we
   * received an interrupt event. */
  do {
    if (src->timeout != 0) {
      GST_TIME_TO_TIMEVAL (src->timeout, t);
    } else {
      GST_TIME_TO_TIMEVAL (1000000000, t);
    }

    retval = select (src->fd + 1, &readfds, NULL, NULL, &t);
  } while (!src->interrupted &&
      ((retval == -1 && errno == EINTR) || (retval == 0 && src->timeout == 0)));

  if (src->interrupted) {
    GST_DEBUG_OBJECT (src, "received interrupt");
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  } else if (retval == -1) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("select on file descriptor: %s.", g_strerror (errno)));
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  } else if (retval == 0) {
    g_signal_emit (G_OBJECT (src), gst_fdsrc_signals[SIGNAL_TIMEOUT], 0);
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
#endif

  do {
    readbytes = read (src->fd, GST_BUFFER_DATA (buf), src->blocksize);
  } while (readbytes == -1 && errno == EINTR);  /* retry if interrupted */

  if (readbytes > 0) {
    GST_BUFFER_OFFSET (buf) = src->curoffset;
    GST_BUFFER_SIZE (buf) = readbytes;
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
    src->curoffset += readbytes;

    /* we're done, return the buffer */
    return GST_DATA (buf);
  } else if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  } else {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("read on file descriptor: %s.", g_strerror (errno)));
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_fdsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_fdsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "fd", NULL };

  return protocols;
}
static const gchar *
gst_fdsrc_uri_get_uri (GstURIHandler * handler)
{
  GstFdSrc *src = GST_FDSRC (handler);

  return src->uri;
}

static gboolean
gst_fdsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol;
  GstFdSrc *src = GST_FDSRC (handler);
  gint fd = src->fd;

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "fd") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  sscanf (uri, "fd://%d", &fd);
  src->fd = fd;
  g_free (src->uri);
  src->uri = g_strdup (uri);

  return TRUE;
}

static void
gst_fdsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_fdsrc_uri_get_type;
  iface->get_protocols = gst_fdsrc_uri_get_protocols;
  iface->get_uri = gst_fdsrc_uri_get_uri;
  iface->set_uri = gst_fdsrc_uri_set_uri;
}
