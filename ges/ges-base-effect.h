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
#include <ges/ges-operation.h>

G_BEGIN_DECLS

#define GES_TYPE_BASE_EFFECT ges_base_effect_get_type()
GES_DECLARE_TYPE(BaseEffect, base_effect, BASE_EFFECT);

/**
 * GESBaseEffect:
 */
struct _GESBaseEffect
{
  /*< private > */
  GESOperation parent;
  GESBaseEffectPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESBaseEffectClass:
 * @parent_class: parent class
 */

struct _GESBaseEffectClass
{
  /*< private > */
  GESOperationClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];

};

/**
 * GESBaseEffectTimeTranslationFunc:
 * @effect: The #GESBaseEffect that is doing the time translation
 * @time: The #GstClockTime to translation
 * @time_property_values: (element-type gchar* GValue*): A table of child
 * property name/value pairs
 * @user_data: Data passed to ges_base_effect_set_time_translation_funcs()
 *
 * A function for querying how an effect would translate a time if it had
 * the given child property values set. The keys for @time_properties will
 * be the same string that was passed to
 * ges_base_effect_register_time_property(), the values will be #GValue*
 * values of the corresponding child properties. You should always use the
 * values given in @time_properties before using the currently set values.
 *
 * Returns: The translated time.
 * Since: 1.18
 */
typedef GstClockTime (*GESBaseEffectTimeTranslationFunc) (GESBaseEffect * effect,
                                                          GstClockTime time,
                                                          GHashTable * time_property_values,
                                                          gpointer user_data);

GES_API gboolean
ges_base_effect_register_time_property     (GESBaseEffect * effect,
                                            const gchar * child_property_name);
GES_API gboolean
ges_base_effect_set_time_translation_funcs (GESBaseEffect * effect,
                                            GESBaseEffectTimeTranslationFunc source_to_sink_func,
                                            GESBaseEffectTimeTranslationFunc sink_to_source_func,
                                            gpointer user_data,
                                            GDestroyNotify destroy);
GES_API gboolean
ges_base_effect_is_time_effect             (GESBaseEffect * effect);

G_END_DECLS
