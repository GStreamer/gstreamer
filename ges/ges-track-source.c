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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:ges-track-source
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-track-source.h"

G_DEFINE_TYPE (GESTrackSource, ges_track_source, GES_TYPE_TRACK_ELEMENT);

struct _GESTrackSourcePrivate
{
  /*  Dummy variable */
  void *nothing;
};

static void
ges_track_source_class_init (GESTrackSourceClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackSourcePrivate));

  track_class->gnlobject_factorytype = "gnlsource";
  track_class->create_element = NULL;
}

static void
ges_track_source_init (GESTrackSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_SOURCE, GESTrackSourcePrivate);
}
