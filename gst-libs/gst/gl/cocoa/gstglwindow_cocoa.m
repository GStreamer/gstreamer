/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it un der the terms of the GNU Library General Public
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

#include "gstgl_cocoa_private.h"

/* =============================================================*/
/*                                                              */
/*               GstGLNSWindow declaration                      */
/*                                                              */
/* =============================================================*/

@interface GstGLNSWindow: NSWindow {
  BOOL m_isClosed;
  GstGLWindowCocoa *m_cocoa;
}
- (id)initWithContentRect:(NSRect)contentRect
    styleMask: (unsigned int) styleMask
    backing: (NSBackingStoreType) bufferingType
    defer: (BOOL) flag screen: (NSScreen *) aScreen
    gstWin: (GstGLWindowCocoa *) window;
- (void) setClosed;
- (BOOL) isClosed;
- (BOOL) canBecomeMainWindow;
- (BOOL) canBecomeKeyWindow;
@end

/* =============================================================*/
/*                                                              */
/*                      GstGLWindow                             */
/*                                                              */
/* =============================================================*/

#ifndef GNUSTEP
static BOOL GSRegisterCurrentThread(void) { return TRUE; };
static void GSUnregisterCurrentThread(void) {};
#endif

#define GST_GL_WINDOW_COCOA_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_COCOA, GstGLWindowCocoaPrivate))

#define GST_CAT_DEFAULT gst_gl_window_cocoa_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_cocoa_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowCocoa, gst_gl_window_cocoa, GST_GL_TYPE_WINDOW, DEBUG_INIT);

static guintptr gst_gl_window_cocoa_get_window_handle (GstGLWindow * window);
static void gst_gl_window_cocoa_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_cocoa_draw (GstGLWindow * window, guint width, guint height);
static void gst_gl_window_cocoa_run (GstGLWindow * window);
static void gst_gl_window_cocoa_quit (GstGLWindow * window);
static void gst_gl_window_cocoa_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);

struct _GstGLWindowCocoaPrivate
{
  GstGLNSWindow *internal_win_id;
  NSView *external_view;
  gboolean visible;
  NSThread *thread;
  gboolean running;
};

static void
gst_gl_window_cocoa_class_init (GstGLWindowCocoaClass * klass)
{
  GstGLWindowClass *window_class;

  window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowCocoaPrivate));

  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_send_message_async);
}

static void
gst_gl_window_cocoa_init (GstGLWindowCocoa * window)
{
  window->priv = GST_GL_WINDOW_COCOA_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstGLWindowCocoa *
gst_gl_window_cocoa_new (void)
{
  GstGLWindowCocoa *window = g_object_new (GST_GL_TYPE_WINDOW_COCOA, NULL);

  return window;
}

gboolean
gst_gl_window_cocoa_create_window (GstGLWindowCocoa *window_cocoa)
{
  GstGLWindow *window = GST_GL_WINDOW (window_cocoa);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextCocoa *context_cocoa = GST_GL_CONTEXT_COCOA (context);
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;
  NSRect rect = context_cocoa->priv->rect;

  priv->internal_win_id =[[GstGLNSWindow alloc] initWithContentRect:rect styleMask: 
    (NSTitledWindowMask | NSClosableWindowMask |
    NSResizableWindowMask | NSMiniaturizableWindowMask)
    backing: NSBackingStoreBuffered defer: NO screen: nil gstWin: window_cocoa];

  GST_DEBUG ("NSWindow id: %"G_GUINTPTR_FORMAT, (guintptr) priv->internal_win_id);

  priv->thread = [NSThread currentThread];

  [NSApp setDelegate: priv->internal_win_id];

  gst_object_unref (context);

  return TRUE;
}

static guintptr
gst_gl_window_cocoa_get_window_handle (GstGLWindow *window)
{
  return (guintptr) GST_GL_WINDOW_COCOA (window)->priv->internal_win_id;
}

static void
gst_gl_window_cocoa_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;
  
  if (priv->internal_win_id) {
    GstGLContextCocoa *context = (GstGLContextCocoa *) gst_gl_window_get_context (window);
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] init:window_cocoa];

    GSRegisterCurrentThread();

    if (context) {
      if (context->priv->source_id) {
        g_source_remove (context->priv->source_id);
        context->priv->source_id = 0;
      }
      gst_object_unref (context);
    }

    if (handle) {
      priv->external_view = (NSView *) handle;
      priv->visible = TRUE;
    } else {
      /* bring back our internal window */
      priv->external_view = 0;
      priv->visible = FALSE;
    }
   
    [app_thread_performer performSelectorOnMainThread:@selector(setWindow) 
        withObject:0 waitUntilDone:YES];

    [pool release];
  } else {
    /* not internal window yet so delay it to the next drawing */
    priv->external_view = (NSView*) handle;
    priv->visible = FALSE;
  }
}

/* Thread safe */
static void
gst_gl_window_cocoa_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  AppThreadPerformer* app_thread_performer;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  GSRegisterCurrentThread();

  app_thread_performer = [[AppThreadPerformer alloc] init:window_cocoa];

  /* useful when set_window_handle is called before
   * the internal NSWindow */
  if (priv->external_view && !priv->visible) {
    gst_gl_window_cocoa_set_window_handle (window, (guintptr) priv->external_view);
    priv->visible = TRUE;
  }

  if (!priv->external_view && !priv->visible) {
    static gint x = 0;
    static gint y = 0;

    NSRect mainRect = [[NSScreen mainScreen] visibleFrame];
    NSRect windowRect = [priv->internal_win_id frame];

    GST_DEBUG ("main screen rect: %d %d %d %d\n", (int) mainRect.origin.x,
        (int) mainRect.origin.y, (int) mainRect.size.width,
        (int) mainRect.size.height);

    windowRect.origin.x += x;
    windowRect.origin.y += mainRect.size.height > y ? (mainRect.size.height - y) * 0.5 : y;
    windowRect.size.width = width;
    windowRect.size.height = height;

    GST_DEBUG ("window rect: %d %d %d %d\n", (int) windowRect.origin.x,
        (int) windowRect.origin.y, (int) windowRect.size.width,
        (int) windowRect.size.height);

    x += 20;
    y += 20;

#ifndef GNUSTEP
    [priv->internal_win_id setFrame:windowRect display:NO];
    GST_DEBUG ("make the window available\n");
    [priv->internal_win_id makeMainWindow];
#endif
    [app_thread_performer performSelector:@selector(orderFront)
        onThread:priv->thread withObject:nil waitUntilDone:YES];

    /*[priv->internal_win_id setViewsNeedDisplay:YES]; */
    priv->visible = TRUE;
  }

  [app_thread_performer performSelector:@selector(updateWindow)
      onThread:priv->thread withObject:nil waitUntilDone:YES];

  [pool release];
}

static void
gst_gl_window_cocoa_run (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSRunLoop *run_loop = [NSRunLoop currentRunLoop];

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  [run_loop addPort:[NSPort port] forMode:NSDefaultRunLoopMode];

  GST_DEBUG ("begin loop\n");

  if (priv->internal_win_id != nil) {
    priv->running = TRUE;
    while (priv->running)
      [run_loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];

    [priv->internal_win_id release];
    priv->internal_win_id = nil;
  }

  [pool release];

  GST_DEBUG ("end loop\n");
}

/* Thread safe */
static void
gst_gl_window_cocoa_quit (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  if (window) {
    if (GSRegisterCurrentThread() || 1) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      
      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc]
          initWithAll:window_cocoa callback:NULL userData:NULL];
      [app_thread_performer performSelector:@selector(stopApp)
          onThread:priv->thread withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      GST_DEBUG ("failed to register current thread, application thread is lost\n");
  }
}

/* Thread safe */
static void
gst_gl_window_cocoa_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  GSRegisterCurrentThread ();

  if (window) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    /* performSelector is not re-entrant so do it manually */
    if (G_UNLIKELY ([NSThread currentThread] == priv->thread)) {
      if (callback)
        callback (data);
    } else {
      AppThreadPerformer* app_thread_performer =
          [[AppThreadPerformer alloc] initWithAll:window_cocoa
              callback:callback userData:data];

      [app_thread_performer performSelector:@selector(sendToApp)
          onThread:priv->thread withObject:nil waitUntilDone:NO];
      
      [pool release];
    }
  }
}

/* =============================================================*/
/*                                                              */
/*                    GstGLNSWindow implementation              */
/*                                                              */
/* =============================================================*/

@implementation GstGLNSWindow

- (id) initWithContentRect: (NSRect) contentRect
        styleMask: (unsigned int) styleMask
    backing: (NSBackingStoreType) bufferingType
    defer: (BOOL) flag screen: (NSScreen *) aScreen
    gstWin: (GstGLWindowCocoa *) cocoa {

  m_isClosed = NO;
  m_cocoa = cocoa;

  self = [super initWithContentRect: contentRect
        styleMask: styleMask backing: bufferingType
        defer: flag screen:aScreen];

  [self setReleasedWhenClosed:NO];

  GST_DEBUG ("initializing GstGLNSWindow\n");

  [self setTitle:@"OpenGL renderer"];

  [self setBackgroundColor:[NSColor clearColor]];

  [self orderOut:m_cocoa->priv->internal_win_id];

  if (m_cocoa->priv->external_view) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSView *view = [m_cocoa->priv->internal_win_id contentView];

    [m_cocoa->priv->external_view addSubview: view];
    [view setFrame: [m_cocoa->priv->external_view bounds]];
    [view setAutoresizingMask: NSViewWidthSizable|NSViewHeightSizable];

    [pool release];
  }

  return self;
}

- (void) setClosed {
  m_isClosed = YES;
}

- (BOOL) isClosed {
  return m_isClosed;
}

- (BOOL) canBecomeMainWindow {
  return YES;
}

- (BOOL) canBecomeKeyWindow {
  return YES;
}

/* Called in the main thread which is never the gl thread */
- (BOOL) windowShouldClose:(id)sender {
    
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] 
    init:m_cocoa];
    
  GST_DEBUG ("user clicked the close button\n");
  
  [app_thread_performer performSelector:@selector(closeWindow) onThread:m_cocoa->priv->thread
    withObject:nil waitUntilDone:YES];
  
  [pool release];
  
  return YES;
}

- (void) applicationDidFinishLaunching: (NSNotification *) not {
}

- (void) applicationWillFinishLaunching: (NSNotification *) not {
}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
  /* the application is manually stopped by calling stopApp on the AppThreadPerformer */
  return NO;
}

- (void) applicationWillTerminate:(NSNotification *)aNotification {
#ifdef GNUSTEP
  /* fixes segfault with gst-launch-1.0 -e ... and sending SIGINT (Ctrl-C)
   * which causes GNUstep to run a signal handler in the main thread.
   * However that thread has never been 'registered' with GNUstep so
   * the autorelease magic of objective-c causes a segfault from accessing
   * a null NSThread object somewhere deep in GNUstep.
   *
   * I put it here because this is the first time we can register the thread.
   */
  GSRegisterCurrentThread();
#endif
}

@end


/* =============================================================*/
/*                                                              */
/*                GstGLNSOpenGLView implementation              */
/*                                                              */
/* =============================================================*/

@implementation GstGLNSOpenGLView

- (id)initWithFrame:(GstGLWindowCocoa *)window rect:(NSRect)contentRect pixelFormat:(NSOpenGLPixelFormat *)fmt {

  self = [super initWithFrame: contentRect pixelFormat: fmt];

  m_cocoa = window;

#ifndef GNUSTEP
  [self setWantsLayer:NO];
#endif

  return self;
}

- (void)reshape {
  GstGLWindow *window;

  window = GST_GL_WINDOW (m_cocoa);

  if (window->resize) {

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSRect bounds = [self bounds];
    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc]
      initWithSize:m_cocoa callback:window->resize userData:window->resize_data
      toSize:bounds.size];

    [app_thread_performer performSelector:@selector(resizeWindow) onThread:m_cocoa->priv->thread
      withObject:nil waitUntilDone:YES];

    [pool release];
  }
}

- (void) update {
}

@end

/* =============================================================*/
/*                                                              */
/*               AppThreadPerformer implementation              */
/*                                                              */
/* =============================================================*/

@implementation AppThreadPerformer

- (id) init: (GstGLWindowCocoa *) window {
  m_cocoa = window;
  m_callback = NULL;
  m_callback2 = NULL;
  m_data = NULL;
  m_width = 0;
  m_height = 0;
  return self;
}

- (id) initWithCallback:(GstGLWindowCocoa *)window callback:(GstGLWindowCB)callback userData:(gpointer)data {
  m_cocoa = window;
  m_callback = callback;
  m_callback2 = NULL;
  m_data = data;
  m_width = 0;
  m_height = 0;
  return self;
}

- (id) initWithSize: (GstGLWindowCocoa *) window
    callback:(GstGLWindowResizeCB)callback userData:(gpointer)data
  toSize:(NSSize)size {
  m_cocoa = window;
  m_callback = NULL;
  m_callback2 = callback;
  m_data = data;
  m_width = size.width;
  m_height = size.height;
  return self;
}

- (id) initWithAll: (GstGLWindowCocoa *) window
    callback:(GstGLWindowCB) callback userData: (gpointer) data {
  m_cocoa = window;
  m_callback = callback;
  m_callback2 = NULL;
  m_data = data;
  m_width = 0;
  m_height = 0;
  return self;
}

- (void) updateWindow {
  if (m_cocoa->priv->running) {

    if (![m_cocoa->priv->internal_win_id isClosed]) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

      /* draw opengl scene in the back buffer */
      GST_GL_WINDOW (m_cocoa)->draw (GST_GL_WINDOW (m_cocoa)->draw_data);
      /* Copy the back buffer to the front buffer */
      [[[m_cocoa->priv->internal_win_id contentView] openGLContext] flushBuffer];

      [pool release];
    }
  }
}

- (void) resizeWindow {
  if (m_cocoa->priv->running && ![m_cocoa->priv->internal_win_id isClosed]) {
    m_callback2 (m_data, m_width, m_height);
    [[[m_cocoa->priv->internal_win_id contentView] openGLContext] update];
      GST_GL_WINDOW (m_cocoa)->draw (GST_GL_WINDOW (m_cocoa)->draw_data);
    [[[m_cocoa->priv->internal_win_id contentView] openGLContext] flushBuffer];
  }
}

- (void) sendToApp {
  if (m_callback)
    m_callback (m_data);
}

- (void) setWindow {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSView *view = [m_cocoa->priv->internal_win_id contentView];

  [m_cocoa->priv->internal_win_id orderOut:m_cocoa->priv->internal_win_id];

  [m_cocoa->priv->external_view addSubview: view];

  [view setFrame: [m_cocoa->priv->external_view bounds]];
  [view setAutoresizingMask: NSViewWidthSizable|NSViewHeightSizable];

  [pool release];
}

- (void) stopApp {
#ifdef GNUSTEP
  NSAutoreleasePool *pool = nil;
#endif

  m_cocoa->priv->running = FALSE;
  if (m_callback)
    m_callback (m_data);

#ifdef GNUSTEP
  pool = [[NSAutoreleasePool alloc] init];
  if ([NSApp isRunning])
    [NSApp stop:self];
  [pool release];
#endif
}

- (void) closeWindow {
  GstGLWindow *window;

  window = GST_GL_WINDOW (m_cocoa);

  [m_cocoa->priv->internal_win_id setClosed];
  if (window->close) {
    window->close (window->close_data);
  }
}

- (void) orderFront {
  [m_cocoa->priv->internal_win_id orderFrontRegardless];
}

@end
