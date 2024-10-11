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

#include <stdio.h>

#include <gst/video/video.h>
#include "qtitem.h"
#include "gstqsgmaterial.h"
#include "gstqtglutility.h"

#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>

/**
 * SECTION:QtGLVideoItem
 * @short_description: a Qt5 QtQuick item that renders GStreamer video #GstBuffers
 *
 * #QtGLVideoItem is an #QQuickItem that renders GStreamer video buffers.
 */

#define GST_CAT_DEFAULT qt_item_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
};

struct _QtGLVideoItemPrivate
{
  GMutex lock;

  /* properties */
  gboolean force_aspect_ratio;
  gint par_n, par_d;

  GWeakRef sink;

  gint display_width;
  gint display_height;

  GstBuffer *buffer;
  GstCaps *new_caps;
  GstCaps *caps;
  GstVideoInfo new_v_info;
  GstGLTextureTarget new_tex_target;

  GstVideoInfo v_info;
  GstGLTextureTarget tex_target;
  GstVideoRectangle v_rect;

  gboolean initted;
  GstGLDisplay *display;
  QOpenGLContext *qt_context;
  GstGLContext *other_context;
  GstGLContext *context;

  /* buffers with textures that were bound by QML */
  GQueue bound_buffers;
  /* buffers that were previously bound but in the meantime a new one was
   * bound so this one is most likely not used anymore
   * FIXME: Ideally we would use fences for this but there seems to be no
   * way to reliably "try wait" on a fence */
  GQueue potentially_unbound_buffers;
};

QtGLVideoItem::QtGLVideoItem()
{
  static gsize _debug;

  if (g_once_init_enter (&_debug)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "qtglwidget", 0, "Qt GL Widget");
    g_once_init_leave (&_debug, 1);
  }

  this->setFlag (QQuickItem::ItemHasContents, true);

  this->priv = g_new0 (QtGLVideoItemPrivate, 1);

  this->priv->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  this->priv->par_n = DEFAULT_PAR_N;
  this->priv->par_d = DEFAULT_PAR_D;

  this->priv->initted = FALSE;

  g_mutex_init (&this->priv->lock);

  g_weak_ref_init (&priv->sink, NULL);

  this->priv->display = gst_qt_get_gl_display(TRUE);

  connect(this, SIGNAL(windowChanged(QQuickWindow*)), this,
          SLOT(handleWindowChanged(QQuickWindow*)));

  this->proxy = QSharedPointer<QtGLVideoItemInterface>(new QtGLVideoItemInterface(this));

  setFlag(ItemHasContents, true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
  setAcceptTouchEvents(true);
#else
  GST_INFO ("Qt version is below 5.10, touchscreen events will not work");
#endif

  GST_DEBUG ("%p init Qt Video Item", this);
}

QtGLVideoItem::~QtGLVideoItem()
{
  GstBuffer *tmp_buffer;

  /* Before destroying the priv info, make sure
   * no qmlglsink's will call in again, and that
   * any ongoing calls are done by invalidating the proxy
   * pointer */
  GST_INFO ("%p Destroying QtGLVideoItem and invalidating the proxy %p", this, proxy.data());
  proxy->invalidateRef();
  proxy.clear();

  g_mutex_clear (&this->priv->lock);
  if (this->priv->context)
    gst_object_unref(this->priv->context);
  if (this->priv->other_context)
    gst_object_unref(this->priv->other_context);
  if (this->priv->display)
    gst_object_unref(this->priv->display);

  while ((tmp_buffer = (GstBuffer*) g_queue_pop_head (&this->priv->potentially_unbound_buffers))) {
    GST_TRACE ("old buffer %p should be unbound now, unreffing", tmp_buffer);
    gst_buffer_unref (tmp_buffer);
  }
  while ((tmp_buffer = (GstBuffer*) g_queue_pop_head (&this->priv->bound_buffers))) {
    GST_TRACE ("old buffer %p should be unbound now, unreffing", tmp_buffer);
    gst_buffer_unref (tmp_buffer);
  }

  gst_buffer_replace (&this->priv->buffer, NULL);

  gst_caps_replace (&this->priv->caps, NULL);
  gst_caps_replace (&this->priv->new_caps, NULL);

  g_weak_ref_clear (&this->priv->sink);

  g_free (this->priv);
  this->priv = NULL;
}

void
QtGLVideoItem::setDAR(gint num, gint den)
{
  this->priv->par_n = num;
  this->priv->par_d = den;
}

void
QtGLVideoItem::getDAR(gint * num, gint * den)
{
  if (num)
    *num = this->priv->par_n;
  if (den)
    *den = this->priv->par_d;
}

void
QtGLVideoItem::setForceAspectRatio(bool force_aspect_ratio)
{
  this->priv->force_aspect_ratio = !!force_aspect_ratio;

  emit forceAspectRatioChanged(force_aspect_ratio);
}

bool
QtGLVideoItem::getForceAspectRatio()
{
  return this->priv->force_aspect_ratio;
}

bool
QtGLVideoItem::itemInitialized()
{
  return this->priv->initted;
}

static gboolean
_calculate_par (QtGLVideoItem * widget, GstVideoInfo * info)
{
  gboolean ok;
  gint width, height;
  gint par_n, par_d;
  gint display_par_n, display_par_d;
  guint display_ratio_num, display_ratio_den;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  par_n = GST_VIDEO_INFO_PAR_N (info);
  par_d = GST_VIDEO_INFO_PAR_D (info);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (widget->priv->par_n != 0 && widget->priv->par_d != 0) {
    display_par_n = widget->priv->par_n;
    display_par_d = widget->priv->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  widget->setImplicitWidth (width);
  widget->setImplicitHeight (height);

  GST_LOG ("%p PAR: %u/%u DAR:%u/%u", widget, par_n, par_d, display_par_n,
      display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("%p keeping video height", widget);
    widget->priv->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->priv->display_height = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("%p keeping video width", widget);
    widget->priv->display_width = width;
    widget->priv->display_height = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("%p approximating while keeping video height", widget);
    widget->priv->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->priv->display_height = height;
  }
  GST_DEBUG ("%p scaling to %dx%d", widget, widget->priv->display_width,
      widget->priv->display_height);

  return TRUE;
}

QSGNode *
QtGLVideoItem::updatePaintNode(QSGNode * oldNode,
    UpdatePaintNodeData * updatePaintNodeData)
{
  GstBuffer *old_buffer;
  gboolean was_bound = FALSE;

  if (!this->priv->initted)
    return oldNode;

  QSGGeometryNode *texNode = static_cast<QSGGeometryNode *> (oldNode);
  GstVideoRectangle src, dst, result;
  GstQSGMaterial *tex = nullptr;
  QSGGeometry *geometry = nullptr;

  g_mutex_lock (&this->priv->lock);

  GST_TRACE ("%p updatePaintNode", this);

  if (!this->priv->caps) {
    GST_LOG ("%p no caps yet", this);
    g_mutex_unlock (&this->priv->lock);
    return NULL;
  }

  if (gst_gl_context_get_current() == NULL)
    gst_gl_context_activate (this->priv->other_context, TRUE);

  if (texNode) {
    geometry = texNode->geometry();
    tex = static_cast<GstQSGMaterial *>(texNode->material());
    if (tex && !tex->compatibleWith(&this->priv->v_info, this->priv->tex_target)) {
      delete texNode;
      texNode = nullptr;
    }
  }

  if (!texNode) {
    texNode = new QSGGeometryNode();
    geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    texNode->setGeometry(geometry);
    texNode->setFlag(QSGGeometryNode::Flag::OwnsGeometry);
    tex = GstQSGMaterial::new_for_format_and_target (GST_VIDEO_INFO_FORMAT (&this->priv->v_info), this->priv->tex_target);
    texNode->setMaterial(tex);
    texNode->setFlag(QSGGeometryNode::Flag::OwnsMaterial);
  }

  if ((old_buffer = tex->getBuffer(&was_bound))) {
    if (old_buffer == this->priv->buffer) {
      /* same buffer */
      gst_buffer_unref (old_buffer);
    } else if (!was_bound) {
      GST_TRACE ("old buffer %p was not bound yet, unreffing", old_buffer);
      gst_buffer_unref (old_buffer);
    } else {
      GstBuffer *tmp_buffer;

      GST_TRACE ("old buffer %p was bound, queueing up for later", old_buffer);
      /* Unref all buffers that were previously not bound anymore. At least
       * one more buffer was bound in the meantime so this one is most likely
       * not in use anymore. */
      while ((tmp_buffer = (GstBuffer*) g_queue_pop_head (&this->priv->potentially_unbound_buffers))) {
        GST_TRACE ("old buffer %p should be unbound now, unreffing", tmp_buffer);
        gst_buffer_unref (tmp_buffer);
      }

      /* Move previous bound buffers to the next queue. We now know that
       * another buffer was bound in the meantime and will free them on
       * the next iteration above. */
      while ((tmp_buffer = (GstBuffer*) g_queue_pop_head (&this->priv->bound_buffers))) {
        GST_TRACE ("old buffer %p is potentially unbound now", tmp_buffer);
        g_queue_push_tail (&this->priv->potentially_unbound_buffers, tmp_buffer);
      }
      g_queue_push_tail (&this->priv->bound_buffers, old_buffer);
    }
    old_buffer = NULL;
  }

  tex->setCaps (this->priv->caps);
  tex->setBuffer (this->priv->buffer);
  texNode->markDirty(QSGNode::DirtyMaterial);

  if (this->priv->force_aspect_ratio) {
    src.w = this->priv->display_width;
    src.h = this->priv->display_height;

    dst.x = boundingRect().x();
    dst.y = boundingRect().y();
    dst.w = boundingRect().width();
    dst.h = boundingRect().height();

    gst_video_sink_center_rect (src, dst, &result, TRUE);
  } else {
    result.x = boundingRect().x();
    result.y = boundingRect().y();
    result.w = boundingRect().width();
    result.h = boundingRect().height();
  }

  QRectF rect(result.x, result.y, result.w, result.h);
  QRectF sourceRect(0, 0, 1, 1);
  QSGGeometry::updateTexturedRectGeometry(geometry, rect, sourceRect);
  if(priv->v_rect.x != result.x || priv->v_rect.y != result.y ||
     priv->v_rect.w != result.w || priv->v_rect.h != result.h) {
    texNode->markDirty(QSGNode::DirtyGeometry);
    priv->v_rect = result;
  }

  g_mutex_unlock (&this->priv->lock);

  return texNode;
}

/* This method has to be invoked with the the priv->lock taken */
void
QtGLVideoItem::fitStreamToAllocatedSize(GstVideoRectangle * result)
{
  if (this->priv->force_aspect_ratio) {
    GstVideoRectangle src, dst;

    src.x = 0;
    src.y = 0;
    src.w = this->priv->display_width;
    src.h = this->priv->display_height;

    dst.x = 0;
    dst.y = 0;
    dst.w = width();
    dst.h = height();

    gst_video_sink_center_rect (src, dst, result, TRUE);
  } else {
    result->x = 0;
    result->y = 0;
    result->w = width();
    result->h = height();
  }
}

/* This method has to be invoked with the the priv->lock taken */
QPointF
QtGLVideoItem::mapPointToStreamSize(QPointF pos)
{
  gdouble stream_width, stream_height;
  GstVideoRectangle result;
  double stream_x, stream_y;
  double x, y;

  fitStreamToAllocatedSize(&result);

  stream_width = (gdouble) GST_VIDEO_INFO_WIDTH (&this->priv->v_info);
  stream_height = (gdouble) GST_VIDEO_INFO_HEIGHT (&this->priv->v_info);
  x = pos.x();
  y = pos.y();

  /* from display coordinates to stream coordinates */
  if (result.w > 0)
    stream_x = (x - result.x) / result.w * stream_width;
  else
    stream_x = 0.;

  /* clip to stream size */
  stream_x = CLAMP(stream_x, 0., stream_width);

  /* same for y-axis */
  if (result.h > 0)
    stream_y = (y - result.y) / result.h * stream_height;
  else
    stream_y = 0.;

  stream_y = CLAMP(stream_y, 0., stream_height);
  GST_TRACE ("transform %fx%f into %fx%f", x, y, stream_x, stream_y);

  return QPointF(stream_x, stream_y);
}

static GstNavigationModifierType
translateModifiers(Qt::KeyboardModifiers modifiers)
{
  return (GstNavigationModifierType)(
    ((modifiers & Qt::KeyboardModifier::ShiftModifier) ? GST_NAVIGATION_MODIFIER_SHIFT_MASK : 0) |
    ((modifiers & Qt::KeyboardModifier::ControlModifier) ? GST_NAVIGATION_MODIFIER_CONTROL_MASK : 0) |
    ((modifiers & Qt::KeyboardModifier::AltModifier) ? GST_NAVIGATION_MODIFIER_MOD1_MASK : 0) |
    ((modifiers & Qt::KeyboardModifier::MetaModifier) ? GST_NAVIGATION_MODIFIER_META_MASK : 0));
}

static GstNavigationModifierType
translateMouseButtons(Qt::MouseButtons buttons)
{
  return (GstNavigationModifierType)(
    ((buttons & Qt::LeftButton) ? GST_NAVIGATION_MODIFIER_BUTTON1_MASK : 0) |
    ((buttons & Qt::RightButton) ? GST_NAVIGATION_MODIFIER_BUTTON2_MASK : 0) |
    ((buttons & Qt::MiddleButton) ? GST_NAVIGATION_MODIFIER_BUTTON3_MASK : 0) |
    ((buttons & Qt::BackButton) ? GST_NAVIGATION_MODIFIER_BUTTON4_MASK : 0) |
    ((buttons & Qt::ForwardButton) ? GST_NAVIGATION_MODIFIER_BUTTON5_MASK : 0));
}

void
QtGLVideoItem::wheelEvent(QWheelEvent * event)
{
  g_mutex_lock (&this->priv->lock);
  QPoint delta = event->angleDelta();
  GstElement *element = GST_ELEMENT_CAST (g_weak_ref_get (&this->priv->sink));

  if (element != NULL) {
#if (QT_VERSION >= QT_VERSION_CHECK (5, 14, 0))
    auto position = event->position();
#else
    auto position = *event;
#endif
    gst_navigation_send_event_simple (GST_NAVIGATION (element),
        gst_navigation_event_new_mouse_scroll (position.x(), position.y(),
                                               delta.x(), delta.y(),
                                               (GstNavigationModifierType) (
                                                 translateModifiers(event->modifiers()) | translateMouseButtons(event->buttons()))));
    g_object_unref (element);
  }
  g_mutex_unlock (&this->priv->lock);
}

void
QtGLVideoItem::hoverEnterEvent(QHoverEvent *)
{
  mouseHovering = true;
}

void
QtGLVideoItem::hoverLeaveEvent(QHoverEvent *)
{
  mouseHovering = false;
}

void
QtGLVideoItem::hoverMoveEvent(QHoverEvent * event)
{
  if (!mouseHovering)
    return;

  g_mutex_lock (&this->priv->lock);

  /* can't do anything when we don't have input format */
  if (!this->priv->caps) {
    g_mutex_unlock (&this->priv->lock);
    return;
  }

  if (event->pos() != event->oldPos()) {
    QPointF pos = mapPointToStreamSize(event->pos());
    GstElement *element = GST_ELEMENT_CAST (g_weak_ref_get (&this->priv->sink));

    if (element != NULL) {
      gst_navigation_send_event_simple (GST_NAVIGATION (element),
          gst_navigation_event_new_mouse_move (pos.x(), pos.y(),
                                               translateModifiers(event->modifiers())));
      g_object_unref (element);
    }
  }
  g_mutex_unlock (&this->priv->lock);
}

void
QtGLVideoItem::touchEvent(QTouchEvent * event)
{
  g_mutex_lock (&this->priv->lock);

  /* can't do anything when we don't have input format */
  if (!this->priv->caps) {
    g_mutex_unlock (&this->priv->lock);
    return;
  }

  GstElement *element = GST_ELEMENT_CAST (g_weak_ref_get (&this->priv->sink));
  if (element == NULL)
    return;

  if (event->type() == QEvent::TouchCancel) {
    gst_navigation_send_event_simple (GST_NAVIGATION (element),
        gst_navigation_event_new_touch_cancel (translateModifiers(event->modifiers())));
  } else {
    const QList<QTouchEvent::TouchPoint> points = event->touchPoints();
    gboolean sent_event = FALSE;

    for (int i = 0; i < points.count(); i++) {
      GstEvent *nav_event;
      QPointF pos = mapPointToStreamSize(points[i].pos());

      switch (points[i].state()) {
        case Qt::TouchPointPressed:
          nav_event = gst_navigation_event_new_touch_down ((guint) points[i].id(),
              pos.x(), pos.y(), (gdouble) points[i].pressure(), translateModifiers(event->modifiers()));
          break;
        case Qt::TouchPointMoved:
          nav_event = gst_navigation_event_new_touch_motion ((guint) points[i].id(),
              pos.x(), pos.y(), (gdouble) points[i].pressure(), translateModifiers(event->modifiers()));
          break;
        case Qt::TouchPointReleased:
          nav_event = gst_navigation_event_new_touch_up ((guint) points[i].id(),
              pos.x(), pos.y(), translateModifiers(event->modifiers()));
          break;
        /* Don't send an event if the point did not change */
        default:
          nav_event = NULL;
          break;
      }

      if (nav_event) {
        gst_navigation_send_event_simple (GST_NAVIGATION (element), nav_event);
        sent_event = TRUE;
      }
    }

    /* Group simultaneos touch events with a frame event */
    if (sent_event) {
      gst_navigation_send_event_simple (GST_NAVIGATION (element),
          gst_navigation_event_new_touch_frame (translateModifiers(event->modifiers())));
    }
  }

  g_object_unref (element);
  g_mutex_unlock (&this->priv->lock);
}

void
QtGLVideoItem::sendMouseEvent(QMouseEvent * event, gboolean is_press)
{
  quint32 button = 0;

  switch (event->button()) {
  case Qt::LeftButton:
    button = 1;
    break;
  case Qt::RightButton:
    button = 2;
    break;
  default:
    break;
  }

  mousePressedButton = button;

  g_mutex_lock (&this->priv->lock);

  /* can't do anything when we don't have input format */
  if (!this->priv->caps) {
    g_mutex_unlock (&this->priv->lock);
    return;
  }

  QPointF pos = mapPointToStreamSize(event->pos());
  GstElement *element = GST_ELEMENT_CAST (g_weak_ref_get (&this->priv->sink));

  if (element != NULL) {
    gst_navigation_send_event_simple (GST_NAVIGATION (element),
        (is_press) ? gst_navigation_event_new_mouse_button_press (button,
                pos.x(), pos.y(),
                (GstNavigationModifierType) (
                  translateModifiers(event->modifiers()) | translateMouseButtons(event->buttons()))) :
            gst_navigation_event_new_mouse_button_release (button, pos.x(),
                pos.y(),
                (GstNavigationModifierType) (
                  translateModifiers(event->modifiers()) | translateMouseButtons(event->buttons()))));
    g_object_unref (element);
  }

  g_mutex_unlock (&this->priv->lock);
}

void
QtGLVideoItem::mousePressEvent(QMouseEvent * event)
{
  forceActiveFocus();
  sendMouseEvent(event, TRUE);
}

void
QtGLVideoItem::mouseReleaseEvent(QMouseEvent * event)
{
  sendMouseEvent(event, FALSE);
}

void
QtGLVideoItemInterface::setSink (GstElement * sink)
{
  QMutexLocker locker(&lock);
  if (qt_item == NULL)
    return;

  g_mutex_lock (&qt_item->priv->lock);
  g_weak_ref_set (&qt_item->priv->sink, sink);
  g_mutex_unlock (&qt_item->priv->lock);
}

void
QtGLVideoItemInterface::setBuffer (GstBuffer * buffer)
{
  QMutexLocker locker(&lock);

  if (qt_item == NULL) {
    GST_WARNING ("%p actual item is NULL. setBuffer call ignored", this);
    return;
  }

  if (!qt_item->priv->caps && !qt_item->priv->new_caps) {
    GST_WARNING ("%p Got buffer on unnegotiated QtGLVideoItem. Dropping", this);
    return;
  }

  g_mutex_lock (&qt_item->priv->lock);

  if (qt_item->priv->new_caps) {
    GST_DEBUG ("%p caps change from %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
        this, qt_item->priv->caps, qt_item->priv->new_caps);
    gst_caps_take (&qt_item->priv->caps, qt_item->priv->new_caps);
    qt_item->priv->new_caps = NULL;
    qt_item->priv->v_info = qt_item->priv->new_v_info;
    qt_item->priv->tex_target = qt_item->priv->new_tex_target;

    if (!_calculate_par (qt_item, &qt_item->priv->v_info)) {
      g_mutex_unlock (&qt_item->priv->lock);
      return;
    }
  }

  gst_buffer_replace (&qt_item->priv->buffer, buffer);

  QMetaObject::invokeMethod(qt_item, "update", Qt::QueuedConnection);

  g_mutex_unlock (&qt_item->priv->lock);
}

void
QtGLVideoItem::onSceneGraphInitialized ()
{
  if (this->window() == NULL)
    return;

  GST_DEBUG ("%p scene graph initialization with Qt GL context %p", this,
      this->window()->openglContext ());

  if (this->priv->qt_context == this->window()->openglContext ())
    return;

  this->priv->qt_context = this->window()->openglContext ();
  if (this->priv->qt_context == NULL) {
    g_assert_not_reached ();
    return;
  }

  this->priv->initted = gst_qt_get_gl_wrapcontext (this->priv->display,
      &this->priv->other_context, &this->priv->context);

  GST_DEBUG ("%p created wrapped GL context %" GST_PTR_FORMAT, this,
      this->priv->other_context);

  emit itemInitializedChanged();
}

void
QtGLVideoItem::onSceneGraphInvalidated ()
{
  GST_FIXME ("%p scene graph invalidated", this);
}

/*
 * Retrieve and populate the GL context information from the current
 * OpenGL context.
 */
gboolean
QtGLVideoItemInterface::initWinSys ()
{
  QMutexLocker locker(&lock);

  GError *error = NULL;

  if (qt_item == NULL)
    return FALSE;

  g_mutex_lock (&qt_item->priv->lock);

  if (qt_item->priv->display && qt_item->priv->qt_context
      && qt_item->priv->other_context && qt_item->priv->context) {
    /* already have the necessary state */
    g_mutex_unlock (&qt_item->priv->lock);
    return TRUE;
  }

  if (!GST_IS_GL_DISPLAY (qt_item->priv->display)) {
    GST_ERROR ("%p failed to retrieve display connection %" GST_PTR_FORMAT,
        qt_item, qt_item->priv->display);
    g_mutex_unlock (&qt_item->priv->lock);
    return FALSE;
  }

  if (!GST_IS_GL_CONTEXT (qt_item->priv->other_context)) {
    GST_ERROR ("%p failed to retrieve wrapped context %" GST_PTR_FORMAT, qt_item,
        qt_item->priv->other_context);
    g_mutex_unlock (&qt_item->priv->lock);
    return FALSE;
  }

  qt_item->priv->context = gst_gl_context_new (qt_item->priv->display);

  if (!qt_item->priv->context) {
    g_mutex_unlock (&qt_item->priv->lock);
    return FALSE;
  }

  if (!gst_gl_context_create (qt_item->priv->context, qt_item->priv->other_context,
        &error)) {
    GST_ERROR ("%s", error->message);
    g_mutex_unlock (&qt_item->priv->lock);
    return FALSE;
  }

  g_mutex_unlock (&qt_item->priv->lock);
  return TRUE;
}

void
QtGLVideoItem::handleWindowChanged (QQuickWindow * win)
{
  if (win) {
    if (win->isSceneGraphInitialized ())
      win->scheduleRenderJob (new RenderJob (std::
              bind (&QtGLVideoItem::onSceneGraphInitialized, this)),
          QQuickWindow::BeforeSynchronizingStage);
    else
      connect (win, SIGNAL (sceneGraphInitialized ()), this,
          SLOT (onSceneGraphInitialized ()), Qt::DirectConnection);

    connect (win, SIGNAL (sceneGraphInvalidated ()), this,
        SLOT (onSceneGraphInvalidated ()), Qt::DirectConnection);
  } else {
    this->priv->qt_context = NULL;
    this->priv->initted = FALSE;
  }
}

gboolean
QtGLVideoItemInterface::setCaps (GstCaps * caps)
{
  QMutexLocker locker(&lock);
  GstVideoInfo v_info;

  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  if (qt_item == NULL)
    return FALSE;

  if (qt_item->priv->caps && gst_caps_is_equal_fixed (qt_item->priv->caps, caps))
    return TRUE;

  if (!gst_video_info_from_caps (&v_info, caps))
    return FALSE;

  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *target_str = gst_structure_get_string (s, "texture-target");

  g_mutex_lock (&qt_item->priv->lock);

  GST_DEBUG ("%p set caps %" GST_PTR_FORMAT, qt_item, caps);

  gst_caps_replace (&qt_item->priv->new_caps, caps);

  qt_item->priv->new_v_info = v_info;
  if (target_str) {
    qt_item->priv->new_tex_target = gst_gl_texture_target_from_string(target_str);
  } else {
    qt_item->priv->new_tex_target = GST_GL_TEXTURE_TARGET_2D;
  }

  g_mutex_unlock (&qt_item->priv->lock);

  return TRUE;
}

GstGLContext *
QtGLVideoItemInterface::getQtContext ()
{
  QMutexLocker locker(&lock);

  if (!qt_item || !qt_item->priv->other_context)
    return NULL;

  return (GstGLContext *) gst_object_ref (qt_item->priv->other_context);
}

GstGLContext *
QtGLVideoItemInterface::getContext ()
{
  QMutexLocker locker(&lock);

  if (!qt_item || !qt_item->priv->context)
    return NULL;

  return (GstGLContext *) gst_object_ref (qt_item->priv->context);
}

GstGLDisplay *
QtGLVideoItemInterface::getDisplay()
{
  QMutexLocker locker(&lock);

  if (!qt_item || !qt_item->priv->display)
    return NULL;

  return (GstGLDisplay *) gst_object_ref (qt_item->priv->display);
}

void
QtGLVideoItemInterface::setDAR(gint num, gint den)
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return;
  qt_item->setDAR(num, den);
}

void
QtGLVideoItemInterface::getDAR(gint * num, gint * den)
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return;
  qt_item->getDAR (num, den);
}

void
QtGLVideoItemInterface::setForceAspectRatio(bool force_aspect_ratio)
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return;
  qt_item->setForceAspectRatio(force_aspect_ratio);
}

bool
QtGLVideoItemInterface::getForceAspectRatio()
{
  QMutexLocker locker(&lock);
  if (!qt_item)
    return FALSE;
  return qt_item->getForceAspectRatio();
}

void
QtGLVideoItemInterface::invalidateRef()
{
  QMutexLocker locker(&lock);
  qt_item = NULL;
}

