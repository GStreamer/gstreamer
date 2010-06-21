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
 * SECTION:ges-track-title-source
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges-track-overlay.h"

G_DEFINE_TYPE (GESTrackOverlay, ges_track_overlay, GES_TYPE_TRACK_OBJECT);

static void ges_track_overlay_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);

static void ges_track_overlay_dispose (GObject *);

static void ges_track_overlay_finalize (GObject *);

static void ges_track_overlay_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);

static gboolean ges_track_overlay_create_gnl_object (GESTrackObject * object);

static GstElement
    * ges_track_overlay_create_element_func (GESTrackOverlay * object);

enum
{
  PROP_0,
};

static void
ges_track_overlay_class_init (GESTrackOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_overlay_get_property;
  object_class->set_property = ges_track_overlay_set_property;
  object_class->dispose = ges_track_overlay_dispose;
  object_class->finalize = ges_track_overlay_finalize;

  track_class->create_gnl_object = ges_track_overlay_create_gnl_object;
  klass->create_element = ges_track_overlay_create_element_func;
}

static void
ges_track_overlay_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_overlay_parent_class)->dispose (object);
}

static void
ges_track_overlay_init (GESTrackOverlay * self)
{
  self->element = NULL;
}

static void
ges_track_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_overlay_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_overlay_parent_class)->finalize (object);
}

static gboolean
ges_track_overlay_create_gnl_object (GESTrackObject * object)
{
  GESTrackOverlayClass *klass;
  GESTrackOverlay *self;

  self = GES_TRACK_OVERLAY (object);
  klass = GES_TRACK_OVERLAY_GET_CLASS (object);

  object->gnlobject = gst_element_factory_make ("gnloperation", NULL);
  self->element = klass->create_element (GES_TRACK_OVERLAY (object));
  gst_bin_add (GST_BIN (object->gnlobject), self->element);

  return TRUE;
}

static GstElement *
ges_track_overlay_create_element_func (GESTrackOverlay * self)
{
  return gst_element_factory_make ("identity", NULL);
}

GESTrackOverlay *
ges_track_overlay_new (void)
{
  return g_object_new (GES_TYPE_TRACK_OVERLAY, NULL);
}
