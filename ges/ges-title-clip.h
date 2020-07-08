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
#include <ges/ges-source-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TITLE_CLIP ges_title_clip_get_type()
GES_DECLARE_TYPE(TitleClip, title_clip, TITLE_CLIP);

/**
 * GESTitleClip:
 *
 * Render stand-alone titles in GESLayer.
 */

struct _GESTitleClip {
  GESSourceClip parent;

  /*< private >*/
  GESTitleClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESTitleClipClass {
  /*< private >*/
  GESSourceClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_text( GESTitleClip * self, const gchar * text);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_font_desc (GESTitleClip * self, const gchar * font_desc);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_valignment (GESTitleClip * self, GESTextVAlign valign);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_halignment (GESTitleClip * self, GESTextHAlign halign);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_color (GESTitleClip * self, guint32 color);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_background (GESTitleClip * self, guint32 background);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_xpos (GESTitleClip * self, gdouble position);

GES_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_ypos (GESTitleClip * self, gdouble position);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const gchar*
ges_title_clip_get_font_desc (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) GESTextVAlign
ges_title_clip_get_valignment (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) GESTextHAlign
ges_title_clip_get_halignment (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const guint32
ges_title_clip_get_text_color (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const guint32
ges_title_clip_get_background_color (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const gdouble
ges_title_clip_get_xpos (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const gdouble
ges_title_clip_get_ypos (GESTitleClip * self);

GES_DEPRECATED_FOR(ges_timeline_element_get_children_properties)
const gchar* ges_title_clip_get_text (GESTitleClip * self);

GES_API
GESTitleClip* ges_title_clip_new (void);

G_END_DECLS
