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
#include <ges/ges-track.h>

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

typedef struct _GESTimelineObjectPrivate GESTimelineObjectPrivate;

/**
 * GESFillTrackObjectFunc:
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
typedef gboolean (*GESFillTrackObjectFunc) (GESTimelineObject * object,
                    GESTrackObject * trobject,
                    GstElement * gnlobj);

/**
 * GESCreateTrackObjectFunc:
 * @object: a #GESTimelineObject
 * @track: a #GESTrack
 *
 * Creates the 'primary' track object for this @object.
 *
 * Subclasses should implement this method if they only provide a
 * single #GESTrackObject per track.
 *
 * If the subclass needs to create more than one #GESTrackObject for a
 * given track, then it should implement the 'create_track_objects'
 * method instead.
 *
 * The implementer of this function shall return the proper #GESTrackObject
 * that should be controlled by @object for the given @track.
 *
 * The returned #GESTrackObject will be automatically added to the list
 * of objects controlled by the #GESTimelineObject.
 *
 * Returns: the #GESTrackObject to be used, or %NULL if it can't provide one
 * for the given @track.
 */
typedef GESTrackObject* (*GESCreateTrackObjectFunc) (GESTimelineObject * object,
                         GESTrack * track);

/**
 * GESCreateTrackObjectsFunc:
 * @object: a #GESTimelineObject
 * @track: a #GESTrack
 *
 * Create all track objects this object handles for this type of track.
 *
 * Subclasses should implement this method if they potentially need to
 * return more than one #GESTrackObject(s) for a given #GESTrack.
 *
 * For each object created, the subclass must call
 * ges_timeline_object_add_track_object() with the newly created object
 * and provided @track.
 *
 * Returns: %TRUE on success %FALSE on failure.
 */
typedef gboolean (*GESCreateTrackObjectsFunc) (GESTimelineObject * object,
                                            GESTrack *track);

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
 * The priority of the object.
 */
#define GES_TIMELINE_OBJECT_PRIORITY(obj) (((GESTimelineObject*)obj)->priority)

/**
 * GES_TIMELINE_OBJECT_HEIGHT:
 * @obj: a #GESTimelineObject
 *
 * The span of priorities this object occupies.
 */
#define GES_TIMELINE_OBJECT_HEIGHT(obj) (((GESTimelineObject*)obj)->height)

/**
 * GESTimelineObject:
 *
 * The #GESTimelineObject base class.
 */
struct _GESTimelineObject {
  /*< private >*/
  GInitiallyUnowned parent;

  GESTimelineObjectPrivate *priv;
  
  /* We don't add those properties to the priv struct for optimization purposes
   * start, inpoint, duration and fullduration are in nanoseconds */
  guint64 start;    /* position (in time) of the object in the layer */
  guint64 inpoint;  /* in-point */
  guint64 duration; /* duration of the object used in the layer */
  guint32 priority; /* priority of the object in the layer (0:top priority) */
  guint32 height;       /* the span of priorities this object needs */

  guint64 fullduration; /* Full usable duration of the object (-1: no duration) */

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineObjectClass:
 * @create_track_object: method to create a single #GESTrackObject for a given #GESTrack.
 * @create_track_objects: method to create multiple #GESTrackObjects for a
 * #GESTrack.
 * @fill_track_object: method to fill an associated #GESTrackObject.
 * @need_fill_track: Set to TRUE if @fill_track_object needs to be called.
 * @track_object_added: Should be overridden by subclasses if they need to perform an
 * operation when a #GESTrackObject is added. Since: 0.10.2
 * @track_object_released: Should be overridden by subclassed if they need to perform
 * action when a #GESTrackObject is released. Since: 0.10.2
 *
 * Subclasses can override the @create_track_object and @fill_track_object methods.
 */
struct _GESTimelineObjectClass {
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/
  GESCreateTrackObjectFunc create_track_object;
  GESCreateTrackObjectsFunc create_track_objects;

  /* FIXME : might need a release_track_object */
  GESFillTrackObjectFunc  fill_track_object;
  gboolean need_fill_track;

  void (*track_object_added)    (GESTimelineObject *object,
                                GESTrackObject *tck_object);
  void (*track_object_released) (GESTimelineObject *object,
                                GESTrackObject *tck_object);

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING - 2];
};

GType ges_timeline_object_get_type (void);

/* Setters */
void ges_timeline_object_set_start    (GESTimelineObject * object,
				       guint64 start);
void ges_timeline_object_set_inpoint  (GESTimelineObject * object,
				       guint64 inpoint);
void ges_timeline_object_set_duration (GESTimelineObject * object,
				       guint64 duration);
void ges_timeline_object_set_priority (GESTimelineObject * object,
				       guint priority);

void ges_timeline_object_set_layer    (GESTimelineObject * object,
				       GESTimelineLayer * layer);

/* TrackObject handling */
GESTrackObject *
ges_timeline_object_create_track_object  (GESTimelineObject * object,
					  GESTrack * track);

gboolean
ges_timeline_object_create_track_objects (GESTimelineObject * object,
					  GESTrack * track);

gboolean
ges_timeline_object_release_track_object (GESTimelineObject * object,
					  GESTrackObject * trackobject);

gboolean
ges_timeline_object_fill_track_object    (GESTimelineObject * object,
					  GESTrackObject * trackobj,
					  GstElement * gnlobj);

GESTrackObject *
ges_timeline_object_find_track_object    (GESTimelineObject * object,
					  GESTrack * track, GType type);

GList *
ges_timeline_object_get_track_objects    (GESTimelineObject *object);

gboolean
ges_timeline_object_add_track_object     (GESTimelineObject *object,
					  GESTrackObject *trobj);

/* Layer */
GESTimelineLayer *
ges_timeline_object_get_layer            (GESTimelineObject *object);

gboolean
ges_timeline_object_move_to_layer        (GESTimelineObject *object,
					     GESTimelineLayer *layer);

gboolean
ges_timeline_object_is_moving_from_layer (GESTimelineObject *object);

void
ges_timeline_object_set_moving_from_layer (GESTimelineObject * object,
					     gboolean is_moving);

/* Effects */
GList *
ges_timeline_object_get_top_effects      (GESTimelineObject *object);

gint 
ges_timeline_object_get_top_effect_position (GESTimelineObject *object,
					     GESTrackEffect *effect);

gboolean
ges_timeline_object_set_top_effect_priority (GESTimelineObject *object,
					     GESTrackEffect *effect,
					     guint newpriority);

GESTrackType
ges_timeline_object_get_supported_formats (GESTimelineObject * self);

void
ges_timeline_object_set_supported_formats (GESTimelineObject * self,
					    GESTrackType supportedformats);

GESTimelineObject *
ges_timeline_object_split(GESTimelineObject * ref_object, gint64 position);

G_END_DECLS

#endif /* _GES_TIMELINE_OBJECT */

