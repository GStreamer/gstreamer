/* GStreamer
 * Copyright (C) <2009> Collabora Ltd
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk
 * Copyright (C) <2009> Nokia Inc
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
#include "config.h"
#endif

#include "gstshmsrc.h"

#include <gst/gst.h>

#include <string.h>

/* signals */
enum
{
  LAST_SIGNAL
};

/* properties */
enum
{
  PROP_0,
  PROP_SOCKET_PATH,
  PROP_IS_LIVE
};

struct GstShmBuffer
{
  char *buf;
  GstShmSrc *src;
};


GST_DEBUG_CATEGORY_STATIC (shmsrc_debug);
#define GST_CAT_DEFAULT shmsrc_debug

static const GstElementDetails gst_shm_src_details =
GST_ELEMENT_DETAILS ("Shared Memory Source",
    "Source",
    "Receive data from the sharem memory sink",
    "Olivier Crete <olivier.crete@collabora.co.uk");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstShmSrc, gst_shm_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void gst_shm_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_shm_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_shm_src_start (GstBaseSrc * bsrc);
static gboolean gst_shm_src_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_shm_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_shm_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_shm_src_unlock_stop (GstBaseSrc * bsrc);

// static guint gst_shm_src_signals[LAST_SIGNAL] = { 0 };


static void
gst_shm_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_shm_src_details);
}

static void
gst_shm_src_class_init (GstShmSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_shm_src_set_property;
  gobject_class->get_property = gst_shm_src_get_property;

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_shm_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_shm_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_shm_src_unlock);
  gstbasesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_shm_src_unlock_stop);

  gstpush_src_class->create = gst_shm_src_create;

  g_object_class_install_property (gobject_class, PROP_SOCKET_PATH,
      g_param_spec_string ("socket-path",
          "Path to the control socket",
          "The path to the control socket used to control the shared memory"
          " transport", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is this a live source",
          "True if the element cannot produce data in PAUSED", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (shmsrc_debug, "shmsrc", 0, "Shared Memory Source");
}

static void
gst_shm_src_init (GstShmSrc * self, GstShmSrcClass * g_class)
{
}


static void
gst_shm_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShmSrc *self = GST_SHM_SRC (object);

  switch (prop_id) {
    case PROP_SOCKET_PATH:
      GST_OBJECT_LOCK (object);
      if (self->pipe) {
        GST_WARNING_OBJECT (object, "Can not modify socket path while the "
            "element is playing");
      } else {
        g_free (self->socket_path);
        self->socket_path = g_value_dup_string (value);
      }
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (object),
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_shm_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstShmSrc *self = GST_SHM_SRC (object);

  switch (prop_id) {
    case PROP_SOCKET_PATH:
      GST_OBJECT_LOCK (object);
      g_value_set_string (value, self->socket_path);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_shm_src_start (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  if (!self->socket_path) {
    GST_ELEMENT_ERROR (bsrc, RESOURCE, NOT_FOUND,
        ("No path specified for socket."), (NULL));
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  self->pipe = sp_client_open (self->socket_path);
  GST_OBJECT_UNLOCK (self);

  if (!self->pipe) {
    GST_ELEMENT_ERROR (bsrc, RESOURCE, OPEN_READ_WRITE,
        ("Could not open socket: %d %s", errno, strerror (errno)), (NULL));
    return FALSE;
  }

  self->poll = gst_poll_new (TRUE);
  gst_poll_fd_init (&self->pollfd);
  self->pollfd.fd = sp_get_fd (self->pipe);
  gst_poll_add_fd (self->poll, &self->pollfd);
  gst_poll_fd_ctl_read (self->poll, &self->pollfd, TRUE);

  return TRUE;
}

static gboolean
gst_shm_src_stop (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Stopping %p", self);

  GST_OBJECT_LOCK (self);
  sp_close (self->pipe);
  self->pipe = NULL;
  GST_OBJECT_UNLOCK (self);

  gst_poll_free (self->poll);
  self->poll = NULL;

  return TRUE;
}


static void
free_buffer (gpointer data)
{
  struct GstShmBuffer *gsb = data;
  g_return_if_fail (gsb->src->pipe != NULL);

  GST_LOG ("Freeing buffer %p", gsb->buf);

  GST_OBJECT_LOCK (gsb->src);
  sp_client_recv_finish (gsb->src->pipe, gsb->buf);
  GST_OBJECT_UNLOCK (gsb->src);

  gst_object_unref (gsb->src);
  g_slice_free (struct GstShmBuffer, gsb);
}

static GstFlowReturn
gst_shm_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstShmSrc *self = GST_SHM_SRC (psrc);
  gchar *buf = NULL;
  int rv = 0;
  struct GstShmBuffer *gsb;

  do {
    if (gst_poll_wait (self->poll, GST_CLOCK_TIME_NONE) < 0) {
      if (errno == EBUSY)
        return GST_FLOW_WRONG_STATE;
      GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Failed to read from shmsrc"),
          ("Poll failed on fd: %s", strerror (errno)));
      return GST_FLOW_ERROR;
    }

    if (self->unlocked)
      return GST_FLOW_WRONG_STATE;

    if (gst_poll_fd_has_closed (self->poll, &self->pollfd)) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Failed to read from shmsrc"),
          ("Control socket has closed"));
      return GST_FLOW_ERROR;
    }

    if (gst_poll_fd_has_error (self->poll, &self->pollfd)) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Failed to read from shmsrc"),
          ("Control socket has error"));
      return GST_FLOW_ERROR;
    }

    if (gst_poll_fd_can_read (self->poll, &self->pollfd)) {
      buf = NULL;
      GST_LOG_OBJECT (self, "Reading from pipe");
      GST_OBJECT_LOCK (self);
      rv = sp_client_recv (self->pipe, &buf);
      GST_OBJECT_UNLOCK (self);
      if (rv < 0) {
        GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Failed to read from shmsrc"),
            ("Error reading control data: %d", rv));
        return GST_FLOW_ERROR;
      }
    }
  } while (buf == NULL);

  GST_LOG_OBJECT (self, "Got buffer %p of size %d", buf, rv);

  gsb = g_slice_new0 (struct GstShmBuffer);
  gsb->buf = buf;
  gsb->src = gst_object_ref (self);

  *outbuf = gst_buffer_new ();
  GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_READONLY);
  GST_BUFFER_DATA (*outbuf) = (guint8 *) buf;
  GST_BUFFER_SIZE (*outbuf) = rv;
  GST_BUFFER_MALLOCDATA (*outbuf) = (guint8 *) gsb;
  GST_BUFFER_FREE_FUNC (*outbuf) = free_buffer;

  return GST_FLOW_OK;
}

static gboolean
gst_shm_src_unlock (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  self->unlocked = TRUE;

  if (self->poll)
    gst_poll_set_flushing (self->poll, TRUE);

  return TRUE;
}

static gboolean
gst_shm_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  self->unlocked = FALSE;

  if (self->poll)
    gst_poll_set_flushing (self->poll, FALSE);

  return TRUE;
}
