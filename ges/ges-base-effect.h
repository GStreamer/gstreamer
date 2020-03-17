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

#ifndef _GES_BASE_EFFECT
#define _GES_BASE_EFFECT

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

G_END_DECLS
#endif /* _GES_BASE_EFFECT */
