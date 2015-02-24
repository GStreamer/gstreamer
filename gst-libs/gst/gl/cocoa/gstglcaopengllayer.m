/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include "gstglcaopengllayer.h"
#include "gstgl_cocoa_private.h"

@implementation GstGLCAOpenGLLayer
- (void)dealloc {
  if (self->draw_notify)
    self->draw_notify (self->draw_data);

  GST_TRACE ("dealloc GstGLCAOpenGLLayer %p context %p", self, self->gst_gl_context);

  [super dealloc];
}

static void
_context_ready (gpointer data)
{
  GstGLCAOpenGLLayer *ca_layer = data;

  g_atomic_int_set (&ca_layer->can_draw, 1);
}

- (id)initWithGstGLContext:(GstGLContextCocoa *)parent_gl_context {
  [super init];

  GST_LOG ("init CAOpenGLLayer");

  self->gst_gl_context = parent_gl_context;
  self.needsDisplayOnBoundsChange = YES;

  gst_gl_window_send_message_async (GST_GL_CONTEXT (parent_gl_context)->window,
      (GstGLWindowCB) _context_ready, self, NULL);

  return self;
}

- (CGLPixelFormatObj)copyCGLPixelFormatForDisplayMask:(uint32_t)mask {
  CGLPixelFormatObj fmt = NULL;

  if (self->gst_gl_context)
    fmt = gst_gl_context_cocoa_get_pixel_format (self->gst_gl_context);

  if (!fmt) {
    CGLPixelFormatAttribute attribs[] = {
      kCGLPFADoubleBuffer,
      kCGLPFAAccumSize, 32,
      0
    };
    CGLError ret;
    gint npix = 0;

    GST_DEBUG ("creating new pixel format for CAOpenGLLayer %p", self);

    ret = CGLChoosePixelFormat (attribs, &fmt, &npix);
    if (ret != kCGLNoError) {
      GST_ERROR ("CAOpenGLLayer cannot choose a pixel format: %s", CGLErrorString (ret));
    }
  }

  return fmt;
}

- (CGLContextObj)copyCGLContextForPixelFormat:(CGLPixelFormatObj)pixelFormat {
  CGLContextObj external_context = NULL;
  CGLError ret;

  if (self->gst_gl_context)
    external_context = (CGLContextObj) gst_gl_context_get_gl_context (GST_GL_CONTEXT (self->gst_gl_context));

  GST_INFO ("attempting to create CGLContext for CAOpenGLLayer with "
      "share context %p", external_context);

  ret = CGLCreateContext (pixelFormat, external_context, &self->gl_context);
  if (ret != kCGLNoError) {
    GST_ERROR ("failed to create CGL context in CAOpenGLLayer with share context %p: %s", external_context, CGLErrorString(ret));
  }

  return self->gl_context;
}

- (void)releaseCGLContext:(CGLContextObj)glContext {
  CGLReleaseContext (glContext);
}

- (void)setDrawCallback:(GstGLWindowCB)cb data:(gpointer)data
      notify:(GDestroyNotify)notify {
  g_return_if_fail (cb);

  if (self->draw_notify)
    self->draw_notify (self->draw_data);

  self->draw_cb = cb;
  self->draw_data = data;
  self->draw_notify = notify;
}

- (void)setResizeCallback:(GstGLWindowResizeCB)cb data:(gpointer)data
      notify:(GDestroyNotify)notify {
  if (self->resize_notify)
    self->resize_notify (self->resize_notify);

  self->resize_cb = cb;
  self->resize_data = data;
  self->resize_notify = notify;
}

- (BOOL)canDrawInCGLContext:(CGLContextObj)glContext
               pixelFormat:(CGLPixelFormatObj)pixelFormat
            forLayerTime:(CFTimeInterval)interval
             displayTime:(const CVTimeStamp *)timeStamp {
  return g_atomic_int_get (&self->can_draw);
}

- (void)drawInCGLContext:(CGLContextObj)glContext
               pixelFormat:(CGLPixelFormatObj)pixelFormat
            forLayerTime:(CFTimeInterval)interval
             displayTime:(const CVTimeStamp *)timeStamp {
  const GstGLFuncs *gl = ((GstGLContext *)self->gst_gl_context)->gl_vtable;
  GstVideoRectangle src, dst, result;
  gint ca_viewport[4];

  GST_LOG ("CAOpenGLLayer drawing with cgl context %p", glContext);

  /* attempt to get the correct viewport back due to CA being too smart
   * and messing around with it so center the expected viewport into
   * the CA viewport set up on entry to this function */
  gl->GetIntegerv (GL_VIEWPORT, ca_viewport);

  if (self->last_bounds.size.width != self.bounds.size.width
      || self->last_bounds.size.height != self.bounds.size.height) {
    if (self->resize_cb) {
      self->resize_cb (self->resize_data, self.bounds.size.width,
          self.bounds.size.height);

      gl->GetIntegerv (GL_VIEWPORT, self->expected_dims);
    } else {
      /* default to whatever ca gives us */
      self->expected_dims[0] = ca_viewport[0];
      self->expected_dims[1] = ca_viewport[1];
      self->expected_dims[2] = ca_viewport[2];
      self->expected_dims[3] = ca_viewport[3];
    }

    self->last_bounds = self.bounds;
  }

  src.x = self->expected_dims[0];
  src.y = self->expected_dims[1];
  src.w = self->expected_dims[2];
  src.h = self->expected_dims[3];

  dst.x = ca_viewport[0];
  dst.y = ca_viewport[1];
  dst.w = ca_viewport[2];
  dst.h = ca_viewport[3];

  gst_video_sink_center_rect (src, dst, &result, TRUE);

  gl->Viewport (result.x, result.y, result.w, result.h);

  if (self->draw_cb)
    self->draw_cb (self->draw_data);

  /* flushes the buffer */
  [super drawInCGLContext:glContext pixelFormat:pixelFormat forLayerTime:interval displayTime:timeStamp];
}

@end
