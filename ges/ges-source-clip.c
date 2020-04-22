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
 * @title: GESSourceClip
 * @short_description: Base Class for sources of a #GESLayer
 *
 * #GESSourceClip-s are clips whose core elements are #GESSource-s.
 *
 * ## Effects
 *
 * #GESSourceClip-s can also have #GESBaseEffect-s added as non-core
 * elements. These effects are applied to the core sources of the clip
 * that they share a #GESTrack with. See #GESClip for how to add and move
 * these effects from the clip.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

static GESExtractableInterface *parent_extractable_iface = NULL;

static gchar *
extractable_check_id (GType type, const gchar * id, GError ** error)
{
  if (type == GES_TYPE_SOURCE_CLIP) {
    g_set_error (error, GES_ERROR, GES_ERROR_ASSET_WRONG_ID,
        "Only `time-overlay` is supported as an ID for type: `GESSourceClip`,"
        " got: '%s'", id);
    return NULL;
  }

  return parent_extractable_iface->check_id (type, id, error);
}

static GType
extractable_get_real_extractable_type (GType wanted_type, const gchar * id)
{
  GstStructure *structure;

  if (!id || (wanted_type != GES_TYPE_SOURCE_CLIP
          && wanted_type != GES_TYPE_TEST_CLIP))
    return wanted_type;

  structure = gst_structure_new_from_string (id);
  if (!structure)
    return wanted_type;

  if (gst_structure_has_name (structure, "time-overlay"))
    /* Just reusing TestClip to create a GESTimeOverlayClip
     * as it already does the job! */
    wanted_type = GES_TYPE_TEST_CLIP;

  gst_structure_free (structure);

  return wanted_type;
}

static void
extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_SOURCE_CLIP_ASSET;
  iface->get_real_extractable_type = extractable_get_real_extractable_type;
  iface->check_id = extractable_check_id;

  parent_extractable_iface = g_type_interface_peek_parent (iface);
}

G_DEFINE_TYPE_WITH_CODE (GESSourceClip, ges_source_clip,
    GES_TYPE_CLIP, G_ADD_PRIVATE (GESSourceClip)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, extractable_interface_init));

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

  object_class->get_property = ges_source_clip_get_property;
  object_class->set_property = ges_source_clip_set_property;
  object_class->finalize = ges_source_clip_finalize;

  GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass) = TRUE;
}

static void
ges_source_clip_init (GESSourceClip * self)
{
  self->priv = ges_source_clip_get_instance_private (self);
}
