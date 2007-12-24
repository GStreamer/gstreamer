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

static void gst_gl_display_finalize (GObject * object);
static void gst_gl_display_init_tmp_window (GstGLDisplay * display);

GST_BOILERPLATE (GstGLDisplay, gst_gl_display, GObject, G_TYPE_OBJECT);

static void
gst_gl_display_base_init (gpointer g_class)
{

}

static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}

static void
gst_gl_display_init (GstGLDisplay * display, GstGLDisplayClass * klass)
{

  display->lock = g_mutex_new ();

}

static void
gst_gl_display_finalize (GObject * object)
{
  GstGLDisplay *display = GST_GL_DISPLAY (object);

  if (display->assigned_window == None) {
    XDestroyWindow (display->display, display->window);
  }
  if (display->context) {
    glXDestroyContext (display->display, display->context);
  }
  if (display->visinfo) {
    XFree (display->visinfo);
  }
  if (display->display) {
    XCloseDisplay (display->display);
  }

  if (display->lock) {
    g_mutex_free (display->lock);
  }
}

static gboolean gst_gl_display_check_features (GstGLDisplay * display);

GstGLDisplay *
gst_gl_display_new (void)
{
  return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
}

#define HANDLE_X_ERRORS
#ifdef HANDLE_X_ERRORS
static int
x_error_handler (Display * display, XErrorEvent * event)
{
  g_assert_not_reached ();
}
#endif

gboolean
gst_gl_display_connect (GstGLDisplay * display, const char *display_name)
{
  gboolean usable;
  XGCValues values;
  XPixmapFormatValues *px_formats;
  int n_formats;
  int i;

  display->display = XOpenDisplay (display_name);
  if (display->display == NULL) {
    return FALSE;
  }
#ifdef HANDLE_X_ERRORS
  XSynchronize (display->display, True);
  XSetErrorHandler (x_error_handler);
#endif

  usable = gst_gl_display_check_features (display);
  if (!usable) {
    return FALSE;
  }

  display->screen = DefaultScreenOfDisplay (display->display);
  display->screen_num = DefaultScreen (display->display);
  display->visual = DefaultVisual (display->display, display->screen_num);
  display->root = DefaultRootWindow (display->display);
  display->white = XWhitePixel (display->display, display->screen_num);
  display->black = XBlackPixel (display->display, display->screen_num);
  display->depth = DefaultDepthOfScreen (display->screen);

  display->gc = XCreateGC (display->display,
      DefaultRootWindow (display->display), 0, &values);

  px_formats = XListPixmapFormats (display->display, &n_formats);
  for (i = 0; i < n_formats; i++) {
    GST_DEBUG ("%d: depth %d bpp %d pad %d", i,
        px_formats[i].depth,
        px_formats[i].bits_per_pixel, px_formats[i].scanline_pad);
  }

  gst_gl_display_init_tmp_window (display);

  return TRUE;
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
gst_gl_display_can_handle_type (GstGLDisplay * display, GstVideoFormat type)
{
  switch (type) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      return TRUE;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return display->have_ycbcr_texture;
    case GST_VIDEO_FORMAT_AYUV:
      return display->have_color_matrix;
    default:
      return FALSE;
  }
}

void
gst_gl_display_lock (GstGLDisplay * display)
{
  g_mutex_lock (display->lock);
  glXMakeCurrent (display->display, display->window, display->context);
  gst_gl_display_check_error (display, __LINE__);
}

void
gst_gl_display_unlock (GstGLDisplay * display)
{
  gst_gl_display_check_error (display, __LINE__);
  glXMakeCurrent (display->display, None, NULL);
  g_mutex_unlock (display->lock);
}

static void
gst_gl_display_init_tmp_window (GstGLDisplay * display)
{
  XSetWindowAttributes attr = { 0 };
  int scrnum;
  int mask;
  Window root;
  Screen *screen;

  GST_ERROR ("creating temp window");

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

  display->window = XCreateWindow (display->display,
      root, 0, 0, 100, 100,
      0, display->visinfo->depth, InputOutput,
      display->visinfo->visual, mask, &attr);
  XMapWindow (display->display, display->window);
  XSync (display->display, FALSE);
}

static void
gst_gl_display_destroy_tmp_window (GstGLDisplay * display)
{
  XDestroyWindow (display->display, display->window);
}

void
gst_gl_display_set_window (GstGLDisplay * display, Window window)
{
  g_mutex_lock (display->lock);

  if (window != display->assigned_window) {
    if (display->assigned_window == None) {
      gst_gl_display_destroy_tmp_window (display);
    }
    display->assigned_window = window;
    if (display->assigned_window == None) {
      gst_gl_display_init_tmp_window (display);
    } else {
      display->window = window;
    }
  }

  g_mutex_unlock (display->lock);
}

void
gst_gl_display_update_attributes (GstGLDisplay * display)
{
  XWindowAttributes attr;

  if (display->window != None) {
    XGetWindowAttributes (display->display, display->window, &attr);
    display->win_width = attr.width;
    display->win_height = attr.height;
  } else {
    display->win_width = 0;
    display->win_height = 0;
  }
}

void
gst_gl_display_clear (GstGLDisplay * display)
{
  gst_gl_display_lock (display);

  glDepthFunc (GL_LESS);
  glEnable (GL_DEPTH_TEST);
  glClearColor (0.2, 0.2, 0.2, 1.0);
  glViewport (0, 0, display->win_width, display->win_height);

  gst_gl_display_unlock (display);
}

void
gst_gl_display_check_error (GstGLDisplay * display, int line)
{
  GLenum err = glGetError ();

  if (err) {
    GST_ERROR ("GL Error 0x%x at line %d", (int) err, line);
    g_assert (0);
  }
}


GLuint
gst_gl_display_upload_texture_rectangle (GstGLDisplay * display,
    GstVideoFormat type, void *data, int width, int height)
{
  GLuint texture;

  glGenTextures (1, &texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  switch (type) {
    case GST_VIDEO_FORMAT_RGBx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    default:
      g_assert_not_reached ();
  }

  return texture;
}


static void
draw_rect_texture (GstGLDisplay * display, GstVideoFormat type,
    void *data, int width, int height)
{
  GLuint texture;

  GST_DEBUG ("using rectangular texture");

#ifdef GL_TEXTURE_RECTANGLE_ARB
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  //glGenTextures (1, &texture);
  //glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  texture = gst_gl_display_upload_texture_rectangle (display, type,
      data, width, height);

  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

#if 0
  switch (type) {
    case GST_VIDEO_FORMAT_RGBx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    default:
      g_assert_not_reached ();
  }

#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
  if (type == GST_VIDEO_FORMAT_AYUV) {
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
draw_pow2_texture (GstGLDisplay * display, GstVideoFormat type,
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
    case GST_VIDEO_FORMAT_RGBx:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_YCBCR_MESA, pow2_width, pow2_height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_YCBCR_MESA, pow2_width, pow2_height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, pow2_width, pow2_height,
          0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    default:
      g_assert_not_reached ();
  }

#ifdef GL_POST_COLOR_MATRIX_RED_BIAS
  if (type == GST_VIDEO_FORMAT_AYUV) {
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
gst_gl_display_draw_image (GstGLDisplay * display, GstVideoFormat type,
    void *data, int width, int height)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);

  gst_gl_display_lock (display);

#if 0
  /* Doesn't work */
  {
    int64_t ust = 1234;
    int64_t mst = 1234;
    int64_t sbc = 1234;
    gboolean ret;

    ret = glXGetSyncValuesOML (display->display, display->window,
        &ust, &mst, &sbc);
    GST_DEBUG ("sync values %d %lld %lld %lld", ret, ust, mst, sbc);
  }
#endif

#if 0
  /* Does work, but is not relevant */
  {
    int32_t num = 1234;
    int32_t den = 1234;
    gboolean ret;

    ret = glXGetMscRateOML (display->display, display->window, &num, &den);
    GST_DEBUG ("rate %d %d %d", ret, num, den);
  }
#endif

  gst_gl_display_update_attributes (display);

  //glXSwapIntervalSGI (1);
  glViewport (0, 0, display->win_width, display->win_height);

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

  if (display->have_texture_rectangle) {
    draw_rect_texture (display, type, data, width, height);
  } else {
    draw_pow2_texture (display, type, data, width, height);
  }

  glXSwapBuffers (display->display, display->window);
#if 0
  /* Doesn't work */
  {
    ret = glXSwapBuffersMscOML (display->display, display->window, 0, 1, 0);
    if (ret == 0) {
      GST_DEBUG ("glXSwapBuffersMscOML failed");
    }
  }
#endif

  gst_gl_display_unlock (display);
}

void
gst_gl_display_draw_rbo (GstGLDisplay * display, GLuint rbo,
    int width, int height)
{
  GLuint texture;
  GLuint fbo;

  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);
  g_return_if_fail (rbo != None);

  gst_gl_display_lock (display);

  g_assert (display->window != None);
  g_assert (display->context != NULL);

  gst_gl_display_update_attributes (display);

  glViewport (0, 0, display->win_width, display->win_height);

  glClearColor (0.3, 0.3, 0.3, 1.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

  glGenFramebuffersEXT (1, &fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
      GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rbo);

  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);
  glReadBuffer (GL_COLOR_ATTACHMENT0_EXT);

  g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
      GL_FRAMEBUFFER_COMPLETE_EXT);

#if 1
  {
    void *buffer;

    buffer = malloc (320 * 240 * 4);
    memset (buffer, random (), 320 * 240 * 4);
    free (buffer);
  }
#endif

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glGenTextures (1, &texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB, width, height, 0,
      GL_RGB, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, texture, 0);

  glDrawBuffer (0);
  glReadBuffer (0);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
  glColor4f (1, 0, 1, 1);
  gst_gl_display_check_error (display, __LINE__);
//glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  glTexCoord2f (width, 0);
  glVertex3f (0.9, 0.9, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-0.9, 0.9, 0);
  glTexCoord2f (0, height);
  glVertex3f (-0.9, -0.9, 0);
  glTexCoord2f (width, height);
  glVertex3f (0.9, -0.9, 0);
  glEnd ();
  gst_gl_display_check_error (display, __LINE__);
  glDeleteTextures (1, &texture);

  glDeleteFramebuffersEXT (1, &fbo);
  gst_gl_display_check_error (display, __LINE__);

  glXSwapBuffers (display->display, display->window);

  gst_gl_display_unlock (display);
}

void
gst_gl_display_draw_texture (GstGLDisplay * display, GLuint texture,
    int width, int height)
{
  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);
  g_return_if_fail (texture != None);

  gst_gl_display_lock (display);

  g_assert (display->window != None);
  g_assert (display->context != NULL);

  gst_gl_display_update_attributes (display);

  glViewport (0, 0, display->win_width, display->win_height);

  glClearColor (0.3, 0.3, 0.3, 1.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glDisable (GL_CULL_FACE);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);

  glColor4f (1, 1, 1, 1);

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glColor4f (1, 0, 1, 1);
  gst_gl_display_check_error (display, __LINE__);
  glBegin (GL_QUADS);

  glNormal3f (0, 0, -1);

  glTexCoord2f (width, 0);
  glVertex3f (0.9, 0.9, 0);
  glTexCoord2f (0, 0);
  glVertex3f (-0.9, 0.9, 0);
  glTexCoord2f (0, height);
  glVertex3f (-0.9, -0.9, 0);
  glTexCoord2f (width, height);
  glVertex3f (0.9, -0.9, 0);
  glEnd ();
  gst_gl_display_check_error (display, __LINE__);

  glXSwapBuffers (display->display, display->window);

  gst_gl_display_unlock (display);
}
