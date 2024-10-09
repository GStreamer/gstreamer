/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12.h"
#include <wrl.h>
#include <queue>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_cmd_alloc_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_cmd_alloc_pool_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct _GstD3D12CmdAlloc : public GstMiniObject
{
  GstD3D12CmdAllocPool *pool = nullptr;
  D3D12_COMMAND_LIST_TYPE type;
  ComPtr < ID3D12CommandAllocator > ca;
};

struct _GstD3D12CmdAllocPoolPrivate
{
  ~_GstD3D12CmdAllocPoolPrivate ()
  {
    while (!cmd_pool.empty ()) {
      auto ca = cmd_pool.front ();
      cmd_pool.pop ();
      gst_mini_object_unref (ca);
    }
  }

  ComPtr<ID3D12Device> device;

  std::mutex lock;
  std::queue<GstD3D12CmdAlloc *>cmd_pool;
  D3D12_COMMAND_LIST_TYPE cmd_type;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12CmdAlloc, gst_d3d12_cmd_alloc);

static void gst_d3d12_cmd_alloc_pool_finalize (GObject * object);

#define gst_d3d12_cmd_alloc_pool_parent_class parent_class
G_DEFINE_TYPE (GstD3D12CmdAllocPool, gst_d3d12_cmd_alloc_pool, GST_TYPE_OBJECT);

static void
gst_d3d12_cmd_alloc_pool_class_init (GstD3D12CmdAllocPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_cmd_alloc_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_cmd_alloc_pool_debug,
      "d3d12cmdallocpool", 0, "d3d12cmdallocpool");
}

static void
gst_d3d12_cmd_alloc_pool_init (GstD3D12CmdAllocPool * self)
{
  self->priv = new GstD3D12CmdAllocPoolPrivate ();
}

static void
gst_d3d12_cmd_alloc_pool_finalize (GObject * object)
{
  auto self = GST_D3D12_CMD_ALLOC_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_d3d12_cmd_alloc_pool_new:
 * @device: a #GstD3D12Device
 * @type: D3D12_COMMAND_LIST_TYPE
 *
 * Returns: (transfer full): a new #GstD3D12CmdAllocPool instance
 *
 * Since: 1.26
 */
GstD3D12CmdAllocPool *
gst_d3d12_cmd_alloc_pool_new (ID3D12Device * device,
    D3D12_COMMAND_LIST_TYPE type)
{
  g_return_val_if_fail (device, nullptr);

  auto self = (GstD3D12CmdAllocPool *)
      g_object_new (GST_TYPE_D3D12_CMD_ALLOC_POOL, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = device;
  priv->cmd_type = type;

  return self;
}

static void
gst_d3d12_cmd_alloc_pool_release (GstD3D12CmdAllocPool * pool,
    GstD3D12CmdAlloc * ca)
{
  auto priv = pool->priv;
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    ca->dispose = nullptr;
    ca->pool = nullptr;
    priv->cmd_pool.push (ca);
  }

  gst_object_unref (pool);
}

static gboolean
gst_d3d12_cmd_alloc_dispose (GstD3D12CmdAlloc * ca)
{
  if (!ca->pool)
    return TRUE;

  gst_mini_object_ref (ca);
  gst_d3d12_cmd_alloc_pool_release (ca->pool, ca);

  return FALSE;
}

static void
gst_d3d12_cmd_alloc_free (GstD3D12CmdAlloc * ca)
{
  delete ca;
}

static GstD3D12CmdAlloc *
gst_d3d12_cmd_alloc_new (ID3D12CommandAllocator * handle,
    D3D12_COMMAND_LIST_TYPE type)
{
  auto ca = new GstD3D12CmdAlloc ();
  ca->ca = handle;
  ca->type = type;

  gst_mini_object_init (ca, 0, gst_d3d12_cmd_alloc_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_d3d12_cmd_alloc_free);

  return ca;
}

/**
 * gst_d3d12_cmd_alloc_pool_acquire:
 * @pool: a #GstD3D12CmdAllocPool
 * @ca: (out) (transfer full): a pointer to #GstD3D12CmdAlloc
 *
 * Acquire #GstD3D12CmdAlloc object
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_cmd_alloc_pool_acquire (GstD3D12CmdAllocPool * pool,
    GstD3D12CmdAlloc ** ca)
{
  g_return_val_if_fail (GST_IS_D3D12_CMD_ALLOC_POOL (pool), FALSE);
  g_return_val_if_fail (ca, FALSE);

  *ca = nullptr;

  auto priv = pool->priv;
  GstD3D12CmdAlloc *new_ca = nullptr;
  HRESULT hr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->cmd_pool.empty ()) {
      new_ca = priv->cmd_pool.front ();
      priv->cmd_pool.pop ();
    }
  }

  if (!new_ca) {
    ComPtr < ID3D12CommandAllocator > ca_handle;
    hr = priv->device->CreateCommandAllocator (priv->cmd_type,
        IID_PPV_ARGS (&ca_handle));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't create command allocator, hr: 0x%x",
          (guint) hr);
      return FALSE;
    }

    new_ca = gst_d3d12_cmd_alloc_new (ca_handle.Get (), priv->cmd_type);
    if (GST_OBJECT_FLAG_IS_SET (pool, GST_OBJECT_FLAG_MAY_BE_LEAKED))
      GST_MINI_OBJECT_FLAG_SET (new_ca, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  }

  new_ca->pool = (GstD3D12CmdAllocPool *) gst_object_ref (pool);
  new_ca->dispose = (GstMiniObjectDisposeFunction) gst_d3d12_cmd_alloc_dispose;

  *ca = new_ca;

  return TRUE;
}

/**
 * gst_d3d12_cmd_alloc_ref:
 * @ca: a #GstD3D12CmdAlloc
 *
 * Increments the refcount of @ca
 *
 * Returns: (transfer full): a #GstD3D12CmdAlloc
 *
 * Since: 1.26
 */
GstD3D12CmdAlloc *
gst_d3d12_cmd_alloc_ref (GstD3D12CmdAlloc * ca)
{
  return (GstD3D12CmdAlloc *) gst_mini_object_ref (ca);
}

/**
 * gst_d3d12_cmd_alloc_unref:
 * @ca: a #GstD3D12CmdAlloc
 *
 * Decrements the refcount of @ca
 *
 * Since: 1.26
 */
void
gst_d3d12_cmd_alloc_unref (GstD3D12CmdAlloc * ca)
{
  gst_mini_object_unref (ca);
}

/**
 * gst_clear_d3d12_cmd_alloc:
 * @ca: a pointer to #GstD3D12CmdAlloc
 *
 * Clears a reference to a #GstD3D12CmdAlloc
 *
 * Since: 1.26
 */
void
gst_clear_d3d12_cmd_alloc (GstD3D12CmdAlloc ** ca)
{
  gst_clear_mini_object (ca);
}

/**
 * gst_d3d12_cmd_alloc_get_handle:
 * @ca: a #GstD3D12CmdAlloc
 *
 * Gets ID3D12CommandAllocator handle.
 *
 * Returns: (transfer none): ID3D12CommandAllocator handle
 *
 * Since: 1.26
 */
ID3D12CommandAllocator *
gst_d3d12_cmd_alloc_get_handle (GstD3D12CmdAlloc * ca)
{
  g_return_val_if_fail (ca, nullptr);

  return ca->ca.Get ();
}
