/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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


#ifndef __GST_FACTORY_LISTS_H__
#define __GST_FACTORY_LISTS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum {
  GST_FACTORY_LIST_DECODER = (1 << 0),
  GST_FACTORY_LIST_ENCODER = (1 << 1),
  GST_FACTORY_LIST_SINK    = (1 << 2),
  GST_FACTORY_LIST_SRC     = (1 << 3)
} GstFactoryListType;

gboolean      gst_factory_list_is_type      (GstElementFactory *factory, GstFactoryListType type);
GValueArray * gst_factory_list_get_elements (GstFactoryListType type);

void          gst_factory_list_debug        (GValueArray *array);

GValueArray * gst_factory_list_filter       (GValueArray *array, const GstCaps *caps);

#ifndef GST_DISABLE_GST_DEBUG
#define GST_FACTORY_LIST_DEBUG(array) gst_factory_list_debug(array)
#else
#define GST_FACTORY_LIST_DEBUG(array)
#endif

G_END_DECLS

#endif /* __GST_FACTORY_LISTS_H__ */
