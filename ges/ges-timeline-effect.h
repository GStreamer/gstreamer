/* GStreamer Editing Services
 * Copyright (C) 2011 Thibault Saunier <thibault.saunier@collabora.co.uk>
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

#ifndef _GES_TIMELINE_EFFECT
#define _GES_TIMELINE_EFFECT

#include <glib-object.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_EFFECT ges_timeline_effect_get_type()

#define GES_TIMELINE_EFFECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_EFFECT, GESTimelineEffect))

#define GES_TIMELINE_EFFECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_EFFECT, GESTimelineEffectClass))

#define GES_IS_TIMELINE_EFFECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_EFFECT))

#define GES_IS_TIMELINE_EFFECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_EFFECT))

#define GES_TIMELINE_EFFECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_EFFECT, GESTimelineEffectClass))

typedef struct _GESTimelineEffectPrivate GESTimelineEffectPrivate;

/**
 * GESTimelineEffect:
 */
struct _GESTimelineEffect {
  /*< private >*/
  GESTimelineOperation parent;

  GESTimelineEffectPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineEffectClass:
 *
 */

struct _GESTimelineEffectClass {
  /*< private >*/
  GESTimelineOperationClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_effect_get_type (void);

G_END_DECLS
#endif /* _GES_TIMELINE_EFFECT */
