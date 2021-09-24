/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *
 * nlecomposition.h: Header for base NleComposition
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


#ifndef __NLE_COMPOSITION_H__
#define __NLE_COMPOSITION_H__

#include <gst/gst.h>
#include "nleobject.h"

G_BEGIN_DECLS
#define NLE_TYPE_COMPOSITION \
  (nle_composition_get_type())
#define NLE_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),NLE_TYPE_COMPOSITION,NleComposition))
#define NLE_COMPOSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),NLE_TYPE_COMPOSITION,NleCompositionClass))
#define NLE_COMPOSITION_GET_CLASS(obj) \
  (NLE_COMPOSITION_CLASS (G_OBJECT_GET_CLASS (obj)))
#define NLE_IS_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),NLE_TYPE_COMPOSITION))
#define NLE_IS_COMPOSITION_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),NLE_TYPE_COMPOSITION))

typedef struct _NleCompositionPrivate NleCompositionPrivate;

struct _NleComposition
{
  NleObject parent;

  GstTask               * task;
  GRecMutex		            task_rec_lock;

  /*< private >*/
  NleCompositionPrivate * priv;

};

struct _NleCompositionClass
{
  NleObjectClass parent_class;
};

GType nle_composition_get_type (void) G_GNUC_INTERNAL;

G_END_DECLS
#endif /* __NLE_COMPOSITION_H__ */
