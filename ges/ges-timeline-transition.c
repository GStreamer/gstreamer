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
 * SECTION: ges-timeline-transition
 * @short_description: Base classes for transitions
 */

#include <ges/ges.h>
#include "ges-internal.h"

struct _GESTimelineTransitionPrivate
{
  /* Dummy variable */
  void *nothing;
};

G_DEFINE_ABSTRACT_TYPE (GESTimelineTransition, ges_timeline_transition,
    GES_TYPE_TIMELINE_OPERATION);

static void
ges_timeline_transition_class_init (GESTimelineTransitionClass * klass)
{
  g_type_class_add_private (klass, sizeof (GESTimelineTransitionPrivate));
}

static void
ges_timeline_transition_init (GESTimelineTransition * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_TRANSITION, GESTimelineTransitionPrivate);
}
