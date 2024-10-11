/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2023 Havard Graff <havard@pexip.com
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

#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED) || MAC_OS_X_VERSION_MAX_ALLOWED >= 1014
# define GL_SILENCE_DEPRECATION
#endif

#include <Cocoa/Cocoa.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/gl/gl.h>

#if MAC_OS_X_VERSION_MAX_ALLOWED < 101200
#define NSEventMaskAny                       NSAnyEventMask
#define NSWindowStyleMaskTitled              NSTitledWindowMask
#define NSWindowStyleMaskClosable            NSClosableWindowMask
#define NSWindowStyleMaskResizable           NSResizableWindowMask
#define NSWindowStyleMaskMiniaturizable      NSMiniaturizableWindowMask
#endif

/* ============================================================= */
/*                                                               */
/*                          MainWindow                           */
/*                                                               */
/* ============================================================= */

@ interface MainWindow:NSWindow < NSApplicationDelegate > {
  GMainLoop * m_loop;
  GstElement *m_pipeline;
  gboolean m_isClosed;
}

-(id) initWithContentRect:(NSRect)
     contentRect Loop:(GMainLoop *)
     loop Pipeline:(GstElement *) pipeline;
-(GMainLoop *) loop;
-(GstElement *) pipeline;
-(gboolean) isClosed;
@end @ implementation MainWindow - (id) initWithContentRect:(NSRect)
     contentRect Loop:(GMainLoop *)
     loop Pipeline:(GstElement *) pipeline
{
  m_loop = loop;
  m_pipeline = pipeline;
  m_isClosed = FALSE;

self =[super initWithContentRect: contentRect styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
          NSWindowStyleMaskResizable |
          NSWindowStyleMaskMiniaturizable)
backing: NSBackingStoreBuffered defer: NO screen:nil];

[self setReleasedWhenClosed:NO];
[[NSApplication sharedApplication] setDelegate:self];

[self setTitle:@"gst-plugins-gl implements videooverlay interface"];

  return self;
}

-(GMainLoop *) loop {
  return m_loop;
}

-(GstElement *) pipeline {
  return m_pipeline;
}

-(gboolean) isClosed {
  return m_isClosed;
}

-(void) customClose {
  m_isClosed = TRUE;
}

-(BOOL) windowShouldClose:(id) sender {
  gst_element_send_event (m_pipeline, gst_event_new_eos ());
  return YES;
}

-(void) applicationDidFinishLaunching:(NSNotification *) not {
  [self makeMainWindow];
  [self center];
[self orderFront:self];
}

-(BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication *) app {
  return NO;
}

@end typedef struct
{
  GstGLDisplay *display;
  GstGLContext *context;
} GLStuff;


/* ============================================================= */
/*                                                               */
/*                   gstreamer callbacks                         */
/*                                                               */
/* ============================================================= */

static void
end_stream_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * message,
    MainWindow * window)
{
  g_print ("end of stream\n");

  gst_element_set_state ([window pipeline], GST_STATE_NULL);
  gst_object_unref ([window pipeline]);
  g_main_loop_quit ([window loop]);

[window performSelectorOnMainThread: @selector (customClose) withObject: nil waitUntilDone:YES];
}

GstBusSyncReply
need_context (G_GNUC_UNUSED GstBus * bus, GstMessage * message,
    gpointer user_data)
{
  GLStuff *gl_stuff = user_data;
  const gchar *context_type;
  GstContext *context = NULL;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_NEED_CONTEXT)
    return GST_BUS_PASS;

  gst_message_parse_context_type (message, &context_type);
  GST_INFO ("got need context %s", context_type);

  if (g_strcmp0 (context_type, "gst.gl.app_context") == 0) {
    GstStructure *s;

    context = gst_context_new ("gst.gl.app_context", TRUE);
    s = gst_context_writable_structure (context);
    gst_structure_set (s, "context", GST_TYPE_GL_CONTEXT, gl_stuff->context,
        NULL);

    GST_INFO ("Setting glcontext on element %" GST_PTR_FORMAT,
        GST_ELEMENT_CAST (message->src));
    gst_element_set_context (GST_ELEMENT (message->src), context);
  } else if (g_strcmp0 (context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) {
    context = gst_context_new (GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
    gst_context_set_gl_display (context, gl_stuff->display);
    GST_INFO ("Setting gldisplay on element %" GST_PTR_FORMAT,
        GST_ELEMENT_CAST (message->src));
    gst_element_set_context (GST_ELEMENT (message->src), context);
  }

  if (context)
    gst_context_unref (context);

  return GST_BUS_DROP;
}

static gpointer
thread_func (MainWindow * window)
{
  g_main_loop_run ([window loop]);

  return NULL;
}

typedef struct
{
  MainWindow *window;
  GstElement *videosrc;
  GstElement *videosink;
} Src2Sink;

static void
_src2sink_create (Src2Sink * ctx, GstBin * pipeline, GstCaps * caps,
    MainWindow * window)
{
  ctx->window = window;
  ctx->videosrc = gst_element_factory_make ("videotestsrc", NULL);
  ctx->videosink = gst_element_factory_make ("glimagesink", NULL);

  g_object_set (G_OBJECT (ctx->videosrc), "is-live", TRUE, NULL);
  g_object_set (G_OBJECT (ctx->videosink), "async", FALSE, NULL);

  gst_bin_add_many (pipeline, ctx->videosrc, ctx->videosink, NULL);
  gst_element_link_filtered (ctx->videosrc, ctx->videosink, caps);

  gst_element_set_state (ctx->videosink, GST_STATE_PLAYING);
  gst_element_set_state (ctx->videosrc, GST_STATE_PLAYING);
}

static void
_start_src2sink (Src2Sink * ctx)
{
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (ctx->videosink),
      (guintptr)[ctx->window contentView]);
  gst_element_set_state (ctx->videosink, GST_STATE_PLAYING);
  gst_element_set_state (ctx->videosrc, GST_STATE_PLAYING);
}

void
_stop_src2sink (Src2Sink * ctx)
{
  gst_element_set_state (ctx->videosrc, GST_STATE_NULL);
  gst_element_set_state (ctx->videosink, GST_STATE_NULL);
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (ctx->videosink),
      (guintptr) NULL);
}

static gpointer
_start_and_stop_thread (gpointer user_data)
{
  GArray *srcsinkctxs = user_data;
  guint i;

  g_usleep (G_USEC_PER_SEC * 2);

  for (i = 0; i < srcsinkctxs->len; i++) {
    Src2Sink *ctx = &g_array_index (srcsinkctxs, Src2Sink, i);
    _start_src2sink (ctx);
  }

  g_usleep (G_USEC_PER_SEC * 2);

  for (i = 0; i < srcsinkctxs->len; i++) {
    Src2Sink *ctx = &g_array_index (srcsinkctxs, Src2Sink, i);
    _stop_src2sink (ctx);
  }

  g_usleep (G_USEC_PER_SEC * 2);

  for (i = 0; i < srcsinkctxs->len; i++) {
    Src2Sink *ctx = &g_array_index (srcsinkctxs, Src2Sink, i);
    _start_src2sink (ctx);
  }

  g_usleep (G_USEC_PER_SEC * 2);

  for (i = 0; i < srcsinkctxs->len; i++) {
    Src2Sink *ctx = &g_array_index (srcsinkctxs, Src2Sink, i);
    _stop_src2sink (ctx);
  }

  return NULL;
}

/* ============================================================= */
/*                                                               */
/*                         application                           */
/*                                                               */
/* ============================================================= */

int
main (int argc, char **argv)
{
  int width = 640;
  int height = 480;

  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL;

  GstCaps *caps = NULL;
  GstBus *bus = NULL;
  GThread *loop_thread = NULL;
  GThread *start_n_stop_thread = NULL;
  NSRect rect;
  //GLStuff gl_stuff;
  //GError *error = NULL;
  MainWindow *window;
  GArray *srcsinkctxs;
  guint num_srcsinks = 10;
  guint i;

  [NSApplication sharedApplication];

  gst_init (&argc, &argv);

#if 0
  /* Create GLDisplay and GLContext */
  gl_stuff.display = gst_gl_display_new ();
  if (!gst_gl_display_create_context (gl_stuff.display,
          NULL, &gl_stuff.context, &error)) {
    GST_ERROR ("Could not create GLContext %s", error->message);
    g_clear_error (&error);
    g_assert_not_reached ();
  }
#endif

  loop = g_main_loop_new (NULL, FALSE);
  pipeline = gst_pipeline_new ("pipeline");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  rect.origin.x = 0;
  rect.origin.y = 0;
  rect.size.width = width;
  rect.size.height = height;

window =[[MainWindow alloc] initWithContentRect: rect Loop: loop Pipeline:pipeline];

  caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 25, 1,
      "format", G_TYPE_STRING, "I420", NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", G_CALLBACK (end_stream_cb),
      (__bridge gpointer) window);
  //gst_bus_set_sync_handler (bus, (GstBusSyncHandler) need_context, &gl_stuff, NULL);
  gst_object_unref (bus);

  loop_thread = g_thread_new (NULL,
      (GThreadFunc) thread_func, (__bridge gpointer) window);

[window orderFront:window];

  srcsinkctxs = g_array_new (FALSE, FALSE, sizeof (Src2Sink));
  for (i = 0; i < num_srcsinks; i++) {
    Src2Sink ctx;
    _src2sink_create (&ctx, GST_BIN (pipeline), caps, window);
    g_array_append_val (srcsinkctxs, ctx);
  }

  start_n_stop_thread =
      g_thread_new ("startnstop", _start_and_stop_thread, srcsinkctxs);

  while (![window isClosed]) {
  NSEvent *event =[NSApp nextEventMatchingMask: NSEventMaskAny untilDate: [NSDate dateWithTimeIntervalSinceNow:1]
  inMode: NSDefaultRunLoopMode dequeue:YES];
    if (event)
    [NSApp sendEvent:event];
  }

  g_thread_join (start_n_stop_thread);
  g_thread_join (loop_thread);
  gst_caps_unref (caps);

  return 0;
}
