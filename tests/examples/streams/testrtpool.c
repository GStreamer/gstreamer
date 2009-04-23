/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.com>
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

#include "testrtpool.h"

static void test_rt_pool_class_init (TestRTPoolClass * klass);
static void test_rt_pool_init (TestRTPool * pool);
static void test_rt_pool_finalize (GObject * object);

G_DEFINE_TYPE (TestRTPool, test_rt_pool, GST_TYPE_TASK_POOL);

static void
default_prepare (GstTaskPool * pool, GFunc func, gpointer user_data,
    GError ** error)
{
  g_message ("prepare Realtime pool %p", pool);
}

static void
default_cleanup (GstTaskPool * pool)
{
  g_message ("cleanup Realtime pool %p", pool);
}

static gpointer
default_push (GstTaskPool * pool, gpointer data, GError ** error)
{
  g_message ("pushing Realtime pool %p", pool);

  *error = g_error_new (1, 1, "not supported");

  return NULL;
}

static void
default_join (GstTaskPool * pool, gpointer id)
{
  g_message ("joining Realtime pool %p", pool);
}

static void
test_rt_pool_class_init (TestRTPoolClass * klass)
{
  GObjectClass *gobject_class;
  GstTaskPoolClass *gsttaskpool_class;

  gobject_class = (GObjectClass *) klass;
  gsttaskpool_class = (GstTaskPoolClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (test_rt_pool_finalize);

  gsttaskpool_class->prepare = default_prepare;
  gsttaskpool_class->cleanup = default_cleanup;
  gsttaskpool_class->push = default_push;
  gsttaskpool_class->join = default_join;
}

static void
test_rt_pool_init (TestRTPool * pool)
{
}

static void
test_rt_pool_finalize (GObject * object)
{
  G_OBJECT_CLASS (test_rt_pool_parent_class)->finalize (object);
}

GstTaskPool *
test_rt_pool_new (void)
{
  GstTaskPool *pool;

  pool = g_object_new (TEST_TYPE_RT_POOL, NULL);

  return pool;
}
