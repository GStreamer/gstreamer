/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2009 Andrey Nechypurenko <andreynech@gmail.com>
 * Copyright (C) 2010 Nuno Santos <nunosantos@imaginando.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GLCONTEXTID_H
#define __GLCONTEXTID_H

#include <gst/gl/gstglconfig.h>

#if defined(GST_GL_HAVE_PLATFORM_WGL)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <Wingdi.h>
    #include <GL/gl.h>
#elif defined (GST_GL_HAVE_PLATFORM_COCOA)
    #include <OpenGL/OpenGL.h>
    class NSOpenGLContext;
#else
    #include <X11/Xlib.h>
    #include <GL/gl.h>
    #include <GL/glu.h>
    #include <GL/glx.h>
#endif


#if defined(GST_GL_HAVE_PLATFORM_WGL)
    typedef struct _tagGLContextID
    {
        HGLRC contextId;
        HDC dc;
    } GLContextID;
#elif  defined(GST_GL_HAVE_PLATFORM_COCOA)
    typedef struct _tagGLContextID
    {
        NSOpenGLContext* contextId;
    } GLContextID;
#elif defined(GST_GL_HAVE_PLATFORM_GLX)
    typedef struct _tagGLContextID
    {
        GLXContext contextId;
        Display *display;
        Window wnd;
    } GLContextID;
#endif

#endif // __GLCONTEXTID_H

