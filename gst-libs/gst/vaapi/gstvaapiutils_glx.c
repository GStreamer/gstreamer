/*
 *  gstvaapiutils_glx.c - GLX utilties
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

#include "config.h"
#include <math.h>
#include "gstvaapiutils_glx.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/**
 * gl_get_error_string:
 * @error: an OpenGL error enumeration
 *
 * Retrieves the string representation the OpenGL @error.
 *
 * Return error: the static string representing the OpenGL @error
 */
const char *
gl_get_error_string(GLenum error)
{
    static const struct {
        GLenum val;
        const char *str;
    }
    gl_errors[] = {
        { GL_NO_ERROR,          "no error" },
        { GL_INVALID_ENUM,      "invalid enumerant" },
        { GL_INVALID_VALUE,     "invalid value" },
        { GL_INVALID_OPERATION, "invalid operation" },
        { GL_STACK_OVERFLOW,    "stack overflow" },
        { GL_STACK_UNDERFLOW,   "stack underflow" },
        { GL_OUT_OF_MEMORY,     "out of memory" },
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION_EXT
        { GL_INVALID_FRAMEBUFFER_OPERATION_EXT, "invalid framebuffer operation" },
#endif
        { ~0, NULL }
    };

    guint i;
    for (i = 0; gl_errors[i].str; i++) {
        if (gl_errors[i].val == error)
            return gl_errors[i].str;
    }
    return "unknown";
}

/**
 * gl_purge_errors:
 *
 * Purges all OpenGL errors. This function is generally useful to
 * clear up the pending errors prior to calling gl_check_error().
 */
void
gl_purge_errors(void)
{
    while (glGetError() != GL_NO_ERROR)
        ; /* nothing */
}

/**
 * gl_check_error:
 *
 * Checks whether there is any OpenGL error pending.
 *
 * Return value: %TRUE if an error was encountered
 */
gboolean
gl_check_error(void)
{
    GLenum error;
    gboolean has_errors = FALSE;

    while ((error = glGetError()) != GL_NO_ERROR) {
        GST_DEBUG("glError: %s caught", gl_get_error_string(error));
        has_errors = TRUE;
    }
    return has_errors;
}

/**
 * gl_get_param:
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetIntegerv() that does extra
 * error checking.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_get_param(GLenum param, guint *pval)
{
    GLint val;

    gl_purge_errors();
    glGetIntegerv(param, &val);
    if (gl_check_error())
        return FALSE;

    if (pval)
        *pval = val;
    return TRUE;
}

/**
 * gl_get_texture_param:
 * @target: the target to which the texture is bound
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetTexLevelParameteriv() that
 * does extra error checking.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_get_texture_param(GLenum target, GLenum param, guint *pval)
{
    GLint val;

    gl_purge_errors();
    glGetTexLevelParameteriv(target, 0, param, &val);
    if (gl_check_error())
        return FALSE;

    if (pval)
        *pval = val;
    return TRUE;
}

/**
 * gl_set_bgcolor:
 * @color: the requested RGB color
 *
 * Sets background color to the RGB @color. This basically is a
 * wrapper around glClearColor().
 */
void
gl_set_bgcolor(guint32 color)
{
    glClearColor(
        ((color >> 16) & 0xff) / 255.0f,
        ((color >>  8) & 0xff) / 255.0f,
        ( color        & 0xff) / 255.0f,
        1.0f
    );
}

/**
 * gl_perspective:
 * @fovy: the field of view angle, in degrees, in the y direction
 * @aspect: the aspect ratio that determines the field of view in the
 *   x direction.  The aspect ratio is the ratio of x (width) to y
 *   (height)
 * @zNear: the distance from the viewer to the near clipping plane
 *   (always positive)
 * @zFar: the distance from the viewer to the far clipping plane
 *   (always positive)
 *
 * Specified a viewing frustum into the world coordinate system. This
 * basically is the Mesa implementation of gluPerspective().
 */
static void
frustum(GLdouble left, GLdouble right,
        GLdouble bottom, GLdouble top, 
        GLdouble nearval, GLdouble farval)
{
    GLdouble x, y, a, b, c, d;
    GLdouble m[16];

    x = (2.0 * nearval) / (right - left);
    y = (2.0 * nearval) / (top - bottom);
    a = (right + left) / (right - left);
    b = (top + bottom) / (top - bottom);
    c = -(farval + nearval) / ( farval - nearval);
    d = -(2.0 * farval * nearval) / (farval - nearval);

#define M(row,col)  m[col*4+row]
    M(0,0) = x;     M(0,1) = 0.0F;  M(0,2) = a;      M(0,3) = 0.0F;
    M(1,0) = 0.0F;  M(1,1) = y;     M(1,2) = b;      M(1,3) = 0.0F;
    M(2,0) = 0.0F;  M(2,1) = 0.0F;  M(2,2) = c;      M(2,3) = d;
    M(3,0) = 0.0F;  M(3,1) = 0.0F;  M(3,2) = -1.0F;  M(3,3) = 0.0F;
#undef M

    glMultMatrixd(m);
}

static void
gl_perspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
    GLdouble xmin, xmax, ymin, ymax;

    ymax = zNear * tan(fovy * M_PI / 360.0);
    ymin = -ymax;
    xmin = ymin * aspect;
    xmax = ymax * aspect;

    /* Don't call glFrustum() because of error semantics (covglu) */
    frustum(xmin, xmax, ymin, ymax, zNear, zFar);
}

/**
 * gl_resize:
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Resizes the OpenGL viewport to the specified dimensions, using an
 * orthogonal projection. (0,0) represents the top-left corner of the
 * window.
 */
void
gl_resize(guint width, guint height)
{
#define FOVY     60.0f
#define ASPECT   1.0f
#define Z_NEAR   0.1f
#define Z_FAR    100.0f
#define Z_CAMERA 0.869f

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gl_perspective(FOVY, ASPECT, Z_NEAR, Z_FAR);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslatef(-0.5f, -0.5f, -Z_CAMERA);
    glScalef(1.0f/width, -1.0f/height, 1.0f/width);
    glTranslatef(0.0f, -1.0f*height, 0.0f);
}

/**
 * gl_make_current:
 * @dpy: an X11 #Display
 * @win: an X11 #Window
 * @ctx: the requested GLX context
 * @state: an optional #GLContextState
 *
 * Makes the @window GLX context the current GLX rendering context of
 * the calling thread, replacing the previously current context if
 * there was one.
 *
 * If @state is non %NULL, the previously current GLX context and
 * window are recorded.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_make_current(Display *dpy, Window win, GLXContext ctx, GLContextState *state)
{
    if (state) {
        state->context = glXGetCurrentContext();
        state->window  = glXGetCurrentDrawable();
        if (state->context == ctx && state->window == win)
            return TRUE;
    }
    return glXMakeCurrent(dpy, win, ctx);
}

/**
 * gl_swap_buffers:
 * @dpy: an X11 #Display
 * @win: an X11 #Window
 *
 * Promotes the contents of the back buffer of the @win window to
 * become the contents of the front buffer. This simply is wrapper
 * around glXSwapBuffers().
 */
void
gl_swap_buffers(Display *dpy, Window win)
{
    glXSwapBuffers(dpy, win);
}

/**
 * gl_bind_texture:
 * @ts: a #GLTextureState
 * @target: the target to which the texture is bound
 * @texture: the name of a texture
 *
 * Binds @texture to the specified @target, while recording the
 * previous state in @ts.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_bind_texture(GLTextureState *ts, GLenum target, GLuint texture)
{
    ts->target      = target;
    ts->old_texture = 0;
    ts->was_bound   = 0;
    ts->was_enabled = glIsEnabled(target);
    if (!ts->was_enabled)
        glEnable(target);

    GLenum texture_binding;
    switch (target) {
    case GL_TEXTURE_1D:
        texture_binding = GL_TEXTURE_BINDING_1D;
        break;
    case GL_TEXTURE_2D:
        texture_binding = GL_TEXTURE_BINDING_2D;
        break;
    case GL_TEXTURE_3D:
        texture_binding = GL_TEXTURE_BINDING_3D;
        break;
    case GL_TEXTURE_RECTANGLE_ARB:
        texture_binding = GL_TEXTURE_BINDING_RECTANGLE_ARB;
        break;
    default:
        g_assert(!texture);
        return FALSE;
    }

    if (ts->was_enabled && !gl_get_param(texture_binding, &ts->old_texture))
        return FALSE;

    ts->was_bound = texture == ts->old_texture;
    if (!ts->was_bound) {
        gl_purge_errors();
        glBindTexture(target, texture);
        if (gl_check_error())
            return FALSE;
    }
    return TRUE;
}

/**
 * gl_unbind_texture:
 * @ts: a #GLTextureState
 *
 * Rebinds the texture that was previously bound and recorded in @ts.
 */
void
gl_unbind_texture(GLTextureState *ts)
{
    if (!ts->was_bound && ts->old_texture)
        glBindTexture(ts->target, ts->old_texture);
    if (!ts->was_enabled)
        glDisable(ts->target);
}

/**
 * gl_create_texture:
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates a texture with the specified dimensions and @format. The
 * internal format will be automatically derived from @format.
 *
 * Return value: the newly created texture name
 */
GLuint
gl_create_texture(GLenum target, GLenum format, guint width, guint height)
{
    GLuint texture;
    GLTextureState ts;
    guint bytes_per_component;

    switch (format) {
    case GL_LUMINANCE:
        bytes_per_component = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        bytes_per_component = 2;
        break;
    case GL_RGBA:
    case GL_BGRA:
        bytes_per_component = 4;
        break;
    default:
        bytes_per_component = 0;
        break;
    }
    g_assert(bytes_per_component > 0);

    glGenTextures(1, &texture);
    if (!gl_bind_texture(&ts, target, texture))
        return 0;
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, bytes_per_component);
    glTexImage2D(
        target,
        0,
        bytes_per_component,
        width, height,
        0,
        format,
        GL_UNSIGNED_BYTE,
        NULL
    );
    gl_unbind_texture(&ts);
    return texture;
}
