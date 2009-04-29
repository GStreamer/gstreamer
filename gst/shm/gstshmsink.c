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

#include "gstshmsink.h"
#include "gstshm.h"

#include <gst/gst.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>

/* signals */
enum
{
  LAST_SIGNAL
};

/* properties */
enum
{
  PROP_0,
  PROP_SHM_NAME,
  PROP_PERMS
};


GST_DEBUG_CATEGORY_STATIC (shmsink_debug);

static const GstElementDetails gst_shm_sink_details =
GST_ELEMENT_DETAILS ("Shared Memory Sink",
    "Sink",
    "Send data over shared memory to the matching source",
    "Olivier Crete <olivier.crete@collabora.co.uk>");

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstShmSink, gst_shm_sink, GstBaseSink, GST_TYPE_BASE_SINK);

static void gst_shm_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_shm_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_shm_sink_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_shm_sink_start (GstBaseSink * bsink);
static gboolean gst_shm_sink_stop (GstBaseSink * bsink);
static gboolean gst_shm_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_shm_sink_render (GstBaseSink * bsink, GstBuffer * buf);
static gboolean gst_shm_sink_event (GstBaseSink * bsink, GstEvent * event);

// static guint gst_shm_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_shm_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &gst_shm_sink_details);
}


static void
gst_shm_sink_init (GstShmSink * self, GstShmSinkClass * g_class)
{
  self->fd = -1;
  self->shm_area = MAP_FAILED;
}

static void
gst_shm_sink_class_init (GstShmSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_shm_sink_set_property;
  gobject_class->get_property = gst_shm_sink_get_property;

  gstelement_class->change_state = gst_shm_sink_change_state;

  gstbasesink_class->start = gst_shm_sink_start;
  gstbasesink_class->stop = gst_shm_sink_stop;
  gstbasesink_class->set_caps = gst_shm_sink_set_caps;
  gstbasesink_class->render = gst_shm_sink_render;
  gstbasesink_class->event = gst_shm_sink_event;

  g_object_class_install_property (gobject_class, PROP_SHM_NAME,
      g_param_spec_string ("shm-name",
          "Name of the shared memory area",
          "The name of the shared memory area that the source can read from",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PERMS,
      g_param_spec_uint ("perms",
          "Permissions on the shm area",
          "Permissions to set on the shm area",
          0, 07777, S_IRWXU | S_IRWXG,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (shmsink_debug, "shmsink", 0, "Shared Memory Sink");
}

/*
 * Set the value of a property for the server sink.
 */
static void
gst_shm_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShmSink *self = GST_SHM_SINK (object);

  switch (prop_id) {
    case PROP_SHM_NAME:
      GST_OBJECT_LOCK (object);
      g_free (self->shm_name);
      self->shm_name = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_PERMS:
      self->perms = g_value_get_uint (value);
      if (self->fd >= 0)
        if (fchmod (self->fd, g_value_get_uint (value)))
          GST_WARNING_OBJECT (self,
              "Could not set permissions %o on shm area: %s",
              g_value_get_uint (value), strerror (errno));
      break;
    default:
      break;
  }
}

static void
gst_shm_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstShmSink *self = GST_SHM_SINK (object);

  switch (prop_id) {
    case PROP_SHM_NAME:
      GST_OBJECT_LOCK (object);
      g_value_set_string (value, self->shm_name);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_PERMS:
      self->perms = g_value_get_uint (value);
      if (self->fd >= 0)
        fchmod (self->fd, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}



static gboolean
gst_shm_sink_start (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);
  g_return_val_if_fail (self->fd == -1, FALSE);

  GST_OBJECT_LOCK (self);
  if (!self->shm_name) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "Must set the name of the shm area first");
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("One must specify the name of the shm area"),
        ("shm-name property not set"));
    return FALSE;
  }

  self->fd = shm_open (self->shm_name, O_RDWR | O_CREAT | O_TRUNC, self->perms);

  if (self->fd < 0) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "Could not open shm area: %s", strerror (errno));
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not open the shm area"),
        ("shm_open failed (%d): %s", errno, strerror (errno)));
    return FALSE;
  }
  self->opened_name = g_strdup (self->shm_name);

  GST_OBJECT_UNLOCK (self);

  self->shm_area_len = sizeof (struct GstShmHeader);

  if (ftruncate (self->fd, self->shm_area_len)) {
    GST_ERROR_OBJECT (self,
        "Could not make shm area large enough for header: %s",
        strerror (errno));
    gst_shm_sink_stop (bsink);
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not resize memory area"),
        ("ftruncate failed (%d): %s", errno, strerror (errno)));
    return FALSE;
  }

  self->shm_area = mmap (NULL, self->shm_area_len, PROT_READ | PROT_WRITE,
      MAP_SHARED, self->fd, 0);

  if (self->shm_area == MAP_FAILED) {
    GST_ERROR_OBJECT (self, "Could not map shm area");
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not map memory area"),
        ("mmap failed (%d): %s", errno, strerror (errno)));
    gst_shm_sink_stop (bsink);
    return FALSE;
  }

  memset (self->shm_area, 0, self->shm_area_len);
  g_assert (sem_init (&self->shm_area->notification, 1, 0) == 0);
  g_assert (sem_init (&self->shm_area->mutex, 1, 1) == 0);

  return TRUE;
}


static gboolean
gst_shm_sink_stop (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  if (self->fd >= 0)
    close (self->fd);
  self->fd = -1;

  if (self->opened_name) {
    shm_unlink (self->opened_name);
    g_free (self->opened_name);
    self->opened_name = NULL;
  }

  if (self->shm_area != MAP_FAILED)
    munmap (self->shm_area, self->shm_area_len);
  self->shm_area_len = 0;
  self->shm_area = MAP_FAILED;

  return TRUE;
}

static gboolean
gst_shm_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  self->caps_gen++;

  return TRUE;
}

static gboolean
resize_area (GstShmSink * self, size_t desired_length)
{
  if (desired_length <= self->shm_area_len)
    return TRUE;

  SHM_UNLOCK (self->shm_area);

  if (munmap (self->shm_area, self->shm_area_len)) {
    GST_ERROR_OBJECT (self, "Could not unmap shared area");
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not unmap memory area"),
        ("munmap failed (%d): %s", errno, strerror (errno)));
    return FALSE;
  }
  if (ftruncate (self->fd, desired_length)) {
    GST_ERROR_OBJECT (self, "Could not resize shared area");
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not resize memory area"),
        ("ftruncate failed (%d): %s", errno, strerror (errno)));
    return FALSE;
  }
  self->shm_area = mmap (NULL, desired_length, PROT_READ | PROT_WRITE,
      MAP_SHARED, self->fd, 0);
  self->shm_area_len = desired_length;

  if (self->shm_area == MAP_FAILED) {
    self->shm_area = NULL;
    GST_ERROR_OBJECT (self, "Could not remap shared area");
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not map memory area"),
        ("mmap failed (%d): %s", errno, strerror (errno)));
    return FALSE;
  }

  SHM_LOCK (self->shm_area);

  return TRUE;
}

static GstFlowReturn
gst_shm_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  g_return_val_if_fail (self->shm_area != MAP_FAILED, GST_FLOW_ERROR);

  SHM_LOCK (self->shm_area);

  if (self->caps_gen != self->shm_area->caps_gen) {
    gchar *caps_str =
        gst_caps_to_string (GST_PAD_CAPS (GST_BASE_SINK_PAD (bsink)));
    guint caps_size = strlen (caps_str) + 1;

    if (!resize_area (self, sizeof (struct GstShmHeader) +
            caps_size + GST_BUFFER_SIZE (buf))) {
      g_free (caps_str);
      return GST_FLOW_ERROR;
    }

    self->shm_area->caps_size = caps_size;

    memcpy (GST_SHM_CAPS_BUFFER (self->shm_area), caps_str, caps_size);
    g_free (caps_str);

    self->shm_area->caps_gen = self->caps_gen;
  } else {
    if (!resize_area (self, sizeof (struct GstShmHeader) +
            self->shm_area->caps_size + GST_BUFFER_SIZE (buf)))
      return GST_FLOW_ERROR;
  }

  memcpy (GST_SHM_BUFFER (self->shm_area), GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));

  self->shm_area->buffer_size = GST_BUFFER_SIZE (buf);
  self->shm_area->buffer_gen++;

  self->shm_area->timestamp = GST_BUFFER_TIMESTAMP (buf);
  self->shm_area->duration = GST_BUFFER_DURATION (buf);
  self->shm_area->offset = GST_BUFFER_OFFSET (buf);
  self->shm_area->offset_end = GST_BUFFER_OFFSET_END (buf);
  self->shm_area->flags = GST_BUFFER_FLAGS (buf) & (GST_BUFFER_FLAG_DISCONT |
      GST_BUFFER_FLAG_GAP | GST_BUFFER_FLAG_DELTA_UNIT);

  sem_post (&self->shm_area->notification);

  SHM_UNLOCK (self->shm_area);

  return GST_FLOW_OK;
}

static gboolean
gst_shm_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  g_return_val_if_fail (self->shm_area != MAP_FAILED, FALSE);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    SHM_LOCK (self->shm_area);
    self->shm_area->eos = TRUE;
    SHM_UNLOCK (self->shm_area);
  }

  return TRUE;
}

static GstStateChangeReturn
gst_shm_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstShmSink *self = GST_SHM_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_return_val_if_fail (self->shm_area != MAP_FAILED,
          GST_STATE_CHANGE_FAILURE);
      SHM_LOCK (self->shm_area);
      self->shm_area->eos = FALSE;
      SHM_UNLOCK (self->shm_area);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
