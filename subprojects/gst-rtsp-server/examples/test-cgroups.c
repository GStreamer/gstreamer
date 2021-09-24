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

/* Runs a pipeline and clasifies the media pipelines based on the
 * authenticated user.
 *
 * This test requires 2 cpu cgroups to exist named 'user' and 'admin'.
 * The rtsp server should have permission to add its threads to the
 * cgroups.
 *
 *   sudo cgcreate -t uid:gid -g cpu:/user
 *   sudo cgcreate -t uid:gid -g cpu:/admin
 *
 * With -t you can give the user and group access to the task file to
 * write the thread ids. The user running the server can be used.
 *
 * Then you would want to change the cpu shares assigned to each group:
 *
 *   sudo cgset -r cpu.shares=100 user
 *   sudo cgset -r cpu.shares=1024 admin
 *
 * Then start clients for 'user' until the stream is degraded because of
 * lack of CPU. Then start a client for 'admin' and check that the stream
 * is not degraded.
 */

#include <libcgroup.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

typedef struct _GstRTSPCGroupPool GstRTSPCGroupPool;
typedef struct _GstRTSPCGroupPoolClass GstRTSPCGroupPoolClass;

#define GST_TYPE_RTSP_CGROUP_POOL              (gst_rtsp_cgroup_pool_get_type ())
#define GST_IS_RTSP_CGROUP_POOL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_CGROUP_POOL))
#define GST_IS_RTSP_CGROUP_POOL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_CGROUP_POOL))
#define GST_RTSP_CGROUP_POOL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_CGROUP_POOL, GstRTSPCGroupPoolClass))
#define GST_RTSP_CGROUP_POOL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_CGROUP_POOL, GstRTSPCGroupPool))
#define GST_RTSP_CGROUP_POOL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_CGROUP_POOL, GstRTSPCGroupPoolClass))
#define GST_RTSP_CGROUP_POOL_CAST(obj)         ((GstRTSPCGroupPool*)(obj))
#define GST_RTSP_CGROUP_POOL_CLASS_CAST(klass) ((GstRTSPCGroupPoolClass*)(klass))

struct _GstRTSPCGroupPool
{
  GstRTSPThreadPool parent;

  struct cgroup *user;
  struct cgroup *admin;
};

struct _GstRTSPCGroupPoolClass
{
  GstRTSPThreadPoolClass parent_class;
};

static GQuark thread_cgroup;

static void gst_rtsp_cgroup_pool_finalize (GObject * obj);

static void default_thread_enter (GstRTSPThreadPool * pool,
    GstRTSPThread * thread);
static void default_configure_thread (GstRTSPThreadPool * pool,
    GstRTSPThread * thread, GstRTSPContext * ctx);

static GType gst_rtsp_cgroup_pool_get_type (void);

G_DEFINE_TYPE (GstRTSPCGroupPool, gst_rtsp_cgroup_pool,
    GST_TYPE_RTSP_THREAD_POOL);

static void
gst_rtsp_cgroup_pool_class_init (GstRTSPCGroupPoolClass * klass)
{
  GObjectClass *gobject_class;
  GstRTSPThreadPoolClass *tpool_class;

  gobject_class = G_OBJECT_CLASS (klass);
  tpool_class = GST_RTSP_THREAD_POOL_CLASS (klass);

  gobject_class->finalize = gst_rtsp_cgroup_pool_finalize;

  tpool_class->configure_thread = default_configure_thread;
  tpool_class->thread_enter = default_thread_enter;

  thread_cgroup = g_quark_from_string ("cgroup.pool.thread.cgroup");

  cgroup_init ();
}

static void
gst_rtsp_cgroup_pool_init (GstRTSPCGroupPool * pool)
{
  pool->user = cgroup_new_cgroup ("user");
  if (cgroup_add_controller (pool->user, "cpu") == NULL)
    g_error ("Failed to add cpu controller to user cgroup");
  pool->admin = cgroup_new_cgroup ("admin");
  if (cgroup_add_controller (pool->admin, "cpu") == NULL)
    g_error ("Failed to add cpu controller to admin cgroup");
}

static void
gst_rtsp_cgroup_pool_finalize (GObject * obj)
{
  GstRTSPCGroupPool *pool = GST_RTSP_CGROUP_POOL (obj);

  GST_INFO ("finalize pool %p", pool);

  cgroup_free (&pool->user);
  cgroup_free (&pool->admin);

  G_OBJECT_CLASS (gst_rtsp_cgroup_pool_parent_class)->finalize (obj);
}

static void
default_thread_enter (GstRTSPThreadPool * pool, GstRTSPThread * thread)
{
  struct cgroup *cgroup;

  cgroup = gst_mini_object_get_qdata (GST_MINI_OBJECT (thread), thread_cgroup);
  if (cgroup) {
    gint res = 0;

    res = cgroup_attach_task (cgroup);

    if (res != 0)
      GST_ERROR ("error: %d (%s)", res, cgroup_strerror (res));
  }
}

static void
default_configure_thread (GstRTSPThreadPool * pool,
    GstRTSPThread * thread, GstRTSPContext * ctx)
{
  GstRTSPCGroupPool *cpool = GST_RTSP_CGROUP_POOL (pool);
  const gchar *cls;
  struct cgroup *cgroup;

  if (ctx->token)
    cls = gst_rtsp_token_get_string (ctx->token, "cgroup.pool.media.class");
  else
    cls = NULL;

  GST_DEBUG ("manage cgroup %s", cls);

  if (!g_strcmp0 (cls, "admin"))
    cgroup = cpool->admin;
  else
    cgroup = cpool->user;

  /* attach the cgroup to the thread */
  gst_mini_object_set_qdata (GST_MINI_OBJECT (thread), thread_cgroup,
      cgroup, NULL);
}

static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GstRTSPAuth *auth;
  GstRTSPToken *token;
  gchar *basic;
  GstRTSPThreadPool *thread_pool;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* get the mounts for this server, every server has a default mapper object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines. 
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, "( "
      "videotestsrc ! video/x-raw,width=640,height=480,framerate=50/1 ! "
      "x264enc ! rtph264pay name=pay0 pt=96 "
      "audiotestsrc ! audio/x-raw,rate=8000 ! "
      "alawenc ! rtppcmapay name=pay1 pt=97 " ")");
  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* allow user and admin to access this resource */
  gst_rtsp_media_factory_add_role (factory, "user",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_media_factory_add_role (factory, "admin",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, TRUE, NULL);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* make a new authentication manager */
  auth = gst_rtsp_auth_new ();

  /* make user token */
  token = gst_rtsp_token_new ("cgroup.pool.media.class", G_TYPE_STRING, "user",
      "media.factory.role", G_TYPE_STRING, "user", NULL);
  basic = gst_rtsp_auth_make_basic ("user", "password");
  gst_rtsp_auth_add_basic (auth, basic, token);
  g_free (basic);
  gst_rtsp_token_unref (token);

  /* make admin token */
  token = gst_rtsp_token_new ("cgroup.pool.media.class", G_TYPE_STRING, "admin",
      "media.factory.role", G_TYPE_STRING, "admin", NULL);
  basic = gst_rtsp_auth_make_basic ("admin", "power");
  gst_rtsp_auth_add_basic (auth, basic, token);
  g_free (basic);
  gst_rtsp_token_unref (token);

  /* set as the server authentication manager */
  gst_rtsp_server_set_auth (server, auth);
  g_object_unref (auth);

  thread_pool = g_object_new (GST_TYPE_RTSP_CGROUP_POOL, NULL);
  gst_rtsp_server_set_thread_pool (server, thread_pool);
  g_object_unref (thread_pool);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);

  /* start serving */
  g_print ("stream with user:password ready at rtsp://127.0.0.1:8554/test\n");
  g_print ("stream with admin:power ready at rtsp://127.0.0.1:8554/test\n");
  g_main_loop_run (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
