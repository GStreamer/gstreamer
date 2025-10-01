/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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
#include <ges/ges-video-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TITLE_SOURCE ges_title_source_get_type()
GES_DECLARE_TYPE(TitleSource, title_source, TITLE_SOURCE);

/**
 * GESTitleSource:
 *
 * ## Children Properties
 *
 *  {{ libs/GESTitleSource-children-props.md }}
 */
struct _GESTitleSource {
  GESVideoSource parent;

  /*< private >*/
  GESTitleSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTitleSourceClass:
 * @parent_class: parent class
 */
struct _GESTitleSourceClass {
  GESVideoSourceClass parent_class;

  /*< private >*/

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING - 1];
};

GES_API
void ges_title_source_set_text (GESTitleSource *self,
				     const gchar *text);

GES_API
void ges_title_source_set_font_desc (GESTitleSource *self,
					  const gchar *font_desc);

GES_API
void ges_title_source_set_halignment (GESTitleSource *self,
					   GESTextHAlign halign);

GES_API
void ges_title_source_set_valignment (GESTitleSource *self,
					   GESTextVAlign valign);

GES_API
void ges_title_source_set_text_color (GESTitleSource *self,
					   guint32 color);
GES_API
void ges_title_source_set_background_color (GESTitleSource *self,
					   guint32 color);
GES_API
void ges_title_source_set_xpos (GESTitleSource *self,
					   gdouble position);
GES_API
void ges_title_source_set_ypos (GESTitleSource *self,
					   gdouble position);

GES_API
const gchar *ges_title_source_get_text (GESTitleSource *source) G_GNUC_WARN_UNUSED_RESULT;
GES_API
const gchar *ges_title_source_get_font_desc (GESTitleSource *source) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GESTextHAlign ges_title_source_get_halignment (GESTitleSource *source);
GES_API
GESTextVAlign ges_title_source_get_valignment (GESTitleSource *source);
GES_API
const guint32 ges_title_source_get_text_color (GESTitleSource *source);
GES_API
const guint32 ges_title_source_get_background_color (GESTitleSource *source);
GES_API
const gdouble ges_title_source_get_xpos (GESTitleSource *source);
GES_API
const gdouble ges_title_source_get_ypos (GESTitleSource *source);

G_END_DECLS
