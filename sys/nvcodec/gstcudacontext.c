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

GST_DEBUG_CATEGORY_STATIC (gst_cuda_context_debug);
#define GST_CAT_DEFAULT gst_cuda_context_debug

enum
{
  PROP_0,
  PROP_DEVICE_ID
};

#define DEFAULT_DEVICE_ID -1

struct _GstCudaContextPrivate
{
  CUcontext context;
  CUdevice device;
  gint device_id;
};

#define gst_cuda_context_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstCudaContext, gst_cuda_context, GST_TYPE_OBJECT);

static void gst_cuda_context_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cuda_context_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_cuda_context_constructed (GObject * object);
static void gst_cuda_context_finalize (GObject * object);

static void
gst_cuda_context_class_init (GstCudaContextClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_cuda_context_set_property;
  gobject_class->get_property = gst_cuda_context_get_property;
  gobject_class->constructed = gst_cuda_context_constructed;
  gobject_class->finalize = gst_cuda_context_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_int ("cuda-device-id", "Cuda Device ID",
          "Set the GPU device to use for operations (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_cuda_context_debug,
      "cudacontext", 0, "CUDA Context");
}

static void
gst_cuda_context_init (GstCudaContext * context)
{
  GstCudaContextPrivate *priv = gst_cuda_context_get_instance_private (context);

  priv->context = NULL;
  priv->device_id = DEFAULT_DEVICE_ID;

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
      priv->device_id = g_value_get_int (value);
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
      g_value_set_int (value, priv->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_context_constructed (GObject * object)
{
  static volatile gsize once = 0;
  GstCudaContext *context = GST_CUDA_CONTEXT (object);
  GstCudaContextPrivate *priv = context->priv;
  CUcontext cuda_ctx, old_ctx;
  gboolean ret = TRUE;
  CUdevice cdev = 0, cuda_dev = -1;
  gint dev_count = 0;
  gchar name[256];
  gint min = 0, maj = 0;
  gint i;

  if (g_once_init_enter (&once)) {
    if (CuInit (0) != CUDA_SUCCESS) {
      GST_ERROR_OBJECT (context, "Failed to cuInit");
      ret = FALSE;
    }
    g_once_init_leave (&once, ret);

    if (!ret)
      return;
  }

  if (!gst_cuda_result (CuDeviceGetCount (&dev_count)) || dev_count == 0) {
    GST_WARNING ("No CUDA devices detected");
    return;
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
      if (priv->device_id == -1 || priv->device_id == cdev) {
        priv->device_id = cuda_dev = cdev;
      }
    }
  }

  if (cuda_dev == -1) {
    GST_WARNING ("Device with id %d does not exist", priv->device_id);
    return;
  }

  GST_DEBUG ("Creating cuda context for device index %d", cuda_dev);

  if (!gst_cuda_result (CuCtxCreate (&cuda_ctx, 0, cuda_dev))) {
    GST_WARNING ("Failed to create CUDA context for cuda device %d", cuda_dev);
    return;
  }

  if (!gst_cuda_result (CuCtxPopCurrent (&old_ctx))) {
    return;
  }

  GST_INFO ("Created CUDA context %p with device-id %d", cuda_ctx, cuda_dev);

  priv->context = cuda_ctx;
  priv->device = cuda_dev;
}

static void
gst_cuda_context_finalize (GObject * object)
{
  GstCudaContext *context = GST_CUDA_CONTEXT_CAST (object);
  GstCudaContextPrivate *priv = context->priv;

  if (priv->context) {
    GST_DEBUG_OBJECT (context, "Destroying CUDA context %p", priv->context);
    gst_cuda_result (CuCtxDestroy (priv->context));
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_cuda_context_new:
 * @device_id: device-id for creating #GstCudaContext or -1 for auto selection
 *
 * Create #GstCudaContext with given device_id. If the @device_id was not -1
 * but was out of range (e.g., exceed the number of device),
 * #GstCudaContext will not be created.
 *
 * Returns: a new #GstCudaContext or %NULL on failure
 */
GstCudaContext *
gst_cuda_context_new (gint device_id)
{
  GstCudaContext *self =
      g_object_new (GST_TYPE_CUDA_CONTEXT, "cuda-device-id", device_id, NULL);

  gst_object_ref_sink (self);

  if (!self->priv->context) {
    GST_ERROR ("Failed to create CUDA context");
    gst_clear_object (&self);
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
 * Returns: the #CUcontext of @ctx
 */
gpointer
gst_cuda_context_get_handle (GstCudaContext * ctx)
{
  g_return_val_if_fail (ctx, NULL);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (ctx), NULL);

  return ctx->priv->context;
}
