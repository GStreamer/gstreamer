/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cuda-gst.h"
#include "gstcudamemorypool.h"
#include "gstcudautils.h"
#include "gstcuda-private.h"
#include <string.h>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_CUDA_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("cudamemorypool", 0, "cudamemorypool");
  } GST_CUDA_CALL_ONCE_END;

  return cat;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

static gint
gst_cuda_memory_pool_compare_func (const GstCudaMemoryPool * a,
    const GstCudaMemoryPool * b)
{
  if (a == b)
    return GST_VALUE_EQUAL;

  return GST_VALUE_UNORDERED;
}

static void
gst_cuda_memory_pool_init_once (GType type)
{
  static GstValueTable table = {
    0, (GstValueCompareFunc) gst_cuda_memory_pool_compare_func,
    nullptr, nullptr
  };

  table.type = type;
  gst_value_register (&table);
}

G_DEFINE_BOXED_TYPE_WITH_CODE (GstCudaMemoryPool, gst_cuda_memory_pool,
    (GBoxedCopyFunc) gst_mini_object_ref,
    (GBoxedFreeFunc) gst_mini_object_unref,
    gst_cuda_memory_pool_init_once (g_define_type_id));

struct _GstCudaMemoryPoolPrivate
{
  CUmemoryPool handle;
};

static void
_gst_cuda_memory_pool_free (GstCudaMemoryPool * self)
{
  auto priv = self->priv;

  if (self->context) {
    if (priv->handle) {
      gst_cuda_context_push (self->context);
      CuMemPoolDestroy (priv->handle);
      gst_cuda_context_pop (nullptr);
    }

    gst_object_unref (self->context);
  }

  g_free (priv);
  g_free (self);
}

/**
 * gst_cuda_memory_pool_new:
 * @context: a #GstCudaContext
 * @props: (allow-none): a CUmemPoolProps
 *
 * Creates a new #GstCudaMemoryPool with @props. If @props is %NULL,
 * non-exportable pool property will be used.
 *
 * Returns: (transfer full) (nullable): a new #GstCudaMemoryPool or %NULL on
 * failure
 *
 * Since: 1.26
 */
GstCudaMemoryPool *
gst_cuda_memory_pool_new (GstCudaContext * context,
    const CUmemPoolProps * props)
{
  GstCudaMemoryPool *self;
  CUresult cuda_ret;
  CUmemPoolProps default_props;
  guint device_id = 0;
  CUmemoryPool handle;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), nullptr);

  g_object_get (context, "cuda-device-id", &device_id, nullptr);

  memset (&default_props, 0, sizeof (CUmemPoolProps));
  default_props.allocType = CU_MEM_ALLOCATION_TYPE_PINNED;
  default_props.handleTypes = CU_MEM_HANDLE_TYPE_NONE;
  default_props.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
  default_props.location.id = device_id;

  if (!gst_cuda_context_push (context)) {
    GST_ERROR_OBJECT (context, "Couldn't push context");
    return nullptr;
  }

  cuda_ret = CuMemPoolCreate (&handle, props ? props : &default_props);
  gst_cuda_context_pop (nullptr);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR_OBJECT (context, "Couldn't create pool");
    return nullptr;
  }

  self = g_new0 (GstCudaMemoryPool, 1);
  self->context = (GstCudaContext *) gst_object_ref (context);
  self->priv = g_new0 (GstCudaMemoryPoolPrivate, 1);
  self->priv->handle = handle;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (self), 0,
      GST_TYPE_CUDA_MEMORY_POOL, nullptr, nullptr,
      (GstMiniObjectFreeFunction) _gst_cuda_memory_pool_free);

  return self;
}

/**
 * gst_cuda_memory_pool_get_handle:
 * @pool: a #GstCudaMemoryPool
 *
 * Get CUDA memory pool handle
 *
 * Returns: a CUmemoryPool handle
 *
 * Since: 1.26
 */
CUmemoryPool
gst_cuda_memory_pool_get_handle (GstCudaMemoryPool * pool)
{
  g_return_val_if_fail (GST_IS_CUDA_MEMORY_POOL (pool), nullptr);

  return pool->priv->handle;
}

/**
 * gst_cuda_memory_pool_ref:
 * @pool: a #GstCudaMemoryPool
 *
 * Increase the reference count of @pool.
 *
 * Returns: (transfer full): @pool
 *
 * Since: 1.26
 */
GstCudaMemoryPool *
gst_cuda_memory_pool_ref (GstCudaMemoryPool * pool)
{
  return (GstCudaMemoryPool *)
      gst_mini_object_ref (GST_MINI_OBJECT_CAST (pool));
}

/**
 * gst_cuda_memory_pool_unref:
 * @pool: a #GstCudaMemoryPool
 *
 * Decrease the reference count of @pool.
 *
 * Since: 1.26
 */
void
gst_cuda_memory_pool_unref (GstCudaMemoryPool * pool)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (pool));
}

/**
 * gst_clear_cuda_memory_pool: (skip)
 * @pool: a pointer to a #GstCudaMemoryPool reference
 *
 * Clears a reference to a #GstCudaMemoryPool.
 *
 * Since: 1.26
 */
void
gst_clear_cuda_memory_pool (GstCudaMemoryPool ** pool)
{
  gst_clear_mini_object ((GstMiniObject **) pool);
}
