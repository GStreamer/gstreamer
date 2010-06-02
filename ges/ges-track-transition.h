/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon@collabora.co.uk>
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

#ifndef _GES_TRACK_TRANSITION
#define _GES_TRACK_TRANSITION

#include <glib-object.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <ges/ges-types.h>
#include <ges/ges-track-object.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_TRANSITION ges_track_transition_get_type()

#define GES_TRACK_TRANSITION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_TRANSITION,\
        GESTrackTransition))

#define GES_TRACK_TRANSITION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_TRANSITION,\
        GESTrackTransitionClass))

#define GES_IS_TRACK_TRANSITION(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_TRANSITION))

#define GES_IS_TRACK_TRANSITION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_TRANSITION))

#define GES_TRACK_TRANSITION_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_TRANSITION,\
        GESTrackTransitionClass)

/**
 * GESTrackTransition:
 * @parent: parent
 * @vtype: a #GEnumValue representing the type of transition to apply.
 *
 * Track level representation of a transition. Has a concrete implementation
 * for both audio and video streams.
 *
 */

struct _GESTrackTransition
{
  GESTrackObject parent;

  /*< public >*/
  /* given to to smpte alpha element */
  gint                          vtype;

  /*< private >*/
  
  /* these enable video interpolation */
  GstController                 *vcontroller;
  GstInterpolationControlSource *vcontrol_source;
  
  /* these will be different depending on whether smptealpha or alpha element
   * is used */
  gdouble                       vstart_value;
  gdouble                       vend_value;

  /* these enable volume interpolation. Unlike video, both inputs are adjusted
   * simultaneously */
  GstController                 *a_acontroller;
  GstInterpolationControlSource *a_acontrol_source;

  GstController                 *a_bcontroller;
  GstInterpolationControlSource *a_bcontrol_source;
};

/**
 * GESTrackTransitionClass
 * @parent_class: parent class
 */

struct _GESTrackTransitionClass {
    GESTrackObjectClass parent_class;
    /* <public> */
};

GType ges_track_transition_get_type (void);

GESTrackTransition *ges_track_transition_new (gint value);

G_END_DECLS

#endif
