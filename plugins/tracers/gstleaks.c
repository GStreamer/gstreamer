/* GStreamer
 * Copyright (C) 2016 Collabora Ltd. <guillaume.desmottes@collabora.co.uk>
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
 * SECTION:gstleaks
 * @short_description: detect GstObject and GstMiniObject leaks
 *
 * A tracing module tracking the lifetime of objects by logging those still
 * alive when program is exiting and raising a warning.
 * The type of objects tracked can be filtered using the parameters of the
 * tracer, for example: GST_TRACERS=leaks(filters="GstEvent,GstMessage",stack-traces-flags=full)
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

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_leaks_debug, "leaks", 0, "leaks tracer");
#define gst_leaks_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLeaksTracer, gst_leaks_tracer,
    GST_TYPE_TRACER, _do_init);

static GstTracerRecord *tr_alive;
static GstTracerRecord *tr_refings;
#ifdef G_OS_UNIX
static GstTracerRecord *tr_added = NULL;
static GstTracerRecord *tr_removed = NULL;
#endif /* G_OS_UNIX */
static GQueue instances = G_QUEUE_INIT;

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
    self->trace_flags = 0;
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
        self->unhandled_filter = g_hash_table_new (NULL, NULL);

      g_hash_table_add (self->unhandled_filter,
          GUINT_TO_POINTER (g_quark_from_string (tmp[i])));
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
  const gchar *filters = gst_structure_get_string (params, "filters");

  if (filters)
    set_filters (self, filters);
  gst_structure_get_boolean (params, "check-refs", &self->check_refs);
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
should_handle_object_type (GstLeaksTracer * self, GType object_type)
{
  guint i, len;

  if (!self->filter)
    /* No filtering, handle all types */
    return TRUE;

  if (g_atomic_int_get (&self->unhandled_filter_count)) {
    GST_OBJECT_LOCK (self);
    if (self->unhandled_filter) {
      GQuark q;

      q = g_type_qname (object_type);
      if (g_hash_table_contains (self->unhandled_filter, GUINT_TO_POINTER (q))) {
        g_array_append_val (self->filter, object_type);
        g_hash_table_remove (self->unhandled_filter, GUINT_TO_POINTER (q));

        if (g_atomic_int_dec_and_test (&self->unhandled_filter_count))
          g_clear_pointer (&self->unhandled_filter, g_hash_table_unref);

        GST_OBJECT_UNLOCK (self);
        return TRUE;
      }
    }
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
  ObjectLog *o = g_slice_new (ObjectLog);

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
  g_slice_free (ObjectLog, obj);
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
  const gchar *type_name;
  guint ref_count;
  gchar *desc;
  ObjectRefingInfos *infos;
} Leak;

/* The content of the returned Leak struct is valid until the self->objects
 * hash table has been modified. */
static Leak *
leak_new (gpointer obj, GType type, guint ref_count, ObjectRefingInfos * infos)
{
  Leak *leak = g_slice_new (Leak);

  leak->obj = obj;
  leak->type_name = g_type_name (type);
  leak->ref_count = ref_count;
  leak->desc = gst_info_strdup_printf ("%" GST_PTR_FORMAT, obj);
  leak->infos = infos;

  return leak;
}

static void
leak_free (Leak * leak)
{
  g_free (leak->desc);
  g_slice_free (Leak, leak);
}

static gint
sort_leaks (gconstpointer _a, gconstpointer _b)
{
  const Leak *a = _a, *b = _b;

  return g_strcmp0 (a->type_name, b->type_name);
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

  return l;
}

/* Return TRUE if at least one leaked object has been logged */
static gboolean
log_leaked (GstLeaksTracer * self)
{
  GList *ref, *leaks, *l;

  leaks = create_leaks_list (self);
  if (!leaks)
    return FALSE;

  for (l = leaks; l != NULL; l = g_list_next (l)) {
    Leak *leak = l->data;

    gst_tracer_record_log (tr_alive, leak->type_name, leak->obj, leak->desc,
        leak->ref_count,
        leak->infos->creation_trace ? leak->infos->creation_trace : "");

    leak->infos->refing_infos = g_list_reverse (leak->infos->refing_infos);
    for (ref = leak->infos->refing_infos; ref; ref = ref->next) {
      ObjectRefingInfo *refinfo = (ObjectRefingInfo *) ref->data;

      gst_tracer_record_log (tr_refings, refinfo->ts, leak->type_name,
          leak->obj, refinfo->reffed ? "reffed" : "unreffed",
          refinfo->new_refcount, refinfo->trace ? refinfo->trace : "");
    }
  }

  g_list_free_full (leaks, (GDestroyNotify) leak_free);

  return TRUE;
}

static void
gst_leaks_tracer_finalize (GObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER (object);
  gboolean leaks;
  GHashTableIter iter;
  gpointer obj;

  self->done = TRUE;

  /* Tracers are destroyed as part of gst_deinit() so now is a good time to
   * report all the objects which are still alive. */
  leaks = log_leaked (self);

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
    g_warning ("Leaks detected");

  ((GObjectClass *) gst_leaks_tracer_parent_class)->finalize (object);
}

#define RECORD_FIELD_TYPE_TS \
    "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, GST_TYPE_CLOCK_TIME, \
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PROCESS, \
        NULL)
#define RECORD_FIELD_TYPE_NAME \
    "type-name", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_STRING, \
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PROCESS, \
        NULL)
#define RECORD_FIELD_ADDRESS \
    "address", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_POINTER, \
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, \
        GST_TRACER_VALUE_SCOPE_PROCESS, \
        NULL)
#define RECORD_FIELD_DESC \
    "description", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_STRING, \
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PROCESS, \
        NULL)
#define RECORD_FIELD_REF_COUNT \
    "ref-count", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_UINT, \
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PROCESS, \
        NULL)
#define RECORD_FIELD_TRACE \
    "trace", GST_TYPE_STRUCTURE, gst_structure_new ("value", \
        "type", G_TYPE_GTYPE, G_TYPE_STRING, \
        "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PROCESS, \
        NULL)

#ifdef G_OS_UNIX
static void
sig_usr1_handler_foreach (gpointer data, gpointer user_data)
{
  GstLeaksTracer *tracer = data;

  GST_OBJECT_LOCK (tracer);
  GST_TRACE_OBJECT (tracer, "start listing currently alive objects");
  log_leaked (tracer);
  GST_TRACE_OBJECT (tracer, "done listing currently alive objects");
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
#endif /* G_OS_UNIX */

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

  if (g_getenv ("GST_LEAKS_TRACER_SIG")) {
#ifdef G_OS_UNIX
    setup_signals ();
#else
    g_warning ("System doesn't support POSIX signals");
#endif /* G_OS_UNIX */
  }
}
