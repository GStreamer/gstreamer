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

#ifndef _GES_TIMELINE_TEXT_OVERLAY
#define _GES_TIMELINE_TEXT_OVERLAY

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-overlay-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS
#define GES_TYPE_TIMELINE_TEXT_OVERLAY ges_timeline_text_overlay_get_type()
#define GES_TIMELINE_TEXT_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_TEXT_OVERLAY, GESTimelineTextOverlay))
#define GES_TIMELINE_TEXT_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_TEXT_OVERLAY, GESTimelineTextOverlayClass))
#define GES_IS_TIMELINE_TEXT_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_TEXT_OVERLAY))
#define GES_IS_TIMELINE_TEXT_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_TEXT_OVERLAY))
#define GES_TIMELINE_TEXT_OVERLAY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_TEXT_OVERLAY, GESTimelineTextOverlayClass))
typedef struct _GESTimelineTextOverlayPrivate GESTimelineTextOverlayPrivate;

/**
 * GESTimelineTextOverlay:
 * 
 */

struct _GESTimelineTextOverlay
{
  GESOverlayClip parent;

  /*< private > */
  GESTimelineTextOverlayPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineTextOverlayClass:
 */

struct _GESTimelineTextOverlayClass
{
  /*< private > */

  GESOverlayClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_text_overlay_get_type (void);

void
ges_timeline_text_overlay_set_text (GESTimelineTextOverlay * self,
    const gchar * text);

void
ges_timeline_text_overlay_set_font_desc (GESTimelineTextOverlay * self,
    const gchar * font_desc);

void
ges_timeline_text_overlay_set_valign (GESTimelineTextOverlay * self,
    GESTextVAlign valign);

void
ges_timeline_text_overlay_set_halign (GESTimelineTextOverlay * self,
    GESTextHAlign halign);

void
ges_timeline_text_overlay_set_color (GESTimelineTextOverlay * self,
    guint32 color);

void
ges_timeline_text_overlay_set_xpos (GESTimelineTextOverlay * self,
    gdouble position);

void
ges_timeline_text_overlay_set_ypos (GESTimelineTextOverlay * self,
    gdouble position);

const gchar *ges_timeline_text_overlay_get_text (GESTimelineTextOverlay * self);

const gchar *ges_timeline_text_overlay_get_font_desc (GESTimelineTextOverlay *
    self);

GESTextVAlign
ges_timeline_text_overlay_get_valignment (GESTimelineTextOverlay * self);

const guint32
ges_timeline_text_overlay_get_color (GESTimelineTextOverlay * self);

const gdouble
ges_timeline_text_overlay_get_xpos (GESTimelineTextOverlay * self);

const gdouble
ges_timeline_text_overlay_get_ypos (GESTimelineTextOverlay * self);

GESTextHAlign
ges_timeline_text_overlay_get_halignment (GESTimelineTextOverlay * self);

GESTimelineTextOverlay *ges_timeline_text_overlay_new (void);

G_END_DECLS
#endif /* _GES_TL_OVERLAY */
