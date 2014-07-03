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
    GstGLContext * other_context, GError **error);
static void gst_gl_context_cocoa_destroy_context (GstGLContext *context);
static guintptr gst_gl_context_cocoa_get_gl_context (GstGLContext * window);
static gboolean gst_gl_context_cocoa_activate (GstGLContext * context, gboolean activate);
static GstGLAPI gst_gl_context_cocoa_get_gl_api (GstGLContext * context);
static GstGLPlatform gst_gl_context_cocoa_get_gl_platform (GstGLContext * context);

#define GST_GL_CONTEXT_COCOA_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_CONTEXT_COCOA, GstGLContextCocoaPrivate))

G_DEFINE_TYPE (GstGLContextCocoa, gst_gl_context_cocoa, GST_GL_TYPE_CONTEXT);

#ifndef GNUSTEP
static GMutex nsapp_lock;
static GCond nsapp_cond;

static gboolean
gst_gl_window_cocoa_init_nsapp (gpointer data)
{
  NSAutoreleasePool *pool = nil;

  g_mutex_lock (&nsapp_lock);

  pool = [[NSAutoreleasePool alloc] init];

  /* The sharedApplication class method initializes
   * the display environment and connects your program
   * to the window server and the display server
   */

  /* TODO: so consider to create GstGLDisplayCocoa
   * in gst/gl/cocoa/gstgldisplay_cocoa.h/c
   */

  /* has to be called in the main thread */
  [NSApplication sharedApplication];

  GST_DEBUG ("NSApp initialized from a GTimeoutSource");

  [pool release];

  g_cond_signal (&nsapp_cond);
  g_mutex_unlock (&nsapp_lock);

  return FALSE;
}

static gboolean
gst_gl_window_cocoa_nsapp_iteration (gpointer data)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  NSEvent *event = nil;

  if ([NSThread isMainThread]) {

    while ((event = ([NSApp nextEventMatchingMask:NSAnyEventMask
      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]
      inMode:NSDefaultRunLoopMode dequeue:YES])) != nil) {

      [NSApp sendEvent:event];
    }
  }

  [pool release];

  return TRUE;
}
#endif

static void
gst_gl_context_cocoa_class_init (GstGLContextCocoaClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

#ifndef GNUSTEP
  NSAutoreleasePool* pool = nil;
#endif

  g_type_class_add_private (klass, sizeof (GstGLContextCocoaPrivate));

  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_destroy_context);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_create_context);
  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_get_gl_context);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_activate);
  context_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_cocoa_get_gl_platform);

#ifndef GNUSTEP
  pool = [[NSAutoreleasePool alloc] init];

  /* [NSApplication sharedApplication] will usually be
   * called in your application so it's not necessary
   * to do that the following. Except for debugging 
   * purpose like when using gst-launch.
   * So here we handle the two cases where the first
   * GstGLContext is either created in the main thread
   * or from another thread like a streaming thread
   */

  if ([NSThread isMainThread]) {
    /* In the main thread so just do the call now */
    
    /* The sharedApplication class method initializes
     * the display environment and connects your program
     * to the window server and the display server
     */

    /* TODO: so consider to create GstGLDisplayCocoa
     * in gst/gl/cocoa/gstgldisplay_cocoa.h/c
     */

    /* has to be called in the main thread */
    [NSApplication sharedApplication];

    GST_DEBUG ("NSApp initialized");
  } else {
    /* Not in the main thread, assume there is a
     * glib main loop running this is for debugging
     * purposes so that's ok to let us a chance
     */
    gboolean is_loop_running = FALSE;
    gint64 end_time = 0;

    g_mutex_init (&nsapp_lock);
    g_cond_init (&nsapp_cond);

    g_mutex_lock (&nsapp_lock);
    g_idle_add_full (G_PRIORITY_HIGH, gst_gl_window_cocoa_init_nsapp, NULL, NULL);
    end_time = g_get_monotonic_time () + 2 * 1000 * 1000;
    is_loop_running = g_cond_wait_until (&nsapp_cond, &nsapp_lock, end_time);
    g_mutex_unlock (&nsapp_lock);

    if (!is_loop_running) {
      GST_WARNING ("no mainloop running");
    }

    g_cond_clear (&nsapp_cond);
    g_mutex_clear (&nsapp_lock);
  }

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
    GstGLContext *other_context, GError **error)
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
  if (other_context)
    priv->external_gl_context = (NSOpenGLContext *) gst_gl_context_get_gl_context (other_context);
  else
    priv->external_gl_context = NULL;

#ifdef GNUSTEP
  GSRegisterCurrentThread();
#endif

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

  GST_DEBUG ("NSOpenGL context created: %"G_GUINTPTR_FORMAT, (guintptr) glContext);

  context_cocoa->priv->gl_context = glContext;

  [glContext setView:glView];

  [glView setOpenGLContext:glContext];

#else
  /* FIXME try to make context sharing work in GNUstep */
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

static void
gst_gl_context_cocoa_destroy_context (GstGLContext *context)
{
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

static GstGLPlatform
gst_gl_context_cocoa_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_CGL;
}
