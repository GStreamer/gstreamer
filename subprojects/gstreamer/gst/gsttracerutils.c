/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerutils.c: tracing subsystem
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

/* Tracing subsystem:
 *
 * The tracing subsystem provides hooks in the core library and API for modules
 * to attach to them.
 *
 * The user can activate tracers by setting the environment variable GST_TRACE
 * to a ';' separated list of tracers.
 *
 * Note that instantiating tracers at runtime is possible but is not thread safe
 * and needs to be done before any pipeline state is set to PAUSED.
 */

#include "gst_private.h"
#include "gstenumtypes.h"
#include "gsttracer.h"
#include "gsttracerfactory.h"
#include "gstvalue.h"
#include "gsttracerutils.h"

#ifndef GST_DISABLE_GST_TRACER_HOOKS

/* tracer quarks */

/* These strings must match order and number declared in the GstTracerQuarkId
 * enum in gsttracerutils.h! */
static const gchar *_quark_strings[] = {
  "pad-push-pre", "pad-push-post", "pad-push-list-pre", "pad-push-list-post",
  "pad-pull-range-pre", "pad-pull-range-post", "pad-push-event-pre",
  "pad-push-event-post", "pad-query-pre", "pad-query-post",
  "element-post-message-pre",
  "element-post-message-post", "element-query-pre", "element-query-post",
  "element-new", "element-add-pad", "element-remove-pad",
  "bin-add-pre", "bin-add-post", "bin-remove-pre", "bin-remove-post",
  "pad-link-pre", "pad-link-post", "pad-unlink-pre", "pad-unlink-post",
  "element-change-state-pre", "element-change-state-post",
  "mini-object-created", "mini-object-destroyed", "object-created",
  "object-destroyed", "mini-object-reffed", "mini-object-unreffed",
  "object-reffed", "object-unreffed", "plugin-feature-loaded",
  "pad-chain-pre", "pad-chain-post", "pad-chain-list-pre",
  "pad-chain-list-post", "pad-send-event-pre", "pad-send-event-post",
  "memory-init", "memory-free-pre", "memory-free-post",
  "pool-buffer-queued", "pool-buffer-dequeued", "object-parent-set",
  "span-begin", "span-end", "event",


  "none",                       /* This is a special quark for no hook - should always be LAST */
};

GQuark _priv_gst_tracer_quark_table[GST_TRACER_QUARK_MAX + 1];

/* tracing helpers */

gboolean _priv_tracer_enabled = FALSE;
GHashTable *_priv_tracers = NULL;

GST_DEBUG_CATEGORY_EXTERN (tracer_debug);

struct _GstTraceFormat
{
  GstStructure *fields;         /* name = span name, field-name -> nested GstStructure */
  gchar *description;
};

struct _GstTraceFormatBuilder
{
  GstStructure *fields;
  gchar *description;
};

struct _GstTraceField
{
  gchar *name;
  GstStructure *metadata;       /* "type" (GstTracerFieldType as gint) plus optional metadata keys */
};

static GPtrArray *_priv_span_formats = NULL;
static gint _priv_span_id_counter = 0;
G_LOCK_DEFINE_STATIC (span_formats);

static gboolean
gst_trace_format_type_is_supported (GstTracerFieldType type)
{
  return type >= GST_TRACER_FIELD_TYPE_BOOLEAN &&
      type <= GST_TRACER_FIELD_TYPE_OBJECT;
}

static void
gst_trace_format_free (GstTraceFormat * format)
{
  if (format->fields)
    gst_structure_free (format->fields);
  g_free (format->description);
  g_free (format);
}

static gchar *
list_available_tracer_properties (GObjectClass * class)
{
  GParamSpec **properties;
  guint n_properties;
  GString *props_str;
  guint i;

  props_str = g_string_new (NULL);
  properties = g_object_class_list_properties (class, &n_properties);

  if (n_properties == 0) {
    g_string_append (props_str, "No properties available");
    g_free (properties);
    return g_string_free (props_str, FALSE);
  }

  g_string_append (props_str, "Available properties:");

  for (i = 0; i < n_properties; i++) {
    GParamSpec *prop = properties[i];

    if (!((prop->flags & G_PARAM_CONSTRUCT)
            || (prop->flags & G_PARAM_CONSTRUCT_ONLY))
        || !(prop->flags & G_PARAM_WRITABLE))
      continue;

    if (!g_strcmp0 (g_param_spec_get_name (prop), "parent"))
      continue;
    if (!g_strcmp0 (g_param_spec_get_name (prop), "params"))
      continue;

    const gchar *type_name = G_PARAM_SPEC_TYPE_NAME (prop);
    GValue default_value = G_VALUE_INIT;

    /* Get default value if possible */
    g_value_init (&default_value, prop->value_type);
    g_param_value_set_default (prop, &default_value);
    gchar *default_str = g_strdup_value_contents (&default_value);

    g_string_append_printf (props_str,
        "\n  '%s' (%s) (Default: %s): %s",
        g_param_spec_get_name (prop),
        type_name,
        default_str,
        g_param_spec_get_blurb (prop) ? g_param_spec_get_blurb (prop) :
        "(no description available)");

    g_free (default_str);
    g_value_unset (&default_value);
  }

  g_free (properties);
  return g_string_free (props_str, FALSE);
}

static void
gst_tracer_utils_create_tracer (GstTracerFactory * factory, const gchar * name,
    const gchar * params)
{
  gchar *available_props = NULL;
  GObjectClass *gobject_class = g_type_class_ref (factory->type);
  GstTracer *tracer = NULL;
  const gchar **names = NULL;
  GValue *values = NULL;
  gint n_properties = 1;
  GstStructure *structure = NULL;

  if (gst_tracer_class_uses_structure_params (GST_TRACER_CLASS (gobject_class))) {
    GST_DEBUG ("Use structure parameters for %s", params);

    if (!params) {
      n_properties = 0;
      goto create;
    }

    gchar *struct_str = g_strdup_printf ("%s,%s", name, params);
    structure = gst_structure_from_string (struct_str, NULL);
    g_free (struct_str);

    if (!structure) {
      available_props = list_available_tracer_properties (gobject_class);
      g_warning
          ("Can't instantiate `%s` tracer: invalid parameters '%s'\n  %s\n",
          name, params, available_props);
      goto done;
    }
    n_properties = gst_structure_n_fields (structure);

    names = g_new0 (const gchar *, n_properties);
    values = g_new0 (GValue, n_properties);
    for (gint i = 0; i < n_properties; i++) {
      const gchar *field_name = gst_structure_nth_field_name (structure, i);
      const GValue *field_value =
          gst_structure_get_value (structure, field_name);
      GParamSpec *pspec =
          g_object_class_find_property (gobject_class, field_name);

      if (!pspec) {
        available_props = list_available_tracer_properties (gobject_class);
        g_warning
            ("Can't instantiate `%s` tracer: property '%s' not found\n  %s\n",
            name, field_name, available_props);
        goto done;
      }

      if (G_VALUE_TYPE (field_value) == pspec->value_type) {
        names[i] = field_name;
        g_value_init (&values[i], G_VALUE_TYPE (field_value));
        g_value_copy (field_value, &values[i]);
      } else if (G_VALUE_TYPE (field_value) == G_TYPE_STRING) {
        names[i] = field_name;
        g_value_init (&values[i], G_PARAM_SPEC_VALUE_TYPE (pspec));
        if (!gst_value_deserialize_with_pspec (&values[i],
                g_value_get_string (field_value), pspec)) {
          available_props = list_available_tracer_properties (gobject_class);
          g_warning
              ("Can't instantiate `%s` tracer: invalid property '%s' value: '%s'\n  %s\n",
              name, field_name, g_value_get_string (field_value),
              available_props);
          goto done;
        }
      } else if (g_value_type_transformable (G_VALUE_TYPE (field_value),
              pspec->value_type)) {
        names[i] = field_name;
        g_value_init (&values[i], pspec->value_type);
        if (!g_value_transform (field_value, &values[i])) {
          available_props = list_available_tracer_properties (gobject_class);
          g_warning
              ("Can't instantiate `%s` tracer: failed to convert property '%s' from %s to %s\n  %s\n",
              name, field_name, g_type_name (G_VALUE_TYPE (field_value)),
              g_type_name (pspec->value_type), available_props);
          goto done;
        }
      } else {
        available_props = list_available_tracer_properties (gobject_class);
        g_warning
            ("Can't instantiate `%s` tracer: property '%s' type mismatch, expected %s, got %s\n  %s\n",
            name, field_name, g_type_name (pspec->value_type),
            g_type_name (G_VALUE_TYPE (field_value)), available_props);
        goto done;
      }
    }
  } else {
    names = g_new0 (const gchar *, n_properties);
    names[0] = (const gchar *) "params";
    values = g_new0 (GValue, 1);
    g_value_init (&values[0], G_TYPE_STRING);
    g_value_set_string (&values[0], params);
  }
  GST_INFO_OBJECT (factory, "creating tracer: type-id=%u",
      (guint) factory->type);

create:
  tracer =
      GST_TRACER (g_object_new_with_properties (factory->type,
          n_properties, names, values));

done:
  g_free (available_props);

  if (structure)
    gst_structure_free (structure);

  if (values) {
    for (gint j = 0; j < n_properties; j++) {
      if (G_VALUE_TYPE (&values[j]) != G_TYPE_INVALID)
        g_value_unset (&values[j]);
    }
  }

  g_free (names);
  g_free (values);

  if (tracer) {
    /* Clear floating flag */
    gst_object_ref_sink (tracer);


    /* Check if the tracer has registered to any hook by looking through all hooks */
    gboolean tracer_registered = FALSE;
    if (_priv_tracers) {
      GList *h_list = g_hash_table_get_values (_priv_tracers);
      for (GList * h_node = h_list; h_node && !tracer_registered;
          h_node = g_list_next (h_node)) {
        for (GList * t_node = h_node->data; t_node;
            t_node = g_list_next (t_node)) {
          GstTracerHook *hook = (GstTracerHook *) t_node->data;
          if (G_OBJECT (hook->tracer) == G_OBJECT (tracer)) {
            tracer_registered = TRUE;
            break;
          }
        }
      }
      g_list_free (h_list);
    }

    /* If the tracer has not registered to any hook, register it to the "none" hook
     * to keep it alive until the end of the program */
    if (!tracer_registered) {
      GST_DEBUG_OBJECT (tracer,
          "Tracer has not registered to any hook, registering to 'none' hook");
      gst_tracing_register_hook (tracer, "none", NULL);
    }

    gst_object_unref (tracer);

  }

  g_type_class_unref (gobject_class);
}

/* Initialize the tracing system */
void
_priv_gst_tracing_init (void)
{
  gint i = 0;
  const gchar *env = g_getenv ("GST_TRACERS");

  /* We initialize the tracer sub system even if the end
   * user did not activate it through the env variable
   * so that external tools can use it anyway */
  GST_DEBUG ("Initializing GstTracer");
  _priv_tracers = g_hash_table_new (NULL, NULL);


  if (G_N_ELEMENTS (_quark_strings) != GST_TRACER_QUARK_MAX + 1)
    g_warning ("the quark table is not consistent! %d != %d",
        (gint) G_N_ELEMENTS (_quark_strings), GST_TRACER_QUARK_MAX + 1);

  for (i = 0; i <= GST_TRACER_QUARK_MAX; i++) {
    _priv_gst_tracer_quark_table[i] =
        g_quark_from_static_string (_quark_strings[i]);
  }

  if (env != NULL && *env != '\0') {
    GstRegistry *registry = gst_registry_get ();
    GstPluginFeature *feature;
    GstTracerFactory *factory;
    gchar **t = g_strsplit_set (env, ";", 0);
    gchar *params;

    GST_INFO ("enabling tracers: '%s'", env);
    i = 0;
    while (t[i]) {
      // check t[i] for params
      if ((params = strchr (t[i], '('))) {
        // params can contain multiple '(' when using this kind of parameter: 'max-buffer-size=(uint)5'
        guint n_par = 1, j;
        gchar *end = NULL;

        for (j = 1; params[j] != '\0'; j++) {
          if (params[j] == '(')
            n_par++;
          else if (params[j] == ')') {
            n_par--;
            if (n_par == 0) {
              end = &params[j];
              break;
            }
          }
        }
        *params = '\0';
        params++;
        if (end)
          *end = '\0';
      } else {
        params = NULL;
      }

      GST_INFO ("checking tracer: '%s'", t[i]);

      if ((feature = gst_registry_lookup_feature (registry, t[i]))) {
        factory = GST_TRACER_FACTORY (gst_plugin_feature_load (feature));
        if (factory) {
          gst_tracer_utils_create_tracer (factory, t[i], params);
          gst_object_unref (factory);
        } else {
          g_warning ("loading plugin containing feature %s failed!", t[i]);
        }
        gst_object_unref (feature);
      } else if (t[i][0] != '\0') {
        g_warning ("no tracer named '%s'", t[i]);
      }
      i++;
    }
    g_strfreev (t);
  }
}

void
_priv_gst_tracing_deinit (void)
{
  GList *h_list, *h_node, *t_node;
  GstTracerHook *hook;

  _priv_tracer_enabled = FALSE;
  if (!_priv_tracers) {
    G_LOCK (span_formats);
    g_clear_pointer (&_priv_span_formats, g_ptr_array_unref);
    G_UNLOCK (span_formats);
    return;
  }

  /* shutdown tracers for final reports */
  h_list = g_hash_table_get_values (_priv_tracers);
  for (h_node = h_list; h_node; h_node = g_list_next (h_node)) {
    for (t_node = h_node->data; t_node; t_node = g_list_next (t_node)) {
      hook = (GstTracerHook *) t_node->data;
      gst_object_unref (hook->tracer);
      g_free (hook);
    }
    g_list_free (h_node->data);
  }
  g_list_free (h_list);
  g_hash_table_destroy (_priv_tracers);
  _priv_tracers = NULL;

  G_LOCK (span_formats);
  g_clear_pointer (&_priv_span_formats, g_ptr_array_unref);
  G_UNLOCK (span_formats);
}

static void
gst_tracing_register_hook_id (GstTracer * tracer, GQuark detail, GCallback func)
{
  gpointer key = GINT_TO_POINTER (detail);
  GList *list = g_hash_table_lookup (_priv_tracers, key);
  GstTracerHook *hook = g_new0 (GstTracerHook, 1);
  hook->tracer = gst_object_ref (tracer);
  hook->func = func;

  list = g_list_prepend (list, hook);
  g_hash_table_replace (_priv_tracers, key, list);
  GST_DEBUG ("registering tracer for '%s', list.len=%d",
      (detail ? g_quark_to_string (detail) : "*"), g_list_length (list));
  _priv_tracer_enabled = TRUE;
}

/**
 * gst_tracing_register_hook:
 * @tracer: the tracer
 * @detail: the detailed hook
 * @func: (scope async): the callback
 *
 * Register @func to be called when the trace hook @detail is getting invoked.
 * Use %NULL for @detail to register to all hooks.
 *
 * Since: 1.8
 */
void
gst_tracing_register_hook (GstTracer * tracer, const gchar * detail,
    GCallback func)
{
  gst_tracing_register_hook_id (tracer, g_quark_try_string (detail), func);
}

/**
 * gst_tracing_get_active_tracers:
 *
 * Get a list of all active tracer objects owned by the tracing framework for
 * the entirety of the run-time of the process or till gst_deinit() is called.
 *
 * Returns: (transfer full) (element-type Gst.Tracer): A #GList of
 * #GstTracer objects
 *
 * Since: 1.18
 */
GList *
gst_tracing_get_active_tracers (void)
{
  GList *tracers, *h_list, *h_node, *t_node;
  GstTracerHook *hook;

  if (!_priv_tracer_enabled || !_priv_tracers)
    return NULL;

  tracers = NULL;
  h_list = g_hash_table_get_values (_priv_tracers);
  for (h_node = h_list; h_node; h_node = g_list_next (h_node)) {
    for (t_node = h_node->data; t_node; t_node = g_list_next (t_node)) {
      hook = (GstTracerHook *) t_node->data;
      /* Skip duplicate tracers from different hooks. This function is O(n), but
       * that should be fine since the number of tracers enabled on a process
       * should be small. */
      if (g_list_index (tracers, hook->tracer) >= 0)
        continue;
      tracers = g_list_prepend (tracers, gst_object_ref (hook->tracer));
    }
  }
  g_list_free (h_list);

  return tracers;
}

/**
 * gst_trace_format_builder_new:
 * @name: the span name
 *
 * Creates a new builder for a span format. The builder is used to
 * declare the span's field schema before registering it with
 * gst_trace_format_builder_register().
 *
 * Returns: (transfer full): a new #GstTraceFormatBuilder
 *
 * Since: 1.30
 */
GstTraceFormatBuilder *
gst_trace_format_builder_new (const gchar * name)
{
  GstTraceFormatBuilder *builder;

  g_return_val_if_fail (name != NULL && *name != '\0', NULL);

  builder = g_new0 (GstTraceFormatBuilder, 1);
  builder->fields = gst_structure_new_empty (name);

  return builder;
}

/**
 * gst_trace_format_builder_free:
 * @builder: (transfer full) (nullable): a #GstTraceFormatBuilder
 *
 * Frees an unregistered builder.
 *
 * Since: 1.30
 */
void
gst_trace_format_builder_free (GstTraceFormatBuilder * builder)
{
  if (!builder)
    return;

  if (builder->fields)
    gst_structure_free (builder->fields);
  g_free (builder->description);
  g_free (builder);
}

/**
 * gst_trace_format_builder_set_description:
 * @builder: a #GstTraceFormatBuilder
 * @description: (nullable): a human-readable description
 *
 * Sets a human-readable description on the format being built. Replaces any
 * previous description.
 *
 * Returns: (transfer none): @builder for chaining
 *
 * Since: 1.30
 */
GstTraceFormatBuilder *
gst_trace_format_builder_set_description (GstTraceFormatBuilder *
    builder, const gchar * description)
{
  g_return_val_if_fail (builder != NULL, NULL);

  g_free (builder->description);
  builder->description = g_strdup (description);

  return builder;
}

/**
 * gst_trace_format_builder_add_field:
 * @builder: a #GstTraceFormatBuilder
 * @name: the field name
 * @type: the field #GType
 *
 * Adds a positional field to the format. @type must be one of %G_TYPE_BOOLEAN,
 * %G_TYPE_INT, %G_TYPE_UINT, %G_TYPE_INT64, %G_TYPE_UINT64, %G_TYPE_DOUBLE,
 * %G_TYPE_STRING or %G_TYPE_POINTER, matching the `GstTraceValue` union
 * member callers will pass to gst_trace_span_begin(). Duplicate
 * field names are rejected.
 *
 * Returns: (transfer none): @builder for chaining
 *
 * Since: 1.30
 */
GstTraceFormatBuilder *
gst_trace_format_builder_add_field (GstTraceFormatBuilder * builder,
    const gchar * name, GstTracerFieldType type)
{
  return gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_new (name, type));
}

/**
 * gst_trace_format_builder_add_field_full:
 * @builder: a #GstTraceFormatBuilder
 * @field: (transfer full): a #GstTraceField built via gst_trace_field_new()
 *   and optionally configured with chainable setters such as
 *   gst_trace_field_set_description()
 *
 * Adds a positional field with optional metadata to the format. The field is
 * consumed and freed by this call; do not use @field afterwards.
 *
 * Returns: (transfer none): @builder for chaining
 *
 * Since: 1.30
 */
GstTraceFormatBuilder *
gst_trace_format_builder_add_field_full (GstTraceFormatBuilder *
    builder, GstTraceField * field)
{
  gint type = -1;

  g_return_val_if_fail (builder != NULL, NULL);
  g_return_val_if_fail (field != NULL, builder);

  gst_structure_get (field->metadata, "type", G_TYPE_INT, &type, NULL);
  if (!gst_trace_format_type_is_supported (type)) {
    g_warning ("Field '%s' has unsupported field type %d on span format '%s'",
        field->name, type, gst_structure_get_name (builder->fields));
    gst_trace_field_free (field);
    return builder;
  }

  if (gst_structure_has_field (builder->fields, field->name)) {
    g_warning ("Field '%s' already declared on span format '%s'",
        field->name, gst_structure_get_name (builder->fields));
    gst_trace_field_free (field);
    return builder;
  }

  gst_structure_set (builder->fields, field->name, GST_TYPE_STRUCTURE,
      field->metadata, NULL);

  gst_trace_field_free (field);

  return builder;
}

/**
 * gst_trace_field_new:
 * @name: the field name
 * @type: the field #GstTracerFieldType
 *
 * Creates a transient field descriptor that can be configured with chainable
 * setters and then consumed by
 * gst_trace_format_builder_add_field_full().
 *
 * Returns: (transfer full): a new #GstTraceField
 *
 * Since: 1.30
 */
GstTraceField *
gst_trace_field_new (const gchar * name, GstTracerFieldType type)
{
  GstTraceField *field;

  g_return_val_if_fail (name != NULL && *name != '\0', NULL);

  field = g_new0 (GstTraceField, 1);
  field->name = g_strdup (name);
  field->metadata = gst_structure_new_empty ("field");
  gst_structure_set (field->metadata, "type", G_TYPE_INT, (gint) type, NULL);

  return field;
}

/**
 * gst_trace_field_free:
 * @field: (transfer full) (nullable): a #GstTraceField
 *
 * Frees a field that has not been added to a builder.
 *
 * Since: 1.30
 */
void
gst_trace_field_free (GstTraceField * field)
{
  if (!field)
    return;

  g_free (field->name);
  if (field->metadata)
    gst_structure_free (field->metadata);
  g_free (field);
}

/**
 * gst_trace_field_set_description:
 * @field: a #GstTraceField
 * @description: (nullable): a human-readable description; %NULL clears any
 *   previously set description
 *
 * Sets the description metadata on @field. Tracers and documentation
 * generators surface this string when describing the field.
 *
 * Returns: (transfer none): @field for chaining
 *
 * Since: 1.30
 */
GstTraceField *
gst_trace_field_set_description (GstTraceField * field,
    const gchar * description)
{
  g_return_val_if_fail (field != NULL, NULL);

  if (description == NULL)
    gst_structure_remove_field (field->metadata, "description");
  else
    gst_structure_set (field->metadata, "description", G_TYPE_STRING,
        description, NULL);

  return field;
}

/**
 * gst_trace_field_set_scope:
 * @field: a #GstTraceField
 * @scope: the #GstTracerValueScope the field relates to
 *
 * Declares which entity (process, thread, element or pad) the field's value
 * relates to. Post-processing tools such as `gst-stats` use this to group
 * measurements.
 *
 * Returns: (transfer none): @field for chaining
 *
 * Since: 1.30
 */
GstTraceField *
gst_trace_field_set_scope (GstTraceField * field, GstTracerValueScope scope)
{
  g_return_val_if_fail (field != NULL, NULL);

  gst_structure_set (field->metadata, "scope", GST_TYPE_TRACER_VALUE_SCOPE,
      scope, NULL);

  return field;
}

/**
 * gst_trace_field_set_flags:
 * @field: a #GstTraceField
 * @flags: the #GstTracerValueFlags describing the field
 *
 * Sets metadata flags on @field. %GST_TRACER_VALUE_FLAGS_OPTIONAL marks the
 * field as optional: when emitting an event, the field's value must be
 * preceded by a #gboolean value telling whether it is present.
 *
 * Returns: (transfer none): @field for chaining
 *
 * Since: 1.30
 */
GstTraceField *
gst_trace_field_set_flags (GstTraceField * field, GstTracerValueFlags flags)
{
  g_return_val_if_fail (field != NULL, NULL);

  gst_structure_set (field->metadata, "flags", GST_TYPE_TRACER_VALUE_FLAGS,
      flags, NULL);

  return field;
}

/**
 * gst_trace_format_builder_register:
 * @builder: (transfer full): a #GstTraceFormatBuilder
 *
 * Registers the format declared by @builder and consumes the builder.
 * The returned format is valid until gst_deinit().
 *
 * Returns: (transfer none): the registered #GstTraceFormat
 *
 * Since: 1.30
 */
GstTraceFormat *
gst_trace_format_builder_register (GstTraceFormatBuilder * builder)
{
  GstTraceFormat *format;

  g_return_val_if_fail (builder != NULL, NULL);

  format = g_new0 (GstTraceFormat, 1);
  format->fields = g_steal_pointer (&builder->fields);
  format->description = g_steal_pointer (&builder->description);

  gst_trace_format_builder_free (builder);

  G_LOCK (span_formats);
  if (!_priv_span_formats)
    _priv_span_formats = g_ptr_array_new_with_free_func ((GDestroyNotify)
        gst_trace_format_free);
  g_ptr_array_add (_priv_span_formats, format);
  G_UNLOCK (span_formats);

  return format;
}

/**
 * gst_trace_format_register: (skip)
 * @name: the span name
 * @...: %NULL-terminated list of (field-name, #GType) pairs
 *
 * Convenience wrapper around #GstTraceFormatBuilder for the common case
 * where only the field names and types are declared. Equivalent to creating
 * a builder, calling gst_trace_format_builder_add_field() for each pair
 * and then gst_trace_format_builder_register().
 *
 * Returns: (transfer none): the registered #GstTraceFormat
 *
 * Since: 1.30
 */
GstTraceFormat *
gst_trace_format_register (const gchar * name, ...)
{
  GstTraceFormatBuilder *builder;
  va_list args;
  const gchar *field_name;

  g_return_val_if_fail (name != NULL, NULL);

  builder = gst_trace_format_builder_new (name);

  va_start (args, name);
  while ((field_name = va_arg (args, const gchar *)) != NULL)
  {
    GstTracerFieldType field_type = (GstTracerFieldType) va_arg (args, int);
    gst_trace_format_builder_add_field (builder, field_name, field_type);
  }
  va_end (args);

  return gst_trace_format_builder_register (builder);
}

/**
 * gst_trace_format_get_name:
 * @format: a #GstTraceFormat
 *
 * Returns: (transfer none): the registered span name
 *
 * Since: 1.30
 */
const gchar *
gst_trace_format_get_name (GstTraceFormat * format)
{
  g_return_val_if_fail (format != NULL, NULL);

  return gst_structure_get_name (format->fields);
}

/**
 * gst_trace_format_get_description:
 * @format: a #GstTraceFormat
 *
 * Returns: (transfer none) (nullable): the description set on the builder, or
 *   %NULL if none was set
 *
 * Since: 1.30
 */
const gchar *
gst_trace_format_get_description (GstTraceFormat * format)
{
  g_return_val_if_fail (format != NULL, NULL);

  return format->description;
}

/**
 * gst_trace_format_get_n_fields:
 * @format: a #GstTraceFormat
 *
 * Returns: the number of fields declared on @format
 *
 * Since: 1.30
 */
guint
gst_trace_format_get_n_fields (GstTraceFormat * format)
{
  g_return_val_if_fail (format != NULL, 0);

  return gst_structure_n_fields (format->fields);
}

/**
 * gst_trace_format_get_field_name:
 * @format: a #GstTraceFormat
 * @index: a field index
 *
 * Returns: (transfer none): the name of the field at @index, or %NULL if
 *   @index is out of range
 *
 * Since: 1.30
 */
const gchar *
gst_trace_format_get_field_name (GstTraceFormat * format, guint index)
{
  g_return_val_if_fail (format != NULL, NULL);

  return gst_structure_nth_field_name (format->fields, index);
}

/**
 * gst_trace_format_get_field_structure:
 * @format: a #GstTraceFormat
 * @index: a field index
 *
 * Returns the raw #GstStructure describing the field at @index. The structure
 * carries the field's type (`"type"`, a #GstTracerFieldType stored as #gint)
 * and any optional metadata declared via
 * gst_trace_format_builder_add_field_full() (e.g. `"description"`).
 *
 * Returns: (transfer none) (nullable): the field structure, or %NULL if
 *   @index is out of range
 *
 * Since: 1.30
 */
const GstStructure *
gst_trace_format_get_field_structure (GstTraceFormat * format, guint index)
{
  const gchar *name;
  const GValue *value;

  g_return_val_if_fail (format != NULL, NULL);

  name = gst_structure_nth_field_name (format->fields, index);
  if (!name)
    return NULL;

  value = gst_structure_get_value (format->fields, name);
  if (!value || !G_VALUE_HOLDS (value, GST_TYPE_STRUCTURE))
    return NULL;

  return gst_value_get_structure (value);
}

/**
 * gst_trace_format_get_field_type:
 * @format: a #GstTraceFormat
 * @index: a field index
 *
 * Returns: the #GstTracerFieldType of the field at @index (defaults to
 *   %GST_TRACER_FIELD_TYPE_BOOLEAN if @index is out of range)
 *
 * Since: 1.30
 */
GstTracerFieldType
gst_trace_format_get_field_type (GstTraceFormat * format, guint index)
{
  const GstStructure *field;
  gint type = GST_TRACER_FIELD_TYPE_BOOLEAN;

  g_return_val_if_fail (format != NULL, GST_TRACER_FIELD_TYPE_BOOLEAN);

  field = gst_trace_format_get_field_structure (format, index);
  if (!field)
    return GST_TRACER_FIELD_TYPE_BOOLEAN;

  gst_structure_get (field, "type", G_TYPE_INT, &type, NULL);
  return (GstTracerFieldType) type;
}

/**
 * gst_trace_format_get_field_description:
 * @format: a #GstTraceFormat
 * @index: a field index
 *
 * Returns: (transfer none) (nullable): the description declared for the field
 *   at @index, or %NULL if no description was set (or @index is out of range)
 *
 * Since: 1.30
 */
const gchar *
gst_trace_format_get_field_description (GstTraceFormat * format, guint index)
{
  const GstStructure *field;

  g_return_val_if_fail (format != NULL, NULL);

  field = gst_trace_format_get_field_structure (format, index);
  if (!field)
    return NULL;

  return gst_structure_get_string (field, "description");
}

/**
 * gst_trace_format_is_enabled:
 * @format: a #GstTraceFormat
 *
 * Cheap check that lets callers skip preparing span values when no tracer
 * is active. Returns %TRUE whenever any tracer is registered, regardless
 * of which hooks it listens for.
 *
 * Returns: %TRUE if any tracer is active
 *
 * Since: 1.30
 */
gboolean
gst_trace_format_is_enabled (GstTraceFormat * format)
{
  return format != NULL && _priv_tracer_enabled;
}

/**
 * gst_trace_span_begin:
 * @format: a #GstTraceFormat
 * @values: (array) (nullable): positional values matching @format
 *
 * Emits a span begin hook for @format.
 *
 * Values are borrowed for the duration of the hook call. The number of
 * @values must match the fields of the structure used to register @format.
 *
 * Returns: a span id to pass to gst_trace_span_end(), or
 * %GST_TRACE_SPAN_ID_NONE if no tracer is listening for spans
 *
 * Since: 1.30
 */
GstTraceSpanId
gst_trace_span_begin (GstTraceFormat * format, const GstTraceValue * values)
{
  GstTraceSpanId span_id;

  if (G_LIKELY (!format || !_priv_tracer_enabled))
    return GST_TRACE_SPAN_ID_NONE;

  span_id = (GstTraceSpanId)
      (g_atomic_int_add (&_priv_span_id_counter, 1) + 1);
  GST_TRACER_DISPATCH (GST_TRACER_QUARK (HOOK_SPAN_BEGIN),
      GstTracerHookSpanBegin, (GST_TRACER_ARGS, span_id, format, values));

  return span_id;
}

/**
 * gst_trace_span_end:
 * @span_id: a span id returned by gst_trace_span_begin()
 *
 * Emits a span end hook. Passing %GST_TRACE_SPAN_ID_NONE is allowed and
 * is a no-op.
 *
 * Since: 1.30
 */
void
gst_trace_span_end (GstTraceSpanId span_id)
{
  if (G_LIKELY (span_id == GST_TRACE_SPAN_ID_NONE))
    return;

  GST_TRACER_DISPATCH (GST_TRACER_QUARK (HOOK_SPAN_END),
      GstTracerHookSpanEnd, (GST_TRACER_ARGS, span_id));
}

/**
 * gst_trace_span_end_and_clear:
 * @span_id: (inout): a pointer to a #GstTraceSpanId; may not be %NULL
 *
 * Ends the span pointed to by @span_id and resets it to
 * %GST_TRACE_SPAN_ID_NONE. Safe to call when @span_id already holds
 * %GST_TRACE_SPAN_ID_NONE. Intended for spans stored on long-lived state
 * (struct fields) where the slot must be cleared after closing the span.
 *
 * Since: 1.30
 */
void
gst_trace_span_end_and_clear (GstTraceSpanId * span_id)
{
  g_return_if_fail (span_id != NULL);

  gst_trace_span_end (*span_id);
  *span_id = GST_TRACE_SPAN_ID_NONE;
}

/**
 * gst_trace_event:
 * @format: a #GstTraceFormat describing the fields of the event
 * @values: (array) (nullable): the positional values for @format's fields, in
 *   declaration order; %NULL logs all fields as unset
 *
 * Emits a point event for @format: a single timestamped record with no
 * duration, the counterpart to the begin/end pair of gst_trace_span_begin()
 * and the structured replacement for gst_tracer_record_log().
 *
 * The event is delivered to tracers through the "event" hook; the `log` tracer
 * renders it to the GST_TRACER debug log. Values are positional and must match
 * @format; keep their computation cheap as they are built whenever tracing is
 * enabled.
 *
 * Since: 1.30
 */
void
gst_trace_event (GstTraceFormat * format, const GstTraceValue * values)
{
  if (G_UNLIKELY (!format))
    return;

  GST_TRACER_DISPATCH (GST_TRACER_QUARK (HOOK_EVENT),
      GstTracerHookEvent, (GST_TRACER_ARGS, format, values));
}

#else /* !GST_DISABLE_GST_TRACER_HOOKS */

void
gst_tracing_register_hook (GstTracer * tracer, const gchar * detail,
    GCallback func)
{
}

GList *
gst_tracing_get_active_tracers (void)
{
  return NULL;
}

GstTraceFormatBuilder *
gst_trace_format_builder_new (const gchar * name)
{
  return NULL;
}

void
gst_trace_format_builder_free (GstTraceFormatBuilder * builder)
{
}

GstTraceFormatBuilder *
gst_trace_format_builder_set_description (GstTraceFormatBuilder *
    builder, const gchar * description)
{
  return builder;
}

GstTraceFormatBuilder *
gst_trace_format_builder_add_field (GstTraceFormatBuilder * builder,
    const gchar * name, GstTracerFieldType type)
{
  return builder;
}

GstTraceFormatBuilder *
gst_trace_format_builder_add_field_full (GstTraceFormatBuilder *
    builder, GstTraceField * field)
{
  return builder;
}

GstTraceField *
gst_trace_field_new (const gchar * name, GstTracerFieldType type)
{
  return NULL;
}

GstTraceField *
gst_trace_field_set_description (GstTraceField * field,
    const gchar * description)
{
  return field;
}

GstTraceField *
gst_trace_field_set_scope (GstTraceField * field, GstTracerValueScope scope)
{
  return field;
}

GstTraceField *
gst_trace_field_set_flags (GstTraceField * field, GstTracerValueFlags flags)
{
  return field;
}

void
gst_trace_field_free (GstTraceField * field)
{
}

GstTraceFormat *
gst_trace_format_builder_register (GstTraceFormatBuilder * builder)
{
  return NULL;
}

GstTraceFormat *
gst_trace_format_register (const gchar * name, ...)
{
  return NULL;
}

const gchar *
gst_trace_format_get_name (GstTraceFormat * format)
{
  return NULL;
}

const gchar *
gst_trace_format_get_description (GstTraceFormat * format)
{
  return NULL;
}

guint
gst_trace_format_get_n_fields (GstTraceFormat * format)
{
  return 0;
}

const gchar *
gst_trace_format_get_field_name (GstTraceFormat * format, guint index)
{
  return NULL;
}

GstTracerFieldType
gst_trace_format_get_field_type (GstTraceFormat * format, guint index)
{
  return GST_TRACER_FIELD_TYPE_BOOLEAN;
}

const gchar *
gst_trace_format_get_field_description (GstTraceFormat * format, guint index)
{
  return NULL;
}

const GstStructure *
gst_trace_format_get_field_structure (GstTraceFormat * format, guint index)
{
  return NULL;
}

gboolean
gst_trace_format_is_enabled (GstTraceFormat * format)
{
  return FALSE;
}

GstTraceSpanId
gst_trace_span_begin (GstTraceFormat * format, const GstTraceValue * values)
{
  return GST_TRACE_SPAN_ID_NONE;
}

void
gst_trace_span_end (GstTraceSpanId span_id)
{
}

void
gst_trace_span_end_and_clear (GstTraceSpanId * span_id)
{
  g_return_if_fail (span_id != NULL);

  *span_id = GST_TRACE_SPAN_ID_NONE;
}

void
gst_trace_event (GstTraceFormat * format, const GstTraceValue * values)
{
}
#endif /* GST_DISABLE_GST_TRACER_HOOKS */
