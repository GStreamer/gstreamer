/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@gmail.com>
 *               2004-2008 Edward Hervey <bilboed@bilboed.com>
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
  LAST_SIGNAL
};

typedef struct _GnlCompositionEntry GnlCompositionEntry;

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

  GThread *update_pipeline_thread;
  GCond update_pipeline_cond;
  GMutex update_pipeline_mutex;

  gboolean reset_time;

  gboolean running;

  GstState deactivated_elements_state;
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

static GstPadProbeReturn pad_blocked (GstPad * pad, GstPadProbeInfo * info,
    GnlComposition * comp);
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

#define WAIT_FOR_UPDATE_PIPELINE(comp)   G_STMT_START {                        \
  GST_INFO_OBJECT (comp, "waiting for EOS from thread %p",                     \
        g_thread_self());                                                      \
  g_mutex_lock(&(comp->priv->update_pipeline_mutex));                          \
  g_cond_wait(&(comp->priv->update_pipeline_cond),                             \
      &(comp->priv->update_pipeline_mutex));                                   \
  g_mutex_unlock(&(comp->priv->update_pipeline_mutex));                        \
  } G_STMT_END

#define SIGNAL_UPDATE_PIPELINE(comp) {                                         \
  GST_INFO_OBJECT (comp, "signaling EOS from thread %p",                       \
        g_thread_self());                                                      \
  g_mutex_lock(&(comp->priv->update_pipeline_mutex));                          \
  g_cond_signal(&(comp->priv->update_pipeline_cond));                          \
  g_mutex_unlock(&(comp->priv->update_pipeline_mutex));                        \
  } G_STMT_END



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

  gnlobject_class->commit = gnl_composition_commit_func;
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

  priv->reset_time = FALSE;

  priv->objects_hash = g_hash_table_new_full
      (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) hash_value_destroy);

  priv->deactivated_elements_state = GST_STATE_READY;

  comp->priv = priv;

  gnl_composition_reset (comp);

  priv->gnl_event_pad_func = GST_PAD_EVENTFUNC (GNL_OBJECT_SRC (comp));
  gst_pad_set_event_function (GNL_OBJECT_SRC (comp),
      GST_DEBUG_FUNCPTR (gnl_composition_event_handler));
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

  GST_INFO ("finalize");

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

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

  children = gst_bin_iterate_elements (GST_BIN (comp));

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

  children = gst_bin_iterate_elements (GST_BIN (comp));
retry:
  if (G_UNLIKELY (gst_iterator_fold (children,
              (GstIteratorFoldFunction) reset_child, NULL,
              comp) == GST_ITERATOR_RESYNC)) {
    gst_iterator_resync (children);
    goto retry;
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

  priv->send_stream_start = TRUE;

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

      SIGNAL_UPDATE_PIPELINE (comp);

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
  GList *tmp;
  gboolean commited = FALSE;
  GnlComposition *comp = GNL_COMPOSITION (object);
  GnlCompositionPrivate *priv = comp->priv;


  GST_DEBUG_OBJECT (object, "Commiting state");
  COMP_OBJECTS_LOCK (comp);
  for (tmp = priv->objects_start; tmp; tmp = tmp->next) {
    if (gnl_object_commit (tmp->data, recurse))
      commited = TRUE;
  }

  GST_DEBUG_OBJECT (object, "Linking up commit vmethod");
  if (commited == FALSE &&
      (GNL_OBJECT_CLASS (parent_class)->commit (object, recurse) == FALSE)) {
    COMP_OBJECTS_UNLOCK (comp);
    GST_DEBUG_OBJECT (object, "Nothing to commit, leaving");
    return FALSE;
  }

  /* The topology of the composition might have changed, update the lists */
  priv->objects_start = g_list_sort
      (priv->objects_start, (GCompareFunc) objects_start_compare);
  priv->objects_stop = g_list_sort
      (priv->objects_stop, (GCompareFunc) objects_stop_compare);

  /* And update the pipeline at current position if needed */
  update_pipeline_at_current_position (comp);
  COMP_OBJECTS_UNLOCK (comp);

  GST_DEBUG_OBJECT (object, "Done commiting");
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

  /* 1. Try querying position downstream */

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

  /* 2. If downstream fails , try within the current stack */
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
handle_seek_event (GnlComposition * comp, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  GnlCompositionPrivate *priv = comp->priv;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  GST_DEBUG_OBJECT (comp,
      "start:%" GST_TIME_FORMAT " -- stop:%" GST_TIME_FORMAT "  flags:%d",
      GST_TIME_ARGS (cur), GST_TIME_ARGS (stop), flags);

  gst_segment_do_seek (priv->segment,
      rate, format, flags, cur_type, cur, stop_type, stop, NULL);
  gst_segment_do_seek (priv->outside_segment,
      rate, format, flags, cur_type, cur, stop_type, stop, NULL);

  GST_DEBUG_OBJECT (comp, "Segment now has flags:%d", priv->segment->flags);

  if (priv->segment->start >= GNL_OBJECT_STOP (comp)) {
    GST_INFO_OBJECT (comp,
        "Start %" GST_TIME_FORMAT " > comp->stop: %" GST_TIME_FORMAT
        " Not seeking", GST_TIME_ARGS (priv->segment->start),
        GST_TIME_ARGS (GNL_OBJECT_STOP (comp)));
    return FALSE;
  }

  /* crop the segment start/stop values */
  /* Only crop segment start value if we don't have a default object */
  if (priv->expandables == NULL)
    priv->segment->start = MAX (priv->segment->start, GNL_OBJECT_START (comp));
  priv->segment->stop = MIN (priv->segment->stop, GNL_OBJECT_STOP (comp));

  comp->priv->next_base_time = 0;

  seek_handling (comp, TRUE, FALSE);
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
      GstEvent *nevent;

      if ((res = handle_seek_event (comp, event)) == FALSE) {
        gst_event_unref (event);
        event = NULL;
        break;
      }

      /* the incoming event might not be quite correct, we get a new proper
       * event to pass on to the children. */
      COMP_OBJECTS_LOCK (comp);
      nevent = get_new_seek_event (comp, FALSE, FALSE);
      COMP_OBJECTS_UNLOCK (comp);
      gst_event_unref (event);
      event = nevent;
      priv->reset_time = TRUE;
      break;
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

static GstPadProbeReturn
pad_blocked (GstPad * pad, GstPadProbeInfo * info, GnlComposition * comp)
{
  GST_DEBUG_OBJECT (comp, "Pad : %s:%s", GST_DEBUG_PAD_NAME (pad));

  return GST_PAD_PROBE_OK;
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

static GstPadProbeReturn
drop_data (GstPad * pad, GstPadProbeInfo * info, GnlCompositionEntry * entry)
{
  /* When updating the pipeline, do not let data flowing */
  if (!GST_IS_EVENT (info->data)) {
    GST_LOG_OBJECT (pad, "Dropping data while updating pipeline");
    return GST_PAD_PROBE_DROP;
  } else {
    GstEvent *event = GST_EVENT (info->data);

    if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
      entry->seeked = TRUE;
      GST_DEBUG_OBJECT (pad, "Got SEEK event");
    } else if (entry->seeked == TRUE &&
        GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
      entry->seeked = FALSE;
      entry->dataprobeid = 0;

      GST_DEBUG_OBJECT (pad, "Already seeked and got segment,"
          " removing probe");
      return GST_PAD_PROBE_REMOVE;
    }
  }

  return GST_PAD_PROBE_OK;
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

  /* Unset previous target */
  if (ptarget) {
    GST_DEBUG_OBJECT (comp, "Previous target was %s:%s",
        GST_DEBUG_PAD_NAME (ptarget));

    if (!priv->toplevelentry->probeid) {
      /* If it's not blocked, block it */
      priv->toplevelentry->probeid =
          gst_pad_add_probe (ptarget,
          GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_IDLE,
          (GstPadProbeCallback) pad_blocked, comp, NULL);
    }

    if (!priv->toplevelentry->dataprobeid) {
      priv->toplevelentry->dataprobeid = gst_pad_add_probe (ptarget,
          GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
          GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback) drop_data,
          priv->toplevelentry, NULL);
    }

    /* remove event probe */
    if (priv->ghosteventprobe) {
      gst_pad_remove_probe (ptarget, priv->ghosteventprobe);
      priv->ghosteventprobe = 0;
    }
    gst_object_unref (ptarget);

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

static gpointer
update_pipeline_func (GnlComposition * comp)
{
  while (comp->priv->running) {
    GnlCompositionPrivate *priv;
    gboolean reverse;

    WAIT_FOR_UPDATE_PIPELINE (comp);

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

    if (!priv->current) {
      /* If we're at the end, post SEGMENT_DONE, or push EOS */
      GST_DEBUG_OBJECT (comp, "Nothing else to play");

      if (!(priv->segment->flags & GST_SEEK_FLAG_SEGMENT)) {
        GST_DEBUG_OBJECT (comp, "Real EOS should be sent now");
      } else if (priv->segment->flags & GST_SEEK_FLAG_SEGMENT) {
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
    }

  }

  return NULL;
}

static GstStateChangeReturn
gnl_composition_change_state (GstElement * element, GstStateChange transition)
{
  GnlComposition *comp = (GnlComposition *) element;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (comp, "%s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      comp->priv->running = TRUE;
      comp->priv->update_pipeline_thread =
          g_thread_new ("update_pipeline_thread",
          (GThreadFunc) update_pipeline_func, comp);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GstIterator *children;

      gnl_composition_reset (comp);

      /* state-lock all elements */
      GST_DEBUG_OBJECT (comp,
          "Setting all children to READY and locking their state");

      children = gst_bin_iterate_elements (GST_BIN (comp));

    retry_lock:
      if (G_UNLIKELY (gst_iterator_fold (children,
                  (GstIteratorFoldFunction) lock_child_state, NULL,
                  NULL) == GST_ITERATOR_RESYNC)) {
        gst_iterator_resync (children);
        goto retry_lock;
      }

      gst_iterator_free (children);

      /* Set caps on all objects */
      if (G_UNLIKELY (!gst_caps_is_any (GNL_OBJECT (comp)->caps))) {
        children = gst_bin_iterate_elements (GST_BIN (comp));

      retry_caps:
        if (G_UNLIKELY (gst_iterator_fold (children,
                    (GstIteratorFoldFunction) set_child_caps, NULL,
                    comp) == GST_ITERATOR_RESYNC)) {
          gst_iterator_resync (children);
          goto retry_caps;
        }
        gst_iterator_free (children);
      }


      /* set ghostpad target */
      COMP_OBJECTS_LOCK (comp);
      if (!(update_pipeline (comp, COMP_REAL_START (comp), TRUE, TRUE))) {
        ret = GST_STATE_CHANGE_FAILURE;
        COMP_OBJECTS_UNLOCK (comp);
        goto beach;
      }

      COMP_OBJECTS_UNLOCK (comp);
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gnl_composition_reset (comp);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gnl_composition_reset (comp);
      comp->priv->running = FALSE;
      SIGNAL_UPDATE_PIPELINE (comp);
      g_thread_join (comp->priv->update_pipeline_thread);
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

beach:
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
compare_relink_single_node (GnlComposition * comp, GNode * node,
    GNode * oldstack)
{
  GNode *child;
  GNode *oldnode = NULL;
  GnlObject *newobj;
  GnlObject *newparent;
  GnlObject *oldparent = NULL;
  GstPad *srcpad = NULL, *sinkpad = NULL;
  GnlCompositionEntry *entry;

  if (G_UNLIKELY (!node))
    return;

  newparent = G_NODE_IS_ROOT (node) ? NULL : (GnlObject *) node->parent->data;
  newobj = (GnlObject *) node->data;
  if (oldstack) {
    oldnode = g_node_find (oldstack, G_IN_ORDER, G_TRAVERSE_ALL, newobj);
    if (oldnode)
      oldparent =
          G_NODE_IS_ROOT (oldnode) ? NULL : (GnlObject *) oldnode->parent->data;
  }

  GST_DEBUG_OBJECT (comp, "newobj:%s",
      GST_ELEMENT_NAME ((GstElement *) newobj));

  srcpad = GNL_OBJECT_SRC (newobj);

  /* 1. Make sure the source pad is blocked for new objects */
  if (G_UNLIKELY (!oldnode)) {
    GnlCompositionEntry *oldentry = COMP_ENTRY (comp, newobj);
    if (!oldentry->probeid) {
      GST_LOG_OBJECT (comp, "block_async(%s:%s, TRUE)",
          GST_DEBUG_PAD_NAME (srcpad));
      oldentry->probeid =
          gst_pad_add_probe (srcpad,
          GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_IDLE,
          (GstPadProbeCallback) pad_blocked, comp, NULL);
    }
    if (!oldentry->dataprobeid) {
      oldentry->dataprobeid = gst_pad_add_probe (srcpad,
          GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
          GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback) drop_data,
          oldentry, NULL);
    }
  }

  entry = COMP_ENTRY (comp, newobj);
  /* 2. link to parent if needed.  */
  GST_LOG_OBJECT (comp, "has a valid source pad");
  /* POST PROCESSING */
  if ((oldparent != newparent) ||
      (oldparent && newparent &&
          (g_node_child_index (node,
                  newobj) != g_node_child_index (oldnode, newobj)))) {
    GST_LOG_OBJECT (comp,
        "not same parent, or same parent but in different order");
    /* relink to new parent in required order */
    if (newparent) {
      GstPad *sinkpad;
      GST_LOG_OBJECT (comp, "Linking %s and %s",
          GST_ELEMENT_NAME (GST_ELEMENT (newobj)),
          GST_ELEMENT_NAME (GST_ELEMENT (newparent)));
      sinkpad = get_unlinked_sink_ghost_pad ((GnlOperation *) newparent);
      if (G_UNLIKELY (sinkpad == NULL)) {
        GST_WARNING_OBJECT (comp,
            "Couldn't find an unlinked sinkpad from %s",
            GST_ELEMENT_NAME (newparent));
      } else {
        if (G_UNLIKELY (gst_pad_link_full (srcpad, sinkpad,
                    GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK)) {
          GST_WARNING_OBJECT (comp, "Failed to link pads %s:%s - %s:%s",
              GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
        }
        gst_object_unref (sinkpad);
      }
    }
  } else {
    GST_LOG_OBJECT (newobj, "Same parent and same position in the new stack");
  }

  /* If there's an operation, inform it about priority changes */
  if (newparent) {
    sinkpad = gst_pad_get_peer (srcpad);
    gnl_operation_signal_input_priority_changed ((GnlOperation *)
        newparent, sinkpad, newobj->priority);
    gst_object_unref (sinkpad);
  }


  /* 3. Handle children */
  if (GNL_IS_OPERATION (newobj)) {
    guint nbchildren = g_node_n_children (node);
    GnlOperation *oper = (GnlOperation *) newobj;
    GST_LOG_OBJECT (newobj, "is a %s operation, analyzing the %d children",
        oper->dynamicsinks ? "dynamic" : "regular", nbchildren);
    /* Update the operation's number of sinks, that will make it have the proper
     * number of sink pads to connect the children to. */
    if (oper->dynamicsinks)
      g_object_set (G_OBJECT (newobj), "sinks", nbchildren, NULL);
    for (child = node->children; child; child = child->next)
      compare_relink_single_node (comp, child, oldstack);
    if (G_UNLIKELY (nbchildren < oper->num_sinks))
      GST_ERROR
          ("Not enough sinkpads to link all objects to the operation ! %d / %d",
          oper->num_sinks, nbchildren);
    if (G_UNLIKELY (nbchildren == 0))
      GST_ERROR ("Operation has no child objects to be connected to !!!");
    /* Make sure we have enough sinkpads */
  } else {
    /* FIXME : do we need to do something specific for sources ? */
  }

  /* 4. Unblock source pad */
  if (!G_NODE_IS_ROOT (node) && entry->probeid) {
    GST_LOG_OBJECT (comp, "Unblocking pad %s:%s", GST_DEBUG_PAD_NAME (srcpad));
    gst_pad_remove_probe (srcpad, entry->probeid);
    entry->probeid = 0;
  }

  GST_LOG_OBJECT (comp, "done with object %s",
      GST_ELEMENT_NAME (GST_ELEMENT (newobj)));
}

/*
 * recursive depth-first compare stack function on old stack
 *
 * _ Add no-longer used objects to the deactivate list
 * _ unlink child-parent relations that have changed (not same parent, or not same order)
 * _ blocks available source pads
 *
 * FIXME : modify is only used for the root element.
 *    It is TRUE all the time except when the update is done from a seek
 *
 * WITH OBJECTS LOCK TAKEN
 */
static GList *
compare_deactivate_single_node (GnlComposition * comp, GNode * node,
    GNode * newstack, gboolean modify)
{
  GNode *child;
  GNode *newnode = NULL;        /* Same node in newstack */
  GnlObject *oldparent;
  GList *deactivate = NULL;
  GnlObject *oldobj = NULL;
  GstPad *srcpad = NULL;
  GstPad *peerpad = NULL;
  GnlCompositionEntry *entry;

  if (G_UNLIKELY (!node))
    return NULL;

  /* The former parent GnlObject (i.e. downstream) of the given node */
  oldparent = G_NODE_IS_ROOT (node) ? NULL : (GnlObject *) node->parent->data;

  /* The former GnlObject */
  oldobj = (GnlObject *) node->data;

  /* The node corresponding to oldobj in the new stack */
  if (newstack)
    newnode = g_node_find (newstack, G_IN_ORDER, G_TRAVERSE_ALL, oldobj);

  GST_DEBUG_OBJECT (comp, "oldobj:%s",
      GST_ELEMENT_NAME ((GstElement *) oldobj));
  srcpad = GNL_OBJECT_SRC (oldobj);

  entry = COMP_ENTRY (comp, oldobj);

  /* 1. Block source pad
   *   This makes sure that no data/event flow will come out of this element after this
   *   point.
   *
   * If entry is NULL, this means the element is in the process of being removed.
   */
  if (entry && !entry->probeid) {
    GST_LOG_OBJECT (comp, "Setting BLOCKING probe on %s:%s",
        GST_DEBUG_PAD_NAME (srcpad));
    entry->probeid =
        gst_pad_add_probe (srcpad,
        GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_IDLE,
        (GstPadProbeCallback) pad_blocked, comp, NULL);
  }
  if (entry && !entry->dataprobeid) {
    entry->dataprobeid = gst_pad_add_probe (srcpad,
        GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
        GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback) drop_data, entry,
        NULL);
  }

  /* 2. If we have to modify or we have a parent, flush downstream
   *   This ensures the streaming thread going through the current object has
   *   either stopped or is blocking against the source pad. */
  if ((modify || oldparent) && (peerpad = gst_pad_get_peer (srcpad))) {
    GST_LOG_OBJECT (comp, "Sending flush start/stop downstream ");
    gst_pad_send_event (peerpad, gst_event_new_flush_start ());
    gst_pad_send_event (peerpad, gst_event_new_flush_stop (TRUE));
    GST_DEBUG_OBJECT (comp, "DONE Sending flush events downstream");
    gst_object_unref (peerpad);
  }

  /* 3. Unlink from the parent if we've changed position */

  GST_LOG_OBJECT (comp,
      "Checking if we need to unlink from downstream element");
  if (G_UNLIKELY (!oldparent)) {
    GST_LOG_OBJECT (comp, "Top-level object");
    /* for top-level objects we just set the ghostpad target to NULL */
    gnl_composition_ghost_pad_set_target (comp, NULL, NULL);
  } else {
    GnlObject *newparent = NULL;

    GST_LOG_OBJECT (comp, "non-toplevel object");

    if (newnode)
      newparent =
          G_NODE_IS_ROOT (newnode) ? NULL : (GnlObject *) newnode->parent->data;

    if ((!newnode) || (oldparent != newparent) ||
        (newparent &&
            (g_node_child_index (node,
                    oldobj) != g_node_child_index (newnode, oldobj)))) {
      GstPad *peerpad = NULL;

      GST_LOG_OBJECT (comp, "Topology changed, unlinking from downstream");

      if ((peerpad = gst_pad_get_peer (srcpad))) {
        GST_LOG_OBJECT (peerpad, "Sending flush start/stop");
        gst_pad_send_event (peerpad, gst_event_new_flush_start ());
        gst_pad_send_event (peerpad, gst_event_new_flush_stop (TRUE));
        gst_pad_unlink (srcpad, peerpad);
        gst_object_unref (peerpad);
      }
    } else
      GST_LOG_OBJECT (comp, "Topology unchanged");
  }

  /* 4. If we're dealing with an operation, call this method recursively on it */
  if (G_UNLIKELY (GNL_IS_OPERATION (oldobj))) {
    GST_LOG_OBJECT (comp,
        "Object is an operation, recursively calling on children");
    for (child = node->children; child; child = child->next) {
      GList *newdeac =
          compare_deactivate_single_node (comp, child, newstack, modify);

      if (newdeac)
        deactivate = g_list_concat (deactivate, newdeac);
    }
  }

  /* 5. If object isn't used anymore, add it to the list of objects to deactivate */
  if (G_LIKELY (!newnode)) {
    GST_LOG_OBJECT (comp, "Object doesn't exist in new stack");
    deactivate = g_list_prepend (deactivate, oldobj);
  }

  GST_LOG_OBJECT (comp, "done with object %s",
      GST_ELEMENT_NAME (GST_ELEMENT (oldobj)));

  return deactivate;
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
  GList *deactivate = NULL;

  /* 1. Traverse old stack to deactivate no longer used objects */
  deactivate =
      compare_deactivate_single_node (comp, comp->priv->current, stack, modify);

  /* 2. Traverse new stack to do needed (re)links */
  compare_relink_single_node (comp, stack, comp->priv->current);

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
  gboolean ret = TRUE;
  GList *todeactivate = NULL;
  gboolean samestack = FALSE;
  GstState state = GST_STATE (comp);
  GnlCompositionPrivate *priv = comp->priv;
  GstClockTime new_stop = GST_CLOCK_TIME_NONE;
  GstClockTime new_start = GST_CLOCK_TIME_NONE;
  GstState nextstate = (GST_STATE_NEXT (comp) == GST_STATE_VOID_PENDING) ?
      GST_STATE (comp) : GST_STATE_NEXT (comp);

  GST_DEBUG_OBJECT (comp,
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

  /* 1. Get new stack and compare it to current one */
  stack = get_clean_toplevel_stack (comp, &currenttime, &new_start, &new_stop);
  samestack = are_same_stacks (priv->current, stack);

  /* invalidate the stack while modifying it */
  priv->stackvalid = FALSE;

  /* 2. If stacks are different, unlink/relink objects */
  if (!samestack)
    todeactivate = compare_relink_stack (comp, stack, modify);

  if (priv->segment->rate >= 0.0) {
    startchanged = priv->segment_start != currenttime;
    stopchanged = priv->segment_stop != new_stop;
  } else {
    startchanged = priv->segment_start != new_start;
    stopchanged = priv->segment_stop != currenttime;
  }

  /* 3. set new segment_start/stop (the current zone over which the new stack
   *    is valid) */
  if (priv->segment->rate >= 0.0) {
    priv->segment_start = currenttime;
    priv->segment_stop = new_stop;
  } else {
    priv->segment_start = new_start;
    priv->segment_stop = currenttime;
  }

  /* Invalidate current stack */
  if (priv->current)
    g_node_destroy (priv->current);
  priv->current = NULL;

  /* 4. deactivate unused elements */
  if (todeactivate) {
    GList *tmp;
    GstElement *element;

    GST_DEBUG_OBJECT (comp, "De-activating objects no longer used");

    /* state-lock elements no more used */
    for (tmp = todeactivate; tmp; tmp = tmp->next) {
      element = GST_ELEMENT_CAST (tmp->data);

      gst_element_set_state (element, priv->deactivated_elements_state);
      gst_element_set_locked_state (element, TRUE);
    }

    g_list_free (todeactivate);

    GST_DEBUG_OBJECT (comp, "Finished de-activating objects no longer used");
  }

  /* 5. Unlock all elements in new stack */
  GST_DEBUG_OBJECT (comp, "Setting current stack");
  priv->current = stack;

  if (!samestack && stack) {
    GST_DEBUG_OBJECT (comp, "activating objects in new stack to %s",
        gst_element_state_get_name (nextstate));
    unlock_activate_stack (comp, stack, nextstate);
    GST_DEBUG_OBJECT (comp, "Finished activating objects in new stack");
  }

  /* 6. Activate stack (might happen asynchronously) */
  if (priv->current) {
    GstEvent *event;
    GstPad *pad;
    GstElement *topelement;
    GnlCompositionEntry *topentry;

    priv->stackvalid = TRUE;

    /* 6.1. Create new seek event for newly configured timeline stack */
    if (samestack && (startchanged || stopchanged))
      event =
          get_new_seek_event (comp,
          (state == GST_STATE_PLAYING) ? FALSE : TRUE, !startchanged);
    else
      event = get_new_seek_event (comp, initial, FALSE);

    /* 6.2 The stack is entirely ready, send seek out synchronously */
    topelement = GST_ELEMENT (priv->current->data);
    /* Get toplevel object source pad */
    pad = GNL_OBJECT_SRC (topelement);
    topentry = COMP_ENTRY (comp, topelement);

    GST_DEBUG_OBJECT (comp,
        "We have a valid toplevel element pad %s:%s", GST_DEBUG_PAD_NAME (pad));

    /* Send seek event */
    GST_LOG_OBJECT (comp, "sending seek event");
    if (gst_pad_send_event (pad, event)) {
      /* Unconditionnaly set the ghostpad target to pad */
      GST_LOG_OBJECT (comp,
          "Setting the composition's ghostpad target to %s:%s",
          GST_DEBUG_PAD_NAME (pad));

      gnl_composition_ghost_pad_set_target (comp, pad, topentry);

      if (topentry->probeid) {

        /* unblock top-level pad */
        GST_LOG_OBJECT (comp, "About to unblock top-level srcpad");
        gst_pad_remove_probe (pad, topentry->probeid);
        topentry->probeid = 0;
      }
    } else {
      ret = FALSE;
    }
  } else {
    if ((!priv->objects_start)) {
      gnl_composition_reset_target_pad (comp);
      priv->segment_start = 0;
      priv->segment_stop = GST_CLOCK_TIME_NONE;
    }
  }

  GST_DEBUG_OBJECT (comp, "Returning %d", ret);
  return ret;
}

static gboolean
gnl_composition_add_object (GstBin * bin, GstElement * element)
{
  gboolean ret;
  GnlCompositionEntry *entry;
  GnlComposition *comp = (GnlComposition *) bin;
  GnlCompositionPrivate *priv = comp->priv;

  /* we only accept GnlObject */
  g_return_val_if_fail (GNL_IS_OBJECT (element), FALSE);

  GST_DEBUG_OBJECT (bin, "element %s", GST_OBJECT_NAME (element));
  GST_DEBUG_OBJECT (element, "%" GST_TIME_FORMAT "--%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GNL_OBJECT_START (element)),
      GST_TIME_ARGS (GNL_OBJECT_STOP (element)));

  gst_object_ref (element);

  COMP_OBJECTS_LOCK (comp);

  if ((GNL_OBJECT_IS_EXPANDABLE (element)) &&
      g_list_find (priv->expandables, element)) {
    GST_WARNING_OBJECT (comp,
        "We already have an expandable, remove it before adding new one");
    ret = FALSE;

    goto chiringuito;
  }

  /* Call parent class ::add_element() */
  ret = GST_BIN_CLASS (parent_class)->add_element (bin, element);

  gnl_object_set_commit_needed (GNL_OBJECT (comp));

  if (!ret) {
    GST_WARNING_OBJECT (bin, "couldn't add element");
    goto chiringuito;
  }

  /* lock state of child ! */
  GST_LOG_OBJECT (bin, "Locking state of %s", GST_ELEMENT_NAME (element));
  gst_element_set_locked_state (element, TRUE);

  /* wrap new element in a GnlCompositionEntry ... */
  entry = g_slice_new0 (GnlCompositionEntry);
  entry->object = (GnlObject *) element;
  entry->comp = comp;

  if (GNL_OBJECT_IS_EXPANDABLE (element)) {
    /* Only react on non-default objects properties */
    g_object_set (element,
        "start", (GstClockTime) 0,
        "inpoint", (GstClockTime) 0,
        "duration", (GstClockTimeDiff) GNL_OBJECT_STOP (comp), NULL);

    GST_INFO_OBJECT (element, "Used as expandable, commiting now");
    gnl_object_commit (GNL_OBJECT (element), FALSE);
  }

  /* ...and add it to the hash table */
  g_hash_table_insert (priv->objects_hash, element, entry);

  entry->dataprobeid = gst_pad_add_probe (entry->object->srcpad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
      GST_PAD_PROBE_TYPE_EVENT_BOTH, (GstPadProbeCallback) drop_data, entry,
      NULL);

  entry->probeid =
      gst_pad_add_probe (entry->object->srcpad,
      GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_IDLE,
      (GstPadProbeCallback) pad_blocked, comp, NULL);

  /* Set the caps of the composition */
  if (G_UNLIKELY (!gst_caps_is_any (((GnlObject *) comp)->caps)))
    gnl_object_set_caps ((GnlObject *) element, ((GnlObject *) comp)->caps);

  /* Special case for default source. */
  if (GNL_OBJECT_IS_EXPANDABLE (element)) {
    /* It doesn't get added to objects_start and objects_stop. */
    priv->expandables = g_list_prepend (priv->expandables, element);
    goto beach;
  }

  /* add it sorted to the objects list */
  priv->objects_start = g_list_insert_sorted
      (priv->objects_start, element, (GCompareFunc) objects_start_compare);

  if (priv->objects_start)
    GST_LOG_OBJECT (comp,
        "Head of objects_start is now %s [%" GST_TIME_FORMAT "--%"
        GST_TIME_FORMAT "]",
        GST_OBJECT_NAME (priv->objects_start->data),
        GST_TIME_ARGS (GNL_OBJECT_START (priv->objects_start->data)),
        GST_TIME_ARGS (GNL_OBJECT_STOP (priv->objects_start->data)));

  priv->objects_stop = g_list_insert_sorted
      (priv->objects_stop, element, (GCompareFunc) objects_stop_compare);

  /* Now the object is ready to be commited and then used */

beach:
  COMP_OBJECTS_UNLOCK (comp);

  gst_object_unref (element);
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
  GnlCompositionPrivate *priv = comp->priv;
  gboolean ret = FALSE;
  gboolean update_required;
  GnlCompositionEntry *entry;

  GST_DEBUG_OBJECT (bin, "element %s", GST_OBJECT_NAME (element));

  /* we only accept GnlObject */
  g_return_val_if_fail (GNL_IS_OBJECT (element), FALSE);
  COMP_OBJECTS_LOCK (comp);
  entry = COMP_ENTRY (comp, element);
  if (entry == NULL) {
    COMP_OBJECTS_UNLOCK (comp);
    goto out;
  }

  gst_object_ref (element);
  gst_element_set_locked_state (element, FALSE);

  /* handle default source */
  if (GNL_OBJECT_IS_EXPANDABLE (element)) {
    /* Find it in the list */
    priv->expandables = g_list_remove (priv->expandables, element);
  } else {
    /* remove it from the objects list and resort the lists */
    priv->objects_start = g_list_remove (priv->objects_start, element);
    priv->objects_stop = g_list_remove (priv->objects_stop, element);
    GST_LOG_OBJECT (element, "Removed from the objects start/stop list");
  }

  g_hash_table_remove (priv->objects_hash, element);
  update_required = OBJECT_IN_ACTIVE_SEGMENT (comp, element) ||
      (GNL_OBJECT_PRIORITY (element) == G_MAXUINT32) ||
      GNL_OBJECT_IS_EXPANDABLE (element);

  if (G_LIKELY (update_required)) {
    /* And update the pipeline at current position if needed */
    update_pipeline_at_current_position (comp);
  } else
    update_start_stop_duration (comp);

  ret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);
  GST_LOG_OBJECT (element, "Done removing from the composition, now updating");
  COMP_OBJECTS_UNLOCK (comp);


  /* Make it possible to reuse the same object later */
  gnl_object_reset (GNL_OBJECT (element));
  gst_object_unref (element);

out:
  return ret;
}
