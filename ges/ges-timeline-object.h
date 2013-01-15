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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_TIMELINE_OBJECT
#define _GES_TIMELINE_OBJECT

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-timeline-element.h>
#include <ges/ges-types.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_OBJECT             ges_timeline_object_get_type()
#define GES_TIMELINE_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_OBJECT, GESTimelineObject))
#define GES_TIMELINE_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_OBJECT, GESTimelineObjectClass))
#define GES_IS_TIMELINE_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_OBJECT))
#define GES_IS_TIMELINE_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_OBJECT))
#define GES_TIMELINE_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_OBJECT, GESTimelineObjectClass))

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
typedef gboolean (*GESFillTrackObjectFunc) (GESTimelineObject *object,
                                            GESTrackObject *trobject,
                                            GstElement *gnlobj);

/**
 * GESCreateTrackObjectFunc:
 * @object: a #GESTimelineObject
 * @type: a #GESTrackType
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
typedef GESTrackObject *(*GESCreateTrackObjectFunc) (GESTimelineObject * object,
                                                     GESTrackType type);

/**
 * GESCreateTrackObjectsFunc:
 * @object: a #GESTimelineObject
 * @type: a #GESTrackType
 *
 * Create all track objects this object handles for this type of track.
 *
 * Subclasses should implement this method if they potentially need to
 * return more than one #GESTrackObject(s) for a given #GESTrack.
 *
 * For each object created, the subclass must call
 * ges_timeline_object_add_track_object() with the newly created object
 * and provided @type.
 *
 * Returns: %TRUE on success %FALSE on failure.
 */

typedef GList * (*GESCreateTrackObjectsFunc) (GESTimelineObject * object, GESTrackType type);

/**
 * GES_TIMELINE_OBJECT_HEIGHT:
 * @obj: a #GESTimelineObject
 *
 * The span of priorities this object occupies.
 */
#define GES_TIMELINE_OBJECT_HEIGHT(obj) (((GESTimelineObject*)obj)->height)

/**
 * GESTimelineObject:
 * @trackobjects: (element-type GES.TrackObject): A list of TrackObject
 * controlled by this TimelineObject sorted by priority. NOTE: Do not modify.
 *
 * The #GESTimelineObject base class.
 */
struct _GESTimelineObject
{
  GESTimelineElement parent;

  /*< readonly >*/
  GList *trackobjects;

  /* We don't add those properties to the priv struct for optimization purposes
   * start, inpoint, duration and fullduration are in nanoseconds */
  guint32 height;               /* the span of priorities this object needs */
  guint64 fullduration;         /* Full usable duration of the object (-1: no duration) */

  /*< private >*/
  GESTimelineObjectPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

/**
 * GESTimelineObjectClass:
 * @create_track_object: method to create a single #GESTrackObject for a given #GESTrack.
 * @create_track_objects: method to create multiple #GESTrackObjects for a
 * #GESTrack.
 * @fill_track_object: method to fill an associated #GESTrackObject.
 * @need_fill_track: Set to TRUE if @fill_track_object needs to be called.
 * @snaps: Set to %TRUE if the objects of this type snap with
 *  other objects in a timeline %FALSE otherwise (default is %FALSE). Basically only
 *  sources snap.
 * @track_object_added: Should be overridden by subclasses if they need to perform an
 * operation when a #GESTrackObject is added. Since: 0.10.2
 * @track_object_released: Should be overridden by subclasses if they need to perform
 * action when a #GESTrackObject is released. Since: 0.10.2
 *
 * Subclasses can override the @create_track_object and @fill_track_object methods.
 */
struct _GESTimelineObjectClass
{
  /*< private > */
  GESTimelineElementClass parent_class;

  /*< public > */
  GESCreateTrackObjectFunc create_track_object;
  GESCreateTrackObjectsFunc create_track_objects;

  /* FIXME : might need a release_track_object */
  GESFillTrackObjectFunc fill_track_object;
  gboolean need_fill_track;
  gboolean snaps;

  void (*track_object_added)    (GESTimelineObject *object,
                                GESTrackObject *tck_object);
  void (*track_object_released) (GESTimelineObject *object,
                                GESTrackObject *tck_object);

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

GType ges_timeline_object_get_type (void);

/* Setters */
void ges_timeline_object_set_layer            (GESTimelineObject *object,
                                               GESTimelineLayer  *layer);

/* TrackObject handling */
GList* ges_timeline_object_get_track_objects            (GESTimelineObject *object);
GESTrackType ges_timeline_object_get_supported_formats  (GESTimelineObject *object);
GESTrackObject *ges_timeline_object_create_track_object (GESTimelineObject *object, GESTrackType type);
GList * ges_timeline_object_create_track_objects        (GESTimelineObject *object, GESTrackType type);
gboolean ges_timeline_object_release_track_object       (GESTimelineObject *object, GESTrackObject    *trackobject);
void ges_timeline_object_set_supported_formats          (GESTimelineObject *object, GESTrackType       supportedformats);
gboolean ges_timeline_object_add_asset               (GESTimelineObject *object, GESAsset       *asset);
gboolean ges_timeline_object_add_track_object           (GESTimelineObject *object, GESTrackObject    *trobj);
gboolean ges_timeline_object_fill_track_object          (GESTimelineObject *object, GESTrackObject    *trackobj, GstElement *gnlobj);
GESTrackObject *ges_timeline_object_find_track_object   (GESTimelineObject *object, GESTrack          *track,    GType      type);

/* Layer */
GESTimelineLayer *ges_timeline_object_get_layer   (GESTimelineObject *object);
gboolean ges_timeline_object_is_moving_from_layer (GESTimelineObject *object);
gboolean ges_timeline_object_move_to_layer        (GESTimelineObject *object, GESTimelineLayer  *layer);
void ges_timeline_object_set_moving_from_layer    (GESTimelineObject *object, gboolean is_moving);

/* Effects */
GList* ges_timeline_object_get_top_effects           (GESTimelineObject *object);
gint   ges_timeline_object_get_top_effect_position   (GESTimelineObject *object, GESTrackEffect *effect);
gboolean ges_timeline_object_set_top_effect_priority (GESTimelineObject *object, GESTrackEffect *effect, guint newpriority);

/* Editing */
GESTimelineObject *ges_timeline_object_split  (GESTimelineObject *object, guint64  position);
void ges_timeline_object_objects_set_locked   (GESTimelineObject *object, gboolean locked);

gboolean ges_timeline_object_edit             (GESTimelineObject *object, GList   *layers,
                                               gint  new_layer_priority, GESEditMode mode,
                                               GESEdge edge, guint64 position);

G_END_DECLS
#endif /* _GES_TIMELINE_OBJECT */
