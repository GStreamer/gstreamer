/* GStreamer Editing Services
 * Copyright (C) 2009 Brandon Lewis <brandon.lewis@collabora.co.uk>
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
#include <ges/ges-overlay-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS
#define GES_TYPE_OVERLAY_TEXT_CLIP ges_text_overlay_clip_get_type()
GES_DECLARE_TYPE(TextOverlayClip, text_overlay_clip, OVERLAY_TEXT_CLIP);

/**
 * GESTextOverlayClip:
 */

struct _GESTextOverlayClip
{
  GESOverlayClip parent;

  /*< private > */
  GESTextOverlayClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTextOverlayClipClass:
 */

struct _GESTextOverlayClipClass
{
  /*< private > */

  GESOverlayClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API void
ges_text_overlay_clip_set_text (GESTextOverlayClip * self,
    const gchar * text);

GES_API void
ges_text_overlay_clip_set_font_desc (GESTextOverlayClip * self,
    const gchar * font_desc);

GES_API void
ges_text_overlay_clip_set_valign (GESTextOverlayClip * self,
    GESTextVAlign valign);

GES_API void
ges_text_overlay_clip_set_halign (GESTextOverlayClip * self,
    GESTextHAlign halign);

GES_API void
ges_text_overlay_clip_set_color (GESTextOverlayClip * self,
    guint32 color);

GES_API void
ges_text_overlay_clip_set_xpos (GESTextOverlayClip * self,
    gdouble position);

GES_API void
ges_text_overlay_clip_set_ypos (GESTextOverlayClip * self,
    gdouble position);

GES_API
const gchar *ges_text_overlay_clip_get_text (GESTextOverlayClip * self);

GES_API
const gchar *ges_text_overlay_clip_get_font_desc (GESTextOverlayClip *
    self);

GES_API GESTextVAlign
ges_text_overlay_clip_get_valignment (GESTextOverlayClip * self);

GES_API const guint32
ges_text_overlay_clip_get_color (GESTextOverlayClip * self);

GES_API const gdouble
ges_text_overlay_clip_get_xpos (GESTextOverlayClip * self);

GES_API const gdouble
ges_text_overlay_clip_get_ypos (GESTextOverlayClip * self);

GES_API GESTextHAlign
ges_text_overlay_clip_get_halignment (GESTextOverlayClip * self);

GES_API
GESTextOverlayClip *ges_text_overlay_clip_new (void) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
