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
 * SECTION:gessourceclip
 * @short_description: Base Class for sources of a GESLayer
 */

#include "ges-internal.h"
#include "ges-clip.h"
#include "ges-source-clip.h"
#include "ges-source.h"


struct _GESSourceClipPrivate
{
  /*  dummy variable */
  void *nothing;
};

enum
{
  PROP_0,
};

G_DEFINE_TYPE (GESSourceClip, ges_source_clip, GES_TYPE_CLIP);

static void
ges_source_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_source_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_source_clip_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_source_clip_parent_class)->finalize (object);
}

static void
ges_source_clip_class_init (GESSourceClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSourceClipPrivate));

  object_class->get_property = ges_source_clip_get_property;
  object_class->set_property = ges_source_clip_set_property;
  object_class->finalize = ges_source_clip_finalize;
}

static void
ges_source_clip_init (GESSourceClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_SOURCE_CLIP, GESSourceClipPrivate);
}
