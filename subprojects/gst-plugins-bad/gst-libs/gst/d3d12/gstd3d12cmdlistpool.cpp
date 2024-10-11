/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you cln redistribute it and/or
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
#include "gstd3d12cmdlistpool.h"
#include <wrl.h>
#include <queue>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_cmd_list_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_cmd_list_pool_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct _GstD3D12CmdList : public GstMiniObject
{
  GstD3D12CmdListPool *pool = nullptr;
  D3D12_COMMAND_LIST_TYPE type;
  ComPtr < ID3D12CommandList > cl;
};

struct GstD3D12CmdListPoolPrivate
{
  ~GstD3D12CmdListPoolPrivate ()
  {
    while (!cmd_pool.empty ()) {
      auto cmd = cmd_pool.front ();
      cmd_pool.pop ();
      gst_mini_object_unref (cmd);
    }
  }

  ComPtr<ID3D12Device> device;

  std::mutex lock;
  std::queue<GstD3D12CmdList *>cmd_pool;
  D3D12_COMMAND_LIST_TYPE cmd_type;
};
/* *INDENT-ON* */

struct _GstD3D12CmdListPool
{
  GstObject parent;

  GstD3D12CmdListPoolPrivate *priv;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12CmdList, gst_d3d12_cmd_list);

static void gst_d3d12_cmd_list_pool_finalize (GObject * object);

#define gst_d3d12_cmd_list_pool_parent_class parent_class
G_DEFINE_TYPE (GstD3D12CmdListPool, gst_d3d12_cmd_list_pool, GST_TYPE_OBJECT);

static void
gst_d3d12_cmd_list_pool_class_init (GstD3D12CmdListPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_cmd_list_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_cmd_list_pool_debug,
      "d3d12cmdlistpool", 0, "d3d12cmdlistpool");
}

static void
gst_d3d12_cmd_list_pool_init (GstD3D12CmdListPool * self)
{
  self->priv = new GstD3D12CmdListPoolPrivate ();
}

static void
gst_d3d12_cmd_list_pool_finalize (GObject * object)
{
  auto self = GST_D3D12_CMD_LIST_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GstD3D12CmdListPool *
gst_d3d12_cmd_list_pool_new (ID3D12Device * device,
    D3D12_COMMAND_LIST_TYPE type)
{
  g_return_val_if_fail (device, nullptr);

  if (type != D3D12_COMMAND_LIST_TYPE_DIRECT &&
      type != D3D12_COMMAND_LIST_TYPE_COMPUTE &&
      type != D3D12_COMMAND_LIST_TYPE_COPY) {
    GST_ERROR ("Not supported command list type");
    return nullptr;
  }

  auto self = (GstD3D12CmdListPool *)
      g_object_new (GST_TYPE_D3D12_CMD_LIST_POOL, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = device;
  priv->cmd_type = type;

  return self;
}

static void
gst_d3d12_cmd_list_pool_release (GstD3D12CmdListPool * pool,
    GstD3D12CmdList * cmd)
{
  auto priv = pool->priv;
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    cmd->dispose = nullptr;
    cmd->pool = nullptr;
    priv->cmd_pool.push (cmd);
  }

  gst_object_unref (pool);
}

static gboolean
gst_d3d12_cmd_list_dispose (GstD3D12CmdList * cmd)
{
  if (!cmd->pool)
    return TRUE;

  gst_mini_object_ref (cmd);
  gst_d3d12_cmd_list_pool_release (cmd->pool, cmd);

  return FALSE;
}

static void
gst_d3d12_cmd_list_free (GstD3D12CmdList * cmd)
{
  delete cmd;
}

static GstD3D12CmdList *
gst_d3d12_cmd_list_new (ID3D12CommandList * cl, D3D12_COMMAND_LIST_TYPE type)
{
  auto cmd = new GstD3D12CmdList ();
  cmd->cl = cl;
  cmd->type = type;

  gst_mini_object_init (cmd, 0, gst_d3d12_cmd_list_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_d3d12_cmd_list_free);

  return cmd;
}

gboolean
gst_d3d12_cmd_list_pool_acquire (GstD3D12CmdListPool * pool,
    ID3D12CommandAllocator * ca, GstD3D12CmdList ** cl)
{
  g_return_val_if_fail (GST_IS_D3D12_CMD_LIST_POOL (pool), FALSE);
  g_return_val_if_fail (ca, FALSE);
  g_return_val_if_fail (cl, FALSE);

  *cl = nullptr;

  auto priv = pool->priv;
  GstD3D12CmdList *new_cl = nullptr;
  HRESULT hr;

  hr = ca->Reset ();
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (pool, "Couldn't reset command allocator");
    return FALSE;
  }

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->cmd_pool.empty ()) {
      new_cl = priv->cmd_pool.front ();
      priv->cmd_pool.pop ();
    }
  }

  ComPtr < ID3D12GraphicsCommandList > handle;
  if (!new_cl) {
    hr = priv->device->CreateCommandList (0, priv->cmd_type,
        ca, nullptr, IID_PPV_ARGS (&handle));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't create command list, hr: 0x%x",
          (guint) hr);
      return FALSE;
    }

    new_cl = gst_d3d12_cmd_list_new (handle.Get (), priv->cmd_type);
    if (GST_OBJECT_FLAG_IS_SET (pool, GST_OBJECT_FLAG_MAY_BE_LEAKED))
      GST_MINI_OBJECT_FLAG_SET (new_cl, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  } else {
    new_cl->cl->QueryInterface (IID_PPV_ARGS (&handle));
    hr = handle->Reset (ca, nullptr);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't reset command list");
      gst_mini_object_unref (new_cl);
      return false;
    }
  }

  new_cl->pool = (GstD3D12CmdListPool *) gst_object_ref (pool);
  new_cl->dispose = (GstMiniObjectDisposeFunction) gst_d3d12_cmd_list_dispose;

  *cl = new_cl;

  return TRUE;
}

GstD3D12CmdList *
gst_d3d12_cmd_list_ref (GstD3D12CmdList * cl)
{
  return (GstD3D12CmdList *) gst_mini_object_ref (cl);
}

void
gst_d3d12_cmd_list_unref (GstD3D12CmdList * cl)
{
  gst_mini_object_unref (cl);
}

void
gst_clear_d3d12_cmd_list (GstD3D12CmdList ** cl)
{
  gst_clear_mini_object (cl);
}

ID3D12CommandList *
gst_d3d12_cmd_list_get_handle (GstD3D12CmdList * cl)
{
  g_return_val_if_fail (cl, nullptr);

  return cl->cl.Get ();
}
