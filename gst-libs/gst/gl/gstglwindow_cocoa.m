/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglwindow.h"

#import <Cocoa/Cocoa.h>


/* ============================================================= */
/*                                                               */
/*              GstGLNSWindow declaration                        */
/*                                                               */
/* ============================================================= */

@interface GstGLNSWindow: NSWindow {
  BOOL m_isClosed;
  GstGLWindowPrivate *m_priv;
}
- (id)initWithContentRect:(NSRect)contentRect
		styleMask: (unsigned int) styleMask
		backing: (NSBackingStoreType) bufferingType
		defer: (BOOL) flag screen: (NSScreen *) aScreen
    gstWin: (GstGLWindowPrivate *) priv;
@end


/* ============================================================= */
/*                                                               */
/*               AppThreadPerformer declaration                  */
/*                                                               */
/* ============================================================= */

/* Perform actions in the Application thread */
@interface AppThreadPerformer : NSObject {
  GstGLWindowPrivate *m_priv;
  GstGLWindowCB m_callback;
  gpointer m_data;
}
- (id) initWithPrivate : (GstGLWindowPrivate *) priv;
- (id) initWithCallback : (GstGLWindowCB) callback userData: (gpointer) data;
- (id) initWithAll: (GstGLWindowCB) callback userData: (gpointer) data private: (GstGLWindowPrivate *) priv;
- (void) updateWindow;
- (void) sendToApp;
- (void) setWindow: (NSWindow *) window;
- (void) stopApp;
@end


/* ============================================================= */
/*                                                               */
/*                         GstGLWindow                           */
/*                                                               */
/* ============================================================= */

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
  GstGLWindowCB draw_cb;
  gpointer draw_data;
  GstGLWindowCB2 resize_cb;
  gpointer resize_data;
  GstGLWindowCB close_cb;
  gpointer close_data;
  gboolean visible;
  NSWindow *parent;
};

G_DEFINE_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GstGLWindow"

gboolean _gst_gl_window_debug = FALSE;

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
gst_gl_window_new (gint width, gint height)
{
  GstGLWindow *window = g_object_new (GST_GL_TYPE_WINDOW, NULL);
  GstGLWindowPrivate *priv = window->priv;

  NSAutoreleasePool *pool = nil;
  NSRect rect;

  static gint x = 0;
  static gint y = 0;

  x += 20;
  y += 20;

  priv->internal_win_id = nil;
  priv->draw_cb = NULL;
  priv->draw_data = NULL;
  priv->resize_cb = NULL;
  priv->resize_data = NULL;
  priv->close_cb = NULL;
  priv->close_data = NULL;
  priv->visible = FALSE;
  priv->parent = nil;

  GSRegisterCurrentThread();

  pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];

  rect.origin.x = 0;
  rect.origin.y = 0;
  rect.size.width = width;
  rect.size.height = height;

  priv->internal_win_id =[[GstGLNSWindow alloc] initWithContentRect:rect
    styleMask: (NSTitledWindowMask | NSClosableWindowMask | NSResizableWindowMask | NSMiniaturizableWindowMask)
    backing: NSBackingStoreBuffered defer: NO screen: nil gstWin: priv];

  if (priv->internal_win_id) {
    NSRect windowRect;
    NSRect mainRect = [[NSScreen mainScreen] visibleFrame];
    GST_DEBUG ("main screen rect: %d %d %d %d", (int) mainRect.origin.x, (int) mainRect.origin.y, 
      (int) mainRect.size.width, (int) mainRect.size.height);

    windowRect = [priv->internal_win_id frame];
    GST_DEBUG ("window rect: %d %d %d %d", (int) windowRect.origin.x, (int) windowRect.origin.y, 
      (int) windowRect.size.width, (int) windowRect.size.height);

    windowRect.origin.x += x;
    windowRect.origin.y += mainRect.size.height > y ? (mainRect.size.height - y) * 0.5 : y;
    [priv->internal_win_id setFrame:windowRect display:NO];
  }

  [pool release];

  return window;
}

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error");
}

void
gst_gl_window_set_external_window_id (GstGLWindow * window, guint64 id)
{
  GstGLWindowPrivate *priv = window->priv;

  if (GSRegisterCurrentThread()) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithPrivate:priv];
    [app_thread_performer performSelectorOnMainThread:@selector(setWindow) withObject:(NSWindow *)(gulong)id waitUntilDone:YES];

    [pool release];

    GSUnregisterCurrentThread();
  }
  else
    g_debug ("failed to register current thread, cannot set external window id");
}

void
gst_gl_window_set_external_gl_context (GstGLWindow * window, guint64 context)
{
  g_warning ("gst_gl_window_set_external_gl_context: not implemented\n");
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
gst_gl_window_draw_unlocked (GstGLWindow * window)
{
  gst_gl_window_draw (window);
}

/* Thread safe */
void
gst_gl_window_draw (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;

  if (GSRegisterCurrentThread()) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithPrivate:priv];
    [app_thread_performer performSelectorOnMainThread:@selector(updateWindow) withObject:nil waitUntilDone:YES];

    [pool release];

    GSUnregisterCurrentThread();
  }
  else
    g_debug ("failed to register current thread, cannot draw");
}

void
gst_gl_window_run_loop (GstGLWindow * window)
{
  GstGLWindowPrivate *priv = window->priv;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  g_debug ("begin loop\n");

  if (priv->internal_win_id != nil) {
    [NSApp setDelegate:priv->internal_win_id];
    [NSApp run];
    [priv->internal_win_id release];
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
    if (GSRegisterCurrentThread()) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithCallback:callback userData:data];
      [app_thread_performer performSelectorOnMainThread:@selector(stopApp) withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      g_debug ("failed to register current thread, application thread is lost");
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

      AppThreadPerformer* app_thread_performer = [[AppThreadPerformer alloc] initWithAll:callback userData:data private:priv];
      [app_thread_performer performSelectorOnMainThread:@selector(sendToApp) withObject:nil waitUntilDone:YES];

      [pool release];

      GSUnregisterCurrentThread();
    }
    else
      g_debug ("failed to register current thread, cannot send message");
  }
}


/* ============================================================= */
/*                                                               */
/*                 GstGLNSWindow implementation                  */
/*                                                               */
/* ============================================================= */

@implementation GstGLNSWindow

- (id) initWithContentRect: (NSRect) contentRect
		styleMask: (unsigned int) styleMask
    backing: (NSBackingStoreType) bufferingType
    defer: (BOOL) flag screen: (NSScreen *) aScreen
    gstWin: (GstGLWindowPrivate *) priv {

  NSOpenGLView *glView = nil;
  NSOpenGLPixelFormat *fmt = nil;
  NSOpenGLContext *glContext = nil;
  NSOpenGLPixelFormatAttribute attribs[] = {
    NSOpenGLPFAAccelerated,
    NSOpenGLPFANoRecovery,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
    NSOpenGLPFADepthSize, 24,
    NSOpenGLPFAWindow,
    0
  };

  m_isClosed = NO;
  m_priv = priv;

  self = [super initWithContentRect: contentRect
		styleMask: styleMask backing: bufferingType
		defer: flag screen:aScreen];

  [self setReleasedWhenClosed:NO];

  g_debug ("initializing GstGLNSWindow");

  glView = [NSOpenGLView alloc];

  [self setContentView:glView];

  fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];

  if (!fmt) {
    g_warning ("cannot create NSOpenGLPixelFormat");
    return nil;
  }

  glView = [glView initWithFrame:contentRect pixelFormat:fmt];

  glContext = [glView openGLContext];

  /* OpenGL context is made current only one time threre.
   * Indeed, all OpenGL calls are made in only one thread,
   * the Application thread */
  [glContext makeCurrentContext];

  [glContext update];

  /* Back and front buffers are swapped only during the vertical retrace of the monitor.
   * Discarded if you configured your driver to Never-use-V-Sync.
   */
  NS_DURING {
#if 0
    /* FIXME doesn't compile */
    if (glContext) {
      long swapInterval = 1;
      [[glView openGLContext] setValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];
    }
#endif
  } NS_HANDLER {
     g_debug ("your back-end does not implement NSOpenglContext::setValues");
  }
  NS_ENDHANDLER

  g_debug ("opengl GstGLNSWindow initialized: %d x %d",
    (int) contentRect.size.width, (int) contentRect.size.height);

  [self setTitle:@"OpenGL renderer"];

  return self;
}

- (BOOL) isClosed {
  return m_isClosed;
}

- (BOOL) windowShouldClose:(id)sender {
  g_debug ("user clicked the close button");
  m_isClosed = YES;
  if (m_priv->close_cb)
    m_priv->close_cb (m_priv->close_data);
  m_priv->draw_cb = NULL;
  m_priv->draw_data = NULL;
  m_priv->resize_cb = NULL;
  m_priv->resize_data = NULL;
  m_priv->close_cb = NULL;
  m_priv->close_data = NULL;
  return YES;
}

- (void) windowDidResize: (NSNotification *) not {
  NSLog(@"windowDidResize"); //FIXME: seems to be not reached on win32
  if (m_priv->resize_cb) {
    NSWindow *window = [not object];
    NSRect rect = [window frame];
    m_priv->resize_cb (m_priv->resize_data, rect.size.width, rect.size.height);
  }
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


/* ============================================================= */
/*                                                               */
/*               AppThreadPerformer implementation               */
/*                                                               */
/* ============================================================= */

@implementation AppThreadPerformer

- (id) initWithPrivate: (GstGLWindowPrivate *) priv {
  m_priv = priv;
  m_callback = NULL;
  m_data = NULL;
  return self;
}

- (id) initWithCallback: (GstGLWindowCB) callback userData: (gpointer) data {
  m_priv = NULL;
  m_callback = callback;
  m_data = data;
  return self;
}

- (id) initWithAll: (GstGLWindowCB) callback userData: (gpointer) data private: (GstGLWindowPrivate *) priv {
  m_priv = priv;
  m_callback = callback;
  m_data = data;
  return self;
}

- (void) updateWindow {
  if ([NSApp isRunning]) {

    if (![m_priv->internal_win_id isClosed]) {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

      if (!m_priv->visible) {
          g_debug ("make the window available");
          [m_priv->internal_win_id makeMainWindow];
          //[m_priv->internal_win_id center];
          [m_priv->internal_win_id orderFront:m_priv->internal_win_id];
          m_priv->visible = TRUE;
      }

      if (m_priv->parent) {
        NSRect parent_rect = [[m_priv->internal_win_id parentWindow] frame];
        NSRect rect = [m_priv->internal_win_id frame];

        if (rect.origin.x != parent_rect.origin.x || rect.origin.y != parent_rect.origin.y ||
            rect.size.width != parent_rect.size.width || rect.size.height != parent_rect.size.height) {

          [m_priv->internal_win_id setFrame:parent_rect display:YES];

          g_debug ("parent resize:  %d, %d, %d, %d\n",
            (int) parent_rect.origin.x, (int) parent_rect.origin.y,
            (int) parent_rect.size.width, (int) parent_rect.size.height);
        }
      }

      if ([[m_priv->internal_win_id contentView] lockFocusIfCanDraw]) {
        /* draw opengl scene in the back buffer */
        m_priv->draw_cb (m_priv->draw_data);
        /* Copy the back buffer to the front buffer */
        [[[m_priv->internal_win_id contentView] openGLContext] flushBuffer];
        [[m_priv->internal_win_id contentView] unlockFocus];
      }

      [pool release];
    }
  }
}

- (void) sendToApp {
  if ([NSApp isRunning]) {
    if (![m_priv->internal_win_id isClosed]) {
      m_callback (m_data);
    }
  }
}

- (void) setWindow: (NSWindow *) window {
  if ([NSApp isRunning]) {

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    [m_priv->internal_win_id setParentWindow:window];

    m_priv->parent = window;

    [m_priv->internal_win_id setFrame:[window frame] display:YES];

    [pool release];
  }
}

- (void) stopApp {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  if ([NSApp isRunning])
    [NSApp stop:self];

  [pool release];
}

@end
