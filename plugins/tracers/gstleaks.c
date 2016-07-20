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
 * tracer, for example: GST_TRACERS="leaks(GstEvent,GstMessage)"
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_UNWIND
/* No need for remote debugging so turn on the 'local only' optimizations in
 * libunwind */
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif /* HAVE_UNWIND */

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif /* HAVE_BACKTRACE */

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
#ifdef G_OS_UNIX
static GstTracerRecord *tr_added = NULL;
static GstTracerRecord *tr_removed = NULL;
#endif /* G_OS_UNIX */
static GQueue instances = G_QUEUE_INIT;

static void
set_filtering (GstLeaksTracer * self)
{
  gchar *params;
  GStrv tmp;
  guint i;

  g_object_get (self, "params", &params, NULL);
  if (!params)
    return;

  tmp = g_strsplit (params, ",", -1);

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
  g_free (params);
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

#ifdef HAVE_UNWIND
#define BT_NAME_SIZE 256
static gchar *
generate_unwind_trace (void)
{
  unw_context_t ctx;
  unw_cursor_t cursor;
  GString *trace;

  if (unw_getcontext (&ctx))
    return NULL;

  if (unw_init_local (&cursor, &ctx))
    return NULL;

  trace = g_string_new (NULL);
  while (unw_step (&cursor) > 0) {
    char name[BT_NAME_SIZE];
    unw_word_t offp;
    int ret;

    ret = unw_get_proc_name (&cursor, name, BT_NAME_SIZE, &offp);
    /* -UNW_ENOMEM is returned if name has been truncated */
    if (ret != 0 && ret != -UNW_ENOMEM)
      break;

    g_string_append_printf (trace, "%s\n", name);
  }

  return g_string_free (trace, FALSE);
}
#endif /* HAVE_UNWIND */

#ifdef HAVE_BACKTRACE
#define BT_BUF_SIZE 100
static gchar *
generate_backtrace_trace (void)
{
  int j, nptrs;
  void *buffer[BT_BUF_SIZE];
  char **strings;
  GString *trace;

  trace = g_string_new (NULL);
  nptrs = backtrace (buffer, BT_BUF_SIZE);

  strings = backtrace_symbols (buffer, nptrs);
  if (!strings)
    return NULL;

  for (j = 0; j < nptrs; j++)
    g_string_append_printf (trace, "%s\n", strings[j]);

  return g_string_free (trace, FALSE);
}
#endif /* HAVE_BACKTRACE */

static gchar *
generate_trace (void)
{
  gchar *trace = NULL;

#ifdef HAVE_UNWIND
  trace = generate_unwind_trace ();
  if (trace)
    return trace;
#endif /* HAVE_UNWIND */

#ifdef HAVE_BACKTRACE
  trace = generate_backtrace_trace ();
#endif /* HAVE_BACKTRACE */

  return trace;
}

static void
handle_object_created (GstLeaksTracer * self, gpointer object, GType type,
    gboolean gobject)
{
  gchar *trace = NULL;

  if (!should_handle_object_type (self, type))
    return;

  if (gobject)
    g_object_weak_ref ((GObject *) object, object_weak_cb, self);
  else
    gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (object),
        mini_object_weak_cb, self);

  GST_OBJECT_LOCK (self);
  if (self->log_stack_trace) {
    trace = generate_trace ();
  }

  g_hash_table_insert (self->objects, object, trace);

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
gst_leaks_tracer_init (GstLeaksTracer * self)
{
  self->objects = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  if (g_getenv ("GST_LEAKS_TRACER_STACK_TRACE")) {
    gchar *trace;

    /* Test if we can retrieve backtrace */
    trace = generate_trace ();
    if (trace) {
      self->log_stack_trace = TRUE;
      g_free (trace);
    } else {
      g_warning ("Can't retrieve backtrace on this system");
    }
  }

  g_queue_push_tail (&instances, self);
}

static void
gst_leaks_tracer_constructed (GObject * object)
{
  GstLeaksTracer *self = GST_LEAKS_TRACER (object);
  GstTracer *tracer = GST_TRACER (object);

  set_filtering (self);

  gst_tracing_register_hook (tracer, "mini-object-created",
      G_CALLBACK (mini_object_created_cb));
  gst_tracing_register_hook (tracer, "object-created",
      G_CALLBACK (object_created_cb));

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
  const gchar *trace;
} Leak;

/* The content of the returned Leak struct is valid until the self->objects
 * hash table has been modified. */
static Leak *
leak_new (gpointer obj, GType type, guint ref_count, const gchar * trace)
{
  Leak *leak = g_slice_new (Leak);

  leak->obj = obj;
  leak->type_name = g_type_name (type);
  leak->ref_count = ref_count;
  leak->desc = gst_info_strdup_printf ("%" GST_PTR_FORMAT, obj);
  leak->trace = trace;

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
  gpointer obj, trace;

  g_hash_table_iter_init (&iter, self->objects);
  while (g_hash_table_iter_next (&iter, &obj, &trace)) {
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

    l = g_list_prepend (l, leak_new (obj, type, ref_count, trace));
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
  GList *leaks, *l;

  leaks = create_leaks_list (self);
  if (!leaks)
    return FALSE;

  for (l = leaks; l != NULL; l = g_list_next (l)) {
    Leak *leak = l->data;

    gst_tracer_record_log (tr_alive, leak->type_name, leak->obj, leak->desc,
        leak->ref_count, leak->trace ? leak->trace : "");
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

  if (g_getenv ("GST_LEAKS_TRACER_SIG")) {
#ifdef G_OS_UNIX
    setup_signals ();
#else
    g_warning ("System doesn't support POSIX signals");
#endif /* G_OS_UNIX */
  }
}
