/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *
 * gnlcomposition.h: Header for base GnlComposition
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


#ifndef __GNL_COMPOSITION_H__
#define __GNL_COMPOSITION_H__

#include <gst/gst.h>
#include "gnlobject.h"

G_BEGIN_DECLS
#define GNL_TYPE_COMPOSITION \
  (gnl_composition_get_type())
#define GNL_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_COMPOSITION,GnlComposition))
#define GNL_COMPOSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_COMPOSITION,GnlCompositionClass))
#define GNL_COMPOSITION_GET_CLASS(obj) \
  (GNL_COMPOSITION_CLASS (G_OBJECT_GET_CLASS (obj)))
#define GNL_IS_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_COMPOSITION))
#define GNL_IS_COMPOSITION_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_COMPOSITION))

typedef struct _GnlCompositionPrivate GnlCompositionPrivate;

struct _GnlComposition
{
  GnlObject parent;

  GstTask               * task;
  GRecMutex		            task_rec_lock;
  /*< private >*/

  GnlCompositionPrivate * priv;
};

struct _GnlCompositionClass
{
  GnlObjectClass parent_class;
};

GType gnl_composition_get_type (void);

G_END_DECLS
#endif /* __GNL_COMPOSITION_H__ */
