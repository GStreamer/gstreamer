/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-override.c - Validate Override that allows customizing Validate behavior
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
/**
 * SECTION: gst-validate-override
 * @title: GstValidateOverride
 * @short_description: TODO
 *
 * TODO
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "gst-validate-internal.h"
#include "gst-validate-override.h"

/*  *INDENT-OFF* */

struct _GstValidateOverridePrivate
{
  GHashTable *level_override;
};

enum
{
  PROP_FIRST_PROP = 1,
  PROP_RUNNER,
  PROP_LAST
};

G_DEFINE_TYPE_WITH_CODE (GstValidateOverride, gst_validate_override,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstValidateOverride)
    G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, NULL))

/*  *INDENT-ON* */

static void
_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    case PROP_RUNNER:
      g_value_take_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object)));
      break;
    default:
      break;
  }
}

static void
_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (object),
          g_value_get_object (value));
      break;
    default:
      break;
  }
}

static void
gst_validate_override_finalize (GObject * object)
{
  GstValidateOverride *self = GST_VALIDATE_OVERRIDE (object);

  void (*chain_up) (GObject *) =
      ((GObjectClass *) gst_validate_override_parent_class)->finalize;

  g_hash_table_unref (self->priv->level_override);

  chain_up (object);
}

static void
gst_validate_override_class_init (GstValidateOverrideClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->finalize = gst_validate_override_finalize;

  oclass->get_property = _get_property;
  oclass->set_property = _set_property;

  g_object_class_install_property (oclass, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
gst_validate_override_init (GstValidateOverride * self)
{
  self->priv = gst_validate_override_get_instance_private (self);

  self->priv->level_override = g_hash_table_new (g_direct_hash, g_direct_equal);
}

GstValidateOverride *
gst_validate_override_new (void)
{
  return g_object_new (GST_TYPE_VALIDATE_OVERRIDE, NULL);
}

void
gst_validate_override_change_severity (GstValidateOverride * override,
    GstValidateIssueId issue_id, GstValidateReportLevel new_level)
{
  g_hash_table_insert (override->priv->level_override,
      GINT_TO_POINTER (issue_id), (gpointer) new_level);
}

/*
 * Also receives @default_level to preserve a custom level that might have
 * been set by a previous GstValidateOverride and should not go back to the
 * GstValidateIssue default
 */
GstValidateReportLevel
gst_validate_override_get_severity (GstValidateOverride * override,
    GstValidateIssueId issue_id, GstValidateReportLevel default_level)
{
  GstValidateReportLevel *level = NULL;

  if (g_hash_table_lookup_extended (override->priv->level_override,
          GINT_TO_POINTER (issue_id), NULL, (gpointer) & level)) {

    return GPOINTER_TO_INT (level);
  }
  return default_level;
}

/**
 * gst_validate_override_set_event_handler: (skip):
 */
void
gst_validate_override_set_event_handler (GstValidateOverride * override,
    GstValidateOverrideEventHandler handler)
{
  override->event_handler = handler;
}

/**
 * gst_validate_override_set_buffer_handler : (skip):
 */
void
gst_validate_override_set_buffer_handler (GstValidateOverride * override,
    GstValidateOverrideBufferHandler handler)
{
  override->buffer_handler = handler;
}

/**
 * gst_validate_override_set_query_handler: (skip):
 */
void
gst_validate_override_set_query_handler (GstValidateOverride * override,
    GstValidateOverrideQueryHandler handler)
{
  override->query_handler = handler;
}

/**
 * gst_validate_override_set_buffer_probe_handler: (skip):
 */
void
gst_validate_override_set_buffer_probe_handler (GstValidateOverride * override,
    GstValidateOverrideBufferHandler handler)
{
  override->buffer_probe_handler = handler;
}

/**
 * gst_validate_override_set_getcaps_handler: (skip):
 */
void
gst_validate_override_set_getcaps_handler (GstValidateOverride * override,
    GstValidateOverrideGetCapsHandler handler)
{
  override->getcaps_handler = handler;
}

/**
 * gst_validate_override_set_setcaps_handler: (skip):
 */
void
gst_validate_override_set_setcaps_handler (GstValidateOverride * override,
    GstValidateOverrideSetCapsHandler handler)
{
  override->setcaps_handler = handler;
}

/**
 * gst_validate_override_event_handler: (skip):
 */
void
gst_validate_override_event_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstEvent * event)
{
  if (override->event_handler)
    override->event_handler (override, monitor, event);
}

/**
 * gst_validate_override_buffer_handler: (skip):
 */
void
gst_validate_override_buffer_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstBuffer * buffer)
{
  if (override->buffer_handler)
    override->buffer_handler (override, monitor, buffer);
}

/**
 * gst_validate_override_query_handler: (skip):
 */
void
gst_validate_override_query_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstQuery * query)
{
  if (override->query_handler)
    override->query_handler (override, monitor, query);
}

/**
 * gst_validate_override_buffer_probe_handler: (skip):
 */
void
gst_validate_override_buffer_probe_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstBuffer * buffer)
{
  if (override->buffer_probe_handler)
    override->buffer_probe_handler (override, monitor, buffer);
}

/**
 * gst_validate_override_getcaps_handler: (skip):
 */
void
gst_validate_override_getcaps_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstCaps * caps)
{
  if (override->getcaps_handler)
    override->getcaps_handler (override, monitor, caps);
}

/**
 * gst_validate_override_setcaps_handler: (skip):
 */
void
gst_validate_override_setcaps_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstCaps * caps)
{
  if (override->setcaps_handler)
    override->setcaps_handler (override, monitor, caps);
}

/**
 * gst_validate_override_element_added_handler: (skip):
 */
void
gst_validate_override_element_added_handler (GstValidateOverride * override,
    GstValidateMonitor * monitor, GstElement * child)
{
  if (override->element_added_handler)
    override->element_added_handler (override, monitor, child);
}

/**
 * gst_validate_override_set_element_added_handler: (skip):
 */
void
gst_validate_override_set_element_added_handler (GstValidateOverride * override,
    GstValidateOverrideElementAddedHandler func)
{
  override->element_added_handler = func;
}

/**
 * gst_validate_override_can_attach: (skip):
 */
gboolean
gst_validate_override_can_attach (GstValidateOverride * override,
    GstValidateMonitor * monitor)
{
  GstValidateOverrideClass *klass = GST_VALIDATE_OVERRIDE_GET_CLASS (override);

  if (klass->can_attach)
    return klass->can_attach (override, monitor);

  return TRUE;
}

void
gst_validate_override_attached (GstValidateOverride * override)
{
  GstValidateOverrideClass *klass = GST_VALIDATE_OVERRIDE_GET_CLASS (override);

  if (klass->attached)
    klass->attached (override);
}
