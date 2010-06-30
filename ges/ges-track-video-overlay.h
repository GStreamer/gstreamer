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

#ifndef _GES_TRACK_VIDEO_OVERLAY
#define _GES_TRACK_VIDEO_OVERLAY

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-title-source.h>
#include <ges/ges-track-overlay.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_VIDEO_OVERLAY ges_track_video_overlay_get_type()

#define GES_TRACK_VIDEO_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_VIDEO_OVERLAY, GESTrackVideoOverlay))

#define GES_TRACK_VIDEO_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_VIDEO_OVERLAY, GESTrackVideoOverlayClass))

#define GES_IS_TRACK_VIDEO_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_VIDEO_OVERLAY))

#define GES_IS_TRACK_VIDEO_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_VIDEO_OVERLAY))

#define GES_TRACK_VIDEO_OVERLAY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_VIDEO_OVERLAY, GESTrackVideoOverlayClass))

/** 
 * GESTrackVideoOverlay:
 * @parent: parent
 *
 */
struct _GESTrackVideoOverlay {
  GESTrackOverlay parent;

  /*< private >*/
  gchar         *text;
  gchar         *font_desc;
  gint          halign;
  gint          valign;
  GstElement    *text_el;
};

/**
 * GESTrackVideoOverlayClass:
 * @parent_class: parent class
 */

struct _GESTrackVideoOverlayClass {
  GESTrackOverlayClass parent_class;

  /*< private >*/
};

GType ges_track_video_overlay_get_type (void);

void ges_track_video_overlay_set_text(GESTrackVideoOverlay *self, const
    gchar *text);

void ges_track_video_overlay_set_font_desc(GESTrackVideoOverlay *self,
    const gchar *font_desc);

void ges_track_video_overlay_set_halignment(GESTrackVideoOverlay
    *self, GESTrackTitleSrcHAlign halgn);

void ges_track_video_overlay_set_valignment(GESTrackVideoOverlay
    *self, GESTrackTitleSrcVAlign valign);

GESTrackVideoOverlay* ges_track_video_overlay_new (void);

G_END_DECLS

#endif /* _GES_TRACK_VIDEO_OVERLAY */

