/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __TEST_RT_POOL_H__
#define __TEST_RT_POOL_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* --- standard type macros --- */
#define TEST_TYPE_RT_POOL             (test_rt_pool_get_type ())
#define TEST_RT_POOL(pool)            (G_TYPE_CHECK_INSTANCE_CAST ((pool), TEST_TYPE_RT_POOL, TestRTPool))
#define TEST_IS_RT_POOL(pool)         (G_TYPE_CHECK_INSTANCE_TYPE ((pool), TEST_TYPE_RT_POOL))
#define TEST_RT_POOL_CLASS(pclass)    (G_TYPE_CHECK_CLASS_CAST ((pclass), TEST_TYPE_RT_POOL, TestRTPoolClass))
#define TEST_IS_RT_POOL_CLASS(pclass) (G_TYPE_CHECK_CLASS_TYPE ((pclass), TEST_TYPE_RT_POOL))
#define TEST_RT_POOL_GET_CLASS(pool)  (G_TYPE_INSTANCE_GET_CLASS ((pool), TEST_TYPE_RT_POOL, TestRTPoolClass))
#define TEST_RT_POOL_CAST(pool)       ((TestRTPool*)(pool))

typedef struct _TestRTPool TestRTPool;
typedef struct _TestRTPoolClass TestRTPoolClass;

struct _TestRTPool {
  GstTaskPool    object;
};

struct _TestRTPoolClass {
  GstTaskPoolClass parent_class;
};

GType           test_rt_pool_get_type    (void);

GstTaskPool *   test_rt_pool_new         (void);

G_END_DECLS

#endif /* __TEST_RT_POOL_H__ */
