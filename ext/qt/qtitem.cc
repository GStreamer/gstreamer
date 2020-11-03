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
#include "gstqsgtexture.h"
#include "gstqtglutility.h"

#include <QtCore/QRunnable>
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>

/**
 * SECTION:gtkgstglwidget
 * @short_description: a #GtkGLArea that renders GStreamer video #GstBuffers
 * @see_also: #GtkGLArea, #GstBuffer
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

  gint display_width;
  gint display_height;

  gboolean negotiated;
  GstBuffer *buffer;
  GstCaps *caps;
  GstVideoInfo v_info;

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

class InitializeSceneGraph : public QRunnable
{
public:
  InitializeSceneGraph(QtGLVideoItem *item);
  void run();

private:
  QPointer<QtGLVideoItem> item_;
};

InitializeSceneGraph::InitializeSceneGraph(QtGLVideoItem *item) :
  item_(item)
{
}

void InitializeSceneGraph::run()
{
  if(item_)
    item_->onSceneGraphInitialized();
}

QtGLVideoItem::QtGLVideoItem()
{
  static volatile gsize _debug;

  if (g_once_init_enter (&_debug)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "qtglwidget", 0, "Qt GL Widget");
    g_once_init_leave (&_debug, 1);
  }
  this->m_openGlContextInitialized = false;
  this->setFlag (QQuickItem::ItemHasContents, true);

  this->priv = g_new0 (QtGLVideoItemPrivate, 1);

  this->priv->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  this->priv->par_n = DEFAULT_PAR_N;
  this->priv->par_d = DEFAULT_PAR_D;

  g_mutex_init (&this->priv->lock);

  this->priv->display = gst_qt_get_gl_display();

  connect(this, SIGNAL(windowChanged(QQuickWindow*)), this,
          SLOT(handleWindowChanged(QQuickWindow*)));

  this->proxy = QSharedPointer<QtGLVideoItemInterface>(new QtGLVideoItemInterface(this));

  GST_DEBUG ("%p init Qt Video Item", this);
}

QtGLVideoItem::~QtGLVideoItem()
{
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

  gst_buffer_replace (&this->priv->buffer, NULL);

  gst_caps_replace (&this->priv->caps, NULL);
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
}

bool
QtGLVideoItem::getForceAspectRatio()
{
  return this->priv->force_aspect_ratio;
}

bool
QtGLVideoItem::itemInitialized()
{
  return m_openGlContextInitialized;
}

QSGNode *
QtGLVideoItem::updatePaintNode(QSGNode * oldNode,
    UpdatePaintNodeData * updatePaintNodeData)
{
  GstBuffer *old_buffer;
  gboolean was_bound = FALSE;

  if (!m_openGlContextInitialized) {
    return oldNode;
  }

  QSGSimpleTextureNode *texNode = static_cast<QSGSimpleTextureNode *> (oldNode);
  GstVideoRectangle src, dst, result;
  GstQSGTexture *tex;

  g_mutex_lock (&this->priv->lock);

  if (gst_gl_context_get_current() == NULL)
    gst_gl_context_activate (this->priv->other_context, TRUE);

  GST_TRACE ("%p updatePaintNode", this);

  if (!this->priv->caps) {
    g_mutex_unlock (&this->priv->lock);
    return NULL;
  }

  if (!texNode) {
    texNode = new QSGSimpleTextureNode ();
    texNode->setOwnsTexture (true);
    texNode->setTexture (new GstQSGTexture ());
  }

  tex = static_cast<GstQSGTexture *> (texNode->texture());

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

  texNode->setRect (QRectF (result.x, result.y, result.w, result.h));

  g_mutex_unlock (&this->priv->lock);

  return texNode;
}

static void
_reset (QtGLVideoItem * qt_item)
{
  GstBuffer *tmp_buffer;

  gst_buffer_replace (&qt_item->priv->buffer, NULL);

  gst_caps_replace (&qt_item->priv->caps, NULL);

  qt_item->priv->negotiated = FALSE;
  qt_item->priv->initted = FALSE;

  while ((tmp_buffer = (GstBuffer*) g_queue_pop_head (&qt_item->priv->potentially_unbound_buffers))) {
    GST_TRACE ("old buffer %p should be unbound now, unreffing", tmp_buffer);
    gst_buffer_unref (tmp_buffer);
  }
  while ((tmp_buffer = (GstBuffer*) g_queue_pop_head (&qt_item->priv->bound_buffers))) {
    GST_TRACE ("old buffer %p should be unbound now, unreffing", tmp_buffer);
    gst_buffer_unref (tmp_buffer);
  }
}

void
QtGLVideoItemInterface::setBuffer (GstBuffer * buffer)
{
  QMutexLocker locker(&lock);

  if (qt_item == NULL) {
    GST_WARNING ("%p actual item is NULL. setBuffer call ignored", this);
    return;
  }

  if (!qt_item->priv->negotiated) {
    GST_WARNING ("%p Got buffer on unnegotiated QtGLVideoItem. Dropping", this);
    return;
  }

  g_mutex_lock (&qt_item->priv->lock);

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

  m_openGlContextInitialized = gst_qt_get_gl_wrapcontext (this->priv->display,
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

/**
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
QtGLVideoItem::handleWindowChanged(QQuickWindow *win)
{
  if (win) {
    if (win->isSceneGraphInitialized())
      win->scheduleRenderJob(new InitializeSceneGraph(this), QQuickWindow::BeforeSynchronizingStage);
    else
      connect(win, SIGNAL(sceneGraphInitialized()), this, SLOT(onSceneGraphInitialized()), Qt::DirectConnection);

    connect(win, SIGNAL(sceneGraphInvalidated()), this, SLOT(onSceneGraphInvalidated()), Qt::DirectConnection);
  } else {
    this->priv->qt_context = NULL;
  }
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

  g_mutex_lock (&qt_item->priv->lock);

  _reset (qt_item);

  gst_caps_replace (&qt_item->priv->caps, caps);

  if (!_calculate_par (qt_item, &v_info)) {
    g_mutex_unlock (&qt_item->priv->lock);
    return FALSE;
  }

  qt_item->priv->v_info = v_info;
  qt_item->priv->negotiated = TRUE;

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

