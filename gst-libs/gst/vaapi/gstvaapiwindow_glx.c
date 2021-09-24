/*
 *  gstvaapiwindow_glx.c - VA/GLX window abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapiwindow_glx
 * @short_description: VA/GLX window abstraction
 */

#include "sysdeps.h"
#include "gstvaapiwindow_glx.h"
#include "gstvaapiwindow_x11_priv.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapidisplay_glx_priv.h"
#include "gstvaapiutils_x11.h"
#include "gstvaapiutils_glx.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_vaapi_window);
#define GST_CAT_DEFAULT gst_debug_vaapi_window

#define GST_VAAPI_WINDOW_GLX_CAST(obj) ((GstVaapiWindowGLX *)(obj))
#define GST_VAAPI_WINDOW_GLX_GET_PRIVATE(window) \
    gst_vaapi_window_glx_get_instance_private (GST_VAAPI_WINDOW_GLX_CAST (window))

#define GST_VAAPI_WINDOW_GLX_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_WINDOW_GLX, GstVaapiWindowGLXClass))

typedef struct _GstVaapiWindowGLXPrivate GstVaapiWindowGLXPrivate;
typedef struct _GstVaapiWindowGLXClass GstVaapiWindowGLXClass;

struct _GstVaapiWindowGLXPrivate
{
  Colormap cmap;
  GLContextState *gl_context;
};

/**
 * GstVaapiWindowGLX:
 *
 * An X11 Window suitable for GLX rendering.
 */
struct _GstVaapiWindowGLX
{
  /*< private > */
  GstVaapiWindowX11 parent_instance;
};

/**
 * GstVaapiWindowGLXClass:
 *
 * An X11 Window suitable for GLX rendering.
 */
struct _GstVaapiWindowGLXClass
{
  /*< private > */
  GstVaapiWindowX11Class parent_class;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstVaapiWindowGLX, gst_vaapi_window_glx,
    GST_TYPE_VAAPI_WINDOW_X11);

/* Fill rectangle coords with capped bounds */
static inline void
fill_rect (GstVaapiRectangle * dst_rect,
    const GstVaapiRectangle * src_rect, guint width, guint height)
{
  if (src_rect) {
    dst_rect->x = src_rect->x > 0 ? src_rect->x : 0;
    dst_rect->y = src_rect->y > 0 ? src_rect->y : 0;
    if (src_rect->x + src_rect->width < width)
      dst_rect->width = src_rect->width;
    else
      dst_rect->width = width - dst_rect->x;
    if (src_rect->y + src_rect->height < height)
      dst_rect->height = src_rect->height;
    else
      dst_rect->height = height - dst_rect->y;
  } else {
    dst_rect->x = 0;
    dst_rect->y = 0;
    dst_rect->width = width;
    dst_rect->height = height;
  }
}

static void
_gst_vaapi_window_glx_destroy_context (GstVaapiWindow * window)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  if (priv->gl_context) {
    gl_destroy_context (priv->gl_context);
    priv->gl_context = NULL;
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
}

static gboolean
_gst_vaapi_window_glx_create_context (GstVaapiWindow * window,
    GLXContext foreign_context)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  GLContextState parent_cs;

  parent_cs.display = dpy;
  parent_cs.window = None;
  parent_cs.context = foreign_context;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  priv->gl_context = gl_create_context (dpy, DefaultScreen (dpy), &parent_cs);
  if (!priv->gl_context) {
    GST_DEBUG ("could not create GLX context");
    goto end;
  }

  if (!glXIsDirect (dpy, priv->gl_context->context)) {
    GST_DEBUG ("could not create a direct-rendering GLX context");
    goto out_destroy_context;
  }
  goto end;

out_destroy_context:
  gl_destroy_context (priv->gl_context);
  priv->gl_context = NULL;
end:
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  return priv->gl_context != NULL;
}

static gboolean
_gst_vaapi_window_glx_ensure_context (GstVaapiWindow * window,
    GLXContext foreign_context)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);

  if (priv->gl_context) {
    if (!foreign_context || foreign_context == priv->gl_context->context)
      return TRUE;
    _gst_vaapi_window_glx_destroy_context (window);
  }
  return _gst_vaapi_window_glx_create_context (window, foreign_context);
}

static gboolean
gst_vaapi_window_glx_ensure_context (GstVaapiWindow * window,
    GLXContext foreign_context)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  GLContextState old_cs;
  guint width, height;

  if (!_gst_vaapi_window_glx_ensure_context (window, foreign_context))
    return FALSE;

  priv->gl_context->window = GST_VAAPI_WINDOW_ID (window);
  if (!gl_set_current_context (priv->gl_context, &old_cs)) {
    GST_DEBUG ("could not make newly created GLX context current");
    return FALSE;
  }

  glDisable (GL_DEPTH_TEST);
  glDepthMask (GL_FALSE);
  glDisable (GL_CULL_FACE);
  glDrawBuffer (GL_BACK);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  gst_vaapi_window_get_size (window, &width, &height);
  gl_resize (width, height);

  gl_set_bgcolor (0);
  glClear (GL_COLOR_BUFFER_BIT);
  gl_set_current_context (&old_cs, NULL);
  return TRUE;
}

static guintptr
gst_vaapi_window_glx_get_visual_id (GstVaapiWindow * window)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);

  if (!_gst_vaapi_window_glx_ensure_context (window, NULL))
    return 0;
  return priv->gl_context->visual->visualid;
}

static void
gst_vaapi_window_glx_destroy_colormap (GstVaapiWindow * window)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);

  if (priv->cmap) {
    if (!window->use_foreign_window) {
      GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
      XFreeColormap (dpy, priv->cmap);
      GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    }
    priv->cmap = None;
  }
}

static Colormap
gst_vaapi_window_glx_create_colormap (GstVaapiWindow * window)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  XWindowAttributes wattr;
  gboolean success = FALSE;

  if (!priv->cmap) {
    if (!window->use_foreign_window) {
      if (!_gst_vaapi_window_glx_ensure_context (window, NULL))
        return None;
      GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
      x11_trap_errors ();
      /* XXX: add a GstVaapiDisplayX11:x11-screen property? */
      priv->cmap = XCreateColormap (dpy, RootWindow (dpy, DefaultScreen (dpy)),
          priv->gl_context->visual->visual, AllocNone);
      success = x11_untrap_errors () == 0;
      GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    } else {
      GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
      x11_trap_errors ();
      XGetWindowAttributes (dpy, GST_VAAPI_WINDOW_ID (window), &wattr);
      priv->cmap = wattr.colormap;
      success = x11_untrap_errors () == 0;
      GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    }
    if (!success)
      return None;
  }
  return priv->cmap;
}

static guintptr
gst_vaapi_window_glx_get_colormap (GstVaapiWindow * window)
{
  return GPOINTER_TO_SIZE (gst_vaapi_window_glx_create_colormap (window));
}

static gboolean
gst_vaapi_window_glx_resize (GstVaapiWindow * window, guint width, guint height)
{
  GstVaapiWindowGLXPrivate *const priv =
      GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  const GstVaapiWindowClass *const parent_klass =
      GST_VAAPI_WINDOW_CLASS (gst_vaapi_window_glx_parent_class);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  GLContextState old_cs;

  if (!parent_klass->resize (window, width, height))
    return FALSE;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  XSync (dpy, False);           /* make sure resize completed */
  if (gl_set_current_context (priv->gl_context, &old_cs)) {
    gl_resize (width, height);
    gl_set_current_context (&old_cs, NULL);
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  return TRUE;
}

static void
gst_vaapi_window_glx_finalize (GObject * object)
{
  GstVaapiWindow *const window = GST_VAAPI_WINDOW (object);

  _gst_vaapi_window_glx_destroy_context (window);
  gst_vaapi_window_glx_destroy_colormap (window);

  G_OBJECT_CLASS (gst_vaapi_window_glx_parent_class)->finalize (object);
}

static void
gst_vaapi_window_glx_class_init (GstVaapiWindowGLXClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiWindowClass *const window_class = GST_VAAPI_WINDOW_CLASS (klass);

  object_class->finalize = gst_vaapi_window_glx_finalize;

  window_class->resize = gst_vaapi_window_glx_resize;
  window_class->get_visual_id = gst_vaapi_window_glx_get_visual_id;
  window_class->get_colormap = gst_vaapi_window_glx_get_colormap;
}

static void
gst_vaapi_window_glx_init (GstVaapiWindowGLX * window)
{
}

/**
 * gst_vaapi_window_glx_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels
 * @height: the requested windo height, in pixels
 *
 * Creates a window with the specified @width and @height. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_glx_new (GstVaapiDisplay * display, guint width, guint height)
{
  GstVaapiWindow *window;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_GLX (display), NULL);

  window = gst_vaapi_window_new_internal (GST_TYPE_VAAPI_WINDOW_GLX, display,
      GST_VAAPI_ID_INVALID, width, height);
  if (!window)
    return NULL;

  if (!gst_vaapi_window_glx_ensure_context (window, NULL))
    goto error;
  return window;

  /* ERRORS */
error:
  {
    gst_object_unref (window);
    return NULL;
  }
}

/**
 * gst_vaapi_window_glx_new_with_xid:
 * @display: a #GstVaapiDisplay
 * @xid: an X11 Window id
 *
 * Creates a #GstVaapiWindow using the X11 Window @xid. The caller
 * still owns the window and must call XDestroyWindow() when all
 * #GstVaapiWindow references are released. Doing so too early can
 * yield undefined behaviour.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_glx_new_with_xid (GstVaapiDisplay * display, Window xid)
{
  GstVaapiWindow *window;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_GLX (display), NULL);
  g_return_val_if_fail (xid != None, NULL);

  window = gst_vaapi_window_new_internal (GST_TYPE_VAAPI_WINDOW_GLX, display,
      xid, 0, 0);
  if (!window)
    return NULL;

  if (!gst_vaapi_window_glx_ensure_context (window, NULL))
    goto error;
  return window;

  /* ERRORS */
error:
  {
    gst_object_unref (window);
    return NULL;
  }
}

/**
 * gst_vaapi_window_glx_get_context:
 * @window: a #GstVaapiWindowGLX
 *
 * Returns the #GLXContext bound to the @window.
 *
 * Return value: the #GLXContext bound to the @window
 */
GLXContext
gst_vaapi_window_glx_get_context (GstVaapiWindowGLX * window)
{
  GstVaapiWindowGLXPrivate *priv;

  g_return_val_if_fail (GST_VAAPI_IS_WINDOW_GLX (window), NULL);

  priv = GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  return priv->gl_context->context;
}

/**
 * gst_vaapi_window_glx_set_context:
 * @window: a #GstVaapiWindowGLX
 * @ctx: a GLX context
 *
 * Binds GLX context @ctx to @window. If @ctx is non %NULL, the caller
 * is responsible to making sure it has compatible visual with that of
 * the underlying X window. If @ctx is %NULL, a new context is created
 * and the @window owns it.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_window_glx_set_context (GstVaapiWindowGLX * window, GLXContext ctx)
{
  g_return_val_if_fail (GST_VAAPI_IS_WINDOW_GLX (window), FALSE);

  return gst_vaapi_window_glx_ensure_context (GST_VAAPI_WINDOW (window), ctx);
}

/**
 * gst_vaapi_window_glx_make_current:
 * @window: a #GstVaapiWindowGLX
 *
 * Makes the @window GLX context the current GLX rendering context of
 * the calling thread, replacing the previously current context if
 * there was one.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_window_glx_make_current (GstVaapiWindowGLX * window)
{
  gboolean success;
  GstVaapiWindowGLXPrivate *priv;

  g_return_val_if_fail (GST_VAAPI_IS_WINDOW_GLX (window), FALSE);

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  priv = GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  success = gl_set_current_context (priv->gl_context, NULL);
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  return success;
}

/**
 * gst_vaapi_window_glx_swap_buffers:
 * @window: a #GstVaapiWindowGLX
 *
 * Promotes the contents of the back buffer of @window to become the
 * contents of the front buffer of @window. This simply is wrapper
 * around glXSwapBuffers().
 */
void
gst_vaapi_window_glx_swap_buffers (GstVaapiWindowGLX * window)
{
  GstVaapiWindowGLXPrivate *priv;

  g_return_if_fail (GST_VAAPI_IS_WINDOW_GLX (window));

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  priv = GST_VAAPI_WINDOW_GLX_GET_PRIVATE (window);
  gl_swap_buffers (priv->gl_context);
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
}

/**
 * gst_vaapi_window_glx_put_texture:
 * @window: a #GstVaapiWindowGLX
 * @texture: a #GstVaapiTexture
 * @src_rect: the sub-rectangle of the source texture to
 *   extract and process. If %NULL, the entire texture will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   window into which the texture is rendered. If %NULL, the entire
 *   window will be used.
 *
 * Renders the @texture region specified by @src_rect into the @window
 * region specified by @dst_rect.
 *
 * NOTE: only GL_TEXTURE_2D textures are supported at this time.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_window_glx_put_texture (GstVaapiWindowGLX * window,
    GstVaapiTexture * texture,
    const GstVaapiRectangle * src_rect, const GstVaapiRectangle * dst_rect)
{
  GstVaapiRectangle tmp_src_rect, tmp_dst_rect;
  GLTextureState ts;
  GLenum tex_target;
  GLuint tex_id;
  guint tex_width, tex_height;
  guint win_width, win_height;

  g_return_val_if_fail (GST_VAAPI_IS_WINDOW_GLX (window), FALSE);
  g_return_val_if_fail (texture != NULL, FALSE);

  gst_vaapi_texture_get_size (texture, &tex_width, &tex_height);
  fill_rect (&tmp_src_rect, src_rect, tex_width, tex_height);
  src_rect = &tmp_src_rect;

  gst_vaapi_window_get_size (GST_VAAPI_WINDOW (window), &win_width,
      &win_height);
  fill_rect (&tmp_dst_rect, dst_rect, win_width, win_height);
  dst_rect = &tmp_dst_rect;

  /* XXX: only GL_TEXTURE_2D textures are supported at this time */
  tex_target = gst_vaapi_texture_get_target (texture);
  if (tex_target != GL_TEXTURE_2D)
    return FALSE;

  tex_id = gst_vaapi_texture_get_id (texture);
  if (!gl_bind_texture (&ts, tex_target, tex_id))
    return FALSE;
  glColor4f (1.0f, 1.0f, 1.0f, 1.0f);
  glPushMatrix ();
  glTranslatef ((GLfloat) dst_rect->x, (GLfloat) dst_rect->y, 0.0f);
  glBegin (GL_QUADS);
  {
    const float tx1 = (float) src_rect->x / tex_width;
    const float tx2 = (float) (src_rect->x + src_rect->width) / tex_width;
    const float ty1 = (float) src_rect->y / tex_height;
    const float ty2 = (float) (src_rect->y + src_rect->height) / tex_height;
    const guint w = dst_rect->width;
    const guint h = dst_rect->height;
    glTexCoord2f (tx1, ty1);
    glVertex2i (0, 0);
    glTexCoord2f (tx1, ty2);
    glVertex2i (0, h);
    glTexCoord2f (tx2, ty2);
    glVertex2i (w, h);
    glTexCoord2f (tx2, ty1);
    glVertex2i (w, 0);
  }
  glEnd ();
  glPopMatrix ();
  gl_unbind_texture (&ts);
  return TRUE;
}
