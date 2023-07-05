/* GStreamer
 * Copyright (C) 2016 Collabora Ltd. <guillaume.desmottes@collabora.co.uk>
 * Copyright (C) 2019 Nirbheek Chauhan <nirbheek@centricular.com>
 *
 * gstleaks.c: tracing module detecting object leaks
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
/**
 * SECTION:tracer-leaks
 * @short_description: detect GstObject and GstMiniObject leaks
 *
 * This tracing module tracks the lifetimes of #GstObject and #GstMiniObject
 * objects and prints a list of leaks to the debug log under `GST_TRACER:7` when
 * gst_deinit() is called, and also prints a g_warning().
 *
 * Starting with GStreamer 1.18, you can also use GObject action signals on the tracer
 * object to fetch leak information. Use gst_tracing_get_active_tracers() to
 * get a list of all active tracers and find the right one by name.
 *
 * If the `GST_LEAKS_TRACER_SIG` env variable is defined, you can use the
 * following POSIX signals to interact with the leaks tracer:
 * - SIGUSR1: log alive objects
 * - SIGUSR2: create a checkpoint and print a list of objects created and
 *   destroyed since the previous checkpoint.
 *
 * You can activate this tracer in the usual way by adding the string 'leaks'
 * to the environment variable `GST_TRACERS`. Such as: `GST_TRACERS=leaks`
 *
 * Note that the values are separated by semicolon (`;`), such as:
 * `GST_TRACERS=leaks;latency`, and multiple instances of the same tracer can be
 * active at the same time.
 *
 * Parameters can also be passed to each tracer. The leaks tracer currently
 * accepts five params:
 * 1. filters: (string) to filter which objects to record
 * 2. check-refs: (boolean) whether to record every location where a leaked
 *    object was reffed and unreffed
 * 3. stack-traces-flags: (string) full or none; see: #GstStackTraceFlags
 * 4. name: (string) set a name for the tracer object itself
 * 5. log-leaks-on-deinit: (boolean) whether to report all leaks on
 *    gst_deinit() by printing them in the debug log; "true" by default
 *
 * Examples:
 * ```
 * GST_TRACERS='leaks(filters="GstEvent,GstMessage",stack-traces-flags=none)'
 * ```
 * ```
 * GST_TRACERS='leaks(filters="GstBuffer",stack-traces-flags=full,check-refs=true);leaks(name=all-leaks)'
 * ```
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstleaks.h"

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <pthread.h>
#endif /* G_OS_UNIX */

GST_DEBUG_CATEGORY_STATIC (gst_leaks_debug);
#define GST_CAT_DEFAULT gst_leaks_debug

enum
{
  /* actions */
  SIGNAL_GET_LIVE_OBJECTS,
  SIGNAL_LOG_LIVE_OBJECTS,
  SIGNAL_ACTIVITY_START_TRACKING,
  SIGNAL_ACTIVITY_GET_CHECKPOINT,
  SIGNAL_ACTIVITY_LOG_CHECKPOINT,
  SIGNAL_ACTIVITY_STOP_TRACKING,

  LAST_SIGNAL
};

#define DEFAULT_LOG_LEAKS TRUE  /* for backwards-compat */

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_leaks_debug, "leaks", 0, "leaks tracer");
#define gst_leaks_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLeaksTracer, gst_leaks_tracer,
    GST_TYPE_TRACER, _do_init);

static GstStructure *gst_leaks_tracer_get_live_objects (GstLeaksTracer * self);
static void gst_leaks_tracer_log_live_objects (GstLeaksTracer * self);
static void gst_leaks_tracer_activity_start_tracking (GstLeaksTracer * self);
static GstStructure *gst_leaks_tracer_activity_get_checkpoint (GstLeaksTracer *
    self);
static void gst_leaks_tracer_activity_log_checkpoint (GstLeaksTracer * self);
static void gst_leaks_tracer_activity_stop_tracking (GstLeaksTracer * self);

#ifdef G_OS_UNIX
static void gst_leaks_tracer_setup_signals (GstLeaksTracer * leaks);
static void gst_leaks_tracer_cleanup_signals (GstLeaksTracer * leaks);
#endif

static GstTracerRecord *tr_alive;
static GstTracerRecord *tr_refings;
static GstTracerRecord *tr_added = NULL;
static GstTracerRecord *tr_removed = NULL;
static GQueue instances = G_QUEUE_INIT;
static guint gst_leaks_tracer_signals[LAST_SIGNAL] = { 0 };

G_LOCK_DEFINE_STATIC (instances);

typedef enum
{
  GOBJECT,
  MINI_OBJECT,
} ObjectKind;

typedef struct
{
  gboolean reffed;
  gchar *trace;
  gint new_refcount;
  GstClockTime ts;
} ObjectRefingInfo;

typedef struct
{
  gchar *creation_trace;
  ObjectKind kind;
  GList *refing_infos;
} ObjectRefingInfos;

static void
object_refing_info_free (ObjectRefingInfo * refinfo)
{
  g_free (refinfo->trace);
  g_free (refinfo);
}

static void
object_refing_infos_free (ObjectRefingInfos * infos)
{
  g_list_free_full (infos->refing_infos,
      (GDestroyNotify) object_refing_info_free);

  g_free (infos->creation_trace);
  g_free (infos);
}

static void
set_print_stack_trace_from_string (GstLeaksTracer * self, const gchar * str)
{
  gchar *trace;

  /* Test if we can retrieve backtrace */
  trace = gst_debug_get_stack_trace (FALSE);
  if (!trace)
    return;

  g_free (trace);

  if (g_strcmp0 (str, "full") == 0)
    self->trace_flags = GST_STACK_TRACE_SHOW_FULL;
  else
    self->trace_flags = GST_STACK_TRACE_SHOW_NONE;
}

static void
set_print_stack_trace (GstLeaksTracer * self, GstStructure * params)
{
  const gchar *trace_flags = g_getenv ("GST_LEAKS_TRACER_STACK_TRACE");

  self->trace_flags = -1;
  if (!trace_flags && params)
    trace_flags = gst_structure_get_string (params, "stack-traces-flags");

  if (!trace_flags)
    return;

  set_print_stack_trace_from_string (self, trace_flags);
}

static void
set_filters (GstLeaksTracer * self, const gchar * filters)
{
  guint i;
  GStrv tmp = g_strsplit (filters, ",", -1);

  self->filter = g_array_sized_new (FALSE, FALSE, sizeof (GType),
      g_strv_length (tmp));
  for (i = 0; tmp[i]; i++) {
    GType type;

    type = g_type_from_name (tmp[i]);
    if (type == 0) {
      /* The type may not yet be known by the type system, typically because
       * the plugin implementing it as not yet be loaded. Save it for now as
       * it will have another chance to be added to the filter later in
       * should_handle_object_type() when/if the object type is actually
       * used. */
      if (!self->unhandled_filter)
        self->unhandled_filter = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, NULL);

      g_hash_table_add (self->unhandled_filter, g_strdup (tmp[i]));
      g_atomic_int_inc (&self->unhandled_filter_count);
      continue;
    }

    GST_DEBUG_OBJECT (self, "add filter on %s", tmp[i]);

    g_array_append_val (self->filter, type);
  }

  g_strfreev (tmp);
}

static void
set_params_from_structure (GstLeaksTracer * self, GstStructure * params)
{
  const gchar *filters, *name;

  filters = gst_structure_get_string (params, "filters");
  if (filters)
    set_filters (self, filters);

  name = gst_structure_get_string (params, "name");
  if (name)
    gst_object_set_name (GST_OBJECT (self), name);

  gst_structure_get_boolean (params, "check-refs", &self->check_refs);
  gst_structure_get_boolean (params, "log-leaks-on-deinit", &self->log_leaks);
}

static void
set_params (GstLeaksTracer * self)
{
  gchar *params, *tmp;
  GstStructure *params_struct = NULL;

  g_object_get (self, "params", &params, NULL);
  if (!params)
    goto set_stacktrace;

  tmp = g_strdup_printf ("leaks,%s", params);
  params_struct = gst_structure_from_string (tmp, NULL);
  g_free (tmp);

  if (params_struct)
    set_params_from_structure (self, params_struct);
  else
    set_filters (self, params);

  g_free (params);

set_stacktrace:
  set_print_stack_trace (self, params_struct);

  if (params_struct)
    gst_structure_free (params_struct);
}

static gboolean
_expand_unhandled_filters (gchar * typename, gpointer unused_value,
    GstLeaksTracer * self)
{
  GType type;

  type = g_type_from_name (typename);

  if (type == 0)
    return FALSE;

  g_atomic_int_dec_and_test (&self->unhandled_filter_count);
  g_array_append_val (self->filter, type);

  return TRUE;
}

static gboolean
should_handle_object_type (GstLeaksTracer * self, GType object_type)
{
  guint i, len;

  if (!self->filter)
    /* No filtering, handle all types */
    return TRUE;

  if (object_type == 0)
    return FALSE;


  if (g_atomic_int_get (&self->unhandled_filter_count)) {
    GST_OBJECT_LOCK (self);
    g_hash_table_foreach_remove (self->unhandled_filter,
        (GHRFunc) _expand_unhandled_filters, self);
    GST_OBJECT_UNLOCK (self);
  }

  len = self->filter->len;
  for (i = 0; i < len; i++) {
    GType type = g_array_index (self->filter, GType, i);

    if (g_type_is_a (object_type, type))
      return TRUE;
  }

  return FALSE;
}

/* The object may be destroyed when we log it using the checkpointing system so
 * we have to save its type name */
typedef struct
{
  gpointer object;
  GQuark type_qname;
} ObjectLog;

static ObjectLog *
object_log_new (gpointer obj, ObjectKind kind)
{
  ObjectLog *o = g_new (ObjectLog, 1);

  o->object = obj;

  switch (kind) {
    case GOBJECT:
      o->type_qname = g_type_qname (G_OBJECT_TYPE (obj));
      break;
    case MINI_OBJECT:
      o->type_qname = g_type_qname (GST_MINI_OBJECT_TYPE (obj));
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return o;
}

static void
object_log_free (ObjectLog * obj)
{
  g_free (obj);
}

static void
handle_object_destroyed (GstLeaksTracer * self, gpointer object,
    ObjectKind kind)
{
  GST_OBJECT_LOCK (self);
  if (self->done) {
    g_warning
        ("object %p destroyed while the leaks tracer was finalizing. Some threads are still running?",
        object);
    goto out;
  }

  g_hash_table_remove (self->objects, object);
  if (self->removed)
    g_hash_table_add (self->removed, object_log_new (object, kind));
out:
  GST_OBJECT_UNLOCK (self);
}

static void
object_weak_cb (gpointer data, GObject * object)
{
  GstLeaksTracer *self = data;

  handle_object_destroyed (self, object, GOBJECT);
}

static void
mini_object_weak_cb (gpointer data, GstMiniObject * object)
{
  GstLeaksTracer *self = data;

  handle_object_destroyed (self, object, MINI_OBJECT);
}

static void
handle_object_created (GstLeaksTracer * self, gpointer object, GType type,
    ObjectKind kind)
{
  ObjectRefingInfos *infos;

  if (!should_handle_object_type (self, type))
    return;

  infos = g_malloc0 (sizeof (ObjectRefingInfos));
  infos->kind = kind;
  switch (kind) {
    case GOBJECT:
      g_object_weak_ref ((GObject *) object, object_weak_cb, self);
      break;
    case MINI_OBJECT:
      gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (object),
          mini_object_weak_cb, self);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  GST_OBJECT_LOCK (self);
  if ((gint) self->trace_flags != -1)
    infos->creation_trace = gst_debug_get_stack_trace (self->trace_flags);

  g_hash_table_insert (self->objects, object, infos);

  if (self->added)
    g_hash_table_add (self->added, object_log_new (object, kind));
  GST_OBJECT_UNLOCK (self);
}

static void
mini_object_created_cb (GstTracer * tracer, GstClockTime ts,
    GstMiniObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_created (self, object, GST_MINI_OBJECT_TYPE (object),
      MINI_OBJECT);
}

static void
object_created_cb (GstTracer * tracer, GstClockTime ts, GstObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);
  GType object_type = G_OBJECT_TYPE (object);

  /* Can't track tracers as they may be disposed after the leak tracer itself */
  if (g_type_is_a (object_type, GST_TYPE_TRACER))
    return;

  handle_object_created (self, object, object_type, GOBJECT);
}

static void
handle_object_reffed (GstLeaksTracer * self, gpointer object, GType type,
    gint new_refcount, gboolean reffed, GstClockTime ts)
{
  ObjectRefingInfos *infos;
  ObjectRefingInfo *refinfo;

  if (!self->check_refs)
    return;

  if (!should_handle_object_type (self, type))
    return;

  GST_OBJECT_LOCK (self);
  infos = g_hash_table_lookup (self->objects, object);
  if (!infos)
    goto out;

  refinfo = g_malloc0 (sizeof (ObjectRefingInfo));
  refinfo->ts = ts;
  refinfo->new_refcount = new_refcount;
  refinfo->reffed = reffed;
  if ((gint) self->trace_flags != -1)
    refinfo->trace = gst_debug_get_stack_trace (self->trace_flags);

  infos->refing_infos = g_list_prepend (infos->refing_infos, refinfo);

out:
  GST_OBJECT_UNLOCK (self);
}

static void
object_reffed_cb (GstTracer * tracer, GstClockTime ts, GstObject * object,
    gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, G_OBJECT_TYPE (object), new_refcount,
      TRUE, ts);
}

static void
object_unreffed_cb (GstTracer * tracer, GstClockTime ts, GstObject * object,
    gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, G_OBJECT_TYPE (object), new_refcount,
      FALSE, ts);
}

static void
mini_object_reffed_cb (GstTracer * tracer, GstClockTime ts,
    GstMiniObject * object, gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, GST_MINI_OBJECT_TYPE (object),
      new_refcount, TRUE, ts);
}

static void
mini_object_unreffed_cb (GstTracer * tracer, GstClockTime ts,
    GstMiniObject * object, gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, GST_MINI_OBJECT_TYPE (object),
      new_refcount, FALSE, ts);
}

static void
gst_leaks_tracer_init (GstLeaksTracer * self)
{
  self->log_leaks = DEFAULT_LOG_LEAKS;
  self->objects = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) object_refing_infos_free);

  if (g_getenv ("GST_LEAKS_TRACER_SIG")) {
#ifdef G_OS_UNIX
    gst_leaks_tracer_setup_signals (self);
#else
    g_warning ("System doesn't support POSIX signals");
#endif /* G_OS_UNIX */
  }

  G_LOCK (instances);
  g_queue_push_tail (&instances, self);
  G_UNLOCK (instances);
}

static void
gst_leaks_tracer_constructed (GObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER (object);
  GstTracer *tracer = GST_TRACER (object);

  set_params (self);

  gst_tracing_register_hook (tracer, "mini-object-created",
      G_CALLBACK (mini_object_created_cb));
  gst_tracing_register_hook (tracer, "object-created",
      G_CALLBACK (object_created_cb));

  if (self->check_refs) {
    gst_tracing_register_hook (tracer, "object-reffed",
        G_CALLBACK (object_reffed_cb));
    gst_tracing_register_hook (tracer, "mini-object-reffed",
        G_CALLBACK (mini_object_reffed_cb));
    gst_tracing_register_hook (tracer, "mini-object-unreffed",
        G_CALLBACK (mini_object_unreffed_cb));
    gst_tracing_register_hook (tracer, "object-unreffed",
        G_CALLBACK (object_unreffed_cb));
  }

  /* We rely on weak pointers rather than (mini-)object-destroyed hooks so we
   * are notified of objects being destroyed even during the shuting down of
   * the tracing system. */

  ((GObjectClass *) gst_leaks_tracer_parent_class)->constructed (object);
}

typedef struct
{
  gpointer obj;
  GType type;
  guint ref_count;
  gchar *desc;
  ObjectRefingInfos *infos;
} Leak;

/* The content of the returned Leak struct is valid until the self->objects
 * hash table has been modified. */
static Leak *
leak_new (gpointer obj, GType type, guint ref_count, ObjectRefingInfos * infos)
{
  Leak *leak = g_new (Leak, 1);

  leak->obj = obj;
  leak->type = type;
  leak->ref_count = ref_count;
  leak->desc = gst_info_strdup_printf ("%" GST_PTR_FORMAT, obj);
  leak->infos = infos;

  return leak;
}

static void
leak_free (Leak * leak)
{
  g_free (leak->desc);
  g_free (leak);
}

static gint
sort_leaks (gconstpointer _a, gconstpointer _b)
{
  const Leak *a = _a, *b = _b;

  return g_strcmp0 (g_type_name (a->type), g_type_name (b->type));
}

static GList *
create_leaks_list (GstLeaksTracer * self)
{
  GList *l = NULL;
  GHashTableIter iter;
  gpointer obj, infos;

  g_hash_table_iter_init (&iter, self->objects);
  while (g_hash_table_iter_next (&iter, &obj, &infos)) {
    GType type;
    guint ref_count;

    switch (((ObjectRefingInfos *) infos)->kind) {
      case GOBJECT:
        if (GST_OBJECT_FLAG_IS_SET (obj, GST_OBJECT_FLAG_MAY_BE_LEAKED))
          continue;

        type = G_OBJECT_TYPE (obj);
        ref_count = ((GObject *) obj)->ref_count;
        break;
      case MINI_OBJECT:
        if (GST_MINI_OBJECT_FLAG_IS_SET (obj,
                GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED))
          continue;

        type = GST_MINI_OBJECT_TYPE (obj);
        ref_count = ((GstMiniObject *) obj)->refcount;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    l = g_list_prepend (l, leak_new (obj, type, ref_count, infos));
  }

  /* Sort leaks by type name so they are grouped together making the output
   * easier to read */
  l = g_list_sort (l, sort_leaks);

  /* Reverse list to sort objects by creation time; this is needed because we
   * prepended objects into this list earlier, and because g_list_sort() above
   * is stable so the creation order is preserved when sorting by type name. */
  return g_list_reverse (l);
}

static void
process_leak (Leak * leak, GValue * ret_leaks)
{
  GstStructure *r, *s = NULL;
  GList *ref;
  GValue refings = G_VALUE_INIT;

  if (!ret_leaks) {
    /* log to the debug log */
    gst_tracer_record_log (tr_alive, g_type_name (leak->type), leak->obj,
        leak->desc, leak->ref_count,
        leak->infos->creation_trace ? leak->infos->creation_trace : "");
  } else {
    GValue s_value = G_VALUE_INIT;
    GValue obj_value = G_VALUE_INIT;
    /* for leaked objects, we take ownership of the object instead of
     * reffing ("collecting") it to avoid deadlocks */
    g_value_init (&obj_value, leak->type);
    switch (leak->infos->kind) {
      case GOBJECT:
        g_value_take_object (&obj_value, leak->obj);
        break;
      case MINI_OBJECT:
        g_value_take_boxed (&obj_value, leak->obj);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
    s = gst_structure_new_empty ("object-alive");
    gst_structure_take_value (s, "object", &obj_value);
    gst_structure_set (s, "ref-count", G_TYPE_UINT, leak->ref_count,
        "trace", G_TYPE_STRING, leak->infos->creation_trace, NULL);
    /* avoid copy of structure */
    g_value_init (&s_value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&s_value, s);
    gst_value_list_append_and_take_value (ret_leaks, &s_value);
  }

  /* store refinfo if available */
  if (leak->infos->refing_infos)
    g_value_init (&refings, GST_TYPE_LIST);

  /* iterate the list from last to first to correct the order */
  for (ref = g_list_last (leak->infos->refing_infos); ref; ref = ref->prev) {
    ObjectRefingInfo *refinfo = (ObjectRefingInfo *) ref->data;

    if (!ret_leaks) {
      /* log to the debug log */
      gst_tracer_record_log (tr_refings, refinfo->ts, g_type_name (leak->type),
          leak->obj, refinfo->reffed ? "reffed" : "unreffed",
          refinfo->new_refcount, refinfo->trace ? refinfo->trace : "");
    } else {
      GValue r_value = G_VALUE_INIT;
      r = gst_structure_new_empty ("object-refings");
      gst_structure_set (r, "ts", GST_TYPE_CLOCK_TIME, refinfo->ts,
          "desc", G_TYPE_STRING, refinfo->reffed ? "reffed" : "unreffed",
          "ref-count", G_TYPE_UINT, refinfo->new_refcount,
          "trace", G_TYPE_STRING, refinfo->trace, NULL);
      /* avoid copy of structure */
      g_value_init (&r_value, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&r_value, r);
      gst_value_list_append_and_take_value (&refings, &r_value);
    }
  }

  if (ret_leaks && leak->infos->refing_infos)
    gst_structure_take_value (s, "ref-infos", &refings);
}

/* Return TRUE if at least one leaked object was found */
static gboolean
process_leaks (GstLeaksTracer * self, GValue * ret_leaks)
{
  GList *leaks, *l;
  gboolean ret = FALSE;
  guint n = 0;

  if (!ret_leaks)
    GST_TRACE_OBJECT (self, "start listing currently alive objects");

  leaks = create_leaks_list (self);
  if (!leaks) {
    if (!ret_leaks)
      GST_TRACE_OBJECT (self, "No objects alive currently");
    goto done;
  }

  for (l = leaks; l; l = l->next) {
    process_leak (l->data, ret_leaks);
    n++;
  }

  g_list_free_full (leaks, (GDestroyNotify) leak_free);

  ret = TRUE;

done:
  if (!ret_leaks)
    GST_TRACE_OBJECT (self, "listed %u alive objects", n);

  return ret;
}

static void
gst_leaks_tracer_finalize (GObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER (object);
  gboolean leaks = FALSE;
  GHashTableIter iter;
  gpointer obj, infos;

  GST_DEBUG_OBJECT (self, "destroying tracer, checking for leaks");

  self->done = TRUE;

  /* Tracers are destroyed as part of gst_deinit() so now is a good time to
   * report all the objects which are still alive. */
  if (self->log_leaks)
    leaks = process_leaks (self, NULL);

  /* Remove weak references */
  g_hash_table_iter_init (&iter, self->objects);
  while (g_hash_table_iter_next (&iter, &obj, &infos)) {
    switch (((ObjectRefingInfos *) infos)->kind) {
      case GOBJECT:
        g_object_weak_unref (obj, object_weak_cb, self);
        break;
      case MINI_OBJECT:
        gst_mini_object_weak_unref (GST_MINI_OBJECT_CAST (obj),
            mini_object_weak_cb, self);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  g_clear_pointer (&self->objects, g_hash_table_unref);
  if (self->filter)
    g_array_free (self->filter, TRUE);
  g_clear_pointer (&self->added, g_hash_table_unref);
  g_clear_pointer (&self->removed, g_hash_table_unref);
  g_clear_pointer (&self->unhandled_filter, g_hash_table_unref);

  G_LOCK (instances);
  g_queue_remove (&instances, self);
  G_UNLOCK (instances);

#ifdef G_OS_UNIX
  gst_leaks_tracer_cleanup_signals (self);
#endif

  if (leaks)
    g_warning ("Leaks detected and logged under GST_DEBUG=GST_TRACER:7");

  ((GObjectClass *) gst_leaks_tracer_parent_class)->finalize (object);
}

#define RECORD_FIELD_TYPE_TS \
    "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, GST_TYPE_CLOCK_TIME, \
        NULL)
#define RECORD_FIELD_TYPE_NAME \
    "type-name", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_STRING, \
        NULL)
#define RECORD_FIELD_ADDRESS \
    "address", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_POINTER, \
        NULL)
#define RECORD_FIELD_DESC \
    "description", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_STRING, \
        NULL)
#define RECORD_FIELD_REF_COUNT \
    "ref-count", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_UINT, \
        NULL)
#define RECORD_FIELD_TRACE \
    "trace", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_STRING, \
        NULL)

#ifdef G_OS_UNIX
static gboolean
sig_usr1_handler (gpointer data)
{
  G_LOCK (instances);
  g_queue_foreach (&instances, (GFunc) gst_leaks_tracer_log_live_objects, NULL);
  G_UNLOCK (instances);

  return G_SOURCE_CONTINUE;
}

static void
sig_usr2_handler_foreach (gpointer data, gpointer user_data)
{
  GstLeaksTracer *tracer = data;

  if (!tracer->added) {
    GST_TRACE_OBJECT (tracer, "First checkpoint, start tracking objects");
    gst_leaks_tracer_activity_start_tracking (tracer);
  } else {
    gst_leaks_tracer_activity_log_checkpoint (tracer);
  }
}

static gboolean
sig_usr2_handler (gpointer data)
{
  G_LOCK (instances);
  g_queue_foreach (&instances, sig_usr2_handler_foreach, NULL);
  G_UNLOCK (instances);

  return G_SOURCE_CONTINUE;
}

struct signal_thread_data
{
  GMutex lock;
  GCond cond;
  gboolean ready;
};

static GMainLoop *signal_loop;  /* NULL */
static GThread *signal_thread;  /* NULL */
static gint signal_thread_users;        /* 0 */
G_LOCK_DEFINE_STATIC (signal_thread);

static gboolean
unlock_mutex (gpointer data)
{
  g_mutex_unlock ((GMutex *) data);

  return G_SOURCE_REMOVE;
}

static gpointer
gst_leaks_tracer_signal_thread (struct signal_thread_data *data)
{
  static GMainContext *signal_ctx;
  GSource *source1, *source2, *unlock_source;

  signal_ctx = g_main_context_new ();
  signal_loop = g_main_loop_new (signal_ctx, FALSE);

  unlock_source = g_idle_source_new ();
  g_source_set_callback (unlock_source, unlock_mutex, &data->lock, NULL);
  g_source_attach (unlock_source, signal_ctx);

  source1 = g_unix_signal_source_new (SIGUSR1);
  g_source_set_callback (source1, sig_usr1_handler, NULL, NULL);
  g_source_attach (source1, signal_ctx);

  source2 = g_unix_signal_source_new (SIGUSR2);
  g_source_set_callback (source2, sig_usr2_handler, NULL, NULL);
  g_source_attach (source2, signal_ctx);

  g_mutex_lock (&data->lock);
  data->ready = TRUE;
  g_cond_broadcast (&data->cond);

  g_main_loop_run (signal_loop);

  g_source_destroy (source1);
  g_source_destroy (source2);
  g_main_loop_unref (signal_loop);
  signal_loop = NULL;
  g_main_context_unref (signal_ctx);
  signal_ctx = NULL;

  return NULL;
}

static void
atfork_prepare (void)
{
  G_LOCK (signal_thread);
}

static void
atfork_parent (void)
{
  G_UNLOCK (signal_thread);
}

static void
atfork_child (void)
{
  signal_thread_users = 0;
  signal_thread = NULL;
  G_UNLOCK (signal_thread);
}

static void
gst_leaks_tracer_setup_signals (GstLeaksTracer * leaks)
{
  struct signal_thread_data data;

  G_LOCK (signal_thread);
  signal_thread_users++;
  if (signal_thread_users == 1) {
    gint res;

    GST_INFO_OBJECT (leaks, "Setting up signal handling");

    /* If application is forked, the child process won't inherit the extra thread.
     * As a result we need to reset the child process thread state accordingly.
     * This is typically needed when running tests as libcheck fork the tests.
     *
     * See https://pubs.opengroup.org/onlinepubs/007904975/functions/pthread_atfork.html
     * for details. */
    res = pthread_atfork (atfork_prepare, atfork_parent, atfork_child);
    if (res != 0) {
      GST_WARNING_OBJECT (leaks, "pthread_atfork() failed (%d)", res);
    }

    data.ready = FALSE;
    g_mutex_init (&data.lock);
    g_cond_init (&data.cond);
    signal_thread = g_thread_new ("gstleak-signal",
        (GThreadFunc) gst_leaks_tracer_signal_thread, &data);

    g_mutex_lock (&data.lock);
    while (!data.ready)
      g_cond_wait (&data.cond, &data.lock);
    g_mutex_unlock (&data.lock);

    g_mutex_clear (&data.lock);
    g_cond_clear (&data.cond);
  }
  G_UNLOCK (signal_thread);
}

static void
gst_leaks_tracer_cleanup_signals (GstLeaksTracer * leaks)
{
  G_LOCK (signal_thread);
  signal_thread_users--;
  if (signal_thread_users == 0) {
    GST_INFO_OBJECT (leaks, "Cleaning up signal handling");
    g_main_loop_quit (signal_loop);
    g_thread_join (signal_thread);
    signal_thread = NULL;
    gst_object_unref (tr_added);
    tr_added = NULL;
    gst_object_unref (tr_removed);
    tr_removed = NULL;
  }
  G_UNLOCK (signal_thread);
}

#else
#define setup_signals() g_warning ("System doesn't support POSIX signals");
#endif /* G_OS_UNIX */

static GstStructure *
gst_leaks_tracer_get_live_objects (GstLeaksTracer * self)
{
  GstStructure *info;
  GValue live_objects = G_VALUE_INIT;

  g_value_init (&live_objects, GST_TYPE_LIST);

  GST_OBJECT_LOCK (self);
  process_leaks (self, &live_objects);
  GST_OBJECT_UNLOCK (self);

  info = gst_structure_new_empty ("live-objects-info");
  gst_structure_take_value (info, "live-objects-list", &live_objects);

  return info;
}

static void
gst_leaks_tracer_log_live_objects (GstLeaksTracer * self)
{
  GST_OBJECT_LOCK (self);
  process_leaks (self, NULL);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_leaks_tracer_activity_start_tracking (GstLeaksTracer * self)
{
  GST_OBJECT_LOCK (self);
  if (self->added) {
    GST_ERROR_OBJECT (self, "tracking is already in progress");
    return;
  }

  self->added = g_hash_table_new_full (NULL, NULL,
      (GDestroyNotify) object_log_free, NULL);
  self->removed = g_hash_table_new_full (NULL, NULL,
      (GDestroyNotify) object_log_free, NULL);
  GST_OBJECT_UNLOCK (self);
}

/* When @ret is %NULL, this simply logs the activities */
static void
process_checkpoint (GstTracerRecord * record, const gchar * record_type,
    GHashTable * hash, GValue * ret)
{
  GHashTableIter iter;
  gpointer o;

  g_hash_table_iter_init (&iter, hash);
  while (g_hash_table_iter_next (&iter, &o, NULL)) {
    ObjectLog *obj = o;

    const gchar *type_name = g_quark_to_string (obj->type_qname);

    if (!ret) {
      /* log to the debug log */
      gst_tracer_record_log (record, type_name, obj->object);
    } else {
      GValue s_value = G_VALUE_INIT;
      GValue addr_value = G_VALUE_INIT;
      gchar *address = g_strdup_printf ("%p", obj->object);
      GstStructure *s = gst_structure_new_empty (record_type);
      /* copy type_name because it's owned by @obj */
      gst_structure_set (s, "type-name", G_TYPE_STRING, type_name, NULL);
      /* avoid copy of @address */
      g_value_init (&addr_value, G_TYPE_STRING);
      g_value_take_string (&addr_value, address);
      gst_structure_take_value (s, "address", &addr_value);
      /* avoid copy of the structure */
      g_value_init (&s_value, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&s_value, s);
      gst_value_list_append_and_take_value (ret, &s_value);
    }
  }
}

static GstStructure *
gst_leaks_tracer_activity_get_checkpoint (GstLeaksTracer * self)
{
  GValue added = G_VALUE_INIT;
  GValue removed = G_VALUE_INIT;
  GstStructure *s = gst_structure_new_empty ("activity-checkpoint");

  g_value_init (&added, GST_TYPE_LIST);
  g_value_init (&removed, GST_TYPE_LIST);

  GST_OBJECT_LOCK (self);
  process_checkpoint (tr_added, "objects-created", self->added, &added);
  process_checkpoint (tr_removed, "objects-removed", self->removed, &removed);

  g_hash_table_remove_all (self->added);
  g_hash_table_remove_all (self->removed);
  GST_OBJECT_UNLOCK (self);

  gst_structure_take_value (s, "objects-created-list", &added);
  gst_structure_take_value (s, "objects-removed-list", &removed);

  return s;
}

static void
gst_leaks_tracer_activity_log_checkpoint (GstLeaksTracer * self)
{
  GST_OBJECT_LOCK (self);
  GST_TRACE_OBJECT (self, "listing objects created since last checkpoint");
  process_checkpoint (tr_added, NULL, self->added, NULL);
  GST_TRACE_OBJECT (self, "listing objects removed since last checkpoint");
  process_checkpoint (tr_removed, NULL, self->removed, NULL);
  g_hash_table_remove_all (self->added);
  g_hash_table_remove_all (self->removed);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_leaks_tracer_activity_stop_tracking (GstLeaksTracer * self)
{
  GST_OBJECT_LOCK (self);
  g_clear_pointer (&self->added, g_hash_table_destroy);
  g_clear_pointer (&self->removed, g_hash_table_destroy);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_leaks_tracer_class_init (GstLeaksTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_leaks_tracer_constructed;
  gobject_class->finalize = gst_leaks_tracer_finalize;

  tr_alive = gst_tracer_record_new ("object-alive.class",
      RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS, RECORD_FIELD_DESC,
      RECORD_FIELD_REF_COUNT, RECORD_FIELD_TRACE, NULL);
  GST_OBJECT_FLAG_SET (tr_alive, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  tr_refings = gst_tracer_record_new ("object-refings.class",
      RECORD_FIELD_TYPE_TS, RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS,
      RECORD_FIELD_DESC, RECORD_FIELD_REF_COUNT, RECORD_FIELD_TRACE, NULL);
  GST_OBJECT_FLAG_SET (tr_refings, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  tr_added = gst_tracer_record_new ("object-added.class",
      RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS, NULL);
  GST_OBJECT_FLAG_SET (tr_added, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  tr_removed = gst_tracer_record_new ("object-removed.class",
      RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS, NULL);
  GST_OBJECT_FLAG_SET (tr_removed, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  /**
   * GstLeaksTracer::get-live-objects:
   * @leakstracer: the leaks tracer object to emit this signal on
   *
   * Returns a #GstStructure containing a #GValue of type #GST_TYPE_LIST which
   * is a list of #GstStructure objects containing information about the
   * objects that are still alive, which is useful for detecting leaks. Each
   * #GstStructure object has the following fields:
   *
   * `object`: containing the leaked object itself
   * `ref-count`: the current reference count of the object
   * `trace`: the allocation stack trace for the object, only available if the
   *          `stack-traces-flags` param is set to `full`
   * `ref-infos`: a #GValue of type #GST_TYPE_LIST which is a list of
   *             #GstStructure objects containing information about the
   *             ref/unref history of the object; only available if the
   *             `check-refs` param is set to `true`
   *
   * Each `ref-infos` #GstStructure has the following fields:
   *
   * `ts`: the timestamp for the ref/unref
   * `desc`: either "reffed" or "unreffed"
   * `ref-count`: the reference count after the ref/unref
   * `trace`: the stack trace for the ref/unref
   *
   * **Notes on usage**: This action signal is supposed to be called at the
   * end of an application before it exits, or at the end of an execution run
   * when all streaming has stopped and all pipelines have been freed. It is
   * assumed that at this point any GStreamer object that is still alive is
   * leaked, and there are no legitimate owners any more. As such, ownership
   * of the leaked objects is transferred to you then, assuming no other code
   * still retrains references to them.
   *
   * If that's not the case, and there is code somewhere still holding
   * a reference, then the application behaviour is undefined after this
   * function is called, since we will have stolen some other code's valid
   * reference and when the returned #GstStructure is freed that code will be
   * holding a reference to an invalid object, which will most likely crash
   * sooner or later.
   *
   * If you don't want to just check for leaks at the end of a program, the
   * activity checkpoint action signals might be a better fit for your use
   * case.
   *
   * Returns: (transfer full): a newly-allocated #GstStructure
   *
   * Since: 1.18
   */
  gst_leaks_tracer_signals[SIGNAL_GET_LIVE_OBJECTS] =
      g_signal_new ("get-live-objects", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstLeaksTracerClass,
          get_live_objects), NULL, NULL, NULL, GST_TYPE_STRUCTURE, 0,
      G_TYPE_NONE);

  /**
   * GstLeaksTracer::log-live-objects:
   * @leakstracer: the leaks tracer object to emit this signal on
   *
   * Logs all objects that are still alive to the debug log in the same format
   * as the logging during gst_deinit().
   *
   * Since: 1.18
   */
  gst_leaks_tracer_signals[SIGNAL_LOG_LIVE_OBJECTS] =
      g_signal_new ("log-live-objects", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstLeaksTracerClass,
          log_live_objects), NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstLeaksTracer:::activity-start-tracking
   * @leakstracer: the leaks tracer object to emit this signal on
   *
   * Start storing information about all objects that are being created or
   * removed. Call `stop-tracking` to stop.
   *
   * NOTE: You do not need to call this to use the *-live-objects action
   * signals listed above.
   *
   * Since: 1.18
   */
  gst_leaks_tracer_signals[SIGNAL_ACTIVITY_START_TRACKING] =
      g_signal_new ("activity-start-tracking", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstLeaksTracerClass,
          activity_start_tracking), NULL, NULL, NULL, G_TYPE_NONE, 0,
      G_TYPE_NONE);

  /**
   * GstLeaksTracer:::activity-get-checkpoint
   * @leakstracer: the leaks tracer object to emit this signal on
   *
   * You must call this after calling `activity-start-tracking` and you should
   * call `activity-stop-tracking` when you are done tracking.
   *
   * Returns a #GstStructure with two fields: `"objects-created-list"` and
   * `"objects-removed-list"`, each of which is a #GValue of type #GST_TYPE_LIST
   * containing all objects that were created/removed since the last
   * checkpoint, or since tracking started if this is the first checkpoint.
   *
   * The list elements are in order of creation/removal. Each list element is
   * a #GValue containing a #GstStructure with the following fields:
   *
   * `type-name`: a string representing the type of the object
   * `address`: a string representing the address of the object; the object
   *            itself cannot be returned since we don't own it and it may be
   *            freed at any moment, or it may already have been freed
   *
   * Returns: (transfer full): a newly-allocated #GstStructure
   *
   * Since: 1.18
   */
  gst_leaks_tracer_signals[SIGNAL_ACTIVITY_GET_CHECKPOINT] =
      g_signal_new ("activity-get-checkpoint", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstLeaksTracerClass,
          activity_get_checkpoint), NULL, NULL, NULL, GST_TYPE_STRUCTURE, 0,
      G_TYPE_NONE);

  /**
   * GstLeaksTracer:::activity-log-checkpoint
   * @leakstracer: the leaks tracer object to emit this signal on
   *
   * You must call this after calling `activity-start-tracking` and you should
   * call `activity-stop-tracking` when you are done tracking.
   *
   * List all objects that were created or removed since the last checkpoint,
   * or since tracking started if this is the first checkpoint.
   *
   * This action signal is equivalent to `activity-get-checkpoint` except that
   * the checkpoint data will be printed to the debug log under `GST_TRACER:7`.
   *
   * Since: 1.18
   */
  gst_leaks_tracer_signals[SIGNAL_ACTIVITY_LOG_CHECKPOINT] =
      g_signal_new ("activity-log-checkpoint", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstLeaksTracerClass,
          activity_log_checkpoint), NULL, NULL, NULL, G_TYPE_NONE, 0,
      G_TYPE_NONE);

  /**
   * GstLeaksTracer:::activity-stop-tracking
   * @leakstracer: the leaks tracer object to emit this signal on
   *
   * Stop tracking all objects that are being created or removed, undoes the
   * effects of the `start-tracking` signal.
   *
   * Since: 1.18
   */
  gst_leaks_tracer_signals[SIGNAL_ACTIVITY_STOP_TRACKING] =
      g_signal_new ("activity-stop-tracking", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstLeaksTracerClass,
          activity_stop_tracking), NULL, NULL, NULL, G_TYPE_NONE, 0,
      G_TYPE_NONE);

  klass->get_live_objects = gst_leaks_tracer_get_live_objects;
  klass->log_live_objects = gst_leaks_tracer_log_live_objects;
  klass->activity_start_tracking = gst_leaks_tracer_activity_start_tracking;
  klass->activity_get_checkpoint = gst_leaks_tracer_activity_get_checkpoint;
  klass->activity_log_checkpoint = gst_leaks_tracer_activity_log_checkpoint;
  klass->activity_stop_tracking = gst_leaks_tracer_activity_stop_tracking;
}
