/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <tsaunier@gnome.org>
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
 * SECTION:ges-track-effect
 * @short_description: adds an effect to a stream in a #GESTimelineSource or a
 * #GESTimelineLayer
 *
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-effect.h"

G_DEFINE_ABSTRACT_TYPE (GESTrackEffect, ges_track_effect,
    GES_TYPE_TRACK_OPERATION);

struct _GESTrackEffectPrivate
{
  void *nothing;
};

static void
ges_track_effect_class_init (GESTrackEffectClass * klass)
{
  g_type_class_add_private (klass, sizeof (GESTrackEffectPrivate));
}

static void
ges_track_effect_init (GESTrackEffect * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GES_TYPE_TRACK_EFFECT,
      GESTrackEffectPrivate);
}
