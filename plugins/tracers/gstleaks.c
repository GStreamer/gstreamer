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
 * Starting with GStreamer 1.18, you can also use action signals on the tracer
 * object to fetch leak information. Use gst_tracing_get_active_tracers() to
 * get a list of all active tracers and find the right one by name.
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
#include <signal.h>
#endif /* G_OS_UNIX */

GST_DEBUG_CATEGORY_STATIC (gst_leaks_debug);
#define GST_CAT_DEFAULT gst_leaks_debug

enum
{
  /* actions */
  SIGNAL_GET_LIVE_OBJECTS,

  LAST_SIGNAL
};

#define DEFAULT_LOG_LEAKS TRUE  /* for backwards-compat */

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_leaks_debug, "leaks", 0, "leaks tracer");
#define gst_leaks_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLeaksTracer, gst_leaks_tracer,
    GST_TYPE_TRACER, _do_init);

static GstStructure *gst_leaks_tracer_get_live_objects (GstLeaksTracer * self);

static GstTracerRecord *tr_alive;
static GstTracerRecord *tr_refings;
#ifdef G_OS_UNIX
static GstTracerRecord *tr_added = NULL;
static GstTracerRecord *tr_removed = NULL;
#endif /* G_OS_UNIX */
static GQueue instances = G_QUEUE_INIT;
static guint gst_leaks_tracer_signals[LAST_SIGNAL] = { 0 };

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

#ifdef G_OS_UNIX
/* The object may be destroyed when we log it using the checkpointing system so
 * we have to save its type name */
typedef struct
{
  gpointer object;
  const gchar *type_name;
} ObjectLog;

static ObjectLog *
object_log_new (gpointer obj)
{
  ObjectLog *o = g_new (ObjectLog, 1);

  o->object = obj;

  if (G_IS_OBJECT (obj))
    o->type_name = G_OBJECT_TYPE_NAME (obj);
  else
    o->type_name = g_type_name (GST_MINI_OBJECT_TYPE (obj));

  return o;
}

static void
object_log_free (ObjectLog * obj)
{
  g_free (obj);
}
#endif /* G_OS_UNIX */

static void
handle_object_destroyed (GstLeaksTracer * self, gpointer object)
{
  GST_OBJECT_LOCK (self);
  if (self->done) {
    g_warning
        ("object %p destroyed while the leaks tracer was finalizing. Some threads are still running?",
        object);
    goto out;
  }

  g_hash_table_remove (self->objects, object);
#ifdef G_OS_UNIX
  if (self->removed)
    g_hash_table_add (self->removed, object_log_new (object));
#endif /* G_OS_UNIX */
out:
  GST_OBJECT_UNLOCK (self);
}

static void
object_weak_cb (gpointer data, GObject * object)
{
  GstLeaksTracer *self = data;

  handle_object_destroyed (self, object);
}

static void
mini_object_weak_cb (gpointer data, GstMiniObject * object)
{
  GstLeaksTracer *self = data;

  handle_object_destroyed (self, object);
}

static void
handle_object_created (GstLeaksTracer * self, gpointer object, GType type,
    gboolean gobject)
{
  ObjectRefingInfos *infos;


  if (!should_handle_object_type (self, type))
    return;

  infos = g_malloc0 (sizeof (ObjectRefingInfos));
  if (gobject)
    g_object_weak_ref ((GObject *) object, object_weak_cb, self);
  else
    gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (object),
        mini_object_weak_cb, self);

  GST_OBJECT_LOCK (self);
  if ((gint) self->trace_flags != -1)
    infos->creation_trace = gst_debug_get_stack_trace (self->trace_flags);

  g_hash_table_insert (self->objects, object, infos);

#ifdef G_OS_UNIX
  if (self->added)
    g_hash_table_add (self->added, object_log_new (object));
#endif /* G_OS_UNIX */
  GST_OBJECT_UNLOCK (self);
}

static void
mini_object_created_cb (GstTracer * tracer, GstClockTime ts,
    GstMiniObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_created (self, object, GST_MINI_OBJECT_TYPE (object), FALSE);
}

static void
object_created_cb (GstTracer * tracer, GstClockTime ts, GstObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);
  GType object_type = G_OBJECT_TYPE (object);

  /* Can't track tracers as they may be disposed after the leak tracer itself */
  if (g_type_is_a (object_type, GST_TYPE_TRACER))
    return;

  handle_object_created (self, object, object_type, TRUE);
}

static void
handle_object_reffed (GstLeaksTracer * self, gpointer object, gint new_refcount,
    gboolean reffed, GstClockTime ts)
{
  ObjectRefingInfos *infos;
  ObjectRefingInfo *refinfo;

  if (!self->check_refs)
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

  handle_object_reffed (self, object, new_refcount, TRUE, ts);
}

static void
object_unreffed_cb (GstTracer * tracer, GstClockTime ts, GstObject * object,
    gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, new_refcount, FALSE, ts);
}

static void
mini_object_reffed_cb (GstTracer * tracer, GstClockTime ts,
    GstMiniObject * object, gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, new_refcount, TRUE, ts);
}

static void
mini_object_unreffed_cb (GstTracer * tracer, GstClockTime ts,
    GstMiniObject * object, gint new_refcount)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER_CAST (tracer);

  handle_object_reffed (self, object, new_refcount, FALSE, ts);
}

static void
gst_leaks_tracer_init (GstLeaksTracer * self)
{
  self->log_leaks = DEFAULT_LOG_LEAKS;
  self->objects = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) object_refing_infos_free);

  g_queue_push_tail (&instances, self);
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

    if (GST_IS_OBJECT (obj)) {
      if (GST_OBJECT_FLAG_IS_SET (obj, GST_OBJECT_FLAG_MAY_BE_LEAKED))
        continue;

      type = G_OBJECT_TYPE (obj);
      ref_count = ((GObject *) obj)->ref_count;
    } else {
      if (GST_MINI_OBJECT_FLAG_IS_SET (obj, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED))
        continue;

      type = GST_MINI_OBJECT_TYPE (obj);
      ref_count = ((GstMiniObject *) obj)->refcount;
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
    if (GST_IS_OBJECT (leak->obj))
      g_value_take_object (&obj_value, leak->obj);
    else
      /* mini objects */
      g_value_take_boxed (&obj_value, leak->obj);
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
      GValue r_value;
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

  if (!ret_leaks)
    GST_TRACE_OBJECT (self, "start listing currently alive objects");

  leaks = create_leaks_list (self);
  if (!leaks) {
    if (!ret_leaks)
      GST_TRACE_OBJECT (self, "No objects alive currently");
    goto done;
  }

  for (l = leaks; l; l = l->next)
    process_leak (l->data, ret_leaks);

  g_list_free_full (leaks, (GDestroyNotify) leak_free);

  ret = TRUE;

done:
  if (!ret_leaks)
    GST_TRACE_OBJECT (self, "done listing currently alive objects");

  return ret;
}

static void
gst_leaks_tracer_finalize (GObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER (object);
  gboolean leaks = FALSE;
  GHashTableIter iter;
  gpointer obj;

  self->done = TRUE;

  /* Tracers are destroyed as part of gst_deinit() so now is a good time to
   * report all the objects which are still alive. */
  if (self->log_leaks)
    leaks = process_leaks (self, NULL);

  /* Remove weak references */
  g_hash_table_iter_init (&iter, self->objects);
  while (g_hash_table_iter_next (&iter, &obj, NULL)) {
    if (GST_IS_OBJECT (obj))
      g_object_weak_unref (obj, object_weak_cb, self);
    else
      gst_mini_object_weak_unref (GST_MINI_OBJECT_CAST (obj),
          mini_object_weak_cb, self);
  }

  g_clear_pointer (&self->objects, g_hash_table_unref);
  if (self->filter)
    g_array_free (self->filter, TRUE);
  g_clear_pointer (&self->added, g_hash_table_unref);
  g_clear_pointer (&self->removed, g_hash_table_unref);
  g_clear_pointer (&self->unhandled_filter, g_hash_table_unref);

  g_queue_remove (&instances, self);

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
static void
sig_usr1_handler_foreach (gpointer data, gpointer user_data)
{
  GstLeaksTracer *tracer = data;

  GST_OBJECT_LOCK (tracer);
  process_leaks (tracer, NULL);
  GST_OBJECT_UNLOCK (tracer);
}

static void
sig_usr1_handler (G_GNUC_UNUSED int signal)
{
  g_queue_foreach (&instances, sig_usr1_handler_foreach, NULL);
}

static void
log_checkpoint (GHashTable * hash, GstTracerRecord * record)
{
  GHashTableIter iter;
  gpointer o;

  g_hash_table_iter_init (&iter, hash);
  while (g_hash_table_iter_next (&iter, &o, NULL)) {
    ObjectLog *obj = o;

    gst_tracer_record_log (record, obj->type_name, obj->object);
  }
}

static void
do_checkpoint (GstLeaksTracer * self)
{
  GST_TRACE_OBJECT (self, "listing objects created since last checkpoint");
  log_checkpoint (self->added, tr_added);
  GST_TRACE_OBJECT (self, "listing objects removed since last checkpoint");
  log_checkpoint (self->removed, tr_removed);

  g_hash_table_remove_all (self->added);
  g_hash_table_remove_all (self->removed);
}

static void
sig_usr2_handler_foreach (gpointer data, gpointer user_data)
{
  GstLeaksTracer *tracer = data;

  GST_OBJECT_LOCK (tracer);

  if (!tracer->added) {
    GST_TRACE_OBJECT (tracer, "First checkpoint, start tracking objects");

    tracer->added = g_hash_table_new_full (NULL, NULL,
        (GDestroyNotify) object_log_free, NULL);
    tracer->removed = g_hash_table_new_full (NULL, NULL,
        (GDestroyNotify) object_log_free, NULL);
  } else {
    do_checkpoint (tracer);
  }

  GST_OBJECT_UNLOCK (tracer);
}

static void
sig_usr2_handler (G_GNUC_UNUSED int signal)
{
  g_queue_foreach (&instances, sig_usr2_handler_foreach, NULL);
}

static void
setup_signals (void)
{
  tr_added = gst_tracer_record_new ("object-added.class",
      RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS, NULL);
  GST_OBJECT_FLAG_SET (tr_added, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  tr_removed = gst_tracer_record_new ("object-removed.class",
      RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS, NULL);
  GST_OBJECT_FLAG_SET (tr_removed, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  signal (SIGUSR1, sig_usr1_handler);
  signal (SIGUSR2, sig_usr2_handler);
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
  GST_OBJECT_FLAG_SET (tr_alive, GST_OBJECT_FLAG_MAY_BE_LEAKED);

  if (g_getenv ("GST_LEAKS_TRACER_SIG"))
    setup_signals ();

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
   * NOTE: Ownership of the leaked objects is transferred to you assuming that
   *       no other code still retains references to them. If that's not true,
   *       these objects may become invalid if your application continues
   *       execution after receiving this leak information.
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

  klass->get_live_objects = gst_leaks_tracer_get_live_objects;
}
