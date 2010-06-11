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

#ifndef _GES_TL_BACKGROUNDSOURCE
#define _GES_TL_BACKGROUNDSOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-timeline-source.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_BACKGROUND_SOURCE ges_tl_bg_src_get_type()

#define GES_TIMELINE_BACKGROUND_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_BACKGROUND_SOURCE, GESTimelineBackgroundSource))

#define GES_TIMELINE_BACKGROUND_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_BACKGROUND_SOURCE, GESTimelineBackgroundSourceClass))

#define GES_IS_TIMELINE_BACKGROUND_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_BACKGROUND_SOURCE))

#define GES_IS_TIMELINE_BACKGROUND_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_BACKGROUND_SOURCE))

#define GES_TIMELINE_BACKGROUND_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_BACKGROUND_SOURCE, GESTimelineBackgroundSourceClass))

/**
 * GESTimelineSource:
 * 
 */

struct _GESTimelineBackgroundSource {
  GESTimelineSource parent;
  gboolean mute;

  /*< private >*/
  gint vpattern;
};

/**
 * GESTimelineBackgroundSourceClass:
 * @parent_class: parent class
 */

struct _GESTimelineBackgroundSourceClass {
  GESTimelineSourceClass parent_class;

  /*< public >*/
};

GType ges_tl_bg_src_get_type (void);

GESTimelineBackgroundSource* ges_timeline_background_source_new (void);
GESTimelineBackgroundSource* ges_timeline_background_source_new_for_nick(gchar
    * nick);

G_END_DECLS

#endif /* _GES_TL_BACKGROUNDSOURCE */

