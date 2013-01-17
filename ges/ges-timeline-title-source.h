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

#ifndef _GES_TIMELINE_TITLESOURCE
#define _GES_TIMELINE_TITLESOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-source-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_TITLE_SOURCE ges_timeline_title_source_get_type()

#define GES_TIMELINE_TITLE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_TITLE_SOURCE, GESTimelineTitleSource))

#define GES_TIMELINE_TITLE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_TITLE_SOURCE, GESTimelineTitleSourceClass))

#define GES_IS_TIMELINE_TITLE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_TITLE_SOURCE))

#define GES_IS_TIMELINE_TITLE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_TITLE_SOURCE))

#define GES_TIMELINE_TITLE_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_TITLE_SOURCE, GESTimelineTitleSourceClass))

typedef struct _GESTimelineTitleSourcePrivate GESTimelineTitleSourcePrivate;

/**
 * GESTimelineTitleSource:
 *
 * Render stand-alone titles in GESTimelineLayer.
 */

struct _GESTimelineTitleSource {
  GESSourceClip parent;

  /*< private >*/
  GESTimelineTitleSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESTimelineTitleSourceClass {
  /*< private >*/
  GESSourceClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_title_source_get_type (void);

void
ges_timeline_title_source_set_mute (GESTimelineTitleSource * self,
    gboolean mute);

void
ges_timeline_title_source_set_text( GESTimelineTitleSource * self,
    const gchar * text);

void
ges_timeline_title_source_set_font_desc (GESTimelineTitleSource * self,
    const gchar * font_desc);

void
ges_timeline_title_source_set_valignment (GESTimelineTitleSource * self,
    GESTextVAlign valign);

void
ges_timeline_title_source_set_halignment (GESTimelineTitleSource * self,
    GESTextHAlign halign);

void
ges_timeline_title_source_set_color (GESTimelineTitleSource * self,
    guint32 color);

void
ges_timeline_title_source_set_background (GESTimelineTitleSource * self,
    guint32 background);

void
ges_timeline_title_source_set_xpos (GESTimelineTitleSource * self,
    gdouble position);

void
ges_timeline_title_source_set_ypos (GESTimelineTitleSource * self,
    gdouble position);

const gchar*
ges_timeline_title_source_get_font_desc (GESTimelineTitleSource * self);

GESTextVAlign
ges_timeline_title_source_get_valignment (GESTimelineTitleSource * self);

GESTextHAlign
ges_timeline_title_source_get_halignment (GESTimelineTitleSource * self);

const guint32
ges_timeline_title_source_get_color (GESTimelineTitleSource * self);

const guint32
ges_timeline_title_source_get_background (GESTimelineTitleSource * self);

const gdouble
ges_timeline_title_source_get_xpos (GESTimelineTitleSource * self);

const gdouble
ges_timeline_title_source_get_ypos (GESTimelineTitleSource * self);

gboolean ges_timeline_title_source_is_muted (GESTimelineTitleSource * self);
const gchar* ges_timeline_title_source_get_text (GESTimelineTitleSource * self);

GESTimelineTitleSource* ges_timeline_title_source_new (void);

G_END_DECLS

#endif /* _GES_TIMELINE_TITLESOURCE */

