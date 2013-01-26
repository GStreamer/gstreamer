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
 * SECTION:ges-track-operation
 * @short_description: Base Class for effects and overlays
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-track-operation.h"

G_DEFINE_ABSTRACT_TYPE (GESTrackOperation, ges_track_operation,
    GES_TYPE_TRACK_ELEMENT);

struct _GESTrackOperationPrivate
{
  /* Dummy variable */
  void *nothing;
};

static void
ges_track_operation_class_init (GESTrackOperationClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackOperationPrivate));

  track_class->gnlobject_factorytype = "gnloperation";
}

static void
ges_track_operation_init (GESTrackOperation * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_OPERATION, GESTrackOperationPrivate);
}
