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

#ifndef _GES_TRACK
#define _GES_TRACK

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK ges_track_get_type()

#define GES_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK, GESTrack))

#define GES_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK, GESTrackClass))

#define GES_IS_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK))

#define GES_IS_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK))

#define GES_TRACK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK, GESTrackClass))


#define GES_TYPE_TRACK_TYPE (ges_track_type_get_type ())
GType ges_track_type_get_type (void);

/**
 * GESTrackType:
 * @GES_TRACK_TYPE_AUDIO: An audio track
 * @GES_TRACK_TYPE_VIDEO: A video track
 * @GES_TRACK_TYPE_TEXT: A text (subtitle) track
 * @GES_TRACK_TYPE_CUSTOM: A custom-content track
 *
 * Types of content handled by a track. If the content is not one of
 * @GEST_TRACK_TYPE_AUDIO, @GES_TRACK_TYPE_VIDEO or @GES_TRACK_TYPE_TEXT,
 * the user of the #GESTrack must set the type to @GES_TRACK_TYPE_CUSTOM.
 */

typedef enum {
  GES_TRACK_TYPE_AUDIO	= 0,
  GES_TRACK_TYPE_VIDEO	= 1,
  GES_TRACK_TYPE_TEXT	= 2,
  GES_TRACK_TYPE_CUSTOM	= 3
} GESTrackType;

struct _GESTrack {
  GstBin parent;

  /*< public >*/ /* READ-ONLY */
  GESTrackType type;

  /*< private >*/
  GESTimeline * timeline;

  GstCaps * caps;

  GstElement * composition;	/* The composition associated with this track */
  GstPad * srcpad;		/* The source GhostPad */
};

struct _GESTrackClass {
  GstBinClass parent_class;
};

GType ges_track_get_type (void);

GESTrack* ges_track_new (GESTrackType type, GstCaps * caps);

void ges_track_set_timeline (GESTrack * track, GESTimeline *timeline);
void ges_track_set_caps (GESTrack * track, const GstCaps * caps);

gboolean ges_track_add_object (GESTrack * track, GESTrackObject * object);
gboolean ges_track_remove_object (GESTrack * track, GESTrackObject * object);

GESTrack *ges_track_video_raw_new ();
GESTrack *ges_track_audio_raw_new ();

G_END_DECLS

#endif /* _GES_TRACK */

