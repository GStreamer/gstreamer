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

#ifndef _GES_TRACK_TEXT_OVERLAY
#define _GES_TRACK_TEXT_OVERLAY

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-title-source.h>
#include <ges/ges-track-operation.h>

G_BEGIN_DECLS
#define GES_TYPE_TRACK_TEXT_OVERLAY ges_track_text_overlay_get_type()
#define GES_TRACK_TEXT_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_TEXT_OVERLAY, GESTrackTextOverlay))
#define GES_TRACK_TEXT_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_TEXT_OVERLAY, GESTrackTextOverlayClass))
#define GES_IS_TRACK_TEXT_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_TEXT_OVERLAY))
#define GES_IS_TRACK_TEXT_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_TEXT_OVERLAY))
#define GES_TRACK_TEXT_OVERLAY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_TEXT_OVERLAY, GESTrackTextOverlayClass))
typedef struct _GESTrackTextOverlayPrivate GESTrackTextOverlayPrivate;

/**
 * GESTrackTextOverlay:
 */
struct _GESTrackTextOverlay
{
  GESTrackOperation parent;

  /*< private > */
  GESTrackTextOverlayPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESTrackTextOverlayClass
{
  GESTrackOperationClass parent_class;

  /*< private > */

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_track_text_overlay_get_type (void);

void ges_track_text_overlay_set_text (GESTrackTextOverlay * self,
    const gchar * text);
void ges_track_text_overlay_set_font_desc (GESTrackTextOverlay * self,
    const gchar * font_desc);

void ges_track_text_overlay_set_halignment (GESTrackTextOverlay * self,
    GESTextHAlign halign);

void ges_track_text_overlay_set_valignment (GESTrackTextOverlay * self,
    GESTextVAlign valign);
void ges_track_text_overlay_set_color (GESTrackTextOverlay * self,
    guint32 color);
void ges_track_text_overlay_set_xpos (GESTrackTextOverlay * self,
    gdouble position);
void ges_track_text_overlay_set_ypos (GESTrackTextOverlay * self,
    gdouble position);

const gchar *ges_track_text_overlay_get_text (GESTrackTextOverlay * self);
const char *ges_track_text_overlay_get_font_desc (GESTrackTextOverlay * self);
GESTextHAlign ges_track_text_overlay_get_halignment (GESTrackTextOverlay *
    self);
GESTextVAlign ges_track_text_overlay_get_valignment (GESTrackTextOverlay *
    self);
const guint32 ges_track_text_overlay_get_color (GESTrackTextOverlay * self);
const gdouble ges_track_text_overlay_get_xpos (GESTrackTextOverlay * self);
const gdouble ges_track_text_overlay_get_ypos (GESTrackTextOverlay * self);

GESTrackTextOverlay *ges_track_text_overlay_new (void);

G_END_DECLS
#endif /* _GES_TRACK_TEXT_OVERLAY */
