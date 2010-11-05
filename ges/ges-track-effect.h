/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <tsaunier@gnome.org>
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

#ifndef _GES_TRACK_EFFECT
#define _GES_TRACK_EFFECT

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-operation.h>

G_BEGIN_DECLS
#define GES_TYPE_TRACK_EFFECT ges_track_effect_get_type()
#define GES_TRACK_EFFECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_EFFECT, GESTrackEffect))
#define GES_TRACK_EFFECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_EFFECT, GESTrackEffectClass))
#define GES_IS_TRACK_EFFECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_EFFECT))
#define GES_IS_TRACK_EFFECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_EFFECT))
#define GES_TRACK_EFFECT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_EFFECT, GESTrackEffectClass))
/**
 * GESTrackEffect:
 *
 */
    struct _GESTrackEffect
{
  GESTrackOperation parent;

  /*< private > */
  gchar *bin_description;
  gchar *human_name;
};

/**
 * GESTrackEffectClass:
 * @parent_class: parent class
 */

struct _GESTrackEffectClass
{
  GESTrackOperationClass parent_class;

  /*< private > */
};

GType ges_track_effect_get_type (void);

void ges_track_effect_set_human_name (GESTrackEffect * self,
    const gchar * human_name);
gchar *ges_track_effect_get_human_name (GESTrackEffect * self);

GESTrackEffect *ges_track_effect_new (const gchar * bin_description);
GESTrackEffect *ges_track_effect_new_with_name (const gchar * bin_description,
    const gchar * human_name);

G_END_DECLS
#endif /* _GES_TRACK_EFFECT */
