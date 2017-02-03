/*
 * GStreamer
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

#import <OpenGLES/EAGL.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#include <OpenGLES/ES2/gl.h>

#include "gstglcontext_eagl.h"

#define GST_CAT_DEFAULT gst_gl_context_debug

static gboolean gst_gl_context_eagl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error);
static void gst_gl_context_eagl_destroy_context (GstGLContext * context);
static gboolean gst_gl_context_eagl_choose_format (GstGLContext * context,
    GError ** error);
static guintptr gst_gl_context_eagl_get_gl_context (GstGLContext * window);
static gboolean gst_gl_context_eagl_activate (GstGLContext * context,
    gboolean activate);
static void gst_gl_context_eagl_swap_buffers (GstGLContext * context);
static GstGLAPI gst_gl_context_eagl_get_gl_api (GstGLContext * context);
static GstGLPlatform gst_gl_context_eagl_get_gl_platform (GstGLContext *
    context);

struct _GstGLContextEaglPrivate
{
  gpointer eagl_context;

  /* Used if we render to a window */
  gpointer eagl_layer;
  GLuint framebuffer;
  GLuint color_renderbuffer;
  GLuint depth_renderbuffer;
};

#define GST_GL_CONTEXT_EAGL_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_CONTEXT_EAGL, GstGLContextEaglPrivate))

G_DEFINE_TYPE (GstGLContextEagl, gst_gl_context_eagl, GST_TYPE_GL_CONTEXT);

static void
gst_gl_context_eagl_class_init (GstGLContextEaglClass * klass)
{
  GstGLContextClass *context_class;

  context_class = (GstGLContextClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLContextEaglPrivate));

  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_destroy_context);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_create_context);
  context_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_choose_format);
  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_get_gl_context);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_eagl_activate);
  context_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_swap_buffers);
  context_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_eagl_get_gl_platform);
}

static void
gst_gl_context_eagl_init (GstGLContextEagl * context)
{
  context->priv = GST_GL_CONTEXT_EAGL_GET_PRIVATE (context);
}

/* Must be called in the gl thread */
GstGLContextEagl *
gst_gl_context_eagl_new (GstGLDisplay * display)
{
  /* there isn't actually a display type for eagl yet? */
  return g_object_new (GST_TYPE_GL_CONTEXT_EAGL, NULL);
}

void
gst_gl_context_eagl_resize (GstGLContextEagl * eagl_context)
{
  int width, height;

  glBindRenderbuffer (GL_RENDERBUFFER, eagl_context->priv->color_renderbuffer);
  [GS_GL_CONTEXT_EAGL_CONTEXT(eagl_context) renderbufferStorage:GL_RENDERBUFFER fromDrawable:GS_GL_CONTEXT_EAGL_LAYER(eagl_context)];
  glGetRenderbufferParameteriv (GL_RENDERBUFFER,
      GL_RENDERBUFFER_WIDTH, &width);
  glGetRenderbufferParameteriv (GL_RENDERBUFFER,
      GL_RENDERBUFFER_HEIGHT, &height);
  glBindRenderbuffer (GL_RENDERBUFFER, eagl_context->priv->depth_renderbuffer);
  glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width,
      height);
}

static void
gst_gl_context_eagl_release_layer (GstGLContext * context)
{
  GstGLContextEagl *context_eagl;

  context_eagl = GST_GL_CONTEXT_EAGL (context);

  if (context_eagl->priv->eagl_layer) {
    gst_gl_context_eagl_activate (context, TRUE);

    [GS_GL_CONTEXT_EAGL_CONTEXT(context_eagl) renderbufferStorage: GL_RENDERBUFFER fromDrawable:nil];

    glDeleteFramebuffers (1, &context_eagl->priv->framebuffer);
    context_eagl->priv->framebuffer = 0;

    glDeleteRenderbuffers (1, &context_eagl->priv->depth_renderbuffer);
    context_eagl->priv->depth_renderbuffer = 0;
    glDeleteRenderbuffers (1, &context_eagl->priv->color_renderbuffer);
    context_eagl->priv->color_renderbuffer = 0;

    context_eagl->priv->eagl_layer = nil;
    gst_gl_context_eagl_activate (context, FALSE);
  }
}

void
gst_gl_context_eagl_update_layer (GstGLContext * context)
{
  GLuint framebuffer;
  GLuint color_renderbuffer;
  GLuint depth_renderbuffer;
  GLint width;
  GLint height;
  CAEAGLLayer *eagl_layer;
  GLenum status;
  GstGLContextEagl *context_eagl = GST_GL_CONTEXT_EAGL (context);
  GstGLContextEaglPrivate *priv = context_eagl->priv;
  UIView *window_handle = nil;
  GstGLWindow *window = gst_gl_context_get_window (context);
  if (window)
    window_handle = (__bridge UIView *)((void *)gst_gl_window_get_window_handle (window));

  if (!window_handle) {
    GST_INFO_OBJECT (context, "window handle not set yet, not updating layer");
    goto out;
  }

  GST_INFO_OBJECT (context, "updating layer, frame %fx%f",
      window_handle.frame.size.width, window_handle.frame.size.height);

  if (priv->eagl_layer)
    gst_gl_context_eagl_release_layer (context);

  eagl_layer = (CAEAGLLayer *)[window_handle layer];
  [EAGLContext setCurrentContext:GS_GL_CONTEXT_EAGL_CONTEXT(context_eagl)];

  /* Allocate framebuffer */
  glGenFramebuffers (1, &framebuffer);
  glBindFramebuffer (GL_FRAMEBUFFER, framebuffer);
  /* Allocate color render buffer */
  glGenRenderbuffers (1, &color_renderbuffer);
  glBindRenderbuffer (GL_RENDERBUFFER, color_renderbuffer);
  [GS_GL_CONTEXT_EAGL_CONTEXT(context_eagl) renderbufferStorage: GL_RENDERBUFFER fromDrawable:eagl_layer];
  glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_RENDERBUFFER, color_renderbuffer);
  /* Get renderbuffer width/height */
  glGetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,
      &width);
  glGetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT,
      &height);
  /* allocate depth render buffer */
  glGenRenderbuffers (1, &depth_renderbuffer);
  glBindRenderbuffer (GL_RENDERBUFFER, depth_renderbuffer);
  glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width,
      height);
  glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_RENDERBUFFER, depth_renderbuffer);

  /* check creation status */
  status = glCheckFramebufferStatus (GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    GST_ERROR ("Failed to make complete framebuffer object %x", status);
    goto out;
  }
  glBindRenderbuffer (GL_RENDERBUFFER, 0);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);

  priv->eagl_layer = (__bridge_retained gpointer)eagl_layer;
  priv->framebuffer = framebuffer;
  priv->color_renderbuffer = color_renderbuffer;
  priv->depth_renderbuffer = depth_renderbuffer;

out:
  if (window)
    gst_object_unref (window);
}

static gboolean
gst_gl_context_eagl_create_context (GstGLContext * context, GstGLAPI gl_api,
    GstGLContext * other_context, GError ** error)
{
  GstGLContextEagl *context_eagl = GST_GL_CONTEXT_EAGL (context);
  GstGLContextEaglPrivate *priv = context_eagl->priv;
  EAGLSharegroup *share_group;

  if (other_context) {
    EAGLContext *external_gl_context = (__bridge EAGLContext *)(void *)
        gst_gl_context_get_gl_context (other_context);
    share_group = [external_gl_context sharegroup];
  } else {
    share_group = nil;
  }

  priv->eagl_context = (__bridge_retained gpointer)[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3 sharegroup:share_group];
  if (!priv->eagl_context) {
    priv->eagl_context = (__bridge_retained gpointer)[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2 sharegroup:share_group];
  }
  if (!priv->eagl_context) {
    g_set_error_literal (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
        "Failed to create OpenGL ES context");
    return FALSE;
  }

  priv->eagl_layer = NULL;
  priv->framebuffer = 0;
  priv->color_renderbuffer = 0;
  priv->depth_renderbuffer = 0;

  GST_INFO_OBJECT (context, "context created, updating layer");
  gst_gl_context_eagl_update_layer (context);

  return TRUE;
}

static void
gst_gl_context_eagl_destroy_context (GstGLContext * context)
{
  GstGLContextEagl *context_eagl;

  context_eagl = GST_GL_CONTEXT_EAGL (context);

  if (!context_eagl->priv->eagl_context)
    return;

  gst_gl_context_eagl_release_layer (context);

  CFRelease(context_eagl->priv->eagl_context);
  context_eagl->priv->eagl_context = NULL;
}

static gboolean
gst_gl_context_eagl_choose_format (GstGLContext * context, GError ** error)
{
  GstGLContextEagl *context_eagl;
  GstGLWindow *window;
  UIView *window_handle = nil;

  context_eagl = GST_GL_CONTEXT_EAGL (context);
  window = gst_gl_context_get_window (context);

  if (!window)
    return TRUE;

  if (window)
    window_handle = (__bridge UIView *)(void *)gst_gl_window_get_window_handle (window);

  if (!window_handle) {
    gst_object_unref (window);
    return TRUE;
  }
  
  CAEAGLLayer *eagl_layer;
  NSDictionary * dict =[NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithBool:NO], kEAGLDrawablePropertyRetainedBacking,
      kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat, nil];

  eagl_layer = (CAEAGLLayer *)[window_handle layer];
  [eagl_layer setOpaque:YES];
  [eagl_layer setDrawableProperties:dict];

  gst_object_unref (window);

  return TRUE;
}

static guintptr
gst_gl_context_eagl_get_gl_context (GstGLContext * context)
{
  return (guintptr) GST_GL_CONTEXT_EAGL (context)->priv->eagl_context;
}

void
gst_gl_context_eagl_prepare_draw (GstGLContextEagl * context)
{
  if (!context->priv->eagl_layer)
    return;
  
  glBindFramebuffer (GL_FRAMEBUFFER, context->priv->framebuffer);
  glBindRenderbuffer (GL_RENDERBUFFER, context->priv->color_renderbuffer);
}

void
gst_gl_context_eagl_finish_draw (GstGLContextEagl * context)
{
  if (!context->priv->eagl_layer)
    return;

  glBindRenderbuffer (GL_RENDERBUFFER, 0);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

static void
gst_gl_context_eagl_swap_buffers (GstGLContext * context)
{
  GstGLContextEagl *context_eagl;

  context_eagl = GST_GL_CONTEXT_EAGL (context);

  if (!context_eagl->priv->eagl_layer)
    return;

  [GS_GL_CONTEXT_EAGL_CONTEXT(context_eagl) presentRenderbuffer:GL_RENDERBUFFER];
}

static gboolean
gst_gl_context_eagl_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextEagl *context_eagl;

  context_eagl = GST_GL_CONTEXT_EAGL (context);

  if (activate) {
    EAGLContext *cur_ctx =[EAGLContext currentContext];

    if (cur_ctx == context_eagl->priv->eagl_context) {
      GST_DEBUG ("Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG ("Attaching context to thread %p", g_thread_self ());
    if ([EAGLContext setCurrentContext:GS_GL_CONTEXT_EAGL_CONTEXT(context_eagl)] == NO) {
      GST_ERROR ("Couldn't make context current");
      return FALSE;
    }
  } else {
    GST_DEBUG ("Detaching context from thread %p", g_thread_self ());
    if ([EAGLContext setCurrentContext:nil] == NO) {
      GST_ERROR ("Couldn't unbind context");
      return FALSE;
    }
  }

  return TRUE;
}

static GstGLAPI
gst_gl_context_eagl_get_gl_api (GstGLContext * context)
{
  return GST_GL_API_GLES2;
}

static GstGLPlatform
gst_gl_context_eagl_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_EAGL;
}

guintptr
gst_gl_context_eagl_get_current_context (void)
{
  return (guintptr) [EAGLContext currentContext];
}
