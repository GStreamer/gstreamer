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
 * SECTION: gesbasetransitionclip
 * @title: GESBaseTransitionClip
 * @short_description: Base classes for transitions
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ges/ges.h>
#include "ges-internal.h"

struct _GESBaseTransitionClipPrivate
{
  /* Dummy variable */
  void *nothing;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GESBaseTransitionClip,
    ges_base_transition_clip, GES_TYPE_OPERATION_CLIP);

static void
ges_base_transition_clip_class_init (GESBaseTransitionClipClass * klass)
{
}

static void
ges_base_transition_clip_init (GESBaseTransitionClip * self)
{
  self->priv = ges_base_transition_clip_get_instance_private (self);
}
