/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
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

#ifndef _GES_TRACK_OBJECT
#define _GES_TRACK_OBJECT

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-timeline-object.h>
#include <ges/ges-track.h>

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

/**
 * GES_TRACK_OBJECT_START:
 * @obj: a #GESTrackObject
 *
 * The start position of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_START(obj) (((GESTrackObject*)obj)->start)

/**
 * GES_TRACK_OBJECT_INPOINT:
 * @obj: a #GESTrackObject
 *
 * The in-point of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_INPOINT(obj) (((GESTrackObject*)obj)->inpoint)

/**
 * GES_TRACK_OBJECT_DURATION:
 * @obj: a #GESTrackObject
 *
 * The duration position of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_DURATION(obj) (((GESTrackObject*)obj)->duration)

/**
 * GES_TRACK_OBJECT_PRIORITY:
 * @obj: a #GESTrackObject
 *
 * The priority of the object (in nanoseconds).
 */
#define GES_TRACK_OBJECT_PRIORITY(obj) (((GESTrackObject*)obj)->priority)

/**
 * GESTrackObject:
 * @timelineobj: The #GESTimelineObject to which this object belongs.
 * @track: The #GESTrack in which this object is.
 * @valid: #TRUE if the content of the @gnlobject is valid.
 * @start: Position (in nanoseconds) of the object the track.
 * @inpoint: in-point (in nanoseconds) of the object in the track.
 * @duration: Duration of the object
 * @priority: Priority of the object in the track (0:top priority)
 * @gnlobject: The GNonLin object this object is controlling.
 *
 * The GESTrackObject base class. Only sub-classes can access these fields.
 */
struct _GESTrackObject {
  GObject parent;

  /*< public >*/
  GESTimelineObject *timelineobj;
  GESTrack *track;

  gboolean valid;

  /* Cached values of the gnlobject properties */
  guint64 start;
  guint64 inpoint;
  guint64 duration;
  guint32 priority;
  gboolean active;

  GstElement *gnlobject;
};

/**
 * GESTrackObjectClass:
 * @create_gnl_object: method to create the GNonLin container object.
 *
 * Subclasses can override the @create_gnl_object method to override what type
 * of GNonLin object will be created.
 */ 
struct _GESTrackObjectClass {
  GObjectClass parent_class;

  /*< private >*/
  /* signals */
  void	(*changed)	(GESTrackObject * object);

  /*< public >*/
  /* virtual methods for subclasses */
  gboolean (*create_gnl_object) (GESTrackObject * object);
};

GType ges_track_object_get_type (void);

gboolean ges_track_object_set_track (GESTrackObject * object, GESTrack * track);
void ges_track_object_set_timeline_object (GESTrackObject * object, GESTimelineObject * tlobject);

/* Private methods for GESTimelineObject's usage only */
gboolean ges_track_object_set_start_internal (GESTrackObject * object, guint64 start);
gboolean ges_track_object_set_inpoint_internal (GESTrackObject * object, guint64 inpoint);
gboolean ges_track_object_set_duration_internal (GESTrackObject * object, guint64 duration);
gboolean ges_track_object_set_priority_internal (GESTrackObject * object, guint32 priority);
gboolean ges_track_object_set_active (GESTrackObject * object, gboolean active);
G_END_DECLS

#endif /* _GES_TRACK_OBJECT */
