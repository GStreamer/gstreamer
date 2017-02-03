/*
 * GStreamer
 * Copyright (C) 2015 Julien Isorce <julien.isorce@gmail.com>
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

#include <gst/gl/cocoa/gstgldisplay_cocoa.h>

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

G_DEFINE_TYPE (GstGLDisplayCocoa, gst_gl_display_cocoa, GST_TYPE_GL_DISPLAY);

static void gst_gl_display_cocoa_finalize (GObject * object);
static guintptr gst_gl_display_cocoa_get_handle (GstGLDisplay * display);

/* Define this if the GLib patch from
 * https://bugzilla.gnome.org/show_bug.cgi?id=741450
 * is used
 */
#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION

static GstGLDisplayCocoa *singleton = NULL;
static gint nsapp_source_id = 0;
static GMutex nsapp_lock;
static GCond nsapp_cond;

static gboolean
gst_gl_display_cocoa_nsapp_iteration (gpointer data)
{
  NSEvent *event = nil;

  if (![NSThread isMainThread]) {
    GST_WARNING ("NSApp iteration not running in the main thread");
    return FALSE;
  }


  while ((event = ([NSApp nextEventMatchingMask:NSAnyEventMask
      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
      inMode:NSDefaultRunLoopMode dequeue:YES])) != nil) {
    [NSApp sendEvent:event];
  }

  return TRUE;
}

static void
gst_gl_display_cocoa_open_and_attach_source (gpointer data)
{
  if ([NSThread isMainThread]) {
    /* The sharedApplication class method initializes
     * the display environment and connects your program
     * to the window server and the display server.
     * It has to be done in the main thread.
     */
    [NSApplication sharedApplication];

    GST_DEBUG ("Custom NSApp initialization done");

    nsapp_source_id = g_timeout_add (60, gst_gl_display_cocoa_nsapp_iteration,
        NULL);

    GST_DEBUG ("NSApp iteration loop attached, id %d", nsapp_source_id);
  }
}

static gboolean
gst_gl_display_cocoa_init_nsapp (gpointer data)
{
  g_mutex_lock (&nsapp_lock);

  gst_gl_display_cocoa_open_and_attach_source (data);

  g_cond_signal (&nsapp_cond);
  g_mutex_unlock (&nsapp_lock);

  return FALSE;
}

static GstGLDisplayCocoa *
gst_gl_display_cocoa_setup_nsapp (gpointer data)
{
  GMainContext *context = g_main_context_default ();
  gint delta_ms = 0;

  g_mutex_lock (&nsapp_lock);

  if (singleton) {
    GST_DEBUG ("Get existing display");
    singleton = gst_object_ref (singleton);
    g_mutex_unlock (&nsapp_lock);
    return singleton;
  }

  if (NSApp != nil && !singleton) {
    GstGLDisplayCocoa *ret = g_object_new (GST_TYPE_GL_DISPLAY_COCOA, NULL);
    g_mutex_unlock (&nsapp_lock);
    return ret;
  }

  /* All application have to start with [NSApplication sharedApplication]
   * so if NSApp is nil here let's assume this is a debugging application
   * that runs a glib main loop. */
  g_assert (NSApp == nil);

  GST_DEBUG ("The application has not initialized NSApp");

  if ([NSThread isMainThread]) {

    GST_DEBUG ("Setting up NSApp from the main thread");
    if (g_main_context_is_owner (context)) {
      GST_DEBUG ("The main thread own the context");
      gst_gl_display_cocoa_open_and_attach_source (data);
    } else if (g_main_context_acquire (context)) {
      GST_DEBUG ("The main loop should be shortly running in the main thread");
      gst_gl_display_cocoa_open_and_attach_source (data);
      g_main_context_release (context);
    } else {
      GST_WARNING ("Main loop running in another thread");
    }
  } else {

    GST_DEBUG ("Setting up NSApp not from the main thread");

    if (g_main_context_is_owner (context)) {
      GST_WARNING ("Default context not own by the main thread");
      delta_ms = -1;
    } else if (g_main_context_acquire (context)) {
      GST_DEBUG ("The main loop should be shortly running in the main thread");
      delta_ms = 1000;
      g_main_context_release (context);
    } else {
      GST_DEBUG ("Main loop running in main thread");
      delta_ms = 500;
    }

    if (delta_ms > 0) {
      gint64 end_time = g_get_monotonic_time () + delta_ms * 1000;;
      g_idle_add_full (G_PRIORITY_HIGH, gst_gl_display_cocoa_init_nsapp, data, NULL);
      g_cond_wait_until (&nsapp_cond, &nsapp_lock, end_time);
    }
  }

  if (NSApp == nil) {
    GST_ERROR ("Custom NSApp initialization failed");
  } else {
    GST_DEBUG ("Create display");
    singleton = g_object_new (GST_TYPE_GL_DISPLAY_COCOA, NULL);
  }

  g_mutex_unlock (&nsapp_lock);

  return singleton;
}

#endif

static void
gst_gl_display_cocoa_class_init (GstGLDisplayCocoaClass * klass)
{
  GST_GL_DISPLAY_CLASS (klass)->get_handle =
      GST_DEBUG_FUNCPTR (gst_gl_display_cocoa_get_handle);

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_cocoa_finalize;
}

static void
gst_gl_display_cocoa_init (GstGLDisplayCocoa * display_cocoa)
{
  GstGLDisplay *display = (GstGLDisplay *) display_cocoa;
  display->type = GST_GL_DISPLAY_TYPE_COCOA;
}

static void
gst_gl_display_cocoa_finalize (GObject * object)
{
#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION
  g_mutex_lock (&nsapp_lock);
  if (singleton) {
    GST_DEBUG ("Destroy display");
    singleton = NULL;
    if (nsapp_source_id) {
      GST_DEBUG ("Remove NSApp loop iteration, id %d", nsapp_source_id);
      g_source_remove (nsapp_source_id);
    }
    nsapp_source_id = 0;
    g_mutex_unlock (&nsapp_lock);
  }
  g_mutex_unlock (&nsapp_lock);
#endif

  G_OBJECT_CLASS (gst_gl_display_cocoa_parent_class)->finalize (object);
}

/**
 * gst_gl_display_cocoa_new:
 *
 * Create a new #GstGLDisplayCocoa.
 *
 * Returns: (transfer full): a new #GstGLDisplayCocoa or %NULL
 */
GstGLDisplayCocoa *
gst_gl_display_cocoa_new (void)
{
  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

#ifndef GSTREAMER_GLIB_COCOA_NSAPPLICATION
  return gst_gl_display_cocoa_setup_nsapp (NULL);
#else
  return g_object_new (GST_TYPE_GL_DISPLAY_COCOA, NULL);
#endif
}

static guintptr
gst_gl_display_cocoa_get_handle (GstGLDisplay * display)
{
  return (guintptr) NSApp;
}
