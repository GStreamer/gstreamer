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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-shmsrc
 *
 * Receive data from the shared memory sink.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch shmsrc socket-path=/tmp/blah ! \
 * "video/x-raw-yuv, format=(fourcc)YUY2, color-matrix=(string)sdtv, \
 * chroma-site=(string)mpeg2, width=(int)320, height=(int)240, framerate=(fraction)30/1" ! autovideosink
 * ]| Render video from shm buffers.
 * </refsect2>
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
  GstShmPipe *pipe;
};


GST_DEBUG_CATEGORY_STATIC (shmsrc_debug);
#define GST_CAT_DEFAULT shmsrc_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define gst_shm_src_parent_class parent_class
G_DEFINE_TYPE (GstShmSrc, gst_shm_src, GST_TYPE_PUSH_SRC);

static void gst_shm_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_shm_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_shm_src_finalize (GObject * object);
static gboolean gst_shm_src_start (GstBaseSrc * bsrc);
static gboolean gst_shm_src_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_shm_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_shm_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_shm_src_unlock_stop (GstBaseSrc * bsrc);
static GstStateChangeReturn gst_shm_src_change_state (GstElement * element,
    GstStateChange transition);

static void gst_shm_pipe_inc (GstShmPipe * pipe);
static void gst_shm_pipe_dec (GstShmPipe * pipe);

// static guint gst_shm_src_signals[LAST_SIGNAL] = { 0 };

static void
gst_shm_src_class_init (GstShmSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_shm_src_set_property;
  gobject_class->get_property = gst_shm_src_get_property;
  gobject_class->finalize = gst_shm_src_finalize;

  gstelement_class->change_state = gst_shm_src_change_state;

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

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "Shared Memory Source",
      "Source",
      "Receive data from the shared memory sink",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (shmsrc_debug, "shmsrc", 0, "Shared Memory Source");
}

static void
gst_shm_src_init (GstShmSrc * self)
{
  self->poll = gst_poll_new (TRUE);
  gst_poll_fd_init (&self->pollfd);
}

static void
gst_shm_src_finalize (GObject * object)
{
  GstShmSrc *self = GST_SHM_SRC (object);

  gst_poll_free (self->poll);
  g_free (self->socket_path);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
gst_shm_src_start_reading (GstShmSrc * self)
{
  GstShmPipe *gstpipe;

  if (!self->socket_path) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("No path specified for socket."), (NULL));
    return FALSE;
  }

  gstpipe = g_slice_new0 (GstShmPipe);
  gstpipe->use_count = 1;
  gstpipe->src = gst_object_ref (self);

  GST_DEBUG_OBJECT (self, "Opening socket %s", self->socket_path);

  GST_OBJECT_LOCK (self);
  gstpipe->pipe = sp_client_open (self->socket_path);
  GST_OBJECT_UNLOCK (self);

  if (!gstpipe->pipe) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not open socket %s: %d %s", self->socket_path, errno,
            strerror (errno)), (NULL));
    gst_shm_pipe_dec (gstpipe);
    return FALSE;
  }

  self->pipe = gstpipe;

  gst_poll_set_flushing (self->poll, FALSE);

  gst_poll_fd_init (&self->pollfd);
  self->pollfd.fd = sp_get_fd (self->pipe->pipe);
  gst_poll_add_fd (self->poll, &self->pollfd);
  gst_poll_fd_ctl_read (self->poll, &self->pollfd, TRUE);

  return TRUE;
}

static void
gst_shm_src_stop_reading (GstShmSrc * self)
{
  GST_DEBUG_OBJECT (self, "Stopping %p", self);

  if (self->pipe) {
    gst_shm_pipe_dec (self->pipe);
    self->pipe = NULL;
  }

  gst_poll_remove_fd (self->poll, &self->pollfd);
  gst_poll_fd_init (&self->pollfd);

  gst_poll_set_flushing (self->poll, TRUE);
}

static gboolean
gst_shm_src_start (GstBaseSrc * bsrc)
{
  if (gst_base_src_is_live (bsrc))
    return TRUE;
  else
    return gst_shm_src_start_reading (GST_SHM_SRC (bsrc));
}

static gboolean
gst_shm_src_stop (GstBaseSrc * bsrc)
{
  if (!gst_base_src_is_live (bsrc))
    gst_shm_src_stop_reading (GST_SHM_SRC (bsrc));

  return TRUE;
}


static void
free_buffer (gpointer data)
{
  struct GstShmBuffer *gsb = data;
  g_return_if_fail (gsb->pipe != NULL);
  g_return_if_fail (gsb->pipe->src != NULL);

  GST_LOG ("Freeing buffer %p", gsb->buf);

  GST_OBJECT_LOCK (gsb->pipe->src);
  sp_client_recv_finish (gsb->pipe->pipe, gsb->buf);
  GST_OBJECT_UNLOCK (gsb->pipe->src);

  gst_shm_pipe_dec (gsb->pipe);

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
        return GST_FLOW_FLUSHING;
      GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Failed to read from shmsrc"),
          ("Poll failed on fd: %s", strerror (errno)));
      return GST_FLOW_ERROR;
    }

    if (self->unlocked)
      return GST_FLOW_FLUSHING;

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
      rv = sp_client_recv (self->pipe->pipe, &buf);
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
  gsb->pipe = self->pipe;
  gst_shm_pipe_inc (self->pipe);

  *outbuf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      buf, rv, 0, rv, gsb, free_buffer);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_shm_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstShmSrc *self = GST_SHM_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (gst_base_src_is_live (GST_BASE_SRC (element)))
        if (!gst_shm_src_start_reading (self))
          return GST_STATE_CHANGE_FAILURE;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (gst_base_src_is_live (GST_BASE_SRC (element)))
        gst_shm_src_stop_reading (self);
    default:
      break;
  }

  return ret;
}

static gboolean
gst_shm_src_unlock (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  self->unlocked = TRUE;
  gst_poll_set_flushing (self->poll, TRUE);

  return TRUE;
}

static gboolean
gst_shm_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  self->unlocked = FALSE;
  gst_poll_set_flushing (self->poll, FALSE);

  return TRUE;
}

static void
gst_shm_pipe_inc (GstShmPipe * pipe)
{
  g_return_if_fail (pipe);
  g_return_if_fail (pipe->src);
  g_return_if_fail (pipe->use_count > 0);

  GST_OBJECT_LOCK (pipe->src);
  pipe->use_count++;
  GST_OBJECT_UNLOCK (pipe->src);
}

static void
gst_shm_pipe_dec (GstShmPipe * pipe)
{
  g_return_if_fail (pipe);
  g_return_if_fail (pipe->src);
  g_return_if_fail (pipe->use_count > 0);

  GST_OBJECT_LOCK (pipe->src);
  pipe->use_count--;

  if (pipe->use_count > 0) {
    GST_OBJECT_UNLOCK (pipe->src);
    return;
  }

  if (pipe->pipe)
    sp_client_close (pipe->pipe);
  GST_OBJECT_UNLOCK (pipe->src);

  gst_object_unref (pipe->src);
  g_slice_free (GstShmPipe, pipe);
}
