/* GStreamer
 * Copyright (C) 2013 Wim Taymans <wim.taymans at gmail.com>
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
/**
 * SECTION:rtsp-thread-pool
 * @short_description: A pool of threads
 * @see_also: #GstRTSPMedia, #GstRTSPClient
 *
 * A #GstRTSPThreadPool manages reusable threads for various server tasks.
 * Currently the defined thread types can be found in #GstRTSPThreadType.
 *
 * Threads of type #GST_RTSP_THREAD_TYPE_CLIENT are used to handle requests from
 * a connected client. With gst_rtsp_thread_pool_get_max_threads() a maximum
 * number of threads can be set after which the pool will start to reuse the
 * same thread for multiple clients.
 *
 * Threads of type #GST_RTSP_THREAD_TYPE_MEDIA will be used to perform the state
 * changes of the media pipelines and handle its bus messages.
 *
 * gst_rtsp_thread_pool_get_thread() can be used to create a #GstRTSPThread
 * object of the right type. The thread object contains a mainloop and context
 * that run in a seperate thread and can be used to attached sources to.
 *
 * gst_rtsp_thread_reuse() can be used to reuse a thread for multiple purposes.
 * If all gst_rtsp_thread_reuse() calls are matched with a
 * gst_rtsp_thread_stop() call, the mainloop will be quit and the thread will
 * stop.
 *
 * To configure the threads, a subclass of this object should be made and the
 * virtual methods should be overriden to implement the desired functionality.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-thread-pool.h"

typedef struct _GstRTSPThreadImpl
{
  GstRTSPThread thread;

  gint reused;
  GSource *source;
  /* FIXME, the source has to be part of GstRTSPThreadImpl, due to a bug in GLib:
   * https://bugzilla.gnome.org/show_bug.cgi?id=720186 */
} GstRTSPThreadImpl;

GST_DEFINE_MINI_OBJECT_TYPE (GstRTSPThread, gst_rtsp_thread);

static void gst_rtsp_thread_init (GstRTSPThreadImpl * impl);

static void
_gst_rtsp_thread_free (GstRTSPThreadImpl * impl)
{
  GST_DEBUG ("free thread %p", impl);

  g_source_unref (impl->source);
  g_main_loop_unref (impl->thread.loop);
  g_main_context_unref (impl->thread.context);
  g_free (impl);
}

static GstRTSPThread *
_gst_rtsp_thread_copy (GstRTSPThreadImpl * impl)
{
  GstRTSPThreadImpl *copy;

  GST_DEBUG ("copy thread %p", impl);

  copy = g_new0 (GstRTSPThreadImpl, 1);
  gst_rtsp_thread_init (copy);
  copy->thread.context = g_main_context_ref (impl->thread.context);
  copy->thread.loop = g_main_loop_ref (impl->thread.loop);

  return GST_RTSP_THREAD (copy);
}

static void
gst_rtsp_thread_init (GstRTSPThreadImpl * impl)
{
  gst_mini_object_init (GST_MINI_OBJECT_CAST (impl), 0,
      GST_TYPE_RTSP_THREAD,
      (GstMiniObjectCopyFunction) _gst_rtsp_thread_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_rtsp_thread_free);

  g_atomic_int_set (&impl->reused, 1);
}

/**
 * gst_rtsp_thread_new:
 * @type: the thread type
 *
 * Create a new thread object that can run a mainloop.
 *
 * Returns: (transfer full): a #GstRTSPThread.
 */
GstRTSPThread *
gst_rtsp_thread_new (GstRTSPThreadType type)
{
  GstRTSPThreadImpl *impl;

  impl = g_new0 (GstRTSPThreadImpl, 1);

  gst_rtsp_thread_init (impl);
  impl->thread.type = type;
  impl->thread.context = g_main_context_new ();
  impl->thread.loop = g_main_loop_new (impl->thread.context, TRUE);

  return GST_RTSP_THREAD (impl);
}

/**
 * gst_rtsp_thread_reuse:
 * @thread: (transfer none): a #GstRTSPThread
 *
 * Reuse the mainloop of @thread
 *
 * Returns: %TRUE if the mainloop could be reused
 */
gboolean
gst_rtsp_thread_reuse (GstRTSPThread * thread)
{
  GstRTSPThreadImpl *impl = (GstRTSPThreadImpl *) thread;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_THREAD (thread), FALSE);

  GST_DEBUG ("reuse thread %p", thread);

  res = g_atomic_int_add (&impl->reused, 1) > 0;
  if (res)
    gst_rtsp_thread_ref (thread);

  return res;
}

static gboolean
do_quit (GstRTSPThread * thread)
{
  GST_DEBUG ("stop mainloop of thread %p", thread);
  g_main_loop_quit (thread->loop);
  return FALSE;
}

/**
 * gst_rtsp_thread_stop:
 * @thread: (transfer full): a #GstRTSPThread
 *
 * Stop and unref @thread. When no threads are using the mainloop, the thread
 * will be stopped and the final ref to @thread will be released.
 */
void
gst_rtsp_thread_stop (GstRTSPThread * thread)
{
  GstRTSPThreadImpl *impl = (GstRTSPThreadImpl *) thread;

  g_return_if_fail (GST_IS_RTSP_THREAD (thread));

  GST_DEBUG ("stop thread %p", thread);

  if (g_atomic_int_dec_and_test (&impl->reused)) {
    GST_DEBUG ("add idle source to quit mainloop of thread %p", thread);
    impl->source = g_idle_source_new ();
    g_source_set_callback (impl->source, (GSourceFunc) do_quit,
        thread, (GDestroyNotify) gst_rtsp_thread_unref);
    g_source_attach (impl->source, thread->context);
  } else
    gst_rtsp_thread_unref (thread);
}

struct _GstRTSPThreadPoolPrivate
{
  GMutex lock;

  gint max_threads;
  /* currently used mainloops */
  GQueue threads;
};

#define DEFAULT_MAX_THREADS 1

enum
{
  PROP_0,
  PROP_MAX_THREADS,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_thread_pool_debug);
#define GST_CAT_DEFAULT rtsp_thread_pool_debug

static GQuark thread_pool;

static void gst_rtsp_thread_pool_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_thread_pool_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_thread_pool_finalize (GObject * obj);

static gpointer do_loop (GstRTSPThread * thread);
static GstRTSPThread *default_get_thread (GstRTSPThreadPool * pool,
    GstRTSPThreadType type, GstRTSPContext * ctx);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPThreadPool, gst_rtsp_thread_pool,
    G_TYPE_OBJECT);

static void
gst_rtsp_thread_pool_class_init (GstRTSPThreadPoolClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_thread_pool_get_property;
  gobject_class->set_property = gst_rtsp_thread_pool_set_property;
  gobject_class->finalize = gst_rtsp_thread_pool_finalize;

  /**
   * GstRTSPThreadPool::max-threads:
   *
   * The maximum amount of threads to use for client connections. A value of
   * 0 means to use only the mainloop, -1 means an unlimited amount of
   * threads.
   */
  g_object_class_install_property (gobject_class, PROP_MAX_THREADS,
      g_param_spec_int ("max-threads", "Max Threads",
          "The maximum amount of threads to use for client connections "
          "(0 = only mainloop, -1 = unlimited)", -1, G_MAXINT,
          DEFAULT_MAX_THREADS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->get_thread = default_get_thread;

  GST_DEBUG_CATEGORY_INIT (rtsp_thread_pool_debug, "rtspthreadpool", 0,
      "GstRTSPThreadPool");

  thread_pool = g_quark_from_string ("gst.rtsp.thread.pool");
}

static void
gst_rtsp_thread_pool_init (GstRTSPThreadPool * pool)
{
  GstRTSPThreadPoolPrivate *priv;

  pool->priv = priv = gst_rtsp_thread_pool_get_instance_private (pool);

  g_mutex_init (&priv->lock);
  priv->max_threads = DEFAULT_MAX_THREADS;
  g_queue_init (&priv->threads);
}

static void
gst_rtsp_thread_pool_finalize (GObject * obj)
{
  GstRTSPThreadPool *pool = GST_RTSP_THREAD_POOL (obj);
  GstRTSPThreadPoolPrivate *priv = pool->priv;

  GST_INFO ("finalize pool %p", pool);

  g_queue_clear (&priv->threads);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_thread_pool_parent_class)->finalize (obj);
}

static void
gst_rtsp_thread_pool_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPThreadPool *pool = GST_RTSP_THREAD_POOL (object);

  switch (propid) {
    case PROP_MAX_THREADS:
      g_value_set_int (value, gst_rtsp_thread_pool_get_max_threads (pool));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_thread_pool_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPThreadPool *pool = GST_RTSP_THREAD_POOL (object);

  switch (propid) {
    case PROP_MAX_THREADS:
      gst_rtsp_thread_pool_set_max_threads (pool, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static gpointer
do_loop (GstRTSPThread * thread)
{
  GstRTSPThreadPoolPrivate *priv;
  GstRTSPThreadPoolClass *klass;
  GstRTSPThreadPool *pool;

  pool = gst_mini_object_get_qdata (GST_MINI_OBJECT (thread), thread_pool);
  priv = pool->priv;

  klass = GST_RTSP_THREAD_POOL_GET_CLASS (pool);

  if (klass->thread_enter)
    klass->thread_enter (pool, thread);

  GST_INFO ("enter mainloop of thread %p", thread);
  g_main_loop_run (thread->loop);
  GST_INFO ("exit mainloop of thread %p", thread);

  if (klass->thread_leave)
    klass->thread_leave (pool, thread);

  g_mutex_lock (&priv->lock);
  g_queue_remove (&priv->threads, thread);
  g_mutex_unlock (&priv->lock);

  gst_rtsp_thread_unref (thread);

  return NULL;
}

/**
 * gst_rtsp_thread_pool_new:
 *
 * Create a new #GstRTSPThreadPool instance.
 *
 * Returns: (transfer full): a new #GstRTSPThreadPool
 */
GstRTSPThreadPool *
gst_rtsp_thread_pool_new (void)
{
  GstRTSPThreadPool *result;

  result = g_object_new (GST_TYPE_RTSP_THREAD_POOL, NULL);

  return result;
}

/**
 * gst_rtsp_thread_pool_set_max_threads:
 * @pool: a #GstRTSPThreadPool
 * @max_threads: maximum threads
 *
 * Set the maximum threads used by the pool to handle client requests.
 * A value of 0 will use the pool mainloop, a value of -1 will use an
 * unlimited number of threads.
 */
void
gst_rtsp_thread_pool_set_max_threads (GstRTSPThreadPool * pool,
    gint max_threads)
{
  GstRTSPThreadPoolPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_THREAD_POOL (pool));

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  priv->max_threads = max_threads;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_thread_pool_get_max_threads:
 * @pool: a #GstRTSPThreadPool
 *
 * Get the maximum number of threads used for client connections.
 * See gst_rtsp_thread_pool_set_max_threads().
 *
 * Returns: the maximum number of threads.
 */
gint
gst_rtsp_thread_pool_get_max_threads (GstRTSPThreadPool * pool)
{
  GstRTSPThreadPoolPrivate *priv;
  gint res;

  g_return_val_if_fail (GST_IS_RTSP_THREAD_POOL (pool), -1);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  res = priv->max_threads;
  g_mutex_unlock (&priv->lock);

  return res;
}

static GstRTSPThread *
make_thread (GstRTSPThreadPool * pool, GstRTSPThreadType type,
    GstRTSPContext * ctx)
{
  GstRTSPThreadPoolClass *klass;
  GstRTSPThread *thread;

  klass = GST_RTSP_THREAD_POOL_GET_CLASS (pool);

  thread = gst_rtsp_thread_new (type);
  gst_mini_object_set_qdata (GST_MINI_OBJECT (thread), thread_pool,
      g_object_ref (pool), g_object_unref);

  GST_DEBUG_OBJECT (pool, "new thread %p", thread);

  if (klass->configure_thread)
    klass->configure_thread (pool, thread, ctx);

  return thread;
}

static GstRTSPThread *
default_get_thread (GstRTSPThreadPool * pool,
    GstRTSPThreadType type, GstRTSPContext * ctx)
{
  GstRTSPThreadPoolPrivate *priv = pool->priv;
  GstRTSPThreadPoolClass *klass;
  GstRTSPThread *thread;
  GError *error = NULL;

  klass = GST_RTSP_THREAD_POOL_GET_CLASS (pool);

  switch (type) {
    case GST_RTSP_THREAD_TYPE_CLIENT:
      if (priv->max_threads == 0) {
        /* no threads allowed */
        GST_DEBUG_OBJECT (pool, "no client threads allowed");
        thread = NULL;
      } else {
        g_mutex_lock (&priv->lock);
      retry:
        if (priv->max_threads > 0 &&
            g_queue_get_length (&priv->threads) >= priv->max_threads) {
          /* max threads reached, recycle from queue */
          thread = g_queue_pop_head (&priv->threads);
          GST_DEBUG_OBJECT (pool, "recycle client thread %p", thread);
          if (!gst_rtsp_thread_reuse (thread)) {
            GST_DEBUG_OBJECT (pool, "thread %p stopping, retry", thread);
            /* this can happen if we just decremented the reuse counter of the
             * thread and signaled the mainloop that it should stop. We leave
             * the thread out of the queue now, there is no point to add it
             * again, it will be removed from the mainloop otherwise after it
             * stops. */
            goto retry;
          }
        } else {
          /* make more threads */
          GST_DEBUG_OBJECT (pool, "make new client thread");
          thread = make_thread (pool, type, ctx);

          if (!g_thread_pool_push (klass->pool, gst_rtsp_thread_ref (thread),
                  &error))
            goto thread_error;
        }
        g_queue_push_tail (&priv->threads, thread);
        g_mutex_unlock (&priv->lock);
      }
      break;
    case GST_RTSP_THREAD_TYPE_MEDIA:
      GST_DEBUG_OBJECT (pool, "make new media thread");
      thread = make_thread (pool, type, ctx);

      if (!g_thread_pool_push (klass->pool, gst_rtsp_thread_ref (thread),
              &error))
        goto thread_error;
      break;
    default:
      thread = NULL;
      break;
  }
  return thread;

  /* ERRORS */
thread_error:
  {
    GST_ERROR_OBJECT (pool, "failed to push thread %s", error->message);
    gst_rtsp_thread_unref (thread);
    /* drop also the ref dedicated for the pool */
    gst_rtsp_thread_unref (thread);
    g_clear_error (&error);
    return NULL;
  }
}

/**
 * gst_rtsp_thread_pool_get_thread:
 * @pool: a #GstRTSPThreadPool
 * @type: the #GstRTSPThreadType
 * @ctx: (transfer none): a #GstRTSPContext
 *
 * Get a new #GstRTSPThread for @type and @ctx.
 *
 * Returns: (transfer full) (nullable): a new #GstRTSPThread,
 * gst_rtsp_thread_stop() after usage
 */
GstRTSPThread *
gst_rtsp_thread_pool_get_thread (GstRTSPThreadPool * pool,
    GstRTSPThreadType type, GstRTSPContext * ctx)
{
  GstRTSPThreadPoolClass *klass;
  GstRTSPThread *result = NULL;

  g_return_val_if_fail (GST_IS_RTSP_THREAD_POOL (pool), NULL);

  klass = GST_RTSP_THREAD_POOL_GET_CLASS (pool);

  /* We want to be thread safe as there might be 2 threads wanting to get new
   * #GstRTSPThread at the same time
   */
  if (G_UNLIKELY (!g_atomic_pointer_get (&klass->pool))) {
    GThreadPool *t_pool;
    t_pool = g_thread_pool_new ((GFunc) do_loop, klass, -1, FALSE, NULL);
    if (!g_atomic_pointer_compare_and_exchange (&klass->pool,
            (GThreadPool *) NULL, t_pool))
      g_thread_pool_free (t_pool, FALSE, TRUE);
  }

  if (klass->get_thread)
    result = klass->get_thread (pool, type, ctx);

  return result;
}

/**
 * gst_rtsp_thread_pool_cleanup:
 *
 * Wait for all tasks to be stopped and free all allocated resources. This is
 * mainly used in test suites to ensure proper cleanup of internal data
 * structures.
 */
void
gst_rtsp_thread_pool_cleanup (void)
{
  GstRTSPThreadPoolClass *klass;

  klass =
      GST_RTSP_THREAD_POOL_CLASS (g_type_class_ref
      (gst_rtsp_thread_pool_get_type ()));
  if (klass->pool != NULL) {
    g_thread_pool_free (klass->pool, FALSE, TRUE);
    klass->pool = NULL;
  }
  g_type_class_unref (klass);
}
