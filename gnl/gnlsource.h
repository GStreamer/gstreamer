/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *
 * gnlsource.h: Header for base GnlSource
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


#ifndef __GNL_SOURCE_H__
#define __GNL_SOURCE_H__

#include <gst/gst.h>
#include "gnlobject.h"

G_BEGIN_DECLS
#define GNL_TYPE_SOURCE \
  (gnl_source_get_type())
#define GNL_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_SOURCE,GnlSource))
#define GNL_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_SOURCE,GnlSourceClass))
#define GNL_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GNL_TYPE_SOURCE, GnlSourceClass))
#define GNL_IS_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_SOURCE))
#define GNL_IS_SOURCE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_SOURCE))
typedef struct _GnlSourcePrivate GnlSourcePrivate;

struct _GnlSource
{
  GnlObject parent;

  /* controlled source element, acces with gst_bin_[add|remove]_element */
  GstElement *element;

  GnlSourcePrivate *priv;
};

struct _GnlSourceClass
{
  GnlObjectClass parent_class;

  /* controls_one is TRUE if the class only controls one element */
  gboolean controls_one;
  /* control_element() takes care of controlling the given element */
    gboolean (*control_element) (GnlSource * source, GstElement * element);
};

GType gnl_source_get_type (void);

G_END_DECLS
#endif /* __GNL_SOURCE_H__ */
