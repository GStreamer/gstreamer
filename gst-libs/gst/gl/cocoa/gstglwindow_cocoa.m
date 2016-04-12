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
#include <QuartzCore/QuartzCore.h>

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
static void gst_gl_window_cocoa_finalize (GObject * object);

static gboolean gst_gl_window_cocoa_open (GstGLWindow *window, GError **err);
static void gst_gl_window_cocoa_close (GstGLWindow *window);
static guintptr gst_gl_window_cocoa_get_window_handle (GstGLWindow * window);
static void gst_gl_window_cocoa_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_cocoa_draw (GstGLWindow * window);
static void gst_gl_window_cocoa_set_preferred_size (GstGLWindow * window,
    gint width, gint height);
static void gst_gl_window_cocoa_show (GstGLWindow * window);
static void gst_gl_window_cocoa_queue_resize (GstGLWindow * window);
static void gst_gl_window_cocoa_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);

struct _GstGLWindowCocoaPrivate
{
  GstGLNSWindow *internal_win_id;
  NSView *external_view;
  gboolean visible;
  gint preferred_width;
  gint preferred_height;

  GLint viewport_dim[4];

  /* atomic set when the internal NSView has been created */
  int view_ready;

  dispatch_queue_t gl_queue;
};

static void
gst_gl_window_cocoa_class_init (GstGLWindowCocoaClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowCocoaPrivate));

  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_close);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_set_window_handle);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_draw);
  window_class->set_preferred_size =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_set_preferred_size);
  window_class->show = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_show);
  window_class->queue_resize = GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_queue_resize);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_cocoa_send_message_async);

  gobject_class->finalize = gst_gl_window_cocoa_finalize;
}

static void
gst_gl_window_cocoa_init (GstGLWindowCocoa * window)
{
  window->priv = GST_GL_WINDOW_COCOA_GET_PRIVATE (window);

  window->priv->preferred_width = 320;
  window->priv->preferred_height = 240;
  window->priv->gl_queue =
      dispatch_queue_create ("org.freedesktop.gstreamer.glwindow", NULL);
}

static void
gst_gl_window_cocoa_finalize (GObject * object)
{
  GstGLWindowCocoa *window = GST_GL_WINDOW_COCOA (object);
  dispatch_release (window->priv->gl_queue);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GstGLWindowCocoa *
gst_gl_window_cocoa_new (GstGLDisplay * display)
{
  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_COCOA) == 0)
    /* we require an cocoa display to create CGL windows */
    return NULL;

  return g_object_new (GST_GL_TYPE_WINDOW_COCOA, NULL);
}

/* Must be called from the main thread */
gboolean
gst_gl_window_cocoa_create_window (GstGLWindowCocoa *window_cocoa)
{
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;
  GstGLWindow *window = GST_GL_WINDOW (window_cocoa);
  NSRect mainRect = [[NSScreen mainScreen] visibleFrame];
  gint h = priv->preferred_height;
  gint y = mainRect.size.height > h ? (mainRect.size.height - h) * 0.5 : 0;
  NSRect rect = NSMakeRect (0, y, priv->preferred_width, priv->preferred_height);
  NSRect windowRect = NSMakeRect (0, y, priv->preferred_width, priv->preferred_height);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextCocoa *context_cocoa = GST_GL_CONTEXT_COCOA (context);
  GstGLCAOpenGLLayer *layer = [[GstGLCAOpenGLLayer alloc] initWithGstGLContext:context_cocoa];
  GstGLNSView *glView = [[GstGLNSView alloc] initWithFrameLayer:window_cocoa rect:windowRect layer:layer];

  gst_object_unref (context);

  priv->internal_win_id = [[GstGLNSWindow alloc] initWithContentRect:rect styleMask: 
      (NSTitledWindowMask | NSClosableWindowMask |
      NSResizableWindowMask | NSMiniaturizableWindowMask)
      backing: NSBackingStoreBuffered defer: NO screen: nil gstWin: window_cocoa];

      GST_DEBUG ("NSWindow id: %"G_GUINTPTR_FORMAT, (guintptr) priv->internal_win_id);

  [priv->internal_win_id setContentView:glView];

  g_atomic_int_set (&window_cocoa->priv->view_ready, 1);

  return TRUE;
}

static gboolean
gst_gl_window_cocoa_open (GstGLWindow *window, GError **err)
{
  GstGLWindowCocoa *window_cocoa;

  window_cocoa = GST_GL_WINDOW_COCOA (window);

  return TRUE;
}

static void
gst_gl_window_cocoa_close (GstGLWindow *window)
{
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);

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

static void
_show_window (gpointer data)
{
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (data);
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;

  GST_DEBUG_OBJECT (window_cocoa, "make the window available\n");
  [priv->internal_win_id makeMainWindow];
  [priv->internal_win_id orderFrontRegardless];
  [priv->internal_win_id setViewsNeedDisplay:YES];

  priv->visible = TRUE;
}

static void
gst_gl_window_cocoa_show (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;

  if (!priv->visible) {
    /* useful when set_window_handle is called before
     * the internal NSWindow */
    if (priv->external_view) {
      gst_gl_window_cocoa_set_window_handle (window, (guintptr) priv->external_view);
      priv->visible = TRUE;
      return;
    }

    if (!priv->external_view && !priv->visible)
      _invoke_on_main ((GstGLWindowCB) _show_window, window);
  }
}

static void
gst_gl_window_cocoa_queue_resize (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);
  GstGLNSView *view;

  if (!g_atomic_int_get (&window_cocoa->priv->view_ready))
    return;

  view = (GstGLNSView *)[window_cocoa->priv->internal_win_id contentView];

  [view->layer queueResize];
}

static void
gst_gl_window_cocoa_draw (GstGLWindow * window)
{
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);
  GstGLNSView *view;

  /* As the view is created asynchronously in the main thread we cannot know
   * exactly when it will be ready to draw to */
  if (!g_atomic_int_get (&window_cocoa->priv->view_ready))
    return;

  view = (GstGLNSView *)[window_cocoa->priv->internal_win_id contentView];

  /* this redraws the GstGLCAOpenGLLayer which calls
   * gst_gl_window_cocoa_draw_thread(). Use an explicit CATransaction since we
   * don't know how often the main runloop is running.
   */
  [CATransaction begin];
  [view setNeedsDisplay:YES];
  [CATransaction commit];
}

static void
gst_gl_window_cocoa_set_preferred_size (GstGLWindow * window, gint width,
    gint height)
{
  GstGLWindowCocoa *window_cocoa = GST_GL_WINDOW_COCOA (window);

  window_cocoa->priv->preferred_width = width;
  window_cocoa->priv->preferred_height = height;
}

static void
gst_gl_cocoa_draw_cb (GstGLWindowCocoa *window_cocoa)
{
  GstGLWindowCocoaPrivate *priv = window_cocoa->priv;
  GstGLWindow *window = GST_GL_WINDOW (window_cocoa);

  if (gst_gl_window_is_running (window)) {
    if (![priv->internal_win_id isClosed]) {
     GstGLWindow *window = GST_GL_WINDOW (window_cocoa);

      /* draw opengl scene in the back buffer */
      if (window->draw)
        window->draw (window->draw_data);
    }
  }
}

static void
gst_gl_cocoa_resize_cb (GstGLNSView * view, guint width, guint height)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  GstGLWindowCocoa *window_cocoa = view->window_cocoa;
  GstGLWindow *window = GST_GL_WINDOW (window_cocoa);
  GstGLContext *context = gst_gl_window_get_context (window);

  if (gst_gl_window_is_running (window) && ![window_cocoa->priv->internal_win_id isClosed]) {
    const GstGLFuncs *gl;
    NSRect bounds = [view bounds];
    NSRect visibleRect = [view visibleRect];
    gint viewport_dim[4];

    gl = context->gl_vtable;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    bounds = [view convertRectToBacking:bounds];
    visibleRect = [view convertRectToBacking:visibleRect];
#endif

    GST_DEBUG_OBJECT (window, "Window resized: bounds %lf %lf %lf %lf "
                      "visibleRect %lf %lf %lf %lf",
                      bounds.origin.x, bounds.origin.y,
                      bounds.size.width, bounds.size.height,
                      visibleRect.origin.x, visibleRect.origin.y,
                      visibleRect.size.width, visibleRect.size.height);

    gst_gl_window_resize (window, width, height);
    gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

    gl->Viewport (viewport_dim[0] - visibleRect.origin.x,
                  viewport_dim[1] - visibleRect.origin.y,
                  viewport_dim[2], viewport_dim[3]);
  }

  gst_object_unref (context);
  [pool release];
}

static void
gst_gl_window_cocoa_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowCocoa *window_cocoa = (GstGLWindowCocoa *) window;
  GstGLContext *context = gst_gl_window_get_context (window);
  GThread *thread = gst_gl_context_get_thread (context);

  if (thread == g_thread_self()) {
    /* this case happens for nested calls happening from inside the GCD queue */
    callback (data);
    if (destroy)
      destroy (data);
    gst_object_unref (context);
  } else {
    dispatch_async (window_cocoa->priv->gl_queue, ^{
      gst_gl_context_activate (context, TRUE);
      gst_object_unref (context);
      callback (data);
      if (destroy)
        destroy (data);
    });
  }
  if (thread)
    g_thread_unref (thread);
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

  [self setBackgroundColor:[NSColor blackColor]];

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
  gst_gl_window_send_message_async (GST_GL_WINDOW (window_cocoa),
      (GstGLWindowCB) close_window_cb, gst_object_ref (window_cocoa),
      (GDestroyNotify) gst_object_unref);
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
- (id)initWithFrameLayer:(GstGLWindowCocoa *)window rect:(NSRect)contentRect layer:(CALayer *)layerContent {

  self = [super initWithFrame: contentRect];

  window_cocoa = window;

  /* The order of the next two calls matters.  This creates a layer-hosted
   * NSView.  Calling setWantsLayer before setLayer will create a
   * layer-backed NSView.  See the apple developer documentation on the
   * difference.
   */
  [self setLayer:layerContent];
  [self setWantsLayer:YES];
  self->layer = (GstGLCAOpenGLLayer *)layerContent;
  [self->layer setDrawCallback:(GstGLWindowCB)gst_gl_cocoa_draw_cb
      data:window notify:NULL];
  [self->layer setResizeCallback:(GstGLWindowResizeCB)gst_gl_cocoa_resize_cb
      data:self notify:NULL];

  [self setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawOnSetNeedsDisplay];

  [self setWantsBestResolutionOpenGLSurface:YES];

  return self;
}

- (void) dealloc {
  [self->layer release];

  [super dealloc];
}

- (void)renewGState {
  /* Don't update the screen until we redraw, this
   * prevents flickering during scrolling, clipping,
   * resizing, etc
   */
  [[self window] disableScreenUpdatesUntilFlush];

  [super renewGState];
}

- (BOOL) isOpaque {
    return YES;
}

- (BOOL) isFlipped {
    return NO;
}

@end

void
_invoke_on_main (GstGLWindowCB func, gpointer data)
{
  if ([NSThread isMainThread]) {
    func (data);
  } else {
    dispatch_async (dispatch_get_main_queue (), ^{
      func (data);
    });
  }
}
