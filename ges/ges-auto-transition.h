/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*-  */
/*
 * gst-editing-services
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-editing-services is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gst-editing-services is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.";
 */

#ifndef _GES_AUTO_TRANSITION_H_
#define _GES_AUTO_TRANSITION_H_

#include <glib-object.h>
#include "ges-track-object.h"
#include "ges-clip.h"
#include "ges-timeline-layer.h"

G_BEGIN_DECLS

#define GES_TYPE_AUTO_TRANSITION             (ges_auto_transition_get_type ())
#define GES_AUTO_TRANSITION(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_AUTO_TRANSITION, GESAutoTransition))
#define GES_AUTO_TRANSITION_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_AUTO_TRANSITION, GESAutoTransitionClass))
#define GES_IS_AUTO_TRANSITION(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_AUTO_TRANSITION))
#define GES_IS_AUTO_TRANSITION_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_AUTO_TRANSITION))
#define GES_AUTO_TRANSITION_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_AUTO_TRANSITION, GESAutoTransitionClass))

typedef struct _GESAutoTransitionClass GESAutoTransitionClass;
typedef struct _GESAutoTransition GESAutoTransition;



struct _GESAutoTransitionClass
{
  GObjectClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESAutoTransition
{
  GObject parent_instance;

  /* <read only and construct only> */
  GESTrackObject *previous_source;
  GESTrackObject *next_source;
  GESTrackObject *transition;

  GESTimelineLayer *layer;

  GESClip *previous_clip;
  GESClip *next_clip;
  GESClip *transition_clip;

  gchar *key;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_auto_transition_get_type (void) G_GNUC_CONST;

GESAutoTransition * ges_auto_transition_new (GESTrackObject * transition,
                                             GESTrackObject * previous_source,
                                             GESTrackObject * next_source);

G_END_DECLS
#endif /* _GES_AUTO_TRANSITION_H_ */
