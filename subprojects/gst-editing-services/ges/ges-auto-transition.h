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

#pragma once

#include <glib-object.h>
#include "ges-track-element.h"
#include "ges-clip.h"
#include "ges-layer.h"

G_BEGIN_DECLS

#define GES_TYPE_AUTO_TRANSITION             (ges_auto_transition_get_type ())
typedef struct _GESAutoTransitionClass GESAutoTransitionClass;
typedef struct _GESAutoTransition GESAutoTransition;

GES_DECLARE_TYPE(AutoTransition, auto_transition, AUTO_TRANSITION);

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
  GESTrackElement *previous_source;
  GESTrackElement *next_source;
  GESTrackElement *transition;

  GESClip *transition_clip;
  gboolean positioning;

  gboolean frozen;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

G_GNUC_INTERNAL void ges_auto_transition_update (GESAutoTransition *self);
G_GNUC_INTERNAL GESAutoTransition * ges_auto_transition_new (GESTrackElement * transition,
                                             GESTrackElement * previous_source,
                                             GESTrackElement * next_source);

G_END_DECLS
