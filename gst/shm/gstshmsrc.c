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
  PROP_SHM_NAME
};


GST_DEBUG_CATEGORY_STATIC (shmsrc_debug);

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

  gstbasesrc_class->start = gst_shm_src_start;
  gstbasesrc_class->stop = gst_shm_src_stop;
  gstbasesrc_class->unlock = gst_shm_src_unlock;
  gstbasesrc_class->unlock_stop = gst_shm_src_unlock_stop;

  gstpush_src_class->create = gst_shm_src_create;

  g_object_class_install_property (gobject_class, PROP_SHM_NAME,
      g_param_spec_string ("shm-name",
          "Name of the shared memory area",
          "The name of the shared memory area that the source can read from",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (shmsrc_debug, "shmsrc", 0, "Shared Memory Source");
}

static void
gst_shm_src_init (GstShmSrc * self, GstShmSrcClass * g_class)
{
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), TRUE);

  gst_pad_use_fixed_caps (GST_BASE_SRC_PAD (self));

  self->fd = -1;
  self->shm_area = MAP_FAILED;
}


static void
gst_shm_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShmSrc *self = GST_SHM_SRC (object);

  switch (prop_id) {
    case PROP_SHM_NAME:
      GST_OBJECT_LOCK (object);
      g_free (self->shm_name);
      self->shm_name = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (object);
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
    case PROP_SHM_NAME:
      GST_OBJECT_LOCK (object);
      g_value_set_string (value, self->shm_name);
      GST_OBJECT_UNLOCK (object);
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

  self->fd = shm_open (self->shm_name, O_RDWR, 0);

  if (self->fd < 0) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "Could not open shm area: %s", strerror (errno));
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not open the shm area"),
        ("shm_open failed (%d): %s", errno, strerror (errno)));
    return FALSE;
  }
  GST_OBJECT_UNLOCK (self);


  self->shm_area_len = sizeof (struct GstShmHeader);

  self->shm_area = mmap (NULL, self->shm_area_len, PROT_READ | PROT_WRITE,
      MAP_SHARED, self->fd, 0);

  if (self->shm_area == MAP_FAILED) {
    GST_ERROR_OBJECT (self, "Could not map shm area");
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not map memory area"),
        ("mmap failed (%d): %s", errno, strerror (errno)));
    gst_shm_src_stop (bsrc);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_shm_src_stop (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  if (self->fd >= 0)
    close (self->fd);
  self->fd = -1;

  if (self->shm_area != MAP_FAILED)
    munmap (self->shm_area, self->shm_area_len);
  self->shm_area_len = 0;
  self->shm_area = MAP_FAILED;

  return TRUE;
}


static gboolean
resize_area (GstShmSrc * self)
{
  while ((sizeof (struct GstShmHeader) + self->shm_area->caps_size +
          self->shm_area->buffer_size) > self->shm_area_len) {
    size_t new_size = (sizeof (struct GstShmHeader) +
        self->shm_area->caps_size + self->shm_area->buffer_size);

    SHM_UNLOCK (self->shm_area);
    if (munmap (self->shm_area, self->shm_area_len)) {
      GST_ERROR_OBJECT (self, "Could not unmap shared area");
      GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
          ("Could not unmap memory area"),
          ("munmap failed (%d): %s", errno, strerror (errno)));
      return FALSE;
    }

    self->shm_area = mmap (NULL, new_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, self->fd, 0);

    if (!self->shm_area) {
      GST_ERROR_OBJECT (self, "Could not remap shared area");
      GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
          ("Could not map memory area"),
          ("mmap failed (%d): %s", errno, strerror (errno)));
      return FALSE;
    }
    self->shm_area_len = new_size;
    SHM_LOCK (self->shm_area);
  }

  return TRUE;
}

static GstFlowReturn
gst_shm_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstShmSrc *self = GST_SHM_SRC (psrc);

  if (self->unlocked)
    return GST_FLOW_WRONG_STATE;

  g_return_val_if_fail (self->shm_area != MAP_FAILED, GST_FLOW_ERROR);

  SHM_LOCK (self->shm_area);

  if (self->unlocked)
    goto unlocked;

  if (self->shm_area->eos)
    goto eos;

  while (self->buffer_gen == self->shm_area->buffer_gen) {
    SHM_UNLOCK (self->shm_area);

    if (self->unlocked)
      return GST_FLOW_WRONG_STATE;

    GST_LOG_OBJECT (self, "Waiting for next buffer");

    sem_wait (&self->shm_area->notification);

    if (self->unlocked)
      return GST_FLOW_WRONG_STATE;

    SHM_LOCK (self->shm_area);
  }

  if (self->unlocked)
    goto eos;

  if (!resize_area (self)) {
    return GST_FLOW_ERROR;
  }

  if (self->caps_gen != self->shm_area->caps_gen) {
    GstCaps *caps;

    GST_DEBUG_OBJECT (self, "Got new caps: %s",
        GST_SHM_CAPS_BUFFER (self->shm_area));

    caps = gst_caps_from_string (GST_SHM_CAPS_BUFFER (self->shm_area));

    self->caps_gen = self->shm_area->caps_gen;

    if (!caps) {
      SHM_UNLOCK (self->shm_area);
      GST_ERROR_OBJECT (self, "Could not read caps");
      return GST_FLOW_ERROR;
    }

    if (!gst_pad_set_caps (GST_BASE_SRC_PAD (psrc), caps)) {
      SHM_UNLOCK (self->shm_area);
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  GST_LOG_OBJECT (self, "Create new buffer of size %u",
      self->shm_area->buffer_size);

  *outbuf = gst_buffer_new_and_alloc (self->shm_area->buffer_size);

  memcpy (GST_BUFFER_DATA (*outbuf), GST_SHM_BUFFER (self->shm_area),
      GST_BUFFER_SIZE (*outbuf));

  // GST_BUFFER_TIMESTAMP (*outbuf) = self->shm_area->timestamp;
  GST_BUFFER_DURATION (*outbuf) = self->shm_area->duration;
  GST_BUFFER_OFFSET (*outbuf) = self->shm_area->offset;
  GST_BUFFER_OFFSET_END (*outbuf) = self->shm_area->offset_end;
  GST_BUFFER_FLAGS (*outbuf) = self->shm_area->flags;

  if (self->buffer_gen + 1 != self->shm_area->buffer_gen) {
    GST_WARNING_OBJECT (self, "Skipped %u buffers, setting DISCONT flag",
        self->shm_area->buffer_gen - self->buffer_gen - 1);
    GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_DISCONT);
  }

  self->buffer_gen = self->shm_area->buffer_gen;

  SHM_UNLOCK (self->shm_area);

  gst_buffer_set_caps (*outbuf, GST_PAD_CAPS (GST_BASE_SRC_PAD (psrc)));

  return GST_FLOW_OK;

eos:
  SHM_UNLOCK (self->shm_area);
  return GST_FLOW_UNEXPECTED;

unlocked:
  SHM_UNLOCK (self->shm_area);
  return GST_FLOW_WRONG_STATE;
}

static gboolean
gst_shm_src_unlock (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  self->unlocked = TRUE;

  if (self->shm_area != MAP_FAILED)
    sem_post (&self->shm_area->notification);

  return TRUE;
}

static gboolean
gst_shm_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstShmSrc *self = GST_SHM_SRC (bsrc);

  self->unlocked = FALSE;

  return TRUE;
}
