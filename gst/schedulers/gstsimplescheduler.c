/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 *
 * gstsimplesimple_scheduler.c: A simple_scheduler as simple as possible
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

#include "gst/gst_private.h"

#include <gst/gst.h>

/*** GstCheckSource ***/

typedef struct
{
  GSource source;
  GSourceFunc check;
  gpointer check_data;
} GstCheckSource;

static gboolean
gst_check_prepare (GSource * source, gint * timeout)
{
  GstCheckSource *s = (GstCheckSource *) source;

  if (s->check (s->check_data)) {
    *timeout = 0;
    return TRUE;
  } else {
    *timeout = -1;
    return FALSE;
  }
}

static gboolean
gst_check_check (GSource * source)
{
  GstCheckSource *s = (GstCheckSource *) source;

  if (s->check (s->check_data)) {
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_check_dispatch (GSource * source, GSourceFunc callback, gpointer user_data)
{
  if (!callback) {
    g_warning ("Check source dispatched without callback\n"
        "You must call g_source_set_callback().");
    return FALSE;
  }

  return callback (user_data);
}

static GSourceFuncs gst_check_funcs = {
  gst_check_prepare,
  gst_check_check,
  gst_check_dispatch,
  NULL
};

GSource *
gst_check_source_new (GSourceFunc check, gpointer check_data)
{
  GSource *source;

  g_return_val_if_fail (check, NULL);
  source = g_source_new (&gst_check_funcs, sizeof (GstCheckSource));
  ((GstCheckSource *) source)->check = check;
  ((GstCheckSource *) source)->check_data = check_data;

  return source;
}

/*** the scheduler ***/

/*
GST_DEBUG_CATEGORY_STATIC (sched_debug, "simplescheduler", GST_DEBUG_BOLD,
    "the simplest possible scheduler");
#define GST_CAT_DEFAULT sched_debug
*/

#define GST_TYPE_SIMPLE_SCHEDULER 		(gst_simple_scheduler_get_type ())
#define GST_SIMPLE_SCHEDULER(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SIMPLE_SCHEDULER, GstSimpleScheduler))
#define GST_IS_SIMPLE_SCHEDULER(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SIMPLE_SCHEDULER))
#define GST_SIMPLE_SCHEDULER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SIMPLE_SCHEDULER,GstSimpleSchedulerClass))
#define GST_IS_SIMPLE_SCHEDULER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SIMPLE_SCHEDULER))
#define GST_SIMPLE_SCHEDULER_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SIMPLE_SCHEDULER, GstSimpleSchedulerClass))

typedef struct _GstSimpleScheduler GstSimpleScheduler;
typedef struct _GstSimpleSchedulerClass GstSimpleSchedulerClass;

struct _GstSimpleScheduler
{
  GstScheduler object;

  GHashTable *sources_for_actions;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSimpleSchedulerClass
{
  GstSchedulerClass parent_class;

  gpointer _gst_reserved[GST_PADDING];
};


static void gst_simple_scheduler_class_init (GstSimpleSchedulerClass * klass);
static void gst_simple_scheduler_init (GstSimpleScheduler * sched);
static void gst_simple_scheduler_finalize (GObject * object);

static void gst_simple_scheduler_add_action (GstScheduler * scheduler,
    GstAction * action);
static void gst_simple_scheduler_remove_action (GstScheduler * scheduler,
    GstAction * action);
static void gst_simple_scheduler_pad_push (GstScheduler * scheduler,
    GstRealPad * pad, GstData * data);
static void gst_simple_scheduler_toggle_active (GstScheduler * scheduler,
    GstAction * action);
static void gst_simple_scheduler_update_values (GstScheduler * scheduler,
    GstAction * action);

GstSchedulerClass *parent_class;

GType
gst_simple_scheduler_get_type (void)
{
  static GType _gst_simple_scheduler_type = 0;

  if (!_gst_simple_scheduler_type) {
    static const GTypeInfo simple_scheduler_info = {
      sizeof (GstSimpleSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_simple_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstSimpleScheduler),
      0,
      (GInstanceInitFunc) gst_simple_scheduler_init,
      NULL
    };

    _gst_simple_scheduler_type =
        g_type_register_static (GST_TYPE_SCHEDULER, "GstSimpleScheduler",
        &simple_scheduler_info, 0);
  }
  return _gst_simple_scheduler_type;
}

static void
gst_simple_scheduler_class_init (GstSimpleSchedulerClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstSchedulerClass *sched = GST_SCHEDULER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object->finalize = gst_simple_scheduler_finalize;

  sched->add_action = gst_simple_scheduler_add_action;
  sched->remove_action = gst_simple_scheduler_remove_action;
  sched->pad_push = gst_simple_scheduler_pad_push;
  sched->toggle_active = gst_simple_scheduler_toggle_active;
  sched->update_values = gst_simple_scheduler_update_values;
}

static void
gst_simple_scheduler_init (GstSimpleScheduler * sched)
{
  sched->sources_for_actions = g_hash_table_new (g_direct_hash, g_direct_equal);
}

#ifndef G_DISABLE_ASSERT
static void
print_all_actions (gpointer key, gpointer value, gpointer unused)
{
  GstAction *action = key;
  gchar *str = gst_action_to_string (action);

  g_print ("  action %p: %s --- source %p\n", action, str, value);
  g_free (str);
}
#endif

static void
gst_simple_scheduler_finalize (GObject * object)
{
  GstSimpleScheduler *sched = GST_SIMPLE_SCHEDULER (object);

  /* all actions must have been removed by the scheduler's parent before 
   * disposing */
#ifndef G_DISABLE_ASSERT
  if (g_hash_table_size (sched->sources_for_actions) != 0) {
    g_printerr ("scheduler %p has %u items left:\n", sched,
        g_hash_table_size (sched->sources_for_actions));
    g_hash_table_foreach (sched->sources_for_actions, print_all_actions, NULL);
    g_assert_not_reached ();
  }
#endif
  g_hash_table_destroy (sched->sources_for_actions);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
release_wakeup_cb (gpointer data)
{
  GstAction *action = data;

  gst_action_wakeup_release (action);
  return TRUE;
}

static gboolean
timeout_elapsed_cb (gpointer data)
{
  GstAction *action = data;

#ifndef G_DISABLE_CHECKS
  GstClockTime time = gst_element_get_time (action->any.element);

  if (action->wait.time > time) {
    GST_WARNING ("time on element %s is %" GST_TIME_FORMAT " too short (it's %"
        GST_TIME_FORMAT ".\n", GST_ELEMENT_NAME (action->wait.element),
        GST_TIME_ARGS (action->wait.time - time), GST_TIME_ARGS (time));
  }
#endif
  gst_action_wait_release (action);
  return TRUE;
}

static gboolean
release_fd_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  GstAction *action = data;

  gst_action_fd_release (action, condition);
  return TRUE;
}

static gboolean
push_cb (gpointer action)
{
  GstRealPad *pad = gst_action_get_pad (action);
  GstData *data;

  GST_LOG ("pushing on %s:%s...", GST_DEBUG_PAD_NAME (pad));
  if (GST_RPAD_PEER (pad)) {
    GstRealPad *peer = GST_RPAD_PEER (pad);

    g_assert (peer->sched_private);     /* FIXME: relinking in callback? */
    data = ((GList *) peer->sched_private)->data;
    peer->sched_private = g_list_remove (peer->sched_private, data);
    if (peer->sched_private)
      GST_ERROR ("pad %s:%s had multiple (%u) GstData queued.",
          GST_DEBUG_PAD_NAME (peer), g_list_length (peer->sched_private) + 1);
  } else {
    data = GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
  gst_action_release_sink_pad (action, data);

  return TRUE;
}

static void
gst_simple_scheduler_do_push (GstRealPad * pad, GstData * data)
{
  g_assert (GST_PAD_IS_SRC (pad));
  if (!GST_PAD_PEER (pad)) {
    g_assert (pad->sched_private == NULL);
    gst_data_unref (data);
    return;
  }
  pad->sched_private = g_list_append (pad->sched_private, data);
}

static gboolean
pull_cb (gpointer action)
{
  GstData *data;
  GstRealPad *pad = gst_action_get_pad (action);
  GstScheduler *sched;

  GST_LOG ("pulling...");
  gst_object_ref (pad);
  sched = gst_pad_get_scheduler (GST_PAD (pad));
  data = gst_action_release_src_pad (action);
  if (sched == gst_pad_get_scheduler (GST_PAD (pad)))
    gst_simple_scheduler_do_push (pad, data);
  gst_object_unref (pad);

  return TRUE;
}

static gboolean
check_no_data (gpointer action)
{
  return !(gst_action_get_element (action)->sched_private);
}

static gboolean
check_no_data_srcpad (gpointer action)
{
  return !(gst_action_get_pad (action)->sched_private);
}

static gboolean
check_data_sinkpad (gpointer action)
{
  GstRealPad *pad = gst_action_get_pad (action);

  if (!GST_RPAD_PEER (pad))
    return TRUE;
  return GST_RPAD_PEER (pad)->sched_private != NULL;
}

static void
gst_simple_scheduler_activate_action (GstScheduler * scheduler,
    GstAction * action)
{
  GSource *source;
  GstSimpleScheduler *sched = GST_SIMPLE_SCHEDULER (scheduler);

  g_assert (gst_action_is_active (action));
  switch (action->type) {
    case GST_ACTION_WAKEUP:
      source = gst_check_source_new (check_no_data, action);
      g_source_set_priority (source, G_PRIORITY_DEFAULT);
      g_source_set_callback (source, release_wakeup_cb, action, NULL);
      break;
    case GST_ACTION_SRC_PAD:
      source = gst_check_source_new (check_no_data_srcpad, action);
      g_source_set_callback (source, pull_cb, action, NULL);
      break;
    case GST_ACTION_SINK_PAD:
      source = gst_check_source_new (check_data_sinkpad, action);
      g_source_set_callback (source, push_cb, action, NULL);
      break;
    case GST_ACTION_FD:
    {
      GIOChannel *channel = g_io_channel_unix_new (action->fd.fd);

      source = g_io_create_watch (channel, action->fd.condition);
      g_source_set_callback (source, (GSourceFunc) release_fd_cb, action, NULL);
      g_io_channel_unref (channel);
    }
      break;
    case GST_ACTION_WAIT:
    {
      GstClockTime time = gst_element_get_time (action->any.element);

      GST_LOG_OBJECT (sched,
          "time is %" GST_TIME_FORMAT ", waiting for %" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (time), GST_TIME_ARGS (action->wait.time));
      if (action->wait.time > time) {
        time = action->wait.time - time;
        /* FIXME: make this adjustable by the element's clock */
      } else {
        time = 0;
      }
      source = g_timeout_source_new (time / (GST_SECOND / 1000));
      g_source_set_callback (source, timeout_elapsed_cb, action, NULL);
    }
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  /* FIXME: LEAKS */
  GST_DEBUG_OBJECT (sched, "adding source %p for action %s\n", source,
      gst_action_to_string (action));
  g_hash_table_insert (sched->sources_for_actions, action, source);
  g_source_attach (source, NULL);
  g_source_unref (source);      /* FIXME: need better refcount management? */
  GST_LOG_OBJECT (sched, "%u active sources now",
      g_hash_table_size (sched->sources_for_actions));
}

static void
gst_simple_scheduler_add_action (GstScheduler * scheduler, GstAction * action)
{
  if (gst_action_is_active (action))
    gst_simple_scheduler_activate_action (scheduler, action);
}

static void
gst_simple_scheduler_deactivate_action (GstScheduler * scheduler,
    GstAction * action)
{
  GSource *source;
  GstSimpleScheduler *sched = GST_SIMPLE_SCHEDULER (scheduler);

  source = g_hash_table_lookup (sched->sources_for_actions, action);
  g_assert (source);
  g_source_destroy (source);
  if (!g_hash_table_remove (sched->sources_for_actions, action))
    g_assert_not_reached ();
  GST_DEBUG_OBJECT (sched, "%p removed for action %p, %u active sources now",
      source, action, g_hash_table_size (sched->sources_for_actions));
}

static void
gst_simple_scheduler_remove_action (GstScheduler * scheduler,
    GstAction * action)
{
  if (gst_action_is_active (action))
    gst_simple_scheduler_deactivate_action (scheduler, action);
}

static void
gst_simple_scheduler_toggle_active (GstScheduler * scheduler,
    GstAction * action)
{
  //g_print ("toggling action %s to be %sactive now\n", gst_action_to_string (action), 
  //    gst_action_is_active (action) ? "" : "NOT ");
  if (gst_action_is_active (action)) {
    gst_simple_scheduler_activate_action (scheduler, action);
  } else {
    gst_simple_scheduler_deactivate_action (scheduler, action);
  }
}

static void
gst_simple_scheduler_update_values (GstScheduler * scheduler,
    GstAction * action)
{
  if (gst_action_is_active (action)) {
    gst_simple_scheduler_deactivate_action (scheduler, action);
    gst_simple_scheduler_activate_action (scheduler, action);
  }
}

static void
gst_simple_scheduler_pad_push (GstScheduler * scheduler, GstRealPad * pad,
    GstData * data)
{
  GST_LOG_OBJECT (scheduler, "pad %s:%s pushed %p", GST_DEBUG_PAD_NAME (pad),
      data);
  gst_simple_scheduler_do_push (pad, data);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_scheduler_register (plugin, "simple",
          "A scheduler as simple as possible", GST_TYPE_SIMPLE_SCHEDULER))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "gstsimplescheduler",
    "A scheduler as simple as possible", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE, GST_ORIGIN)
