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

GST_DEBUG_CATEGORY_STATIC (gst_gl_context_cocoa_debug);
#define GST_CAT_DEFAULT gst_gl_context_cocoa_debug

G_DEFINE_TYPE_WITH_CODE (GstGLContextCocoa, gst_gl_context_cocoa,
    GST_GL_TYPE_CONTEXT, GST_DEBUG_CATEGORY_INIT (gst_gl_context_cocoa_debug, "glcontext_cocoa", 0, "Cocoa GL Context"); );

/* Define this if the GLib patch from
 * https://bugzilla.gnome.org/show_bug.cgi?id=741450
 * is used
 */
#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION

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
      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
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
#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION
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

#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION
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
    GMainContext *context;
    gboolean is_loop_running = FALSE;
    gint64 end_time = 0;

    context = g_main_context_default ();

    if (g_main_context_is_owner (context)) {
      /* At the thread running the default GLib main context but
       * not the Cocoa main thread
       * We can't do anything here
       */
    } else if (g_main_context_acquire (context)) {
      /* No main loop running on the default main context,
       * we can't do anything here */
      g_main_context_release (context);
    } else {
      /* Main loop running on the default main context but it
       * is not running in this thread */
      g_mutex_lock (&nsapp_lock);
      g_idle_add_full (G_PRIORITY_HIGH, gst_gl_window_cocoa_init_nsapp, NULL, NULL);
      end_time = g_get_monotonic_time () + 500 * 1000;
      is_loop_running = g_cond_wait_until (&nsapp_cond, &nsapp_lock, end_time);
      g_mutex_unlock (&nsapp_lock);

      if (!is_loop_running) {
        GST_WARNING ("no mainloop running");
      }
    }
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

struct pixel_attr
{
  CGLPixelFormatAttribute attr;
  const gchar *attr_name;
};

static struct pixel_attr pixel_attrs[] = {
  {kCGLPFAAllRenderers, "All Renderers"},
  {kCGLPFADoubleBuffer, "Double Buffered"},
  {kCGLPFAStereo, "Stereo"},
  {kCGLPFAAuxBuffers, "Aux Buffers"},
  {kCGLPFAColorSize, "Color Size"},
  {kCGLPFAAlphaSize, "Alpha Size"},
  {kCGLPFADepthSize, "Depth Size"},
  {kCGLPFAStencilSize, "Stencil Size"},
  {kCGLPFAAccumSize, "Accum Size"},
  {kCGLPFAMinimumPolicy, "Minimum Policy"},
  {kCGLPFAMaximumPolicy, "Maximum Policy"},
//  {kCGLPFAOffScreen, "Off Screen"},
//  {kCGLPFAFullScreen, "Full Screen"},
  {kCGLPFASampleBuffers, "Sample Buffers"},
  {kCGLPFASamples, "Samples"},
  {kCGLPFAAuxDepthStencil, "Aux Depth Stencil"},
  {kCGLPFAColorFloat, "Color Float"},
  {kCGLPFAMultisample, "Multisample"},
  {kCGLPFASupersample, "Supersample"},
  {kCGLPFARendererID, "Renderer ID"},
  {kCGLPFASingleRenderer, "Single Renderer"},
  {kCGLPFANoRecovery, "No Recovery"},
  {kCGLPFAAccelerated, "Accelerated"},
  {kCGLPFAClosestPolicy, "Closest Policy"},
//  {kCGLPFARobust, "Robust"},
  {kCGLPFABackingStore, "Backing Store"},
//  {kCGLPFAMPSafe, "MP Safe"},
  {kCGLPFAWindow, "Window"},
//  {kCGLPFAMultiScreen, "Multi Screen"},
  {kCGLPFACompliant, "Compliant"},
  {kCGLPFADisplayMask, "Display Mask"},
//  {kCGLPFAPBuffer, "PBuffer"},
  {kCGLPFARemotePBuffer, "Remote PBuffer"},
  {kCGLPFAAllowOfflineRenderers, "Allow Offline Renderers"},
  {kCGLPFAAcceleratedCompute, "Accelerated Compute"},
  {kCGLPFAOpenGLProfile, "OpenGL Profile"},
  {kCGLPFAVirtualScreenCount, "Virtual Screen Count"},
};

void
gst_gl_context_cocoa_dump_pixel_format (CGLPixelFormatObj fmt)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (pixel_attrs); i++) {
    gint val;
    CGLError ret = CGLDescribePixelFormat (fmt, 0, pixel_attrs[i].attr, &val);

    if (ret != kCGLNoError) {
      GST_WARNING ("failed to get pixel format %p attribute %s", fmt, pixel_attrs[i].attr_name);
    } else {
      GST_DEBUG ("Pixel format %p attr %s = %i", fmt, pixel_attrs[i].attr_name,
          val);
    }
  }
}

CGLPixelFormatObj
gst_gl_context_cocoa_get_pixel_format (GstGLContextCocoa *context)
{
  return context->priv->pixel_format;
}

static gboolean
gst_gl_context_cocoa_create_context (GstGLContext *context, GstGLAPI gl_api,
    GstGLContext *other_context, GError **error)
{
  GstGLContextCocoa *context_cocoa = GST_GL_CONTEXT_COCOA (context);
  GstGLContextCocoaPrivate *priv = context_cocoa->priv;
  GstGLWindow *window = gst_gl_context_get_window (context);
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);
  const GLint swapInterval = 1;

#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION
  priv->source_id = g_timeout_add (200, gst_gl_window_cocoa_nsapp_iteration, NULL);
#endif

  priv->gl_context = nil;
  if (other_context)
    priv->external_gl_context = (CGLContextObj) gst_gl_context_get_gl_context (other_context);
  else
    priv->external_gl_context = NULL;

  dispatch_sync (dispatch_get_main_queue (), ^{
    NSAutoreleasePool *pool;
    CGLPixelFormatObj fmt = NULL;
    GstGLNSView *glView = NULL;
    GstGLCAOpenGLLayer *layer;
    CGLContextObj glContext;
    CGLPixelFormatAttribute attribs[] = {
      kCGLPFADoubleBuffer,
      kCGLPFAAccumSize, 32,
      0
    };
    CGLError ret;
    NSRect rect;
    NSWindow *window_handle;
    gint npix;

    pool = [[NSAutoreleasePool alloc] init];

    rect.origin.x = 0;
    rect.origin.y = 0;
    rect.size.width = 320;
    rect.size.height = 240;

    gst_gl_window_cocoa_create_window (window_cocoa, rect);
    window_handle = (NSWindow *) gst_gl_window_get_window_handle (window);

    if (priv->external_gl_context) {
      fmt = CGLGetPixelFormat (priv->external_gl_context);
    }

    if (!fmt) {
      ret = CGLChoosePixelFormat (attribs, &fmt, &npix);
      if (ret != kCGLNoError) {
        gst_object_unref (window);
        g_set_error (error, GST_GL_CONTEXT_ERROR,
            GST_GL_CONTEXT_ERROR_WRONG_CONFIG, "cannot choose a pixel format: %s", CGLErrorString (ret));
        return;
      }
    }

    gst_gl_context_cocoa_dump_pixel_format (fmt);

    ret = CGLCreateContext (fmt, priv->external_gl_context, &glContext);
    if (ret != kCGLNoError) {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
          "failed to create context: %s", CGLErrorString (ret));
      gst_object_unref (window);
      return;
    }

    context_cocoa->priv->pixel_format = fmt;
    context_cocoa->priv->gl_context = glContext;

    layer = [[GstGLCAOpenGLLayer alloc] initWithGstGLContext:context_cocoa];
    glView = [[GstGLNSView alloc] initWithFrameLayer:window_cocoa rect:rect layer:layer];

    [window_handle setContentView:glView];

    [pool release];
  });

  if (!context_cocoa->priv->gl_context) {
#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION
    g_source_remove (priv->source_id);
    priv->source_id = 0;
#endif
    return FALSE;
  }

  GST_INFO_OBJECT (context, "GL context created: %p", context_cocoa->priv->gl_context);

  CGLSetCurrentContext (context_cocoa->priv->gl_context);

  /* Back and front buffers are swapped only during the vertical retrace of the monitor.
   * Discarded if you configured your driver to Never-use-V-Sync.
   */
  CGLSetParameter (context_cocoa->priv->gl_context, kCGLCPSwapInterval, &swapInterval);

  gst_object_unref (window);

  return TRUE;
}

static void
gst_gl_context_cocoa_destroy_context (GstGLContext *context)
{
  GstGLContextCocoa *context_cocoa = GST_GL_CONTEXT_COCOA (context);
  GstGLContextCocoaPrivate *priv = context_cocoa->priv;

  /* FIXME: Need to release context and other things? */
  if (priv->source_id) {
    g_source_remove (priv->source_id);
    priv->source_id = 0;
  }
}

static guintptr
gst_gl_context_cocoa_get_gl_context (GstGLContext * context)
{
  return (guintptr) GST_GL_CONTEXT_COCOA (context)->priv->gl_context;
}

static gboolean
gst_gl_context_cocoa_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextCocoa *context_cocoa = GST_GL_CONTEXT_COCOA (context);
  gpointer context_handle = activate ? context_cocoa->priv->gl_context : NULL;

  return kCGLNoError == CGLSetCurrentContext (context_handle);
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

guintptr
gst_gl_context_cocoa_get_current_context (void)
{
  return (guintptr) CGLGetCurrentContext ();
}
