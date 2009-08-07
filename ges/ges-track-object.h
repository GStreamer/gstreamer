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

#ifndef _GES_TRACK_OBJECT
#define _GES_TRACK_OBJECT

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_OBJECT ges_track_object_get_type()

#define GES_TRACK_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_OBJECT, GESTrackObject))

#define GES_TRACK_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_OBJECT, GESTrackObjectClass))

#define GES_IS_TRACK_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_OBJECT))

#define GES_IS_TRACK_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_OBJECT))

#define GES_TRACK_OBJECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_OBJECT, GESTrackObjectClass))

struct _GESTrackObject {
  GObject parent;

  GESTimelineObject *timelineobj;	/* The associated timeline object */
  GESTrack *track;			/* The associated Track */

  gboolean valid;	/* TRUE if the contents of gnlobject are valid/usable */

  /* Cached values of the gnlobject properties */
  guint64 start;	/* position (in time) of the object in the layer */
  guint64 inpoint;	/* in-point */
  guint64 duration;	/* duration of the object used in the layer */
  guint32 priority;	/* priority of the object in the layer (0:top priority) */

  GstElement *gnlobject;	/* The associated GnlObject */
};

struct _GESTrackObjectClass {
  GObjectClass parent_class;

  /* signal callbacks */
  void	(*changed)	(GESTrackObject * object);

  /* virtual methods */
  gboolean (*create_gnl_object) (GESTrackObject * object);
};

GType ges_track_object_get_type (void);

GESTrackObject* ges_track_object_new (GESTimelineObject *timelineobj, GESTrack *track);

gboolean ges_track_object_set_track (GESTrackObject * object, GESTrack * track);
void ges_track_object_set_timeline_object (GESTrackObject * object, GESTimelineObject * tlobject);

/* Private methods for GESTimelineObject's usage only */
gboolean ges_track_object_set_start_internal (GESTrackObject * object, guint64 start);
gboolean ges_track_object_set_inpoint_internal (GESTrackObject * object, guint64 inpoint);
gboolean ges_track_object_set_duration_internal (GESTrackObject * object, guint64 duration);
gboolean ges_track_object_set_priority_internal (GESTrackObject * object, guint32 priority);

G_END_DECLS

#endif /* _GES_TRACK_OBJECT */
