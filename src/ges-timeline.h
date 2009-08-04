/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

typedef struct {
  GstBin parent;

  GList *layers;	/* TimelineLayer(s) ordered by priority */
  GList *tracks;	/* TimelineTrack(s) */
} GESTimeline;

typedef struct {
  GstBinClass parent_class;
} GESTimelineClass;

GType ges_timeline_get_type (void);

GESTimeline* ges_timeline_new (void);


GESTimeline* ges_timeline_load_from_uri (gchar *uri);

gboolean ges_timeline_save (GESTimeline *timeline, gchar *uri);

gboolean ges_timeline_add_layer (GESTimeline *timeline, GESTimelineLayer *layer);
gboolean ges_timeline_remove_layer (GESTimeline *timeline, GESTimelineLayer *layer);

gboolean ges_timeline_add_track (GESTimeline *timeline, GESTrack *track);
gboolean ges_timeline_remove_track (GESTimeline *timeline, GESTrack *track);

G_END_DECLS

#endif /* _GES_TIMELINE */

