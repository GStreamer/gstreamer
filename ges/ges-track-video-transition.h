/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#ifndef _GES_TRACK_VIDEO_TRANSITION
#define _GES_TRACK_VIDEO_TRANSITION

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-transition.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_VIDEO_TRANSITION ges_track_video_transition_get_type()

#define GES_TRACK_VIDEO_TRANSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_VIDEO_TRANSITION, GESTrackVideoTransition))

#define GES_TRACK_VIDEO_TRANSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_VIDEO_TRANSITION, GESTrackVideoTransitionClass))

#define GES_IS_TRACK_VIDEO_TRANSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_VIDEO_TRANSITION))

#define GES_IS_TRACK_VIDEO_TRANSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_VIDEO_TRANSITION))

#define GES_TRACK_VIDEO_TRANSITION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_VIDEO_TRANSITION, GESTrackVideoTransitionClass))

/** 
 * GESTrackVideoTransition:
 * @type: the 
 */

struct _GESTrackVideoTransition {
  GESTrackTransition parent;
  
  /*< public >*/
  GESVideoTransitionType        type;

  /*< private >*/
  
  /* these enable video interpolation */
  GstController                 *controller;
  GstInterpolationControlSource *control_source;

  /* so we can support changing between wipes */
  GstElement                    *smpte;
  GstElement                    *mixer;
  GstPad                        *sinka;
  GstPad                        *sinkb;
  
  /* these will be different depending on whether smptealpha or alpha element
   * is used */
  gdouble                       start_value;
  gdouble                       end_value;

  /*< public >*/
};

/**
 * GESTrackVideoTransitionClass:
 * @parent_class: parent class
 *
 */

struct _GESTrackVideoTransitionClass {
  GESTrackTransitionClass parent_class;

  /*< public >*/
};

GType ges_track_video_transition_get_type (void);

void
ges_track_video_transition_set_type (GESTrackVideoTransition * self, gint type);

GESTrackVideoTransition* ges_track_video_transition_new (void);

G_END_DECLS

#endif /* _GES_TRACK_VIDEO_transition */

