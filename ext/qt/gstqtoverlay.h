/*
 * GStreamer
 * Copyright (C) 2020 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_QT_OVERLAY_H__
#define __GST_QT_OVERLAY_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include "qtglrenderer.h"
#include "qtitem.h"

typedef struct _GstQtOverlay GstQtOverlay;
typedef struct _GstQtOverlayClass GstQtOverlayClass;
typedef struct _GstQtOverlayPrivate GstQtOverlayPrivate;

G_BEGIN_DECLS

GType gst_qt_overlay_get_type (void);
#define GST_TYPE_QT_OVERLAY            (gst_qt_overlay_get_type())
#define GST_QT_OVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QT_OVERLAY,GstQtOverlay))
#define GST_QT_OVERLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QT_OVERLAY,GstQtOverlayClass))
#define GST_IS_QT_OVERLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QT_OVERLAY))
#define GST_IS_QT_OVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QT_OVERLAY))
#define GST_QT_OVERLAY_CAST(obj)       ((GstQtOverlay*)(obj))

/**
 * GstQtOverlay:
 *
 * Opaque #GstQtOverlay object
 */
struct _GstQtOverlay
{
  /* <private> */
  GstGLFilter           parent;

  gchar                *qml_scene;

  GstQuickRenderer     *renderer;

  QSharedPointer<QtGLVideoItemInterface> widget;
};

/**
 * GstQtOverlayClass:
 *
 * The #GstQtOverlayClass struct only contains private data
 */
struct _GstQtOverlayClass
{
  /* <private> */
  GstGLFilterClass parent_class;
};

GstQtOverlay *    gst_qt_overlay_new (void);

G_END_DECLS

#endif /* __GST_QT_OVERLAY_H__ */
