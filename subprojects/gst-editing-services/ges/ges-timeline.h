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
#include <gst/pbutils/gstdiscoverer.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE ges_timeline_get_type()
GES_DECLARE_TYPE(Timeline, timeline, TIMELINE);

#define GES_TIMELINE_GET_TRACKS(obj) (GES_TIMELINE (obj)->tracks)
#define GES_TIMELINE_GET_LAYERS(obj) (GES_TIMELINE (obj)->layers)

/**
 * ges_timeline_get_project:
 * @obj: The #GESTimeline from which to retrieve the project
 *
 * Helper macro to retrieve the project from which @obj was extracted
 */
#define ges_timeline_get_project(obj) (GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE(obj))))

typedef struct _GESTimelinePrivate GESTimelinePrivate;

/**
 * GESTimeline:
 * @layers: (element-type GES.Layer): A list of #GESLayer-s sorted by
 * priority. NOTE: Do not modify.
 * @tracks: Deprecated:1.10: (element-type GES.Track): This is not thread
 * safe, use #ges_timeline_get_tracks instead.
 */
struct _GESTimeline {
  GstBin parent;

  /*< public > */
  /* <readonly> */
  GList *layers;
  GList *tracks;

  /*< private >*/
  GESTimelinePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineClass:
 * @parent_class: parent class
 */

struct _GESTimelineClass {
  GstBinClass parent_class;

  /*< private >*/

  void (*track_added)	(GESTimeline *timeline, GESTrack * track);
  void (*track_removed)	(GESTimeline *timeline, GESTrack * track);
  void (*layer_added)	(GESTimeline *timeline, GESLayer *layer);
  void (*layer_removed)	(GESTimeline *timeline, GESLayer *layer);
  void (*group_added) (GESTimeline *timeline, GESGroup *group);
  void (*group_removed) (GESTimeline *timeline, GESGroup *group, GPtrArray *children);

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GESTimeline* ges_timeline_new (void) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GESTimeline* ges_timeline_new_from_uri (const gchar *uri, GError **error) G_GNUC_WARN_UNUSED_RESULT;

GES_API
gboolean ges_timeline_load_from_uri (GESTimeline *timeline, const gchar *uri, GError **error);
GES_API
gboolean ges_timeline_save_to_uri (GESTimeline * timeline, const gchar * uri,
    GESAsset *formatter_asset, gboolean overwrite, GError ** error);
GES_API
gboolean ges_timeline_add_layer (GESTimeline *timeline, GESLayer *layer);
GES_API
GESLayer * ges_timeline_append_layer (GESTimeline * timeline);
GES_API
gboolean ges_timeline_remove_layer (GESTimeline *timeline, GESLayer *layer);
GES_API
GList* ges_timeline_get_layers (GESTimeline *timeline) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GESLayer* ges_timeline_get_layer (GESTimeline *timeline, guint priority) G_GNUC_WARN_UNUSED_RESULT;

GES_API
gboolean ges_timeline_add_track (GESTimeline *timeline, GESTrack *track);
GES_API
gboolean ges_timeline_remove_track (GESTimeline *timeline, GESTrack *track);

GES_API
GESTrack * ges_timeline_get_track_for_pad (GESTimeline *timeline, GstPad *pad);
GES_API
GstPad * ges_timeline_get_pad_for_track (GESTimeline * timeline, GESTrack *track);
GES_API
GList *ges_timeline_get_tracks (GESTimeline *timeline) G_GNUC_WARN_UNUSED_RESULT;

GES_API
GList* ges_timeline_get_groups (GESTimeline * timeline);

GES_API
gboolean ges_timeline_commit (GESTimeline * timeline);
GES_API
gboolean ges_timeline_commit_sync (GESTimeline * timeline);
GES_API
void ges_timeline_freeze_commit (GESTimeline * timeline);
GES_API
void ges_timeline_thaw_commit (GESTimeline * timeline);

GES_API
GstClockTime ges_timeline_get_duration (GESTimeline *timeline);

GES_API
gboolean ges_timeline_get_auto_transition (GESTimeline * timeline);
GES_API
void ges_timeline_set_auto_transition (GESTimeline * timeline, gboolean auto_transition);
GES_API
GstClockTime ges_timeline_get_snapping_distance (GESTimeline * timeline);
GES_API
void ges_timeline_set_snapping_distance (GESTimeline * timeline, GstClockTime snapping_distance);
GES_API
GESTimelineElement * ges_timeline_get_element (GESTimeline * timeline, const gchar *name) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gboolean ges_timeline_is_empty (GESTimeline * timeline);
GES_API
GESTimelineElement * ges_timeline_paste_element (GESTimeline * timeline,
  GESTimelineElement * element, GstClockTime position, gint layer_priority) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gboolean ges_timeline_move_layer (GESTimeline *timeline, GESLayer *layer, guint new_layer_priority);

GES_API
GstClockTime ges_timeline_get_frame_time(GESTimeline *self,
                                         GESFrameNumber frame_number);

GES_API
GESFrameNumber ges_timeline_get_frame_at (GESTimeline *self,
                                          GstClockTime timestamp);

GES_API
void ges_timeline_disable_edit_apis (GESTimeline * self, gboolean disable_edit_apis);
GES_API
gboolean ges_timeline_get_edit_apis_disabled (GESTimeline * self);

G_END_DECLS
