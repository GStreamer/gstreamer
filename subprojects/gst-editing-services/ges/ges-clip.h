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

#pragma once

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-timeline-element.h>
#include <ges/ges-container.h>
#include <ges/ges-types.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_CLIP             ges_clip_get_type()
GES_DECLARE_TYPE(Clip, clip, CLIP);

/**
 * GES_CLIP_CLASS_CAN_ADD_EFFECTS:
 * @klass: A #GESClipClass
 *
 * Whether the class allows for the user to add additional non-core
 * #GESBaseEffect-s to clips from this class.
 */
#define GES_CLIP_CLASS_CAN_ADD_EFFECTS(klass) ((GES_CLIP_CLASS (klass))->ABI.abi.can_add_effects)

/**
 * GESFillTrackElementFunc:
 * @clip: The #GESClip controlling the track elements
 * @track_element: The #GESTrackElement
 * @nleobj: The nleobject that needs to be filled
 *
 * A function that will be called when the nleobject of a corresponding
 * track element needs to be filled.
 *
 * The implementer of this function shall add the proper #GstElement to @nleobj
 * using gst_bin_add().
 *
 * Deprecated: 1.18: This method type is no longer used.
 *
 * Returns: %TRUE if the implementer successfully filled the @nleobj.
 */
typedef gboolean (*GESFillTrackElementFunc) (GESClip *clip, GESTrackElement *track_element,
                                             GstElement *nleobj);

/**
 * GESCreateTrackElementFunc:
 * @clip: A #GESClip
 * @type: A #GESTrackType to create a #GESTrackElement for
 *
 * A method for creating the core #GESTrackElement of a clip, to be added
 * to a #GESTrack of the given track type.
 *
 * If a clip may produce several track elements per track type,
 * #GESCreateTrackElementsFunc is more appropriate.
 *
 * Returns: (transfer floating) (nullable): The #GESTrackElement created
 * by @clip, or %NULL if @clip can not provide a track element for the
 * given @type or an error occurred.
 */
typedef GESTrackElement *(*GESCreateTrackElementFunc) (GESClip * clip, GESTrackType type);

/**
 * GESCreateTrackElementsFunc:
 * @clip: A #GESClip
 * @type: A #GESTrackType to create #GESTrackElement-s for
 *
 * A method for creating the core #GESTrackElement-s of a clip, to be
 * added to #GESTrack-s of the given track type.
 *
 * Returns: (transfer container) (element-type GESTrackElement): A list of
 * the #GESTrackElement-s created by @clip for the given @type, or %NULL
 * if no track elements are created or an error occurred.
 */
typedef GList * (*GESCreateTrackElementsFunc) (GESClip * clip, GESTrackType type);

/**
 * GESClip:
 */
struct _GESClip
{
  GESContainer    parent;

  /*< private >*/
  GESClipPrivate *priv;

  /* Padding for API extension */
  gpointer       _ges_reserved[GES_PADDING_LARGE];
};

/**
 * GESClipClass:
 * @create_track_element: Method to create the core #GESTrackElement of a clip
 * of this class. If a clip of this class may create several track elements per
 * track type, this should be left as %NULL, and
 * GESClipClass::create_track_elements should be used instead. Otherwise, you
 * should implement this class method and leave
 * GESClipClass::create_track_elements as the default implementation
 * @create_track_elements: Method to create the (multiple) core
 * #GESTrackElement-s of a clip of this class. If
 * GESClipClass::create_track_element is implemented, this should be kept as the
 * default implementation
 * @can_add_effects: Whether the user can add additional non-core
 * #GESBaseEffect-s to clips from this class, to be applied to the output data
 * of the core elements.
 */
struct _GESClipClass
{
  /*< private > */
  GESContainerClass          parent_class;

  /*< public > */
  GESCreateTrackElementFunc  create_track_element;
  GESCreateTrackElementsFunc create_track_elements;

  /*< private >*/
  /* Padding for API extension */
  union {
    gpointer _ges_reserved[GES_PADDING_LARGE];
    struct {
      gboolean can_add_effects;
    } abi;
  } ABI;
};

/****************************************************
 *                TrackElement handling             *
 ****************************************************/
GES_API
GESTrackType      ges_clip_get_supported_formats  (GESClip *clip);
GES_API
void              ges_clip_set_supported_formats  (GESClip *clip,
                                                   GESTrackType supportedformats);
GES_API
GESTrackElement*  ges_clip_add_asset              (GESClip *clip,
                                                   GESAsset *asset);
GES_API
GESTrackElement*  ges_clip_find_track_element     (GESClip *clip,
                                                   GESTrack *track,
                                                   GType type) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GList *           ges_clip_find_track_elements    (GESClip * clip,
                                                   GESTrack * track,
                                                   GESTrackType track_type,
                                                   GType type) G_GNUC_WARN_UNUSED_RESULT;

GES_API
GESTrackElement * ges_clip_add_child_to_track     (GESClip * clip,
                                                   GESTrackElement * child,
                                                   GESTrack * track,
                                                   GError ** error);

/****************************************************
 *                     Layer                        *
 ****************************************************/
GES_API
GESLayer* ges_clip_get_layer              (GESClip * clip) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gboolean  ges_clip_move_to_layer          (GESClip * clip,
                                           GESLayer * layer);
GES_API
gboolean  ges_clip_move_to_layer_full     (GESClip * clip,
                                           GESLayer * layer,
                                           GError ** error);
GES_API
gboolean  ges_clip_is_moving_between_layers (GESClip * clip);

/****************************************************
 *                   Effects                        *
 ****************************************************/
GES_API
gboolean ges_clip_add_top_effect            (GESClip * clip,
                                             GESBaseEffect * effect,
                                             gint index,
                                             GError ** error);
GES_API
gboolean ges_clip_remove_top_effect         (GESClip * clip,
                                             GESBaseEffect * effect,
                                             GError ** error);
GES_API
GList*   ges_clip_get_top_effects           (GESClip * clip) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gint     ges_clip_get_top_effect_position   (GESClip * clip,
                                             GESBaseEffect * effect);
GES_API
gint     ges_clip_get_top_effect_index      (GESClip * clip,
                                             GESBaseEffect * effect);
GES_API
gboolean ges_clip_set_top_effect_priority   (GESClip * clip,
                                             GESBaseEffect * effect,
                                             guint newpriority);
GES_API
gboolean ges_clip_set_top_effect_index      (GESClip * clip,
                                             GESBaseEffect * effect,
                                             guint newindex);
GES_API
gboolean ges_clip_set_top_effect_index_full (GESClip * clip,
                                             GESBaseEffect * effect,
                                             guint newindex,
                                             GError ** error);

/****************************************************
 *                   Editing                        *
 ****************************************************/
GES_API
GESClip*     ges_clip_split                                (GESClip *clip,
                                                            guint64 position);
GES_API
GESClip*     ges_clip_split_full                           (GESClip *clip,
                                                            guint64 position,
                                                            GError ** error);

GES_API
GstClockTime ges_clip_get_internal_time_from_timeline_time (GESClip * clip,
                                                            GESTrackElement * child,
                                                            GstClockTime timeline_time,
                                                            GError ** error);
GES_API
GstClockTime ges_clip_get_timeline_time_from_internal_time (GESClip * clip,
                                                            GESTrackElement * child,
                                                            GstClockTime internal_time,
                                                            GError ** error);
GES_API
GstClockTime ges_clip_get_timeline_time_from_source_frame (GESClip * clip,
                                                           GESFrameNumber frame_number,
                                                           GError ** error);

GES_API
GstClockTime ges_clip_get_duration_limit (GESClip * clip);

G_END_DECLS
