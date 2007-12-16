/* glvideo.c
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "glvideo.h"
#include "glextensions.h"
#include <gst/gst.h>

#include <string.h>


static gboolean gst_gl_display_check_features (GstGLDisplay * display);


GstGLDisplay *
gst_gl_display_new (const char *display_name)
{
  GstGLDisplay *display;
  gboolean usable;

  display = g_malloc0 (sizeof (GstGLDisplay));

  display->display = XOpenDisplay (display_name);
  if (display->display == NULL) {
    g_free (display);
    return NULL;
  }

  usable = gst_gl_display_check_features (display);
  if (!usable) {
    g_free (display);
    return NULL;
  }

  display->lock = g_mutex_new ();

  return display;
}

static gboolean
gst_gl_display_check_features (GstGLDisplay * display)
{
  gboolean ret;
  XVisualInfo *visinfo;
  Screen *screen;
  Window root;
  int scrnum;
  int attrib[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None
  };
  XSetWindowAttributes attr;
  int error_base;
  int event_base;
  int mask;
  const char *extstring;
  Window window;

  screen = XDefaultScreenOfDisplay (display->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (display->display, scrnum);

  ret = glXQueryExtension (display->display, &error_base, &event_base);
  if (!ret) {
    GST_DEBUG ("No GLX extension");
    return FALSE;
  }

  visinfo = glXChooseVisual (display->display, scrnum, attrib);
  if (visinfo == NULL) {
    GST_DEBUG ("No usable visual");
    return FALSE;
  }

  display->visinfo = visinfo;

  display->context = glXCreateContext (display->display, visinfo, NULL, True);

  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (display->display, root,
      visinfo->visual, AllocNone);
  attr.event_mask = StructureNotifyMask | ExposureMask;
  attr.override_redirect = True;

  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  window = XCreateWindow (display->display, root, 0, 0,
      100, 100, 0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr);

  XSync (display->display, FALSE);

  glXMakeCurrent (display->display, window, display->context);

  glGetIntegerv (GL_MAX_TEXTURE_SIZE, &display->max_texture_size);

  extstring = (const char *) glGetString (GL_EXTENSIONS);

  display->have_ycbcr_texture = FALSE;
#ifdef GL_YCBCR_MESA
  if (strstr (extstring, "GL_MESA_ycbcr_texture")) {
    display->have_ycbcr_texture = TRUE;
  }
#endif

  display->have_color_matrix = FALSE;
#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
  if (strstr (extstring, "GL_SGI_color_matrix")) {
    display->have_color_matrix = TRUE;
  }
#endif

  display->have_texture_rectangle = FALSE;
#ifdef GL_TEXTURE_RECTANGLE_ARB
  if (strstr (extstring, "GL_ARB_texture_rectangle")) {
    display->have_texture_rectangle = TRUE;
  }
#endif

  glXMakeCurrent (display->display, None, NULL);
  XDestroyWindow (display->display, window);

  return TRUE;
}

gboolean
gst_gl_display_can_handle_type (GstGLDisplay * display, GstGLImageType type)
{
  switch (type) {
    case GST_GL_IMAGE_TYPE_RGBx:
    case GST_GL_IMAGE_TYPE_BGRx:
    case GST_GL_IMAGE_TYPE_xRGB:
    case GST_GL_IMAGE_TYPE_xBGR:
      return TRUE;
    case GST_GL_IMAGE_TYPE_YUY2:
    case GST_GL_IMAGE_TYPE_UYVY:
      return display->have_ycbcr_texture;
    case GST_GL_IMAGE_TYPE_AYUV:
      return display->have_color_matrix;
    default:
      return FALSE;
  }
}

void
gst_gl_display_free (GstGLDisplay * display)
{
  /* sure hope nobody is using it as it's being freed */
  g_mutex_lock (display->lock);
  g_mutex_unlock (display->lock);

  if (display->context) {
    glXDestroyContext (display->display, display->context);
  }
  if (display->visinfo) {
    XFree (display->visinfo);
  }
  if (display->display) {
    XCloseDisplay (display->display);
  }

  g_mutex_free (display->lock);

  g_free (display);
}


/* drawable */

GstGLDrawable *
gst_gl_drawable_new_window (GstGLDisplay * display)
{
  GstGLDrawable *drawable;
  XSetWindowAttributes attr = { 0 };
  int scrnum;
  int mask;
  Window root;
  Screen *screen;

  drawable = g_malloc0 (sizeof (GstGLDrawable));

  g_mutex_lock (display->lock);
  drawable->display = display;

  screen = XDefaultScreenOfDisplay (display->display);
  scrnum = XScreenNumberOfScreen (screen);
  root = XRootWindow (display->display, scrnum);

  attr.background_pixel = 0;
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap (display->display, root,
      display->visinfo->visual, AllocNone);
  attr.override_redirect = False;
#if 0
  if (display->parent_window) {
    attr.override_redirect = True;
  }
#endif

  mask = CWBackPixel | CWBorderPixel | CWColormap | CWOverrideRedirect;

  drawable->window = XCreateWindow (display->display,
      root, 0, 0, 100, 100,
      0, display->visinfo->depth, InputOutput,
      display->visinfo->visual, mask, &attr);
  XMapWindow (display->display, drawable->window);
  drawable->destroy_on_free = TRUE;

  g_mutex_unlock (display->lock);

  return drawable;
}

GstGLDrawable *
gst_gl_drawable_new_root_window (GstGLDisplay * display)
{
  GstGLDrawable *drawable;
  int scrnum;
  Screen *screen;

  drawable = g_malloc0 (sizeof (GstGLDrawable));

  g_mutex_lock (display->lock);
  drawable->display = display;

  screen = XDefaultScreenOfDisplay (display->display);
  scrnum = XScreenNumberOfScreen (screen);

  drawable->window = XRootWindow (display->display, scrnum);
  drawable->destroy_on_free = FALSE;
  g_mutex_unlock (display->lock);

  return drawable;
}

GstGLDrawable *
gst_gl_drawable_new_from_window (GstGLDisplay * display, Window window)
{
  GstGLDrawable *drawable;

  drawable = g_malloc0 (sizeof (GstGLDrawable));

  g_mutex_lock (display->lock);
  drawable->display = display;

  drawable->window = window;
  drawable->destroy_on_free = FALSE;

  g_mutex_unlock (display->lock);
  return drawable;
}

void
gst_gl_drawable_free (GstGLDrawable * drawable)
{

  g_mutex_lock (drawable->display->lock);
  if (drawable->destroy_on_free) {
    XDestroyWindow (drawable->display->display, drawable->window);
  }
  g_mutex_unlock (drawable->display->lock);

  g_free (drawable);
}

void
gst_gl_drawable_lock (GstGLDrawable * drawable)
{
  g_mutex_lock (drawable->display->lock);
  glXMakeCurrent (drawable->display->display, drawable->window,
      drawable->display->context);
}

void
gst_gl_drawable_unlock (GstGLDrawable * drawable)
{
  glXMakeCurrent (drawable->display->display, None, NULL);
  g_mutex_unlock (drawable->display->lock);
}

void
gst_gl_drawable_update_attributes (GstGLDrawable * drawable)
{
  XWindowAttributes attr;

  XGetWindowAttributes (drawable->display->display, drawable->window, &attr);
  drawable->win_width = attr.width;
  drawable->win_height = attr.height;

}

void
gst_gl_drawable_clear (GstGLDrawable * drawable)
{

  gst_gl_drawable_lock (drawable);

  glDepthFunc (GL_LESS);
  glEnable (GL_DEPTH_TEST);
  glClearColor (0.2, 0.2, 0.2, 1.0);
  glViewport (0, 0, drawable->win_width, drawable->win_height);

  gst_gl_drawable_unlock (drawable);
}



static void
draw_rect_texture (GstGLDrawable * drawable, GstGLImageType type,
    void *data, int width, int height)
{
  GLuint texture;

  GST_DEBUG ("using rectangular texture");

#ifdef GL_TEXTURE_RECTANGLE_ARB
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glGenTextures (1, &texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  switch (type) {
    case GST_GL_IMAGE_TYPE_RGBx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_GL_IMAGE_TYPE_BGRx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_GL_IMAGE_TYPE_xRGB:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_GL_IMAGE_TYPE_xBGR:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_GL_IMAGE_TYPE_YUY2:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_GL_IMAGE_TYPE_UYVY:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_GL_IMAGE_TYPE_AYUV:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    default:
      g_assert_not_reached ();
  }

#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
  if (type == GST_GL_IMAGE_TYPE_AYUV) {
    const double matrix[16] = {
      1, 1, 1, 0,
      0, -0.344 * 1, 1.770 * 1, 0,
      1.403 * 1, -0.714 * 1, 0, 0,
      0, 0, 0, 1
    };
    glMatrixMode (GL_COLOR);
    glLoadMatrixd (matrix);
    glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, -1.403 / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, (0.344 + 0.714) / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, -1.770 / 2);
  }
#endif

  glColor4f (1, 0, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  glTexCoord2f (width, 0);
  glVertex3f (1.0, 1.0, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-1.0, 1.0, 0);
  glTexCoord2f (0, height);
  glVertex3f (-1.0, -1.0, 0);
  glTexCoord2f (width, height);
  glVertex3f (1.0, -1.0, 0);
  glEnd ();
  glDeleteTextures (1, &texture);
#else
  g_assert_not_reached ();
#endif
}

static void
draw_pow2_texture (GstGLDrawable * drawable, GstGLImageType type,
    void *data, int width, int height)
{
  int pow2_width;
  int pow2_height;
  double x, y;
  GLuint texture;

  GST_DEBUG ("using power-of-2 texture");

  for (pow2_height = 64;
      pow2_height < height && pow2_height > 0; pow2_height <<= 1);
  for (pow2_width = 64; pow2_width < width && pow2_width > 0; pow2_width <<= 1);

  glEnable (GL_TEXTURE_2D);
  glGenTextures (1, &texture);
  glBindTexture (GL_TEXTURE_2D, texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  switch (type) {
    case GST_GL_IMAGE_TYPE_RGBx:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_GL_IMAGE_TYPE_BGRx:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_GL_IMAGE_TYPE_xRGB:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_GL_IMAGE_TYPE_xBGR:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_GL_IMAGE_TYPE_YUY2:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_YCBCR_MESA, pow2_width, pow2_height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_GL_IMAGE_TYPE_UYVY:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_YCBCR_MESA, pow2_width, pow2_height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_GL_IMAGE_TYPE_AYUV:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    default:
      g_assert_not_reached ();
  }

#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
  if (type == GST_GL_IMAGE_TYPE_AYUV) {
    const double matrix[16] = {
      1, 1, 1, 0,
      0, -0.344 * 1, 1.770 * 1, 0,
      1.403 * 1, -0.714 * 1, 0, 0,
      0, 0, 0, 1
    };
    glMatrixMode (GL_COLOR);
    glLoadMatrixd (matrix);
    glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, -1.403 / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, (0.344 + 0.714) / 2);
    glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, -1.770 / 2);
  }
#endif

  glColor4f (1, 0, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  x = (double) width / pow2_width;
  y = (double) height / pow2_height;

  glTexCoord2f (x, 0);
  glVertex3f (1.0, 1.0, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-1.0, 1.0, 0);
  glTexCoord2f (0, y);
  glVertex3f (-1.0, -1.0, 0);
  glTexCoord2f (x, y);
  glVertex3f (1.0, -1.0, 0);
  glEnd ();
  glDeleteTextures (1, &texture);
}

void
gst_gl_drawable_draw_image (GstGLDrawable * drawable, GstGLImageType type,
    void *data, int width, int height)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);

  gst_gl_drawable_lock (drawable);

#if 0
  /* Doesn't work */
  {
    int64_t ust = 1234;
    int64_t mst = 1234;
    int64_t sbc = 1234;
    gboolean ret;

    ret = glXGetSyncValuesOML (drawable->display->display, drawable->window,
        &ust, &mst, &sbc);
    GST_ERROR ("sync values %d %lld %lld %lld", ret, ust, mst, sbc);
  }
#endif

#if 0
  /* Does work, but is not relevant */
  {
    int32_t num = 1234;
    int32_t den = 1234;
    gboolean ret;

    ret = glXGetMscRateOML (drawable->display->display, drawable->window,
        &num, &den);
    GST_ERROR ("rate %d %d %d", ret, num, den);
  }
#endif

  gst_gl_drawable_update_attributes (drawable);

  glXSwapIntervalSGI (1);
  glViewport (0, 0, drawable->win_width, drawable->win_height);

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

  if (drawable->display->have_texture_rectangle) {
    draw_rect_texture (drawable, type, data, width, height);
  } else {
    draw_pow2_texture (drawable, type, data, width, height);
  }

  glXSwapBuffers (drawable->display->display, drawable->window);
#if 0
  /* Doesn't work */
  {
    ret = glXSwapBuffersMscOML (drawable->display->display, drawable->window,
        0, 1, 0);
    if (ret == 0) {
      GST_ERROR ("glXSwapBuffersMscOML failed");
    }
  }
#endif

  gst_gl_drawable_unlock (drawable);
}
