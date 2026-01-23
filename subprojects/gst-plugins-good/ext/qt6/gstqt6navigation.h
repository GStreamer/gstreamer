/*
 * GStreamer
 * Copyright (C) 2026 Klarälvdalens Datakonsult AB, a KDAB Group company,
 *           info@kdab.com, author Tobias Koenig <tobias.koenig@kdab.com>
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

#ifndef __GST_QT6_NAVIGATION_H__
#define __GST_QT6_NAVIGATION_H__

#include <QtGui/qtestsupport_gui.h>

#include <gst/gst.h>

G_BEGIN_DECLS

class QPoint;
class QPointingDevice;
class QWindow;

class NavigationContext {

public:
  NavigationContext();

  void setWindow(QWindow *window);
  void setInvertedCoordinates(bool invertedCoordinates);
  void setActive(bool active);

  void processNavigationEvent(GstEvent *event);

private:
  void adaptCoordinate(QPoint &coordinate);

  QWindow *m_window;
  bool m_invertedCoordinates;
  bool m_active;

  QPointingDevice* m_touchDevice;
  QTest::QTouchEventSequence m_touchEventSequence;
};

G_END_DECLS

#endif /* __GST_QT6_NAVIGATION_H__ */
