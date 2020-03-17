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

#pragma once

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-operation-clip.h>

G_BEGIN_DECLS

#define GES_TYPE_OVERLAY_CLIP ges_overlay_clip_get_type()
GES_DECLARE_TYPE(OverlayClip, overlay_clip, OVERLAY_CLIP);

/**
 * GESOverlayClip:
 */

struct _GESOverlayClip {
  /*< private >*/
  GESOperationClip parent;

  GESOverlayClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESOverlayClipClass:
 * @parent_class: parent class
 */

struct _GESOverlayClipClass {
  GESOperationClipClass parent_class;

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};
G_END_DECLS
