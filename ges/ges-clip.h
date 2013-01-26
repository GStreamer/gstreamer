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

#ifndef _GES_CLIP
#define _GES_CLIP

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-timeline-element.h>
#include <ges/ges-types.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_CLIP             ges_clip_get_type()
#define GES_CLIP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_CLIP, GESClip))
#define GES_CLIP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_CLIP, GESClipClass))
#define GES_IS_CLIP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_CLIP))
#define GES_IS_CLIP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_CLIP))
#define GES_CLIP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_CLIP, GESClipClass))

typedef struct _GESClipPrivate GESClipPrivate;

/**
 * GESFillTrackElementFunc:
 * @object: the #GESClip controlling the track object
 * @trobject: the #GESTrackElement
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
typedef gboolean (*GESFillTrackElementFunc) (GESClip *object,
                                            GESTrackElement *trobject,
                                            GstElement *gnlobj);

/**
 * GESCreateTrackElementFunc:
 * @object: a #GESClip
 * @type: a #GESTrackType
 *
 * Creates the 'primary' track object for this @object.
 *
 * Subclasses should implement this method if they only provide a
 * single #GESTrackElement per track.
 *
 * If the subclass needs to create more than one #GESTrackElement for a
 * given track, then it should implement the 'create_track_elements'
 * method instead.
 *
 * The implementer of this function shall return the proper #GESTrackElement
 * that should be controlled by @object for the given @track.
 *
 * The returned #GESTrackElement will be automatically added to the list
 * of objects controlled by the #GESClip.
 *
 * Returns: the #GESTrackElement to be used, or %NULL if it can't provide one
 * for the given @track.
 */
typedef GESTrackElement *(*GESCreateTrackElementFunc) (GESClip * object,
                                                     GESTrackType type);

/**
 * GESCreateTrackElementsFunc:
 * @object: a #GESClip
 * @type: a #GESTrackType
 *
 * Create all track objects this object handles for this type of track.
 *
 * Subclasses should implement this method if they potentially need to
 * return more than one #GESTrackElement(s) for a given #GESTrack.
 *
 * For each object created, the subclass must call
 * ges_clip_add_track_element() with the newly created object
 * and provided @type.
 *
 * Returns: %TRUE on success %FALSE on failure.
 */

typedef GList * (*GESCreateTrackElementsFunc) (GESClip * object, GESTrackType type);

/**
 * GES_CLIP_HEIGHT:
 * @obj: a #GESClip
 *
 * The span of priorities this object occupies.
 */
#define GES_CLIP_HEIGHT(obj) (((GESClip*)obj)->height)

/**
 * GESClip:
 * @trackelements: (element-type GES.TrackElement): A list of TrackElement
 * controlled by this Clip sorted by priority. NOTE: Do not modify.
 *
 * The #GESClip base class.
 */
struct _GESClip
{
  GESTimelineElement parent;

  /*< readonly >*/
  GList *trackelements;

  /* We don't add those properties to the priv struct for optimization purposes
   * start, inpoint, duration and fullduration are in nanoseconds */
  guint32 height;               /* the span of priorities this object needs */
  guint64 fullduration;         /* Full usable duration of the object (-1: no duration) */

  /*< private >*/
  GESClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

/**
 * GESClipClass:
 * @create_track_element: method to create a single #GESTrackElement for a given #GESTrack.
 * @create_track_elements: method to create multiple #GESTrackElements for a
 * #GESTrack.
 * @fill_track_element: method to fill an associated #GESTrackElement.
 * @need_fill_track: Set to TRUE if @fill_track_element needs to be called.
 * @snaps: Set to %TRUE if the objects of this type snap with
 *  other objects in a timeline %FALSE otherwise (default is %FALSE). Basically only
 *  sources snap.
 * @track_element_added: Should be overridden by subclasses if they need to perform an
 * operation when a #GESTrackElement is added. Since: 0.10.2
 * @track_element_released: Should be overridden by subclasses if they need to perform
 * action when a #GESTrackElement is released. Since: 0.10.2
 *
 * Subclasses can override the @create_track_element and @fill_track_element methods.
 */
struct _GESClipClass
{
  /*< private > */
  GESTimelineElementClass parent_class;

  /*< public > */
  GESCreateTrackElementFunc create_track_element;
  GESCreateTrackElementsFunc create_track_elements;

  /* FIXME : might need a release_track_element */
  GESFillTrackElementFunc fill_track_element;
  gboolean need_fill_track;
  gboolean snaps;

  void (*track_element_added)    (GESClip *object,
                                GESTrackElement *tck_object);
  void (*track_element_released) (GESClip *object,
                                GESTrackElement *tck_object);

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

GType ges_clip_get_type (void);

/* Setters */
void ges_clip_set_layer            (GESClip *object,
                                               GESTimelineLayer  *layer);

/* TrackElement handling */
GList* ges_clip_get_track_elements            (GESClip *object);
GESTrackType ges_clip_get_supported_formats  (GESClip *object);
GESTrackElement *ges_clip_create_track_element (GESClip *object, GESTrackType type);
GList * ges_clip_create_track_elements        (GESClip *object, GESTrackType type);
gboolean ges_clip_release_track_element       (GESClip *object, GESTrackElement    *trackelement);
void ges_clip_set_supported_formats          (GESClip *object, GESTrackType       supportedformats);
gboolean ges_clip_add_asset               (GESClip *object, GESAsset       *asset);
gboolean ges_clip_add_track_element           (GESClip *object, GESTrackElement    *trobj);
gboolean ges_clip_fill_track_element          (GESClip *object, GESTrackElement    *trackelement, GstElement *gnlobj);
GESTrackElement *ges_clip_find_track_element   (GESClip *object, GESTrack          *track,    GType      type);

/* Layer */
GESTimelineLayer *ges_clip_get_layer   (GESClip *object);
gboolean ges_clip_is_moving_from_layer (GESClip *object);
gboolean ges_clip_move_to_layer        (GESClip *object, GESTimelineLayer  *layer);
void ges_clip_set_moving_from_layer    (GESClip *object, gboolean is_moving);

/* Effects */
GList* ges_clip_get_top_effects           (GESClip *object);
gint   ges_clip_get_top_effect_position   (GESClip *object, GESTrackEffect *effect);
gboolean ges_clip_set_top_effect_priority (GESClip *object, GESTrackEffect *effect, guint newpriority);

/* Editing */
GESClip *ges_clip_split  (GESClip *object, guint64  position);
void ges_clip_objects_set_locked   (GESClip *object, gboolean locked);

gboolean ges_clip_edit             (GESClip *object, GList   *layers,
                                               gint  new_layer_priority, GESEditMode mode,
                                               GESEdge edge, guint64 position);

G_END_DECLS
#endif /* _GES_CLIP */
