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

#include "gstleaks.h"

GST_DEBUG_CATEGORY_STATIC (gst_leaks_debug);
#define GST_CAT_DEFAULT gst_leaks_debug

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_leaks_debug, "leaks", 0, "leaks tracer");
#define gst_leaks_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLeaksTracer, gst_leaks_tracer,
    GST_TYPE_TRACER, _do_init);

static GstTracerRecord *tr_alive;

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
      GST_WARNING_OBJECT (self, "unknown type %s", tmp[i]);
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

  len = self->filter->len;
  for (i = 0; i < len; i++) {
    GType type = g_array_index (self->filter, GType, i);

    if (g_type_is_a (object_type, type))
      return TRUE;
  }

  return FALSE;
}

static void
object_weak_cb (gpointer data, GObject * object)
{
  GstLeaksTracer *self = data;

  GST_OBJECT_LOCK (self);
  g_hash_table_remove (self->objects, object);
  GST_OBJECT_UNLOCK (self);
}

static void
mini_object_weak_cb (gpointer data, GstMiniObject * object)
{
  GstLeaksTracer *self = data;

  GST_OBJECT_LOCK (self);
  g_hash_table_remove (self->objects, object);
  GST_OBJECT_UNLOCK (self);
}

static void
handle_object_created (GstLeaksTracer * self, gpointer object, GType type,
    gboolean gobject)
{
  if (!should_handle_object_type (self, type))
    return;

  if (gobject)
    g_object_weak_ref ((GObject *) object, object_weak_cb, self);
  else
    gst_mini_object_weak_ref (GST_MINI_OBJECT_CAST (object),
        mini_object_weak_cb, self);

  GST_OBJECT_LOCK (self);
  g_hash_table_add (self->objects, object);
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
  self->objects = g_hash_table_new (NULL, NULL);
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
} Leak;

static Leak *
leak_new (gpointer obj, GType type, guint ref_count)
{
  Leak *leak = g_slice_new (Leak);

  leak->obj = obj;
  leak->type_name = g_type_name (type);
  leak->ref_count = ref_count;
  leak->desc = gst_info_strdup_printf ("%" GST_PTR_FORMAT, obj);

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
  gpointer obj;

  g_hash_table_iter_init (&iter, self->objects);
  while (g_hash_table_iter_next (&iter, &obj, NULL)) {
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

    l = g_list_prepend (l, leak_new (obj, type, ref_count));
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
        leak->ref_count);
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

static void
gst_leaks_tracer_class_init (GstLeaksTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_leaks_tracer_constructed;
  gobject_class->finalize = gst_leaks_tracer_finalize;

  tr_alive = gst_tracer_record_new ("object-alive.class",
      RECORD_FIELD_TYPE_NAME, RECORD_FIELD_ADDRESS, RECORD_FIELD_DESC,
      RECORD_FIELD_REF_COUNT, NULL);
  GST_OBJECT_FLAG_SET (tr_alive, GST_OBJECT_FLAG_MAY_BE_LEAKED);
}
