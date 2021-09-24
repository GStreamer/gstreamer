/* GStreamer Editing Services
 * Copyright (C) 2011 Thibault Saunier <thibault.saunier@collabora.co.uk>
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
 * SECTION: gesbaseeffectclip
 * @title: GESBaseEffectClip
 * @short_description: An effect in a #GESLayer
 *
 * #GESBaseEffectClip-s are clips whose core elements are
 * #GESBaseEffect-s.
 *
 * ## Effects
 *
 * #GESBaseEffectClip-s can have **additional** #GESBaseEffect-s added as
 * non-core elements. These additional effects are applied to the output
 * of the core effects of the clip that they share a #GESTrack with. See
 * #GESClip for how to add and move these effects from the clip.
 *
 * Note that you cannot add time effects to #GESBaseEffectClip, neither
 * as core children, nor as additional effects.
 */

/* FIXME: properly handle the priority of the children. How should we sort
 * the priority of effects when two #GESBaseEffectClip's overlap? */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ges/ges.h>
#include "ges-internal.h"
#include "ges-types.h"

struct _GESBaseEffectClipPrivate
{
  void *nothing;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GESBaseEffectClip, ges_base_effect_clip,
    GES_TYPE_OPERATION_CLIP);

static gboolean
ges_base_effect_clip_add_child (GESContainer * container,
    GESTimelineElement * element)
{
  if (GES_IS_TIME_EFFECT (element)) {
    GST_WARNING_OBJECT (container, "Cannot add %" GES_FORMAT " as a child "
        "because it is a time effect", GES_ARGS (element));
    return FALSE;
  }

  return
      GES_CONTAINER_CLASS (ges_base_effect_clip_parent_class)->add_child
      (container, element);
}

static void
ges_base_effect_clip_class_init (GESBaseEffectClipClass * klass)
{
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);

  GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass) = TRUE;
  container_class->add_child = ges_base_effect_clip_add_child;
}

static void
ges_base_effect_clip_init (GESBaseEffectClip * self)
{
  self->priv = ges_base_effect_clip_get_instance_private (self);
}
