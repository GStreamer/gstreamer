/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
 *               2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 *               2014 Thibault Saunier <tsaunier@gnome.org>
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

#include "nle.h"

/**
 * SECTION:element-nlecomposition
 *
 * A NleComposition contains NleObjects such as NleSources and NleOperations,
 * and connects them dynamically to create a composition timeline.
 */

static GstStaticPadTemplate nle_composition_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (nlecomposition_debug);
#define GST_CAT_DEFAULT nlecomposition_debug

#define _do_init              \
  GST_DEBUG_CATEGORY_INIT (nlecomposition_debug,"nlecomposition", GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Composition");
#define nle_composition_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (NleComposition, nle_composition, NLE_TYPE_OBJECT,
    _do_init);

enum
{
  PROP_0,
  PROP_DEACTIVATED_ELEMENTS_STATE,
  PROP_LAST,
};

/* Properties from NleObject */
enum
{
  NLEOBJECT_PROP_START,
  NLEOBJECT_PROP_STOP,
  NLEOBJECT_PROP_DURATION,
  NLEOBJECT_PROP_LAST
};

enum
{
  COMMIT_SIGNAL,
  COMMITED_SIGNAL,
  LAST_SIGNAL
};

typedef enum
{
  COMP_UPDATE_STACK_INITIALIZE,
  COMP_UPDATE_STACK_ON_COMMIT,
  COMP_UPDATE_STACK_ON_EOS,
  COMP_UPDATE_STACK_ON_SEEK,
  COMP_UPDATE_STACK_NONE
} NleUpdateStackReason;

static const char *UPDATE_PIPELINE_REASONS[] = {
  "Initialize", "Commit", "EOS", "Seek"
};

typedef struct
{
  NleComposition *comp;
  GstEvent *event;
} SeekData;

typedef struct
{
  NleComposition *comp;
  NleObject *object;
} ChildIOData;

typedef struct
{
  NleComposition *comp;
  gint32 seqnum;

  NleUpdateStackReason reason;
} UpdateCompositionData;

struct _NleCompositionPrivate
{
  gboolean dispose_has_run;

  /*
     Sorted List of NleObjects , ThreadSafe
     objects_start : sorted by start-time then priority
     objects_stop : sorted by stop-time then priority
     objects_hash : contains all controlled objects

     Those list should be manipulated exclusively in the main context
     or while the task is totally stopped.
   */
  GList *objects_start;
  GList *objects_stop;
  GHashTable *objects_hash;

  /* List of NleObject to be inserted or removed from the composition on the
   * next commit */
  GHashTable *pending_io;

  gulong ghosteventprobe;

  /* current stack, list of NleObject* */
  GNode *current;

  /* List of NleObject whose start/duration will be the same as the composition */
  GList *expandables;

  /*
     current segment seek start/stop time.
     Reconstruct pipeline ONLY if seeking outside of those values
     FIXME : segment_start isn't always the earliest time before which the
     timeline doesn't need to be modified
   */
  GstClockTime segment_start;
  GstClockTime segment_stop;

  /* Seek segment handler */
  GstSegment *segment;
  GstSegment *outside_segment;

  /* Next running base_time to set on outgoing segment */
  guint64 next_base_time;

  /*
     OUR sync_handler on the child_bus
     We are called before nle_object_sync_handler
   */
  GstPadEventFunction nle_event_pad_func;
  gboolean send_stream_start;

  /* Protect the actions list */
  GMutex actions_lock;
  GCond actions_cond;
  GList *actions;

  gboolean running;
  gboolean initialized;

  GstElement *current_bin;

  gboolean seeking_itself;
  gint real_eos_seqnum;
  gint next_eos_seqnum;
  gint flush_seqnum;

  /* 0 means that we already received the right caps or segment */
  gint seqnum_to_restart_task;
  gboolean waiting_for_buffer;

  gboolean tearing_down_stack;

  NleUpdateStackReason updating_reason;
};

typedef struct _Action
{
  GCClosure closure;
  gint priority;
} Action;

#define ACTION_CALLBACK(__action) (((GCClosure*) (__action))->callback)

static guint _signals[LAST_SIGNAL] = { 0 };

static GParamSpec *nleobject_properties[NLEOBJECT_PROP_LAST];

#define OBJECT_IN_ACTIVE_SEGMENT(comp,element)      \
  ((NLE_OBJECT_START(element) < comp->priv->segment_stop) &&  \
   (NLE_OBJECT_STOP(element) >= comp->priv->segment_start))

static void nle_composition_dispose (GObject * object);
static void nle_composition_finalize (GObject * object);
static void nle_composition_reset (NleComposition * comp);

static gboolean nle_composition_add_object (GstBin * bin, GstElement * element);

static gboolean
nle_composition_remove_object (GstBin * bin, GstElement * element);

static GstStateChangeReturn
nle_composition_change_state (GstElement * element, GstStateChange transition);

static inline void nle_composition_reset_target_pad (NleComposition * comp);

static gboolean
seek_handling (NleComposition * comp, gint32 seqnum,
    NleUpdateStackReason update_stack_reason);
static gint objects_start_compare (NleObject * a, NleObject * b);
static gint objects_stop_compare (NleObject * a, NleObject * b);
static GstClockTime get_current_position (NleComposition * comp);

static gboolean update_pipeline (NleComposition * comp,
    GstClockTime currenttime, gint32 seqnum,
    NleUpdateStackReason update_stack_reason);
static gboolean nle_composition_commit_func (NleObject * object,
    gboolean recurse);
static void update_start_stop_duration (NleComposition * comp);

static gboolean
nle_composition_event_handler (GstPad * ghostpad, GstObject * parent,
    GstEvent * event);
static void _relink_single_node (NleComposition * comp, GNode * node,
    GstEvent * toplevel_seek);
static void _update_pipeline_func (NleComposition * comp,
    UpdateCompositionData * ucompo);
static void _commit_func (NleComposition * comp,
    UpdateCompositionData * ucompo);
static GstEvent *get_new_seek_event (NleComposition * comp, gboolean initial,
    gboolean updatestoponly);
static gboolean _nle_composition_add_object (NleComposition * comp,
    NleObject * object);
static gboolean _nle_composition_remove_object (NleComposition * comp,
    NleObject * object);
static void _deactivate_stack (NleComposition * comp,
    gboolean flush_downstream);
static gboolean _set_real_eos_seqnum_from_seek (NleComposition * comp,
    GstEvent * event);
static void _emit_commited_signal_func (NleComposition * comp, gpointer udata);
static void _restart_task (NleComposition * comp);
static void
_add_action (NleComposition * comp, GCallback func, gpointer data,
    gint priority);
static gboolean
_is_ready_to_restart_task (NleComposition * comp, GstEvent * event);


/* COMP_REAL_START: actual position to start current playback at. */
#define COMP_REAL_START(comp)                                                  \
  (MAX (comp->priv->segment->start, NLE_OBJECT_START (comp)))

#define COMP_REAL_STOP(comp)                                                   \
  (GST_CLOCK_TIME_IS_VALID (comp->priv->segment->stop) ?                       \
   (MIN (comp->priv->segment->stop, NLE_OBJECT_STOP (comp))) :                 \
   NLE_OBJECT_STOP (comp))

#define ACTIONS_LOCK(comp) G_STMT_START {                       \
  GST_LOG_OBJECT (comp, "Getting ACTIONS_LOCK in thread %p",    \
        g_thread_self());                                            \
  g_mutex_lock(&((NleComposition*)comp)->priv->actions_lock);    \
  GST_LOG_OBJECT (comp, "Got ACTIONS_LOCK in thread %p",        \
        g_thread_self());                                            \
} G_STMT_END

#define ACTIONS_UNLOCK(comp) G_STMT_START {                     \
  g_mutex_unlock(&((NleComposition*)comp)->priv->actions_lock);  \
  GST_LOG_OBJECT (comp, "Unlocked ACTIONS_LOCK in thread %p",   \
        g_thread_self());                                            \
} G_STMT_END

#define WAIT_FOR_AN_ACTION(comp) G_STMT_START {                     \
  GST_LOG_OBJECT (comp, "Waiting for an action in thread %p",       \
        g_thread_self());                                           \
  g_cond_wait(&((NleComposition*)comp)->priv->actions_cond,         \
      &((NleComposition*)comp)->priv->actions_lock);                  \
  GST_LOG_OBJECT (comp, "Done WAITING for an action in thread %p",       \
        g_thread_self());                                           \
} G_STMT_END

#define SIGNAL_NEW_ACTION(comp) G_STMT_START {                     \
  GST_LOG_OBJECT (comp, "Signalling new action from thread %p",       \
        g_thread_self());                                           \
  g_cond_signal(&((NleComposition*)comp)->priv->actions_cond);     \
} G_STMT_END

#define GET_TASK_LOCK(comp)    (&(NLE_COMPOSITION(comp)->task_rec_lock))

static inline gboolean
_have_to_flush_downstream (NleUpdateStackReason update_reason)
{
  if (update_reason == COMP_UPDATE_STACK_ON_COMMIT ||
      update_reason == COMP_UPDATE_STACK_ON_SEEK ||
      update_reason == COMP_UPDATE_STACK_INITIALIZE)
    return TRUE;

  return FALSE;
}

static void
_assert_proper_thread (NleComposition * comp)
{
  if (comp->task && gst_task_get_state (comp->task) != GST_TASK_STOPPED &&
      g_thread_self () != comp->task->thread) {
    g_warning ("Trying to touch children in a thread different from"
        " its dedicated thread!");
  }
}

static void
_remove_actions_for_type (NleComposition * comp, GCallback callback)
{
  GList *tmp;

  ACTIONS_LOCK (comp);
  for (tmp = comp->priv->actions; tmp; tmp = tmp->next) {
    Action *act = tmp->data;

    if (ACTION_CALLBACK (act) == callback) {
      g_closure_unref ((GClosure *) act);
      comp->priv->actions = g_list_delete_link (comp->priv->actions, tmp);
    }
  }
  ACTIONS_UNLOCK (comp);

}

static void
_execute_actions (NleComposition * comp)
{
  NleCompositionPrivate *priv = comp->priv;

  ACTIONS_LOCK (comp);
  if (priv->running == FALSE) {
    GST_DEBUG_OBJECT (comp, "Not running anymore");

    ACTIONS_UNLOCK (comp);
    return;
  }

  if (priv->actions == NULL)
    WAIT_FOR_AN_ACTION (comp);

  if (comp->priv->running == FALSE) {
    GST_INFO_OBJECT (comp, "Done waiting but not running anymore");

    ACTIONS_UNLOCK (comp);
    return;
  }

  if (priv->actions) {
    GValue params[1] = { G_VALUE_INIT };
    GList *lact;

    g_value_init (&params[0], G_TYPE_OBJECT);
    g_value_set_object (&params[0], comp);

    lact = priv->actions;
    priv->actions = priv->actions->next;
    ACTIONS_UNLOCK (comp);

    GST_INFO_OBJECT (comp, "Invoking %p:%s",
        lact->data, GST_DEBUG_FUNCPTR_NAME ((ACTION_CALLBACK (lact->data))));
    g_closure_invoke (lact->data, NULL, 1, params, NULL);
    g_value_unset (&params[0]);
  } else {
    ACTIONS_UNLOCK (comp);
  }
}

static void
_start_task (NleComposition * comp)
{
  GstTask *task;

  ACTIONS_LOCK (comp);
  comp->priv->running = TRUE;
  ACTIONS_UNLOCK (comp);

  GST_OBJECT_LOCK (comp);

  task = comp->task;
  if (task == NULL) {
    gchar *taskname =
        g_strdup_printf ("%s_update_management", GST_OBJECT_NAME (comp));

    task = gst_task_new ((GstTaskFunction) _execute_actions, comp, NULL);
    gst_object_set_name (GST_OBJECT_CAST (task), taskname);
    gst_task_set_lock (task, GET_TASK_LOCK (comp));
    GST_INFO_OBJECT (comp, "created task %p", task);
    comp->task = task;
  }

  gst_task_set_state (task, GST_TASK_STARTED);
  GST_OBJECT_UNLOCK (comp);
}

static gboolean
_stop_task (NleComposition * comp)
{
  gboolean res = TRUE;
  GstTask *task;

  GST_INFO_OBJECT (comp, "Stoping children management task");

  ACTIONS_LOCK (comp);
  comp->priv->running = FALSE;

  /*  Make sure we do not stay blocked trying to execute an action */
  SIGNAL_NEW_ACTION (comp);
  ACTIONS_UNLOCK (comp);

  GST_DEBUG_OBJECT (comp, "stop task");

  GST_OBJECT_LOCK (comp);
  task = comp->task;
  if (task == NULL)
    goto no_task;
  comp->task = NULL;
  res = gst_task_set_state (task, GST_TASK_STOPPED);
  GST_OBJECT_UNLOCK (comp);

  if (!gst_task_join (task))
    goto join_failed;

  gst_object_unref (task);

  return res;

no_task:
  {
    GST_OBJECT_UNLOCK (comp);

    /* this is not an error */
    return TRUE;
  }
join_failed:
  {
    /* this is bad, possibly the application tried to join the task from
     * the task's thread. We install the task again so that it will be stopped
     * again from the right thread next time hopefully. */
    GST_OBJECT_LOCK (comp);
    GST_DEBUG_OBJECT (comp, "join failed");
    /* we can only install this task if there was no other task */
    if (comp->task == NULL)
      comp->task = task;
    GST_OBJECT_UNLOCK (comp);

    return FALSE;
  }

  return res;
}

static void
_post_start_composition_update (NleComposition * comp,
    gint32 seqnum, NleUpdateStackReason reason)
{
  GstMessage *msg;

  msg = gst_message_new_element (GST_OBJECT (comp),
      gst_structure_new ("NleCompositionStartUpdate",
          "reason", G_TYPE_STRING, UPDATE_PIPELINE_REASONS[reason], NULL));

  gst_message_set_seqnum (msg, seqnum);
  gst_element_post_message (GST_ELEMENT (comp), msg);
}

static void
_post_start_composition_update_done (NleComposition * comp,
    gint32 seqnum, NleUpdateStackReason reason)
{
  GstMessage *msg = gst_message_new_element (GST_OBJECT (comp),
      gst_structure_new ("NleCompositionUpdateDone",
          "reason", G_TYPE_STRING, UPDATE_PIPELINE_REASONS[reason],
          NULL));

  gst_message_set_seqnum (msg, seqnum);
  gst_element_post_message (GST_ELEMENT (comp), msg);
}

static void
_seek_pipeline_func (NleComposition * comp, SeekData * seekd)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  NleCompositionPrivate *priv = comp->priv;

  gst_event_parse_seek (seekd->event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  GST_DEBUG_OBJECT (seekd->comp,
      "start:%" GST_TIME_FORMAT " -- stop:%" GST_TIME_FORMAT "  flags:%d",
      GST_TIME_ARGS (cur), GST_TIME_ARGS (stop), flags);

  gst_segment_do_seek (priv->segment,
      rate, format, flags, cur_type, cur, stop_type, stop, NULL);
  gst_segment_do_seek (priv->outside_segment,
      rate, format, flags, cur_type, cur, stop_type, stop, NULL);

  GST_DEBUG_OBJECT (seekd->comp, "Segment now has flags:%d",
      priv->segment->flags);

  if (priv->segment->start >= NLE_OBJECT_STOP (seekd->comp)) {
    GST_INFO_OBJECT (seekd->comp,
        "Start %" GST_TIME_FORMAT " > comp->stop: %" GST_TIME_FORMAT
        " Not seeking", GST_TIME_ARGS (priv->segment->start),
        GST_TIME_ARGS (NLE_OBJECT_STOP (seekd->comp)));
    GST_FIXME_OBJECT (seekd->comp, "HANDLE error async!");
    return;
  }

  _post_start_composition_update (seekd->comp,
      gst_event_get_seqnum (seekd->event), COMP_UPDATE_STACK_ON_SEEK);

  /* crop the segment start/stop values */
  /* Only crop segment start value if we don't have a default object */
  if (priv->expandables == NULL)
    priv->segment->start =
        MAX (priv->segment->start, NLE_OBJECT_START (seekd->comp));
  priv->segment->stop =
      MIN (priv->segment->stop, NLE_OBJECT_STOP (seekd->comp));

  priv->next_base_time = 0;

  seek_handling (seekd->comp, gst_event_get_seqnum (seekd->event),
      COMP_UPDATE_STACK_ON_SEEK);

  _post_start_composition_update_done (seekd->comp,
      gst_event_get_seqnum (seekd->event), COMP_UPDATE_STACK_ON_SEEK);
}

/*  Must be called with OBJECTS_LOCK taken */
static void
_process_pending_entries (NleComposition * comp)
{
  NleObject *object;
  GHashTableIter iter;
  gboolean deactivated_stack = FALSE;

  NleCompositionPrivate *priv = comp->priv;

  g_hash_table_iter_init (&iter, priv->pending_io);
  while (g_hash_table_iter_next (&iter, (gpointer *) & object, NULL)) {
    if (g_hash_table_contains (priv->objects_hash, object)) {

      if (GST_OBJECT_PARENT (object) == GST_OBJECT_CAST (priv->current_bin) &&
          deactivated_stack == FALSE) {
        deactivated_stack = TRUE;

        _deactivate_stack (comp, TRUE);
      }

      _nle_composition_remove_object (comp, object);
    } else {
      _nle_composition_add_object (comp, object);
    }
  }

  g_hash_table_remove_all (priv->pending_io);
}


static inline gboolean
_commit_values (NleComposition * comp)
{
  GList *tmp;
  gboolean commited = FALSE;
  NleCompositionPrivate *priv = comp->priv;

  for (tmp = priv->objects_start; tmp; tmp = tmp->next) {
    if (nle_object_commit (tmp->data, TRUE))
      commited = TRUE;
  }

  GST_DEBUG_OBJECT (comp, "Linking up commit vmethod");
  commited |= NLE_OBJECT_CLASS (parent_class)->commit (NLE_OBJECT (comp), TRUE);

  return commited;
}

static gboolean
_commit_all_values (NleComposition * comp)
{
  NleCompositionPrivate *priv = comp->priv;

  priv->next_base_time = 0;

  _process_pending_entries (comp);

  if (_commit_values (comp) == FALSE) {

    return FALSE;;
  }

  /* The topology of the composition might have changed, update the lists */
  priv->objects_start = g_list_sort
      (priv->objects_start, (GCompareFunc) objects_start_compare);
  priv->objects_stop = g_list_sort
      (priv->objects_stop, (GCompareFunc) objects_stop_compare);

  return TRUE;
}

static gboolean
_initialize_stack_func (NleComposition * comp, UpdateCompositionData * ucompo)
{
  NleCompositionPrivate *priv = comp->priv;


  _post_start_composition_update (comp, ucompo->seqnum, ucompo->reason);

  _commit_all_values (comp);
  update_start_stop_duration (comp);
  comp->priv->next_base_time = 0;
  /* set ghostpad target */
  if (!(update_pipeline (comp, COMP_REAL_START (comp),
              ucompo->seqnum, COMP_UPDATE_STACK_INITIALIZE))) {
    GST_FIXME_OBJECT (comp, "PLEASE signal state change failure ASYNC");
  }

  _post_start_composition_update_done (comp, ucompo->seqnum, ucompo->reason);
  priv->initialized = TRUE;

  return G_SOURCE_REMOVE;
}

static void
_remove_object_func (NleComposition * comp, ChildIOData * childio)
{
  NleObject *object = childio->object;

  NleCompositionPrivate *priv = comp->priv;
  NleObject *in_pending_io;

  in_pending_io = g_hash_table_lookup (priv->pending_io, object);

  if (!g_hash_table_contains (priv->objects_hash, object)) {
    if (in_pending_io) {
      GST_INFO_OBJECT (comp, "Object %" GST_PTR_FORMAT " was marked"
          " for addition, removing it from the addition list", object);

      g_hash_table_remove (priv->pending_io, object);
      return;
    }

    GST_ERROR_OBJECT (comp, "Object %" GST_PTR_FORMAT " is "
        " not in the composition", object);

    return;
  }

  if (in_pending_io) {
    GST_WARNING_OBJECT (comp, "Object %" GST_PTR_FORMAT " is already marked"
        " for removal", object);

    return;
  }

  g_hash_table_add (priv->pending_io, object);

  return;
}

static void
_add_remove_object_action (NleComposition * comp, NleObject * object)
{
  ChildIOData *childio = g_slice_new0 (ChildIOData);

  GST_DEBUG_OBJECT (comp, "Adding Action");

  childio->comp = comp;
  childio->object = object;

  _add_action (comp, G_CALLBACK (_remove_object_func),
      childio, G_PRIORITY_DEFAULT);
}

static void
_add_object_func (NleComposition * comp, ChildIOData * childio)
{
  NleObject *object = childio->object;
  NleCompositionPrivate *priv = comp->priv;
  NleObject *in_pending_io;

  in_pending_io = g_hash_table_lookup (priv->pending_io, object);

  if (g_hash_table_contains (priv->objects_hash, object)) {
    GST_ERROR_OBJECT (comp, "Object %" GST_PTR_FORMAT " is "
        " already in the composition", object);

    return;
  }

  if (in_pending_io) {
    GST_WARNING_OBJECT (comp, "Object %" GST_PTR_FORMAT " is already marked"
        " for addition", object);

    return;
  }


  g_hash_table_add (priv->pending_io, object);
}

static void
_add_add_object_action (NleComposition * comp, NleObject * object)
{
  ChildIOData *childio = g_slice_new0 (ChildIOData);

  GST_DEBUG_OBJECT (comp, "Adding Action");

  childio->comp = comp;
  childio->object = object;

  _add_action (comp, G_CALLBACK (_add_object_func), childio,
      G_PRIORITY_DEFAULT);
}

static void
_free_action (Action * action, gpointer udata)
{
  if (ACTION_CALLBACK (action) == _seek_pipeline_func) {
    SeekData *seekd = (SeekData *) udata;

    gst_event_unref (seekd->event);
    g_slice_free (SeekData, seekd);
  } else if (ACTION_CALLBACK (action) == _remove_object_func ||
      ACTION_CALLBACK (action) == _add_object_func) {
    g_slice_free (ChildIOData, udata);
  } else if (ACTION_CALLBACK (action) == _update_pipeline_func ||
      ACTION_CALLBACK (action) == _commit_func ||
      ACTION_CALLBACK (action) == _initialize_stack_func) {
    g_slice_free (UpdateCompositionData, udata);
  }
}

static void
_add_action (NleComposition * comp, GCallback func,
    gpointer data, gint priority)
{
  Action *action;
  NleCompositionPrivate *priv = comp->priv;


  action = (Action *) g_closure_new_simple (sizeof (Action), data);
  g_closure_add_finalize_notifier ((GClosure *) action, data,
      (GClosureNotify) _free_action);
  ACTION_CALLBACK (action) = func;
  action->priority = priority;
  g_closure_set_marshal ((GClosure *) action, g_cclosure_marshal_VOID__VOID);

  ACTIONS_LOCK (comp);
  GST_INFO_OBJECT (comp, "Adding Action for function: %p:%s",
      action, GST_DEBUG_FUNCPTR_NAME (func));

  if (func == G_CALLBACK (_emit_commited_signal_func))
    priv->actions = g_list_prepend (priv->actions, action);
  else
    priv->actions = g_list_append (priv->actions, action);

  SIGNAL_NEW_ACTION (comp);
  ACTIONS_UNLOCK (comp);
}

static void
_add_seek_action (NleComposition * comp, GstEvent * event)
{
  SeekData *seekd = g_slice_new0 (SeekData);

  GST_DEBUG_OBJECT (comp, "Adding Action");

  seekd->comp = comp;
  seekd->event = event;

  comp->priv->next_eos_seqnum = 0;
  comp->priv->real_eos_seqnum = 0;
  _add_action (comp, G_CALLBACK (_seek_pipeline_func), seekd,
      G_PRIORITY_DEFAULT);
}

static void
_remove_update_actions (NleComposition * comp)
{
  _remove_actions_for_type (comp, G_CALLBACK (_update_pipeline_func));
}

static void
_remove_seek_actions (NleComposition * comp)
{
  _remove_actions_for_type (comp, G_CALLBACK (_seek_pipeline_func));
}

static void
_add_update_compo_action (NleComposition * comp,
    GCallback callback, NleUpdateStackReason reason)
{
  UpdateCompositionData *ucompo = g_slice_new0 (UpdateCompositionData);

  ucompo->comp = comp;
  ucompo->reason = reason;
  ucompo->seqnum = gst_util_seqnum_next ();

  GST_INFO_OBJECT (comp, "Updating because: %s -- Setting seqnum: %i",
      UPDATE_PIPELINE_REASONS[reason], ucompo->seqnum);

  _add_action (comp, callback, ucompo, G_PRIORITY_DEFAULT);
}

static void
nle_composition_handle_message (GstBin * bin, GstMessage * message)
{
  NleComposition *comp = (NleComposition *) bin;

  if (comp->priv->tearing_down_stack) {
    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
      GST_FIXME_OBJECT (comp, "Dropping %" GST_PTR_FORMAT " message from "
          " %" GST_PTR_FORMAT " being teared down to READY",
          message, GST_MESSAGE_SRC (message));
    }

    GST_DEBUG_OBJECT (comp, "Dropping message %" GST_PTR_FORMAT " from "
        "object being teared down to READY!", message);
    gst_message_unref (message);

    return;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static void
nle_composition_class_init (NleCompositionClass * klass)
{
  gpointer useless;
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  NleObjectClass *nleobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;
  nleobject_class = (NleObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (NleCompositionPrivate));

  gst_element_class_set_static_metadata (gstelement_class,
      "GNonLin Composition", "Filter/Editor", "Combines NLE objects",
      "Wim Taymans <wim.taymans@gmail.com>, Edward Hervey <bilboed@bilboed.com>,"
      " Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>,"
      " Thibault Saunier <tsaunier@gnome.org>");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (nle_composition_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (nle_composition_finalize);

  gstelement_class->change_state = nle_composition_change_state;

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (nle_composition_add_object);
  gstbin_class->remove_element =
      GST_DEBUG_FUNCPTR (nle_composition_remove_object);
  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (nle_composition_handle_message);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&nle_composition_src_template));

  /* Get the paramspec of the NleObject klass so we can do
   * fast notifies */
  nleobject_properties[NLEOBJECT_PROP_START] =
      g_object_class_find_property (gobject_class, "start");
  nleobject_properties[NLEOBJECT_PROP_STOP] =
      g_object_class_find_property (gobject_class, "stop");
  nleobject_properties[NLEOBJECT_PROP_DURATION] =
      g_object_class_find_property (gobject_class, "duration");

  /**
   * NleComposition::commit
   * @comp: a #NleComposition
   * @recurse: Whether to commit recursiverly into (NleComposition) children of
   *           @object. This is used in case we have composition inside
   *           a nlesource composition, telling it to commit the included
   *           composition state.
   *
   * Action signal to commit all the pending changes of the composition and
   * its children timing properties
   *
   * Returns: %TRUE if changes have been commited, %FALSE if nothing had to
   * be commited
   */
  _signals[COMMIT_SIGNAL] = g_signal_new ("commit", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (NleObjectClass, commit_signal_handler), NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 1, G_TYPE_BOOLEAN);

  _signals[COMMITED_SIGNAL] =
      g_signal_new ("commited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1,
      G_TYPE_BOOLEAN);

  useless = GST_DEBUG_FUNCPTR (_seek_pipeline_func);
  useless = GST_DEBUG_FUNCPTR (_remove_object_func);
  useless = GST_DEBUG_FUNCPTR (_add_object_func);
  useless = GST_DEBUG_FUNCPTR (_update_pipeline_func);
  useless = GST_DEBUG_FUNCPTR (_commit_func);
  useless = GST_DEBUG_FUNCPTR (_emit_commited_signal_func);
  useless = GST_DEBUG_FUNCPTR (_initialize_stack_func);

  /* Just be useless, so the compiler does not warn us
   * about our uselessness */
  nleobject_class->commit = useless;
  nleobject_class->commit = nle_composition_commit_func;

}

static void
nle_composition_init (NleComposition * comp)
{
  NleCompositionPrivate *priv;

  GST_OBJECT_FLAG_SET (comp, NLE_OBJECT_SOURCE);
  GST_OBJECT_FLAG_SET (comp, NLE_OBJECT_COMPOSITION);

  priv = G_TYPE_INSTANCE_GET_PRIVATE (comp, NLE_TYPE_COMPOSITION,
      NleCompositionPrivate);
  priv->objects_start = NULL;
  priv->objects_stop = NULL;

  priv->segment = gst_segment_new ();
  priv->outside_segment = gst_segment_new ();

  g_rec_mutex_init (&comp->task_rec_lock);

  priv->objects_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

  g_mutex_init (&priv->actions_lock);
  g_cond_init (&priv->actions_cond);

  priv->pending_io = g_hash_table_new (g_direct_hash, g_direct_equal);

  comp->priv = priv;

  priv->current_bin = gst_bin_new ("current-bin");
  gst_bin_add (GST_BIN (comp), priv->current_bin);

  nle_composition_reset (comp);

  priv->nle_event_pad_func = GST_PAD_EVENTFUNC (NLE_OBJECT_SRC (comp));
  gst_pad_set_event_function (NLE_OBJECT_SRC (comp),
      GST_DEBUG_FUNCPTR (nle_composition_event_handler));
}

static void
nle_composition_dispose (GObject * object)
{
  GList *iter;
  NleComposition *comp = NLE_COMPOSITION (object);
  NleCompositionPrivate *priv = comp->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  iter = priv->objects_start;
  while (iter) {
    GList *next = iter->next;

    _nle_composition_remove_object (comp, iter->data);
    iter = next;
  }

  g_list_free (priv->objects_stop);

  if (priv->expandables) {
    GList *iter;

    iter = priv->expandables;

    while (iter) {
      GList *next = iter->next;

      _nle_composition_remove_object (comp, iter->data);
      iter = next;
    }

    priv->expandables = NULL;
  }

  nle_composition_reset_target_pad (comp);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
nle_composition_finalize (GObject * object)
{
  NleComposition *comp = NLE_COMPOSITION (object);
  NleCompositionPrivate *priv = comp->priv;

  _assert_proper_thread (comp);

  if (priv->current) {
    g_node_destroy (priv->current);
    priv->current = NULL;
  }

  g_hash_table_destroy (priv->objects_hash);

  gst_segment_free (priv->segment);
  gst_segment_free (priv->outside_segment);

  g_rec_mutex_clear (&comp->task_rec_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);

  g_mutex_clear (&priv->actions_lock);
  g_cond_clear (&priv->actions_cond);
}

/* signal_duration_change
 * Creates a new GST_MESSAGE_DURATION_CHANGED with the currently configured
 * composition duration and sends that on the bus.
 */
static inline void
signal_duration_change (NleComposition * comp)
{
  gst_element_post_message (GST_ELEMENT_CAST (comp),
      gst_message_new_duration_changed (GST_OBJECT_CAST (comp)));
}

static gboolean
_remove_child (GValue * item, GValue * ret G_GNUC_UNUSED, GstBin * bin)
{
  GstElement *child = g_value_get_object (item);

  if (NLE_IS_OPERATION (child))
    nle_operation_hard_cleanup (NLE_OPERATION (child));


  gst_bin_remove (bin, child);

  return TRUE;
}

static void
_empty_bin (GstBin * bin)
{
  GstIterator *children;

  children = gst_bin_iterate_elements (bin);

  while (G_UNLIKELY (gst_iterator_fold (children,
              (GstIteratorFoldFunction) _remove_child, NULL,
              bin) == GST_ITERATOR_RESYNC)) {
    gst_iterator_resync (children);
  }

  gst_iterator_free (children);
}

static void
nle_composition_reset (NleComposition * comp)
{
  NleCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "resetting");

  _assert_proper_thread (comp);

  priv->segment_start = GST_CLOCK_TIME_NONE;
  priv->segment_stop = GST_CLOCK_TIME_NONE;
  priv->next_base_time = 0;

  gst_segment_init (priv->segment, GST_FORMAT_TIME);
  gst_segment_init (priv->outside_segment, GST_FORMAT_TIME);

  if (priv->current)
    g_node_destroy (priv->current);
  priv->current = NULL;

  nle_composition_reset_target_pad (comp);

  priv->initialized = FALSE;
  priv->send_stream_start = TRUE;
  priv->real_eos_seqnum = 0;
  priv->next_eos_seqnum = 0;
  priv->flush_seqnum = 0;

  _empty_bin (GST_BIN_CAST (priv->current_bin));

  GST_DEBUG_OBJECT (comp, "Composition now resetted");
}

static GstPadProbeReturn
ghost_event_probe_handler (GstPad * ghostpad G_GNUC_UNUSED,
    GstPadProbeInfo * info, NleComposition * comp)
{
  GstPadProbeReturn retval = GST_PAD_PROBE_OK;
  NleCompositionPrivate *priv = comp->priv;
  GstEvent *event;

  if (GST_IS_BUFFER (info->data)) {
    if (priv->waiting_for_buffer)
      _restart_task (comp);

    return GST_PAD_PROBE_OK;
  }

  event = GST_PAD_PROBE_INFO_EVENT (info);

  GST_DEBUG_OBJECT (comp, "event: %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      if (_is_ready_to_restart_task (comp, event))
        _restart_task (comp);

      if (gst_event_get_seqnum (event) != comp->priv->flush_seqnum) {
        GST_INFO_OBJECT (comp, "Dropping flush stop %d -- %d",
            gst_event_get_seqnum (event), priv->seqnum_to_restart_task);
        retval = GST_PAD_PROBE_DROP;
      } else {
        GST_INFO_OBJECT (comp, "Forwarding our flush stop with seqnum %i",
            comp->priv->flush_seqnum);
        gst_event_unref (event);
        event = gst_event_new_flush_stop (TRUE);
        GST_PAD_PROBE_INFO_DATA (info) = event;
        gst_event_set_seqnum (event, comp->priv->flush_seqnum);
        comp->priv->flush_seqnum = 0;
      }
      break;
    case GST_EVENT_FLUSH_START:
      if (gst_event_get_seqnum (event) != comp->priv->flush_seqnum) {
        GST_INFO_OBJECT (comp, "Dropping flush start");
        retval = GST_PAD_PROBE_DROP;
      } else {
        GST_INFO_OBJECT (comp, "Forwarding our flush start with seqnum %i",
            comp->priv->flush_seqnum);
      }
      break;
    case GST_EVENT_STREAM_START:
      if (g_atomic_int_compare_and_exchange (&priv->send_stream_start, TRUE,
              FALSE)) {
        /* FIXME: Do we want to create a new stream ID here? */
        GST_DEBUG_OBJECT (comp, "forward stream-start %p", event);
      } else {
        GST_DEBUG_OBJECT (comp, "dropping stream-start %p", event);
        retval = GST_PAD_PROBE_DROP;
      }
      break;
    case GST_EVENT_SEGMENT:
    {
      guint64 rstart, rstop;
      const GstSegment *segment;
      GstSegment copy;
      GstEvent *event2;
      /* next_base_time */

      if (_is_ready_to_restart_task (comp, event))
        _restart_task (comp);

      gst_event_parse_segment (event, &segment);
      gst_segment_copy_into (segment, &copy);

      rstart =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME,
          segment->start);
      rstop =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME, segment->stop);
      copy.base = comp->priv->next_base_time;
      GST_DEBUG_OBJECT (comp,
          "Updating base time to %" GST_TIME_FORMAT ", next:%" GST_TIME_FORMAT,
          GST_TIME_ARGS (comp->priv->next_base_time),
          GST_TIME_ARGS (comp->priv->next_base_time + rstop - rstart));
      comp->priv->next_base_time += rstop - rstart;

      event2 = gst_event_new_segment (&copy);
      GST_EVENT_SEQNUM (event2) = GST_EVENT_SEQNUM (event);

      GST_PAD_PROBE_INFO_DATA (info) = event2;
      gst_event_unref (event);
    }
      break;
    case GST_EVENT_EOS:
    {
      gint seqnum = gst_event_get_seqnum (event);

      GST_INFO_OBJECT (comp, "Got EOS, last EOS seqnum id : %i current "
          "seq num is: %i", comp->priv->real_eos_seqnum, seqnum);

      if (_is_ready_to_restart_task (comp, event)) {
        GST_INFO_OBJECT (comp, "We got an EOS right after seeing the right"
            " segment, restarting task");

        _restart_task (comp);
      }

      if (g_atomic_int_compare_and_exchange (&comp->priv->real_eos_seqnum,
              seqnum, 1)) {

        GST_INFO_OBJECT (comp, "Got EOS for real, seq ID is %i, fowarding it",
            seqnum);

        return GST_PAD_PROBE_OK;
      }

      if (priv->next_eos_seqnum == seqnum)
        _add_update_compo_action (comp, G_CALLBACK (_update_pipeline_func),
            COMP_UPDATE_STACK_ON_EOS);
      else
        GST_INFO_OBJECT (comp,
            "Got an EOS but it seqnum %i != next eos seqnum %i", seqnum,
            priv->next_eos_seqnum);

      retval = GST_PAD_PROBE_DROP;
    }
      break;
    default:
      break;
  }

  return retval;
}

static gint
priority_comp (NleObject * a, NleObject * b)
{
  if (a->priority < b->priority)
    return -1;

  if (a->priority > b->priority)
    return 1;

  return 0;
}

static inline gboolean
have_to_update_pipeline (NleComposition * comp,
    NleUpdateStackReason update_stack_reason)
{
  NleCompositionPrivate *priv = comp->priv;

  if (update_stack_reason == COMP_UPDATE_STACK_ON_EOS)
    return TRUE;

  GST_DEBUG_OBJECT (comp,
      "segment[%" GST_TIME_FORMAT "--%" GST_TIME_FORMAT "] current[%"
      GST_TIME_FORMAT "--%" GST_TIME_FORMAT "]",
      GST_TIME_ARGS (priv->segment->start),
      GST_TIME_ARGS (priv->segment->stop),
      GST_TIME_ARGS (priv->segment_start), GST_TIME_ARGS (priv->segment_stop));

  if (priv->segment->start < priv->segment_start)
    return TRUE;

  if (priv->segment->start >= priv->segment_stop)
    return TRUE;

  return FALSE;
}

static gboolean
nle_composition_commit_func (NleObject * object, gboolean recurse)
{
  _add_update_compo_action (NLE_COMPOSITION (object),
      G_CALLBACK (_commit_func), COMP_UPDATE_STACK_ON_COMMIT);

  return TRUE;
}

/*
 * get_new_seek_event:
 *
 * Returns a seek event for the currently configured segment
 * and start/stop values
 *
 * The GstSegment and segment_start|stop must have been configured
 * before calling this function.
 */
static GstEvent *
get_new_seek_event (NleComposition * comp, gboolean initial,
    gboolean updatestoponly)
{
  GstSeekFlags flags = GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;
  gint64 start, stop;
  GstSeekType starttype = GST_SEEK_TYPE_SET;
  NleCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "initial:%d", initial);
  /* remove the seek flag */
  if (!initial)
    flags |= (GstSeekFlags) priv->segment->flags;

  GST_DEBUG_OBJECT (comp,
      "private->segment->start:%" GST_TIME_FORMAT " segment_start%"
      GST_TIME_FORMAT, GST_TIME_ARGS (priv->segment->start),
      GST_TIME_ARGS (priv->segment_start));

  GST_DEBUG_OBJECT (comp,
      "private->segment->stop:%" GST_TIME_FORMAT " segment_stop%"
      GST_TIME_FORMAT, GST_TIME_ARGS (priv->segment->stop),
      GST_TIME_ARGS (priv->segment_stop));

  start = MAX (priv->segment->start, priv->segment_start);
  stop = GST_CLOCK_TIME_IS_VALID (priv->segment->stop)
      ? MIN (priv->segment->stop, priv->segment_stop)
      : priv->segment_stop;

  if (updatestoponly) {
    starttype = GST_SEEK_TYPE_NONE;
    start = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG_OBJECT (comp,
      "Created new seek event. Flags:%d, start:%" GST_TIME_FORMAT ", stop:%"
      GST_TIME_FORMAT ", rate:%lf", flags, GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop), priv->segment->rate);

  return gst_event_new_seek (priv->segment->rate,
      priv->segment->format, flags, starttype, start, GST_SEEK_TYPE_SET, stop);
}

/* OBJECTS LOCK must be taken when calling this ! */
static GstClockTime
get_current_position (NleComposition * comp)
{
  GstPad *pad;
  NleObject *obj;
  NleCompositionPrivate *priv = comp->priv;
  gboolean res;
  gint64 value = GST_CLOCK_TIME_NONE;

  GstPad *peer = gst_pad_get_peer (NLE_OBJECT (comp)->srcpad);

  /* Try querying position downstream */

  if (peer) {
    res = gst_pad_query_position (peer, GST_FORMAT_TIME, &value);
    gst_object_unref (peer);

    if (res) {
      GST_DEBUG_OBJECT (comp,
          "Successfully got downstream position %" GST_TIME_FORMAT,
          GST_TIME_ARGS ((guint64) value));
      goto beach;
    }
  }

  GST_DEBUG_OBJECT (comp, "Downstream position query failed");

  /* resetting format/value */
  value = GST_CLOCK_TIME_NONE;

  /* If downstream fails , try within the current stack */
  if (!priv->current) {
    GST_DEBUG_OBJECT (comp, "No current stack, can't send query");
    goto beach;
  }

  obj = (NleObject *) priv->current->data;

  pad = NLE_OBJECT_SRC (obj);
  res = gst_pad_query_position (pad, GST_FORMAT_TIME, &value);

  if (G_UNLIKELY (res == FALSE)) {
    GST_WARNING_OBJECT (comp, "query position failed");
    value = GST_CLOCK_TIME_NONE;
  } else {
    GST_LOG_OBJECT (comp, "Query returned %" GST_TIME_FORMAT,
        GST_TIME_ARGS ((guint64) value));
  }

beach:

  if (!GST_CLOCK_TIME_IS_VALID (value)) {
    if (GST_CLOCK_TIME_IS_VALID (comp->priv->segment_start)) {
      value = comp->priv->segment_start;
    } else {
      GST_INFO_OBJECT (comp, "Current position is unknown, " "setting it to 0");

      value = 0;
    }
  }

  return (guint64) value;
}

/* WITH OBJECTS LOCK TAKEN */
static gboolean
update_base_time (GNode * node, GstClockTime * timestamp)
{
  if (NLE_IS_OPERATION (node->data))
    nle_operation_update_base_time (NLE_OPERATION (node->data), *timestamp);

  return FALSE;
}

/* WITH OBJECTS LOCK TAKEN */
static void
update_operations_base_time (NleComposition * comp, gboolean reverse)
{
  GstClockTime timestamp;

  if (reverse)
    timestamp = comp->priv->segment->stop;
  else
    timestamp = comp->priv->segment->start;

  g_node_traverse (comp->priv->current, G_IN_ORDER, G_TRAVERSE_ALL, -1,
      (GNodeTraverseFunc) update_base_time, &timestamp);
}

/* WITH OBJECTS LOCK TAKEN */
static gboolean
_seek_current_stack (NleComposition * comp, GstEvent * event,
    gboolean flush_downstream)
{
  gboolean res;
  NleCompositionPrivate *priv = comp->priv;
  GstPad *peer = gst_pad_get_peer (NLE_OBJECT_SRC (comp));

  GST_INFO_OBJECT (comp, "Seeking itself %" GST_PTR_FORMAT, event);

  if (flush_downstream) {
    priv->flush_seqnum = gst_event_get_seqnum (event);
    GST_INFO_OBJECT (comp, "sending flushes downstream with seqnum %d",
        priv->flush_seqnum);
  }

  priv->seeking_itself = TRUE;
  res = gst_pad_push_event (peer, event);
  priv->seeking_itself = FALSE;
  gst_object_unref (peer);

  GST_DEBUG_OBJECT (comp, "Done seeking");

  return res;
}

/*
  Figures out if pipeline needs updating.
  Updates it and sends the seek event.
  Sends flush events downstream if needed.
  can be called by user_seek or segment_done

  update_stack_reason: The reason for which we need to handle 'seek'
*/

static gboolean
seek_handling (NleComposition * comp, gint32 seqnum,
    NleUpdateStackReason update_stack_reason)
{
  GST_DEBUG_OBJECT (comp, "Seek hanlding update pipeline reason: %s",
      UPDATE_PIPELINE_REASONS[update_stack_reason]);

  if (have_to_update_pipeline (comp, update_stack_reason)) {
    if (comp->priv->segment->rate >= 0.0)
      update_pipeline (comp, comp->priv->segment->start, seqnum,
          update_stack_reason);
    else
      update_pipeline (comp, comp->priv->segment->stop, seqnum,
          update_stack_reason);
  } else {
    GstEvent *toplevel_seek = get_new_seek_event (comp, FALSE, FALSE);

    gst_event_set_seqnum (toplevel_seek, seqnum);
    _set_real_eos_seqnum_from_seek (comp, toplevel_seek);

    _remove_update_actions (comp);
    update_operations_base_time (comp, !(comp->priv->segment->rate >= 0.0));
    _seek_current_stack (comp, toplevel_seek,
        _have_to_flush_downstream (update_stack_reason));
  }

  return TRUE;
}

static gboolean
nle_composition_event_handler (GstPad * ghostpad, GstObject * parent,
    GstEvent * event)
{
  NleComposition *comp = (NleComposition *) parent;
  NleCompositionPrivate *priv = comp->priv;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (comp, "event type:%s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      if (!priv->seeking_itself) {
        _add_seek_action (comp, event);
        event = NULL;
        GST_FIXME_OBJECT (comp, "HANDLE seeking errors!");

        return TRUE;
      }
      break;
    }
    case GST_EVENT_QOS:
    {
      gdouble prop;
      GstQOSType qostype;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &qostype, &prop, &diff, &timestamp);

      GST_DEBUG_OBJECT (comp,
          "timestamp:%" GST_TIME_FORMAT " segment.start:%" GST_TIME_FORMAT
          " segment.stop:%" GST_TIME_FORMAT " segment_start%" GST_TIME_FORMAT
          " segment_stop:%" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
          GST_TIME_ARGS (priv->outside_segment->start),
          GST_TIME_ARGS (priv->outside_segment->stop),
          GST_TIME_ARGS (priv->segment_start),
          GST_TIME_ARGS (priv->segment_stop));

      /* The problem with QoS events is the following:
       * At each new internal segment (i.e. when we re-arrange our internal
       * elements) we send flushing seeks to those elements (to properly
       * configure their playback range) but don't let the FLUSH events get
       * downstream.
       *
       * The problem is that the QoS running timestamps we receive from
       * downstream will not have taken into account those flush.
       *
       * What we need to do is to translate to our internal running timestamps
       * which for each configured segment starts at 0 for those elements.
       *
       * The generic algorithm for the incoming running timestamp translation
       * is therefore:
       *     (original_seek_time : original seek position received from usptream)
       *     (current_segment_start : Start position of the currently configured
       *                              timeline segment)
       *
       *     difference = original_seek_time - current_segment_start
       *     new_qos_position = upstream_qos_position - difference
       *
       * The new_qos_position is only valid when:
       *    * it applies to the current segment (difference > 0)
       *    * The QoS difference + timestamp is greater than the difference
       *
       */

      if (GST_CLOCK_TIME_IS_VALID (priv->outside_segment->start)) {
        GstClockTimeDiff curdiff;

        /* We'll either create a new event or discard it */
        gst_event_unref (event);

        if (priv->segment->rate < 0.0)
          curdiff = priv->outside_segment->stop - priv->segment_stop;
        else
          curdiff = priv->segment_start - priv->outside_segment->start;
        GST_DEBUG ("curdiff %" GST_TIME_FORMAT, GST_TIME_ARGS (curdiff));
        if ((curdiff != 0) && ((timestamp < curdiff)
                || (curdiff > timestamp + diff))) {
          GST_DEBUG_OBJECT (comp,
              "QoS event outside of current segment, discarding");
          /* The QoS timestamp is before the currently set-up pipeline */
          goto beach;
        }

        /* Substract the amount of running time we've already outputted
         * until the currently configured pipeline from the QoS timestamp.*/
        timestamp -= curdiff;
        GST_DEBUG_OBJECT (comp,
            "Creating new QoS event with timestamp %" GST_TIME_FORMAT,
            GST_TIME_ARGS (timestamp));
        event = gst_event_new_qos (qostype, prop, diff, timestamp);
      }
      break;
    }
    default:
      break;
  }

  if (res) {
    GST_DEBUG_OBJECT (comp, "About to call nle_event_pad_func: %p",
        priv->nle_event_pad_func);
    res = priv->nle_event_pad_func (NLE_OBJECT (comp)->srcpad, parent, event);
    GST_DEBUG_OBJECT (comp, "Done calling nle_event_pad_func() %d", res);
  }

beach:
  return res;
}

static inline void
nle_composition_reset_target_pad (NleComposition * comp)
{
  NleCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "Removing ghostpad");

  if (priv->ghosteventprobe) {
    GstPad *target;

    target = gst_ghost_pad_get_target ((GstGhostPad *) NLE_OBJECT_SRC (comp));
    if (target)
      gst_pad_remove_probe (target, priv->ghosteventprobe);
    priv->ghosteventprobe = 0;
  }

  nle_object_ghost_pad_set_target (NLE_OBJECT (comp),
      NLE_OBJECT_SRC (comp), NULL);
  priv->send_stream_start = TRUE;
}

/* nle_composition_ghost_pad_set_target:
 * target: The target #GstPad. The refcount will be decremented (given to the ghostpad).
 */
static void
nle_composition_ghost_pad_set_target (NleComposition * comp, GstPad * target)
{
  GstPad *ptarget;
  NleCompositionPrivate *priv = comp->priv;

  if (target)
    GST_DEBUG_OBJECT (comp, "target:%s:%s", GST_DEBUG_PAD_NAME (target));
  else
    GST_DEBUG_OBJECT (comp, "Removing target");


  ptarget =
      gst_ghost_pad_get_target (GST_GHOST_PAD (NLE_OBJECT (comp)->srcpad));
  if (ptarget) {
    gst_object_unref (ptarget);

    if (ptarget == target) {
      GST_DEBUG_OBJECT (comp,
          "Target of srcpad is the same as existing one, not changing");
      return;
    }
  }

  /* Actually set the target */
  nle_object_ghost_pad_set_target ((NleObject *) comp,
      NLE_OBJECT (comp)->srcpad, target);

  if (target && (priv->ghosteventprobe == 0)) {
    priv->ghosteventprobe =
        gst_pad_add_probe (target,
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH |
        GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
        (GstPadProbeCallback) ghost_event_probe_handler, comp, NULL);
    GST_DEBUG_OBJECT (comp, "added event probe %lu", priv->ghosteventprobe);
  }
}

static void
refine_start_stop_in_region_above_priority (NleComposition * composition,
    GstClockTime timestamp, GstClockTime start,
    GstClockTime stop,
    GstClockTime * rstart, GstClockTime * rstop, guint32 priority)
{
  GList *tmp;
  NleObject *object;
  GstClockTime nstart = start, nstop = stop;

  GST_DEBUG_OBJECT (composition,
      "timestamp:%" GST_TIME_FORMAT " start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " priority:%u", GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop), priority);

  for (tmp = composition->priv->objects_start; tmp; tmp = tmp->next) {
    object = (NleObject *) tmp->data;

    GST_LOG_OBJECT (object, "START %" GST_TIME_FORMAT "--%" GST_TIME_FORMAT,
        GST_TIME_ARGS (object->start), GST_TIME_ARGS (object->stop));

    if ((object->priority >= priority) || (!object->active))
      continue;

    if (object->start <= timestamp)
      continue;

    if (object->start >= nstop)
      continue;

    nstop = object->start;

    GST_DEBUG_OBJECT (composition,
        "START Found %s [prio:%u] at %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (object), object->priority,
        GST_TIME_ARGS (object->start));

    break;
  }

  for (tmp = composition->priv->objects_stop; tmp; tmp = tmp->next) {
    object = (NleObject *) tmp->data;

    GST_LOG_OBJECT (object, "STOP %" GST_TIME_FORMAT "--%" GST_TIME_FORMAT,
        GST_TIME_ARGS (object->start), GST_TIME_ARGS (object->stop));

    if ((object->priority >= priority) || (!object->active))
      continue;

    if (object->stop >= timestamp)
      continue;

    if (object->stop <= nstart)
      continue;

    nstart = object->stop;

    GST_DEBUG_OBJECT (composition,
        "STOP Found %s [prio:%u] at %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (object), object->priority,
        GST_TIME_ARGS (object->start));

    break;
  }

  if (*rstart)
    *rstart = nstart;

  if (*rstop)
    *rstop = nstop;
}


/*
 * Converts a sorted list to a tree
 * Recursive
 *
 * stack will be set to the next item to use in the parent.
 * If operations number of sinks is limited, it will only use that number.
 */

static GNode *
convert_list_to_tree (GList ** stack, GstClockTime * start,
    GstClockTime * stop, guint32 * highprio)
{
  GNode *ret;
  guint nbsinks;
  gboolean limit;
  GList *tmp;
  NleObject *object;

  if (!stack || !*stack)
    return NULL;

  object = (NleObject *) (*stack)->data;

  GST_DEBUG ("object:%s , *start:%" GST_TIME_FORMAT ", *stop:%"
      GST_TIME_FORMAT " highprio:%d",
      GST_ELEMENT_NAME (object), GST_TIME_ARGS (*start),
      GST_TIME_ARGS (*stop), *highprio);

  /* update earliest stop */
  if (GST_CLOCK_TIME_IS_VALID (*stop)) {
    if (GST_CLOCK_TIME_IS_VALID (object->stop) && (*stop > object->stop))
      *stop = object->stop;
  } else {
    *stop = object->stop;
  }

  if (GST_CLOCK_TIME_IS_VALID (*start)) {
    if (GST_CLOCK_TIME_IS_VALID (object->start) && (*start < object->start))
      *start = object->start;
  } else {
    *start = object->start;
  }

  if (NLE_OBJECT_IS_SOURCE (object)) {
    *stack = g_list_next (*stack);

    /* update highest priority.
     * We do this here, since it's only used with sources (leafs of the tree) */
    if (object->priority > *highprio)
      *highprio = object->priority;

    ret = g_node_new (object);

    goto beach;
  } else {
    /* NleOperation */
    NleOperation *oper = (NleOperation *) object;

    GST_LOG_OBJECT (oper, "operation, num_sinks:%d", oper->num_sinks);

    ret = g_node_new (object);
    limit = (oper->dynamicsinks == FALSE);
    nbsinks = oper->num_sinks;

    /* FIXME : if num_sinks == -1 : request the proper number of pads */
    for (tmp = g_list_next (*stack); tmp && (!limit || nbsinks);) {
      g_node_append (ret, convert_list_to_tree (&tmp, start, stop, highprio));
      if (limit)
        nbsinks--;
    }

    *stack = tmp;
  }

beach:
  GST_DEBUG_OBJECT (object,
      "*start:%" GST_TIME_FORMAT " *stop:%" GST_TIME_FORMAT
      " priority:%u", GST_TIME_ARGS (*start), GST_TIME_ARGS (*stop), *highprio);

  return ret;
}

/*
 * get_stack_list:
 * @comp: The #NleComposition
 * @timestamp: The #GstClockTime to look at
 * @priority: The priority level to start looking from
 * @activeonly: Only look for active elements if TRUE
 * @start: The biggest start time of the objects in the stack
 * @stop: The smallest stop time of the objects in the stack
 * @highprio: The highest priority in the stack
 *
 * Not MT-safe, you should take the objects lock before calling it.
 * Returns: A tree of #GNode sorted in priority order, corresponding
 * to the given search arguments. The returned value can be #NULL.
 *
 * WITH OBJECTS LOCK TAKEN
 */
static GNode *
get_stack_list (NleComposition * comp, GstClockTime timestamp,
    guint32 priority, gboolean activeonly, GstClockTime * start,
    GstClockTime * stop, guint * highprio)
{
  GList *tmp;
  GList *stack = NULL;
  GNode *ret = NULL;
  GstClockTime nstart = GST_CLOCK_TIME_NONE;
  GstClockTime nstop = GST_CLOCK_TIME_NONE;
  GstClockTime first_out_of_stack = GST_CLOCK_TIME_NONE;
  guint32 highest = 0;
  gboolean reverse = (comp->priv->segment->rate < 0.0);

  GST_DEBUG_OBJECT (comp,
      "timestamp:%" GST_TIME_FORMAT ", priority:%u, activeonly:%d",
      GST_TIME_ARGS (timestamp), priority, activeonly);

  GST_LOG ("objects_start:%p objects_stop:%p", comp->priv->objects_start,
      comp->priv->objects_stop);

  if (reverse) {
    for (tmp = comp->priv->objects_stop; tmp; tmp = g_list_next (tmp)) {
      NleObject *object = (NleObject *) tmp->data;

      GST_LOG_OBJECT (object,
          "start: %" GST_TIME_FORMAT ", stop:%" GST_TIME_FORMAT " , duration:%"
          GST_TIME_FORMAT ", priority:%u, active:%d",
          GST_TIME_ARGS (object->start), GST_TIME_ARGS (object->stop),
          GST_TIME_ARGS (object->duration), object->priority, object->active);

      if (object->stop >= timestamp) {
        if ((object->start < timestamp) &&
            (object->priority >= priority) &&
            ((!activeonly) || (object->active))) {
          GST_LOG_OBJECT (comp, "adding %s: sorted to the stack",
              GST_OBJECT_NAME (object));
          stack = g_list_insert_sorted (stack, object,
              (GCompareFunc) priority_comp);
          if (NLE_IS_OPERATION (object))
            nle_operation_update_base_time (NLE_OPERATION (object), timestamp);
        }
      } else {
        GST_LOG_OBJECT (comp, "too far, stopping iteration");
        first_out_of_stack = object->stop;
        break;
      }
    }
  } else {
    for (tmp = comp->priv->objects_start; tmp; tmp = g_list_next (tmp)) {
      NleObject *object = (NleObject *) tmp->data;

      GST_LOG_OBJECT (object,
          "start: %" GST_TIME_FORMAT " , stop:%" GST_TIME_FORMAT " , duration:%"
          GST_TIME_FORMAT ", priority:%u", GST_TIME_ARGS (object->start),
          GST_TIME_ARGS (object->stop), GST_TIME_ARGS (object->duration),
          object->priority);

      if (object->start <= timestamp) {
        if ((object->stop > timestamp) &&
            (object->priority >= priority) &&
            ((!activeonly) || (object->active))) {
          GST_LOG_OBJECT (comp, "adding %s: sorted to the stack",
              GST_OBJECT_NAME (object));
          stack = g_list_insert_sorted (stack, object,
              (GCompareFunc) priority_comp);
          if (NLE_IS_OPERATION (object))
            nle_operation_update_base_time (NLE_OPERATION (object), timestamp);
        }
      } else {
        GST_LOG_OBJECT (comp, "too far, stopping iteration");
        first_out_of_stack = object->start;
        break;
      }
    }
  }

  /* Insert the expandables */
  if (G_LIKELY (timestamp < NLE_OBJECT_STOP (comp)))
    for (tmp = comp->priv->expandables; tmp; tmp = tmp->next) {
      GST_DEBUG_OBJECT (comp, "Adding expandable %s sorted to the list",
          GST_OBJECT_NAME (tmp->data));
      stack = g_list_insert_sorted (stack, tmp->data,
          (GCompareFunc) priority_comp);
      if (NLE_IS_OPERATION (tmp->data))
        nle_operation_update_base_time (NLE_OPERATION (tmp->data), timestamp);
    }

  /* convert that list to a stack */
  tmp = stack;
  ret = convert_list_to_tree (&tmp, &nstart, &nstop, &highest);
  if (GST_CLOCK_TIME_IS_VALID (first_out_of_stack)) {
    if (reverse && nstart < first_out_of_stack)
      nstart = first_out_of_stack;
    else if (!reverse && nstop > first_out_of_stack)
      nstop = first_out_of_stack;
  }

  GST_DEBUG ("nstart:%" GST_TIME_FORMAT ", nstop:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (nstart), GST_TIME_ARGS (nstop));

  if (*stop)
    *stop = nstop;
  if (*start)
    *start = nstart;
  if (highprio)
    *highprio = highest;

  g_list_free (stack);

  return ret;
}

/*
 * get_clean_toplevel_stack:
 * @comp: The #NleComposition
 * @timestamp: The #GstClockTime to look at
 * @stop_time: Pointer to a #GstClockTime for min stop time of returned stack
 * @start_time: Pointer to a #GstClockTime for greatest start time of returned stack
 *
 * Returns: The new current stack for the given #NleComposition and @timestamp.
 *
 * WITH OBJECTS LOCK TAKEN
 */
static GNode *
get_clean_toplevel_stack (NleComposition * comp, GstClockTime * timestamp,
    GstClockTime * start_time, GstClockTime * stop_time)
{
  GNode *stack = NULL;
  GstClockTime start = G_MAXUINT64;
  GstClockTime stop = G_MAXUINT64;
  guint highprio;
  gboolean reverse = (comp->priv->segment->rate < 0.0);

  GST_DEBUG_OBJECT (comp, "timestamp:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (*timestamp));
  GST_DEBUG ("start:%" GST_TIME_FORMAT ", stop:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  stack = get_stack_list (comp, *timestamp, 0, TRUE, &start, &stop, &highprio);

  if (!stack &&
      ((reverse && (*timestamp > COMP_REAL_START (comp))) ||
          (!reverse && (*timestamp < COMP_REAL_STOP (comp))))) {
    GST_ELEMENT_ERROR (comp, STREAM, WRONG_TYPE,
        ("Gaps ( at %" GST_TIME_FORMAT
            ") in the stream is not supported, the application is responsible"
            " for filling them", GST_TIME_ARGS (*timestamp)),
        ("Gap in the composition this should never"
            "append, make sure to fill them"));

    return NULL;
  }

  GST_DEBUG ("start:%" GST_TIME_FORMAT ", stop:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  if (stack) {
    guint32 top_priority = NLE_OBJECT_PRIORITY (stack->data);

    /* Figure out if there's anything blocking us with smaller priority */
    refine_start_stop_in_region_above_priority (comp, *timestamp, start,
        stop, &start, &stop, (highprio == 0) ? top_priority : highprio);
  }

  if (*stop_time) {
    if (stack)
      *stop_time = stop;
    else
      *stop_time = 0;
  }

  if (*start_time) {
    if (stack)
      *start_time = start;
    else
      *start_time = 0;
  }

  GST_DEBUG_OBJECT (comp,
      "Returning timestamp:%" GST_TIME_FORMAT " , start_time:%"
      GST_TIME_FORMAT " , stop_time:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (*timestamp), GST_TIME_ARGS (*start_time),
      GST_TIME_ARGS (*stop_time));

  return stack;
}

static GstPadProbeReturn
_drop_all_cb (GstPad * pad G_GNUC_UNUSED,
    GstPadProbeInfo * info, NleComposition * comp)
{
  return GST_PAD_PROBE_DROP;
}

/*  Must be called with OBJECTS_LOCK taken */
static void
_set_current_bin_to_ready (NleComposition * comp, gboolean flush_downstream)
{
  gint probe_id = -1;
  GstPad *ptarget = NULL;
  NleCompositionPrivate *priv = comp->priv;
  GstEvent *flush_event;

  comp->priv->tearing_down_stack = TRUE;
  if (flush_downstream) {
    ptarget = gst_ghost_pad_get_target (GST_GHOST_PAD (NLE_OBJECT_SRC (comp)));
    if (ptarget) {

      /* Make sure that between the flush_start/flush_stop
       * and the time we set the current_bin to READY, no
       * buffer can ever get prerolled which would lead to
       * a deadlock */
      probe_id = gst_pad_add_probe (ptarget,
          GST_PAD_PROBE_TYPE_DATA_BOTH | GST_PAD_PROBE_TYPE_EVENT_BOTH,
          (GstPadProbeCallback) _drop_all_cb, comp, NULL);

      GST_DEBUG_OBJECT (comp, "added event probe %lu", priv->ghosteventprobe);

      flush_event = gst_event_new_flush_start ();
      priv->flush_seqnum = gst_event_get_seqnum (flush_event);
      GST_INFO_OBJECT (comp, "sending flushes downstream with seqnum %d",
          priv->flush_seqnum);
      gst_pad_push_event (ptarget, flush_event);

    }

  }

  gst_element_set_locked_state (priv->current_bin, TRUE);
  gst_element_set_state (priv->current_bin, GST_STATE_READY);

  if (ptarget) {
    if (flush_downstream) {
      flush_event = gst_event_new_flush_stop (TRUE);
      gst_event_set_seqnum (flush_event, priv->flush_seqnum);

      /* Force ad activation so that the event can actually travel.
       * Not doing that would lead to the event being discarded.
       */
      gst_pad_set_active (ptarget, TRUE);
      gst_pad_push_event (ptarget, flush_event);
      gst_pad_set_active (ptarget, FALSE);
    }

    gst_pad_remove_probe (ptarget, probe_id);
    gst_object_unref (ptarget);
  }

  comp->priv->tearing_down_stack = FALSE;
}

static void
_emit_commited_signal_func (NleComposition * comp, gpointer udata)
{
  GST_INFO_OBJECT (comp, "Emiting COMMITED now that the stack " "is ready");

  g_signal_emit (comp, _signals[COMMITED_SIGNAL], 0, TRUE);
}

static void
_restart_task (NleComposition * comp)
{
  GST_INFO_OBJECT (comp, "Restarting task! after %s DONE",
      UPDATE_PIPELINE_REASONS[comp->priv->updating_reason]);

  if (comp->priv->updating_reason == COMP_UPDATE_STACK_ON_COMMIT)
    _add_action (comp, G_CALLBACK (_emit_commited_signal_func), comp,
        G_PRIORITY_HIGH);

  comp->priv->seqnum_to_restart_task = 0;
  comp->priv->waiting_for_buffer = FALSE;

  comp->priv->updating_reason = COMP_UPDATE_STACK_NONE;
  gst_task_start (comp->task);
}

static gboolean
_is_ready_to_restart_task (NleComposition * comp, GstEvent * event)
{
  NleCompositionPrivate *priv = comp->priv;
  gint seqnum = gst_event_get_seqnum (event);


  if (comp->priv->seqnum_to_restart_task == seqnum) {
    gchar *name = g_strdup_printf ("new-stack__%" GST_TIME_FORMAT "--%"
        GST_TIME_FORMAT "", GST_TIME_ARGS (comp->priv->segment_start),
        GST_TIME_ARGS (comp->priv->segment_stop));

    GST_INFO_OBJECT (comp, "Got %s with proper seqnum"
        " done with stack reconfiguration %" GST_PTR_FORMAT,
        GST_EVENT_TYPE_NAME (event), event);

    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (comp),
        GST_DEBUG_GRAPH_SHOW_ALL, name);
    g_free (name);

    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
      return TRUE;

    priv->waiting_for_buffer = TRUE;
    return FALSE;

  } else if (comp->priv->seqnum_to_restart_task) {
    GST_INFO_OBJECT (comp, "WARNING: %s seqnum %i != wanted %i",
        GST_EVENT_TYPE_NAME (event), seqnum,
        comp->priv->seqnum_to_restart_task);
  }

  return FALSE;
}

static void
_commit_func (NleComposition * comp, UpdateCompositionData * ucompo)
{
  GstClockTime curpos;
  NleCompositionPrivate *priv = comp->priv;

  _post_start_composition_update (comp, ucompo->seqnum, ucompo->reason);

  /* Get current so that it represent the duration it was
   * before commiting children */
  curpos = get_current_position (comp);

  if (!_commit_all_values (comp)) {
    GST_DEBUG_OBJECT (comp, "Nothing to commit, leaving");

    g_signal_emit (comp, _signals[COMMITED_SIGNAL], 0, FALSE);
    _post_start_composition_update_done (comp, ucompo->seqnum, ucompo->reason);

    return;
  }

  if (priv->initialized == FALSE) {
    GST_DEBUG_OBJECT (comp, "Not initialized yet, just updating values");

    update_start_stop_duration (comp);

    g_signal_emit (comp, _signals[COMMITED_SIGNAL], 0, TRUE);

  } else {
    /* And update the pipeline at current position if needed */

    update_start_stop_duration (comp);
    update_pipeline (comp, curpos, ucompo->seqnum, COMP_UPDATE_STACK_ON_COMMIT);

    if (!priv->current) {
      GST_INFO_OBJECT (comp, "No new stack set, we can go and keep acting on"
          " our children");

      g_signal_emit (comp, _signals[COMMITED_SIGNAL], 0, TRUE);
    }
  }

  _post_start_composition_update_done (comp, ucompo->seqnum, ucompo->reason);
}

static void
_update_pipeline_func (NleComposition * comp, UpdateCompositionData * ucompo)
{
  gboolean reverse;
  NleCompositionPrivate *priv = comp->priv;

  _post_start_composition_update (comp, ucompo->seqnum, ucompo->reason);

  /* Set up a non-initial seek on segment_stop */
  reverse = (priv->segment->rate < 0.0);
  if (!reverse) {
    GST_DEBUG_OBJECT (comp,
        "Setting segment->start to segment_stop:%" GST_TIME_FORMAT,
        GST_TIME_ARGS (priv->segment_stop));
    priv->segment->start = priv->segment_stop;
  } else {
    GST_DEBUG_OBJECT (comp,
        "Setting segment->stop to segment_start:%" GST_TIME_FORMAT,
        GST_TIME_ARGS (priv->segment_start));
    priv->segment->stop = priv->segment_start;
  }

  seek_handling (comp, ucompo->seqnum, COMP_UPDATE_STACK_ON_EOS);

  /* Post segment done if last seek was a segment seek */
  if (!priv->current && (priv->segment->flags & GST_SEEK_FLAG_SEGMENT)) {
    gint64 epos;

    if (GST_CLOCK_TIME_IS_VALID (priv->segment->stop))
      epos = (MIN (priv->segment->stop, NLE_OBJECT_STOP (comp)));
    else
      epos = NLE_OBJECT_STOP (comp);

    GST_LOG_OBJECT (comp, "Emitting segment done pos %" GST_TIME_FORMAT,
        GST_TIME_ARGS (epos));
    gst_element_post_message (GST_ELEMENT_CAST (comp),
        gst_message_new_segment_done (GST_OBJECT (comp),
            priv->segment->format, epos));
    gst_pad_push_event (NLE_OBJECT (comp)->srcpad,
        gst_event_new_segment_done (priv->segment->format, epos));
  }

  _post_start_composition_update_done (comp, ucompo->seqnum, ucompo->reason);
}

static void
_set_all_children_state (NleComposition * comp, GstState state)
{
  GList *tmp;

  GST_DEBUG_OBJECT (comp, "Setting all children state to %s",
      gst_element_state_get_name (state));

  comp->priv->tearing_down_stack = TRUE;
  gst_element_set_state (comp->priv->current_bin, state);
  for (tmp = comp->priv->objects_start; tmp; tmp = tmp->next)
    gst_element_set_state (tmp->data, state);

  for (tmp = comp->priv->expandables; tmp; tmp = tmp->next)
    gst_element_set_state (tmp->data, state);
  comp->priv->tearing_down_stack = FALSE;
}

static GstStateChangeReturn
nle_composition_change_state (GstElement * element, GstStateChange transition)
{
  NleComposition *comp = (NleComposition *) element;

  GST_DEBUG_OBJECT (comp, "%s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      _start_task (comp);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* state-lock all elements */
      GST_DEBUG_OBJECT (comp,
          "Setting all children to READY and locking their state");

      _add_update_compo_action (comp, G_CALLBACK (_initialize_stack_func),
          COMP_UPDATE_STACK_INITIALIZE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      _stop_task (comp);

      _remove_update_actions (comp);
      _remove_seek_actions (comp);
      _set_all_children_state (comp, GST_STATE_READY);
      nle_composition_reset (comp);

      _start_task (comp);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      _stop_task (comp);

      _remove_update_actions (comp);
      _remove_seek_actions (comp);
      _set_all_children_state (comp, GST_STATE_NULL);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gint
objects_start_compare (NleObject * a, NleObject * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    return 0;
  }
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}

static gint
objects_stop_compare (NleObject * a, NleObject * b)
{
  if (a->stop == b->stop) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    return 0;
  }
  if (b->stop < a->stop)
    return -1;
  if (b->stop > a->stop)
    return 1;
  return 0;
}

/* WITH OBJECTS LOCK TAKEN */
static void
update_start_stop_duration (NleComposition * comp)
{
  NleObject *obj;
  NleObject *cobj = (NleObject *) comp;
  NleCompositionPrivate *priv = comp->priv;

  _assert_proper_thread (comp);

  if (!priv->objects_start) {
    GST_INFO_OBJECT (comp, "no objects, resetting everything to 0");

    if (cobj->start) {
      cobj->start = cobj->pending_start = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          nleobject_properties[NLEOBJECT_PROP_START]);
    }

    if (cobj->duration) {
      cobj->pending_duration = cobj->duration = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          nleobject_properties[NLEOBJECT_PROP_DURATION]);
      signal_duration_change (comp);
    }

    if (cobj->stop) {
      cobj->stop = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          nleobject_properties[NLEOBJECT_PROP_STOP]);
    }

    return;
  }

  /* If we have a default object, the start position is 0 */
  if (priv->expandables) {
    GST_INFO_OBJECT (cobj,
        "Setting start to 0 because we have a default object");

    if (cobj->start != 0) {
      cobj->pending_start = cobj->start = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          nleobject_properties[NLEOBJECT_PROP_START]);
    }

  } else {

    /* Else it's the first object's start value */
    obj = (NleObject *) priv->objects_start->data;

    if (obj->start != cobj->start) {
      GST_INFO_OBJECT (obj, "setting start from %s to %" GST_TIME_FORMAT,
          GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->start));
      cobj->pending_start = cobj->start = obj->start;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          nleobject_properties[NLEOBJECT_PROP_START]);
    }

  }

  obj = (NleObject *) priv->objects_stop->data;

  if (obj->stop != cobj->stop) {
    GST_INFO_OBJECT (obj, "setting stop from %s to %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->stop));

    if (priv->expandables) {
      GList *tmp;

      GST_INFO_OBJECT (comp, "RE-setting all expandables duration and commit");
      for (tmp = priv->expandables; tmp; tmp = tmp->next) {
        g_object_set (tmp->data, "duration", obj->stop, NULL);
        nle_object_commit (NLE_OBJECT (tmp->data), FALSE);
      }
    }

    priv->segment->stop = obj->stop;
    cobj->stop = obj->stop;
    g_object_notify_by_pspec (G_OBJECT (cobj),
        nleobject_properties[NLEOBJECT_PROP_STOP]);
  }

  if ((cobj->stop - cobj->start) != cobj->duration) {
    cobj->pending_duration = cobj->duration = cobj->stop - cobj->start;
    g_object_notify_by_pspec (G_OBJECT (cobj),
        nleobject_properties[NLEOBJECT_PROP_DURATION]);
    signal_duration_change (comp);
  }

  GST_INFO_OBJECT (comp,
      "start:%" GST_TIME_FORMAT
      " stop:%" GST_TIME_FORMAT
      " duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (cobj->start),
      GST_TIME_ARGS (cobj->stop), GST_TIME_ARGS (cobj->duration));
}

static inline gboolean
_parent_or_priority_changed (NleObject * obj, GNode * oldnode,
    NleObject * newparent, GNode * node)
{
  NleObject *oldparent = NULL;

  if (oldnode)
    oldparent =
        G_NODE_IS_ROOT (oldnode) ? NULL : (NleObject *) oldnode->parent->data;

  if (oldparent != newparent)
    return TRUE;

  if (oldparent == NULL || newparent == NULL)
    return FALSE;

  return (g_node_child_index (node, obj) != g_node_child_index (oldnode, obj));
}

static void
_link_to_parent (NleComposition * comp, NleObject * newobj,
    NleObject * newparent)
{
  GstPad *sinkpad;

  /* relink to new parent in required order */
  GST_LOG_OBJECT (comp, "Linking %s and %s",
      GST_ELEMENT_NAME (GST_ELEMENT (newobj)),
      GST_ELEMENT_NAME (GST_ELEMENT (newparent)));

  sinkpad = get_unlinked_sink_ghost_pad ((NleOperation *) newparent);

  if (G_UNLIKELY (sinkpad == NULL)) {
    GST_WARNING_OBJECT (comp,
        "Couldn't find an unlinked sinkpad from %s",
        GST_ELEMENT_NAME (newparent));
  } else {
    if (G_UNLIKELY (gst_pad_link_full (NLE_OBJECT_SRC (newobj), sinkpad,
                GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK)) {
      GST_WARNING_OBJECT (comp, "Failed to link pads %s:%s - %s:%s",
          GST_DEBUG_PAD_NAME (NLE_OBJECT_SRC (newobj)),
          GST_DEBUG_PAD_NAME (sinkpad));
    }
    gst_object_unref (sinkpad);
  }
}

static void
_relink_children_recursively (NleComposition * comp,
    NleObject * newobj, GNode * node, GstEvent * toplevel_seek)
{
  GNode *child;
  guint nbchildren = g_node_n_children (node);
  NleOperation *oper = (NleOperation *) newobj;

  GST_INFO_OBJECT (newobj, "is a %s operation, analyzing the %d children",
      oper->dynamicsinks ? "dynamic" : "regular", nbchildren);
  /* Update the operation's number of sinks, that will make it have the proper
   * number of sink pads to connect the children to. */
  if (oper->dynamicsinks)
    g_object_set (G_OBJECT (newobj), "sinks", nbchildren, NULL);

  for (child = node->children; child; child = child->next)
    _relink_single_node (comp, child, toplevel_seek);

  if (G_UNLIKELY (nbchildren < oper->num_sinks))
    GST_ERROR ("Not enough sinkpads to link all objects to the operation ! "
        "%d / %d", oper->num_sinks, nbchildren);

  if (G_UNLIKELY (nbchildren == 0))
    GST_ERROR ("Operation has no child objects to be connected to !!!");
  /* Make sure we have enough sinkpads */
}

/*
 * recursive depth-first relink stack function on new stack
 *
 * _ relink nodes with changed parent/order
 * _ links new nodes with parents
 * _ unblocks available source pads (except for toplevel)
 *
 * WITH OBJECTS LOCK TAKEN
 */
static void
_relink_single_node (NleComposition * comp, GNode * node,
    GstEvent * toplevel_seek)
{
  NleObject *newobj;
  NleObject *newparent;
  GstPad *srcpad = NULL, *sinkpad = NULL;
  GstEvent *translated_seek;

  if (G_UNLIKELY (!node))
    return;

  newparent = G_NODE_IS_ROOT (node) ? NULL : (NleObject *) node->parent->data;
  newobj = (NleObject *) node->data;

  GST_DEBUG_OBJECT (comp, "newobj:%s",
      GST_ELEMENT_NAME ((GstElement *) newobj));

  srcpad = NLE_OBJECT_SRC (newobj);

  gst_bin_add (GST_BIN (comp->priv->current_bin), GST_ELEMENT (newobj));
  gst_element_sync_state_with_parent (GST_ELEMENT_CAST (newobj));

  translated_seek = nle_object_translate_incoming_seek (newobj, toplevel_seek);

  gst_element_send_event (GST_ELEMENT (newobj), translated_seek);

  /* link to parent if needed.  */
  if (newparent) {
    _link_to_parent (comp, newobj, newparent);

    /* If there's an operation, inform it about priority changes */
    sinkpad = gst_pad_get_peer (srcpad);
    nle_operation_signal_input_priority_changed ((NleOperation *)
        newparent, sinkpad, newobj->priority);
    gst_object_unref (sinkpad);
  }

  /* Handle children */
  if (NLE_IS_OPERATION (newobj))
    _relink_children_recursively (comp, newobj, node, toplevel_seek);

  GST_LOG_OBJECT (comp, "done with object %s",
      GST_ELEMENT_NAME (GST_ELEMENT (newobj)));
}



/*
 * compare_relink_stack:
 * @comp: The #NleComposition
 * @stack: The new stack
 * @modify: TRUE if the timeline has changed and needs downstream flushes.
 *
 * Compares the given stack to the current one and relinks it if needed.
 *
 * WITH OBJECTS LOCK TAKEN
 *
 * Returns: The #GList of #NleObject no longer used
 */

static void
_deactivate_stack (NleComposition * comp, gboolean flush_downstream)
{
  GstPad *ptarget;

  _set_current_bin_to_ready (comp, flush_downstream);

  ptarget = gst_ghost_pad_get_target (GST_GHOST_PAD (NLE_OBJECT_SRC (comp)));
  _empty_bin (GST_BIN_CAST (comp->priv->current_bin));

  if (comp->priv->ghosteventprobe) {
    GST_INFO_OBJECT (comp, "Removing old ghost pad probe");

    gst_pad_remove_probe (ptarget, comp->priv->ghosteventprobe);
    comp->priv->ghosteventprobe = 0;
  }

  if (ptarget)
    gst_object_unref (ptarget);

/*   priv->current = NULL;
 */
}

static void
_relink_new_stack (NleComposition * comp, GNode * stack,
    GstEvent * toplevel_seek)
{
  _relink_single_node (comp, stack, toplevel_seek);

  gst_event_unref (toplevel_seek);
}

/* static void
 * unlock_activate_stack (NleComposition * comp, GNode * node, GstState state)
 * {
 *   GNode *child;
 *
 *   GST_LOG_OBJECT (comp, "object:%s",
 *       GST_ELEMENT_NAME ((GstElement *) (node->data)));
 *
 *   gst_element_set_locked_state ((GstElement *) (node->data), FALSE);
 *   gst_element_set_state (GST_ELEMENT (node->data), state);
 *
 *   for (child = node->children; child; child = child->next)
 *     unlock_activate_stack (comp, child, state);
 * }
 */

static gboolean
are_same_stacks (GNode * stack1, GNode * stack2)
{
  gboolean res = FALSE;

  /* TODO : FIXME : we should also compare start/inpoint */
  /* stacks are not equal if one of them is NULL but not the other */
  if ((!stack1 && stack2) || (stack1 && !stack2))
    goto beach;

  if (stack1 && stack2) {
    GNode *child1, *child2;

    /* if they don't contain the same source, not equal */
    if (!(stack1->data == stack2->data))
      goto beach;

    /* if they don't have the same number of children, not equal */
    if (!(g_node_n_children (stack1) == g_node_n_children (stack2)))
      goto beach;

    child1 = stack1->children;
    child2 = stack2->children;
    while (child1 && child2) {
      if (!(are_same_stacks (child1, child2)))
        goto beach;
      child1 = g_node_next_sibling (child1);
      child2 = g_node_next_sibling (child2);
    }

    /* if there's a difference in child number, stacks are not equal */
    if (child1 || child2)
      goto beach;
  }

  /* if stack1 AND stack2 are NULL, then they're equal (both empty) */
  res = TRUE;

beach:
  GST_LOG ("Stacks are equal : %d", res);

  return res;
}

static inline gboolean
_activate_new_stack (NleComposition * comp)
{
  GstPad *pad;
  GstElement *topelement;

  NleCompositionPrivate *priv = comp->priv;

  if (!priv->current) {
    if ((!priv->objects_start)) {
      nle_composition_reset_target_pad (comp);
      priv->segment_start = 0;
      priv->segment_stop = GST_CLOCK_TIME_NONE;
    }

    GST_DEBUG_OBJECT (comp, "Nothing else in the composition"
        ", update 'worked'");
    goto resync_state;
  }

  /* The stack is entirely ready, send seek out synchronously */
  topelement = GST_ELEMENT (priv->current->data);
  /* Get toplevel object source pad */
  pad = NLE_OBJECT_SRC (topelement);

  GST_INFO_OBJECT (comp,
      "We have a valid toplevel element pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  nle_composition_ghost_pad_set_target (comp, pad);

  GST_DEBUG_OBJECT (comp, "New stack activated!");

resync_state:
  gst_element_set_locked_state (priv->current_bin, FALSE);
  GST_DEBUG ("going back to parent state");
  gst_element_sync_state_with_parent (priv->current_bin);
  GST_DEBUG ("gone back to parent state");

  return TRUE;
}

/* WITH OBJECTS LOCK TAKEN */
static gboolean
_set_real_eos_seqnum_from_seek (NleComposition * comp, GstEvent * event)
{
  GList *tmp;

  gboolean should_check_objects = FALSE;
  NleCompositionPrivate *priv = comp->priv;
  gboolean reverse = (priv->segment->rate < 0);
  gint stack_seqnum = gst_event_get_seqnum (event);

  if (reverse && GST_CLOCK_TIME_IS_VALID (priv->segment_start))
    should_check_objects = TRUE;
  else if (!reverse && GST_CLOCK_TIME_IS_VALID (priv->segment_stop))
    should_check_objects = TRUE;

  if (should_check_objects) {
    for (tmp = priv->objects_stop; tmp; tmp = g_list_next (tmp)) {
      NleObject *object = (NleObject *) tmp->data;

      if (!NLE_IS_SOURCE (object))
        continue;

      if ((!reverse && priv->segment_stop < object->stop) ||
          (reverse && priv->segment_start > object->start)) {
        priv->next_eos_seqnum = stack_seqnum;
        g_atomic_int_set (&priv->real_eos_seqnum, 0);
        return FALSE;
      }
    }
  }

  priv->next_eos_seqnum = stack_seqnum;
  g_atomic_int_set (&priv->real_eos_seqnum, stack_seqnum);

  return TRUE;
}

/*
 * update_pipeline:
 * @comp: The #NleComposition
 * @currenttime: The #GstClockTime to update at, can be GST_CLOCK_TIME_NONE.
 * @update_reason: Reason why we are updating the pipeline
 *
 * Updates the internal pipeline and properties. If @currenttime is
 * GST_CLOCK_TIME_NONE, it will not modify the current pipeline
 *
 * Returns: FALSE if there was an error updating the pipeline.
 *
 * WITH OBJECTS LOCK TAKEN
 */
static gboolean
update_pipeline (NleComposition * comp, GstClockTime currenttime, gint32 seqnum,
    NleUpdateStackReason update_reason)
{

  GstEvent *toplevel_seek;

  GNode *stack = NULL;
  gboolean samestack = FALSE;
  gboolean updatestoponly = FALSE;
  GstState state = GST_STATE (comp);
  NleCompositionPrivate *priv = comp->priv;
  GstClockTime new_stop = GST_CLOCK_TIME_NONE;
  GstClockTime new_start = GST_CLOCK_TIME_NONE;

  GstState nextstate = (GST_STATE_NEXT (comp) == GST_STATE_VOID_PENDING) ?
      GST_STATE (comp) : GST_STATE_NEXT (comp);

  _assert_proper_thread (comp);

  GST_INFO_OBJECT (comp,
      "currenttime:%" GST_TIME_FORMAT
      " Reason: %s, Seqnum: %i", GST_TIME_ARGS (currenttime),
      UPDATE_PIPELINE_REASONS[update_reason], seqnum);

  if (!GST_CLOCK_TIME_IS_VALID (currenttime))
    return FALSE;

  if (state == GST_STATE_NULL && nextstate == GST_STATE_NULL) {
    GST_DEBUG_OBJECT (comp, "STATE_NULL: not updating pipeline");
    return FALSE;
  }

  GST_DEBUG_OBJECT (comp,
      "now really updating the pipeline, current-state:%s",
      gst_element_state_get_name (state));

  /* Get new stack and compare it to current one */
  stack = get_clean_toplevel_stack (comp, &currenttime, &new_start, &new_stop);
  samestack = are_same_stacks (priv->current, stack);

  /* set new segment_start/stop (the current zone over which the new stack
   * is valid) */
  if (priv->segment->rate >= 0.0) {
    priv->segment_start = currenttime;
    priv->segment_stop = new_stop;
  } else {
    priv->segment_start = new_start;
    priv->segment_stop = currenttime;
  }

# if 0
  /* FIXME -- We should be ablt to use updatestoponly in that case,
   * but it simply does not work! Not using it leads to same
   * behaviour, but less optimized */

  gboolean startchanged, stopchanged;

  if (priv->segment->rate >= 0.0) {
    startchanged = priv->segment_start != currenttime;
    stopchanged = priv->segment_stop != new_stop;
  } else {
    startchanged = priv->segment_start != new_start;
    stopchanged = priv->segment_stop != currenttime;
  }

  if (samestack) {
    if (startchanged || stopchanged) {
      /* Update seek events need to be flushing if not in PLAYING,
       * else we will encounter deadlocks. */
      updatestoponly = (state == GST_STATE_PLAYING) ? FALSE : TRUE;
    }
  }
#endif

  toplevel_seek = get_new_seek_event (comp, TRUE, updatestoponly);
  gst_event_set_seqnum (toplevel_seek, seqnum);
  _set_real_eos_seqnum_from_seek (comp, toplevel_seek);

  _remove_update_actions (comp);

  /* If stacks are different, unlink/relink objects */
  if (!samestack) {
    _deactivate_stack (comp, _have_to_flush_downstream (update_reason));
    _relink_new_stack (comp, stack, toplevel_seek);
  }

  /* Unlock all elements in new stack */
  GST_INFO_OBJECT (comp, "Setting current stack [%" GST_TIME_FORMAT " - %"
      GST_TIME_FORMAT "]", GST_TIME_ARGS (priv->segment_start),
      GST_TIME_ARGS (priv->segment_stop));
  priv->current = stack;

  if (priv->current) {
    UpdateCompositionData *ucompo = g_slice_new0 (UpdateCompositionData);

    GST_INFO_OBJECT (comp, "New stack set and ready to run, probing src pad"
        " and stopping children thread until we are actually ready with"
        " that new stack");

    ucompo->comp = comp;
    ucompo->reason = update_reason;
    ucompo->seqnum = seqnum;

    comp->priv->updating_reason = update_reason;
    comp->priv->seqnum_to_restart_task = seqnum;

    GST_OBJECT_LOCK (comp);
    if (comp->task == NULL) {
      GST_INFO_OBJECT (comp,
          "No task set, it must have been stopped, returning");
      GST_OBJECT_UNLOCK (comp);
      return FALSE;
    }

    gst_task_pause (comp->task);
    GST_OBJECT_UNLOCK (comp);
  }

  /* Activate stack */
  if (!samestack)
    return _activate_new_stack (comp);
  else
    return _seek_current_stack (comp, toplevel_seek,
        _have_to_flush_downstream (update_reason));
}

static gboolean
nle_composition_add_object (GstBin * bin, GstElement * element)
{
  NleObject *object;
  NleComposition *comp = (NleComposition *) bin;

  if (element == comp->priv->current_bin) {
    GST_INFO_OBJECT (comp, "Adding internal bin");
    return GST_BIN_CLASS (parent_class)->add_element (bin, element);
  }

  g_return_val_if_fail (NLE_IS_OBJECT (element), FALSE);

  object = NLE_OBJECT (element);
  gst_object_ref_sink (object);

  object->in_composition = TRUE;
  _add_add_object_action (comp, object);

  return TRUE;
}

static gboolean
_nle_composition_add_object (NleComposition * comp, NleObject * object)
{
  gboolean ret = TRUE;
  NleCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "element %s", GST_OBJECT_NAME (object));
  GST_DEBUG_OBJECT (object, "%" GST_TIME_FORMAT "--%" GST_TIME_FORMAT,
      GST_TIME_ARGS (NLE_OBJECT_START (object)),
      GST_TIME_ARGS (NLE_OBJECT_STOP (object)));

  if ((NLE_OBJECT_IS_EXPANDABLE (object)) &&
      g_list_find (priv->expandables, object)) {
    GST_WARNING_OBJECT (comp,
        "We already have an expandable, remove it before adding new one");
    ret = FALSE;

    goto chiringuito;
  }

  nle_object_set_caps (object, NLE_OBJECT (comp)->caps);
  nle_object_set_commit_needed (NLE_OBJECT (comp));

  if (!ret) {
    GST_WARNING_OBJECT (comp, "couldn't add object");
    goto chiringuito;
  }

  /* lock state of child ! */
  GST_LOG_OBJECT (comp, "Locking state of %s", GST_ELEMENT_NAME (object));

  if (NLE_OBJECT_IS_EXPANDABLE (object)) {
    /* Only react on non-default objects properties */
    g_object_set (object,
        "start", (GstClockTime) 0,
        "inpoint", (GstClockTime) 0,
        "duration", (GstClockTimeDiff) NLE_OBJECT_STOP (comp), NULL);

    GST_INFO_OBJECT (object, "Used as expandable, commiting now");
    nle_object_commit (NLE_OBJECT (object), FALSE);
  }

  /* ...and add it to the hash table */
  g_hash_table_add (priv->objects_hash, object);

  /* Set the caps of the composition on the NleObject it handles */
  if (G_UNLIKELY (!gst_caps_is_any (((NleObject *) comp)->caps)))
    nle_object_set_caps ((NleObject *) object, ((NleObject *) comp)->caps);

  /* Special case for default source. */
  if (NLE_OBJECT_IS_EXPANDABLE (object)) {
    /* It doesn't get added to objects_start and objects_stop. */
    priv->expandables = g_list_prepend (priv->expandables, object);
    goto beach;
  }

  /* add it sorted to the objects list */
  priv->objects_start = g_list_insert_sorted
      (priv->objects_start, object, (GCompareFunc) objects_start_compare);

  if (priv->objects_start)
    GST_LOG_OBJECT (comp,
        "Head of objects_start is now %s [%" GST_TIME_FORMAT "--%"
        GST_TIME_FORMAT "]",
        GST_OBJECT_NAME (priv->objects_start->data),
        GST_TIME_ARGS (NLE_OBJECT_START (priv->objects_start->data)),
        GST_TIME_ARGS (NLE_OBJECT_STOP (priv->objects_start->data)));

  priv->objects_stop = g_list_insert_sorted
      (priv->objects_stop, object, (GCompareFunc) objects_stop_compare);

  /* Now the object is ready to be commited and then used */

beach:
  return ret;

chiringuito:
  {
    update_start_stop_duration (comp);
    goto beach;
  }
}

static gboolean
nle_composition_remove_object (GstBin * bin, GstElement * element)
{
  NleObject *object;
  NleComposition *comp = (NleComposition *) bin;

  if (element == comp->priv->current_bin) {
    GST_INFO_OBJECT (comp, "Adding internal bin");
    return GST_BIN_CLASS (parent_class)->remove_element (bin, element);
  }

  g_return_val_if_fail (NLE_IS_OBJECT (element), FALSE);

  object = NLE_OBJECT (element);

  object->in_composition = FALSE;
  _add_remove_object_action (comp, object);

  return TRUE;
}

static gboolean
_nle_composition_remove_object (NleComposition * comp, NleObject * object)
{
  NleCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "removing object %s", GST_OBJECT_NAME (object));

  if (!g_hash_table_contains (priv->objects_hash, object)) {
    GST_INFO_OBJECT (comp, "object was not in composition");
    return FALSE;
  }

  gst_element_set_locked_state (GST_ELEMENT (object), FALSE);
  gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);

  /* handle default source */
  if (NLE_OBJECT_IS_EXPANDABLE (object)) {
    /* Find it in the list */
    priv->expandables = g_list_remove (priv->expandables, object);
  } else {
    /* remove it from the objects list and resort the lists */
    priv->objects_start = g_list_remove (priv->objects_start, object);
    priv->objects_stop = g_list_remove (priv->objects_stop, object);
    GST_LOG_OBJECT (object, "Removed from the objects start/stop list");
  }

  if (priv->current && NLE_OBJECT (priv->current->data) == NLE_OBJECT (object))
    nle_composition_reset_target_pad (comp);

  g_hash_table_remove (priv->objects_hash, object);

  GST_LOG_OBJECT (object, "Done removing from the composition, now updating");

  /* Make it possible to reuse the same object later */
  nle_object_reset (NLE_OBJECT (object));
  gst_object_unref (object);

  return TRUE;
}
