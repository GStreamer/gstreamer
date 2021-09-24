/*
Copyright (c) 2012, Broadcom Europe Ltd
Copyright (c) 2012, OtherCrashOverride
Copyright (C) 2013, Fluendo S.A.
   @author: Josep Torra <josep@fluendo.com>
Copyright (C) 2013, Video Experts Group LLC.
   @author: Ilya Smelykh <ilya@videoexpertsgroup.com>
Copyright (C) 2014 Julien Isorce <julien.isorce@collabora.co.uk>
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

#if defined (USE_OMX_TARGET_RPI) && defined (__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <gst/gst.h>

#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#include <gst/gl/egl/gstgldisplay_egl.h>

#if defined (USE_OMX_TARGET_RPI)
#include <bcm_host.h>
#include <EGL/eglext.h>
#elif defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#if defined (USE_OMX_TARGET_RPI) && defined (__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

#include "cube_texture_and_coords.h"

#ifndef M_PI
#define M_PI 3.141592654
#endif

#define SYNC_BUFFERS TRUE

#define TRACE_VC_MEMORY_ENABLED 0

#if defined (USE_OMX_TARGET_RPI) && TRACE_VC_MEMORY_ENABLED
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

/* some helpers that we should provide in libgstgl */

typedef struct
{
  GLfloat m[4][4];
} GstGLMatrix;

static void
gst_gl_matrix_load_identity (GstGLMatrix * matrix)
{
  memset (matrix, 0x0, sizeof (GstGLMatrix));
  matrix->m[0][0] = 1.0f;
  matrix->m[1][1] = 1.0f;
  matrix->m[2][2] = 1.0f;
  matrix->m[3][3] = 1.0f;
}

static void
gst_gl_matrix_multiply (GstGLMatrix * matrix, GstGLMatrix * srcA,
    GstGLMatrix * srcB)
{
  GstGLMatrix tmp;
  int i;

  for (i = 0; i < 4; i++) {
    tmp.m[i][0] = (srcA->m[i][0] * srcB->m[0][0]) +
        (srcA->m[i][1] * srcB->m[1][0]) +
        (srcA->m[i][2] * srcB->m[2][0]) + (srcA->m[i][3] * srcB->m[3][0]);

    tmp.m[i][1] = (srcA->m[i][0] * srcB->m[0][1]) +
        (srcA->m[i][1] * srcB->m[1][1]) +
        (srcA->m[i][2] * srcB->m[2][1]) + (srcA->m[i][3] * srcB->m[3][1]);

    tmp.m[i][2] = (srcA->m[i][0] * srcB->m[0][2]) +
        (srcA->m[i][1] * srcB->m[1][2]) +
        (srcA->m[i][2] * srcB->m[2][2]) + (srcA->m[i][3] * srcB->m[3][2]);

    tmp.m[i][3] = (srcA->m[i][0] * srcB->m[0][3]) +
        (srcA->m[i][1] * srcB->m[1][3]) +
        (srcA->m[i][2] * srcB->m[2][3]) + (srcA->m[i][3] * srcB->m[3][3]);
  }

  memcpy (matrix, &tmp, sizeof (GstGLMatrix));
}

static void
gst_gl_matrix_translate (GstGLMatrix * matrix, GLfloat tx, GLfloat ty,
    GLfloat tz)
{
  matrix->m[3][0] +=
      (matrix->m[0][0] * tx + matrix->m[1][0] * ty + matrix->m[2][0] * tz);
  matrix->m[3][1] +=
      (matrix->m[0][1] * tx + matrix->m[1][1] * ty + matrix->m[2][1] * tz);
  matrix->m[3][2] +=
      (matrix->m[0][2] * tx + matrix->m[1][2] * ty + matrix->m[2][2] * tz);
  matrix->m[3][3] +=
      (matrix->m[0][3] * tx + matrix->m[1][3] * ty + matrix->m[2][3] * tz);
}

static void
gst_gl_matrix_frustum (GstGLMatrix * matrix, GLfloat left, GLfloat right,
    GLfloat bottom, GLfloat top, GLfloat nearZ, GLfloat farZ)
{
  GLfloat deltaX = right - left;
  GLfloat deltaY = top - bottom;
  GLfloat deltaZ = farZ - nearZ;
  GstGLMatrix frust;

  if ((nearZ <= 0.0f) || (farZ <= 0.0f) ||
      (deltaX <= 0.0f) || (deltaY <= 0.0f) || (deltaZ <= 0.0f))
    return;

  frust.m[0][0] = 2.0f * nearZ / deltaX;
  frust.m[0][1] = frust.m[0][2] = frust.m[0][3] = 0.0f;

  frust.m[1][1] = 2.0f * nearZ / deltaY;
  frust.m[1][0] = frust.m[1][2] = frust.m[1][3] = 0.0f;

  frust.m[2][0] = (right + left) / deltaX;
  frust.m[2][1] = (top + bottom) / deltaY;
  frust.m[2][2] = -(nearZ + farZ) / deltaZ;
  frust.m[2][3] = -1.0f;

  frust.m[3][2] = -2.0f * nearZ * farZ / deltaZ;
  frust.m[3][0] = frust.m[3][1] = frust.m[3][3] = 0.0f;

  gst_gl_matrix_multiply (matrix, &frust, matrix);
}

static void
gst_gl_matrix_perspective (GstGLMatrix * matrix, GLfloat fovy, GLfloat aspect,
    GLfloat nearZ, GLfloat farZ)
{
  GLfloat frustumW, frustumH;

  frustumH = tanf (fovy / 360.0f * M_PI) * nearZ;
  frustumW = frustumH * aspect;

  gst_gl_matrix_frustum (matrix, -frustumW, frustumW, -frustumH, frustumH,
      nearZ, farZ);
}

/* *INDENT-OFF* */

/* vertex source */
static const gchar *cube_v_src =
    "attribute vec4 a_position;                          \n"
    "attribute vec2 a_texCoord;                          \n"
    "uniform float u_rotx;                               \n"
    "uniform float u_roty;                               \n"
    "uniform float u_rotz;                               \n"
    "uniform mat4 u_modelview;                           \n"
    "uniform mat4 u_projection;                          \n"
    "varying vec2 v_texCoord;                            \n"
    "void main()                                         \n"
    "{                                                   \n"
    "   float PI = 3.14159265;                           \n"
    "   float xrot = u_rotx*2.0*PI/360.0;                \n"
    "   float yrot = u_roty*2.0*PI/360.0;                \n"
    "   float zrot = u_rotz*2.0*PI/360.0;                \n"
    "   mat4 matX = mat4 (                               \n"
    "            1.0,        0.0,        0.0, 0.0,       \n"
    "            0.0,  cos(xrot),  sin(xrot), 0.0,       \n"
    "            0.0, -sin(xrot),  cos(xrot), 0.0,       \n"
    "            0.0,        0.0,        0.0, 1.0 );     \n"
    "   mat4 matY = mat4 (                               \n"
    "      cos(yrot),        0.0, -sin(yrot), 0.0,       \n"
    "            0.0,        1.0,        0.0, 0.0,       \n"
    "      sin(yrot),        0.0,  cos(yrot), 0.0,       \n"
    "            0.0,        0.0,       0.0,  1.0 );     \n"
    "   mat4 matZ = mat4 (                               \n"
    "      cos(zrot),  sin(zrot),        0.0, 0.0,       \n"
    "     -sin(zrot),  cos(zrot),        0.0, 0.0,       \n"
    "            0.0,        0.0,        1.0, 0.0,       \n"
    "            0.0,        0.0,        0.0, 1.0 );     \n"
    "   gl_Position = u_projection * u_modelview * matZ * matY * matX * a_position;\n"
    "   v_texCoord = a_texCoord;                         \n"
    "}                                                   \n";

/* fragment source */
static const gchar *cube_f_src =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D s_texture;                        \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = texture2D (s_texture, v_texCoord); \n"
    "}                                                   \n";
/* *INDENT-ON* */

typedef struct
{
#if defined (USE_OMX_TARGET_RPI)
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_ELEMENT_HANDLE_T dispman_element;
#endif

  uint32_t screen_width;
  uint32_t screen_height;
  gboolean animate;

  GstCaps *caps;

  /* OpenGL|ES objects */
  EGLDisplay display;
  EGLSurface surface;
  EGLContext context;
  GLuint tex;

  GLint vshader;
  GLint fshader;
  GLint program;

  GLint u_modelviewmatrix;
  GLint u_projectionmatrix;
  GLint s_texture;
  GLint u_rotx;
  GLint u_roty;
  GLint u_rotz;

  GstGLMatrix modelview;
  GstGLMatrix projection;
  GLfloat fov;
  GLfloat aspect;

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
  GstGLDisplayEGL *gst_display;
  GstGLContext *gl_context;
  gboolean can_avoid_upload;

  /* Interthread comunication */
  GAsyncQueue *queue;
  GMutex queue_lock;
  GCond cond;
  gboolean flushing;
  GstMiniObject *popped_obj;
  GstBuffer *current_buffer;

  /* GLib mainloop */
  GMainLoop *main_loop;
  GstBuffer *last_buffer;

  /* Rendering thread state */
  gboolean running;

  /* number of rendered and dropped frames */
  guint64 rendered;
  guint64 dropped;

#if !defined (USE_OMX_TARGET_RPI) && defined(HAVE_X11)
  Display *xdisplay;
  Window xwindow;
#endif
} APP_STATE_T;

static void init_ogl (APP_STATE_T * state);
static void init_model_proj (APP_STATE_T * state);
static void reset_model (APP_STATE_T * state);
static GLfloat inc_and_wrap_angle (GLfloat angle, GLfloat angle_inc);
static void redraw_scene (APP_STATE_T * state);
static void update_model (APP_STATE_T * state);
static void init_textures (APP_STATE_T * state, GstBuffer * buffer);
static APP_STATE_T _state, *state = &_state;
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
#if defined (USE_OMX_TARGET_RPI)
  int32_t success = 0;
#else
  gint screen_num = 0;
  gulong black_pixel = 0;
#endif
  EGLBoolean result;
  EGLint num_config;
  EGLNativeWindowType window_handle = (EGLNativeWindowType) 0;

#if defined (USE_OMX_TARGET_RPI)
  static EGL_DISPMANX_WINDOW_T nativewindow;

  DISPMANX_UPDATE_HANDLE_T dispman_update;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;

  VC_DISPMANX_ALPHA_T alpha = { DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS, 255, 0 };
#endif

  static const EGLint attribute_list[] = {
    EGL_DEPTH_SIZE, 16,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  EGLConfig config;

  /* get an EGL display connection */
  state->display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  assert (state->display != EGL_NO_DISPLAY);

  /* initialize the EGL display connection */
  result = eglInitialize (state->display, NULL, NULL);
  assert (EGL_FALSE != result);

#if defined (USE_OMX_TARGET_RPI)
  /* get an appropriate EGL frame buffer configuration
   * this uses a BRCM extension that gets the closest match, rather
   * than standard which returns anything that matches. */
  result =
      eglSaneChooseConfigBRCM (state->display, attribute_list, &config, 1,
      &num_config);
  assert (EGL_FALSE != result);
#else
  result =
      eglChooseConfig (state->display, attribute_list, &config, 1, &num_config);
#endif

  /* create an EGL rendering context */
  state->context =
      eglCreateContext (state->display, config, EGL_NO_CONTEXT,
      context_attributes);
  assert (state->context != EGL_NO_CONTEXT);

#if defined (USE_OMX_TARGET_RPI)
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
      &src_rect, DISPMANX_PROTECTION_NONE, &alpha, 0 /*clamp */ ,
      0 /*transform */ );

  nativewindow.element = state->dispman_element;
  nativewindow.width = state->screen_width;
  nativewindow.height = state->screen_height;
  vc_dispmanx_update_submit_sync (dispman_update);

  window_handle = &nativewindow;
#elif defined(HAVE_X11)
  state->screen_width = 1280;
  state->screen_height = 720;
  state->xdisplay = XOpenDisplay (NULL);
  screen_num = DefaultScreen (state->xdisplay);
  black_pixel = XBlackPixel (state->xdisplay, screen_num);
  state->xwindow = XCreateSimpleWindow (state->xdisplay,
      DefaultRootWindow (state->xdisplay), 0, 0, state->screen_width,
      state->screen_height, 0, 0, black_pixel);
  XSetWindowBackgroundPixmap (state->xdisplay, state->xwindow, None);
  XMapRaised (state->xdisplay, state->xwindow);
  XSync (state->xdisplay, FALSE);
  window_handle = state->xwindow;
#endif

  state->surface =
      eglCreateWindowSurface (state->display, config, window_handle, NULL);
  assert (state->surface != EGL_NO_SURFACE);

  /* connect the context to the surface */
  result =
      eglMakeCurrent (state->display, state->surface, state->surface,
      state->context);
  assert (EGL_FALSE != result);

  state->gst_display = gst_gl_display_egl_new_with_egl_display (state->display);
  state->gl_context =
      gst_gl_context_new_wrapped (GST_GL_DISPLAY (state->gst_display),
      (guintptr) state->context, GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);
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
  GLint ret = 0;

  state->vshader = glCreateShader (GL_VERTEX_SHADER);

  glShaderSource (state->vshader, 1, &cube_v_src, NULL);
  glCompileShader (state->vshader);
  assert (glGetError () == GL_NO_ERROR);

  state->fshader = glCreateShader (GL_FRAGMENT_SHADER);

  glShaderSource (state->fshader, 1, &cube_f_src, NULL);
  glCompileShader (state->fshader);
  assert (glGetError () == GL_NO_ERROR);

  state->program = glCreateProgram ();

  glAttachShader (state->program, state->vshader);
  glAttachShader (state->program, state->fshader);

  glBindAttribLocation (state->program, 0, "a_position");
  glBindAttribLocation (state->program, 1, "a_texCoord");

  glLinkProgram (state->program);

  glGetProgramiv (state->program, GL_LINK_STATUS, &ret);
  assert (ret == GL_TRUE);

  glUseProgram (state->program);

  state->u_rotx = glGetUniformLocation (state->program, "u_rotx");
  state->u_roty = glGetUniformLocation (state->program, "u_roty");
  state->u_rotz = glGetUniformLocation (state->program, "u_rotz");

  state->u_modelviewmatrix =
      glGetUniformLocation (state->program, "u_modelview");

  state->u_projectionmatrix =
      glGetUniformLocation (state->program, "u_projection");

  state->s_texture = glGetUniformLocation (state->program, "s_texture");

  glViewport (0, 0, (GLsizei) state->screen_width,
      (GLsizei) state->screen_height);

  state->fov = 45.0f;
  state->distance = 5.0f;
  state->aspect =
      (GLfloat) state->screen_width / (GLfloat) state->screen_height;

  gst_gl_matrix_load_identity (&state->projection);
  gst_gl_matrix_perspective (&state->projection, state->fov, state->aspect,
      1.0f, 100.0f);

  gst_gl_matrix_load_identity (&state->modelview);
  gst_gl_matrix_translate (&state->modelview, 0.0f, 0.0f, -state->distance);

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
  /* reset model rotation */
  state->rot_angle_x = 45.f;
  state->rot_angle_y = 30.f;
  state->rot_angle_z = 0.f;
  state->rot_angle_x_inc = 0.5f;
  state->rot_angle_y_inc = 0.5f;
  state->rot_angle_z_inc = 0.f;
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
  }
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
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  glEnable (GL_CULL_FACE);
  glEnable (GL_DEPTH_TEST);

  /* Set background color and clear buffers */
  glClearColor (0.15f, 0.25f, 0.35f, 1.0f);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram (state->program);

  glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 0, quadx);
  glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texCoords);

  glEnableVertexAttribArray (0);
  glEnableVertexAttribArray (1);

  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, state->tex);
  glUniform1i (state->s_texture, 0);

  glUniform1f (state->u_rotx, state->rot_angle_x);
  glUniform1f (state->u_roty, state->rot_angle_y);
  glUniform1f (state->u_rotz, state->rot_angle_z);

  glUniformMatrix4fv (state->u_modelviewmatrix, 1, GL_FALSE,
      &state->modelview.m[0][0]);

  glUniformMatrix4fv (state->u_projectionmatrix, 1, GL_FALSE,
      &state->projection.m[0][0]);

  /* draw first 4 vertices */
  glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 4, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 8, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 12, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 16, 4);
  glDrawArrays (GL_TRIANGLE_STRIP, 20, 4);

  if (!eglSwapBuffers (state->display, state->surface)) {
    g_main_loop_quit (state->main_loop);
    return;
  }

  glDisable (GL_DEPTH_TEST);
  glDisable (GL_CULL_FACE);
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
init_textures (APP_STATE_T * state, GstBuffer * buffer)
{
  GstCapsFeatures *feature = gst_caps_get_features (state->caps, 0);

  if (gst_caps_features_contains (feature, "memory:GLMemory")) {
    g_print ("Prepare texture for GLMemory\n");
    state->can_avoid_upload = TRUE;
    state->tex = 0;
  } else if (gst_caps_features_contains (feature,
          "meta:GstVideoGLTextureUploadMeta")) {
    GstVideoMeta *meta = NULL;
    guint internal_format =
        gst_gl_sized_gl_format_from_gl_format_type (state->gl_context,
        GL_RGBA, GL_UNSIGNED_BYTE);

    g_print ("Prepare texture for GstVideoGLTextureUploadMeta\n");
    meta = gst_buffer_get_video_meta (buffer);
    state->can_avoid_upload = FALSE;
    glGenTextures (1, &state->tex);
    glBindTexture (GL_TEXTURE_2D, state->tex);
    glTexImage2D (GL_TEXTURE_2D, 0, internal_format, meta->width, meta->height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  } else {
    g_assert_not_reached ();
  }

#if 0
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#else
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  assert (glGetError () == GL_NO_ERROR);
}

static void
render_scene (APP_STATE_T * state)
{
  update_model (state);
  redraw_scene (state);
  TRACE_VC_MEMORY_ONCE_FOR_ID ("after render_scene", gid2);
}

static void
update_image (APP_STATE_T * state, GstBuffer * buffer)
{
  GstVideoGLTextureUploadMeta *meta = NULL;

  if (state->current_buffer) {
    gst_buffer_unref (state->current_buffer);
  } else {
    /* Setup the model world */
    init_model_proj (state);
    TRACE_VC_MEMORY ("after init_model_proj");

    /* initialize the OGLES texture(s) */
    init_textures (state, buffer);
    TRACE_VC_MEMORY ("after init_textures");
  }
  state->current_buffer = gst_buffer_ref (buffer);

  TRACE_VC_MEMORY_ONCE_FOR_ID ("before GstVideoGLTextureUploadMeta", gid0);

  if (state->can_avoid_upload) {
    GstMemory *mem = gst_buffer_peek_memory (state->current_buffer, 0);
    g_assert (gst_is_gl_memory (mem));
    state->tex = ((GstGLMemory *) mem)->tex_id;
  } else if ((meta = gst_buffer_get_video_gl_texture_upload_meta (buffer))) {
    if (meta->n_textures == 1) {
      guint ids[4] = { state->tex, 0, 0, 0 };
      if (!gst_video_gl_texture_upload_meta_upload (meta, ids)) {
        GST_WARNING ("failed to upload to texture");
      }
    }
  }

  TRACE_VC_MEMORY_ONCE_FOR_ID ("after GstVideoGLTextureUploadMeta", gid1);
}

static void
init_intercom (APP_STATE_T * state)
{
  state->queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
  g_mutex_init (&state->queue_lock);
  g_cond_init (&state->cond);
}

static void
terminate_intercom (APP_STATE_T * state)
{
  /* Release intercom */
  if (state->queue) {
    g_async_queue_unref (state->queue);
  }

  g_mutex_clear (&state->queue_lock);
  g_cond_clear (&state->cond);
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

  g_mutex_lock (&state->queue_lock);
  state->flushing = TRUE;
  g_cond_broadcast (&state->cond);
  g_mutex_unlock (&state->queue_lock);

  while ((object = g_async_queue_try_pop (state->queue))) {
    gst_mini_object_unref (object);
  }
  g_mutex_lock (&state->queue_lock);
  flush_internal (state);
  state->popped_obj = NULL;
  g_mutex_unlock (&state->queue_lock);
}

static void
flush_stop (APP_STATE_T * state)
{
  GstMiniObject *object = NULL;

  g_mutex_lock (&state->queue_lock);
  while ((object = GST_MINI_OBJECT_CAST (g_async_queue_try_pop (state->queue)))) {
    gst_mini_object_unref (object);
  }
  flush_internal (state);
  state->popped_obj = NULL;
  state->flushing = FALSE;
  g_mutex_unlock (&state->queue_lock);
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

  g_mutex_lock (&state->queue_lock);
  if (state->flushing) {
    g_cond_broadcast (&state->cond);
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
    g_cond_broadcast (&state->cond);
  }

beach:
  g_mutex_unlock (&state->queue_lock);

  return FALSE;
}

static gboolean
queue_object (APP_STATE_T * state, GstMiniObject * obj, gboolean synchronous)
{
  gboolean res = TRUE;

  g_mutex_lock (&state->queue_lock);
  if (state->flushing) {
    gst_mini_object_unref (obj);
    res = FALSE;
    goto beach;
  }

  g_async_queue_push (state->queue, obj);

  if (synchronous) {
    /* Waiting for object to be handled */
    do {
      g_cond_wait (&state->cond, &state->queue_lock);
    } while (!state->flushing && state->popped_obj != obj);
  }

beach:
  g_mutex_unlock (&state->queue_lock);
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
    case GST_EVENT_CAPS:
    {
      if (state->caps) {
        gst_caps_unref (state->caps);
        state->caps = NULL;
      }
      gst_event_parse_caps (event, &state->caps);
      if (state->caps)
        gst_caps_ref (state->caps);
      break;
    }
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
    case GST_QUERY_CONTEXT:
    {
      if (gst_gl_handle_context_query (state->pipeline, query,
              (GstGLDisplay *) state->gst_display, NULL,
              (GstGLContext *) state->gl_context))
        return GST_PAD_PROBE_HANDLED;
      break;
    }
    case GST_QUERY_DRAIN:
    {
      flush_internal (state);
      break;
    }
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
init_playbin_player (APP_STATE_T * state, const gchar * uri)
{
  GstPad *pad = NULL;
  GstPad *ghostpad = NULL;
  GstElement *vbin = gst_bin_new ("vbin");

  /* insert a gl filter so that the GstGLBufferPool
   * is managed automatically */
  GstElement *glfilter = gst_element_factory_make ("glupload", "glfilter");
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
  GstElement *vsink = gst_element_factory_make ("fakesink", "vsink");

  g_object_set (capsfilter, "caps",
      gst_caps_from_string ("video/x-raw(memory:GLMemory), format=RGBA"), NULL);
  g_object_set (vsink, "sync", TRUE, "silent", TRUE, "qos", TRUE,
      "enable-last-sample", FALSE, "max-lateness", 20 * GST_MSECOND,
      "signal-handoffs", TRUE, NULL);

  g_signal_connect (vsink, "preroll-handoff", G_CALLBACK (preroll_cb), state);
  g_signal_connect (vsink, "handoff", G_CALLBACK (buffers_cb), state);

  gst_bin_add_many (GST_BIN (vbin), glfilter, capsfilter, vsink, NULL);

  pad = gst_element_get_static_pad (glfilter, "sink");
  ghostpad = gst_ghost_pad_new ("sink", pad);
  gst_object_unref (pad);
  gst_element_add_pad (vbin, ghostpad);

  pad = gst_element_get_static_pad (vsink, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, events_cb, state,
      NULL);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, query_cb, state,
      NULL);
  gst_object_unref (pad);

  gst_element_link (glfilter, capsfilter);
  gst_element_link (capsfilter, vsink);

  /* Instantiate and configure playbin */
  state->pipeline = gst_element_factory_make ("playbin", "player");
  g_object_set (state->pipeline, "uri", uri,
      "video-sink", vbin, "flags",
      GST_PLAY_FLAG_NATIVE_VIDEO | GST_PLAY_FLAG_AUDIO, NULL);

  state->vsink = gst_object_ref (vsink);
  return TRUE;
}

static gboolean
init_parse_launch_player (APP_STATE_T * state, const gchar * spipeline)
{
  GstElement *vsink;
  GError *error = NULL;

  /* ex:

     ./testegl "filesrc location=big_buck_bunny_720p_h264.mov ! qtdemux ! \
     h264parse !  omxh264dec ! glcolorscale ! fakesink name=vsink"

     ./testegl "filesrc location=big_buck_bunny_720p_h264.mov ! qtdemux ! \
     h264parse ! omxh264dec ! glcolorscale ! \
     video/x-raw(memory:GLMemory) ! fakesink name=vsink"

     ./testegl "filesrc location=big_buck_bunny_720p_h264.mov ! qtdemux ! \
     h264parse ! omxh264dec ! glcolorscale ! \
     video/x-raw(meta:GstVideoGLTextureUploadMeta) ! \
     fakesink name=vsink"

   */

  /* pipeline 1 and 2 are the same and the most efficient as glcolorscale
   * will enter in passthrough mode and testegl will just bind the eglimage
   * to a gl texture without any copy. */

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
#if defined (USE_OMX_TARGET_RPI)
  DISPMANX_UPDATE_HANDLE_T dispman_update;
#endif

  if (state->fshader) {
    glDeleteShader (state->fshader);
    glDetachShader (state->program, state->fshader);
  }

  if (state->vshader) {
    glDeleteShader (state->vshader);
    glDetachShader (state->program, state->vshader);
  }

  if (state->program)
    glDeleteProgram (state->program);

  if (state->tex)
    glDeleteTextures (1, &state->tex);

  /* clear screen */
  glClear (GL_COLOR_BUFFER_BIT);
  eglSwapBuffers (state->display, state->surface);

  /* Release OpenGL resources */
  eglMakeCurrent (state->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
      EGL_NO_CONTEXT);
  eglDestroySurface (state->display, state->surface);
  eglDestroyContext (state->display, state->context);
  gst_object_unref (state->gl_context);
  gst_object_unref (state->gst_display);

#if defined (USE_OMX_TARGET_RPI)
  dispman_update = vc_dispmanx_update_start (0);
  vc_dispmanx_element_remove (dispman_update, state->dispman_element);
  vc_dispmanx_update_submit_sync (dispman_update);
  vc_dispmanx_display_close (state->dispman_display);
#elif defined(HAVE_X11)
  XSync (state->xdisplay, FALSE);
  XUnmapWindow (state->xdisplay, state->xwindow);
  XDestroyWindow (state->xdisplay, state->xwindow);
  XSync (state->xdisplay, FALSE);
  XCloseDisplay (state->xdisplay);
#endif
}

//==============================================================================

static void
open_ogl (void)
{
  TRACE_VC_MEMORY ("state 0");

#if defined (USE_OMX_TARGET_RPI)
  bcm_host_init ();
  TRACE_VC_MEMORY ("after bcm_host_init");
#endif

  /* Create surface and gl context */
  init_ogl (state);
  TRACE_VC_MEMORY ("after init_ogl");
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
  state->caps = NULL;

  ctx = g_option_context_new ("[ADDITIONAL ARGUMENTS]");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_option_context_free (ctx);
    g_clear_error (&err);
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

  /* Create a GLib Main Loop */
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

  if (state->caps) {
    gst_caps_unref (state->caps);
    state->caps = NULL;
  }

  terminate_intercom (state);

  TRACE_VC_MEMORY ("at exit");
  return 0;
}
