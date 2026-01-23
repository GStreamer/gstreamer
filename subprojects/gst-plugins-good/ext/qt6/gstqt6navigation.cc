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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqt6navigation.h"

#include <gst/video/navigation.h>

#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>
#include <QtTest/QTest>

#ifdef HAVE_XKBCOMMON
#include <QtGui/private/qxkbcommon_p.h>
#include <xkbcommon/xkbcommon.h>
#endif // HAVE_XKBCOMMON

namespace {

struct MouseMove
{
  QPoint localPosition;
};

struct MouseButton
{
  QPoint localPosition;
  Qt::MouseButton button;
};

struct MouseScroll
{
  QPoint localPosition;
  QPoint angleDelta;
};

struct Key
{
  qint32 qtKey;
  Qt::KeyboardModifiers modifiers;
  QString text;
};

struct Touch
{
  quint32 touchId;
  QPoint localPosition;
};

std::optional<MouseMove> mouseMoveFromEvent(GstEvent *event)
{
  gdouble x, y;

  if (gst_navigation_event_parse_mouse_move_event(event, &x, &y))
    return MouseMove{QPoint(x, y)};

  return {};
}

std::optional<MouseButton> mouseButtonFromEvent(GstEvent *event)
{
  gdouble x, y;
  gint button;

  if (gst_navigation_event_parse_mouse_button_event(event, &button, &x, &y)) {
    const auto localPosition = QPoint(x, y);

    auto qtButton = Qt::NoButton;
    if (button == 1)
      qtButton = Qt::LeftButton;
    else if (button == 2)
      qtButton = Qt::MiddleButton;
    else if (button == 3)
      qtButton = Qt::RightButton;
    else if (button == 4)
      qtButton = Qt::ExtraButton21;
    else if (button == 5)
      qtButton = Qt::ExtraButton22;
    else if (button == 6)
      qtButton = Qt::ExtraButton23;
    else if (button == 7)
      qtButton = Qt::ExtraButton24;
    else if (button == 8)
      qtButton = Qt::BackButton;
    else if (button == 9)
      qtButton = Qt::ForwardButton;

    return MouseButton{localPosition, qtButton};
  }

  return {};
}

std::optional<MouseScroll> mouseScrollFromEvent(GstEvent *event)
{
  gdouble x, y, deltaX, deltaY;

  if (gst_navigation_event_parse_mouse_scroll_event(event, &x, &y, &deltaX, &deltaY)) {
    const auto localPosition = QPoint(x, y);

    const auto angleDelta = QPoint(deltaX < 0.0 ? -120 :
                                   deltaX > 0.0 ? 120 : 0,
                                   deltaY < 0.0 ? -120 :
                                   deltaY > 0.0 ? 120 : 0);

    return MouseScroll{localPosition, angleDelta};
  }

  return {};
}

std::optional<Qt::KeyboardModifiers> keyboardModifiersFromEvent(GstEvent *event)
{
  GstNavigationModifierType modifierType;

  if (gst_navigation_event_parse_modifier_state(event, &modifierType)) {
    Qt::KeyboardModifiers modifiers;

    if (modifierType == GST_NAVIGATION_MODIFIER_NONE)
      return modifiers;
    else if (modifierType & GST_NAVIGATION_MODIFIER_SHIFT_MASK)
      modifiers.setFlag(Qt::ShiftModifier);
    else if (modifierType & GST_NAVIGATION_MODIFIER_CONTROL_MASK)
      modifiers.setFlag(Qt::ControlModifier);
    else if (modifierType & GST_NAVIGATION_MODIFIER_META_MASK)
      modifiers.setFlag(Qt::MetaModifier);
    else if (modifierType & GST_NAVIGATION_MODIFIER_MOD1_MASK)
      modifiers.setFlag(Qt::AltModifier);

    return modifiers;
  }

  return {};
}

std::optional<Touch> touchFromEvent(GstEvent *event)
{
  guint touchId;
  gdouble x, y;

  const auto type = gst_navigation_event_get_type(event);

  if (type == GST_NAVIGATION_EVENT_TOUCH_UP) {
    if (gst_navigation_event_parse_touch_up_event(event, &touchId, &x, &y))
      return Touch{touchId, QPoint(x, y)};
  } else {
    if (gst_navigation_event_parse_touch_event(event, &touchId, &x, &y, nullptr))
      return Touch{touchId, QPoint(x, y)};
  }

  return {};
}

std::optional<Key> keyFromEvent(GstEvent *event)
{
#ifdef HAVE_XKBCOMMON
  const gchar* key = nullptr;

  if (gst_navigation_event_parse_key_event(event, &key)) {
    const auto modifiers = keyboardModifiersFromEvent(event);
    if (modifiers.has_value()) {
      const auto keySym = xkb_keysym_from_name(key, XKB_KEYSYM_NO_FLAGS);
      const auto qtKey = QXkbCommon::keysymToQtKey(keySym, modifiers.value());
      const auto text = QXkbCommon::lookupStringNoKeysymTransformations(keySym);

      return Key{qtKey, modifiers.value(), text};
    }
  }
#endif // HAVE_XKBCOMMON

  return {};
}

std::optional<Qt::MouseButtons> mouseButtonsFromEvent(GstEvent *event)
{
  GstNavigationModifierType modifierType;

  if (gst_navigation_event_parse_modifier_state(event, &modifierType)) {
    Qt::MouseButtons mouseButtons;

    if (modifierType & GST_NAVIGATION_MODIFIER_BUTTON1_MASK)
      mouseButtons.setFlag(Qt::LeftButton);
    else if (modifierType & GST_NAVIGATION_MODIFIER_BUTTON2_MASK)
      mouseButtons.setFlag(Qt::RightButton);
    else if (modifierType & GST_NAVIGATION_MODIFIER_BUTTON3_MASK)
      mouseButtons.setFlag(Qt::MiddleButton);
    else if (modifierType & GST_NAVIGATION_MODIFIER_BUTTON4_MASK)
      mouseButtons.setFlag(Qt::BackButton);
    else if (modifierType & GST_NAVIGATION_MODIFIER_BUTTON5_MASK)
      mouseButtons.setFlag(Qt::ForwardButton);

    return mouseButtons;
  }

  return {};
}

} // namespace

NavigationContext::NavigationContext()
  : m_window(nullptr)
  , m_invertedCoordinates(false)
  , m_active(false)
  , m_touchDevice(QTest::createTouchDevice())
  , m_touchEventSequence(QTest::touchEvent(nullptr, m_touchDevice, false))
{
}

void NavigationContext::setWindow(QWindow *window)
{
  m_window = window;

  m_touchEventSequence = QTest::touchEvent(m_window, m_touchDevice, false);
}

void NavigationContext::setInvertedCoordinates(bool invertedCoordinates)
{
  m_invertedCoordinates = invertedCoordinates;
}

void NavigationContext::setActive(bool active)
{
  m_active = active;
}

void NavigationContext::processNavigationEvent(GstEvent *event)
{
  if (!m_window)
    return;

  if (!m_active)
    return;

  switch (gst_navigation_event_get_type(event)) {
    case GST_NAVIGATION_EVENT_INVALID:
      // ignore
      break;
    case GST_NAVIGATION_EVENT_KEY_PRESS: {
      const auto key = keyFromEvent(event);

      if (key.has_value()) {
        auto* event = new QKeyEvent(QEvent::KeyPress, key.value().qtKey, key.value().modifiers, key.value().text);

        qApp->postEvent(m_window, event);
      }
      break;
    }
    case GST_NAVIGATION_EVENT_KEY_RELEASE: {
      const auto key = keyFromEvent(event);

      if (key.has_value()) {
        auto* event = new QKeyEvent(QEvent::KeyRelease, key.value().qtKey, key.value().modifiers, key.value().text);

        qApp->postEvent(m_window, event);
      }
      break;
    }
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS: {
      const auto mouseButton = mouseButtonFromEvent(event);
      const auto modifiers = keyboardModifiersFromEvent(event);
      const auto buttons = mouseButtonsFromEvent(event);

      if (mouseButton.has_value() && modifiers.has_value() && buttons.has_value()) {
        const auto windowPosition = m_window->position();

        auto localPosition = mouseButton.value().localPosition;
        adaptCoordinate(localPosition);

        const auto globalPosition = windowPosition + localPosition;
        const auto actionButton = mouseButton.value().button;

        if (actionButton == Qt::ExtraButton21 || actionButton == Qt::ExtraButton22 ||
          actionButton == Qt::ExtraButton23 || actionButton == Qt::ExtraButton24) {
          // mouse scroll events, ignore here, handle in button release
        } else {
          auto adaptedButtons = buttons.value();
          adaptedButtons.setFlag(actionButton, true);

          auto* event = new QMouseEvent(QEvent::MouseButtonPress, localPosition, localPosition, globalPosition,
                                        actionButton, adaptedButtons, modifiers.value(),
                                        Qt::MouseEventNotSynthesized);

          qApp->postEvent(m_window, event);
        }
      }
      break;
    }
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE: {
      const auto mouseButton = mouseButtonFromEvent(event);
      const auto modifiers = keyboardModifiersFromEvent(event);
      const auto buttons = mouseButtonsFromEvent(event);

      if (mouseButton.has_value() && modifiers.has_value() && buttons.has_value()) {
        const auto windowPosition = m_window->position();

        auto localPosition = mouseButton.value().localPosition;
        adaptCoordinate(localPosition);

        const auto globalPosition = windowPosition + localPosition;
        const auto actionButton = mouseButton.value().button;

        if (actionButton == Qt::ExtraButton21 || actionButton == Qt::ExtraButton22 ||
          actionButton == Qt::ExtraButton23 || actionButton == Qt::ExtraButton24) {
          const auto delta = QPoint(actionButton == Qt::ExtraButton23 ? 120 :
                                    actionButton == Qt::ExtraButton24 ? -120 : 0,
                                    actionButton == Qt::ExtraButton21 ? 120 :
                                    actionButton == Qt::ExtraButton22 ? -120 : 0);
          auto* event = new QWheelEvent(localPosition, globalPosition, {}, delta, buttons.value(), modifiers.value(),
                                        Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized);

          qApp->postEvent(m_window, event);
        } else {
          auto adaptedButtons = buttons.value();
          adaptedButtons.setFlag(actionButton, false);

          auto* event = new QMouseEvent(QEvent::MouseButtonRelease, localPosition, localPosition, globalPosition,
                                        actionButton, adaptedButtons, modifiers.value(),
                                        Qt::MouseEventNotSynthesized);

          qApp->postEvent(m_window, event);
        }
      }
      break;
    }
    case GST_NAVIGATION_EVENT_MOUSE_MOVE: {
      const auto mouseMove = mouseMoveFromEvent(event);
      const auto modifiers = keyboardModifiersFromEvent(event);
      const auto buttons = mouseButtonsFromEvent(event);

      if (mouseMove.has_value() && modifiers.has_value() && buttons.has_value()) {
        const auto windowPosition = m_window->position();

        auto localPosition = mouseMove.value().localPosition;
        adaptCoordinate(localPosition);

        const auto globalPosition = windowPosition + localPosition;

        auto* event = new QMouseEvent(QEvent::MouseMove, localPosition, localPosition, globalPosition,
                                      Qt::NoButton, buttons.value(), modifiers.value(), Qt::MouseEventNotSynthesized);

        qApp->postEvent(m_window, event);
      }
      break;
    }
    case GST_NAVIGATION_EVENT_COMMAND:
      // not supported
      break;
    case GST_NAVIGATION_EVENT_MOUSE_SCROLL: {
      const auto mouseScroll = mouseScrollFromEvent(event);
      const auto modifiers = keyboardModifiersFromEvent(event);
      const auto buttons = mouseButtonsFromEvent(event);

      if (mouseScroll.has_value() && modifiers.has_value() && buttons.has_value()) {
        const auto windowPosition = m_window->position();

        auto localPosition = mouseScroll.value().localPosition;
        adaptCoordinate(localPosition);

        const auto globalPosition = windowPosition + localPosition;
        const auto angleDelta = mouseScroll.value().angleDelta;

        auto* event = new QWheelEvent(localPosition, globalPosition, {}, angleDelta, buttons.value(), modifiers.value(),
                                      Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized);

        qApp->postEvent(m_window, event);
      }
      break;
    }
    case GST_NAVIGATION_EVENT_TOUCH_DOWN: {
      const auto touch = touchFromEvent(event);

      if (touch.has_value()) {
        auto localPosition = touch.value().localPosition;
        adaptCoordinate(localPosition);

        m_touchEventSequence.press(touch.value().touchId, localPosition, m_window);
      }

      break;
    }
    case GST_NAVIGATION_EVENT_TOUCH_MOTION: {
      const auto touch = touchFromEvent(event);

      if (touch.has_value()) {
        auto localPosition = touch.value().localPosition;
        adaptCoordinate(localPosition);

        m_touchEventSequence.move(touch.value().touchId, localPosition, m_window);
      }

      break;
    }
    case GST_NAVIGATION_EVENT_TOUCH_UP: {
      const auto touch = touchFromEvent(event);

      if (touch.has_value()) {
        auto localPosition = touch.value().localPosition;
        adaptCoordinate(localPosition);

        m_touchEventSequence.release(touch.value().touchId, localPosition, m_window);
      }

      break;
    }
    case GST_NAVIGATION_EVENT_TOUCH_FRAME: {
      m_touchEventSequence.commit(false);

      break;
    }
    case GST_NAVIGATION_EVENT_TOUCH_CANCEL:
      break;
    case GST_NAVIGATION_EVENT_MOUSE_DOUBLE_CLICK: {
      const auto mouseButton = mouseButtonFromEvent(event);
      const auto modifiers = keyboardModifiersFromEvent(event);
      const auto buttons = mouseButtonsFromEvent(event);

      if (mouseButton.has_value() && modifiers.has_value() && buttons.has_value()) {
        const auto windowPosition = m_window->position();

        auto localPosition = mouseButton.value().localPosition;
        adaptCoordinate(localPosition);

        const auto globalPosition = windowPosition + localPosition;
        const auto actionButton = mouseButton.value().button;

        auto adaptedButtons = buttons.value();
        adaptedButtons.setFlag(actionButton, true);

        auto* event = new QMouseEvent(QEvent::MouseButtonDblClick, localPosition, localPosition, globalPosition,
                                      actionButton, adaptedButtons, modifiers.value(),
                                      Qt::MouseEventNotSynthesized);

        qApp->postEvent(m_window, event);
      }
      break;
    }
  }
}

void NavigationContext::adaptCoordinate(QPoint &coordinate)
{
  if (!m_window)
    return;

  if (!m_invertedCoordinates)
    return;

  coordinate.setY(m_window->height() - coordinate.y());
}
