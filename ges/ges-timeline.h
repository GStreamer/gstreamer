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

#ifndef _GES_TIMELINE
#define _GES_TIMELINE

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE ges_timeline_get_type()

#define GES_TIMELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE, GESTimeline))

#define GES_TIMELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE, GESTimelineClass))

#define GES_IS_TIMELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE))

#define GES_IS_TIMELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE))

#define GES_TIMELINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE, GESTimelineClass))

#define GES_TIMELINE_GET_TRACKS(obj) (GES_TIMELINE (obj)->tracks)
#define GES_TIMELINE_GET_LAYERS(obj) (GES_TIMELINE (obj)->layers)

/**
 * ges_timeline_get_project:
 * @obj: The #GESTimeline from which to retrieve the project
 *
 * Helper macro to retrieve the project from which a #GESTimeline as been extracted
 */
#define ges_timeline_get_project(obj) (GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE(obj))))

typedef struct _GESTimelinePrivate GESTimelinePrivate;

/**
 * GESTimeline:
 * @layers: (element-type GES.Layer): A list of #GESLayer sorted by priority NOTE: Do not modify.
 * @tracks: (element-type GES.Track): A list of #GESTrack sorted by priority NOTE: Do not modify.
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

GST_EXPORT
GType ges_timeline_get_type (void);

GST_EXPORT
GESTimeline* ges_timeline_new (void);
GST_EXPORT
GESTimeline* ges_timeline_new_from_uri (const gchar *uri, GError **error);

GST_EXPORT
gboolean ges_timeline_load_from_uri (GESTimeline *timeline, const gchar *uri, GError **error);
GST_EXPORT
gboolean ges_timeline_save_to_uri (GESTimeline * timeline, const gchar * uri,
    GESAsset *formatter_asset, gboolean overwrite, GError ** error);
GST_EXPORT
gboolean ges_timeline_add_layer (GESTimeline *timeline, GESLayer *layer);
GST_EXPORT
GESLayer * ges_timeline_append_layer (GESTimeline * timeline);
GST_EXPORT
gboolean ges_timeline_remove_layer (GESTimeline *timeline, GESLayer *layer);
GST_EXPORT
GList* ges_timeline_get_layers (GESTimeline *timeline);
GST_EXPORT
GESLayer* ges_timeline_get_layer (GESTimeline *timeline, guint priority);

GST_EXPORT
gboolean ges_timeline_add_track (GESTimeline *timeline, GESTrack *track);
GST_EXPORT
gboolean ges_timeline_remove_track (GESTimeline *timeline, GESTrack *track);

GST_EXPORT
GESTrack * ges_timeline_get_track_for_pad (GESTimeline *timeline, GstPad *pad);
GST_EXPORT
GstPad * ges_timeline_get_pad_for_track (GESTimeline * timeline, GESTrack *track);
GST_EXPORT
GList *ges_timeline_get_tracks (GESTimeline *timeline);

GST_EXPORT
GList* ges_timeline_get_groups (GESTimeline * timeline);

GST_EXPORT
gboolean ges_timeline_commit (GESTimeline * timeline);
GST_EXPORT
gboolean ges_timeline_commit_sync (GESTimeline * timeline);

GST_EXPORT
GstClockTime ges_timeline_get_duration (GESTimeline *timeline);

GST_EXPORT
gboolean ges_timeline_get_auto_transition (GESTimeline * timeline);
GST_EXPORT
void ges_timeline_set_auto_transition (GESTimeline * timeline, gboolean auto_transition);
GST_EXPORT
GstClockTime ges_timeline_get_snapping_distance (GESTimeline * timeline);
GST_EXPORT
void ges_timeline_set_snapping_distance (GESTimeline * timeline, GstClockTime snapping_distance);
GST_EXPORT
GESTimelineElement * ges_timeline_get_element (GESTimeline * timeline, const gchar *name);
GST_EXPORT
gboolean ges_timeline_is_empty (GESTimeline * timeline);

G_END_DECLS

#endif /* _GES_TIMELINE */

