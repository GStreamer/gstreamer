/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12decodersessionpool.h"
#include <mutex>
#include <vector>
#include <algorithm>

/* *INDENT-OFF* */
struct _GstD3D12DecoderSession : public GstMiniObject
{
  GstD3D12DecoderSessionPool *pool = nullptr;
  gpointer data = nullptr;
  GDestroyNotify notify = nullptr;

  ~_GstD3D12DecoderSession ()
  {
    if (notify)
      notify (data);
  }
};

struct GstD3D12DecoderSessionPoolPrivate
{
  GstD3D12Device *device = nullptr;
  std::mutex lock;
  std::vector<GstD3D12DecoderSession *> sessions;
  bool flushing = false;
};

struct _GstD3D12DecoderSessionPool
{
  GstObject parent;
  GstD3D12DecoderSessionPoolPrivate *priv;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12DecoderSession, gst_d3d12_decoder_session);

#define gst_d3d12_decoder_session_pool_parent_class parent_class
G_DEFINE_TYPE (GstD3D12DecoderSessionPool, gst_d3d12_decoder_session_pool,
    GST_TYPE_OBJECT);

static void
gst_d3d12_decoder_session_pool_finalize (GObject * object)
{
  auto self = GST_D3D12_DECODER_SESSION_POOL (object);
  auto priv = self->priv;

  for (auto it : priv->sessions)
    gst_mini_object_unref (it);

  gst_clear_object (&self->priv->device);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_decoder_session_pool_class_init (GstD3D12DecoderSessionPoolClass *
    klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_decoder_session_pool_finalize;
}

static void
gst_d3d12_decoder_session_pool_init (GstD3D12DecoderSessionPool * self)
{
  self->priv = new GstD3D12DecoderSessionPoolPrivate ();
}

GstD3D12DecoderSessionPool *
gst_d3d12_decoder_session_pool_new (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto self = (GstD3D12DecoderSessionPool *)
      g_object_new (GST_TYPE_D3D12_DECODER_SESSION_POOL, nullptr);
  gst_object_ref_sink (self);

  self->priv->device = (GstD3D12Device *) gst_object_ref (device);

  return self;
}

GstD3D12Device *
gst_d3d12_decoder_session_pool_get_device (GstD3D12DecoderSessionPool * pool)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER_SESSION_POOL (pool), nullptr);

  return pool->priv->device;
}

void
gst_d3d12_decoder_session_pool_flush (GstD3D12DecoderSessionPool * pool)
{
  g_return_if_fail (GST_IS_D3D12_DECODER_SESSION_POOL (pool));

  auto priv = pool->priv;
  std::vector<GstD3D12DecoderSession *> sessions;

  {
    std::lock_guard <std::mutex> lk (priv->lock);
    priv->flushing = true;
    sessions.swap (priv->sessions);
  }

  for (auto session : sessions)
    gst_mini_object_unref (session);
}

static gboolean
gst_d3d12_decoder_session_dispose (GstD3D12DecoderSession * session)
{
  auto pool = session->pool;
  if (!pool)
    return TRUE;

  if (!session->data) {
    session->pool = nullptr;
    gst_object_unref (pool);
    return TRUE;
  }

  auto priv = pool->priv;
  gboolean do_free = TRUE;

  {
    std::lock_guard<std::mutex> lk (priv->lock);

    if (!priv->flushing) {
      gst_mini_object_ref (session);

      session->dispose = nullptr;
      session->pool = nullptr;
      priv->sessions.push_back (session);

      do_free = FALSE;
    } else {
      session->pool = nullptr;
    }
  }

  gst_object_unref (pool);

  return do_free;
}

static void
gst_d3d12_decoder_session_free (GstD3D12DecoderSession * session)
{
  delete session;
}

gboolean
gst_d3d12_decoder_session_pool_acquire (GstD3D12DecoderSessionPool * pool,
    GstD3D12DecoderSessionMatchFunc match, gpointer user_data,
    GstD3D12DecoderSession ** session)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER_SESSION_POOL (pool), FALSE);
  g_return_val_if_fail (match, FALSE);
  g_return_val_if_fail (session, FALSE);

  *session = nullptr;

  auto priv = pool->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  auto it = std::find_if (priv->sessions.begin (), priv->sessions.end (),
      [match, user_data] (GstD3D12DecoderSession * entry) {
        return entry->data && match (entry->data, user_data);
      }
  );

  GstD3D12DecoderSession *entry;
  if (it != priv->sessions.end ()) {
    entry = *it;
    priv->sessions.erase (it);
  } else {
    entry = new GstD3D12DecoderSession ();
    gst_mini_object_init (entry, 0, gst_d3d12_decoder_session_get_type (),
        nullptr, nullptr,
        (GstMiniObjectFreeFunction) gst_d3d12_decoder_session_free);
  }

  entry->pool = (GstD3D12DecoderSessionPool *) gst_object_ref (pool);
  entry->dispose =
      (GstMiniObjectDisposeFunction) gst_d3d12_decoder_session_dispose;
  *session = entry;

  return TRUE;
}
/* *INDENT-ON* */

GstD3D12DecoderSession *
gst_d3d12_decoder_session_new (void)
{
  auto session = new GstD3D12DecoderSession ();
  gst_mini_object_init (session, 0, gst_d3d12_decoder_session_get_type (),
      nullptr, nullptr,
      (GstMiniObjectFreeFunction) gst_d3d12_decoder_session_free);

  return session;
}

GstD3D12DecoderSession *
gst_d3d12_decoder_session_ref (GstD3D12DecoderSession * session)
{
  return (GstD3D12DecoderSession *) gst_mini_object_ref (session);
}

void
gst_d3d12_decoder_session_unref (GstD3D12DecoderSession * session)
{
  gst_mini_object_unref (session);
}

gpointer
gst_d3d12_decoder_session_get_data (GstD3D12DecoderSession * session)
{
  g_return_val_if_fail (session, nullptr);
  return session->data;
}

void
gst_d3d12_decoder_session_set_data (GstD3D12DecoderSession * session,
    gpointer data, GDestroyNotify notify)
{
  g_return_if_fail (session);
  g_return_if_fail (!session->data);
  g_return_if_fail (data);
  g_return_if_fail (notify);

  session->data = data;
  session->notify = notify;
}
