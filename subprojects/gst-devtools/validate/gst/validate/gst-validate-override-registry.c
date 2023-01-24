/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-override-registry.c - Validate Override Registry
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gmodule.h>

#include "gst-validate-report.h"
#include "gst-validate-reporter.h"
#include "gst-validate-utils.h"
#include "gst-validate-internal.h"
#include "gst-validate-monitor.h"
#include "gst-validate-override.h"
#include "gst-validate-override-registry.h"

typedef struct
{
  gchar *name;
  GstValidateOverride *override;
} GstValidateOverrideRegistryNameEntry;

typedef struct
{
  GType gtype;
  GstValidateOverride *override;
} GstValidateOverrideRegistryGTypeEntry;

static GMutex _gst_validate_override_registry_mutex;
static GstValidateOverrideRegistry *_registry_default = NULL;

#define GST_VALIDATE_OVERRIDE_REGISTRY_LOCK(r) g_mutex_lock (&r->mutex)
#define GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK(r) g_mutex_unlock (&r->mutex)

#define GST_VALIDATE_OVERRIDE_INIT_SYMBOL "gst_validate_create_overrides"
typedef int (*GstValidateCreateOverride) (void);

static void
    gst_validate_override_registry_name_entry_free
    (GstValidateOverrideRegistryNameEntry * entry)
{
  g_free (entry->name);
  g_object_unref (entry->override);

  g_free (entry);
}

static void
    gst_validate_override_registry_type_entry_free
    (GstValidateOverrideRegistryGTypeEntry * entry)
{
  g_object_unref (entry->override);

  g_free (entry);
}

static GstValidateOverrideRegistry *
gst_validate_override_registry_new (void)
{
  GstValidateOverrideRegistry *reg = g_new0 (GstValidateOverrideRegistry, 1);

  g_mutex_init (&reg->mutex);
  g_queue_init (&reg->name_overrides);
  g_queue_init (&reg->gtype_overrides);
  g_queue_init (&reg->klass_overrides);

  return reg;
}

static void
gst_validate_overide_registery_free (GstValidateOverrideRegistry * reg)
{
  g_queue_foreach (&reg->klass_overrides,
      (GFunc) gst_validate_override_registry_name_entry_free, NULL);

  g_queue_foreach (&reg->name_overrides,
      (GFunc) gst_validate_override_registry_name_entry_free, NULL);

  g_queue_foreach (&reg->gtype_overrides,
      (GFunc) gst_validate_override_registry_type_entry_free, NULL);

  g_queue_clear (&reg->name_overrides);
  g_queue_clear (&reg->gtype_overrides);
  g_queue_clear (&reg->klass_overrides);
  g_mutex_clear (&reg->mutex);

  g_free (reg);
}

/**
 * gst_validate_override_registry_get: (skip):
 */
GstValidateOverrideRegistry *
gst_validate_override_registry_get (void)
{
  g_mutex_lock (&_gst_validate_override_registry_mutex);
  if (G_UNLIKELY (!_registry_default)) {
    _registry_default = gst_validate_override_registry_new ();
  }
  g_mutex_unlock (&_gst_validate_override_registry_mutex);

  return _registry_default;
}

void
gst_validate_override_register_by_name (const gchar * name,
    GstValidateOverride * override)
{
  GstValidateOverrideRegistry *registry = gst_validate_override_registry_get ();
  GstValidateOverrideRegistryNameEntry *entry =
      g_new (GstValidateOverrideRegistryNameEntry, 1);

  GST_VALIDATE_OVERRIDE_REGISTRY_LOCK (registry);
  entry->name = g_strdup (name);
  entry->override = g_object_ref (override);
  g_queue_push_tail (&registry->name_overrides, entry);
  GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK (registry);
}

void
gst_validate_override_register_by_type (GType gtype,
    GstValidateOverride * override)
{
  GstValidateOverrideRegistry *registry = gst_validate_override_registry_get ();
  GstValidateOverrideRegistryGTypeEntry *entry =
      g_new (GstValidateOverrideRegistryGTypeEntry, 1);

  GST_VALIDATE_OVERRIDE_REGISTRY_LOCK (registry);
  entry->gtype = gtype;
  entry->override = g_object_ref (override);
  g_queue_push_tail (&registry->gtype_overrides, entry);
  GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK (registry);
}

void
gst_validate_override_register_by_klass (const gchar * klass,
    GstValidateOverride * override)
{
  GstValidateOverrideRegistry *registry = gst_validate_override_registry_get ();
  GstValidateOverrideRegistryNameEntry *entry =
      g_new (GstValidateOverrideRegistryNameEntry, 1);

  GST_VALIDATE_OVERRIDE_REGISTRY_LOCK (registry);
  entry->name = g_strdup (klass);
  entry->override = g_object_ref (override);
  g_queue_push_tail (&registry->klass_overrides, entry);
  GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK (registry);
}

static void
    gst_validate_override_registry_attach_name_overrides_unlocked
    (GstValidateOverrideRegistry * registry, GstValidateMonitor * monitor)
{
  GstValidateOverrideRegistryNameEntry *entry;
  GList *iter;
  const gchar *name;

  name = gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor));
  for (iter = registry->name_overrides.head; iter; iter = g_list_next (iter)) {
    entry = iter->data;
    if (g_regex_match_simple (entry->name, name, 0, 0)) {
      GST_INFO ("%p Adding override %s to %s", registry, entry->name, name);

      gst_validate_monitor_attach_override (monitor, entry->override);
    }
  }
}

static void
    gst_validate_override_registry_attach_gtype_overrides_unlocked
    (GstValidateOverrideRegistry * registry, GstValidateMonitor * monitor)
{
  GstValidateOverrideRegistryGTypeEntry *entry;
  GstElement *element;
  GList *iter;

  element = gst_validate_monitor_get_element (monitor);
  if (!element)
    return;

  for (iter = registry->gtype_overrides.head; iter; iter = g_list_next (iter)) {
    entry = iter->data;
    if (G_TYPE_CHECK_INSTANCE_TYPE (element, entry->gtype)) {
      gst_validate_monitor_attach_override (monitor, entry->override);
    }
  }
  gst_object_unref (element);
}

static void
    gst_validate_override_registry_attach_klass_overrides_unlocked
    (GstValidateOverrideRegistry * registry, GstValidateMonitor * monitor)
{
  GstValidateOverrideRegistryNameEntry *entry;
  GList *iter;
  GstElement *element;

  element = gst_validate_monitor_get_element (monitor);
  if (!element)
    return;

  for (iter = registry->klass_overrides.head; iter; iter = g_list_next (iter)) {

    entry = iter->data;

    if (gst_validate_element_has_klass (element, entry->name)) {
      gst_validate_monitor_attach_override (monitor, entry->override);
    }
  }
  gst_object_unref (element);
}

void
gst_validate_override_registry_attach_overrides (GstValidateMonitor * monitor)
{
  GstValidateOverrideRegistry *reg = gst_validate_override_registry_get ();

  GST_VALIDATE_OVERRIDE_REGISTRY_LOCK (reg);
  gst_validate_override_registry_attach_name_overrides_unlocked (reg, monitor);
  gst_validate_override_registry_attach_gtype_overrides_unlocked (reg, monitor);
  gst_validate_override_registry_attach_klass_overrides_unlocked (reg, monitor);
  GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK (reg);
}

enum
{
  WRONG_FILE,
  WRONG_OVERRIDES,
  OK
};

static gboolean
_add_override_from_struct (GstStructure * soverride)
{
  GQuark issue_id;
  GstValidateReportLevel level;
  GstValidateOverride *override;
  const gchar *str_issue_id, *str_new_severity,
      *factory_name = NULL, *name = NULL, *klass = NULL;

  gboolean registered = FALSE;

  if (!gst_structure_has_name (soverride, "change-severity")
      && !gst_structure_has_name (soverride, "change-issue-severity")) {
    gst_validate_abort
        ("Currently only 'change-severity' overrides are supported");

    return FALSE;
  }

  str_issue_id = gst_structure_get_string (soverride, "issue-id");
  if (!str_issue_id) {
    gst_validate_abort ("No issue id provided in override: %" GST_PTR_FORMAT,
        soverride);

    return FALSE;
  }

  issue_id = g_quark_from_string (str_issue_id);
  if (gst_validate_issue_from_id (issue_id) == NULL) {
    gst_validate_abort ("No GstValidateIssue registered for %s", str_issue_id);

    return FALSE;
  }

  str_new_severity = gst_structure_get_string (soverride, "new-severity");
  if (str_new_severity == NULL) {
    gst_validate_abort ("No 'new-severity' field found in %" GST_PTR_FORMAT,
        soverride);

    return FALSE;
  }

  level = gst_validate_report_level_from_name (str_new_severity);
  if (level == GST_VALIDATE_REPORT_LEVEL_UNKNOWN) {
    gst_validate_abort ("Unknown level name %s", str_new_severity);

    return FALSE;
  }
  gst_validate_printf (NULL, "**-> Changing issue '%s' severity to: '%s'\n",
      str_issue_id, str_new_severity);

  override = gst_validate_override_new ();
  gst_validate_override_change_severity (override, issue_id, level);

  name = gst_structure_get_string (soverride, "element-name");
  klass = gst_structure_get_string (soverride, "element-classification");
  factory_name = gst_structure_get_string (soverride, "element-factory-name");

  if (factory_name) {
    GType type;
    GstElement *element = gst_element_factory_make (factory_name, NULL);

    if (element == NULL) {
      GST_ERROR ("Unknown element factory name: %s (gst is %sinitialized)",
          factory_name, gst_is_initialized ()? "" : "NOT ");

      if (!name && !klass)
        return FALSE;
    } else {
      type = G_OBJECT_TYPE (element);
      gst_validate_override_register_by_type (type, override);
      gst_object_unref (element);
    }

    registered = TRUE;
  }

  if (name) {
    gst_validate_override_register_by_name (name, override);
    registered = TRUE;
  }

  if (klass) {
    gst_validate_override_register_by_klass (klass, override);
    registered = TRUE;
  }

  if (!registered) {
    GstValidateIssue *issue = gst_validate_issue_from_id (issue_id);

    if (!issue) {

      g_object_unref (override);
      return FALSE;
    }

    gst_validate_issue_set_default_level (issue, level);
  }

  g_object_unref (override);
  return TRUE;
}

static gboolean
_load_text_override_file (const gchar * filename)
{
  gint ret = OK;
  GList *structs =
      gst_validate_utils_structs_parse_from_filename (filename, NULL, NULL);

  if (structs) {
    GList *tmp;

    for (tmp = structs; tmp; tmp = tmp->next) {
      GstStructure *_struct = (GstStructure *) tmp->data;
      if (!_add_override_from_struct (_struct)) {
        GST_ERROR ("Wrong overrides %" GST_PTR_FORMAT, _struct);
        ret = WRONG_OVERRIDES;
      }
    }

    goto done;
  }

  ret = WRONG_FILE;

done:
  g_list_free_full (structs, (GDestroyNotify) gst_structure_free);
  return ret;
}

int
gst_validate_override_registry_preload (void)
{
  gchar **modlist, *const *modname;
  const char *sos, *loaderr;
  GModule *module;
  int ret, nloaded = 0;
  gpointer ext_create_overrides;
  GList *tmp, *overrides = gst_validate_get_config ("change-issue-severity");

  for (tmp = overrides; tmp; tmp = tmp->next)
    _add_override_from_struct (tmp->data);
  g_list_free (overrides);

  sos = g_getenv ("GST_VALIDATE_OVERRIDE");
  if (!sos) {
    GST_INFO ("No GST_VALIDATE_OVERRIDE found, no overrides to load");
    return 0;
  }

  modlist = g_strsplit (sos, G_SEARCHPATH_SEPARATOR_S, 0);
  for (modname = modlist; *modname; ++modname) {
    GST_INFO ("Loading overrides from %s", *modname);
    module = g_module_open (*modname, G_MODULE_BIND_LAZY);
    if (module == NULL) {

      if (_load_text_override_file (*modname) == WRONG_FILE) {
        loaderr = g_module_error ();
        GST_ERROR ("Failed to load %s %s", *modname,
            loaderr ? loaderr : "no idea why");
      }

      continue;
    }
    if (g_module_symbol (module, GST_VALIDATE_OVERRIDE_INIT_SYMBOL,
            &ext_create_overrides)) {
      ret = ((GstValidateCreateOverride) ext_create_overrides) ();
      if (ret > 0) {
        GST_INFO ("Loaded %d overrides from %s", ret, *modname);
        nloaded += ret;
      } else if (ret < 0) {
        GST_WARNING ("Error loading overrides from %s", *modname);
      } else {
        GST_INFO ("Loaded no overrides from %s", *modname);
      }
    } else {
      GST_WARNING (GST_VALIDATE_OVERRIDE_INIT_SYMBOL " not found in %s",
          *modname);
    }
    g_module_close (module);
  }
  g_strfreev (modlist);
  GST_INFO ("%d overrides loaded", nloaded);
  return nloaded;
}

/**
 * gst_validate_override_registry_get_override_for_names: (skip):
 */
GList *gst_validate_override_registry_get_override_for_names
    (GstValidateOverrideRegistry * reg, const gchar * name, ...)
{
  GList *iter;
  GList *ret = NULL;

  if (name) {
    va_list varargs;
    GstValidateOverrideRegistryNameEntry *entry;

    va_start (varargs, name);

    GST_VALIDATE_OVERRIDE_REGISTRY_LOCK (reg);
    while (name) {
      for (iter = reg->name_overrides.head; iter; iter = g_list_next (iter)) {
        entry = iter->data;
        if ((g_strcmp0 (name, entry->name)) == 0) {
          ret = g_list_prepend (ret, entry->override);
        }
      }
      name = va_arg (varargs, gchar *);
    }
    GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK (reg);

    va_end (varargs);
  }

  return ret;
}

/**
 * gst_validate_override_registry_get_override_list:
 * @registry: the override registry
 *
 * Returns: (transfer full) (element-type GstValidateOverride): a list of #GstValidateOverride
 */
GList *
gst_validate_override_registry_get_override_list (GstValidateOverrideRegistry *
    registry)
{
  GList *all_overrides = NULL;
  GList *i;

  GST_VALIDATE_OVERRIDE_REGISTRY_LOCK (registry);
  for (i = registry->name_overrides.head; i; i = i->next) {
    GstValidateOverrideRegistryNameEntry *entry =
        (GstValidateOverrideRegistryNameEntry *) i->data;
    if (!g_list_find (all_overrides, entry->override))
      all_overrides = g_list_append (all_overrides, entry->override);
  }
  for (i = registry->klass_overrides.head; i; i = i->next) {
    GstValidateOverrideRegistryNameEntry *entry =
        (GstValidateOverrideRegistryNameEntry *) i->data;
    if (!g_list_find (all_overrides, entry->override))
      all_overrides = g_list_append (all_overrides, entry->override);
  }
  for (i = registry->name_overrides.head; i; i = i->next) {
    GstValidateOverrideRegistryGTypeEntry *entry =
        (GstValidateOverrideRegistryGTypeEntry *) i->data;
    if (!g_list_find (all_overrides, entry->override))
      all_overrides = g_list_append (all_overrides, entry->override);
  }
  GST_VALIDATE_OVERRIDE_REGISTRY_UNLOCK (registry);

  return all_overrides;
}

void
_priv_validate_override_registry_deinit (void)
{
  GstValidateOverrideRegistry *reg = NULL;

  g_mutex_lock (&_gst_validate_override_registry_mutex);
  if (G_UNLIKELY (_registry_default)) {
    reg = _registry_default;
    _registry_default = NULL;
  }
  g_mutex_unlock (&_gst_validate_override_registry_mutex);

  if (reg)
    gst_validate_overide_registery_free (reg);
}
