/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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

/* SECTION: geseffectasset
 * @short_description: A GESAsset subclass specialized in GESEffect extraction
 *
 * This is internal, and implementation details, so we are not showing it in the
 * documentation
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-effect-asset.h"
#include "ges-track-element.h"
#include "ges-internal.h"

struct _GESEffectAssetPrivate
{
  gpointer nothing;
};

G_DEFINE_TYPE_WITH_PRIVATE (GESEffectAsset, ges_effect_asset,
    GES_TYPE_TRACK_ELEMENT_ASSET);

static void
_fill_track_type (GESAsset * asset)
{
  GESTrackType ttype;
  gchar *bin_desc;
  const gchar *id = ges_asset_get_id (asset);

  bin_desc = ges_effect_assect_id_get_type_and_bindesc (id, &ttype, NULL);

  if (bin_desc) {
    ges_track_element_asset_set_track_type (GES_TRACK_ELEMENT_ASSET (asset),
        ttype);
    g_free (bin_desc);
  } else {
    GST_WARNING_OBJECT (asset, "No track type set, you should"
        " specify one in [audio, video] as first component" " in the asset id");
  }
}

/* GESAsset virtual methods implementation */
static GESExtractable *
_extract (GESAsset * asset, GError ** error)
{
  GESExtractable *effect;

  effect = GES_ASSET_CLASS (ges_effect_asset_parent_class)->extract (asset,
      error);

  if (effect == NULL || (error && *error)) {
    effect = NULL;

    return NULL;
  }

  return effect;
}

static void
ges_effect_asset_init (GESEffectAsset * self)
{
  self->priv = ges_effect_asset_get_instance_private (self);
}

static void
ges_effect_asset_constructed (GObject * object)
{
  _fill_track_type (GES_ASSET (object));
}

static void
ges_effect_asset_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (ges_effect_asset_parent_class)->finalize (object);
}

static void
ges_effect_asset_class_init (GESEffectAssetClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESAssetClass *asset_class = GES_ASSET_CLASS (klass);

  object_class->finalize = ges_effect_asset_finalize;
  object_class->constructed = ges_effect_asset_constructed;
  asset_class->extract = _extract;
}

gchar *
ges_effect_assect_id_get_type_and_bindesc (const char *id,
    GESTrackType * track_type, GError ** error)
{
  GList *tmp;
  GstElement *effect;
  gchar **typebin_desc = NULL;
  gchar *bindesc = NULL;

  *track_type = GES_TRACK_TYPE_UNKNOWN;
  typebin_desc = g_strsplit (id, " ", 2);
  if (!g_strcmp0 (typebin_desc[0], "audio")) {
    *track_type = GES_TRACK_TYPE_AUDIO;
    bindesc = g_strdup (typebin_desc[1]);
  } else if (!g_strcmp0 (typebin_desc[0], "video")) {
    *track_type = GES_TRACK_TYPE_VIDEO;
    bindesc = g_strdup (typebin_desc[1]);
  } else {
    bindesc = g_strdup (id);
  }

  g_strfreev (typebin_desc);

  effect = gst_parse_bin_from_description (bindesc, TRUE, error);
  if (effect == NULL) {
    g_free (bindesc);

    GST_ERROR ("Could not create element from: %s", id);

    return NULL;
  }

  if (*track_type != GES_TRACK_TYPE_UNKNOWN) {
    gst_object_unref (effect);

    return bindesc;
  }

  for (tmp = GST_BIN_CHILDREN (effect); tmp; tmp = tmp->next) {
    GstElementFactory *factory =
        gst_element_get_factory (GST_ELEMENT (tmp->data));
    const gchar *klass =
        gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);

    if (g_strrstr (klass, "Effect") || g_strrstr (klass, "Filter")) {
      if (g_strrstr (klass, "Audio")) {
        *track_type = GES_TRACK_TYPE_AUDIO;
        break;
      } else if (g_strrstr (klass, "Video")) {
        *track_type = GES_TRACK_TYPE_VIDEO;
        break;
      }
    }
  }

  gst_object_unref (effect);

  if (*track_type == GES_TRACK_TYPE_UNKNOWN) {
    *track_type = GES_TRACK_TYPE_VIDEO;
    GST_ERROR ("Could not determine track type for %s, defaulting to video",
        id);
  }

  return bindesc;
}
