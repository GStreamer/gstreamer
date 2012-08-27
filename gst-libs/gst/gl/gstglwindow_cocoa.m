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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglwindow.h"

#import <Cocoa/Cocoa.h>


/* =============================================================*/
/*                                                              */
/*               GstGLNSWindow declaration                      */
/*                                                              */
/* =============================================================*/

@interface GstGLNSWindow: NSWindow {
  BOOL m_isClosed;
  GstGLWindowPrivate *m_priv;
}
- (id)initWithContentRect:(NSRect)contentRect
    styleMask: (unsigned int) styleMask
    backing: (NSBackingStoreType) bufferingType
    defer: (BOOL) flag screen: (NSScreen *) aScreen
    gstWin: (GstGLWindowPrivate *) priv;
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
  GstGLWindowPrivate *m_priv;
  gint m_resizeCount;
}
- (id) initWithFrame:(NSRect)contentRect pixelFormat:(NSOpenGLPixelFormat *)fmt 
  private: (GstGLWindowPrivate *) priv;
@end


/* =============================================================*/
/*                                                              */
/*                  AppThreadPerformer declaration              */
/*                                                              */
/* =============================================================*/

/* Perform actions in the Application thread */
@interface AppThreadPerformer : NSObject {
  GstGLWindowPrivate *m_priv;
  GstGLWindowCB m_callback;
  GstGLWindowCB2 m_callback2;
  gpointer m_data;
  gint m_width;
  gint m_height;
}
- (id) initWithPrivate: (GstGLWindowPrivate *) priv;
- (id) initWithCallback: (GstGLWindowCB) callback userData: (gpointer) data;
- (id) initWithSize: (GstGLWindowCB2) callback userData: (gpointer) data toSize: (NSSize) size private: (GstGLWindowPrivate *) priv;
- (id) initWithAll: (GstGLWindowCB) callback userData: (gpointer) data private: (GstGLWindowPrivate *) priv;
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

#define GST_GL_WINDOW_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

enum
{
  PROP_0
};

struct _GstGLWindowPrivate
{
  GstGLNSWindow *internal_win_id;
  NSOpenGLContext *gl_context;
  NSOpenGLContext *external_gl_context;
  GstGLWindowCB draw_cb;
  gpointer draw_data;
  GstGLWindowCB2 resize_cb;
  gpointer resize_data;
  GstGLWindowCB close_cb;
  gpointer close_data;
  gboolean visible;
  NSWindow *parent;
  NSThread *thread;
  gboolean running;
  guint source_id;
};

G_DEFINE_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GstGLWindow"

gboolean _gst_gl_window_debug = FALSE;

void gst_gl_window_init_platform ()
{
#ifndef GNUSTEP
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  
  [pool release];
#endif
}

/* Must be called in the gl thread */
static void
gst_gl_window_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

static void
gst_gl_window_log_handler (const gchar * domain, GLogLevelFlags flags,
    const gchar * message, gpointer user_data)
{
  if (_gst_gl_window_debug) {
    g_log_default_handler (domain, flags, message, user_data);
  }
}

gboolean
gst_gl_window_nsapp_iteration (gpointer data)
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
gst_gl_window_class_init (GstGLWindowClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  
  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  obj_class->finalize = gst_gl_window_finalize;
}

static void
gst_gl_window_init (GstGLWindow * window)
{
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);

  if (g_getenv ("GST_GL_WINDOW_DEBUG") != NULL)
    _gst_gl_window_debug = TRUE;

  g_log_set_handler ("GstGLWindow", G_LOG_LEVEL_DEBUG,
      gst_gl_window_log_handler, NULL);
}

/* Must be called in the gl thread */
GstGLWindow *
gst_gl_window_new (guintptr external_gl_context)
{
  GstGLWindow *window = g_object_new (GST_GL_TYPE_WINDOW, NULL);
  GstGLWindowPrivate *priv = window->priv;
  NSRect rect;

  NSAutoreleasePool *pool = nil;

  priv->internal_win_id = nil;
  priv->gl_context = nil;
  priv->external_gl_context = (NSOpenGLContext *) external_gl_context;
  priv->draw_cb = NULL;
  priv->draw_data = NULL;
  priv->resize_cb = NULL;
  priv->resize_data = NULL;
  priv->close_cb = NULL;
  priv->close_data = NULL;
  priv->visible = FALSE;
  priv->parent = nil;
  priv->thread = nil;
  priv->running = TRUE;

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
    backing: NSBackingStoreBuffered defer: NO screen: nil gstWin: priv];

  g_debug ("NSWindow id: %lud\n", (gulong) priv->internal_win_id);

  priv->thread = [NSThread currentThread];
  
  [NSApp setDelegate: priv->internal_win_id];

  [pool release];
  
#ifndef GNUSTEP
  priv->source_id = g_timeout_add_seconds (1, gst_gl_window_nsapp_iteration, NULL);
#endif

  return window;
}

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error");
}

guintptr
gst_gl_window_get_internal_gl_context (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;
  return (guintptr) priv->gl_context;
}

void
callback_activate_gl_context (GstGLWindowPrivate * priv)
{
  [priv->gl_context makeCurrentContext];
}

void
callback_inactivate_gl_context (GstGLWindowPrivate * priv)
{
  //FIXME: [priv->gl_context clearCurrentContext];
}

void
gst_gl_window_activate_gl_context (GstGLWindow * window, gboolean activate)
{
  GstGLWindowPrivate *priv = window->priv;
  if (activate)
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (callback_activate_gl_context), priv);
  else
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (callback_inactivate_gl_context), priv);
}

void
gst_gl_window_set_external_window_id (GstGLWindow * window, guintptr id)
{
  GstGLWindowPrivate *priv = window->priv;
  
  g_source_remove (priv->source_id);

  if (GSRegisterCurrentThread()) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithPrivate:priv];
    priv->parent = (NSWindow*) id;
    [app_thread_performer performSelectorOnMainThread:@selector(setWindow) 
        withObject:0 waitUntilDone:YES];

    [pool release];

    GSUnregisterCurrentThread();
  }
  else
    g_debug ("failed to register current thread, cannot set external window id\n");
}

/* Must be called in the gl thread */
void
gst_gl_window_set_draw_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->draw_cb = callback;
  priv->draw_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_resize_callback (GstGLWindow * window,
    GstGLWindowCB2 callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->resize_cb = callback;
  priv->resize_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_close_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->close_cb = callback;
  priv->close_data = data;
}

void
gst_gl_window_draw_unlocked (GstGLWindow * window, gint width, gint height)
{
  gst_gl_window_draw (window, width, height);
}

/* Thread safe */
void
gst_gl_window_draw (GstGLWindow * window, gint width, gint height)
{
  GstGLWindowPrivate *priv = window->priv;

  if (GSRegisterCurrentThread()) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithPrivate:priv];
    [app_thread_performer performSelector:@selector(updateWindow) 
      onThread:priv->thread withObject:nil waitUntilDone:YES];
    
    if (!priv->parent && !priv->visible) {
      static gint x = 0;
      static gint y = 0;

      NSRect mainRect = [[NSScreen mainScreen] visibleFrame];
      NSRect windowRect = [priv->internal_win_id frame];

      g_debug ("main screen rect: %d %d %d %d\n", (int) mainRect.origin.x, (int) mainRect.origin.y, 
        (int) mainRect.size.width, (int) mainRect.size.height);

      windowRect.origin.x += x;
      windowRect.origin.y += mainRect.size.height > y ? (mainRect.size.height - y) * 0.5 : y;
      windowRect.size.width = width;
      windowRect.size.height = height;
      [priv->internal_win_id setFrame:windowRect display:NO];

      g_debug ("window rect: %d %d %d %d\n", (int) windowRect.origin.x, (int) windowRect.origin.y, 
        (int) windowRect.size.width, (int) windowRect.size.height);

      x += 20;
      y += 20;

#ifndef GNUSTEP
      g_debug ("make the window available\n");
      [priv->internal_win_id makeMainWindow];
#endif
      [priv->internal_win_id orderFront:priv->internal_win_id];
      //[priv->internal_win_id setViewsNeedDisplay:YES];
      priv->visible = TRUE;
    }

    [pool release];

    GSUnregisterCurrentThread();
  }
  else
    g_debug ("failed to register current thread, cannot draw\n");
}

void
gst_gl_window_run_loop (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#ifndef GNUSTEP
  NSRunLoop *run_loop = [NSRunLoop currentRunLoop];
#endif

  g_debug ("begin loop\n");
  
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

  g_debug ("end loop\n");
}

/* Thread safe */
void
gst_gl_window_quit_loop (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  
  if (window) {
    if (GSRegisterCurrentThread() || 1) {
      GstGLWindowPrivate *priv = window->priv;
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      
      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithAll:callback 
        userData:data private:priv];
        
      [app_thread_performer performSelector:@selector(stopApp) onThread:priv->thread 
        withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      g_debug ("failed to register current thread, application thread is lost\n");
  }
}

/* Thread safe */
void
gst_gl_window_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  if (window) {
    GstGLWindowPrivate *priv = window->priv;
    if (GSRegisterCurrentThread()) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithAll:callback 
        userData:data private:priv];
      
      [app_thread_performer performSelector:@selector(sendToApp) onThread:priv->thread
        withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      g_debug ("failed to register current thread, cannot send message\n");
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
    gstWin: (GstGLWindowPrivate *) priv {

  GstGLNSOpenGLView *glView = nil;
  NSOpenGLPixelFormat *fmt = nil;
  NSOpenGLContext *glContext = nil;
  NSOpenGLPixelFormatAttribute attribs[] = {
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFADepthSize, 32,
    0
  };

  m_isClosed = NO;
  m_priv = priv;

  self = [super initWithContentRect: contentRect
        styleMask: styleMask backing: bufferingType
        defer: flag screen:aScreen];

  [self setReleasedWhenClosed:NO];

  g_debug ("initializing GstGLNSWindow\n");

  glView = [GstGLNSOpenGLView alloc];

  fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];

  if (!fmt) {
    g_warning ("cannot create NSOpenGLPixelFormat");
    return nil;
  }

  glView = [glView initWithFrame:contentRect pixelFormat:fmt private: m_priv];
  
  [self setContentView:glView];
  
#ifndef GNUSTEP
  glContext = [[NSOpenGLContext alloc] initWithFormat:fmt 
    shareContext:m_priv->external_gl_context];

  g_debug ("NSOpenGL context created: %lud\n", (gulong) glContext);

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
     g_debug ("your back-end does not implement NSOpenglContext::setValues\n");
  }
  NS_ENDHANDLER

  g_debug ("opengl GstGLNSWindow initialized: %d x %d\n",
    (gint) contentRect.size.width, (gint) contentRect.size.height);

  [self setTitle:@"OpenGL renderer"];
  
  [self setBackgroundColor:[NSColor clearColor]];
  
  [self orderOut:m_priv->internal_win_id];

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
    initWithPrivate:m_priv];
    
  g_debug ("user clicked the close button\n");
  
  [app_thread_performer performSelector:@selector(closeWindow) onThread:m_priv->thread
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
}

@end


/* =============================================================*/
/*                                                              */
/*                GstGLNSOpenGLView implementation              */
/*                                                              */
/* =============================================================*/

@implementation GstGLNSOpenGLView

- (id)initWithFrame:(NSRect)contentRect pixelFormat:(NSOpenGLPixelFormat *)fmt private: (GstGLWindowPrivate *) priv  {
  
  self = [super initWithFrame: contentRect pixelFormat: fmt];
  
  m_priv = priv;
  m_resizeCount = 0;
  
#ifndef GNUSTEP
  [self setWantsLayer:NO];
#endif
  
  return self;    
}

- (void)reshape {

  if (m_resizeCount % 5 == 0) {
    m_resizeCount = 0;
    if (m_priv->resize_cb) {
      
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      NSRect bounds = [self bounds];
      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc]
        initWithSize:m_priv->resize_cb userData:m_priv->resize_data 
        toSize:bounds.size private:m_priv];
    
      [app_thread_performer performSelector:@selector(resizeWindow) onThread:m_priv->thread 
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

- (id) initWithPrivate: (GstGLWindowPrivate *) priv {
  m_priv = priv;
  m_callback = NULL;
  m_callback2 = NULL;
  m_data = NULL;
  m_width = 0;
  m_height = 0;
  return self;
}

- (id) initWithCallback: (GstGLWindowCB) callback userData: (gpointer) data {
  m_priv = NULL;
  m_callback = callback;
  m_callback2 = NULL;
  m_data = data;
  m_width = 0;
  m_height = 0;
  return self;
}

- (id) initWithSize: (GstGLWindowCB2) callback userData: (gpointer) data 
  toSize: (NSSize) size private: (GstGLWindowPrivate *) priv  {
  m_priv = priv;
  m_callback = NULL;
  m_callback2 = callback;
  m_data = data;
  m_width = size.width;
  m_height = size.height;
  return self;
}

- (id) initWithAll: (GstGLWindowCB) callback userData: (gpointer) data
  private: (GstGLWindowPrivate *) priv {
  m_priv = priv;
  m_callback = callback;
  m_callback2 = NULL;
  m_data = data;
  m_width = 0;
  m_height = 0;
  return self;
}

- (void) updateWindow {
  if (m_priv->running) {

    if (![m_priv->internal_win_id isClosed]) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

      /* draw opengl scene in the back buffer */
      m_priv->draw_cb (m_priv->draw_data);
      /* Copy the back buffer to the front buffer */
      [[[m_priv->internal_win_id contentView] openGLContext] flushBuffer];

      [pool release];
    }
  }
}

- (void) resizeWindow {
  if (m_priv->running && ![m_priv->internal_win_id isClosed]) {
    m_callback2 (m_data, m_width, m_height);
    [[[m_priv->internal_win_id contentView] openGLContext] update];
    m_priv->draw_cb (m_priv->draw_data);
    [[[m_priv->internal_win_id contentView] openGLContext] flushBuffer];
  }
}

- (void) sendToApp {
  if (m_priv->running && ![m_priv->internal_win_id isClosed])
    m_callback (m_data);
}

- (void) setWindow {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSWindow *window = m_priv->parent;
  
  [m_priv->internal_win_id orderOut:m_priv->internal_win_id];
  
  [window setContentView: [m_priv->internal_win_id contentView]];

  [pool release];
}

- (void) stopApp {
  m_priv->running = FALSE;
  m_callback (m_data);
 
 #ifdef GNUSTEP
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  if ([NSApp isRunning])
    [NSApp stop:self];
  [pool release];
#endif
}

- (void) closeWindow {
  [m_priv->internal_win_id setClosed];
  if (m_priv->close_cb) {
    m_priv->close_cb (m_priv->close_data);
  }
  m_priv->draw_cb = NULL;
  m_priv->draw_data = NULL;
  m_priv->resize_cb = NULL;
  m_priv->resize_data = NULL;
  m_priv->close_cb = NULL;
  m_priv->close_data = NULL;
}

@end
