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

#ifndef _GES_TIMELINE_OBJECT
#define _GES_TIMELINE_OBJECT

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>

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

/**
 * FillTrackObjectFunc:
 * @object: the #GESTimelineObject controlling the track object
 * @trobject: the #GESTrackObject
 * @gnlobj: the GNonLin object that needs to be filled.
 *
 * A function that will be called when the GNonLin object of a corresponding
 * track object needs to be filled.
 *
 * The implementer of this function shall add the proper #GstElement to @gnlobj
 * using gst_bin_add().
 *
 * Returns: TRUE if the implementer succesfully filled the @gnlobj, else #FALSE.
 */
typedef gboolean (*FillTrackObjectFunc) (GESTimelineObject * object,
					 GESTrackObject * trobject,
					 GstElement * gnlobj);


/**
 * GES_TIMELINE_OBJECT_START:
 * @obj: a #GESTimelineObject
 *
 * The start position of the object (in nanoseconds).
 */
#define GES_TIMELINE_OBJECT_START(obj) (((GESTimelineObject*)obj)->start)

/**
 * GES_TIMELINE_OBJECT_INPOINT:
 * @obj: a #GESTimelineObject
 *
 * The in-point of the object (in nanoseconds).
 */
#define GES_TIMELINE_OBJECT_INPOINT(obj) (((GESTimelineObject*)obj)->inpoint)

/**
 * GES_TIMELINE_OBJECT_DURATION:
 * @obj: a #GESTimelineObject
 *
 * The duration position of the object (in nanoseconds).
 */
#define GES_TIMELINE_OBJECT_DURATION(obj) (((GESTimelineObject*)obj)->duration)

/**
 * GES_TIMELINE_OBJECT_PRIORITY:
 * @obj: a #GESTimelineObject
 *
 * The priority of the object (in nanoseconds).
 */
#define GES_TIMELINE_OBJECT_PRIORITY(obj) (((GESTimelineObject*)obj)->priority)

/**
 * GESTimelineObject:
 * @layer: the #GESTImelineLayer where this object is being used.
 *
 * The GESTimelineObject subclass. Subclasses can access these fields.
 */
struct _GESTimelineObject {
  GObject parent;

  /*< public >*/
  GESTimelineLayer * layer;

  /*< private >*/
  GList *trackobjects;	/* A list of TrackObject controlled by this TimelineObject */

  /* start, inpoint, duration and fullduration are in nanoseconds */
  guint64 start;	/* position (in time) of the object in the layer */
  guint64 inpoint;	/* in-point */
  guint64 duration;	/* duration of the object used in the layer */
  guint32 priority;	/* priority of the object in the layer (0:top priority) */

  guint64 fullduration; /* Full usable duration of the object (-1: no duration) */
};

/**
 * GESTimelineObjectClass:
 * @parent_class: object parent class
 * @create_track_object: method to create a #GESTrackObject for a given #GESTrack.
 * @fill_track_object: method to fill an associated #GESTrackObject.
 * @need_fill_track: Set to TRUE if @fill_track_object needs to be called.
 *
 * Subclasses can override the @create_track_object and @fill_track_object methods.
 */
struct _GESTimelineObjectClass {
  GObjectClass parent_class;

  GESTrackObject*	(*create_track_object)	(GESTimelineObject * object,
						 GESTrack * track);
  /* FIXME : might need a release_track_object */
  FillTrackObjectFunc	fill_track_object;
  gboolean need_fill_track;
};

GType ges_timeline_object_get_type (void);

void ges_timeline_object_set_start (GESTimelineObject * object, guint64 start);
void ges_timeline_object_set_inpoint (GESTimelineObject * object, guint64 inpoint);
void ges_timeline_object_set_duration (GESTimelineObject * object, guint64 duration);
void ges_timeline_object_set_priority (GESTimelineObject * object, guint priority);

void ges_timeline_object_set_layer (GESTimelineObject * object,
				    GESTimelineLayer * layer);

GESTrackObject *
ges_timeline_object_create_track_object (GESTimelineObject * object,
					 GESTrack * track);

gboolean
ges_timeline_object_release_track_object (GESTimelineObject * object,
					  GESTrackObject * trackobject);

gboolean
ges_timeline_object_fill_track_object (GESTimelineObject * object,
				       GESTrackObject * trackobj,
				       GstElement * gnlobj);

GESTrackObject *
ges_timeline_object_find_track_object (GESTimelineObject * object,
				       GESTrack * track);

G_END_DECLS

#endif /* _GES_TIMELINE_OBJECT */

