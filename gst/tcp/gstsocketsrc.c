/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2011> Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2014> William Manley <will@williammanley.net>
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
 * SECTION:element-socketsrc
 *
 * Receive data from a socket.
 *
 * As compared to other elements:
 *
 * socketsrc can be considered a source counterpart to the #multisocketsink
 * sink.
 *
 * socketsrc can also be considered a generalization of #tcpclientsrc and
 * #tcpserversrc: it contains all the logic required to communicate over the
 * socket but none of the logic for creating the sockets/establishing the
 * connection in the first place, allowing the user to accomplish this
 * externally in whatever manner they wish making it applicable to other types
 * of sockets besides TCP.
 *
 * As compared to #fdsrc socketsrc is socket specific and deals with #GSocket
 * objects rather than sockets via integer file-descriptors.
 *
 * @see_also: #multisocketsink
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>
#include "gstsocketsrc.h"
#include "gsttcp.h"

GST_DEBUG_CATEGORY_STATIC (socketsrc_debug);
#define GST_CAT_DEFAULT socketsrc_debug

#define MAX_READ_SIZE                   4 * 1024


static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


enum
{
  PROP_0,
  PROP_SOCKET,
};

#define gst_socket_src_parent_class parent_class
G_DEFINE_TYPE (GstSocketSrc, gst_socket_src, GST_TYPE_PUSH_SRC);


static void gst_socket_src_finalize (GObject * gobject);

static GstFlowReturn gst_socket_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_socket_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_socket_src_unlock_stop (GstBaseSrc * bsrc);

static void gst_socket_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_socket_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_socket_src_class_init (GstSocketSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_socket_src_set_property;
  gobject_class->get_property = gst_socket_src_get_property;
  gobject_class->finalize = gst_socket_src_finalize;

  g_object_class_install_property (gobject_class, PROP_SOCKET,
      g_param_spec_object ("socket", "Socket",
          "The socket to receive packets from", G_TYPE_SOCKET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "socket source", "Source/Network",
      "Receive data from a socket",
      "Thomas Vander Stichele <thomas at apestaart dot org>, "
      "William Manley <will@williammanley.net>");

  gstbasesrc_class->unlock = gst_socket_src_unlock;
  gstbasesrc_class->unlock_stop = gst_socket_src_unlock_stop;

  gstpush_src_class->create = gst_socket_src_create;

  GST_DEBUG_CATEGORY_INIT (socketsrc_debug, "socketsrc", 0, "Socket Source");
}

static void
gst_socket_src_init (GstSocketSrc * this)
{
  this->socket = NULL;
  this->cancellable = g_cancellable_new ();
}

static void
gst_socket_src_finalize (GObject * gobject)
{
  GstSocketSrc *this = GST_SOCKET_SRC (gobject);

  if (this->cancellable)
    g_object_unref (this->cancellable);
  this->cancellable = NULL;
  if (this->socket)
    g_object_unref (this->socket);
  this->socket = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstFlowReturn
gst_socket_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstSocketSrc *src;
  GstFlowReturn ret = GST_FLOW_OK;
  gssize rret;
  GError *err = NULL;
  GstMapInfo map;
  gssize avail, read;
  GSocket *socket;

  src = GST_SOCKET_SRC (psrc);

  GST_OBJECT_LOCK (src);

  socket = src->socket;
  if (socket == NULL) {
    GST_OBJECT_UNLOCK (src);
    goto no_socket;
  }
  g_object_ref (socket);

  GST_OBJECT_UNLOCK (src);

  GST_LOG_OBJECT (src, "asked for a buffer");

  /* read the buffer header */
  avail = g_socket_get_available_bytes (socket);
  if (avail < 0) {
    goto get_available_error;
  } else if (avail == 0) {
    GIOCondition condition;

    if (!g_socket_condition_wait (socket,
            G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, src->cancellable, &err))
      goto select_error;

    condition =
        g_socket_condition_check (socket,
        G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP);

    if ((condition & G_IO_ERR)) {
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Socket in error state"));
      *outbuf = NULL;
      ret = GST_FLOW_ERROR;
      goto done;
    } else if ((condition & G_IO_HUP)) {
      GST_DEBUG_OBJECT (src, "Connection closed");
      *outbuf = NULL;
      ret = GST_FLOW_EOS;
      goto done;
    }
    avail = g_socket_get_available_bytes (socket);
    if (avail < 0)
      goto get_available_error;
  }

  if (avail > 0) {
    read = MIN (avail, MAX_READ_SIZE);
    *outbuf = gst_buffer_new_and_alloc (read);
    gst_buffer_map (*outbuf, &map, GST_MAP_READWRITE);
    rret =
        g_socket_receive (socket, (gchar *) map.data, read,
        src->cancellable, &err);
  } else {
    /* Connection closed */
    *outbuf = NULL;
    read = 0;
    rret = 0;
  }

  if (rret == 0) {
    GST_DEBUG_OBJECT (src, "Connection closed");
    ret = GST_FLOW_EOS;
    if (*outbuf) {
      gst_buffer_unmap (*outbuf, &map);
      gst_buffer_unref (*outbuf);
    }
    *outbuf = NULL;
  } else if (rret < 0) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      ret = GST_FLOW_FLUSHING;
      GST_DEBUG_OBJECT (src, "Cancelled reading from socket");
    } else {
      ret = GST_FLOW_ERROR;
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Failed to read from socket: %s", err->message));
    }
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  } else {
    ret = GST_FLOW_OK;
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_resize (*outbuf, 0, rret);

    GST_LOG_OBJECT (src,
        "Returning buffer from _get of size %" G_GSIZE_FORMAT ", ts %"
        GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
        ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
        gst_buffer_get_size (*outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (*outbuf)),
        GST_BUFFER_OFFSET (*outbuf), GST_BUFFER_OFFSET_END (*outbuf));
  }
  g_clear_error (&err);

done:
  g_object_unref (socket);
  return ret;

select_error:
  {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (src, "Cancelled");
      ret = GST_FLOW_FLUSHING;
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
          ("Select failed: %s", err->message));
      ret = GST_FLOW_ERROR;
    }
    g_clear_error (&err);
    g_object_unref (socket);
    return ret;
  }
get_available_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Failed to get available bytes from socket"));
    g_object_unref (socket);
    return GST_FLOW_ERROR;
  }
no_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
        ("Cannot receive: No socket set on socketsrc"));
    return GST_FLOW_ERROR;
  }
}

#define SWAP(a, b) do { GSocket* tmp = a; a = b; b = tmp; } while (0);

static void
gst_socket_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSocketSrc *socketsrc = GST_SOCKET_SRC (object);

  switch (prop_id) {
    case PROP_SOCKET:{
      GSocket *socket = G_SOCKET (g_value_dup_object (value));
      GST_OBJECT_LOCK (socketsrc);
      SWAP (socket, socketsrc->socket);
      GST_OBJECT_UNLOCK (socketsrc);
      g_clear_object (&socket);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_socket_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSocketSrc *socketsrc = GST_SOCKET_SRC (object);

  switch (prop_id) {
    case PROP_SOCKET:
      g_value_set_object (value, socketsrc->socket);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_socket_src_unlock (GstBaseSrc * bsrc)
{
  GstSocketSrc *src = GST_SOCKET_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "set to flushing");
  g_cancellable_cancel (src->cancellable);

  return TRUE;
}

static gboolean
gst_socket_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstSocketSrc *src = GST_SOCKET_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "unset flushing");
  g_cancellable_reset (src->cancellable);

  return TRUE;
}
