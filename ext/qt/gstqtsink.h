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

#ifndef __GST_QT_SINK_H__
#define __GST_QT_SINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include "qtitem.h"

typedef struct _GstQtSinkPrivate GstQtSinkPrivate;

G_BEGIN_DECLS

#define GST_TYPE_QT_SINK (gst_qt_sink_get_type())
G_DECLARE_FINAL_TYPE (GstQtSink, gst_qt_sink, GST, QT_SINK, GstVideoSink)
#define GST_QT_SINK_CAST(obj) ((GstQtSink*)(obj))

/**
 * GstQtSink:
 *
 * Opaque #GstQtSink object
 */
struct _GstQtSink
{
  /* <private> */
  GstVideoSink          parent;

  GstVideoInfo          v_info;
  GstBufferPool        *pool;

  GstGLDisplay         *display;
  GstGLContext         *context;
  GstGLContext         *qt_context;

  QSharedPointer<QtGLVideoItemInterface> widget;
};

GstQtSink *    gst_qt_sink_new (void);

G_END_DECLS

#endif /* __GST_QT_SINK_H__ */
