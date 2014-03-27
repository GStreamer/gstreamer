/*
Copyright (c) 2012, Broadcom Europe Ltd
Copyright (c) 2012, OtherCrashOverride
Copyright (C) 2013, Fluendo S.A.
   @author: Josep Torra <josep@fluendo.com>
Copyright (C) 2013, Video Experts Group LLC.
   @author: Ilya Smelykh <ilya@videoexpertsgroup.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* A rotating cube rendered with OpenGL|ES and video played using GStreamer on
 * the cube faces.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <gst/gst.h>

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_EGL) && defined (__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#include "bcm_host.h"

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_EGL) && defined (__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

#include <GLES/gl.h>
#include <GLES/glext.h>

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_EGL) && defined (__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#define EGL_EGLEXT_PROTOTYPES
#include <gst/egl/egl.h>

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_EGL) && defined (__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

#include "cube_texture_and_coords.h"

#ifndef M_PI
#define M_PI 3.141592654
#endif

#define SYNC_BUFFERS TRUE

#define TRACE_VC_MEMORY_ENABLED 0

#if TRACE_VC_MEMORY_ENABLED
#define TRACE_VC_MEMORY(str)                 \
  fprintf (stderr, "\n\n" str "\n");         \
  system ("vcdbg reloc >&2")

#define TRACE_VC_MEMORY_DEFINE_ID(id)        \
  static int id = 0

#define TRACE_VC_MEMORY_RESET_ID(id)         \
  G_STMT_START {                             \
    id = 0;                                  \
  } G_STMT_END

#define TRACE_VC_MEMORY_ONCE_FOR_ID(str,id)  \
  G_STMT_START {                             \
    if (id == 0) {                           \
      fprintf (stderr, "\n\n" str "\n");     \
      system ("vcdbg reloc >&2");            \
      id = 1;                                \
    }                                        \
  } G_STMT_END

#define TRACE_VC_MEMORY_ONCE(str,id)         \
  G_STMT_START {                             \
    static int id = 0;                       \
    if (id == 0) {                           \
      fprintf (stderr, "\n\n" str "\n");     \
      system ("vcdbg reloc >&2");            \
      id = 1;                                \
    }                                        \
  } G_STMT_END

#else
#define TRACE_VC_MEMORY(str) while(0)
#define TRACE_VC_MEMORY_DEFINE_ID(id)
#define TRACE_VC_MEMORY_RESET_ID(id) while(0)
#define TRACE_VC_MEMORY_ONCE_FOR_ID(str,id) while(0)
#define TRACE_VC_MEMORY_ONCE(str,id) while(0)
#endif

typedef struct
{
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_ELEMENT_HANDLE_T dispman_element;

  uint32_t screen_width;
  uint32_t screen_height;
  gboolean animate;

  /* OpenGL|ES objects */
  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;
  GLuint tex;

  /* model rotation vector and direction */
  GLfloat rot_angle_x_inc;
  GLfloat rot_angle_y_inc;
  GLfloat rot_angle_z_inc;

  /* current model rotation angles */
  GLfloat rot_angle_x;
  GLfloat rot_angle_y;
  GLfloat rot_angle_z;

  /* current distance from camera */
  GLfloat distance;
  GLfloat distance_inc;

  /* GStreamer related resources */
  GstElement *pipeline;
  GstElement *vsink;
  GstEGLDisplay *gst_display;

  /* Interthread comunication */
  GAsyncQueue *queue;
  GMutex *queue_lock;
  GCond *cond;
  gboolean flushing;
  GstMiniObject *popped_obj;
  GstBuffer *current_buffer;

  GstBufferPool *pool;
  /* GLib mainloop */
  GMainLoop *main_loop;
  GstBuffer *last_buffer;

  GstCaps *current_caps;

  /* Rendering thread state */
  gboolean running;

  /* number of rendered and dropped frames */
  guint64 rendered;
  guint64 dropped;
} APP_STATE_T;

typedef struct
{
  GThread *thread;
  EGLImageKHR image;
  GLuint texture;
  APP_STATE_T *state;
} GstEGLGLESImageData;

/* EGLImage memory, buffer pool, etc */
typedef struct
{
  GstVideoBufferPool parent;

  APP_STATE_T *state;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstVideoInfo info;
  gboolean add_metavideo;
  gboolean want_eglimage;
  GstEGLDisplay *display;
} GstCustomEGLImageBufferPool;

typedef GstVideoBufferPoolClass GstCustomEGLImageBufferPoolClass;

#define GST_CUSTOM_EGL_IMAGE_BUFFER_POOL(p) ((GstCustomEGLImageBufferPool*)(p))

GType gst_custom_egl_image_buffer_pool_get_type (void);

G_DEFINE_TYPE (GstCustomEGLImageBufferPool, gst_custom_egl_image_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

static void init_ogl (APP_STATE_T * state);
static void init_model_proj (APP_STATE_T * state);
static void reset_model (APP_STATE_T * state);
static GLfloat inc_and_wrap_angle (GLfloat angle, GLfloat angle_inc);
static GLfloat inc_and_clip_distance (GLfloat distance, GLfloat distance_inc);
static void redraw_scene (APP_STATE_T * state);
static void update_model (APP_STATE_T * state);
static void init_textures (APP_STATE_T * state);
static APP_STATE_T _state, *state = &_state;
static GstBufferPool *gst_custom_egl_image_buffer_pool_new (APP_STATE_T * state,
    GstEGLDisplay * display);
static gboolean queue_object (APP_STATE_T * state, GstMiniObject * obj,
    gboolean synchronous);

TRACE_VC_MEMORY_DEFINE_ID (gid0);
TRACE_VC_MEMORY_DEFINE_ID (gid1);
TRACE_VC_MEMORY_DEFINE_ID (gid2);

typedef enum
{
  GST_PLAY_FLAG_VIDEO = (1 << 0),
  GST_PLAY_FLAG_AUDIO = (1 << 1),
  GST_PLAY_FLAG_TEXT = (1 << 2),
  GST_PLAY_FLAG_VIS = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD = (1 << 7),
  GST_PLAY_FLAG_BUFFERING = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10)
} GstPlayFlags;

static gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

static gboolean
got_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS) {
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned 0x%04x", wtf,
        error);
    return TRUE;
  }

  return FALSE;
}

static void
image_data_free (GstEGLGLESImageData * data)
{
  if (data->thread == g_thread_self ()) {
    eglDestroyImageKHR (state->display, data->image);
    glDeleteTextures (1, &data->texture);
  } else {
    GstQuery *query;
    GstStructure *s;
    s = gst_structure_new ("eglglessink-deallocate-eglimage",
        "EGLImage", G_TYPE_POINTER, data->image,
        "GLTexture", G_TYPE_POINTER, data->texture, NULL);
    query = gst_query_new_custom (GST_QUERY_CUSTOM, s);
    queue_object (state, GST_MINI_OBJECT_CAST (query), FALSE);
  }
  g_slice_free (GstEGLGLESImageData, data);
}

static GstBuffer *
gst_egl_allocate_eglimage (APP_STATE_T * ctx,
    GstAllocator * allocator, GstVideoFormat format, gint width, gint height)
{
  GstEGLGLESImageData *data = NULL;
  GstBuffer *buffer;
  GstVideoInfo info;
  gint i;
  gint stride[3];
  gsize offset[3];
  GstMemory *mem[3] = { NULL, NULL, NULL };
  guint n_mem;
  GstMemoryFlags flags = 0;

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  if (!gst_egl_image_memory_is_mappable ())
    flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  /* See https://bugzilla.gnome.org/show_bug.cgi?id=695203 */
  flags |= GST_MEMORY_FLAG_NO_SHARE;

  gst_video_info_set_format (&info, format, width, height);

  GST_DEBUG ("Allocating EGL Image format %s width %d height %d",
      gst_video_format_to_string (format), width, height);
  switch (format) {
    case GST_VIDEO_FORMAT_RGBA:{
      gsize size;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, ctx->gst_display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);

      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);
        data->thread = g_thread_self ();
        data->state = ctx;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 4);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        data->image =
            eglCreateImageKHR (gst_egl_display_get (ctx->gst_display),
            ctx->context, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (guintptr) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, ctx->gst_display,
            data->image, GST_VIDEO_GL_TEXTURE_TYPE_RGBA, flags, size, data,
            (GDestroyNotify) image_data_free);

        n_mem = 1;
      }
    }
      break;
    default:
      goto mem_error;
      break;
  }

  buffer = gst_buffer_new ();
  gst_buffer_add_video_meta_full (buffer, 0, format, width, height,
      GST_VIDEO_INFO_N_PLANES (&info), offset, stride);

  /* n_mem could be reused for planar colorspaces, for now its == 1 for RGBA */
  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return buffer;
mem_error:
  {
    GST_ERROR ("Failed to create EGLImage");

    if (data)
      image_data_free (data);

    if (mem[0])
      gst_memory_unref (mem[0]);

    return NULL;
  }

}

static const gchar **
gst_custom_egl_image_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_custom_egl_image_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstCustomEGLImageBufferPool *pool = GST_CUSTOM_EGL_IMAGE_BUFFER_POOL (bpool);
  GstCaps *caps;
  GstVideoInfo info;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (!GST_BUFFER_POOL_CLASS
      (gst_custom_egl_image_buffer_pool_parent_class)->set_config (bpool,
          config))
    return FALSE;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
      || !caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (!gst_buffer_pool_config_get_allocator (config, &pool->allocator,
          &pool->params))
    return FALSE;
  if (pool->allocator)
    gst_object_ref (pool->allocator);

  pool->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  pool->want_eglimage = (pool->allocator
      && g_strcmp0 (pool->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);

  pool->info = info;

  return TRUE;
}

static GstFlowReturn
gst_custom_egl_image_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstCustomEGLImageBufferPool *pool = GST_CUSTOM_EGL_IMAGE_BUFFER_POOL (bpool);
  *buffer = NULL;

  if (!pool->add_metavideo || !pool->want_eglimage)
    return
        GST_BUFFER_POOL_CLASS
        (gst_custom_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
        buffer, params);

  if (!pool->allocator)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (pool->info.finfo->format) {
    case GST_VIDEO_FORMAT_RGBA:{
      GstFlowReturn ret;
      GstQuery *query;
      GstStructure *s;
      const GValue *v;

      s = gst_structure_new ("eglglessink-allocate-eglimage",
          "format", GST_TYPE_VIDEO_FORMAT, pool->info.finfo->format,
          "width", G_TYPE_INT, pool->info.width,
          "height", G_TYPE_INT, pool->info.height, NULL);
      query = gst_query_new_custom (GST_QUERY_CUSTOM, s);

      ret = queue_object (state, GST_MINI_OBJECT_CAST (query), TRUE);

      if (ret != TRUE || !gst_structure_has_field (s, "buffer")) {
        GST_WARNING ("Fallback memory allocation");
        gst_query_unref (query);
        return
            GST_BUFFER_POOL_CLASS
            (gst_custom_egl_image_buffer_pool_parent_class)->alloc_buffer
            (bpool, buffer, params);
      }

      v = gst_structure_get_value (s, "buffer");
      *buffer = GST_BUFFER_CAST (g_value_get_pointer (v));
      gst_query_unref (query);

      if (!*buffer) {
        GST_WARNING ("Fallback memory allocation");
        return
            GST_BUFFER_POOL_CLASS
            (gst_custom_egl_image_buffer_pool_parent_class)->alloc_buffer
            (bpool, buffer, params);
      }

      return GST_FLOW_OK;
      break;
    }
    default:
      return
          GST_BUFFER_POOL_CLASS
          (gst_custom_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
          buffer, params);
      break;
  }

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_custom_egl_image_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstCustomEGLImageBufferPool *pool;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_custom_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
      buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  pool = GST_CUSTOM_EGL_IMAGE_BUFFER_POOL (bpool);

  /* XXX: Don't return the memory we just rendered, glEGLImageTargetTexture2DOES()
   * keeps the EGLImage unmappable until the next one is uploaded
   */
  if (*buffer && *buffer == pool->state->current_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret =
        GST_BUFFER_POOL_CLASS
        (gst_custom_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
        buffer, params);
    gst_object_replace ((GstObject **) & oldbuf->pool, (GstObject *) pool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

static void
gst_custom_egl_image_buffer_pool_finalize (GObject * object)
{
  GstCustomEGLImageBufferPool *pool = GST_CUSTOM_EGL_IMAGE_BUFFER_POOL (object);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (pool->display)
    gst_egl_display_unref (pool->display);
  pool->display = NULL;

  G_OBJECT_CLASS (gst_custom_egl_image_buffer_pool_parent_class)->finalize
      (object);
}

static void
gst_custom_egl_image_buffer_pool_class_init (GstCustomEGLImageBufferPoolClass *
    klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_custom_egl_image_buffer_pool_finalize;
  gstbufferpool_class->get_options =
      gst_custom_egl_image_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_custom_egl_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer =
      gst_custom_egl_image_buffer_pool_alloc_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_custom_egl_image_buffer_pool_acquire_buffer;
}

static void
gst_custom_egl_image_buffer_pool_init (GstCustomEGLImageBufferPool * pool)
{
}

static GstBufferPool *
gst_custom_egl_image_buffer_pool_new (APP_STATE_T * state,
    GstEGLDisplay * display)
{
  GstCustomEGLImageBufferPool *pool;

  pool = g_object_new (gst_custom_egl_image_buffer_pool_get_type (), NULL);
  pool->display = gst_egl_display_ref (state->gst_display);
  pool->state = state;

  return (GstBufferPool *) pool;
}

/***********************************************************
 * Name: init_ogl
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the display, OpenGL|ES context and screen stuff
 *
 * Returns: void
 *
 ***********************************************************/
static void
init_ogl (APP_STATE_T * state)
{
  int32_t success = 0;
  EGLBoolean result;
  EGLint num_config;

  static EGL_DISPMANX_WINDOW_T nativewindow;

  DISPMANX_UPDATE_HANDLE_T dispman_update;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;

  static const EGLint attribute_list[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    //EGL_SAMPLES, 4,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };

  EGLConfig config;

  /* get an EGL display connection */
  state->display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  assert (state->display != EGL_NO_DISPLAY);

  /* initialize the EGL display connection */
  result = eglInitialize (state->display, NULL, NULL);
  assert (EGL_FALSE != result);

  /* get an appropriate EGL frame buffer configuration
   * this uses a BRCM extension that gets the closest match, rather
   * than standard which returns anything that matches. */
  result =
      eglSaneChooseConfigBRCM (state->display, attribute_list, &config, 1,
      &num_config);
  assert (EGL_FALSE != result);

  /* create an EGL rendering context */
  state->context =
      eglCreateContext (state->display, config, EGL_NO_CONTEXT, NULL);
  assert (state->context != EGL_NO_CONTEXT);

  /* create an EGL window surface */
  success = graphics_get_display_size (0 /* LCD */ , &state->screen_width,
      &state->screen_height);
  assert (success >= 0);

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = state->screen_width;
  dst_rect.height = state->screen_height;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = state->screen_width << 16;
  src_rect.height = state->screen_height << 16;

  state->dispman_display = vc_dispmanx_display_open (0 /* LCD */ );
  dispman_update = vc_dispmanx_update_start (0);

  state->dispman_element =
      vc_dispmanx_element_add (dispman_update, state->dispman_display,
      0 /*layer */ , &dst_rect, 0 /*src */ ,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha */ , 0 /*clamp */ ,
      0 /*transform */ );

  nativewindow.element = state->dispman_element;
  nativewindow.width = state->screen_width;
  nativewindow.height = state->screen_height;
  vc_dispmanx_update_submit_sync (dispman_update);

  state->surface =
      eglCreateWindowSurface (state->display, config, &nativewindow, NULL);
  assert (state->surface != EGL_NO_SURFACE);

  /* connect the context to the surface */
  result =
      eglMakeCurrent (state->display, state->surface, state->surface,
      state->context);
  assert (EGL_FALSE != result);

  /* Set background color and clear buffers */
  glClearColor (0.15f, 0.25f, 0.35f, 1.0f);

  /* Enable back face culling. */
  glEnable (GL_CULL_FACE);

  glMatrixMode (GL_MODELVIEW);
}

/***********************************************************
 * Name: init_model_proj
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the OpenGL|ES model to default values
 *
 * Returns: void
 *
 ***********************************************************/
static void
init_model_proj (APP_STATE_T * state)
{
  float nearp = 1.0f;
  float farp = 500.0f;
  float hht;
  float hwd;

  glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

  glViewport (0, 0, (GLsizei) state->screen_width,
      (GLsizei) state->screen_height);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  hht = nearp * (float) tan (45.0 / 2.0 / 180.0 * M_PI);
  hwd = hht * (float) state->screen_width / (float) state->screen_height;

  glFrustumf (-hwd, hwd, -hht, hht, nearp, farp);

  glEnableClientState (GL_VERTEX_ARRAY);
  glVertexPointer (3, GL_BYTE, 0, quadx);

  reset_model (state);
}

/***********************************************************
 * Name: reset_model
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description: Resets the Model projection and rotation direction
 *
 * Returns: void
 *
 ***********************************************************/
static void
reset_model (APP_STATE_T * state)
{
  /* reset model position */
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.f, 0.f, -50.f);

  /* reset model rotation */
  state->rot_angle_x = 45.f;
  state->rot_angle_y = 30.f;
  state->rot_angle_z = 0.f;
  state->rot_angle_x_inc = 0.5f;
  state->rot_angle_y_inc = 0.5f;
  state->rot_angle_z_inc = 0.f;
  state->distance = 40.f;
}

/***********************************************************
 * Name: update_model
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description: Updates model projection to current position/rotation
 *
 * Returns: void
 *
 ***********************************************************/
static void
update_model (APP_STATE_T * state)
{
  if (state->animate) {
    /* update position */
    state->rot_angle_x =
        inc_and_wrap_angle (state->rot_angle_x, state->rot_angle_x_inc);
    state->rot_angle_y =
        inc_and_wrap_angle (state->rot_angle_y, state->rot_angle_y_inc);
    state->rot_angle_z =
        inc_and_wrap_angle (state->rot_angle_z, state->rot_angle_z_inc);
    state->distance =
        inc_and_clip_distance (state->distance, state->distance_inc);
  }

  glLoadIdentity ();
  /* move camera back to see the cube */
  glTranslatef (0.f, 0.f, -state->distance);

  /* Rotate model to new position */
  glRotatef (state->rot_angle_x, 1.f, 0.f, 0.f);
  glRotatef (state->rot_angle_y, 0.f, 1.f, 0.f);
  glRotatef (state->rot_angle_z, 0.f, 0.f, 1.f);
}

/***********************************************************
 * Name: inc_and_wrap_angle
 *
 * Arguments:
 *       GLfloat angle     current angle
 *       GLfloat angle_inc angle increment
 *
 * Description:   Increments or decrements angle by angle_inc degrees
 *                Wraps to 0 at 360 deg.
 *
 * Returns: new value of angle
 *
 ***********************************************************/
static GLfloat
inc_and_wrap_angle (GLfloat angle, GLfloat angle_inc)
{
  angle += angle_inc;

  if (angle >= 360.0)
    angle -= 360.f;
  else if (angle <= 0)
    angle += 360.f;

  return angle;
}

/***********************************************************
 * Name: inc_and_clip_distance
 *
 * Arguments:
 *       GLfloat distance     current distance
 *       GLfloat distance_inc distance increment
 *
 * Description:   Increments or decrements distance by distance_inc units
 *                Clips to range
 *
 * Returns: new value of angle
 *
 ***********************************************************/
static GLfloat
inc_and_clip_distance (GLfloat distance, GLfloat distance_inc)
{
  distance += distance_inc;

  if (distance >= 120.0f)
    distance = 120.f;
  else if (distance <= 40.0f)
    distance = 40.0f;

  return distance;
}

/***********************************************************
 * Name: redraw_scene
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description:   Draws the model and calls eglSwapBuffers
 *                to render to screen
 *
 * Returns: void
 *
 ***********************************************************/
static void
redraw_scene (APP_STATE_T * state)
{
  /* Start with a clear screen */
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* Need to rotate textures - do this by rotating each cube face */
  glRotatef (270.f, 0.f, 0.f, 1.f);     /* front face normal along z axis */

  /* draw first 4 vertices */
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);

  /* same pattern for other 5 faces - rotation chosen to make image orientation 'nice' */
  glRotatef (90.f, 0.f, 0.f, 1.f);      /* back face normal along z axis */
  glDrawArrays (GL_TRIANGLE_STRIP, 4, 4);

  glRotatef (90.f, 1.f, 0.f, 0.f);      /* left face normal along x axis */
  glDrawArrays (GL_TRIANGLE_STRIP, 8, 4);

  glRotatef (90.f, 1.f, 0.f, 0.f);      /* right face normal along x axis */
  glDrawArrays (GL_TRIANGLE_STRIP, 12, 4);

  glRotatef (270.f, 0.f, 1.f, 0.f);     /* top face normal along y axis */
  glDrawArrays (GL_TRIANGLE_STRIP, 16, 4);

  glRotatef (90.f, 0.f, 1.f, 0.f);      /* bottom face normal along y axis */
  glDrawArrays (GL_TRIANGLE_STRIP, 20, 4);

  eglSwapBuffers (state->display, state->surface);
}

/***********************************************************
 * Name: init_textures
 *
 * Arguments:
 *       APP_STATE_T *state - holds OGLES model info
 *
 * Description:   Initialise OGL|ES texture surfaces to use image
 *                buffers
 *
 * Returns: void
 *
 ***********************************************************/
static void
init_textures (APP_STATE_T * state)
{
  glGenTextures (1, &state->tex);

  glBindTexture (GL_TEXTURE_2D, state->tex);

#if 0
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#else
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* setup overall texture environment */
  glTexCoordPointer (2, GL_FLOAT, 0, texCoords);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glEnable (GL_TEXTURE_2D);

  /* Bind texture surface to current vertices */
  glBindTexture (GL_TEXTURE_2D, state->tex);
}

static void
render_scene (APP_STATE_T * state)
{
  update_model (state);
  redraw_scene (state);
  TRACE_VC_MEMORY_ONCE_FOR_ID ("after render_scene", gid2);

  return;
}

static void
update_image (APP_STATE_T * state, GstBuffer * buffer)
{
  GstMemory *mem = NULL;

  if (state->current_buffer) {
    gst_buffer_unref (state->current_buffer);
  }
  state->current_buffer = gst_buffer_ref (buffer);

  mem = gst_buffer_peek_memory (buffer, 0);

  TRACE_VC_MEMORY_ONCE_FOR_ID ("before glEGLImageTargetTexture2DOES", gid0);

  glBindTexture (GL_TEXTURE_2D, state->tex);
  glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
      gst_egl_image_memory_get_image (mem));

  TRACE_VC_MEMORY_ONCE_FOR_ID ("after glEGLImageTargetTexture2DOES", gid1);
}

static void
init_intercom (APP_STATE_T * state)
{
  state->queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
  state->queue_lock = g_mutex_new ();
  state->cond = g_cond_new ();
}

static void
terminate_intercom (APP_STATE_T * state)
{
  /* Release intercom */
  if (state->queue) {
    g_async_queue_unref (state->queue);
  }

  if (state->queue_lock) {
    g_mutex_free (state->queue_lock);
  }

  if (state->cond) {
    g_cond_free (state->cond);
  }
}

static void
flush_internal (APP_STATE_T * state)
{
  if (state->current_buffer) {
    gst_buffer_unref (state->current_buffer);
  }
  state->current_buffer = NULL;
}

static void
flush_start (APP_STATE_T * state)
{
  GstMiniObject *object = NULL;

  g_mutex_lock (state->queue_lock);
  state->flushing = TRUE;
  g_cond_broadcast (state->cond);
  g_mutex_unlock (state->queue_lock);

  while ((object = g_async_queue_try_pop (state->queue))) {
    gst_mini_object_unref (object);
  }
  g_mutex_lock (state->queue_lock);
  flush_internal (state);
  state->popped_obj = NULL;
  g_mutex_unlock (state->queue_lock);
}

static void
flush_stop (APP_STATE_T * state)
{
  GstMiniObject *object = NULL;

  g_mutex_lock (state->queue_lock);
  while ((object = GST_MINI_OBJECT_CAST (g_async_queue_try_pop (state->queue)))) {
    gst_mini_object_unref (object);
  }
  flush_internal (state);
  state->popped_obj = NULL;
  state->flushing = FALSE;
  g_mutex_unlock (state->queue_lock);
}

static void
pipeline_pause (APP_STATE_T * state)
{
  gst_element_set_state (state->pipeline, GST_STATE_PAUSED);
}

static void
pipeline_play (APP_STATE_T * state)
{
  gst_element_set_state (state->pipeline, GST_STATE_PLAYING);
}

static gint64
pipeline_get_position (APP_STATE_T * state)
{
  gint64 position = -1;

  if (state->pipeline) {
    gst_element_query_position (state->vsink, GST_FORMAT_TIME, &position);
  }

  return position;
}

static gint64
pipeline_get_duration (APP_STATE_T * state)
{
  gint64 duration = -1;

  if (state->pipeline) {
    gst_element_query_duration (state->pipeline, GST_FORMAT_TIME, &duration);
  }

  return duration;
}

static void
pipeline_seek (APP_STATE_T * state, gint64 position)
{
  if (state->pipeline) {
    GstEvent *event;
    event = gst_event_new_seek (1.0,
        GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
    if (!gst_element_send_event (state->vsink, event)) {
      g_print ("seek failed\n");
    }
  }
}

static gboolean
handle_queued_objects (APP_STATE_T * state)
{
  GstMiniObject *object = NULL;

  g_mutex_lock (state->queue_lock);
  if (state->flushing) {
    g_cond_broadcast (state->cond);
    goto beach;
  } else if (g_async_queue_length (state->queue) == 0) {
    goto beach;
  }

  if ((object = g_async_queue_try_pop (state->queue))) {
    if (GST_IS_BUFFER (object)) {
      GstBuffer *buffer = GST_BUFFER_CAST (object);
      update_image (state, buffer);
      render_scene (state);
      gst_buffer_unref (buffer);
      if (!SYNC_BUFFERS) {
        object = NULL;
      }
    } else if (GST_IS_QUERY (object)) {
      GstQuery *query = GST_QUERY_CAST (object);
      GstStructure *s = (GstStructure *) gst_query_get_structure (query);

      if (gst_structure_has_name (s, "eglglessink-allocate-eglimage")) {
        GstBuffer *buffer;
        GstVideoFormat format;
        gint width, height;
        GValue v = { 0, };

        if (!gst_structure_get_enum (s, "format", GST_TYPE_VIDEO_FORMAT,
                (gint *) & format)
            || !gst_structure_get_int (s, "width", &width)
            || !gst_structure_get_int (s, "height", &height)) {
          g_assert_not_reached ();
        }

        buffer =
            gst_egl_allocate_eglimage (state,
            GST_CUSTOM_EGL_IMAGE_BUFFER_POOL (state->pool)->allocator, format,
            width, height);
        g_value_init (&v, G_TYPE_POINTER);
        g_value_set_pointer (&v, buffer);

        gst_structure_set_value (s, "buffer", &v);
        g_value_unset (&v);
      } else if (gst_structure_has_name (s, "eglglessink-deallocate-eglimage")) {
        gpointer _image, _texture;
        EGLImageKHR image;
        GLuint texture;
        gst_structure_get (s, "EGLImage", G_TYPE_POINTER, &_image, "GLTexture",
            G_TYPE_POINTER, &_texture, NULL);
        image = (EGLImageKHR) _image;
        texture = (GLuint) _texture;
        eglDestroyImageKHR (state->display, image);
        glDeleteTextures (1, &texture);
      } else {
        g_assert_not_reached ();
      }
    } else if (GST_IS_EVENT (object)) {
      GstEvent *event = GST_EVENT_CAST (object);
      g_print ("\nevent %p %s\n", event,
          gst_event_type_get_name (GST_EVENT_TYPE (event)));

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          flush_internal (state);
          break;
        default:
          break;
      }
      gst_event_unref (event);
      object = NULL;
    }
  }

  if (object) {
    state->popped_obj = object;
    g_cond_broadcast (state->cond);
  }

beach:
  g_mutex_unlock (state->queue_lock);

  return FALSE;
}

static gboolean
queue_object (APP_STATE_T * state, GstMiniObject * obj, gboolean synchronous)
{
  gboolean res = TRUE;

  g_mutex_lock (state->queue_lock);
  if (state->flushing) {
    gst_mini_object_unref (obj);
    res = FALSE;
    goto beach;
  }

  g_async_queue_push (state->queue, obj);

  if (synchronous) {
    /* Waiting for object to be handled */
    do {
      g_cond_wait (state->cond, state->queue_lock);
    } while (!state->flushing && state->popped_obj != obj);
  }

beach:
  g_mutex_unlock (state->queue_lock);
  return res;
}

static void
preroll_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  APP_STATE_T *state = (APP_STATE_T *) user_data;
  queue_object (state, GST_MINI_OBJECT_CAST (gst_buffer_ref (buffer)), FALSE);
}

static void
buffers_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  APP_STATE_T *state = (APP_STATE_T *) user_data;
  queue_object (state, GST_MINI_OBJECT_CAST (gst_buffer_ref (buffer)),
      SYNC_BUFFERS);
}

static GstPadProbeReturn
events_cb (GstPad * pad, GstPadProbeInfo * probe_info, gpointer user_data)
{
  APP_STATE_T *state = (APP_STATE_T *) user_data;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (probe_info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      flush_start (state);
      break;
    case GST_EVENT_FLUSH_STOP:
      flush_stop (state);
      break;
    case GST_EVENT_EOS:
      queue_object (state, GST_MINI_OBJECT_CAST (gst_event_ref (event)), FALSE);
      break;
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
query_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  APP_STATE_T *state = (APP_STATE_T *) user_data;
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:{
      GstBufferPool *pool;
      GstStructure *config;
      GstCaps *caps;
      GstVideoInfo info;
      gboolean need_pool;
      guint size;
      GstAllocator *allocator;
      GstAllocationParams params;

      gst_allocation_params_init (&params);

      gst_query_parse_allocation (query, &caps, &need_pool);

      if (!caps) {
        GST_ERROR ("allocation query without caps");
        return GST_PAD_PROBE_OK;
      }

      if (!gst_video_info_from_caps (&info, caps)) {
        GST_ERROR ("allocation query with invalid caps");
        return GST_PAD_PROBE_OK;
      }

      g_mutex_lock (state->queue_lock);
      pool = state->pool ? gst_object_ref (state->pool) : NULL;
      g_mutex_unlock (state->queue_lock);

      if (pool) {
        GstCaps *pcaps;

        /* we had a pool, check caps */

        config = gst_buffer_pool_get_config (pool);
        gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);
        GST_DEBUG ("check existing pool caps %" GST_PTR_FORMAT
            " with new caps %" GST_PTR_FORMAT, pcaps, caps);

        if (!gst_caps_is_equal (caps, pcaps)) {
          GST_DEBUG ("pool has different caps");
          /* different caps, we can't use this pool */
          gst_object_unref (pool);
          pool = NULL;
        }
        gst_structure_free (config);
      }

      GST_DEBUG ("pool %p", pool);
      if (pool == NULL && need_pool) {
        GstVideoInfo info;

        if (!gst_video_info_from_caps (&info, caps)) {
          GST_ERROR ("allocation query has invalid caps %"
              GST_PTR_FORMAT, caps);
          return GST_PAD_PROBE_OK;
        }

        GST_DEBUG ("create new pool");
        state->pool = pool =
            gst_custom_egl_image_buffer_pool_new (state, state->display);
        GST_DEBUG ("done create new pool %p", pool);
        /* the normal size of a frame */
        size = info.size;

        config = gst_buffer_pool_get_config (pool);
        /* we need at least 2 buffer because we hold on to the last one */
        gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
        gst_buffer_pool_config_set_allocator (config, NULL, &params);
        if (!gst_buffer_pool_set_config (pool, config)) {
          gst_object_unref (pool);
          GST_ERROR ("failed to set pool configuration");
          return GST_PAD_PROBE_OK;
        }
      }

      if (pool) {
        /* we need at least 2 buffer because we hold on to the last one */
        gst_query_add_allocation_pool (query, pool, size, 2, 0);
        gst_object_unref (pool);
      }

      /* First the default allocator */
      if (!gst_egl_image_memory_is_mappable ()) {
        allocator = gst_allocator_find (NULL);
        gst_query_add_allocation_param (query, allocator, &params);
        gst_object_unref (allocator);
      }

      allocator = gst_egl_image_allocator_obtain ();
      GST_WARNING ("Allocator obtained %p", allocator);

      if (!gst_egl_image_memory_is_mappable ())
        params.flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
      gst_query_add_allocation_param (query, allocator, &params);
      gst_object_unref (allocator);

      gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
      gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
      gst_query_add_allocation_meta (query,
          GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL);

      GST_DEBUG ("done alocation");
      return GST_PAD_PROBE_OK;
    }
      break;
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
init_playbin_player (APP_STATE_T * state, const gchar * uri)
{
  GstElement *vsink;

  vsink = gst_element_factory_make ("fakesink", "vsink");
  g_object_set (vsink, "sync", TRUE, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE,
      "max-lateness", 20 * GST_MSECOND, "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  gst_pad_add_probe (gst_element_get_static_pad (vsink, "sink"),
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state, NULL);
  gst_pad_add_probe (gst_element_get_static_pad (vsink, "sink"),
      GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state, NULL);

  /* Instantiate and configure playbin */
  state->pipeline = gst_element_factory_make ("playbin", "player");
  g_object_set (state->pipeline, "uri", uri,
      "video-sink", vsink, "flags",
      GST_PLAY_FLAG_NATIVE_VIDEO | GST_PLAY_FLAG_AUDIO, NULL);

  state->vsink = gst_object_ref (vsink);
  return TRUE;
}

static gboolean
init_parse_launch_player (APP_STATE_T * state, const gchar * spipeline)
{
  GstElement *vsink;
  GError *error = NULL;

  state->pipeline = gst_parse_launch (spipeline, &error);

  if (!state->pipeline) {
    g_printerr ("Unable to instatiate pipeline '%s': %s\n",
        spipeline, error->message);
    return FALSE;
  }

  vsink = gst_bin_get_by_name (GST_BIN (state->pipeline), "vsink");

  if (!vsink) {
    g_printerr ("Unable to find a fakesink named 'vsink'");
    return FALSE;
  }

  g_object_set (vsink, "sync", TRUE, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE,
      "max-lateness", 20 * GST_MSECOND, "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  gst_pad_add_probe (gst_element_get_static_pad (vsink, "sink"),
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state, NULL);
  gst_pad_add_probe (gst_element_get_static_pad (vsink, "sink"),
      GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state, NULL);

  state->vsink = gst_object_ref (vsink);
  return TRUE;
}

//------------------------------------------------------------------------------

static void
report_position_duration (APP_STATE_T * state)
{
  gint64 position, duration;

  duration = pipeline_get_duration (state);
  position = pipeline_get_position (state);

  if (position != -1) {
    g_print ("\n position / duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));
  } else {
    g_print ("\n position / duration: unknown");
  }

  if (duration != -1) {
    g_print (" / %" GST_TIME_FORMAT, GST_TIME_ARGS (duration));
  } else {
    g_print (" / unknown");
  }
  g_print ("\n");
}

static void
seek_forward (APP_STATE_T * state)
{
  gint64 position, duration;

  duration = pipeline_get_duration (state);
  position = pipeline_get_position (state);

  if (position != -1) {
    position += 30 * GST_SECOND;
    if (duration != -1) {
      position = MIN (position, duration);
    }
    pipeline_seek (state, position);
  }
}

static void
seek_backward (APP_STATE_T * state)
{
  gint64 position;

  position = pipeline_get_position (state);

  if (position != -1) {
    position -= 30 * GST_SECOND;
    position = MAX (position, 0);
    pipeline_seek (state, position);
  }
}

#define SKIP(t) \
  while (*t) { \
    if ((*t == ' ') || (*t == '\n') || (*t == '\t') || (*t == '\r')) \
      t++; \
    else \
      break; \
  }

/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond, APP_STATE_T * state)
{
  gchar *str = NULL;
  char op;

  if (g_io_channel_read_line (source, &str, NULL, NULL,
          NULL) == G_IO_STATUS_NORMAL) {

    gchar *cmd = str;
    SKIP (cmd)
        op = *cmd;
    cmd++;
    switch (op) {
      case 'a':
        if (state->animate) {
          state->animate = FALSE;
        } else {
          state->animate = TRUE;
        }
        break;
      case 'p':
        pipeline_pause (state);
        break;
      case 'r':
        pipeline_play (state);
        break;
      case 'l':
        report_position_duration (state);
        break;
      case 'f':
        seek_forward (state);
        break;
      case 'b':
        seek_backward (state);
        break;
      case 'q':
        flush_start (state);
        gst_element_set_state (state->pipeline, GST_STATE_READY);
        break;
    }
  }
  g_free (str);
  return TRUE;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, GstPipeline * data)
{
  return GST_BUS_PASS;
}

/* on error print the error and quit the application */
static void
error_cb (GstBus * bus, GstMessage * msg, APP_STATE_T * state)
{
  GError *err;
  gchar *debug_info;

  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("Error received from element %s: %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);
  flush_start (state);
  gst_element_set_state (state->pipeline, GST_STATE_READY);
}

/* buffering */
static void
buffering_cb (GstBus * bus, GstMessage * msg, APP_STATE_T * state)
{
  gint percent;

  gst_message_parse_buffering (msg, &percent);
  g_print ("Buffering %3d%%\r", percent);
  if (percent < 100)
    pipeline_pause (state);
  else {
    g_print ("\n");
    pipeline_play (state);
  }
}

/* on EOS just quit the application */
static void
eos_cb (GstBus * bus, GstMessage * msg, APP_STATE_T * state)
{
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (state->pipeline)) {
    g_print ("End-Of-Stream reached.\n");
    gst_element_set_state (state->pipeline, GST_STATE_READY);
  }
}

static void
state_changed_cb (GstBus * bus, GstMessage * msg, APP_STATE_T * state)
{
  GstState old_state, new_state, pending_state;
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (state->pipeline)) {
    gst_message_parse_state_changed (msg, &old_state, &new_state,
        &pending_state);
    g_print ("State changed to %s\n", gst_element_state_get_name (new_state));
    if (old_state == GST_STATE_PAUSED && new_state == GST_STATE_READY) {
      g_main_loop_quit (state->main_loop);
    }
  }
}

static void
qos_cb (GstBus * bus, GstMessage * msg, APP_STATE_T * state)
{
  GstFormat fmt = GST_FORMAT_BUFFERS;
  gchar *name = gst_element_get_name (GST_MESSAGE_SRC (msg));
  gst_message_parse_qos_stats (msg, &fmt, &state->rendered, &state->dropped);
  g_print ("%s rendered: %" G_GUINT64_FORMAT " dropped: %" G_GUINT64_FORMAT
      " %s\n",
      name, state->rendered, state->dropped,
      (fmt == GST_FORMAT_BUFFERS ? "frames" : "samples"));
  g_free (name);
}

//==============================================================================

static void
close_ogl (void)
{
  DISPMANX_UPDATE_HANDLE_T dispman_update;

  /* clear screen */
  glClear (GL_COLOR_BUFFER_BIT);
  eglSwapBuffers (state->display, state->surface);

  /* Release OpenGL resources */
  eglMakeCurrent (state->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
      EGL_NO_CONTEXT);
  eglDestroySurface (state->display, state->surface);
  eglDestroyContext (state->display, state->context);
  gst_egl_display_unref (state->gst_display);

  dispman_update = vc_dispmanx_update_start (0);
  vc_dispmanx_element_remove (dispman_update, state->dispman_element);
  vc_dispmanx_update_submit_sync (dispman_update);
  vc_dispmanx_display_close (state->dispman_display);
}

//==============================================================================

static void
open_ogl (void)
{
  TRACE_VC_MEMORY ("state 0");

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_EGL)
  bcm_host_init ();
  TRACE_VC_MEMORY ("after bcm_host_init");
#endif

  /* Start OpenGLES */
  init_ogl (state);
  TRACE_VC_MEMORY ("after init_ogl");

  /* Wrap the EGL display */
  state->gst_display = gst_egl_display_new (state->display, NULL);

  /* Setup the model world */
  init_model_proj (state);
  TRACE_VC_MEMORY ("after init_model_proj");

  /* initialize the OGLES texture(s) */
  init_textures (state);
  TRACE_VC_MEMORY ("after init_textures");
}

static gpointer
render_func (gpointer data)
{
  open_ogl ();
  state->running = TRUE;

  do {
    handle_queued_objects (state);
    g_usleep (0);
  } while (state->running == TRUE);

  close_ogl ();
  return NULL;
}

int
main (int argc, char **argv)
{
  GstBus *bus;
  GOptionContext *ctx;
  GIOChannel *io_stdin;
  GError *err = NULL;
  gboolean res;
  GOptionEntry options[] = {
    {NULL}
  };
  GThread *rthread;

  /* Clear application state */
  memset (state, 0, sizeof (*state));
  state->animate = TRUE;
  state->current_buffer = NULL;

  /* must initialise the threading system before using any other GLib funtion */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("[ADDITIONAL ARGUMENTS]");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    exit (1);
  }
  g_option_context_free (ctx);

  if (argc != 2) {
    g_print ("Usage: %s <URI> or <PIPELINE-DESCRIPTION>\n", argv[0]);
    exit (1);
  }

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* initialize inter thread comunnication */
  init_intercom (state);

  TRACE_VC_MEMORY ("state 0");

  if (!(rthread = g_thread_new ("render", (GThreadFunc) render_func, NULL))) {
    g_print ("Render thread create failed\n");
    exit (1);
  }

  /* Initialize player */
  if (gst_uri_is_valid (argv[1])) {
    res = init_playbin_player (state, argv[1]);
  } else {
    res = init_parse_launch_player (state, argv[1]);
  }

  if (!res)
    goto done;

  /* Create a GLib Main Loop and set it to run */
  state->main_loop = g_main_loop_new (NULL, FALSE);

  /* Add a keyboard watch so we get notified of keystrokes */
  io_stdin = g_io_channel_unix_new (fileno (stdin));
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, state);
  g_io_channel_unref (io_stdin);

  /* *INDENT-OFF* */
  g_print ("Available commands: \n"
      "  a - Toggle animation \n"
      "  p - Pause playback \n"
      "  r - Resume playback \n"
      "  l - Query position/duration\n"
      "  f - Seek 30 seconds forward \n"
      "  b - Seek 30 seconds backward \n"
      "  q - Quit \n");
  /* *INDENT-ON* */

  /* Connect the bus handlers */
  bus = gst_element_get_bus (state->pipeline);

  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, state,
      NULL);

  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  gst_bus_enable_sync_message_emission (bus);

  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      state);
  g_signal_connect (G_OBJECT (bus), "message::buffering",
      (GCallback) buffering_cb, state);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, state);
  g_signal_connect (G_OBJECT (bus), "message::qos", (GCallback) qos_cb, state);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      (GCallback) state_changed_cb, state);
  gst_object_unref (bus);

  /* Make player start playing */
  gst_element_set_state (state->pipeline, GST_STATE_PLAYING);

  /* Start the mainloop */
  state->main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (state->main_loop);

done:
  /* Release pipeline */
  if (state->pipeline) {
    gst_element_set_state (state->pipeline, GST_STATE_NULL);
    if (state->vsink) {
      gst_object_unref (state->vsink);
      state->vsink = NULL;
    }

    gst_object_unref (state->pipeline);
  }

  /* Unref the mainloop */
  if (state->main_loop) {
    g_main_loop_unref (state->main_loop);
  }

  /* Stop rendering thread */
  state->running = FALSE;
  g_thread_join (rthread);

  terminate_intercom (state);

  TRACE_VC_MEMORY ("at exit");
  return 0;
}
