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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GES_TRACK_VIDEO_TITLE_SOURCE
#define _GES_TRACK_VIDEO_TITLE_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-title-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_VIDEO_TITLE_SOURCE ges_track_video_title_src_get_type()

#define GES_TRACK_VIDEO_TITLE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_VIDEO_TITLE_SOURCE, GESTrackVideoTitleSource))

#define GES_TRACK_VIDEO_TITLE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_VIDEO_TITLE_SOURCE, GESTrackVideoTitleSourceClass))

#define GES_IS_TRACK_VIDEO_TITLE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_VIDEO_TITLE_SOURCE))

#define GES_IS_TRACK_VIDEO_TITLE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_VIDEO_TITLE_SOURCE))

#define GES_TRACK_VIDEO_TITLE_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_VIDEO_TITLE_SOURCE, GESTrackVideoTitleSourceClass))

#define DEFAULT_FONT_DESC "serif 36"


/** 
 * GESTrackVideoTitleSource:
 * @parent: parent
 *
 */
struct _GESTrackVideoTitleSource {
  GESTrackTitleSource parent;

  /*< private >*/
  gchar         *text;
  gchar         *font_desc;
  GstElement    *text_el;
  GstElement    *background_el;
};

/**
 * GESTrackVideoTitleSourceClass:
 * @parent_class: parent class
 */

struct _GESTrackVideoTitleSourceClass {
  GESTrackTitleSourceClass parent_class;

  /*< private >*/
};

GType ges_track_video_title_src_get_type (void);

void ges_track_video_title_source_set_text(GESTrackVideoTitleSource *, const
    gchar *);
void ges_track_video_title_source_set_font_desc(GESTrackVideoTitleSource *,
    const gchar *);
GESTrackVideoTitleSource* ges_track_video_title_source_new (void);

G_END_DECLS

#endif /* _GES_TRACK_VIDEO_TITLE_SOURCE */

