/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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
  GstGLWindowCocoa *window_cocoa;
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

#define GST_GL_WINDOW_COCOA_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_COCOA, GstGLWindowCocoaPrivate))

#define GST_CAT_DEFAULT gst_gl_window_cocoa_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_cocoa_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowCocoa, gst_gl_window_cocoa, GST_GL_TYPE_WINDOW, DEBUG_INIT);

static gboolean gst_gl_window_cocoa_open (GstGLWindow *window, GError **err);
static void gst_gl_window_cocoa_close (GstGLWindow *window);
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
  GMainContext *main_context;
  GMainLoop *loop;

  GLint viewport_dim[4];
};

static void
gst_gl_window_cocoa_class_init (GstGLWindowCocoaClass * klass)
{
  GstGLWindowClass *window_class;

  window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowCocoaPrivate));

  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_close);
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

/* Must be called from the main thread */
gboolean
gst_gl_window_cocoa_create_window (GstGLWindowCocoa *window_cocoa, NSRect rect)
{
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;

  priv->internal_win_id = [[GstGLNSWindow alloc] initWithContentRect:rect styleMask: 
      (NSTitledWindowMask | NSClosableWindowMask |
      NSResizableWindowMask | NSMiniaturizableWindowMask)
      backing: NSBackingStoreBuffered defer: NO screen: nil gstWin: window_cocoa];

      GST_DEBUG ("NSWindow id: %"G_GUINTPTR_FORMAT, (guintptr) priv->internal_win_id);

  return TRUE;
}

static gboolean
gst_gl_window_cocoa_open (GstGLWindow *window, GError **err)
{
  GstGLWindowCocoa *window_cocoa;

  window_cocoa = GST_GL_WINDOW_COCOA (window);

  window_cocoa->priv->main_context = g_main_context_new ();
  window_cocoa->priv->loop =
      g_main_loop_new (window_cocoa->priv->main_context, FALSE);

  return TRUE;
}

static void
gst_gl_window_cocoa_close (GstGLWindow *window)
{
  GstGLWindowCocoa *window_cocoa;

  window_cocoa = GST_GL_WINDOW_COCOA (window);

  g_main_loop_unref (window_cocoa->priv->loop);
  g_main_context_unref (window_cocoa->priv->main_context);

  [window_cocoa->priv->internal_win_id release];
  window_cocoa->priv->internal_win_id = nil;
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
    if (handle) {
      priv->external_view = (NSView *) handle;
      priv->visible = TRUE;
    } else {
      /* bring back our internal window */
      priv->external_view = 0;
      priv->visible = FALSE;
    }


    dispatch_async (dispatch_get_main_queue (), ^{
      NSView *view = [window_cocoa->priv->internal_win_id contentView];
      [window_cocoa->priv->internal_win_id orderOut:window_cocoa->priv->internal_win_id];

      [window_cocoa->priv->external_view addSubview: view];

      [view setFrame: [window_cocoa->priv->external_view bounds]];
      [view setAutoresizingMask: NSViewWidthSizable|NSViewHeightSizable];
    });
  } else {
    /* no internal window yet so delay it to the next drawing */
    priv->external_view = (NSView*) handle;
    priv->visible = FALSE;
  }
}

/* Thread safe */
struct draw
{
  GstGLWindowCocoa *window;
  guint width, height;
};

static void
draw_cb (gpointer data)
{
  struct draw *draw_data = data;
  GstGLWindowCocoa *window_cocoa = draw_data->window;
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;

  /* useful when set_window_handle is called before
   * the internal NSWindow */
  if (priv->external_view && !priv->visible) {
    gst_gl_window_cocoa_set_window_handle (GST_GL_WINDOW (window_cocoa), (guintptr) priv->external_view);
    priv->visible = TRUE;
  }

  if (!priv->external_view && !priv->visible) {
    dispatch_sync (dispatch_get_main_queue (), ^{
      NSRect mainRect = [[NSScreen mainScreen] visibleFrame];
      NSRect windowRect = [priv->internal_win_id frame];
      gint x = 0;
      gint y = 0;

      GST_DEBUG ("main screen rect: %d %d %d %d\n", (int) mainRect.origin.x,
          (int) mainRect.origin.y, (int) mainRect.size.width,
          (int) mainRect.size.height);

      windowRect.origin.x += x;
      windowRect.origin.y += mainRect.size.height > y ? (mainRect.size.height - y) * 0.5 : y;
      windowRect.size.width = draw_data->width;
      windowRect.size.height = draw_data->height;

      GST_DEBUG ("window rect: %d %d %d %d\n", (int) windowRect.origin.x,
          (int) windowRect.origin.y, (int) windowRect.size.width,
          (int) windowRect.size.height);

      x += 20;
      y += 20;

      [priv->internal_win_id setFrame:windowRect display:NO];
      GST_DEBUG ("make the window available\n");
      [priv->internal_win_id makeMainWindow];

      [priv->internal_win_id orderFrontRegardless];

      [priv->internal_win_id setViewsNeedDisplay:YES];
    });
    priv->visible = TRUE;
  }

  if (g_main_loop_is_running (priv->loop)) {
    if (![priv->internal_win_id isClosed]) {
      GstGLContext *context = gst_gl_window_get_context (GST_GL_WINDOW (window_cocoa));
      NSOpenGLContext * glContext = (NSOpenGLContext *) gst_gl_context_get_gl_context (context);

      /* draw opengl scene in the back buffer */
      GST_GL_WINDOW (window_cocoa)->draw (GST_GL_WINDOW (window_cocoa)->draw_data);

      /* Copy the back buffer to the front buffer */
      [glContext flushBuffer];

      gst_object_unref (context);
    }
  }
}

static void
gst_gl_window_cocoa_draw (GstGLWindow * window, guint width, guint height)
{
  struct draw draw_data;

  draw_data.window = GST_GL_WINDOW_COCOA (window);
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}

static void
gst_gl_window_cocoa_run (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa;

  window_cocoa = GST_GL_WINDOW_COCOA (window);

  GST_LOG ("starting main loop");
  g_main_loop_run (window_cocoa->priv->loop);
  GST_LOG ("exiting main loop");
}

/* Thread safe */
static void
gst_gl_window_cocoa_quit (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa;

  window_cocoa = GST_GL_WINDOW_COCOA (window);

  g_main_loop_quit (window_cocoa->priv->loop);
}

/* Thread safe */
typedef struct _GstGLMessage
{
  GstGLWindowCB callback;
  gpointer data;
  GDestroyNotify destroy;
} GstGLMessage;

static gboolean
_run_message (GstGLMessage * message)
{
  if (message->callback)
    message->callback (message->data);

  if (message->destroy)
    message->destroy (message->data);

  g_slice_free (GstGLMessage, message);

  return FALSE;
}

static void
gst_gl_window_cocoa_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowCocoa *window_cocoa;
  GstGLMessage *message;

  window_cocoa = GST_GL_WINDOW_COCOA (window);
  message = g_slice_new (GstGLMessage);

  message->callback = callback;
  message->data = data;
  message->destroy = destroy;

  g_main_context_invoke (window_cocoa->priv->main_context,
      (GSourceFunc) _run_message, message);
}

/* =============================================================*/
/*                                                              */
/*                    GstGLNSWindow implementation              */
/*                                                              */
/* =============================================================*/

/* Must be called from the main thread */
@implementation GstGLNSWindow

- (id) initWithContentRect: (NSRect) contentRect
        styleMask: (unsigned int) styleMask
    backing: (NSBackingStoreType) bufferingType
    defer: (BOOL) flag screen: (NSScreen *) aScreen
    gstWin: (GstGLWindowCocoa *) cocoa {

  m_isClosed = NO;
  window_cocoa = cocoa;

  self = [super initWithContentRect: contentRect
        styleMask: styleMask backing: bufferingType
        defer: flag screen:aScreen];

  [self setReleasedWhenClosed:NO];

  GST_DEBUG ("initializing GstGLNSWindow\n");

  [self setTitle:@"OpenGL renderer"];

  [self setBackgroundColor:[NSColor clearColor]];

  [self orderOut:window_cocoa->priv->internal_win_id];

  if (window_cocoa->priv->external_view) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSView *view = [window_cocoa->priv->internal_win_id contentView];

    [window_cocoa->priv->external_view addSubview: view];
    [view setFrame: [window_cocoa->priv->external_view bounds]];
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

static void
close_window_cb (gpointer data)
{
  GstGLWindowCocoa *window_cocoa = data;
  GstGLWindow *window;

  window = GST_GL_WINDOW (window_cocoa);

  if (window->close) {
    window->close (window->close_data);
  }
}

/* Called in the main thread which is never the gl thread */
- (BOOL) windowShouldClose:(id)sender {

  GST_DEBUG ("user clicked the close button\n");
  [window_cocoa->priv->internal_win_id setClosed];
  gst_gl_window_send_message_async (GST_GL_WINDOW (window_cocoa), (GstGLWindowCB) close_window_cb, gst_object_ref (window_cocoa), (GDestroyNotify) gst_object_unref);
  return YES;
}

@end


/* =============================================================*/
/*                                                              */
/*                GstGLNSView implementation              */
/*                                                              */
/* =============================================================*/

@implementation GstGLNSView

/* Must be called from the application main thread */
- (id)initWithFrame:(GstGLWindowCocoa *)window rect:(NSRect)contentRect {

  self = [super initWithFrame: contentRect];

  window_cocoa = window;

  [self setWantsLayer:NO];

  /* Get notified about changes */
  [self setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter] addObserver: self selector:@selector(reshape:) name: NSViewFrameDidChangeNotification object: self];
  [self setWantsBestResolutionOpenGLSurface:YES];

  return self;
}

- (void) dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver: self];
  [super dealloc];
}

struct resize
{
  GstGLWindowCocoa * window;
  NSRect bounds, visibleRect;
};

static void
resize_cb (gpointer data)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  struct resize *resize_data = data;
  GstGLWindowCocoa *window_cocoa = resize_data->window;
  GstGLWindow *window = GST_GL_WINDOW (window_cocoa);
  GstGLContext *context = gst_gl_window_get_context (window);
  NSOpenGLContext * glContext = (NSOpenGLContext *) gst_gl_context_get_gl_context (context);

  if (g_main_loop_is_running (window_cocoa->priv->loop) && ![window_cocoa->priv->internal_win_id isClosed]) {
    const GstGLFuncs *gl;

    [glContext update];

    gl = context->gl_vtable;

    if (window->resize) {
      window->resize (window->resize_data, resize_data->bounds.size.width, resize_data->bounds.size.height);
      gl->GetIntegerv (GL_VIEWPORT, window_cocoa->priv->viewport_dim);
    }

    gl->Viewport (window_cocoa->priv->viewport_dim[0] - resize_data->visibleRect.origin.x,
                  window_cocoa->priv->viewport_dim[1] - resize_data->visibleRect.origin.y,
                  window_cocoa->priv->viewport_dim[2], window_cocoa->priv->viewport_dim[3]);

    GST_GL_WINDOW (window_cocoa)->draw (GST_GL_WINDOW (window_cocoa)->draw_data);
    [glContext flushBuffer];
  }
  gst_object_unref (context);
  [pool release];
}

- (void)renewGState {
  /* Don't update the screen until we redraw, this
   * prevents flickering during scrolling, clipping,
   * resizing, etc
   */
  [[self window] disableScreenUpdatesUntilFlush];

  [super renewGState];
}

- (void)reshape: (NSNotification*)notification {
  GstGLWindow *window;

  window = GST_GL_WINDOW (window_cocoa);

  if (window->resize) {
    NSRect bounds = [self bounds];
    NSRect visibleRect = [self visibleRect];
    struct resize *resize_data = g_new (struct resize, 1);

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    bounds = [self convertRectToBacking:bounds];
    visibleRect = [self convertRectToBacking:visibleRect];
#endif

    GST_DEBUG_OBJECT (window, "Window resized: bounds %lf %lf %lf %lf "
                      "visibleRect %lf %lf %lf %lf",
                      bounds.origin.x, bounds.origin.y,
                      bounds.size.width, bounds.size.height,
                      visibleRect.origin.x, visibleRect.origin.y,
                      visibleRect.size.width, visibleRect.size.height);

    resize_data->window = window_cocoa;
    resize_data->bounds = bounds;
    resize_data->visibleRect = visibleRect;

    gst_gl_window_send_message_async (GST_GL_WINDOW (window_cocoa), (GstGLWindowCB) resize_cb, resize_data, (GDestroyNotify) g_free);
  }
}

- (void)drawRect: (NSRect)dirtyRect {
  [self reshape:nil];
}

- (BOOL) isOpaque {
    return YES;
}

- (BOOL) isFlipped {
    return NO;
}

@end

