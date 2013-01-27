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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_TRACK_TRANSITION
#define _GES_TRACK_TRANSITION

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <ges/ges-types.h>
#include <ges/ges-operation.h>

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
        GESTrackTransitionClass))

typedef struct _GESTrackTransitionPrivate GESTrackTransitionPrivate;

/**
 * GESTrackTransition:
 *
 * Base class for media transitions.
 */

struct _GESTrackTransition
{
  /*< private >*/
  GESOperation parent;

  GESTrackTransitionPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTrackTransitionClass:
 */

struct _GESTrackTransitionClass {
  /*< private >*/
  GESOperationClass parent_class;
  
  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_track_transition_get_type (void);

G_END_DECLS

#endif
