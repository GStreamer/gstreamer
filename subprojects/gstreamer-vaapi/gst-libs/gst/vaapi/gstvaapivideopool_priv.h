/*
 *  gstvaapivideopool_priv.h - Video object pool abstraction (private defs)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_VIDEO_POOL_PRIV_H
#define GST_VAAPI_VIDEO_POOL_PRIV_H

#include "gstvaapiminiobject.h"

G_BEGIN_DECLS

#define GST_VAAPI_VIDEO_POOL_CLASS(klass) \
  ((GstVaapiVideoPoolClass *)(klass))
#define GST_VAAPI_IS_VIDEO_POOL_CLASS(klass)    \
  ((klass) != NULL)

typedef struct _GstVaapiVideoPoolClass GstVaapiVideoPoolClass;

/**
 * GstVaapiVideoPool:
 *
 * A pool of lazily allocated video objects. e.g. surfaces, images.
 */
struct _GstVaapiVideoPool
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;

  guint object_type;
  GstVaapiDisplay *display;
  GQueue free_objects;
  GList *used_objects;
  guint used_count;
  guint capacity;
  GMutex mutex;
};

/**
 * GstVaapiVideoPoolClass:
 * @alloc_object: virtual function for allocating a video pool object
 *
 * A pool base class used to hold video objects. e.g. surfaces, images.
 */
struct _GstVaapiVideoPoolClass
{
  /*< private >*/
  GstVaapiMiniObjectClass parent_class;

  /*< public >*/
  gpointer (*alloc_object) (GstVaapiVideoPool * pool);
};

G_GNUC_INTERNAL
void
gst_vaapi_video_pool_init (GstVaapiVideoPool * pool, GstVaapiDisplay * display,
    GstVaapiVideoPoolObjectType object_type);

G_GNUC_INTERNAL
void
gst_vaapi_video_pool_finalize (GstVaapiVideoPool * pool);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_POOL_PRIV_H */
