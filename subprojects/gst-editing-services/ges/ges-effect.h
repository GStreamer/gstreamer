/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <thibault.saunier@collabora.co.uk>
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
#include <ges/ges-base-effect.h>

G_BEGIN_DECLS
#define GES_TYPE_EFFECT ges_effect_get_type()
GES_DECLARE_TYPE(Effect, effect, EFFECT);

/**
 * GESEffect:
 *
 */
struct _GESEffect
{
  /*< private > */
  GESBaseEffect parent;
  GESEffectPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESEffectClass:
 * @parent_class: parent class
 */

struct _GESEffectClass
{
  /*< private > */
  GESBaseEffectClass parent_class;

  GList *rate_properties;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];

};

GES_API GESEffect*
ges_effect_new (const gchar * bin_description) G_GNUC_WARN_UNUSED_RESULT;

GES_API gboolean
ges_effect_class_register_rate_property (GESEffectClass *klass, const gchar *element_name, const gchar *property_name);

G_END_DECLS