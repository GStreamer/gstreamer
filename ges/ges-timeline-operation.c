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
 * SECTION:ges-timeline-operation
 * @short_description: Base Class for operations in a GESTimelineLayer
 *
 * Operations are any kind of object that both outputs AND consumes data.
 */

#include "ges.h"
#include "ges-internal.h"
#include "ges-timeline-operation.h"

G_DEFINE_ABSTRACT_TYPE (GESTimelineOperation, ges_timeline_operation,
    GES_TYPE_CLIP);

struct _GESTimelineOperationPrivate
{
  void *nada;
};

static void
ges_timeline_operation_class_init (GESTimelineOperationClass * klass)
{
  g_type_class_add_private (klass, sizeof (GESTimelineOperationPrivate));
}

static void
ges_timeline_operation_init (GESTimelineOperation * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_OPERATION, GESTimelineOperationPrivate);
}
