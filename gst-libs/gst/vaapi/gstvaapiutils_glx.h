/*
 *  gstvaapiutils_glx.h - GLX utilties
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

#ifndef GST_VAAPI_UTILS_GLX_H
#define GST_VAAPI_UTILS_GLX_H

#include "config.h"
#include <GL/gl.h>
#include <GL/glx.h>
#include <glib/gtypes.h>

const char *
gl_get_error_string(GLenum error)
    attribute_hidden;

void
gl_purge_errors(void)
    attribute_hidden;

gboolean
gl_check_error(void)
    attribute_hidden;

gboolean
gl_get_param(GLenum param, guint *pval)
    attribute_hidden;

gboolean
gl_get_texture_param(GLenum param, guint *pval)
    attribute_hidden;

void
gl_set_bgcolor(guint32 color)
    attribute_hidden;

void
gl_resize(guint width, guint height)
    attribute_hidden;

typedef struct _GLContextState GLContextState;
struct _GLContextState {
    GLXContext  context;
    Window      window;
};

gboolean
gl_make_current(Display *dpy, Window win, GLXContext ctx, GLContextState *state)
    attribute_hidden;

#endif /* GST_VAAPI_UTILS_GLX_H */
