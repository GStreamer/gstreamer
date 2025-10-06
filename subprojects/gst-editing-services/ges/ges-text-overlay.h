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
#include <ges/ges-title-source.h>
#include <ges/ges-operation.h>

G_BEGIN_DECLS
#define GES_TYPE_TEXT_OVERLAY ges_text_overlay_get_type()
GES_DECLARE_TYPE(TextOverlay, text_overlay, TEXT_OVERLAY);

/**
 * GESTextOverlay:
 */
struct _GESTextOverlay
{
  GESOperation parent;

  /*< private > */
  GESTextOverlayPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESTextOverlayClass
{
  GESOperationClass parent_class;

  /*< private > */

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
void ges_text_overlay_set_text (GESTextOverlay * self,
    const gchar * text);
GES_API
void ges_text_overlay_set_font_desc (GESTextOverlay * self,
    const gchar * font_desc);

GES_API
void ges_text_overlay_set_halignment (GESTextOverlay * self,
    GESTextHAlign halign);

GES_API
void ges_text_overlay_set_valignment (GESTextOverlay * self,
    GESTextVAlign valign);
GES_API
void ges_text_overlay_set_color (GESTextOverlay * self,
    guint32 color);
GES_API
void ges_text_overlay_set_xpos (GESTextOverlay * self,
    gdouble position);
GES_API
void ges_text_overlay_set_ypos (GESTextOverlay * self,
    gdouble position);

GES_DEPRECATED
GESTextOverlay *ges_text_overlay_new (void) G_GNUC_WARN_UNUSED_RESULT;

GES_API
const gchar *ges_text_overlay_get_text (GESTextOverlay * self);
GES_API
const char *ges_text_overlay_get_font_desc (GESTextOverlay * self);
GES_API
GESTextHAlign ges_text_overlay_get_halignment (GESTextOverlay *
    self);
GES_API
GESTextVAlign ges_text_overlay_get_valignment (GESTextOverlay *
    self);
GES_API
const guint32 ges_text_overlay_get_color (GESTextOverlay * self);
GES_API
const gdouble ges_text_overlay_get_xpos (GESTextOverlay * self);
GES_API
const gdouble ges_text_overlay_get_ypos (GESTextOverlay * self);

G_END_DECLS
