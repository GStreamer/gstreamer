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

typedef struct _GESTimelinePrivate GESTimelinePrivate;

/**
 * GESTimeline:
 *
 */
struct _GESTimeline {
  GstBin parent;

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
  void (*layer_added)	(GESTimeline *timeline, GESTimelineLayer *layer);
  void (*layer_removed)	(GESTimeline *timeline, GESTimelineLayer *layer);

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_get_type (void);

GESTimeline* ges_timeline_new (void);
GESTimeline* ges_timeline_new_from_uri (const gchar *uri);

gboolean ges_timeline_load_from_uri (GESTimeline *timeline, const gchar *uri);
gboolean ges_timeline_save_to_uri (GESTimeline *timeline, const gchar *uri);

gboolean ges_timeline_add_layer (GESTimeline *timeline, GESTimelineLayer *layer);
GESTimelineLayer * ges_timeline_append_layer (GESTimeline * timeline);
gboolean ges_timeline_remove_layer (GESTimeline *timeline, GESTimelineLayer *layer);
GList* ges_timeline_get_layers (GESTimeline *timeline);

gboolean ges_timeline_add_track (GESTimeline *timeline, GESTrack *track);
gboolean ges_timeline_remove_track (GESTimeline *timeline, GESTrack *track);

GESTrack * ges_timeline_get_track_for_pad (GESTimeline *timeline, GstPad *pad);
GList *ges_timeline_get_tracks (GESTimeline *timeline);

gboolean ges_timeline_enable_update(GESTimeline * timeline, gboolean enabled);
gboolean ges_timeline_is_updating (GESTimeline * timeline);

GstClockTime ges_timeline_get_duration (GESTimeline *timeline);

G_END_DECLS

#endif /* _GES_TIMELINE */

