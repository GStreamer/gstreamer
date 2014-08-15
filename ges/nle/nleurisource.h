/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *
 * nleurisource.h: Header for NleURISource
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


#ifndef __NLE_URI_SOURCE_H__
#define __NLE_URI_SOURCE_H__

#include <gst/gst.h>
#include "nlesource.h"

G_BEGIN_DECLS
#define NLE_TYPE_URI_SOURCE \
  (nle_urisource_get_type())
#define NLE_URI_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),NLE_TYPE_URI_SOURCE,NleURIsource))
#define NLE_URI_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),NLE_TYPE_URI_SOURCE,NleURIsourceClass))
#define NLE_IS_URI_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),NLE_TYPE_URI_SOURCE))
#define NLE_IS_URI_SOURCE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),NLE_TYPE_URI_SOURCE))

struct _NleURISource
{
  NleSource parent;

  gchar *uri;
};

struct _NleURISourceClass
{
  NleSourceClass parent_class;
};

GType nle_urisource_get_type (void);

G_END_DECLS
#endif /* __NLE_URI_SOURCE_H__ */
