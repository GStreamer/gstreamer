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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GES_TIMELINE_TEXT_OVERLAY
#define _GES_TIMELINE_TEXT_OVERLAY

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-timeline-overlay.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_TEXT_OVERLAY ges_tl_text_overlay_get_type()

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

/**
 * GESTimelineTextOverlay:
 * @parent: parent
 * 
 */

struct _GESTimelineTextOverlay {
  GESTimelineOverlay parent;

  /*< private >*/
  gboolean mute;
  gchar *text;
  gchar *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
};

/**
 * GESTimelineTextOverlayClass:
 * @parent_class: parent class
 */

struct _GESTimelineTextOverlayClass {
  GESTimelineOverlayClass parent_class;

  /*< public >*/
};

GType ges_tl_text_overlay_get_type (void);

GESTimelineTextOverlay* ges_timeline_text_overlay_new (void);

G_END_DECLS

#endif /* _GES_TL_OVERLAY */

