/*
 * GStreamer
 * Copyright (C) 2010 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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


#include "gstvdpbufferpool.h"

struct _GstVdpBufferPoolPrivate
{
  GQueue *buffers;
  GMutex *mutex;

  /* properties */
  guint max_buffers;
  GstCaps *caps;
  GstVdpDevice *device;
};

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_CAPS,
  PROP_MAX_BUFFERS
};

G_DEFINE_TYPE (GstVdpBufferPool, gst_vdp_buffer_pool, G_TYPE_OBJECT);

#define DEFAULT_MAX_BUFFERS 20

static void
gst_vdp_buffer_free (GstVdpBuffer * buf)
{
  gst_vdp_buffer_set_buffer_pool (buf, NULL);
  gst_vdp_buffer_unref (buf);
}

static void
gst_vdp_buffer_pool_clear (GstVdpBufferPool * bpool)
{
  GstVdpBufferPoolPrivate *priv = bpool->priv;

  g_queue_foreach (priv->buffers, (GFunc) gst_vdp_buffer_free, NULL);
  g_queue_clear (priv->buffers);
}

gboolean
gst_vdp_buffer_pool_put_buffer (GstVdpBufferPool * bpool, GstVdpBuffer * buf)
{
  GstVdpBufferPoolPrivate *priv;

  gboolean res;
  GstVdpBufferPoolClass *bpool_class;
  GstCaps *caps;

  g_return_val_if_fail (GST_IS_VDP_BUFFER_POOL (bpool), FALSE);
  g_return_val_if_fail (GST_IS_VDP_BUFFER (buf), FALSE);

  priv = bpool->priv;
  g_return_val_if_fail (priv->caps, FALSE);

  g_mutex_lock (priv->mutex);

  if (priv->buffers->length == priv->max_buffers) {
    res = FALSE;
    goto done;
  }

  bpool_class = GST_VDP_BUFFER_POOL_GET_CLASS (bpool);
  caps = GST_BUFFER_CAPS (buf);
  if (!caps)
    goto no_caps;

  if (!bpool_class->check_caps (bpool, caps)) {
    res = FALSE;
    goto done;
  }

  gst_vdp_buffer_ref (buf);
  g_queue_push_tail (priv->buffers, buf);
  res = TRUE;

done:
  g_mutex_unlock (priv->mutex);

  return res;

no_caps:
  GST_WARNING ("Buffer doesn't have any caps");
  res = FALSE;
  goto done;
}

GstVdpBuffer *
gst_vdp_buffer_pool_get_buffer (GstVdpBufferPool * bpool, GError ** error)
{
  GstVdpBufferPoolPrivate *priv;
  GstVdpBuffer *buf;

  g_return_val_if_fail (GST_IS_VDP_BUFFER_POOL (bpool), NULL);

  priv = bpool->priv;
  g_return_val_if_fail (priv->caps, NULL);

  g_mutex_lock (priv->mutex);

  buf = g_queue_pop_head (priv->buffers);
  if (!buf) {
    GstVdpBufferPoolClass *bpool_class = GST_VDP_BUFFER_POOL_GET_CLASS (bpool);

    buf = bpool_class->alloc_buffer (bpool, error);
    if (!buf)
      goto done;
    gst_buffer_set_caps (GST_BUFFER_CAST (buf), priv->caps);
    gst_vdp_buffer_set_buffer_pool (buf, bpool);
  }

done:
  g_mutex_unlock (priv->mutex);
  return buf;
}

void
gst_vdp_buffer_pool_set_max_buffers (GstVdpBufferPool * bpool,
    guint max_buffers)
{
  GstVdpBufferPoolPrivate *priv;

  g_return_if_fail (GST_IS_VDP_BUFFER_POOL (bpool));
  g_return_if_fail (max_buffers >= -1);

  priv = bpool->priv;

  g_mutex_lock (priv->mutex);

  if (max_buffers != -1) {
    while (max_buffers < priv->buffers->length) {
      GstVdpBuffer *buf;

      buf = g_queue_pop_tail (priv->buffers);
      gst_vdp_buffer_unref (buf);
    }
  }

  priv->max_buffers = max_buffers;

  g_mutex_unlock (priv->mutex);
}

guint
gst_vdp_buffer_pool_get_max_buffers (GstVdpBufferPool * bpool)
{
  g_return_val_if_fail (GST_IS_VDP_BUFFER_POOL (bpool), 0);

  return bpool->priv->max_buffers;
}

void
gst_vdp_buffer_pool_set_caps (GstVdpBufferPool * bpool, const GstCaps * caps)
{
  GstVdpBufferPoolPrivate *priv;
  GstVdpBufferPoolClass *bpool_class;
  gboolean clear_bufs;

  g_return_if_fail (GST_IS_VDP_BUFFER_POOL (bpool));
  g_return_if_fail (GST_IS_CAPS (caps));

  priv = bpool->priv;
  bpool_class = GST_VDP_BUFFER_POOL_GET_CLASS (bpool);

  g_mutex_lock (priv->mutex);

  if (!bpool_class->set_caps (bpool, caps, &clear_bufs))
    goto invalid_caps;

  if (clear_bufs)
    gst_vdp_buffer_pool_clear (bpool);

  if (priv->caps)
    gst_caps_unref (priv->caps);

  priv->caps = gst_caps_copy (caps);

done:
  g_mutex_unlock (priv->mutex);
  return;

invalid_caps:
  GST_WARNING ("Subclass didn't accept caps: %" GST_PTR_FORMAT, caps);
  goto done;
}

const GstCaps *
gst_vdp_buffer_pool_get_caps (GstVdpBufferPool * bpool)
{
  g_return_val_if_fail (GST_IS_VDP_BUFFER_POOL (bpool), NULL);

  return bpool->priv->caps;
}

GstVdpDevice *
gst_vdp_buffer_pool_get_device (GstVdpBufferPool * bpool)
{
  g_return_val_if_fail (GST_IS_VDP_BUFFER_POOL (bpool), NULL);

  return bpool->priv->device;
}

static void
gst_vdp_buffer_pool_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVdpBufferPool *bpool = (GstVdpBufferPool *) object;
  GstVdpBufferPoolPrivate *priv = bpool->priv;

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
      break;

    case PROP_CAPS:
      g_value_set_pointer (value, priv->caps);
      break;

    case PROP_MAX_BUFFERS:
      g_value_set_uint (value, priv->max_buffers);
      break;


    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_buffer_pool_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVdpBufferPool *bpool = (GstVdpBufferPool *) object;
  GstVdpBufferPoolPrivate *priv = bpool->priv;

  switch (prop_id) {
    case PROP_DEVICE:
      priv->device = g_value_get_object (value);
      break;

    case PROP_CAPS:
      gst_vdp_buffer_pool_set_caps (bpool, g_value_get_pointer (value));
      break;

    case PROP_MAX_BUFFERS:
      gst_vdp_buffer_pool_set_max_buffers (bpool, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_buffer_pool_finalize (GObject * object)
{
  GstVdpBufferPool *bpool = GST_VDP_BUFFER_POOL (object);
  GstVdpBufferPoolPrivate *priv = bpool->priv;

  g_mutex_free (priv->mutex);

  if (priv->caps)
    gst_caps_unref (priv->caps);

  G_OBJECT_CLASS (gst_vdp_buffer_pool_parent_class)->finalize (object);
}

static void
gst_vdp_buffer_pool_init (GstVdpBufferPool * bpool)
{
  GstVdpBufferPoolPrivate *priv;

  bpool->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (bpool,
      GST_TYPE_VDP_BUFFER_POOL, GstVdpBufferPoolPrivate);

  priv->buffers = g_queue_new ();
  priv->mutex = g_mutex_new ();

  /* properties */
  priv->caps = NULL;
  priv->max_buffers = DEFAULT_MAX_BUFFERS;
}

static void
gst_vdp_buffer_pool_class_init (GstVdpBufferPoolClass * bpool_klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (bpool_klass);

  g_type_class_add_private (bpool_klass, sizeof (GstVdpBufferPoolPrivate));

  object_class->get_property = gst_vdp_buffer_pool_get_property;
  object_class->set_property = gst_vdp_buffer_pool_set_property;

  object_class->finalize = gst_vdp_buffer_pool_finalize;

  /**
   * GstVdpBufferPool:device:
   *
   * The #GstVdpDevice this pool is bound to.
   */
  g_object_class_install_property
      (object_class,
      PROP_DEVICE,
      g_param_spec_object ("device",
          "Device",
          "The GstVdpDevice this pool is bound to",
          GST_TYPE_VDP_DEVICE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GstVdpBufferPool:caps:
   *
   * The video object capabilities represented as a #GstCaps. This
   * shall hold at least the "width" and "height" properties.
   */
  g_object_class_install_property
      (object_class,
      PROP_CAPS,
      g_param_spec_pointer ("caps",
          "Caps", "The buffer capabilities", G_PARAM_READWRITE));

  /**
   * GstVdpBufferPool:max-buffers:
   *
   * The maximum number of buffer in the pool. Or -1, the pool
   * will hold as many objects as possible.
   */
  g_object_class_install_property
      (object_class,
      PROP_MAX_BUFFERS,
      g_param_spec_int ("max-buffers",
          "Max Buffers",
          "The maximum number of buffers in the pool, or -1 for unlimited",
          -1, G_MAXINT32, DEFAULT_MAX_BUFFERS, G_PARAM_READWRITE));
}
