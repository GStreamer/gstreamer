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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:ges-overlay-clip
 * @short_description: Base Class for overlays in a GESTimelineLayer
 *
 * Overlays are objects which modify the underlying layer(s).
 *
 * Examples of overlays include text, image watermarks, or audio dubbing.
 *
 * Transitions, which change from one source to another over time, are
 * not considered overlays. 
 */

#include "ges-internal.h"
#include "ges-operation-clip.h"
#include "ges-overlay-clip.h"

G_DEFINE_ABSTRACT_TYPE (GESOverlayClip, ges_overlay_clip,
    GES_TYPE_OPERATION_CLIP);

struct _GESOverlayClipPrivate
{
  /*  Dummy variable */
  void *nothing;
};

static void
ges_overlay_clip_class_init (GESOverlayClipClass * klass)
{
  g_type_class_add_private (klass, sizeof (GESOverlayClipPrivate));
}

static void
ges_overlay_clip_init (GESOverlayClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_OVERLAY_CLIP, GESOverlayClipPrivate);
}
