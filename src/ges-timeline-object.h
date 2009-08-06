/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#ifndef _GES_TIMELINE_OBJECT
#define _GES_TIMELINE_OBJECT

#include <glib-object.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_OBJECT ges_timeline_object_get_type()

#define GES_TIMELINE_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_OBJECT, GESTimelineObject))

#define GES_TIMELINE_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_OBJECT, GESTimelineObjectClass))

#define GES_IS_TIMELINE_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_OBJECT))

#define GES_IS_TIMELINE_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_OBJECT))

#define GES_TIMELINE_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_OBJECT, GESTimelineObjectClass))

typedef struct _GESTimelineObject GESTimelineObject;
typedef struct _GESTimelineObjectClass GESTimelineObjectClass;

struct _GESTimelineObject {
  GObject parent;

  /* start, inpoint, duration and fullduration are in nanoseconds */
  guint64 start;	/* position (in time) of the object in the layer */
  guint64 inpoint;	/* in-point */
  guint64 duration;	/* duration of the object used in the layer */
  guint32 priority;	/* priority of the object in the layer (0:top priority) */

  guint64 fullduration; /* Full usable duration of the object (-1: no duration) */
};

struct _GESTimelineObjectClass {
  GObjectClass parent_class;
};

GType ges_timeline_object_get_type (void);

GESTimelineObject* ges_timeline_object_new (void);

GESTrackObject * ges_timeline_object_create_track_object (GESTimelineObject * object,
							  GESTrack * track);

G_END_DECLS

#endif /* _GES_TIMELINE_OBJECT */

