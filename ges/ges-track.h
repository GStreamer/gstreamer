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
#include <ges/ges-types.h>
#include <ges/ges-enums.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK            ges_track_get_type()
GES_DECLARE_TYPE(Track, track, TRACK);

/**
 * GESCreateElementForGapFunc:
 * @track: The #GESTrack
 *
 * A function that creates a #GstElement that can be used as a source to
 * fill the gaps of the track. A gap is a timeline region where the track
 * has no #GESTrackElement sources.
 *
 * Returns: A source #GstElement to fill gaps in @track.
 */
typedef GstElement* (*GESCreateElementForGapFunc) (GESTrack *track);

/**
 * GESTrack:
 * @type: The #GESTrack:track-type of the track
 */
struct _GESTrack
{
  GstBin parent;

  /*< public >*/
  /* READ-ONLY */
  GESTrackType     type;

  /*< private >*/
  GESTrackPrivate* priv;
  /* Padding for API extension */
  gpointer         _ges_reserved[GES_PADDING];
};

/**
 * GESTrackClass:
 */
struct _GESTrackClass
{
  /*< private >*/
  GstBinClass parent_class;

  GstElement * (*get_mixing_element) (GESTrack *track);

  /* Padding for API extension */
  gpointer    _ges_reserved[GES_PADDING];
};

GES_API
const GstCaps*     ges_track_get_caps                        (GESTrack *track);
GES_API
GList*             ges_track_get_elements                    (GESTrack *track);
GES_API
const GESTimeline* ges_track_get_timeline                    (GESTrack *track);
GES_API
gboolean           ges_track_commit                          (GESTrack *track);
GES_API
void               ges_track_set_timeline                    (GESTrack *track,
                                                              GESTimeline *timeline);
GES_API
gboolean           ges_track_add_element                     (GESTrack *track,
                                                              GESTrackElement *object);
GES_API
gboolean           ges_track_add_element_full                (GESTrack *track,
                                                              GESTrackElement *object,
                                                              GError ** error);
GES_API
gboolean           ges_track_remove_element                  (GESTrack *track,
                                                              GESTrackElement *object);
GES_API
gboolean           ges_track_remove_element_full             (GESTrack *track,
                                                              GESTrackElement *object,
                                                              GError ** error);
GES_API
void               ges_track_set_create_element_for_gap_func (GESTrack *track,
                                                              GESCreateElementForGapFunc func);
GES_API
void               ges_track_set_mixing                      (GESTrack *track,
                                                              gboolean mixing);
GES_API
gboolean           ges_track_get_mixing                      (GESTrack *track);
GES_API
void               ges_track_set_restriction_caps            (GESTrack *track,
                                                              const GstCaps *caps);
GES_API
void               ges_track_update_restriction_caps         (GESTrack *track,
                                                              const GstCaps *caps);
GES_API
GstCaps *          ges_track_get_restriction_caps            (GESTrack * track);

GES_API
GESTrack*          ges_track_new                             (GESTrackType type,
                                                              GstCaps * caps);

G_END_DECLS
