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
#include <ges/ges-types.h>
#include <ges/ges-base-transition-clip.h>

G_BEGIN_DECLS

#define GES_TYPE_TRANSITION_CLIP ges_transition_clip_get_type()
GES_DECLARE_TYPE(TransitionClip, transition_clip, TRANSITION_CLIP);

/**
 * GESTransitionClip:
 * @vtype: a #GESVideoStandardTransitionType indicating the type of video transition
 * to apply.
 *
 * ### Children Properties
 *
 *  {{ libs/GESTransitionClip-children-props.md }}
 */
struct _GESTransitionClip {
  /*< private >*/
  GESBaseTransitionClip parent;

  /*< public >*/
  GESVideoStandardTransitionType vtype;

  /*< private >*/
  GESTransitionClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTransitionClipClass:
 *
 */

struct _GESTransitionClipClass {
  /*< private >*/
  GESBaseTransitionClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GESTransitionClip *ges_transition_clip_new (GESVideoStandardTransitionType vtype) G_GNUC_WARN_UNUSED_RESULT;
GES_API
GESTransitionClip *ges_transition_clip_new_for_nick (char *nick) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
