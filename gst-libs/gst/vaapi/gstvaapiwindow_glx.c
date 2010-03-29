/*
 *  gstvaapiwindow_glx.c - VA/GLX window abstraction
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * SECTION:gstvaapiwindow_glx
 * @short_description: VA/GLX window abstraction
 */

#include "config.h"
#include "gstvaapiwindow_glx.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapiutils_x11.h"
#include "gstvaapiutils_glx.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiWindowGLX,
              gst_vaapi_window_glx,
              GST_VAAPI_TYPE_WINDOW_X11);

#define GST_VAAPI_WINDOW_GLX_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW_GLX,     \
                                 GstVaapiWindowGLXPrivate))

struct _GstVaapiWindowGLXPrivate {
    XVisualInfo        *vi;
    XVisualInfo         vi_static;
    Colormap            cmap;
    GLXContext          context;
    guint               is_constructed  : 1;
    guint               foreign_context : 1;
    guint               foreign_window  : 1;
    guint               swapped_buffers : 1;
};

enum {
    PROP_0,

    PROP_GLX_CONTEXT
};

static XVisualInfo *
gst_vaapi_window_glx_create_visual(GstVaapiWindowGLX *window);

/* Fill rectangle coords with capped bounds */
static inline void
fill_rect(
    GstVaapiRectangle       *dst_rect,
    const GstVaapiRectangle *src_rect,
    guint                    width,
    guint                    height
)
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
    }
    else {
        dst_rect->x      = 0;
        dst_rect->y      = 0;
        dst_rect->width  = width;
        dst_rect->height = height;
    }
}

static inline void
_gst_vaapi_window_glx_set_context(
    GstVaapiWindowGLX *window,
    GLXContext         context,
    gboolean           is_foreign
)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;

    priv->context         = context;
    priv->foreign_context = is_foreign;
}

static void
gst_vaapi_window_glx_destroy_context(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);

    if (priv->context) {
        if (!priv->foreign_context) {
            GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
            if (glXGetCurrentContext() == priv->context) {
                /* XXX: if buffers were never swapped, the application
                   will crash later with the NVIDIA driver */
                if (!priv->swapped_buffers)
                    gl_swap_buffers(dpy, GST_VAAPI_OBJECT_ID(window));
                gl_make_current(dpy, None, NULL, NULL);
            }
            glXDestroyContext(dpy, priv->context);
            GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        }
        priv->context         = NULL;
        priv->foreign_context = FALSE;
    }
}

static gboolean
gst_vaapi_window_glx_create_context(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    GLXContext                       ctx  = NULL;
    GLContextState                   cs;
    guint                            width, height;
    gboolean                         has_errors = TRUE;

    if (!gst_vaapi_window_glx_create_visual(window))
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    ctx = glXCreateContext(dpy, priv->vi, NULL, True);
    if (ctx && glXIsDirect(dpy, ctx)) {
        _gst_vaapi_window_glx_set_context(window, ctx, FALSE);
        if (gl_make_current(dpy, GST_VAAPI_OBJECT_ID(window), ctx, &cs)) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glDrawBuffer(GL_BACK);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            gst_vaapi_window_get_size(GST_VAAPI_WINDOW(window), &width, &height);
            gl_resize(width, height);

            gl_set_bgcolor(0);
            glClear(GL_COLOR_BUFFER_BIT);
            if (cs.context)
                gl_make_current(dpy, cs.window, cs.context, NULL);
            has_errors = FALSE;
        }
    }
    else if (ctx)
        glXDestroyContext(dpy, ctx);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

    return !has_errors;
}

static inline void
gst_vaapi_window_glx_destroy_visual(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;

    if (priv->vi) {
        if (priv->vi != &priv->vi_static)
            XFree(priv->vi);
        priv->vi = NULL;
    }
}

static XVisualInfo *
gst_vaapi_window_glx_create_visual(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;
    Display * const dpy = GST_VAAPI_OBJECT_XDISPLAY(window);
    XWindowAttributes wattr;
    int screen;
    gboolean has_errors;

    /* XXX: add and use a GstVaapiWindow:double-buffer property? */
    static GLint gl_visual_attr[] = {
        GLX_RGBA,
        GLX_RED_SIZE,   1,
        GLX_GREEN_SIZE, 1,
        GLX_BLUE_SIZE,  1,
        GLX_DOUBLEBUFFER,
        GL_NONE
    };

    if (!priv->vi) {
        /* XXX: add and use a GstVaapiDisplayX11:x11-screen property? */
        screen = DefaultScreen(dpy);

        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        x11_trap_errors();
        if (!priv->foreign_window)
            priv->vi = glXChooseVisual(dpy, screen, gl_visual_attr);
        else {
            XGetWindowAttributes(dpy, GST_VAAPI_OBJECT_ID(window), &wattr);
            if (XMatchVisualInfo(dpy, screen, wattr.depth, wattr.visual->class,
                                 &priv->vi_static))
                priv->vi = &priv->vi_static;
        }
        has_errors = x11_untrap_errors() != 0;
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

        if (has_errors)
            return NULL;
    }
    return priv->vi;
}

static Visual *
gst_vaapi_window_glx_get_visual(GstVaapiWindow *window)
{
    XVisualInfo *vi;

    vi = gst_vaapi_window_glx_create_visual(GST_VAAPI_WINDOW_GLX(window));
    if (!vi)
        return NULL;
    return vi->visual;
}

static void
gst_vaapi_window_glx_destroy_colormap(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);

    if (priv->cmap) {
        if (!priv->foreign_window) {
            GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
            XFreeColormap(dpy, priv->cmap);
            GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        }
        priv->cmap = None;
    }
}

static Colormap
gst_vaapi_window_glx_create_colormap(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate * const priv = window->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    int                              screen;
    XWindowAttributes                wattr;
    XVisualInfo                     *vi;
    gboolean                         has_errors;

    if (!priv->cmap) {
        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        x11_trap_errors();
        if (!priv->foreign_window) {
            vi = gst_vaapi_window_glx_create_visual(window);
            if (vi) {
                /* XXX: add a GstVaapiDisplayX11:x11-screen property? */
                screen     = DefaultScreen(dpy);
                priv->cmap = XCreateColormap(
                    dpy,
                    RootWindow(dpy, screen),
                    vi->visual,
                    AllocNone
                );
            }
        }
        else {
            XGetWindowAttributes(dpy, GST_VAAPI_OBJECT_ID(window), &wattr);
            priv->cmap = wattr.colormap;
        }
        has_errors = x11_untrap_errors() != 0;
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

        if (has_errors)
            return None;
    }
    return priv->cmap;
}

static Colormap
gst_vaapi_window_glx_get_colormap(GstVaapiWindow *window)
{
    return gst_vaapi_window_glx_create_colormap(GST_VAAPI_WINDOW_GLX(window));
}

static gboolean
gst_vaapi_window_glx_resize(GstVaapiWindow *window, guint width, guint height)
{
    GstVaapiWindowGLXPrivate * const priv = GST_VAAPI_WINDOW_GLX(window)->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    GLContextState                   cs;

    if (!GST_VAAPI_WINDOW_CLASS(gst_vaapi_window_glx_parent_class)->
        resize(window, width, height))
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    XSync(dpy, False); /* make sure resize completed */
    if (gl_make_current(dpy, GST_VAAPI_OBJECT_ID(window), priv->context, &cs)) {
        gl_resize(width, height);
        if (cs.context)
            gl_make_current(dpy, cs.window, cs.context, NULL);
    }
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    return TRUE;
}

static void
gst_vaapi_window_glx_finalize(GObject *object)
{
    GstVaapiWindowGLX * const window = GST_VAAPI_WINDOW_GLX(object);

    gst_vaapi_window_glx_destroy_context(window);
    gst_vaapi_window_glx_destroy_visual(window);
    gst_vaapi_window_glx_destroy_colormap(window);

    G_OBJECT_CLASS(gst_vaapi_window_glx_parent_class)->finalize(object);
}

static void
gst_vaapi_window_glx_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiWindowGLX * const window = GST_VAAPI_WINDOW_GLX(object);

    switch (prop_id) {
    case PROP_GLX_CONTEXT:
        gst_vaapi_window_glx_set_context(window, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_window_glx_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiWindowGLX * const window = GST_VAAPI_WINDOW_GLX(object);

    switch (prop_id) {
    case PROP_GLX_CONTEXT:
        g_value_set_pointer(value, gst_vaapi_window_glx_get_context(window));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_window_glx_constructed(GObject *object)
{
    GstVaapiWindowGLXPrivate * const priv = GST_VAAPI_WINDOW_GLX(object)->priv;
    GObjectClass *parent_class;

    priv->foreign_context = priv->context != NULL;

    parent_class = G_OBJECT_CLASS(gst_vaapi_window_glx_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->foreign_window =
        gst_vaapi_window_x11_is_foreign_xid(GST_VAAPI_WINDOW_X11(object));

    priv->is_constructed = priv->foreign_context ||
        gst_vaapi_window_glx_create_context(GST_VAAPI_WINDOW_GLX(object));
}

static void
gst_vaapi_window_glx_class_init(GstVaapiWindowGLXClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiWindowClass * const win_class = GST_VAAPI_WINDOW_CLASS(klass);
    GstVaapiWindowX11Class * const xwin_class = GST_VAAPI_WINDOW_X11_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiWindowGLXPrivate));

    object_class->finalize      = gst_vaapi_window_glx_finalize;
    object_class->set_property  = gst_vaapi_window_glx_set_property;
    object_class->get_property  = gst_vaapi_window_glx_get_property;
    object_class->constructed   = gst_vaapi_window_glx_constructed;

    win_class->resize           = gst_vaapi_window_glx_resize;
    xwin_class->get_visual      = gst_vaapi_window_glx_get_visual;
    xwin_class->get_colormap    = gst_vaapi_window_glx_get_colormap;

    /**
     * GstVaapiDisplayGLX:glx-context:
     *
     * The GLX context that was created by gst_vaapi_window_glx_new()
     * or that was bound from gst_vaapi_window_glx_set_context().
     */
    g_object_class_install_property
        (object_class,
         PROP_GLX_CONTEXT,
         g_param_spec_pointer("glx-context",
                              "GLX context",
                              "GLX context",
                              G_PARAM_READWRITE));
}

static void
gst_vaapi_window_glx_init(GstVaapiWindowGLX *window)
{
    GstVaapiWindowGLXPrivate *priv = GST_VAAPI_WINDOW_GLX_GET_PRIVATE(window);

    window->priv                = priv;
    priv->vi                    = NULL;
    priv->cmap                  = None;
    priv->context               = NULL;
    priv->is_constructed        = FALSE;
    priv->foreign_context       = FALSE;
    priv->foreign_window        = FALSE;
    priv->swapped_buffers       = FALSE;
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
gst_vaapi_window_glx_new(GstVaapiDisplay *display, guint width, guint height)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(width  > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    return g_object_new(GST_VAAPI_TYPE_WINDOW_GLX,
                        "display", display,
                        "id",      GST_VAAPI_ID(None),
                        "width",   width,
                        "height",  height,
                        NULL);
}

/**
 * gst_vaapi_window_glx_new_with_xid:
 * @display: a #GstVaapiDisplay
 * @xid: an X11 #Window id
 *
 * Creates a #GstVaapiWindow using the X11 #Window @xid. The caller
 * still owns the window and must call XDestroyWindow() when all
 * #GstVaapiWindow references are released. Doing so too early can
 * yield undefined behaviour.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_glx_new_with_xid(GstVaapiDisplay *display, Window xid)
{
    GST_DEBUG("new window from xid 0x%08x", xid);

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(xid != None, NULL);

    return g_object_new(GST_VAAPI_TYPE_WINDOW_GLX,
                        "display", display,
                        "id",      GST_VAAPI_ID(xid),
                        NULL);
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
gst_vaapi_window_glx_get_context(GstVaapiWindowGLX *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW_GLX(window), NULL);
    g_return_val_if_fail(window->priv->is_constructed, FALSE);

    return window->priv->context;
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
gst_vaapi_window_glx_set_context(GstVaapiWindowGLX *window, GLXContext ctx)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW_GLX(window), FALSE);
    g_return_val_if_fail(window->priv->is_constructed, FALSE);

    gst_vaapi_window_glx_destroy_context(window);

    if (ctx) {
        _gst_vaapi_window_glx_set_context(window, ctx, TRUE);
        return TRUE;
    }
    return gst_vaapi_window_glx_create_context(window);
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
gst_vaapi_window_glx_make_current(GstVaapiWindowGLX *window)
{
    gboolean success;

    g_return_val_if_fail(GST_VAAPI_IS_WINDOW_GLX(window), FALSE);
    g_return_val_if_fail(window->priv->is_constructed, FALSE);

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    success = gl_make_current(
        GST_VAAPI_OBJECT_XDISPLAY(window),
        GST_VAAPI_OBJECT_ID(window),
        window->priv->context,
        NULL
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
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
gst_vaapi_window_glx_swap_buffers(GstVaapiWindowGLX *window)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW_GLX(window));
    g_return_if_fail(window->priv->is_constructed);

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    gl_swap_buffers(
        GST_VAAPI_OBJECT_XDISPLAY(window),
        GST_VAAPI_OBJECT_ID(window)
    );
    glClear(GL_COLOR_BUFFER_BIT);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

    window->priv->swapped_buffers = TRUE;
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
gst_vaapi_window_glx_put_texture(
    GstVaapiWindowGLX       *window,
    GstVaapiTexture         *texture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
)
{
    GstVaapiRectangle tmp_src_rect, tmp_dst_rect;
    GLTextureState ts;
    GLenum tex_target;
    GLuint tex_id;
    guint tex_width, tex_height;
    guint win_width, win_height;

    g_return_val_if_fail(GST_VAAPI_IS_WINDOW_GLX(window), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), FALSE);

    gst_vaapi_texture_get_size(texture, &tex_width, &tex_height);
    fill_rect(&tmp_src_rect, src_rect, tex_width, tex_height);
    src_rect = &tmp_src_rect;

    gst_vaapi_window_get_size(GST_VAAPI_WINDOW(window), &win_width, &win_height);
    fill_rect(&tmp_dst_rect, dst_rect, win_width, win_height);
    dst_rect = &tmp_dst_rect;

    tex_target = gst_vaapi_texture_get_target(texture);
    tex_id     = gst_vaapi_texture_get_id(texture);

    /* XXX: only GL_TEXTURE_2D textures are supported at this time */
    if (tex_target != GL_TEXTURE_2D)
        return FALSE;

    if (!gl_bind_texture(&ts, tex_target, tex_id))
        return FALSE;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPushMatrix();
    glTranslatef((GLfloat)dst_rect->x, (GLfloat)dst_rect->y, 0.0f);
    glBegin(GL_QUADS);
    {
        const float tx1 = (float)src_rect->x / tex_width;
        const float tx2 = (float)(src_rect->x + src_rect->width) / tex_width;
        const float ty1 = (float)src_rect->y / tex_height;
        const float ty2 = (float)(src_rect->y + src_rect->height) / tex_height;
        const guint w   = dst_rect->width;
        const guint h   = dst_rect->height;
        glTexCoord2f(tx1, ty1); glVertex2i(0, 0);
        glTexCoord2f(tx1, ty2); glVertex2i(0, h);
        glTexCoord2f(tx2, ty2); glVertex2i(w, h);
        glTexCoord2f(tx2, ty1); glVertex2i(w, 0);
    }
    glEnd();
    glPopMatrix();
    gl_unbind_texture(&ts);
    return TRUE;
}
