/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 *
 * gstaction.c: base class for main actions/loops
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gst_private.h"

#include "gstaction.h"
#include "gstelement.h"
#include "gstpad.h"
#include "gstinfo.h"
#include "gstscheduler.h"

/*
GST_DEBUG_CATEGORY_STATIC (debug, "GST_ACTION",
    GST_DEBUG_BOLD | GST_DEBUG_FG_RED, "action handling");
#define GST_CAT_DEFAULT debug
*/
#if 1
#  define RELEASE(action)
#else
#  define RELEASE(action) G_STMT_START{\
  gchar *_str; \
  \
  g_assert (action->any.active); \
  _str = gst_action_to_string (action); \
  g_print ("releasing %s\n", _str); \
  g_free (_str); \
}G_STMT_END
#endif
GType
gst_action_get_type (void)
{
  g_assert_not_reached ();
  return 0;
}

#define GST_ACTION_SCHEDULER_CALL(action, call) G_STMT_START{ \
  GstScheduler *sched = action->any.element->sched; \
  if (sched) { \
    GstSchedulerClass *klass = GST_SCHEDULER_GET_CLASS (sched); \
    g_assert (klass->call); \
    klass->call (sched, action); \
  } \
}G_STMT_END

GstAction *
gst_action_new (GstActionType type)
{
  GstAction *action = g_new0 (GstAction, 1);

  action->type = type;
  action->any.active = FALSE;
  action->any.coupled = TRUE;

  return action;
}

void
gst_element_add_action (GstElement * element, GstAction * action)
{
  g_return_if_fail (action->any.element == NULL);

  action->any.element = element;
  element->actions = g_slist_prepend (element->actions, action);

#ifndef GST_DISABLE_GST_DEBUG
  /* FIXME: make this work with %P */
  G_STMT_START {
    gchar *str = gst_action_to_string (action);

    GST_DEBUG_OBJECT (element, "adding action: %s", str);
    g_free (str);
  } G_STMT_END;
#endif

  GST_ACTION_SCHEDULER_CALL (action, add_action);
}

void
gst_element_remove_action (GstAction * action)
{
  GstElement *element;

  g_return_if_fail (action->any.element != NULL);

  element = gst_action_get_element (action);
  GST_ACTION_SCHEDULER_CALL (action, remove_action);
  g_assert (g_slist_find (element->actions, action));
  element->actions = g_slist_remove (element->actions, action);

#ifndef GST_DISABLE_GST_DEBUG
  /* FIXME: make this work with %P */
  G_STMT_START {
    gchar *str = gst_action_to_string (action);

    GST_DEBUG ("removing action: %s", str);
    g_free (str);
  } G_STMT_END;
#endif

  action->any.element = NULL;
  /* FIXME: pads manage their actions themselves - which kinda sucks */
  if (action->type != GST_ACTION_SRC_PAD && action->type != GST_ACTION_SINK_PAD)
    gst_action_free (action);
}

GstElement *
gst_action_get_element (const GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION (action), NULL);

  return action->any.element;
}

void
gst_action_set_active (GstAction * action, gboolean active)
{
  g_return_if_fail (GST_IS_ACTION (action));

  if (action->any.active == active)
    return;
  action->any.active = active;
  GST_ACTION_SCHEDULER_CALL (action, toggle_active);
}

gboolean
gst_action_is_active (GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION (action), FALSE);

  return action->any.active;
}

/**
 * gst_action_set_initially_active:
 * @action: ithe action to set
 * @active: whether or not the action should be initially active
 *
 * Initially active actions are activated by default when elements reset their 
 * actions. This happens during the state change from READY to PAUSED for 
 * example. This function allows modifying that behaviour for an action.
 **/
void
gst_action_set_initially_active (GstAction * action, gboolean active)
{
  g_return_if_fail (GST_IS_ACTION (action));

  action->any.initially_active = active;
}

/**
 * gst_action_is_initially_active:
 * @action: #GstAction to check
 *
 * Retruns if the @action is initially active or not.
 *
 * Returns: TRUE if the @action is initially active, FALSE otherwise
 **/
gboolean
gst_action_is_initially_active (GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION (action), FALSE);

  return action->any.initially_active;
}

/**
 * gst_action_set_coupled:
 * @action: action to set
 * @coupled: new value
 *
 * Makes an action coupled or not. A coupled action's activity depends on the
 * state of the element it belongs to. It gets deactivated automatically when 
 * the element changes state to READY or below and it gets reset to its initial
 * state by gst_element_reset_actions (). Activity of coupled actions must be
 * set manually by the element at all times.
 **/
void
gst_action_set_coupled (GstAction * action, gboolean coupled)
{
  g_return_if_fail (GST_IS_ACTION (action));

  if (action->any.coupled == coupled)
    return;
  action->any.coupled = coupled;
}

gboolean
gst_action_is_coupled (GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION (action), FALSE);

  return action->any.coupled;
}

GstAction *
gst_element_add_wakeup (GstElement * element, gboolean active,
    GstActionWakeupFunc release, gpointer user_data)
{
  GstAction *action;
  GstActionWakeup *wakeup;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (release != NULL, NULL);

  action = gst_action_new (GST_ACTION_WAKEUP);
  action->any.initially_active = active;
  wakeup = &action->wakeup;
  wakeup->release = release;
  wakeup->user_data = user_data;
  gst_element_add_action (element, action);

  return action;
}

void
gst_action_wakeup_release (GstAction * action)
{
  g_return_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_WAKEUP));

  RELEASE (action);
  action->wakeup.release (action, action->any.element,
      action->wakeup.user_data);
}

GstRealPad *
gst_action_get_pad (const GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION (action), NULL);
  g_return_val_if_fail (action->type == GST_ACTION_SINK_PAD
      || action->type == GST_ACTION_SRC_PAD, NULL);

  if (action->type == GST_ACTION_SINK_PAD) {
    g_assert (action->sinkpad.pad != NULL);
    return action->sinkpad.pad;
  } else if (action->type == GST_ACTION_SRC_PAD) {
    g_assert (action->srcpad.pad != NULL);
    return action->srcpad.pad;
  } else {
    g_assert_not_reached ();
    return NULL;
  }
}

void
gst_action_release_sink_pad (GstAction * action, GstData * data)
{
  g_return_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_SINK_PAD));

  RELEASE (action);
  action->sinkpad.release (action, action->sinkpad.pad, data);
}

GstData *
gst_action_release_src_pad (GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_SRC_PAD), NULL);

  RELEASE (action);
  return action->srcpad.release (action, action->srcpad.pad);
}

GstAction *
gst_real_pad_get_action (GstRealPad * pad)
{
  g_return_val_if_fail (GST_IS_REAL_PAD (pad), NULL);

  g_assert_not_reached ();

  return NULL;
}

GstAction *
gst_element_add_wait (GstElement * element, gboolean active,
    GstClockTime start_time, GstClockTime interval, GstActionWaitFunc release)
{
  GstAction *action;
  GstActionWait *wait;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (release != NULL, NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start_time), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (interval), NULL);

  action = gst_action_new (GST_ACTION_WAIT);
  action->any.initially_active = active;
  wait = &action->wait;
  wait->time = start_time;
  wait->interval = interval;
  wait->release = release;
  gst_element_add_action (element, action);

  return action;
}

void
gst_action_wait_change (GstAction * action, GstClockTime start_time,
    GstClockTime interval)
{
  g_return_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_WAIT));

  action->wait.time = start_time;
  action->wait.interval = interval;
  GST_ACTION_SCHEDULER_CALL (action, update_values);
}

void
gst_action_wait_release (GstAction * action)
{
  GstClockTime time;

  g_return_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_WAIT));

  RELEASE (action);
  time = action->wait.time;
  action->wait.time += action->wait.interval;
  action->wait.release (action, action->any.element, time);
}

GstAction *
gst_element_add_fd (GstElement * element, gboolean active,
    gint fd, gushort condition, GstActionFdFunc release)
{
  GstAction *action;
  GstActionFd *afd;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (release != NULL, NULL);
  g_return_val_if_fail (condition != 0, NULL);

  action = gst_action_new (GST_ACTION_FD);
  action->any.initially_active = active;
  afd = &action->fd;
  afd->fd = fd;
  afd->condition = condition;
  gst_element_add_action (element, action);

  return action;
}

void
gst_action_fd_release (GstAction * action, GIOCondition condition)
{
  g_return_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_FD));

  RELEASE (action);
  action->fd.release (action, action->any.element, action->fd.fd, condition);
}

void
gst_action_fd_change (GstAction * action, gint fd, gushort condition)
{
  g_return_if_fail (GST_IS_ACTION_TYPE (action, GST_ACTION_FD));

  action->fd.fd = fd;
  action->fd.condition = condition;
  GST_ACTION_SCHEDULER_CALL (action, update_values);
}

static const gchar *
gst_action_type_to_string (GstActionType type)
{
  switch (type) {
    case GST_ACTION_WAKEUP:
      return "WAKEUP";
    case GST_ACTION_SINK_PAD:
      return "SINKPAD";
    case GST_ACTION_SRC_PAD:
      return "SRCPAD";
    case GST_ACTION_FD:
      return "FD";
    case GST_ACTION_WAIT:
      return "TIME";
    default:
      g_return_val_if_reached (NULL);
  }
}

gchar *
gst_action_to_string (const GstAction * action)
{
  g_return_val_if_fail (GST_IS_ACTION (action), NULL);

  return g_strdup_printf ("%s for %s",
      gst_action_type_to_string (action->type),
      GST_ELEMENT_NAME (gst_action_get_element (action)));
}
