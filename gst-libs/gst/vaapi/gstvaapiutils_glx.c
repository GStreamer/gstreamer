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
 * gl_get_param:
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetTexLevelParameteriv() that
 * does extra error checking.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_get_texture_param(GLenum param, guint *pval)
{
    GLint val;

    gl_purge_errors();
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, param, &val);
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
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
