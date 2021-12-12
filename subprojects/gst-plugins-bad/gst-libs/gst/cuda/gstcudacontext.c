/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstcudaloader.h"
#include "gstcudacontext.h"
#include "gstcudautils.h"

#ifdef GST_CUDA_HAS_D3D
#include <gst/d3d11/gstd3d11.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_cuda_context_debug);
#define GST_CAT_DEFAULT gst_cuda_context_debug

/* store all context object with weak ref */
static GList *context_list = NULL;
G_LOCK_DEFINE_STATIC (list_lock);

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_DXGI_ADAPTER_LUID,
};

struct _GstCudaContextPrivate
{
  CUcontext context;
  CUdevice device;
  guint device_id;
  gint64 dxgi_adapter_luid;

  gint tex_align;

  GHashTable *accessible_peer;
  gboolean owns_context;
};

#define gst_cuda_context_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstCudaContext, gst_cuda_context, GST_TYPE_OBJECT);

static void gst_cuda_context_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cuda_context_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_cuda_context_finalize (GObject * object);
static void gst_cuda_context_weak_ref_notify (gpointer data,
    GstCudaContext * context);
static void gst_cuda_context_enable_peer_access (GstCudaContext * context,
    GstCudaContext * peer);

static void
gst_cuda_context_class_init (GstCudaContextClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_cuda_context_set_property;
  gobject_class->get_property = gst_cuda_context_get_property;
  gobject_class->finalize = gst_cuda_context_finalize;

  /**
   * GstCudaContext::cuda-device-id:
   *
   * The GPU index to use for the context.
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("cuda-device-id", "Cuda Device ID",
          "Set the GPU device to use for operations",
          0, G_MAXUINT, 0,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#ifdef GST_CUDA_HAS_D3D
  g_object_class_install_property (gobject_class, PROP_DXGI_ADAPTER_LUID,
      g_param_spec_int64 ("dxgi-adapter-luid", "DXGI Adapter LUID",
          "Associated DXGI Adapter LUID (Locally Unique Identifier) ",
          G_MININT64, G_MAXINT64, 0,
          GST_PARAM_CONDITIONALLY_AVAILABLE | G_PARAM_READABLE |
          G_PARAM_STATIC_STRINGS));
#endif
}

static void
gst_cuda_context_init (GstCudaContext * context)
{
  GstCudaContextPrivate *priv = gst_cuda_context_get_instance_private (context);

  priv->accessible_peer = g_hash_table_new (g_direct_hash, g_direct_equal);

  context->priv = priv;
}

static void
gst_cuda_context_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCudaContext *context = GST_CUDA_CONTEXT (object);
  GstCudaContextPrivate *priv = context->priv;

  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_context_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCudaContext *context = GST_CUDA_CONTEXT (object);
  GstCudaContextPrivate *priv = context->priv;

  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_uint (value, priv->device_id);
      break;
    case PROP_DXGI_ADAPTER_LUID:
      g_value_set_int64 (value, priv->dxgi_adapter_luid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifdef GST_CUDA_HAS_D3D
static gint64
gst_cuda_context_find_dxgi_adapter_luid (CUdevice cuda_device)
{
  gint64 ret = 0;
  HRESULT hr;
  IDXGIFactory1 *factory = NULL;
  guint i;

  hr = CreateDXGIFactory1 (&IID_IDXGIFactory1, (void **) &factory);
  if (FAILED (hr))
    return 0;

  for (i = 0;; i++) {
    IDXGIAdapter1 *adapter;
    DXGI_ADAPTER_DESC desc;
    CUdevice other_dev = 0;
    CUresult cuda_ret;

    hr = IDXGIFactory1_EnumAdapters1 (factory, i, &adapter);
    if (FAILED (hr))
      break;

    hr = IDXGIAdapter1_GetDesc (adapter, &desc);
    if (FAILED (hr)) {
      IDXGIAdapter1_Release (adapter);
      continue;
    }

    if (desc.VendorId != 0x10de) {
      IDXGIAdapter1_Release (adapter);
      continue;
    }

    cuda_ret = CuD3D11GetDevice (&other_dev, adapter);
    IDXGIAdapter1_Release (adapter);

    if (cuda_ret == CUDA_SUCCESS && other_dev == cuda_device) {
      ret = gst_d3d11_luid_to_int64 (&desc.AdapterLuid);
      break;
    }
  }

  IDXGIFactory1_Release (factory);

  return ret;
}
#endif
static gboolean
init_cuda_ctx (void)
{
  gboolean ret = TRUE;

  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    if (CuInit (0) != CUDA_SUCCESS) {
      GST_ERROR ("Failed to cuInit");
      ret = FALSE;
    }
    GST_DEBUG_CATEGORY_INIT (gst_cuda_context_debug,
        "cudacontext", 0, "CUDA Context");
    g_once_init_leave (&once, ret);
  }

  return ret;
}

static gboolean
gst_create_cucontext (guint * device_id, CUcontext * context)
{
  CUcontext cuda_ctx;
  CUdevice cdev = 0, cuda_dev = -1;
  gint dev_count = 0;
  gchar name[256];
  gint min = 0, maj = 0;
  gint i;


  if (!init_cuda_ctx ())
    return FALSE;

  if (!gst_cuda_result (CuDeviceGetCount (&dev_count)) || dev_count == 0) {
    GST_WARNING ("No CUDA devices detected");
    return FALSE;
  }

  for (i = 0; i < dev_count; ++i) {
    if (gst_cuda_result (CuDeviceGet (&cdev, i)) &&
        gst_cuda_result (CuDeviceGetName (name, sizeof (name), cdev)) &&
        gst_cuda_result (CuDeviceGetAttribute (&maj,
                CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, cdev)) &&
        gst_cuda_result (CuDeviceGetAttribute (&min,
                CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, cdev))) {
      GST_INFO ("GPU #%d supports NVENC: %s (%s) (Compute SM %d.%d)", i,
          (((maj << 4) + min) >= 0x30) ? "yes" : "no", name, maj, min);
      if (*device_id == -1 || *device_id == cdev) {
        *device_id = cuda_dev = cdev;
        break;
      }
    }
  }

  if (cuda_dev == -1) {
    GST_WARNING ("Device with id %d does not exist", *device_id);
    return FALSE;
  }

  if (!gst_cuda_result (CuDeviceGet (&cdev, *device_id))) {
    GST_WARNING ("Failed to get device for id %d", *device_id);
    return FALSE;
  }

  if (!gst_cuda_result (CuCtxCreate (&cuda_ctx, 0, cuda_dev))) {
    GST_WARNING ("Failed to create CUDA context for cuda device %d", cuda_dev);
    return FALSE;
  }

  GST_INFO ("Created CUDA context %p with device-id %d", cuda_ctx, *device_id);

  *context = cuda_ctx;

  return TRUE;
}

/* must be called with list_lock taken */
static void
gst_cuda_context_enable_peer_access (GstCudaContext * context,
    GstCudaContext * peer)
{
  GstCudaContextPrivate *priv = context->priv;
  GstCudaContextPrivate *peer_priv = peer->priv;
  CUdevice device = priv->device;
  CUdevice other_dev = peer_priv->device;
  CUresult cuda_ret;
  gint can_access = 0;

  cuda_ret = CuDeviceCanAccessPeer (&can_access, device, other_dev);

  if (!gst_cuda_result (cuda_ret) || !can_access) {
    GST_DEBUG_OBJECT (context,
        "Peer access to %" GST_PTR_FORMAT " is not allowed", peer);
    return;
  }

  gst_cuda_context_push (context);
  if (gst_cuda_result (CuCtxEnablePeerAccess (peer_priv->context, 0))) {
    GST_DEBUG_OBJECT (context, "Enable peer access to %" GST_PTR_FORMAT, peer);
    g_hash_table_add (priv->accessible_peer, peer);
  }

  gst_cuda_context_pop (NULL);
}

static void
gst_cuda_context_weak_ref_notify (gpointer data, GstCudaContext * context)
{
  GList *iter;

  G_LOCK (list_lock);
  context_list = g_list_remove (context_list, context);

  /* disable self -> peer access */
  if (context->priv->accessible_peer) {
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init (&iter, context->priv->accessible_peer);
    if (gst_cuda_context_push (context)) {
      while (g_hash_table_iter_next (&iter, &key, NULL)) {
        GstCudaContext *peer = GST_CUDA_CONTEXT (key);
        CUcontext peer_handle = gst_cuda_context_get_handle (peer);
        GST_DEBUG_OBJECT (context,
            "Disable peer access to %" GST_PTR_FORMAT, peer);
        gst_cuda_result (CuCtxDisablePeerAccess (peer_handle));
      }
      gst_cuda_context_pop (NULL);
    }

    g_hash_table_destroy (context->priv->accessible_peer);
    context->priv->accessible_peer = NULL;
  }

  /* disable peer -> self access */
  for (iter = context_list; iter; iter = g_list_next (iter)) {
    GstCudaContext *other = (GstCudaContext *) iter->data;
    GstCudaContextPrivate *other_priv = other->priv;
    CUcontext self_handle;

    if (!other_priv->accessible_peer)
      continue;

    if (g_hash_table_lookup (other_priv->accessible_peer, context)) {
      if (gst_cuda_context_push (other)) {
        self_handle = gst_cuda_context_get_handle (context);
        GST_DEBUG_OBJECT (other,
            "Disable peer access to %" GST_PTR_FORMAT, context);
        gst_cuda_result (CuCtxDisablePeerAccess (self_handle));
        gst_cuda_context_pop (NULL);
      }

      g_hash_table_remove (other_priv->accessible_peer, context);
    }
  }
  G_UNLOCK (list_lock);
}

static void
gst_cuda_context_finalize (GObject * object)
{
  GstCudaContext *context = GST_CUDA_CONTEXT_CAST (object);
  GstCudaContextPrivate *priv = context->priv;

  if (priv->context && priv->owns_context) {
    GST_DEBUG_OBJECT (context, "Destroying CUDA context %p", priv->context);
    gst_cuda_result (CuCtxDestroy (priv->context));
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_cuda_context_new:
 * @device_id: device-id for creating #GstCudaContext
 *
 * Create #GstCudaContext with given device_id
 *
 * Returns: a new #GstCudaContext or %NULL on failure
 *
 * Since: 1.22
 */
GstCudaContext *
gst_cuda_context_new (guint device_id)
{
  CUcontext old_ctx;
  CUcontext ctx;
  GstCudaContext *self;

  if (!gst_create_cucontext (&device_id, &ctx)) {
    return NULL;
  }

  self = gst_cuda_context_new_wrapped (ctx, device_id);
  if (!self)
    return NULL;

  self->priv->owns_context = TRUE;

  if (!gst_cuda_result (CuCtxPopCurrent (&old_ctx))) {
    GST_ERROR ("Could not pop current context");

    return NULL;
  }

  return self;
}

/**
 * gst_cuda_context_push:
 * @ctx: a #GstCudaContext to push current thread
 *
 * Pushes the given @ctx onto the CPU thread's stack of current contexts.
 * The specified context becomes the CPU thread's current context,
 * so all CUDA functions that operate on the current context are affected.
 *
 * Returns: %TRUE if @ctx was pushed without error.
 *
 * Since: 1.22
 */
gboolean
gst_cuda_context_push (GstCudaContext * ctx)
{
  g_return_val_if_fail (ctx, FALSE);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (ctx), FALSE);

  return gst_cuda_result (CuCtxPushCurrent (ctx->priv->context));
}

/**
 * gst_cuda_context_pop:
 *
 * Pops the current CUDA context from CPU thread
 *
 * Returns: %TRUE if @ctx was pushed without error.
 *
 * Since: 1.22
 */
gboolean
gst_cuda_context_pop (CUcontext * cuda_ctx)
{
  return gst_cuda_result (CuCtxPopCurrent (cuda_ctx));
}

/**
 * gst_cuda_context_get_handle:
 * @ctx: a #GstCudaContext
 *
 * Get CUDA device context. Caller must not modify and/or destroy
 * returned device context.
 *
 * Returns: the `CUcontext` of @ctx
 *
 * Since: 1.22
 */
gpointer
gst_cuda_context_get_handle (GstCudaContext * ctx)
{
  g_return_val_if_fail (ctx, NULL);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (ctx), NULL);

  return ctx->priv->context;
}

/**
 * gst_cuda_context_get_texture_alignment:
 * @ctx: a #GstCudaContext
 *
 * Get required texture alignment by device
 *
 * Returns: the `CUcontext` of @ctx
 *
 * Since: 1.22
 */
gint
gst_cuda_context_get_texture_alignment (GstCudaContext * ctx)
{
  g_return_val_if_fail (ctx, 0);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (ctx), 0);

  return ctx->priv->tex_align;
}

/**
 * gst_cuda_context_can_access_peer:
 * @ctx: a #GstCudaContext
 * @peer: a #GstCudaContext
 *
 * Query whether @ctx can access any memory which belongs to @peer directly.
 *
 * Returns: %TRUE if @ctx can access @peer directly
 *
 * Since: 1.22
 */
gboolean
gst_cuda_context_can_access_peer (GstCudaContext * ctx, GstCudaContext * peer)
{
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (ctx), FALSE);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (peer), FALSE);

  G_LOCK (list_lock);
  if (ctx->priv->accessible_peer &&
      g_hash_table_lookup (ctx->priv->accessible_peer, peer)) {
    ret = TRUE;
  }
  G_UNLOCK (list_lock);

  return ret;
}


/**
 * gst_cuda_context_new_wrapped:
 * @handler: A
 * [CUcontext](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__TYPES.html#group__CUDA__TYPES_1gf9f5bd81658f866613785b3a0bb7d7d9)
 * to wrap
 * @device: A
 * [CUDevice](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__TYPES.html#group__CUDA__TYPES_1gf9f5bd81658f866613785b3a0bb7d7d9)
 * to wrap
 *
 * Note: The caller is responsible for ensuring that the CUcontext and CUdevice
 * represented by @handle and @device stay alive while the returned
 * #GstCudaContext is active.
 *
 * Returns: (transfer full): A newly created #GstCudaContext
 *
 * Since: 1.22
 */
GstCudaContext *
gst_cuda_context_new_wrapped (CUcontext handler, CUdevice device)
{
  GList *iter;
  gint tex_align = 0;

  GstCudaContext *self;

  g_return_val_if_fail (handler, NULL);
  g_return_val_if_fail (device >= 0, NULL);

  if (!init_cuda_ctx ())
    return FALSE;

  if (!gst_cuda_result (CuDeviceGetAttribute (&tex_align,
              CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT, device))) {
    GST_ERROR ("Could not get texture alignment for %d", device);

    return NULL;
  }

  self = g_object_new (GST_TYPE_CUDA_CONTEXT, "cuda-device-id", device, NULL);
  self->priv->context = handler;
  self->priv->device = device;
  gst_object_ref_sink (self);

#ifdef GST_CUDA_HAS_D3D
  self->priv->dxgi_adapter_luid =
      gst_cuda_context_find_dxgi_adapter_luid (self->priv->device);
#endif


  G_LOCK (list_lock);
  g_object_weak_ref (G_OBJECT (self),
      (GWeakNotify) gst_cuda_context_weak_ref_notify, NULL);
  for (iter = context_list; iter; iter = g_list_next (iter)) {
    GstCudaContext *peer = (GstCudaContext *) iter->data;

    /* EnablePeerAccess is unidirectional */
    gst_cuda_context_enable_peer_access (self, peer);
    gst_cuda_context_enable_peer_access (peer, self);
  }

  context_list = g_list_append (context_list, self);
  G_UNLOCK (list_lock);

  return self;
}
