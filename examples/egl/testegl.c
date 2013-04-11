/*
Copyright (c) 2012, Broadcom Europe Ltd
Copyright (c) 2012, OtherCrashOverride
Copyright (C) 2013, Fluendo S.A.
   @author: Josep Torra <josep@fluendo.com>
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
  gboolean sync_animation_with_video;

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
  GstEGLDisplay *gst_display;

  /* Interthread comunication */
  GAsyncQueue *queue;
  GMutex *lock;
  GCond *cond;
  gboolean flushing;
  GstMiniObject *popped_obj;
  GstEGLImageMemory *current_mem;

  /* GLib mainloop */
  GMainLoop *main_loop;
} APP_STATE_T;

static void init_ogl (APP_STATE_T * state);
static void init_model_proj (APP_STATE_T * state);
static void reset_model (APP_STATE_T * state);
static GLfloat inc_and_wrap_angle (GLfloat angle, GLfloat angle_inc);
static GLfloat inc_and_clip_distance (GLfloat distance, GLfloat distance_inc);
static void redraw_scene (APP_STATE_T * state);
static void update_model (APP_STATE_T * state);
static void init_textures (APP_STATE_T * state);
static APP_STATE_T _state, *state = &_state;

TRACE_VC_MEMORY_DEFINE_ID (gid0);
TRACE_VC_MEMORY_DEFINE_ID (gid1);
TRACE_VC_MEMORY_DEFINE_ID (gid2);

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
destroy_pool_resources (GstEGLImageMemoryPool * pool, gpointer user_data)
{
  APP_STATE_T *state = (APP_STATE_T *) user_data;
  gint i, size = gst_egl_image_memory_pool_get_size (pool);
  EGLClientBuffer client_buffer;
  EGLImageKHR image;
  EGLint error;

  TRACE_VC_MEMORY ("before pool destruction");
  for (i = 0; i < size; i++) {
    if (gst_egl_image_memory_pool_get_resources (pool, i, &client_buffer,
            &image)) {
      GLuint tid = (GLuint) client_buffer;
      error = EGL_SUCCESS;

      if (image != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR (state->display, image);
        if ((error = eglGetError ()) != EGL_SUCCESS) {
          g_print ("eglDestroyImageKHR failed %x\n", error);
        }
      }

      if (tid) {
        error = GL_NO_ERROR;
        glDeleteTextures (1, &tid);
        if ((error = glGetError ()) != GL_NO_ERROR) {
          g_print ("glDeleteTextures failed %x\n", error);
        }
      }
      g_print ("destroyed texture %x image %p\n", tid, image);
    }
  }
  TRACE_VC_MEMORY ("after pool destruction");
}

static GstEGLImageMemoryPool *
create_pool (APP_STATE_T * state, gint size, gint width, gint height)
{
  GstEGLImageMemoryPool *pool;
  gint i;
  EGLint error;

  TRACE_VC_MEMORY ("before pool creation");
  pool = gst_egl_image_memory_pool_new (size, state->gst_display, state,
      destroy_pool_resources);

  for (i = 0; i < size; i++) {
    GLuint tid;
    EGLImageKHR image;

    error = GL_NO_ERROR;
    glGenTextures (1, &tid);
    if ((error = glGetError ()) != GL_NO_ERROR) {
      g_print ("glGenTextures failed %x\n", error);
      goto failed;
    }

    glBindTexture (GL_TEXTURE_2D, tid);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, NULL);
    if ((error = glGetError ()) != GL_NO_ERROR) {
      g_print ("glTexImage2D failed %x\n", error);
      goto failed;
    }

    /* Create EGL Image */
    error = EGL_SUCCESS;
    image = eglCreateImageKHR (state->display,
        state->context, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) tid, 0);

    if (image == EGL_NO_IMAGE_KHR) {
      if ((error = eglGetError ()) != EGL_SUCCESS) {
        g_print ("eglCreateImageKHR failed %x\n", error);
      } else {
        g_print ("eglCreateImageKHR failed.\n");
      }
      goto failed;
    }
    g_print ("created texture %x image %p\n", tid, image);
    gst_egl_image_memory_pool_set_resources (pool, i, (EGLClientBuffer) tid,
        image);
  }

  TRACE_VC_MEMORY ("after pool creation");
  TRACE_VC_MEMORY_RESET_ID (gid0);
  TRACE_VC_MEMORY_RESET_ID (gid1);
  TRACE_VC_MEMORY_RESET_ID (gid2);

  return pool;

failed:
  gst_egl_image_memory_pool_unref (pool);
  return NULL;
}

static gboolean
render_scene (APP_STATE_T * state)
{
  update_model (state);
  redraw_scene (state);
  TRACE_VC_MEMORY_ONCE_FOR_ID ("after render_scene", gid2);

  return !state->sync_animation_with_video;
}

static void
update_image (APP_STATE_T * state, GstBuffer * buffer)
{
  GstEGLImageMemory *mem = (GstEGLImageMemory *) GST_BUFFER_DATA (buffer);

  g_mutex_lock (state->lock);
  if (state->current_mem) {
    gst_egl_image_memory_unref (state->current_mem);
  }
  state->current_mem = gst_egl_image_memory_ref (mem);
  g_mutex_unlock (state->lock);

  TRACE_VC_MEMORY_ONCE_FOR_ID ("before glEGLImageTargetTexture2DOES", gid0);
  glBindTexture (GL_TEXTURE_2D, state->tex);
  glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
      gst_egl_image_memory_get_image (mem));
  TRACE_VC_MEMORY_ONCE_FOR_ID ("after glEGLImageTargetTexture2DOES", gid1);

  if (state->sync_animation_with_video) {
    render_scene (state);
  }
}

static void
init_intercom (APP_STATE_T * state)
{
  state->queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
  state->lock = g_mutex_new ();
  state->cond = g_cond_new ();
}

static void
flush_internal (APP_STATE_T * state)
{
  if (state->current_mem) {
    gst_egl_image_memory_unref (state->current_mem);
  }
  state->current_mem = NULL;
}

static void
flush_start (APP_STATE_T * state)
{
  GstMiniObject *object = NULL;

  g_mutex_lock (state->lock);
  state->flushing = TRUE;
  g_cond_broadcast (state->cond);
  g_mutex_unlock (state->lock);

  while ((object = g_async_queue_try_pop (state->queue))) {
    gst_mini_object_unref (object);
  }

  flush_internal (state);
}

static void
flush_stop (APP_STATE_T * state)
{
  g_mutex_lock (state->lock);
  state->popped_obj = NULL;
  state->flushing = FALSE;
  g_mutex_unlock (state->lock);
}

static void
pipeline_pause (APP_STATE_T * state)
{
  flush_start (state);
  gst_element_set_state (state->pipeline, GST_STATE_PAUSED);
  flush_stop (state);
}

static gint64
pipeline_get_position (APP_STATE_T * state)
{
  gint64 position = -1;

  if (state->pipeline) {
    gst_element_query_position (state->pipeline, GST_FORMAT_TIME, &position);
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
    if (!gst_element_send_event (state->pipeline, event)) {
      g_print ("seek failed\n");
    }
  }
}

static gboolean
handle_queued_objects (APP_STATE_T * state)
{
  GstMiniObject *object = NULL;

  if (g_async_queue_length (state->queue) == 0) {
    return FALSE;
  }

  while ((object = g_async_queue_try_pop (state->queue))) {

    g_mutex_lock (state->lock);
    if (state->flushing) {
      state->popped_obj = object;
      gst_mini_object_unref (object);
      g_cond_broadcast (state->cond);
      g_mutex_unlock (state->lock);
      continue;
    }
    g_mutex_unlock (state->lock);

    if (GST_IS_BUFFER (object)) {
      GstBuffer *buffer = GST_BUFFER_CAST (object);
      update_image (state, buffer);
      gst_buffer_unref (buffer);
    } else if (GST_IS_MESSAGE (object)) {
      GstMessage *message = GST_MESSAGE_CAST (object);
      g_print ("\nmessage %p ", message);
      if (gst_structure_has_name (message->structure, "need-egl-pool")) {
        GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));
        gint size, width, height;

        gst_message_parse_need_egl_pool (message, &size, &width, &height);

        g_print ("need-egl-pool, size %d width %d height %d\n", size, width,
            height);

        if (g_object_class_find_property (G_OBJECT_GET_CLASS (element), "pool")) {
          GstEGLImageMemoryPool *pool = NULL;

          if ((pool = create_pool (state, size, width, height))) {
            g_object_set (element, "pool", pool, NULL);
          }
        }
      }
      gst_message_unref (message);
    } else if (GST_IS_EVENT (object)) {
      GstEvent *event = GST_EVENT_CAST (object);
      g_print ("\nevent %p %s\n", event,
          gst_event_type_get_name (GST_EVENT_TYPE (event)));

      g_mutex_lock (state->lock);
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          flush_internal (state);
          break;
        default:
          break;
      }
      g_mutex_unlock (state->lock);
      gst_event_unref (event);
    }

    g_mutex_lock (state->lock);
    state->popped_obj = object;
    g_cond_broadcast (state->cond);
    g_mutex_unlock (state->lock);
  }

  return FALSE;
}

static gboolean
queue_object (APP_STATE_T * state, GstMiniObject * obj, gboolean synchronous)
{
  g_mutex_lock (state->lock);
  if (state->flushing) {
    g_mutex_unlock (state->lock);
    gst_mini_object_unref (obj);
    return FALSE;
  }

  g_async_queue_push (state->queue, obj);


  if (state->sync_animation_with_video) {
    g_idle_add_full (G_PRIORITY_HIGH_IDLE, (GSourceFunc) handle_queued_objects,
        state, NULL);
  }

  if (synchronous) {
    /* Waiting for object to be handled */
    do {
      g_cond_wait (state->cond, state->lock);
    } while (!state->flushing && state->popped_obj != obj);
  }
  g_mutex_unlock (state->lock);

  return TRUE;
}

static gboolean
handle_msgs_and_render_scene (APP_STATE_T * state)
{
  handle_queued_objects (state);
  return render_scene (state);
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
  queue_object (state, GST_MINI_OBJECT_CAST (gst_buffer_ref (buffer)), TRUE);
}

static gboolean
events_cb (GstPad * pad, GstEvent * event, gpointer user_data)
{
  APP_STATE_T *state = (APP_STATE_T *) user_data;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      flush_start (state);
      break;
    case GST_EVENT_FLUSH_STOP:
      flush_stop (state);
      break;
    case GST_EVENT_EOS:
      queue_object (state, GST_MINI_OBJECT_CAST (gst_event_ref (event)), TRUE);
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
init_playbin_player (APP_STATE_T * state, const gchar * uri)
{
  GstElement *asink;
  GstElement *vsink;

  vsink = gst_element_factory_make ("fakesink", "vsink");
  g_object_set (vsink, "sync", TRUE, "silent", TRUE,
      "enable-last-buffer", FALSE,
      "max-lateness", 20 * GST_MSECOND, "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  gst_pad_add_event_probe (gst_element_get_static_pad (vsink, "sink"),
      G_CALLBACK (events_cb), state);

#if 0
  asink = gst_element_factory_make ("fakesink", "asink");
  g_object_set (asink, "sync", TRUE, "silent", TRUE, NULL);
#else
  asink = gst_element_factory_make ("alsasink", "asink");
#endif

  /* Instantiate and configure playbin */
  state->pipeline = gst_element_factory_make ("playbin", "player");
  g_object_set (state->pipeline, "uri", uri,
      "video-sink", vsink, "audio-sink", asink, NULL);

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

  g_object_set (vsink, "sync", TRUE, "silent", TRUE,
      "enable-last-buffer", FALSE,
      "max-lateness", 20 * GST_MSECOND, "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  gst_pad_add_event_probe (gst_element_get_static_pad (vsink, "sink"),
      G_CALLBACK (events_cb), state);

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
        gst_element_set_state (state->pipeline, GST_STATE_PLAYING);
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
      case 'S':
        if (state->sync_animation_with_video) {
          state->sync_animation_with_video = FALSE;
          /* Add the rendering task */
          g_idle_add_full (G_PRIORITY_HIGH_IDLE,
              (GSourceFunc) handle_msgs_and_render_scene, state, NULL);
          g_print ("\nanimation is not synchoronized with video\n");
        } else {
          state->sync_animation_with_video = TRUE;
          g_print ("\nanimation is synchoronized with video\n");
        }
        break;
    }
  }
  g_free (str);
  return TRUE;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, GstPipeline * data)
{
  if ((GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT) &&
      gst_structure_has_name (message->structure, "need-egl-pool")) {
    queue_object (state, GST_MINI_OBJECT_CAST (gst_message_ref (message)),
        TRUE);
  }
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
    gst_element_set_state (state->pipeline, GST_STATE_PAUSED);
  else {
    g_print ("\n");
    gst_element_set_state (state->pipeline, GST_STATE_PLAYING);
  }
}

/* on EOS just quit the application */
static void
eos_cb (GstBus * bus, GstMessage * msg, APP_STATE_T * state)
{
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (state->pipeline)) {
    g_print ("End-Of-Stream reached.\n");
    flush_start (state);
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

  /* Clear application state */
  memset (state, 0, sizeof (*state));
  state->animate = TRUE;
  state->sync_animation_with_video = TRUE;

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

  TRACE_VC_MEMORY ("state 0");

#if defined (USE_OMX_TARGET_RPI) && defined (HAVE_GST_EGL)
  bcm_host_init ();
  TRACE_VC_MEMORY ("after bcm_host_init");
#endif


  /* Start OpenGLES */
  init_ogl (state);
  TRACE_VC_MEMORY ("after init_ogl");

  /* Wrap the EGL display */
  state->gst_display = gst_egl_display_new (state->display);

  /* Setup the model world */
  init_model_proj (state);
  TRACE_VC_MEMORY ("after init_model_proj");

  /* initialize the OGLES texture(s) */
  init_textures (state);
  TRACE_VC_MEMORY ("after init_textures");

  /* initialize inter thread comunnication */
  init_intercom (state);

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
      "  S - Toggle synchronization of video and animation \n"
      "  q - Quit \n");
  /* *INDENT-ON* */

  if (!state->sync_animation_with_video) {
    /* Add the rendering task */
    g_idle_add_full (G_PRIORITY_HIGH_IDLE,
        (GSourceFunc) handle_msgs_and_render_scene, state, NULL);
  }

  /* Connect the bus handlers */
  bus = gst_element_get_bus (state->pipeline);

  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, state);

  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);
  gst_bus_enable_sync_message_emission (bus);

  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      state);
  g_signal_connect (G_OBJECT (bus), "message::buffering",
      (GCallback) buffering_cb, state);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, state);
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
    gst_object_unref (state->pipeline);
  }

  /* Release intercom */
  if (state->queue) {
    g_async_queue_unref (state->queue);
  }

  if (state->lock) {
    g_mutex_free (state->lock);
  }

  if (state->cond) {
    g_cond_free (state->cond);
  }

  /* Unref the mainloop */
  if (state->main_loop) {
    g_main_loop_unref (state->main_loop);
  }

  close_ogl ();

  TRACE_VC_MEMORY ("at exit");
  return 0;
}
