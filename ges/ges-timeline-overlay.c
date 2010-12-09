/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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
 * SECTION:ges-timeline-overlay
 * @short_description: Base Class for overlays in a #GESTimelineLayer
 *
 * Overlays are objects which modify the underlying layer(s).
 *
 * Examples of overlays include text, image watermarks, or audio dubbing.
 *
 * Transitions, which change from one source to another over time, are
 * not considered overlays. 
 */

#include "ges-internal.h"
#include "ges-timeline-operation.h"
#include "ges-timeline-overlay.h"

G_DEFINE_ABSTRACT_TYPE (GESTimelineOverlay, ges_timeline_overlay,
    GES_TYPE_TIMELINE_OPERATION);

struct _GESTimelineOverlayPrivate
{
  /*  Dummy variable */
  void *nothing;
};

static void
ges_timeline_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_overlay_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_overlay_parent_class)->dispose (object);
}

static void
ges_timeline_overlay_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_overlay_parent_class)->finalize (object);
}

static void
ges_timeline_overlay_class_init (GESTimelineOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineOverlayPrivate));

  object_class->get_property = ges_timeline_overlay_get_property;
  object_class->set_property = ges_timeline_overlay_set_property;
  object_class->dispose = ges_timeline_overlay_dispose;
  object_class->finalize = ges_timeline_overlay_finalize;
}

static void
ges_timeline_overlay_init (GESTimelineOverlay * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_OVERLAY, GESTimelineOverlayPrivate);
}
