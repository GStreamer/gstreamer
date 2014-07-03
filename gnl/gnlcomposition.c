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

#include "gnl.h"

/**
 * SECTION:element-gnlcomposition
 *
 * A GnlComposition contains GnlObjects such as GnlSources and GnlOperations,
 * and connects them dynamically to create a composition timeline.
 */

static GstStaticPadTemplate gnl_composition_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gnlcomposition_debug);
#define GST_CAT_DEFAULT gnlcomposition_debug

#define _do_init              \
  GST_DEBUG_CATEGORY_INIT (gnlcomposition_debug,"gnlcomposition", GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Composition");
#define gnl_composition_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GnlComposition, gnl_composition, GNL_TYPE_OBJECT,
    _do_init);


enum
{
  PROP_0,
  PROP_DEACTIVATED_ELEMENTS_STATE,
  PROP_LAST,
};

/* Properties from GnlObject */
enum
{
  GNLOBJECT_PROP_START,
  GNLOBJECT_PROP_STOP,
  GNLOBJECT_PROP_DURATION,
  GNLOBJECT_PROP_LAST
};

enum
{
  COMMIT_SIGNAL,
  COMMITED_SIGNAL,
  ADD_OBJECT_SIGNAL,
  REMOVE_OBJECT_SIGNAL,
  LAST_SIGNAL
};

typedef struct _GnlCompositionEntry GnlCompositionEntry;

typedef struct
{
  GnlComposition *comp;
  GstEvent *event;
} SeekData;

typedef struct
{
  GnlComposition *comp;
  GnlObject *object;
} ChildIOData;

struct _GnlCompositionPrivate
{
  gboolean dispose_has_run;

  /*
     Sorted List of GnlObjects , ThreadSafe
     objects_start : sorted by start-time then priority
     objects_stop : sorted by stop-time then priority
     objects_hash : contains signal handlers id for controlled objects
     objects_lock : mutex to acces/modify any of those lists/hashtable
   */
  GList *objects_start;
  GList *objects_stop;
  GHashTable *objects_hash;
  GMutex objects_lock;

  /* List of GnlObject to be inserted or removed from the composition on the
   * next commit */
  GHashTable *pending_io;
  GMutex pending_io_lock;

  /*
     thread-safe Seek handling.
     flushing_lock : mutex to access flushing and pending_idle
     flushing :
   */
  GMutex flushing_lock;
  gboolean flushing;

  /* source top-level ghostpad, probe and entry */
  gulong ghosteventprobe;
  GnlCompositionEntry *toplevelentry;

  /* current stack, list of GnlObject* */
  GNode *current;

  /* List of GnlObject whose start/duration will be the same as the composition */
  GList *expandables;

  /* TRUE if the stack is valid.
   * This is meant to prevent the top-level pad to be unblocked before the stack
   * is fully done. Protected by OBJECTS_LOCK */
  gboolean stackvalid;

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
     We are called before gnl_object_sync_handler
   */
  GstPadEventFunction gnl_event_pad_func;
  gboolean send_stream_start;

  GMainContext *mcontext;
  /* Ensure that when we remove all sources from the maincontext
   * we can not add any source, avoiding:
   * "g_source_attach: assertion '!SOURCE_DESTROYED (source)' failed" */
  GMutex mcontext_lock;

  gboolean reset_time;

  gboolean running;
  gboolean initialized;

  GstState deactivated_elements_state;

  GstElement *current_bin;
};

static guint _signals[LAST_SIGNAL] = { 0 };

static GParamSpec *gnlobject_properties[GNLOBJECT_PROP_LAST];
static GParamSpec *_properties[PROP_LAST];

#define OBJECT_IN_ACTIVE_SEGMENT(comp,element)      \
  ((GNL_OBJECT_START(element) < comp->priv->segment_stop) &&  \
   (GNL_OBJECT_STOP(element) >= comp->priv->segment_start))

static void gnl_composition_dispose (GObject * object);
static void gnl_composition_finalize (GObject * object);
static void gnl_composition_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspsec);
static void gnl_composition_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspsec);
static void gnl_composition_reset (GnlComposition * comp);

static gboolean gnl_composition_add_object (GstBin * bin, GstElement * element);

static void gnl_composition_handle_message (GstBin * bin, GstMessage * message);

static gboolean
gnl_composition_remove_object (GstBin * bin, GstElement * element);

static GstStateChangeReturn
gnl_composition_change_state (GstElement * element, GstStateChange transition);

static inline void gnl_composition_reset_target_pad (GnlComposition * comp);

static gboolean
seek_handling (GnlComposition * comp, gboolean initial, gboolean update);
static gint objects_start_compare (GnlObject * a, GnlObject * b);
static gint objects_stop_compare (GnlObject * a, GnlObject * b);
static GstClockTime get_current_position (GnlComposition * comp);

static gboolean update_pipeline (GnlComposition * comp,
    GstClockTime currenttime, gboolean initial, gboolean modify);
static gboolean gnl_composition_commit_func (GnlObject * object,
    gboolean recurse);
static void update_start_stop_duration (GnlComposition * comp);

static gboolean
gnl_composition_event_handler (GstPad * ghostpad, GstObject * parent,
    GstEvent * event);
static void _relink_single_node (GnlComposition * comp, GNode * node,
    GstEvent * toplevel_seek);
static gboolean update_pipeline_func (GnlComposition * comp);
static gboolean _commit_func (GnlComposition * comp);
static gboolean lock_child_state (GValue * item, GValue * ret,
    gpointer udata G_GNUC_UNUSED);
static gboolean
set_child_caps (GValue * item, GValue * ret G_GNUC_UNUSED, GnlObject * comp);
static GstEvent *get_new_seek_event (GnlComposition * comp, gboolean initial,
    gboolean updatestoponly);
static gboolean
_gnl_composition_add_entry (GnlComposition * comp, GnlObject * object);
static gboolean
_gnl_composition_remove_entry (GnlComposition * comp, GnlObject * object);


/* COMP_REAL_START: actual position to start current playback at. */
#define COMP_REAL_START(comp)                                                  \
  (MAX (comp->priv->segment->start, GNL_OBJECT_START (comp)))

#define COMP_REAL_STOP(comp)                                                   \
  (GST_CLOCK_TIME_IS_VALID (comp->priv->segment->stop) ?                       \
   (MIN (comp->priv->segment->stop, GNL_OBJECT_STOP (comp))) :                 \
   GNL_OBJECT_STOP (comp))

#define COMP_ENTRY(comp, object)                                               \
  (g_hash_table_lookup (comp->priv->objects_hash, (gconstpointer) object))

#define COMP_OBJECTS_LOCK(comp) G_STMT_START {                                 \
    GST_LOG_OBJECT (comp, "locking objects_lock from thread %p",               \
        g_thread_self());                                                      \
    g_mutex_lock (&comp->priv->objects_lock);                                  \
    GST_LOG_OBJECT (comp, "locked objects_lock from thread %p",                \
        g_thread_self());                                                      \
  } G_STMT_END

#define COMP_OBJECTS_UNLOCK(comp) G_STMT_START {                               \
    GST_LOG_OBJECT (comp, "unlocking objects_lock from thread %p",             \
        g_thread_self());                                                      \
    g_mutex_unlock (&comp->priv->objects_lock);                                \
  } G_STMT_END

#define COMP_PENDING_IO_LOCK(comp) G_STMT_START {                                 \
    GST_LOG_OBJECT (comp, "locking pending_io_lock from thread %p",               \
        g_thread_self());                                                      \
    g_mutex_lock (&comp->priv->pending_io_lock);                                  \
    GST_LOG_OBJECT (comp, "locked pending_io_lock from thread %p",                \
        g_thread_self());                                                      \
  } G_STMT_END

#define COMP_FLUSHING_LOCK(comp) G_STMT_START {                                \
    GST_LOG_OBJECT (comp, "locking flushing_lock from thread %p",              \
        g_thread_self());                                                      \
    g_mutex_lock (&comp->priv->flushing_lock);                                 \
    GST_LOG_OBJECT (comp, "locked flushing_lock from thread %p",               \
        g_thread_self());                                                      \
  } G_STMT_END
#define COMP_FLUSHING_UNLOCK(comp) G_STMT_START {                              \
    GST_LOG_OBJECT (comp, "unlocking flushing_lock from thread %p",            \
        g_thread_self());                                                      \
    g_mutex_unlock (&comp->priv->flushing_lock);                               \
  } G_STMT_END

#define MAIN_CONTEXT_LOCK(comp) G_STMT_START {                       \
  GST_LOG_OBJECT (comp, "Getting MAIN_CONTEXT_LOCK in thread %p",    \
        g_thread_self());                                            \
  g_mutex_lock(&((GnlComposition*)comp)->priv->mcontext_lock);    \
  GST_LOG_OBJECT (comp, "Got MAIN_CONTEXT_LOCK in thread %p",        \
        g_thread_self());                                            \
} G_STMT_END

#define MAIN_CONTEXT_UNLOCK(comp) G_STMT_START {                     \
  g_mutex_unlock(&((GnlComposition*)comp)->priv->mcontext_lock);  \
  GST_LOG_OBJECT (comp, "Unlocked MAIN_CONTEXT_LOCK in thread %p",   \
        g_thread_self());                                            \
} G_STMT_END

#define GET_TASK_LOCK(comp)    (&(GNL_COMPOSITION(comp)->task_rec_lock))

struct _GnlCompositionEntry
{
  GnlObject *object;
  GnlComposition *comp;

  /* handler id for block probe */
  gulong probeid;
  gulong dataprobeid;

  gboolean seeked;
};

static void
_remove_all_sources (GnlComposition * comp)
{
  GSource *source;

  MAIN_CONTEXT_LOCK (comp);
  while ((source =
          g_main_context_find_source_by_user_data (comp->priv->mcontext,
              comp))) {
    g_source_destroy (source);
  }
  MAIN_CONTEXT_UNLOCK (comp);
}

static void
iterate_main_context_func (GnlComposition * comp)
{
  if (comp->priv->running == FALSE) {
    GST_DEBUG_OBJECT (comp, "Not running anymore");

    return;
  }

  g_main_context_iteration (comp->priv->mcontext, TRUE);
}

static void
_start_task (GnlComposition * comp)
{
  GstTask *task;

  comp->priv->running = TRUE;

  GST_OBJECT_LOCK (comp);

  task = comp->task;
  if (task == NULL) {
    task =
        gst_task_new ((GstTaskFunction) iterate_main_context_func, comp, NULL);
    gst_task_set_lock (task, GET_TASK_LOCK (comp));
    GST_INFO_OBJECT (comp, "created task %p", task);
    comp->task = task;
  }

  gst_task_set_state (task, GST_TASK_STARTED);
  GST_OBJECT_UNLOCK (comp);
}

static gboolean
_stop_task (GnlComposition * comp, GstEvent * flush_start)
{
  gboolean res = TRUE;
  GstTask *task;
  GnlObject *obj = GNL_OBJECT (comp);

  GST_ERROR_OBJECT (comp, "%s srcpad task",
      flush_start ? "Pausing" : "Stopping");

  comp->priv->running = FALSE;

  /*  Clean the stack of GSource set on the MainContext */
  g_main_context_wakeup (comp->priv->mcontext);
  _remove_all_sources (comp);
  if (flush_start) {
    res = gst_pad_push_event (obj->srcpad, flush_start);
  }

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

static gboolean
_seek_pipeline_func (SeekData * seekd)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  GnlCompositionPrivate *priv = seekd->comp->priv;

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

  if (priv->segment->start >= GNL_OBJECT_STOP (seekd->comp)) {
    GST_INFO_OBJECT (seekd->comp,
        "Start %" GST_TIME_FORMAT " > comp->stop: %" GST_TIME_FORMAT
        " Not seeking", GST_TIME_ARGS (priv->segment->start),
        GST_TIME_ARGS (GNL_OBJECT_STOP (seekd->comp)));
    GST_FIXME_OBJECT (seekd->comp, "HANDLE error async!");
    goto beach;
  }

  /* crop the segment start/stop values */
  /* Only crop segment start value if we don't have a default object */
  if (priv->expandables == NULL)
    priv->segment->start =
        MAX (priv->segment->start, GNL_OBJECT_START (seekd->comp));
  priv->segment->stop =
      MIN (priv->segment->stop, GNL_OBJECT_STOP (seekd->comp));

  priv->next_base_time = 0;

  seek_handling (seekd->comp, TRUE, FALSE);

  priv->reset_time = TRUE;
  priv->gnl_event_pad_func (GNL_OBJECT (seekd->comp)->srcpad,
      GST_OBJECT (seekd->comp), get_new_seek_event (seekd->comp, FALSE, FALSE));
  priv->reset_time = FALSE;

beach:
  gst_event_unref (seekd->event);
  g_slice_free (SeekData, seekd);

  return G_SOURCE_REMOVE;
}



static void
_add_update_gsource (GnlComposition * comp)
{
  MAIN_CONTEXT_LOCK (comp);
  g_main_context_invoke (comp->priv->mcontext,
      (GSourceFunc) update_pipeline_func, comp);
  MAIN_CONTEXT_UNLOCK (comp);
}

static void
_add_commit_gsource (GnlComposition * comp)
{
  MAIN_CONTEXT_LOCK (comp);
  g_main_context_invoke (comp->priv->mcontext,
      (GSourceFunc) _commit_func, comp);
  MAIN_CONTEXT_UNLOCK (comp);
}

static void
_add_seek_gsource (GnlComposition * comp, GstEvent * event)
{
  SeekData *seekd = g_slice_new0 (SeekData);

  seekd->comp = comp;
  seekd->event = event;

  MAIN_CONTEXT_LOCK (comp);
  g_main_context_invoke (comp->priv->mcontext,
      (GSourceFunc) _seek_pipeline_func, seekd);
  MAIN_CONTEXT_UNLOCK (comp);
}


static gboolean
_initialize_stack_func (GnlComposition * comp)
{
  GnlCompositionPrivate *priv = comp->priv;

  /* set ghostpad target */
  COMP_OBJECTS_LOCK (comp);
  if (!(update_pipeline (comp, COMP_REAL_START (comp), TRUE, TRUE))) {
    COMP_OBJECTS_UNLOCK (comp);
    GST_FIXME_OBJECT (comp, "PLEASE signal state change failure ASYNC");

    return G_SOURCE_REMOVE;
  }
  COMP_OBJECTS_UNLOCK (comp);

  priv->initialized = TRUE;

  return G_SOURCE_REMOVE;
}

static void
_add_initialize_stack_gsource (GnlComposition * comp)
{
  MAIN_CONTEXT_LOCK (comp);
  g_main_context_invoke (comp->priv->mcontext,
      (GSourceFunc) _initialize_stack_func, comp);
  MAIN_CONTEXT_UNLOCK (comp);
}

static void
_free_child_io_data (gpointer childio)
{
  g_slice_free (ChildIOData, childio);
}

static void
_remove_object_func (ChildIOData * childio)
{
  GnlComposition *comp = childio->comp;
  GnlObject *object = childio->object;

  GnlCompositionPrivate *priv = comp->priv;
  GnlCompositionEntry *entry;
  GnlObject *in_pending_io;

  COMP_OBJECTS_LOCK (comp);
  entry = COMP_ENTRY (comp, object);
  in_pending_io = g_hash_table_lookup (priv->pending_io, object);

  if (!entry) {
    if (in_pending_io) {
      GST_INFO_OBJECT (comp, "Object %" GST_PTR_FORMAT " was marked"
          " for addition, removing it from the addition list", object);

      g_hash_table_remove (priv->pending_io, object);
      COMP_OBJECTS_UNLOCK (comp);
      return;
    }

    GST_ERROR_OBJECT (comp, "Object %" GST_PTR_FORMAT " is "
        " not in the composition", object);

    COMP_OBJECTS_UNLOCK (comp);
    return;
  }

  if (in_pending_io) {
    GST_WARNING_OBJECT (comp, "Object %" GST_PTR_FORMAT " is already marked"
        " for removal", object);

    COMP_OBJECTS_UNLOCK (comp);
    return;
  }


  g_hash_table_add (priv->pending_io, object);
  COMP_OBJECTS_UNLOCK (comp);

  return;
}

static void
_add_remove_object_gsource (GnlComposition * comp, GnlObject * object)
{
  ChildIOData *childio = g_slice_new0 (ChildIOData);

  childio->comp = comp;
  childio->object = object;

  MAIN_CONTEXT_LOCK (comp);
  g_main_context_invoke_full (comp->priv->mcontext, G_PRIORITY_HIGH,
      (GSourceFunc) _remove_object_func, childio, _free_child_io_data);
  MAIN_CONTEXT_UNLOCK (comp);
}

static gboolean
remove_object_handler (GnlComposition * comp, GnlObject * object)
{
  g_return_val_if_fail (GNL_IS_OBJECT (object), FALSE);

  _add_remove_object_gsource (comp, object);

  return TRUE;
}

static void
_add_object_func (ChildIOData * childio)
{
  GnlComposition *comp = childio->comp;
  GnlObject *object = childio->object;
  GnlCompositionPrivate *priv = comp->priv;
  GnlCompositionEntry *entry;
  GnlObject *in_pending_io;

  COMP_OBJECTS_LOCK (comp);
  entry = COMP_ENTRY (comp, object);
  in_pending_io = g_hash_table_lookup (priv->pending_io, object);

  if (entry) {
    GST_ERROR_OBJECT (comp, "Object %" GST_PTR_FORMAT " is "
        " already in the composition", object);

    COMP_OBJECTS_UNLOCK (comp);
    return;
  }

  if (in_pending_io) {
    GST_WARNING_OBJECT (comp, "Object %" GST_PTR_FORMAT " is already marked"
        " for addition", object);

    COMP_OBJECTS_UNLOCK (comp);
    return;
  }


  g_hash_table_add (priv->pending_io, object);

  COMP_OBJECTS_UNLOCK (comp);
  return;
}

static void
_add_add_object_gsource (GnlComposition * comp, GnlObject * object)
{
  ChildIOData *childio = g_slice_new0 (ChildIOData);

  childio->comp = comp;
  childio->object = object;

  MAIN_CONTEXT_LOCK (comp);
  g_main_context_invoke_full (comp->priv->mcontext, G_PRIORITY_HIGH,
      (GSourceFunc) _add_object_func, childio, _free_child_io_data);
  MAIN_CONTEXT_UNLOCK (comp);
}

static gboolean
add_object_handler (GnlComposition * comp, GnlObject * object)
{
  g_return_val_if_fail (GNL_IS_OBJECT (object), FALSE);

  _add_add_object_gsource (comp, object);

  return TRUE;
}

static void
gnl_composition_class_init (GnlCompositionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  GnlObjectClass *gnlobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;
  gnlobject_class = (GnlObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GnlCompositionPrivate));

  gst_element_class_set_static_metadata (gstelement_class,
      "GNonLin Composition", "Filter/Editor", "Combines GNL objects",
      "Wim Taymans <wim.taymans@gmail.com>, Edward Hervey <bilboed@bilboed.com>");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gnl_composition_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gnl_composition_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gnl_composition_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gnl_composition_get_property);

  gstelement_class->change_state = gnl_composition_change_state;

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (gnl_composition_add_object);
  gstbin_class->remove_element =
      GST_DEBUG_FUNCPTR (gnl_composition_remove_object);
  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gnl_composition_handle_message);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gnl_composition_src_template));

  /* Get the paramspec of the GnlObject klass so we can do
   * fast notifies */
  gnlobject_properties[GNLOBJECT_PROP_START] =
      g_object_class_find_property (gobject_class, "start");
  gnlobject_properties[GNLOBJECT_PROP_STOP] =
      g_object_class_find_property (gobject_class, "stop");
  gnlobject_properties[GNLOBJECT_PROP_DURATION] =
      g_object_class_find_property (gobject_class, "duration");

  /**
   * GnlComposition:deactivated-elements-state
   *
   * Get or set the #GstState in which elements that are not used
   * in the currently configured pipeline should be set.
   * By default the state is GST_STATE_READY to lower memory usage and avoid
   * using all the avalaible threads from the kernel but that means that in
   * certain case gapless will be more 'complicated' than if the state was set
   * to GST_STATE_PAUSED.
   */
  _properties[PROP_DEACTIVATED_ELEMENTS_STATE] =
      g_param_spec_enum ("deactivated-elements-state",
      "Deactivate elements state", "The state in which elements"
      " not used in the currently configured pipeline should"
      " be set", GST_TYPE_STATE, GST_STATE_READY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, _properties);

  /**
   * GnlComposition::commit
   * @comp: a #GnlComposition
   * @recurse: Whether to commit recursiverly into (GnlComposition) children of
   *           @object. This is used in case we have composition inside
   *           a gnlsource composition, telling it to commit the included
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
      G_STRUCT_OFFSET (GnlObjectClass, commit_signal_handler), NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 1, G_TYPE_BOOLEAN);

  _signals[COMMITED_SIGNAL] =
      g_signal_new ("commited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1,
      G_TYPE_BOOLEAN);

  _signals[REMOVE_OBJECT_SIGNAL] =
      g_signal_new ("remove-object", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GnlCompositionClass, remove_object_handler), NULL, NULL,
      NULL, G_TYPE_BOOLEAN, 1, GNL_TYPE_OBJECT);

  _signals[ADD_OBJECT_SIGNAL] =
      g_signal_new ("add-object", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GnlCompositionClass, add_object_handler), NULL, NULL,
      NULL, G_TYPE_BOOLEAN, 1, GNL_TYPE_OBJECT);


  gnlobject_class->commit = gnl_composition_commit_func;
  klass->remove_object_handler = remove_object_handler;
  klass->add_object_handler = add_object_handler;
}

static void
hash_value_destroy (GnlCompositionEntry * entry)
{
  GstPad *srcpad;
  GstElement *element = GST_ELEMENT (entry->object);

  srcpad = GNL_OBJECT_SRC (element);
  if (entry->probeid) {
    gst_pad_remove_probe (srcpad, entry->probeid);
    entry->probeid = 0;
  }

  if (entry->dataprobeid) {
    gst_pad_remove_probe (srcpad, entry->dataprobeid);
    entry->dataprobeid = 0;
  }

  g_slice_free (GnlCompositionEntry, entry);
}

static void
gnl_composition_init (GnlComposition * comp)
{
  GnlCompositionPrivate *priv;

  GST_OBJECT_FLAG_SET (comp, GNL_OBJECT_SOURCE);
  GST_OBJECT_FLAG_SET (comp, GNL_OBJECT_COMPOSITION);

  priv = G_TYPE_INSTANCE_GET_PRIVATE (comp, GNL_TYPE_COMPOSITION,
      GnlCompositionPrivate);
  g_mutex_init (&priv->objects_lock);
  priv->objects_start = NULL;
  priv->objects_stop = NULL;

  g_mutex_init (&priv->flushing_lock);
  priv->flushing = FALSE;

  priv->segment = gst_segment_new ();
  priv->outside_segment = gst_segment_new ();

  g_rec_mutex_init (&comp->task_rec_lock);

  priv->reset_time = FALSE;

  priv->objects_hash = g_hash_table_new_full
      (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) hash_value_destroy);

  priv->deactivated_elements_state = GST_STATE_READY;
  priv->mcontext = g_main_context_new ();
  g_mutex_init (&priv->mcontext_lock);
  priv->objects_hash = g_hash_table_new_full
      (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) hash_value_destroy);

  g_mutex_init (&priv->pending_io_lock);
  priv->pending_io = g_hash_table_new (g_direct_hash, g_direct_equal);

  comp->priv = priv;

  GST_ERROR_OBJECT (comp, "HERE");
  priv->current_bin = gst_bin_new ("current-bin");
  gst_bin_add (GST_BIN (comp), priv->current_bin);
  GST_ERROR_OBJECT (comp, "There");

  gnl_composition_reset (comp);

  priv->gnl_event_pad_func = GST_PAD_EVENTFUNC (GNL_OBJECT_SRC (comp));
  gst_pad_set_event_function (GNL_OBJECT_SRC (comp),
      GST_DEBUG_FUNCPTR (gnl_composition_event_handler));

  _start_task (comp);
}

static void
gnl_composition_dispose (GObject * object)
{
  GnlComposition *comp = GNL_COMPOSITION (object);
  GnlCompositionPrivate *priv = comp->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->current) {
    g_node_destroy (priv->current);
    priv->current = NULL;
  }

  if (priv->expandables) {
    g_list_free (priv->expandables);
    priv->expandables = NULL;
  }
  gnl_composition_reset_target_pad (comp);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gnl_composition_finalize (GObject * object)
{
  GnlComposition *comp = GNL_COMPOSITION (object);
  GnlCompositionPrivate *priv = comp->priv;

  COMP_OBJECTS_LOCK (comp);
  g_list_free (priv->objects_start);
  g_list_free (priv->objects_stop);
  if (priv->current)
    g_node_destroy (priv->current);
  g_hash_table_destroy (priv->objects_hash);
  COMP_OBJECTS_UNLOCK (comp);

  gst_segment_free (priv->segment);
  gst_segment_free (priv->outside_segment);

  g_mutex_clear (&priv->objects_lock);
  g_mutex_clear (&priv->flushing_lock);
  g_mutex_clear (&priv->pending_io_lock);

  _stop_task (comp, FALSE);
  g_rec_mutex_clear (&comp->task_rec_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);

  g_mutex_clear (&priv->mcontext_lock);
}

static void
gnl_composition_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GnlComposition *comp = GNL_COMPOSITION (object);

  switch (prop_id) {
    case PROP_DEACTIVATED_ELEMENTS_STATE:
      comp->priv->deactivated_elements_state = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gnl_composition_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GnlComposition *comp = GNL_COMPOSITION (object);

  switch (prop_id) {
    case PROP_DEACTIVATED_ELEMENTS_STATE:
      g_value_set_enum (value, comp->priv->deactivated_elements_state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* signal_duration_change
 * Creates a new GST_MESSAGE_DURATION_CHANGED with the currently configured
 * composition duration and sends that on the bus.
 */
static inline void
signal_duration_change (GnlComposition * comp)
{
  gst_element_post_message (GST_ELEMENT_CAST (comp),
      gst_message_new_duration_changed (GST_OBJECT_CAST (comp)));
}

static gboolean
unblock_child_pads (GValue * item, GValue * ret G_GNUC_UNUSED,
    GnlComposition * comp)
{
  GstPad *pad;
  GstElement *child = g_value_get_object (item);
  GnlCompositionEntry *entry = COMP_ENTRY (comp, child);

  GST_DEBUG_OBJECT (child, "unblocking pads");

  pad = GNL_OBJECT_SRC (child);
  if (entry->probeid) {
    gst_pad_remove_probe (pad, entry->probeid);
    entry->probeid = 0;
  }
  return TRUE;
}

static void
unblock_children (GnlComposition * comp)
{
  GstIterator *children;

  children = gst_bin_iterate_elements (GST_BIN (comp->priv->current_bin));

retry:
  if (G_UNLIKELY (gst_iterator_fold (children,
              (GstIteratorFoldFunction) unblock_child_pads, NULL,
              comp) == GST_ITERATOR_RESYNC)) {
    gst_iterator_resync (children);
    goto retry;
  }
  gst_iterator_free (children);
}


static gboolean
reset_child (GValue * item, GValue * ret G_GNUC_UNUSED, gpointer user_data)
{
  GnlCompositionEntry *entry;
  GstElement *child = g_value_get_object (item);
  GnlComposition *comp = GNL_COMPOSITION (user_data);
  GnlObject *object;
  GstPad *srcpad, *peerpad;

  GST_DEBUG_OBJECT (child, "unlocking state");
  gst_element_set_locked_state (child, FALSE);

  entry = COMP_ENTRY (comp, child);
  object = entry->object;
  srcpad = object->srcpad;
  peerpad = gst_pad_get_peer (srcpad);
  if (peerpad) {
    gst_pad_unlink (srcpad, peerpad);
    gst_object_unref (peerpad);
  }

  return TRUE;
}

static gboolean
lock_child_state (GValue * item, GValue * ret G_GNUC_UNUSED,
    gpointer udata G_GNUC_UNUSED)
{
  GstElement *child = g_value_get_object (item);

  GST_DEBUG_OBJECT (child, "locking state");
  gst_element_set_locked_state (child, TRUE);

  return TRUE;
}

static void
reset_children (GnlComposition * comp)
{
  GstIterator *children;

  children = gst_bin_iterate_elements (GST_BIN (comp->priv->current_bin));

retry:
  if (G_UNLIKELY (gst_iterator_fold (children,
              (GstIteratorFoldFunction) reset_child, NULL,
              comp) == GST_ITERATOR_RESYNC)) {
    gst_iterator_resync (children);
    goto retry;
  }
  gst_iterator_free (children);
}

static gboolean
_remove_child (GValue * item, GValue * ret G_GNUC_UNUSED, GstBin * bin)
{
  GstElement *child = g_value_get_object (item);

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
gnl_composition_reset (GnlComposition * comp)
{
  GnlCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "resetting");

  priv->segment_start = GST_CLOCK_TIME_NONE;
  priv->segment_stop = GST_CLOCK_TIME_NONE;
  priv->next_base_time = 0;

  gst_segment_init (priv->segment, GST_FORMAT_TIME);
  gst_segment_init (priv->outside_segment, GST_FORMAT_TIME);

  if (priv->current)
    g_node_destroy (priv->current);
  priv->current = NULL;

  priv->stackvalid = FALSE;

  gnl_composition_reset_target_pad (comp);

  reset_children (comp);

  COMP_FLUSHING_LOCK (comp);

  priv->flushing = FALSE;

  COMP_FLUSHING_UNLOCK (comp);

  priv->reset_time = FALSE;
  priv->initialized = FALSE;
  priv->send_stream_start = TRUE;

  _empty_bin (GST_BIN_CAST (priv->current_bin));

  GST_DEBUG_OBJECT (comp, "Composition now resetted");
}

static GstPadProbeReturn
ghost_event_probe_handler (GstPad * ghostpad G_GNUC_UNUSED,
    GstPadProbeInfo * info, GnlComposition * comp)
{
  GstPadProbeReturn retval = GST_PAD_PROBE_OK;
  GnlCompositionPrivate *priv = comp->priv;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GList *tmp;

  GST_DEBUG_OBJECT (comp, "event: %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (comp,
          "replacing flush stop event with a flush stop event with 'reset_time' = %d",
          priv->reset_time);
      GST_PAD_PROBE_INFO_DATA (info) =
          gst_event_new_flush_stop (priv->reset_time);
      gst_event_unref (event);
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

      COMP_FLUSHING_LOCK (comp);

      priv->flushing = FALSE;
      COMP_FLUSHING_UNLOCK (comp);

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
      gboolean reverse = (comp->priv->segment->rate < 0);
      gboolean should_check_objects = FALSE;

      GST_ERROR ("EOS");
      COMP_FLUSHING_LOCK (comp);
      if (priv->flushing) {
        GST_DEBUG_OBJECT (comp, "flushing, bailing out");
        COMP_FLUSHING_UNLOCK (comp);
        retval = GST_PAD_PROBE_DROP;
        break;
      }
      COMP_FLUSHING_UNLOCK (comp);

      if (reverse && GST_CLOCK_TIME_IS_VALID (comp->priv->segment_start))
        should_check_objects = TRUE;
      else if (!reverse && GST_CLOCK_TIME_IS_VALID (comp->priv->segment_stop))
        should_check_objects = TRUE;

      if (should_check_objects) {
        retval = GST_PAD_PROBE_OK;
        COMP_OBJECTS_LOCK (comp);
        for (tmp = comp->priv->objects_stop; tmp; tmp = g_list_next (tmp)) {
          GnlObject *object = (GnlObject *) tmp->data;

          if (!GNL_IS_SOURCE (object))
            continue;

          if ((!reverse && comp->priv->segment_stop < object->stop) ||
              (reverse && comp->priv->segment_start > object->start)) {
            retval = GST_PAD_PROBE_DROP;
            break;
          }
        }
        COMP_OBJECTS_UNLOCK (comp);
      }

      if (retval == GST_PAD_PROBE_OK) {
        GST_DEBUG_OBJECT (comp, "Got EOS for real, fowarding it");

        return GST_PAD_PROBE_OK;
      }

      _add_update_gsource (comp);

      retval = GST_PAD_PROBE_DROP;
    }
      break;
    default:
      break;
  }

  return retval;
}



/* Warning : Don't take the objects lock in this method */
static void
gnl_composition_handle_message (GstBin * bin, GstMessage * message)
{
  GnlComposition *comp = (GnlComposition *) bin;
  gboolean dropit = FALSE;

  GST_DEBUG_OBJECT (comp, "message:%s from %s",
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)),
      GST_MESSAGE_SRC (message) ? GST_ELEMENT_NAME (GST_MESSAGE_SRC (message)) :
      "UNKNOWN");

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
    {
      /* FIXME / HACK
       * There is a massive issue with reverse negotiation and dynamic pipelines.
       *
       * Since we're not waiting for the pads of the previous stack to block before
       * re-switching, we might end up switching sources in the middle of a downstrea
       * negotiation which will do reverse negotiation... with the new source (which
       * is no longer the one that issues the request). That negotiation will fail
       * and the original source will emit an ERROR message.
       *
       * In order to avoid those issues, we just ignore error messages from elements
       * which aren't in the currently configured stack
       */
      if (GST_MESSAGE_SRC (message) && GNL_IS_OBJECT (GST_MESSAGE_SRC (message))
          && !OBJECT_IN_ACTIVE_SEGMENT (comp, GST_MESSAGE_SRC (message))) {
        GST_DEBUG_OBJECT (comp,
            "HACK Dropping error message from object not in currently configured stack !");
        dropit = TRUE;
      }
    }
    default:
      break;
  }

  if (dropit)
    gst_message_unref (message);
  else
    GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static gint
priority_comp (GnlObject * a, GnlObject * b)
{
  if (a->priority < b->priority)
    return -1;

  if (a->priority > b->priority)
    return 1;

  return 0;
}

static inline gboolean
have_to_update_pipeline (GnlComposition * comp)
{
  GnlCompositionPrivate *priv = comp->priv;

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

/* OBJECTS LOCK must be taken when calling this ! */
static gboolean
update_pipeline_at_current_position (GnlComposition * comp)
{
  GstClockTime curpos;

  /* Get current position */
  if ((curpos = get_current_position (comp)) == GST_CLOCK_TIME_NONE) {
    if (GST_CLOCK_TIME_IS_VALID (comp->priv->segment_start))
      curpos = comp->priv->segment->start = comp->priv->segment_start;
    else
      curpos = 0;
  }

  update_start_stop_duration (comp);

  return update_pipeline (comp, curpos, TRUE, TRUE);
}

static gboolean
gnl_composition_commit_func (GnlObject * object, gboolean recurse)
{
  GST_ERROR ("Adding commit gsource");
  _add_commit_gsource (GNL_COMPOSITION (object));
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
get_new_seek_event (GnlComposition * comp, gboolean initial,
    gboolean updatestoponly)
{
  GstSeekFlags flags = GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;
  gint64 start, stop;
  GstSeekType starttype = GST_SEEK_TYPE_SET;
  GnlCompositionPrivate *priv = comp->priv;

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
get_current_position (GnlComposition * comp)
{
  GstPad *pad;
  GnlObject *obj;
  GnlCompositionPrivate *priv = comp->priv;
  gboolean res;
  gint64 value = GST_CLOCK_TIME_NONE;

  GstPad *peer = gst_pad_get_peer (GNL_OBJECT (comp)->srcpad);

  /* Try querying position downstream */

  if (peer) {
    res = gst_pad_query_position (peer, GST_FORMAT_TIME, &value);
    gst_object_unref (peer);

    if (res) {
      GST_LOG_OBJECT (comp,
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

  obj = (GnlObject *) priv->current->data;

  pad = GNL_OBJECT_SRC (obj);
  res = gst_pad_query_position (pad, GST_FORMAT_TIME, &value);

  if (G_UNLIKELY (res == FALSE)) {
    GST_WARNING_OBJECT (comp, "query position failed");
    value = GST_CLOCK_TIME_NONE;
  } else {
    GST_LOG_OBJECT (comp, "Query returned %" GST_TIME_FORMAT,
        GST_TIME_ARGS ((guint64) value));
  }

beach:
  return (guint64) value;
}

static gboolean
update_base_time (GNode * node, GstClockTime * timestamp)
{
  if (GNL_IS_OPERATION (node->data))
    gnl_operation_update_base_time (GNL_OPERATION (node->data), *timestamp);

  return FALSE;
}

/* WITH OBJECTS LOCK TAKEN */
static void
update_operations_base_time (GnlComposition * comp, gboolean reverse)
{
  GstClockTime timestamp;

  if (reverse)
    timestamp = comp->priv->segment->stop;
  else
    timestamp = comp->priv->segment->start;

  g_node_traverse (comp->priv->current, G_IN_ORDER, G_TRAVERSE_ALL, -1,
      (GNodeTraverseFunc) update_base_time, &timestamp);
}

/*
  Figures out if pipeline needs updating.
  Updates it and sends the seek event.
  Sends flush events downstream if needed.
  can be called by user_seek or segment_done

  initial : FIXME : ???? Always seems to be TRUE
  update : TRUE from EOS, FALSE from seek
*/

static gboolean
seek_handling (GnlComposition * comp, gboolean initial, gboolean update)
{
  GST_DEBUG_OBJECT (comp, "initial:%d, update:%d", initial, update);

  COMP_FLUSHING_LOCK (comp);
  GST_DEBUG_OBJECT (comp, "Setting flushing to TRUE");
  comp->priv->flushing = TRUE;
  COMP_FLUSHING_UNLOCK (comp);

  COMP_OBJECTS_LOCK (comp);
  if (update || have_to_update_pipeline (comp)) {
    if (comp->priv->segment->rate >= 0.0)
      update_pipeline (comp, comp->priv->segment->start, initial, !update);
    else
      update_pipeline (comp, comp->priv->segment->stop, initial, !update);
  } else {
    update_operations_base_time (comp, !(comp->priv->segment->rate >= 0.0));
  }
  COMP_OBJECTS_UNLOCK (comp);

  return TRUE;
}

static gboolean
gnl_composition_event_handler (GstPad * ghostpad, GstObject * parent,
    GstEvent * event)
{
  GnlComposition *comp = (GnlComposition *) parent;
  GnlCompositionPrivate *priv = comp->priv;
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (comp, "event type:%s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      _add_seek_gsource (comp, event);
      event = NULL;
      GST_FIXME_OBJECT (comp, "HANDLE seeking errors!");

      return TRUE;
    }
    case GST_EVENT_QOS:
    {
      gdouble prop;
      GstQOSType qostype;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &qostype, &prop, &diff, &timestamp);

      GST_INFO_OBJECT (comp,
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
        GST_INFO_OBJECT (comp,
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
    GST_DEBUG_OBJECT (comp, "About to call gnl_event_pad_func: %p",
        priv->gnl_event_pad_func);
    res = priv->gnl_event_pad_func (GNL_OBJECT (comp)->srcpad, parent, event);
    priv->reset_time = FALSE;
    GST_DEBUG_OBJECT (comp, "Done calling gnl_event_pad_func() %d", res);
  }

beach:
  return res;
}

static inline void
gnl_composition_reset_target_pad (GnlComposition * comp)
{
  GnlCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "Removing ghostpad");

  if (priv->ghosteventprobe) {
    GstPad *target;

    target = gst_ghost_pad_get_target ((GstGhostPad *) GNL_OBJECT_SRC (comp));
    if (target)
      gst_pad_remove_probe (target, priv->ghosteventprobe);
    priv->ghosteventprobe = 0;
  }

  gnl_object_ghost_pad_set_target (GNL_OBJECT (comp),
      GNL_OBJECT_SRC (comp), NULL);
  priv->toplevelentry = NULL;
  GST_ERROR ("NEED STRAM START");
  priv->send_stream_start = TRUE;
}

/* gnl_composition_ghost_pad_set_target:
 * target: The target #GstPad. The refcount will be decremented (given to the ghostpad).
 * entry: The GnlCompositionEntry to which the pad belongs
 */
static void
gnl_composition_ghost_pad_set_target (GnlComposition * comp, GstPad * target,
    GnlCompositionEntry * entry)
{
  GstPad *ptarget;
  GnlCompositionPrivate *priv = comp->priv;

  if (target)
    GST_DEBUG_OBJECT (comp, "target:%s:%s", GST_DEBUG_PAD_NAME (target));
  else
    GST_DEBUG_OBJECT (comp, "Removing target");


  ptarget =
      gst_ghost_pad_get_target (GST_GHOST_PAD (GNL_OBJECT (comp)->srcpad));
  if (ptarget && ptarget == target) {
    GST_DEBUG_OBJECT (comp,
        "Target of srcpad is the same as existing one, not changing");
    gst_object_unref (ptarget);
    return;
  }

  /* Actually set the target */
  gnl_object_ghost_pad_set_target ((GnlObject *) comp,
      GNL_OBJECT (comp)->srcpad, target);

  /* Set top-level entry (will be NULL if unsetting) */
  priv->toplevelentry = entry;

  if (target && (priv->ghosteventprobe == 0)) {
    priv->ghosteventprobe =
        gst_pad_add_probe (target,
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH,
        (GstPadProbeCallback) ghost_event_probe_handler, comp, NULL);
    GST_DEBUG_OBJECT (comp, "added event probe %lu", priv->ghosteventprobe);
  }

  GST_DEBUG_OBJECT (comp, "END");
}

static void
refine_start_stop_in_region_above_priority (GnlComposition * composition,
    GstClockTime timestamp, GstClockTime start,
    GstClockTime stop,
    GstClockTime * rstart, GstClockTime * rstop, guint32 priority)
{
  GList *tmp;
  GnlObject *object;
  GstClockTime nstart = start, nstop = stop;

  GST_DEBUG_OBJECT (composition,
      "timestamp:%" GST_TIME_FORMAT " start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " priority:%u", GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop), priority);

  for (tmp = composition->priv->objects_start; tmp; tmp = tmp->next) {
    object = (GnlObject *) tmp->data;

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
    object = (GnlObject *) tmp->data;

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
  GnlObject *object;

  if (!stack || !*stack)
    return NULL;

  object = (GnlObject *) (*stack)->data;

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

  if (GNL_OBJECT_IS_SOURCE (object)) {
    *stack = g_list_next (*stack);

    /* update highest priority.
     * We do this here, since it's only used with sources (leafs of the tree) */
    if (object->priority > *highprio)
      *highprio = object->priority;

    ret = g_node_new (object);

    goto beach;
  } else {
    /* GnlOperation */
    GnlOperation *oper = (GnlOperation *) object;

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
 * @comp: The #GnlComposition
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
get_stack_list (GnlComposition * comp, GstClockTime timestamp,
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
      GnlObject *object = (GnlObject *) tmp->data;

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
          if (GNL_IS_OPERATION (object))
            gnl_operation_update_base_time (GNL_OPERATION (object), timestamp);
        }
      } else {
        GST_LOG_OBJECT (comp, "too far, stopping iteration");
        first_out_of_stack = object->stop;
        break;
      }
    }
  } else {
    for (tmp = comp->priv->objects_start; tmp; tmp = g_list_next (tmp)) {
      GnlObject *object = (GnlObject *) tmp->data;

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
          if (GNL_IS_OPERATION (object))
            gnl_operation_update_base_time (GNL_OPERATION (object), timestamp);
        }
      } else {
        GST_LOG_OBJECT (comp, "too far, stopping iteration");
        first_out_of_stack = object->start;
        break;
      }
    }
  }

  /* Insert the expandables */
  if (G_LIKELY (timestamp < GNL_OBJECT_STOP (comp)))
    for (tmp = comp->priv->expandables; tmp; tmp = tmp->next) {
      GST_DEBUG_OBJECT (comp, "Adding expandable %s sorted to the list",
          GST_OBJECT_NAME (tmp->data));
      stack = g_list_insert_sorted (stack, tmp->data,
          (GCompareFunc) priority_comp);
      if (GNL_IS_OPERATION (tmp->data))
        gnl_operation_update_base_time (GNL_OPERATION (tmp->data), timestamp);
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
 * @comp: The #GnlComposition
 * @timestamp: The #GstClockTime to look at
 * @stop_time: Pointer to a #GstClockTime for min stop time of returned stack
 * @start_time: Pointer to a #GstClockTime for greatest start time of returned stack
 *
 * Returns: The new current stack for the given #GnlComposition and @timestamp.
 *
 * WITH OBJECTS LOCK TAKEN
 */
static GNode *
get_clean_toplevel_stack (GnlComposition * comp, GstClockTime * timestamp,
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
    guint32 top_priority = GNL_OBJECT_PRIORITY (stack->data);

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


static gboolean
set_child_caps (GValue * item, GValue * ret G_GNUC_UNUSED, GnlObject * comp)
{
  GstElement *child = g_value_get_object (item);

  gnl_object_set_caps ((GnlObject *) child, comp->caps);

  return TRUE;
}

/*  Must be called with OBJECTS_LOCK and PENDING_IO_LOCK taken */
static gboolean
_process_pending_entry (GnlObject * object,
    GnlObject * unused_object G_GNUC_UNUSED, GnlComposition * comp)
{
  GnlCompositionEntry *entry = COMP_ENTRY (comp, object);

  if (entry) {
    _gnl_composition_remove_entry (comp, object);
  } else {
    _gnl_composition_add_entry (comp, object);
  }

  return TRUE;
}

static gboolean
_commit_func (GnlComposition * comp)
{
  GList *tmp;
  gboolean commited = FALSE;
  GnlObject *object = GNL_OBJECT (comp);
  GnlCompositionPrivate *priv = comp->priv;

  GST_ERROR_OBJECT (object, "Commiting state");
  COMP_OBJECTS_LOCK (comp);

  g_hash_table_foreach_remove (priv->pending_io,
      (GHRFunc) _process_pending_entry, comp);

  for (tmp = priv->objects_start; tmp; tmp = tmp->next) {
    if (gnl_object_commit (tmp->data, TRUE))
      commited = TRUE;
  }

  GST_DEBUG_OBJECT (object, "Linking up commit vmethod");
  if (commited == FALSE &&
      (GNL_OBJECT_CLASS (parent_class)->commit (object, TRUE) == FALSE)) {
    COMP_OBJECTS_UNLOCK (comp);
    GST_ERROR_OBJECT (object, "Nothing to commit, leaving");
    g_signal_emit (comp, _signals[COMMITED_SIGNAL], 0, FALSE);
    return G_SOURCE_REMOVE;
  }

  /* The topology of the composition might have changed, update the lists */
  priv->objects_start = g_list_sort
      (priv->objects_start, (GCompareFunc) objects_start_compare);
  priv->objects_stop = g_list_sort
      (priv->objects_stop, (GCompareFunc) objects_stop_compare);

  if (priv->initialized == FALSE) {
    update_start_stop_duration (comp);
  } else {
    /* And update the pipeline at current position if needed */
    update_pipeline_at_current_position (comp);
  }
  COMP_OBJECTS_UNLOCK (comp);

  GST_ERROR ("emitted signal");
  g_signal_emit (comp, _signals[COMMITED_SIGNAL], 0, TRUE);
  return G_SOURCE_REMOVE;
}

static gboolean
update_pipeline_func (GnlComposition * comp)
{
  GnlCompositionPrivate *priv;
  gboolean reverse;

  /* Set up a non-initial seek on segment_stop */
  priv = comp->priv;
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

  seek_handling (comp, TRUE, TRUE);

  /* Post segment done if last seek was a segment seek */
  if (!priv->current && (priv->segment->flags & GST_SEEK_FLAG_SEGMENT)) {
    gint64 epos;

    if (GST_CLOCK_TIME_IS_VALID (priv->segment->stop))
      epos = (MIN (priv->segment->stop, GNL_OBJECT_STOP (comp)));
    else
      epos = GNL_OBJECT_STOP (comp);

    GST_LOG_OBJECT (comp, "Emitting segment done pos %" GST_TIME_FORMAT,
        GST_TIME_ARGS (epos));
    gst_element_post_message (GST_ELEMENT_CAST (comp),
        gst_message_new_segment_done (GST_OBJECT (comp),
            priv->segment->format, epos));
    gst_pad_push_event (GNL_OBJECT (comp)->srcpad,
        gst_event_new_segment_done (priv->segment->format, epos));
  }


  return G_SOURCE_REMOVE;
}

static GstStateChangeReturn
gnl_composition_change_state (GstElement * element, GstStateChange transition)
{
  GstIterator *children;
  GnlComposition *comp = (GnlComposition *) element;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (comp, "%s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gnl_composition_reset (comp);

      /* state-lock all elements */
      GST_DEBUG_OBJECT (comp,
          "Setting all children to READY and locking their state");

      children = gst_bin_iterate_elements (GST_BIN (comp->priv->current_bin));

      while (G_UNLIKELY (gst_iterator_fold (children,
                  (GstIteratorFoldFunction) lock_child_state, NULL,
                  NULL) == GST_ITERATOR_RESYNC)) {
        gst_iterator_resync (children);
      }
      gst_iterator_free (children);

      /* Set caps on all objects */
      if (G_UNLIKELY (!gst_caps_is_any (GNL_OBJECT (comp)->caps))) {
        children = gst_bin_iterate_elements (GST_BIN (comp->priv->current_bin));

        while (G_UNLIKELY (gst_iterator_fold (children,
                    (GstIteratorFoldFunction) set_child_caps, NULL,
                    comp) == GST_ITERATOR_RESYNC)) {
          gst_iterator_resync (children);
        }
        gst_iterator_free (children);
      }

      _add_initialize_stack_gsource (comp);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_element_set_state (comp->priv->current_bin, GST_STATE_READY);
      gnl_composition_reset (comp);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_element_set_state (comp->priv->current_bin, GST_STATE_NULL);
      gnl_composition_reset (comp);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      unblock_children (comp);
      break;
    default:
      break;
  }

  return ret;
}

static gint
objects_start_compare (GnlObject * a, GnlObject * b)
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
objects_stop_compare (GnlObject * a, GnlObject * b)
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
update_start_stop_duration (GnlComposition * comp)
{
  GnlObject *obj;
  GnlObject *cobj = (GnlObject *) comp;
  GnlCompositionPrivate *priv = comp->priv;

  if (!priv->objects_start) {
    GST_LOG ("no objects, resetting everything to 0");

    if (cobj->start) {
      cobj->start = cobj->pending_start = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          gnlobject_properties[GNLOBJECT_PROP_START]);
    }

    if (cobj->duration) {
      cobj->pending_duration = cobj->duration = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          gnlobject_properties[GNLOBJECT_PROP_DURATION]);
      signal_duration_change (comp);
    }

    if (cobj->stop) {
      cobj->stop = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          gnlobject_properties[GNLOBJECT_PROP_STOP]);
    }

    return;
  }

  /* If we have a default object, the start position is 0 */
  if (priv->expandables) {
    GST_LOG_OBJECT (cobj,
        "Setting start to 0 because we have a default object");

    if (cobj->start != 0) {
      cobj->pending_start = cobj->start = 0;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          gnlobject_properties[GNLOBJECT_PROP_START]);
    }

  } else {

    /* Else it's the first object's start value */
    obj = (GnlObject *) priv->objects_start->data;

    if (obj->start != cobj->start) {
      GST_LOG_OBJECT (obj, "setting start from %s to %" GST_TIME_FORMAT,
          GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->start));
      cobj->pending_start = cobj->start = obj->start;
      g_object_notify_by_pspec (G_OBJECT (cobj),
          gnlobject_properties[GNLOBJECT_PROP_START]);
    }

  }

  obj = (GnlObject *) priv->objects_stop->data;

  if (obj->stop != cobj->stop) {
    GST_LOG_OBJECT (obj, "setting stop from %s to %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->stop));

    if (priv->expandables) {
      GList *tmp;

      GST_INFO_OBJECT (comp, "RE-setting all expandables duration and commit");
      for (tmp = priv->expandables; tmp; tmp = tmp->next) {
        g_object_set (tmp->data, "duration", obj->stop, NULL);
        gnl_object_commit (GNL_OBJECT (tmp->data), FALSE);
      }
    }

    priv->segment->stop = obj->stop;
    cobj->stop = obj->stop;
    g_object_notify_by_pspec (G_OBJECT (cobj),
        gnlobject_properties[GNLOBJECT_PROP_STOP]);
  }

  if ((cobj->stop - cobj->start) != cobj->duration) {
    cobj->pending_duration = cobj->duration = cobj->stop - cobj->start;
    g_object_notify_by_pspec (G_OBJECT (cobj),
        gnlobject_properties[GNLOBJECT_PROP_DURATION]);
    signal_duration_change (comp);
  }

  GST_LOG_OBJECT (comp,
      "start:%" GST_TIME_FORMAT
      " stop:%" GST_TIME_FORMAT
      " duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (cobj->start),
      GST_TIME_ARGS (cobj->stop), GST_TIME_ARGS (cobj->duration));
}

static inline gboolean
_parent_or_priority_changed (GnlObject * obj, GNode * oldnode,
    GnlObject * newparent, GNode * node)
{
  GnlObject *oldparent = NULL;

  if (oldnode)
    oldparent =
        G_NODE_IS_ROOT (oldnode) ? NULL : (GnlObject *) oldnode->parent->data;

  if (oldparent != newparent)
    return TRUE;

  if (oldparent == NULL || newparent == NULL)
    return FALSE;

  return (g_node_child_index (node, obj) != g_node_child_index (oldnode, obj));
}

static void
_link_to_parent (GnlComposition * comp, GnlObject * newobj,
    GnlObject * newparent)
{
  GstPad *sinkpad;

  /* relink to new parent in required order */
  GST_LOG_OBJECT (comp, "Linking %s and %s",
      GST_ELEMENT_NAME (GST_ELEMENT (newobj)),
      GST_ELEMENT_NAME (GST_ELEMENT (newparent)));

  sinkpad = get_unlinked_sink_ghost_pad ((GnlOperation *) newparent);

  if (G_UNLIKELY (sinkpad == NULL)) {
    GST_WARNING_OBJECT (comp,
        "Couldn't find an unlinked sinkpad from %s",
        GST_ELEMENT_NAME (newparent));
  } else {
    if (G_UNLIKELY (gst_pad_link_full (GNL_OBJECT_SRC (newobj), sinkpad,
                GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK)) {
      GST_WARNING_OBJECT (comp, "Failed to link pads %s:%s - %s:%s",
          GST_DEBUG_PAD_NAME (GNL_OBJECT_SRC (newobj)),
          GST_DEBUG_PAD_NAME (sinkpad));
    }
    gst_object_unref (sinkpad);
  }
}

static void
_relink_children_recursively (GnlComposition * comp,
    GnlObject * newobj, GNode * node, GstEvent * toplevel_seek)
{
  GNode *child;
  guint nbchildren = g_node_n_children (node);
  GnlOperation *oper = (GnlOperation *) newobj;

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
_relink_single_node (GnlComposition * comp, GNode * node,
    GstEvent * toplevel_seek)
{
  GnlObject *newobj;
  GnlObject *newparent;
  GstPad *srcpad = NULL, *sinkpad = NULL;
  GstEvent *translated_seek;

  if (G_UNLIKELY (!node))
    return;

  newparent = G_NODE_IS_ROOT (node) ? NULL : (GnlObject *) node->parent->data;
  newobj = (GnlObject *) node->data;

  GST_DEBUG_OBJECT (comp, "newobj:%s",
      GST_ELEMENT_NAME ((GstElement *) newobj));

  srcpad = GNL_OBJECT_SRC (newobj);

  gst_bin_add (GST_BIN (comp->priv->current_bin), gst_object_ref (newobj));
  gst_element_sync_state_with_parent (GST_ELEMENT_CAST (newobj));

  translated_seek = gnl_object_translate_incoming_seek (newobj, toplevel_seek);

  gst_element_send_event (GST_ELEMENT (newobj), translated_seek);

  /* link to parent if needed.  */
  if (newparent) {
    _link_to_parent (comp, newobj, newparent);

    /* If there's an operation, inform it about priority changes */
    sinkpad = gst_pad_get_peer (srcpad);
    gnl_operation_signal_input_priority_changed ((GnlOperation *)
        newparent, sinkpad, newobj->priority);
    gst_object_unref (sinkpad);
  }

  /* Handle children */
  if (GNL_IS_OPERATION (newobj))
    _relink_children_recursively (comp, newobj, node, toplevel_seek);

  GST_LOG_OBJECT (comp, "done with object %s",
      GST_ELEMENT_NAME (GST_ELEMENT (newobj)));
}



/*
 * compare_relink_stack:
 * @comp: The #GnlComposition
 * @stack: The new stack
 * @modify: TRUE if the timeline has changed and needs downstream flushes.
 *
 * Compares the given stack to the current one and relinks it if needed.
 *
 * WITH OBJECTS LOCK TAKEN
 *
 * Returns: The #GList of #GnlObject no longer used
 */

static GList *
compare_relink_stack (GnlComposition * comp, GNode * stack, gboolean modify)
{
  GstPad *ptarget;
  GstEvent *toplevel_seek = get_new_seek_event (comp, TRUE, FALSE);
  GList *deactivate = NULL;

  gst_element_set_locked_state (comp->priv->current_bin, TRUE);

  GST_ERROR ("Set state return: %s",
      gst_element_state_change_return_get_name
      (gst_element_set_state (comp->priv->current_bin, GST_STATE_READY)));

  ptarget =
      gst_ghost_pad_get_target (GST_GHOST_PAD (GNL_OBJECT (comp)->srcpad));
  _empty_bin (GST_BIN_CAST (comp->priv->current_bin));

  if (comp->priv->ghosteventprobe) {
    gst_pad_remove_probe (ptarget, comp->priv->ghosteventprobe);
    comp->priv->ghosteventprobe = 0;
  }


  _relink_single_node (comp, stack, toplevel_seek);

  gst_element_set_locked_state (comp->priv->current_bin, FALSE);
  gst_element_sync_state_with_parent (comp->priv->current_bin);

  return deactivate;
}

static void
unlock_activate_stack (GnlComposition * comp, GNode * node, GstState state)
{
  GNode *child;

  GST_LOG_OBJECT (comp, "object:%s",
      GST_ELEMENT_NAME ((GstElement *) (node->data)));

  gst_element_set_locked_state ((GstElement *) (node->data), FALSE);
  gst_element_set_state (GST_ELEMENT (node->data), state);

  for (child = node->children; child; child = child->next)
    unlock_activate_stack (comp, child, state);
}

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
_activate_new_stack (GnlComposition * comp, gboolean forcing_flush)
{
  GstPad *pad;
  GstElement *topelement;
  GnlCompositionEntry *topentry;

  GnlCompositionPrivate *priv = comp->priv;

  if (!priv->current) {
    if ((!priv->objects_start)) {
      gnl_composition_reset_target_pad (comp);
      priv->segment_start = 0;
      priv->segment_stop = GST_CLOCK_TIME_NONE;
    }

    GST_ERROR_OBJECT (comp, "Nothing else in the composition"
        ", update 'worked'");
    return TRUE;
  }

  priv->stackvalid = TRUE;

  /* The stack is entirely ready, send seek out synchronously */
  topelement = GST_ELEMENT (priv->current->data);
  /* Get toplevel object source pad */
  pad = GNL_OBJECT_SRC (topelement);
  topentry = COMP_ENTRY (comp, topelement);

  GST_ERROR_OBJECT (comp,
      "We have a valid toplevel element pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gnl_composition_ghost_pad_set_target (comp, pad, topentry);

  GST_ERROR_OBJECT (comp, "New stack activated!");
  return TRUE;
}

/*
 * update_pipeline:
 * @comp: The #GnlComposition
 * @currenttime: The #GstClockTime to update at, can be GST_CLOCK_TIME_NONE.
 * @initial: TRUE if this is the first setup
 * @change_state: Change the state of the (de)activated objects if TRUE.
 * @modify: Flush downstream if TRUE. Needed for modified timelines.
 *
 * Updates the internal pipeline and properties. If @currenttime is
 * GST_CLOCK_TIME_NONE, it will not modify the current pipeline
 *
 * Returns: FALSE if there was an error updating the pipeline.
 *
 * WITH OBJECTS LOCK TAKEN
 */
static gboolean
update_pipeline (GnlComposition * comp, GstClockTime currenttime,
    gboolean initial, gboolean modify)
{
  gboolean startchanged, stopchanged;

  GNode *stack = NULL;
  gboolean samestack = FALSE;
  gboolean forcing_flush = initial;
  GstState state = GST_STATE (comp);
  GnlCompositionPrivate *priv = comp->priv;
  GstClockTime new_stop = GST_CLOCK_TIME_NONE;
  GstClockTime new_start = GST_CLOCK_TIME_NONE;
  GstState nextstate = (GST_STATE_NEXT (comp) == GST_STATE_VOID_PENDING) ?
      GST_STATE (comp) : GST_STATE_NEXT (comp);

  GST_ERROR_OBJECT (comp,
      "currenttime:%" GST_TIME_FORMAT
      " initial:%d , modify:%d", GST_TIME_ARGS (currenttime), initial, modify);

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

  /* invalidate the stack while modifying it */
  priv->stackvalid = FALSE;

  if (priv->segment->rate >= 0.0) {
    startchanged = priv->segment_start != currenttime;
    stopchanged = priv->segment_stop != new_stop;
  } else {
    startchanged = priv->segment_start != new_start;
    stopchanged = priv->segment_stop != currenttime;
  }

  /* set new segment_start/stop (the current zone over which the new stack
   * is valid) */
  if (priv->segment->rate >= 0.0) {
    priv->segment_start = currenttime;
    priv->segment_stop = new_stop;
  } else {
    priv->segment_start = new_start;
    priv->segment_stop = currenttime;
  }

  /* If stacks are different, unlink/relink objects */
  if (!samestack)
    compare_relink_stack (comp, stack, modify);

  /* Unlock all elements in new stack */
  GST_DEBUG_OBJECT (comp, "Setting current stack");
  priv->current = stack;

  if (!samestack && stack) {
    GST_DEBUG_OBJECT (comp, "activating objects in new stack to %s",
        gst_element_state_get_name (nextstate));
    unlock_activate_stack (comp, stack, nextstate);
    GST_DEBUG_OBJECT (comp, "Finished activating objects in new stack");
  }

  /* Activate stack */
  if (samestack && (startchanged || stopchanged)) {
    /* Update seek events need to be flushing if not in PLAYING,
     * else we will encounter deadlocks. */
    forcing_flush = (state == GST_STATE_PLAYING) ? FALSE : TRUE;
  }

  _activate_new_stack (comp, forcing_flush);
  return TRUE;
}

static gboolean
gnl_composition_add_object (GstBin * bin, GstElement * element)
{
  GnlComposition *comp = (GnlComposition *) bin;

  if (element == comp->priv->current_bin) {
    GST_ERROR_OBJECT (comp, "Adding internal bin");
    return GST_BIN_CLASS (parent_class)->add_element (bin, element);
  }

  g_assert_not_reached ();

  return FALSE;
}

static gboolean
_gnl_composition_add_entry (GnlComposition * comp, GnlObject * object)
{
  gboolean ret = TRUE;

  GnlCompositionEntry *entry;
  GnlCompositionPrivate *priv = comp->priv;

  GST_DEBUG_OBJECT (comp, "element %s", GST_OBJECT_NAME (object));
  GST_DEBUG_OBJECT (object, "%" GST_TIME_FORMAT "--%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GNL_OBJECT_START (object)),
      GST_TIME_ARGS (GNL_OBJECT_STOP (object)));

  g_object_ref_sink (object);

  if ((GNL_OBJECT_IS_EXPANDABLE (object)) &&
      g_list_find (priv->expandables, object)) {
    GST_WARNING_OBJECT (comp,
        "We already have an expandable, remove it before adding new one");
    ret = FALSE;

    goto chiringuito;
  }

  gnl_object_set_commit_needed (GNL_OBJECT (comp));

  if (!ret) {
    GST_WARNING_OBJECT (comp, "couldn't add object");
    goto chiringuito;
  }

  /* lock state of child ! */
  GST_LOG_OBJECT (comp, "Locking state of %s", GST_ELEMENT_NAME (object));
  gst_element_set_locked_state (GST_ELEMENT (object), TRUE);

  /* wrap new element in a GnlCompositionEntry ... */
  entry = g_slice_new0 (GnlCompositionEntry);
  entry->object = (GnlObject *) object;
  entry->comp = comp;

  if (GNL_OBJECT_IS_EXPANDABLE (object)) {
    /* Only react on non-default objects properties */
    g_object_set (object,
        "start", (GstClockTime) 0,
        "inpoint", (GstClockTime) 0,
        "duration", (GstClockTimeDiff) GNL_OBJECT_STOP (comp), NULL);

    GST_INFO_OBJECT (object, "Used as expandable, commiting now");
    gnl_object_commit (GNL_OBJECT (object), FALSE);
  }

  /* ...and add it to the hash table */
  g_hash_table_insert (priv->objects_hash, object, entry);

  /* Set the caps of the composition */
  if (G_UNLIKELY (!gst_caps_is_any (((GnlObject *) comp)->caps)))
    gnl_object_set_caps ((GnlObject *) object, ((GnlObject *) comp)->caps);

  /* Special case for default source. */
  if (GNL_OBJECT_IS_EXPANDABLE (object)) {
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
        GST_TIME_ARGS (GNL_OBJECT_START (priv->objects_start->data)),
        GST_TIME_ARGS (GNL_OBJECT_STOP (priv->objects_start->data)));

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
gnl_composition_remove_object (GstBin * bin, GstElement * element)
{
  GnlComposition *comp = (GnlComposition *) bin;

  if (element == comp->priv->current_bin) {
    GST_ERROR_OBJECT (comp, "Adding internal bin");
    return GST_BIN_CLASS (parent_class)->remove_element (bin, element);
  }

  g_assert_not_reached ();

  return FALSE;
}

static gboolean
_gnl_composition_remove_entry (GnlComposition * comp, GnlObject * object)
{
  gboolean ret = FALSE;
  GnlCompositionEntry *entry;
  GnlCompositionPrivate *priv = comp->priv;

  GST_ERROR_OBJECT (comp, "object %s", GST_OBJECT_NAME (object));

  /* we only accept GnlObject */
  entry = COMP_ENTRY (comp, object);
  if (entry == NULL) {
    goto out;
  }

  gst_element_set_locked_state (GST_ELEMENT (object), FALSE);

  /* handle default source */
  if (GNL_OBJECT_IS_EXPANDABLE (object)) {
    /* Find it in the list */
    priv->expandables = g_list_remove (priv->expandables, object);
  } else {
    /* remove it from the objects list and resort the lists */
    priv->objects_start = g_list_remove (priv->objects_start, object);
    priv->objects_stop = g_list_remove (priv->objects_stop, object);
    GST_LOG_OBJECT (object, "Removed from the objects start/stop list");
  }

  if (priv->current && GNL_OBJECT (priv->current->data) == GNL_OBJECT (object))
    gnl_composition_reset_target_pad (comp);

  g_hash_table_remove (priv->objects_hash, object);

  GST_LOG_OBJECT (object, "Done removing from the composition, now updating");

  /* Make it possible to reuse the same object later */
  gnl_object_reset (GNL_OBJECT (object));
  gst_object_unref (object);

out:
  return ret;
}
