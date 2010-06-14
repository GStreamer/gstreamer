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
 * SECTION:ges-track-background-source
 * @short_description: Base Class for background source track objects
 */

#include "ges-internal.h"
#include "ges-track-background-source.h"

G_DEFINE_TYPE (GESTrackBackgroundSource, ges_track_bg_src,
    GES_TYPE_TRACK_SOURCE);

static void ges_track_bg_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);

static void ges_track_bg_src_dispose (GObject *);

static void ges_track_bg_src_finalize (GObject *);

static void ges_track_bg_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);

static gboolean ges_track_bg_src_create_gnl_object (GESTrackObject * object);

static GstElement
    * ges_track_bg_src_create_element_func (GESTrackBackgroundSource * object);

enum
{
  PROP_0,
};

static void
ges_track_bg_src_class_init (GESTrackBackgroundSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *track_class = GES_TRACK_OBJECT_CLASS (klass);

  object_class->get_property = ges_track_bg_src_get_property;
  object_class->set_property = ges_track_bg_src_set_property;
  object_class->dispose = ges_track_bg_src_dispose;
  object_class->finalize = ges_track_bg_src_finalize;

  track_class->create_gnl_object = ges_track_bg_src_create_gnl_object;
  klass->create_element = ges_track_bg_src_create_element_func;
}

static void
ges_track_bg_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_bg_src_parent_class)->dispose (object);
}

static void
ges_track_bg_src_init (GESTrackBackgroundSource * self)
{
  self->element = NULL;
}

static void
ges_track_bg_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_bg_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_bg_src_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_bg_src_parent_class)->finalize (object);
}

static gboolean
ges_track_bg_src_create_gnl_object (GESTrackObject * object)
{
  GESTrackBackgroundSourceClass *klass;
  GESTrackBackgroundSource *self;

  self = GES_TRACK_BACKGROUND_SOURCE (object);
  klass = GES_TRACK_BACKGROUND_SOURCE_GET_CLASS (object);

  object->gnlobject = gst_element_factory_make ("gnlsource", NULL);
  self->element = klass->create_element (GES_TRACK_BACKGROUND_SOURCE (object));
  gst_bin_add (GST_BIN (object->gnlobject), self->element);

  return TRUE;
}

static GstElement *
ges_track_bg_src_create_element_func (GESTrackBackgroundSource * self)
{
  return gst_element_factory_make ("fakesrc", NULL);
}

GESTrackBackgroundSource *
ges_track_background_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_BACKGROUND_SOURCE, NULL);
}
