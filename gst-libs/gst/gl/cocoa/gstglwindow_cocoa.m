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

#include "gstglwindow_cocoa.h"

#include <Cocoa/Cocoa.h>


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
/*                  GstGLNSOpenGLView declaration               */
/*                                                              */
/* =============================================================*/

@interface GstGLNSOpenGLView: NSOpenGLView {
  GstGLWindowCocoa *m_cocoa;
  gint m_resizeCount;
}
- (id) initWithFrame:(GstGLWindowCocoa *)window rect:(NSRect)contentRect
    pixelFormat:(NSOpenGLPixelFormat *)fmt;
@end


/* =============================================================*/
/*                                                              */
/*                  AppThreadPerformer declaration              */
/*                                                              */
/* =============================================================*/

/* Perform actions in the Application thread */
@interface AppThreadPerformer : NSObject {
  GstGLWindowCocoa *m_cocoa;
  GstGLWindowCB m_callback;
  GstGLWindowResizeCB m_callback2;
  gpointer m_data;
  gint m_width;
  gint m_height;
}
- (id) init: (GstGLWindowCocoa *)window;
- (id) initWithCallback:(GstGLWindowCocoa *)window callback:(GstGLWindowCB)callback userData:(gpointer) data;
- (id) initWithSize: (GstGLWindowCocoa *)window callback:(GstGLWindowResizeCB)callback userData:(gpointer)data toSize:(NSSize)size;
- (id) initWithAll: (GstGLWindowCocoa *)window callback:(GstGLWindowCB)callback userData:(gpointer) data;
- (void) updateWindow;
- (void) sendToApp;
- (void) setWindow;
- (void) stopApp;
- (void) closeWindow;
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

guintptr gst_gl_window_cocoa_get_gl_context (GstGLWindow * window);
gboolean gst_gl_window_cocoa_activate (GstGLWindow * window, gboolean activate);
void gst_gl_window_cocoa_set_window_handle (GstGLWindow * window,
    guintptr handle);
void gst_gl_window_cocoa_draw_unlocked (GstGLWindow * window, guint width,
    guint height);
void gst_gl_window_cocoa_draw (GstGLWindow * window, guint width, guint height);
void gst_gl_window_cocoa_run (GstGLWindow * window);
void gst_gl_window_cocoa_quit (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data);
void gst_gl_window_cocoa_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);

struct _GstGLWindowCocoaPrivate
{
  GstGLNSWindow *internal_win_id;
  NSOpenGLContext *gl_context;
  NSOpenGLContext *external_gl_context;
  gboolean visible;
  NSWindow *parent;
  NSThread *thread;
  gboolean running;
  guint source_id;
};

gboolean
gst_gl_window_cocoa_nsapp_iteration (gpointer data)
{  
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  
  NSEvent *event = nil;
  
  if ([NSThread isMainThread]) {
    
    while ((event = ([NSApp nextEventMatchingMask:NSAnyEventMask
      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]
      inMode:NSDefaultRunLoopMode dequeue:YES])) != nil)

      [NSApp sendEvent:event];
  }
  
  [pool release];
  
  return TRUE;
}

static void
gst_gl_window_cocoa_class_init (GstGLWindowCocoaClass * klass)
{
  GstGLWindowClass *window_class;

  window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowCocoaPrivate));

  window_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_get_gl_context);
  window_class->activate = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_activate);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_quit);
  window_class->send_message =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_send_message);

#ifndef GNUSTEP
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  
  [pool release];
#endif
}

static void
gst_gl_window_cocoa_init (GstGLWindowCocoa * window)
{
  window->priv = GST_GL_WINDOW_COCOA_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstGLWindowCocoa *
gst_gl_window_cocoa_new (GstGLRendererAPI render_api, guintptr external_gl_context)
{
  GstGLWindowCocoa *window = g_object_new (GST_GL_TYPE_WINDOW_COCOA, NULL);
  GstGLWindowCocoaPrivate *priv = window->priv;
  NSRect rect;
  NSAutoreleasePool *pool;

  priv->internal_win_id = nil;
  priv->gl_context = nil;
  priv->external_gl_context = (NSOpenGLContext *) external_gl_context;
  priv->visible = FALSE;
  priv->parent = nil;
  priv->thread = nil;
  priv->running = TRUE;

  gst_gl_window_set_need_lock (GST_GL_WINDOW (window), FALSE);

  GSRegisterCurrentThread();
  
  pool = [[NSAutoreleasePool alloc] init];
  
#ifdef GNUSTEP
  [NSApplication sharedApplication];
#endif

  rect.origin.x = 0;
  rect.origin.y = 0;
  rect.size.width = 320;
  rect.size.height = 240;

  priv->internal_win_id =[[GstGLNSWindow alloc] initWithContentRect:rect styleMask: 
    (NSTitledWindowMask | NSClosableWindowMask |
    NSResizableWindowMask | NSMiniaturizableWindowMask)
    backing: NSBackingStoreBuffered defer: NO screen: nil gstWin: window];

  GST_DEBUG ("NSWindow id: %lud\n", (gulong) priv->internal_win_id);

  priv->thread = [NSThread currentThread];

  [NSApp setDelegate: priv->internal_win_id];

  [pool release];
  
#ifndef GNUSTEP
  priv->source_id = g_timeout_add_seconds (1, gst_gl_window_cocoa_nsapp_iteration, NULL);
#endif

  return window;
}

guintptr
gst_gl_window_cocoa_get_gl_context (GstGLWindow * window)
{
  return (guintptr) GST_GL_WINDOW_COCOA (window)->priv->gl_context;
}

gboolean
gst_gl_window_cocoa_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  if (activate)
    [priv->gl_context makeCurrentContext];
#if 0
  else
    /* FIXME */
    [priv->gl_context clearCurrentContext];
#endif
  return TRUE;
}

void
gst_gl_window_cocoa_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  g_source_remove (priv->source_id);

  if (GSRegisterCurrentThread()) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] init:window_cocoa];
    priv->parent = (NSWindow*) handle;
    [app_thread_performer performSelectorOnMainThread:@selector(setWindow) 
        withObject:0 waitUntilDone:YES];

    [pool release];

    GSUnregisterCurrentThread();
  }
  else
    GST_DEBUG ("failed to register current thread, cannot set external window id\n");
}

/* Thread safe */
void
gst_gl_window_cocoa_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  if (GSRegisterCurrentThread()) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] init:window_cocoa];
    [app_thread_performer performSelector:@selector(updateWindow) 
      onThread:priv->thread withObject:nil waitUntilDone:YES];
    
    if (!priv->parent && !priv->visible) {
      static gint x = 0;
      static gint y = 0;

      NSRect mainRect = [[NSScreen mainScreen] visibleFrame];
      NSRect windowRect = [priv->internal_win_id frame];

      GST_DEBUG ("main screen rect: %d %d %d %d\n", (int) mainRect.origin.x, (int) mainRect.origin.y, 
        (int) mainRect.size.width, (int) mainRect.size.height);

      windowRect.origin.x += x;
      windowRect.origin.y += mainRect.size.height > y ? (mainRect.size.height - y) * 0.5 : y;
      windowRect.size.width = width;
      windowRect.size.height = height;

      GST_DEBUG ("window rect: %d %d %d %d\n", (int) windowRect.origin.x, (int) windowRect.origin.y, 
        (int) windowRect.size.width, (int) windowRect.size.height);

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

    [pool release];

    GSUnregisterCurrentThread();
  }
  else
    GST_DEBUG ("failed to register current thread, cannot draw\n");
}

void
gst_gl_window_cocoa_run (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#ifndef GNUSTEP
  NSRunLoop *run_loop = [NSRunLoop currentRunLoop];
#endif

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  GST_DEBUG ("begin loop\n");
  
  if (priv->internal_win_id != nil) {
#ifndef GNUSTEP
    while (priv->running)
      [run_loop runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
#else
    [NSApp run];
#endif

    [priv->internal_win_id release];
    priv->internal_win_id = nil;
  }

  [pool release];

  GST_DEBUG ("end loop\n");
}

/* Thread safe */
void
gst_gl_window_cocoa_quit (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  if (window) {
    if (GSRegisterCurrentThread() || 1) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      
      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc]
          initWithAll:window_cocoa callback:callback userData:data];
        
      [app_thread_performer performSelector:@selector(stopApp) onThread:priv->thread 
        withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      GST_DEBUG ("failed to register current thread, application thread is lost\n");
  }
}

/* Thread safe */
void
gst_gl_window_cocoa_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLWindowCocoaPrivate *priv;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  priv = window_cocoa->priv;

  if (window) {
    if (GSRegisterCurrentThread()) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc]
          initWithAll:window_cocoa callback:callback userData:data];
      
      [app_thread_performer performSelector:@selector(sendToApp) onThread:priv->thread
        withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      GST_DEBUG ("failed to register current thread, cannot send message\n");
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

  GstGLNSOpenGLView *glView = nil;
  NSOpenGLPixelFormat *fmt = nil;
  NSOpenGLContext *glContext = nil;
  NSOpenGLPixelFormatAttribute attribs[] = {
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAAccumSize, 32,
    0
  };

  m_isClosed = NO;
  m_cocoa = cocoa;

  self = [super initWithContentRect: contentRect
        styleMask: styleMask backing: bufferingType
        defer: flag screen:aScreen];

  [self setReleasedWhenClosed:NO];

  GST_DEBUG ("initializing GstGLNSWindow\n");

  glView = [GstGLNSOpenGLView alloc];

  fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];

  if (!fmt) {
    GST_WARNING ("cannot create NSOpenGLPixelFormat");
    return nil;
  }

  glView = [glView initWithFrame:m_cocoa rect:contentRect pixelFormat:fmt];
  
  [self setContentView:glView];
  
#ifndef GNUSTEP
  glContext = [[NSOpenGLContext alloc] initWithFormat:fmt 
    shareContext:m_cocoa->priv->external_gl_context];

  GST_DEBUG ("NSOpenGL context created: %lud\n", (gulong) glContext);

  priv->gl_context = glContext;
  
  [glView setOpenGLContext:glContext];
#else
  glContext = [glView openGLContext];
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
    (gint) contentRect.size.width, (gint) contentRect.size.height);

  [self setTitle:@"OpenGL renderer"];
  
  [self setBackgroundColor:[NSColor clearColor]];
  
  [self orderOut:m_cocoa->priv->internal_win_id];

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
  m_resizeCount = 0;
  
#ifndef GNUSTEP
  [self setWantsLayer:NO];
#endif
  
  return self;    
}

- (void)reshape {
  GstGLWindow *window;

  window = GST_GL_WINDOW (m_cocoa);

  if (m_resizeCount % 5 == 0) {
    m_resizeCount = 0;
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
  m_resizeCount++;
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
  if (m_cocoa->priv->running && ![m_cocoa->priv->internal_win_id isClosed])
    m_callback (m_data);
}

- (void) setWindow {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSWindow *window = m_cocoa->priv->parent;
  
  [m_cocoa->priv->internal_win_id orderOut:m_cocoa->priv->internal_win_id];
  
  [window setContentView: [m_cocoa->priv->internal_win_id contentView]];

  [pool release];
}

- (void) stopApp {
  NSAutoreleasePool *pool;

  m_cocoa->priv->running = FALSE;
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
  [m_cocoa->priv->internal_win_id orderFront:m_cocoa->priv->internal_win_id];
}

@end
