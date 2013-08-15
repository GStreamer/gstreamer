/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Cocoa/Cocoa.h>

#include "gstglcontext_cocoa.h"
#include "gstgl_cocoa_private.h"

static gboolean gst_gl_context_cocoa_create_context (GstGLContext *context, GstGLAPI gl_api,
    guintptr external_opengl_context, GError **error);
static guintptr gst_gl_context_cocoa_get_gl_context (GstGLContext * window);
static gboolean gst_gl_context_cocoa_activate (GstGLContext * context, gboolean activate);
static GstGLAPI gst_gl_context_cocoa_get_gl_api (GstGLContext * context);

#define GST_GL_CONTEXT_COCOA_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_CONTEXT_COCOA, GstGLContextCocoaPrivate))

G_DEFINE_TYPE (GstGLContextCocoa, gst_gl_context_cocoa, GST_GL_TYPE_CONTEXT);

static void
gst_gl_context_cocoa_class_init (GstGLContextCocoaClass * klass)
{
  GstGLContextClass *context_class;

#ifndef GNUSTEP
  NSAutoreleasePool* pool = nil;
#endif

  context_class = (GstGLContextClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLContextCocoaPrivate));

  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_create_context);
  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_get_gl_context);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_activate);
  context_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_get_gl_api);

#ifndef GNUSTEP
  pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];

  [pool release];
#endif
}

static void
gst_gl_context_cocoa_init (GstGLContextCocoa * context)
{
  context->priv = GST_GL_CONTEXT_COCOA_GET_PRIVATE (context);
}

/* Must be called in the gl thread */
GstGLContextCocoa *
gst_gl_context_cocoa_new (void)
{
  GstGLContextCocoa *context = g_object_new (GST_GL_TYPE_CONTEXT_COCOA, NULL);

  return context;
}

static gboolean
gst_gl_context_cocoa_create_context (GstGLContext *context, GstGLAPI gl_api,
    guintptr external_gl_context, GError **error)
{
  GstGLContextCocoa *context_cocoa = GST_GL_CONTEXT_COCOA (context);
  GstGLContextCocoaPrivate *priv = context_cocoa->priv;
  GstGLWindow *window = gst_gl_context_get_window (context);
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);
  GstGLNSOpenGLView *glView = nil;
  NSWindow *window_handle;
  NSRect rect;
  NSAutoreleasePool *pool;
  NSOpenGLPixelFormat *fmt = nil;
  NSOpenGLContext *glContext = nil;
  NSOpenGLPixelFormatAttribute attribs[] = {
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAAccumSize, 32,
    0
  };

  priv->gl_context = nil;
  priv->external_gl_context = (NSOpenGLContext *) external_gl_context;

  GSRegisterCurrentThread();

  pool = [[NSAutoreleasePool alloc] init];

#ifdef GNUSTEP
  [NSApplication sharedApplication];
#endif

  rect.origin.x = 0;
  rect.origin.y = 0;
  rect.size.width = 320;
  rect.size.height = 240;

  priv->rect = rect;

  gst_gl_window_cocoa_create_window (window_cocoa);
  window_handle = (NSWindow *) gst_gl_window_get_window_handle (window);

  glView = [GstGLNSOpenGLView alloc];

  fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];

  if (!fmt) {
    gst_object_unref (window);
    GST_WARNING ("cannot create NSOpenGLPixelFormat");
    return FALSE;
  }

  glView = [glView initWithFrame:window_cocoa rect:rect pixelFormat:fmt];

  [window_handle setContentView:glView];

#ifndef GNUSTEP
  glContext = [[NSOpenGLContext alloc] initWithFormat:fmt 
    shareContext:context_cocoa->priv->external_gl_context];

  GST_DEBUG ("NSOpenGL context created: %lud\n", (guintptr) glContext);

  context_cocoa->priv->gl_context = glContext;

  [glView setOpenGLContext:glContext];
#else
  context_cocoa->priv->gl_context = [glView openGLContext];
#endif

  /* OpenGL context is made current only one time threre.
   * Indeed, all OpenGL calls are made in only one thread,
   * the Application thread */
  [glContext makeCurrentContext];

  [glContext update];

  /* Back and front buffers are swapped only during the vertical retrace of the monitor.
   * Discarded if you configured your driver to Never-use-V-Sync.
   */
  NS_DURING {
    if (glContext) {
#ifdef GNUSTEP
      const long swapInterval = 1;
#else
      const GLint swapInterval = 1;
#endif
      [[glView openGLContext] setValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];
    }
  } NS_HANDLER {
     GST_DEBUG ("your back-end does not implement NSOpenglContext::setValues\n");
  }
  NS_ENDHANDLER

  GST_DEBUG ("opengl GstGLNSWindow initialized: %d x %d\n",
    (gint) rect.size.width, (gint) rect.size.height);

  [pool release];

#ifndef GNUSTEP
  priv->source_id = g_timeout_add_seconds (1, gst_gl_window_cocoa_nsapp_iteration, NULL);
#endif

  gst_object_unref (window);

  return TRUE;
}

static guintptr
gst_gl_context_cocoa_get_gl_context (GstGLContext * context)
{
  return (guintptr) GST_GL_CONTEXT_COCOA (context)->priv->gl_context;
}

static gboolean
gst_gl_context_cocoa_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextCocoa *context_cocoa;

  context_cocoa = GST_GL_CONTEXT_COCOA (context);

  if (activate)
    [context_cocoa->priv->gl_context makeCurrentContext];
#if 0
  else
    /* FIXME */
    [context_cocoa->priv->gl_context clearCurrentContext];
#endif
  return TRUE;
}

static GstGLAPI
gst_gl_context_cocoa_get_gl_api (GstGLContext * context)
{
  return GST_GL_API_OPENGL;
}
