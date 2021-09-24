/* GStreamer Editing Services
 * Copyright (C) 2020 Igalia S.L
 *     Author: 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION: gessourceclipasset
 * @title: GESSourceClipAsset
 * @short_description: A GESAsset subclass, baseclass for #GESSourceClip-s extraction
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-source-clip-asset.h"
#include "ges-internal.h"

G_DEFINE_TYPE (GESSourceClipAsset, ges_source_clip_asset, GES_TYPE_CLIP_ASSET);

static gboolean
get_natural_framerate (GESClipAsset * self, gint * framerate_n,
    gint * framerate_d)
{
  *framerate_n = DEFAULT_FRAMERATE_N;
  *framerate_d = DEFAULT_FRAMERATE_D;
  return TRUE;
}

static void
ges_source_clip_asset_class_init (GESSourceClipAssetClass * klass)
{
  GES_CLIP_ASSET_CLASS (klass)->get_natural_framerate = get_natural_framerate;
}

static void
ges_source_clip_asset_init (GESSourceClipAsset * self)
{
}
