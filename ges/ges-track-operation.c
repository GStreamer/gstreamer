/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:ges-track-operation
 * @short_description: Base Class for effects and overlays
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-operation.h"

G_DEFINE_TYPE (GESTrackOperation, ges_track_operation, GES_TYPE_TRACK_OBJECT);

static void
ges_track_operation_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_operation_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_operation_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_operation_parent_class)->dispose (object);
}

static void
ges_track_operation_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_operation_parent_class)->finalize (object);
}

static gboolean
ges_track_operation_create_gnl_object (GESTrackObject * object)
{
  GESTrackOperationClass *klass = NULL;
  GESTrackOperation *self = NULL;
  GstElement *child = NULL;
  GstElement *gnlobject;

  self = GES_TRACK_OPERATION (object);
  klass = GES_TRACK_OPERATION_GET_CLASS (self);

  gnlobject = gst_element_factory_make ("gnloperation", NULL);

  if (klass->create_element) {
    child = klass->create_element (self);

    if (G_UNLIKELY (!child)) {
      GST_ERROR ("create_element returned NULL");
      return TRUE;
    }

    gst_bin_add (GST_BIN (gnlobject), child);
    self->element = child;
  }

  object->gnlobject = gnlobject;

  return TRUE;
}

static void
ges_track_operation_class_init (GESTrackOperationClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_operation_get_property;
  object_class->set_property = ges_track_operation_set_property;
  object_class->dispose = ges_track_operation_dispose;
  object_class->finalize = ges_track_operation_finalize;

  track_class->create_gnl_object = ges_track_operation_create_gnl_object;
  klass->create_element = NULL;
}

static void
ges_track_operation_init (GESTrackOperation * self)
{
}

GESTrackOperation *
ges_track_operation_new (void)
{
  return g_object_new (GES_TYPE_TRACK_OPERATION, NULL);
}
