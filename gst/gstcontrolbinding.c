/* GStreamer
 *
 * Copyright (C) 2011 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstcontrolbinding.c: Attachment for control sources
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:gstcontrolbinding
 * @short_description: attachment for control source sources
 *
 * A value mapping object that attaches control sources to gobject properties.
 */

#include "gst_private.h"

#include <glib-object.h>
#include <gst/gst.h>

#include "gstcontrolbinding.h"

#define GST_CAT_DEFAULT control_binding_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void gst_control_binding_dispose (GObject * object);
static void gst_control_binding_finalize (GObject * object);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gstcontrolbinding", 0, \
      "dynamic parameter control source attachment");

G_DEFINE_TYPE_WITH_CODE (GstControlBinding, gst_control_binding, G_TYPE_OBJECT,
    _do_init);

static void
gst_control_binding_class_init (GstControlBindingClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_control_binding_dispose;
  gobject_class->finalize = gst_control_binding_finalize;
}

static void
gst_control_binding_init (GstControlBinding * self)
{
}

static void
gst_control_binding_dispose (GObject * object)
{
  GstControlBinding *self = GST_CONTROL_BINDING (object);

  if (self->csource) {
    // FIXME: hack
    self->csource->bound = FALSE;
    g_object_unref (self->csource);
    self->csource = NULL;
  }
}

static void
gst_control_binding_finalize (GObject * object)
{
  GstControlBinding *self = GST_CONTROL_BINDING (object);

  g_value_unset (&self->cur_value);
  g_value_unset (&self->last_value);
}

/**
 * gst_control_binding_new:
 * @object: the object of the property
 * @property_name: the property-name to attach the control source
 * @csource: the control source
 *
 * Create a new control-binding that attaches the #GstControlSource to the
 * #GObject property.
 *
 * Returns: the new #GstControlBinding
 */
GstControlBinding *
gst_control_binding_new (GstObject * object, const gchar * property_name,
    GstControlSource * csource)
{
  GstControlBinding *self = NULL;
  GParamSpec *pspec;

  GST_INFO_OBJECT (object, "trying to put property '%s' under control",
      property_name);

  /* check if the object has a property of that name */
  if ((pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object),
              property_name))) {
    GST_DEBUG_OBJECT (object, "  psec->flags : 0x%08x", pspec->flags);

    /* check if this param is witable && controlable && !construct-only */
    g_return_val_if_fail ((pspec->flags & (G_PARAM_WRITABLE |
                GST_PARAM_CONTROLLABLE | G_PARAM_CONSTRUCT_ONLY)) ==
        (G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE), NULL);

    if (gst_control_source_bind (csource, pspec)) {
      if ((self = (GstControlBinding *) g_object_newv (GST_TYPE_CONTROL_BINDING,
                  0, NULL))) {
        self->pspec = pspec;
        self->name = pspec->name;
        self->csource = g_object_ref (csource);
        self->disabled = FALSE;

        g_value_init (&self->cur_value, G_PARAM_SPEC_VALUE_TYPE (pspec));
        g_value_init (&self->last_value, G_PARAM_SPEC_VALUE_TYPE (pspec));
      }
    }
  } else {
    GST_WARNING_OBJECT (object, "class '%s' has no property '%s'",
        G_OBJECT_TYPE_NAME (object), property_name);
  }
  return self;
}

/* functions */

/**
 * gst_control_binding_sync_values:
 * @self: the control binding
 * @object: the object that has controlled properties
 * @timestamp: the time that should be processed
 * @last_sync: the last time this was called
 *
 * Sets the property of the @object, according to the #GstControlSources that
 * handle them and for the given timestamp.
 *
 * If this function fails, it is most likely the application developers fault.
 * Most probably the control sources are not setup correctly.
 *
 * Returns: %TRUE if the controller value could be applied to the object
 * property, %FALSE otherwise
 */
gboolean
gst_control_binding_sync_values (GstControlBinding * self, GstObject * object,
    GstClockTime timestamp, GstClockTime last_sync)
{
  GValue *value;
  gboolean ret;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), FALSE);

  if (self->disabled)
    return TRUE;

  GST_LOG_OBJECT (object, "property '%s' at ts=%" G_GUINT64_FORMAT,
      self->name, timestamp);

  value = &self->cur_value;
  ret = gst_control_source_get_value (self->csource, timestamp, value);
  if (G_LIKELY (ret)) {
    /* always set the value for first time, but then only if it changed
     * this should limit g_object_notify invocations.
     * FIXME: can we detect negative playback rates?
     */
    if ((timestamp < last_sync) ||
        gst_value_compare (value, &self->last_value) != GST_VALUE_EQUAL) {
      /* we can make this faster
       * http://bugzilla.gnome.org/show_bug.cgi?id=536939
       */
      g_object_set_property ((GObject *) object, self->name, value);
      g_value_copy (value, &self->last_value);
    }
  } else {
    GST_DEBUG_OBJECT (object, "no control value for param %s", self->name);
  }
  return (ret);
}

/**
 * gst_control_binding_get_control_source:
 * @self: the control binding
 *
 * Get the control source.
 *
 * Returns: the control source. Unref when done with it.
 */
GstControlSource *
gst_control_binding_get_control_source (GstControlBinding * self)
{
  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), NULL);
  return g_object_ref (self->csource);
}

/**
 * gst_control_binding_set_disabled:
 * @self: the control binding
 * @disabled: boolean that specifies whether to disable the controller
 * or not.
 *
 * This function is used to disable a control binding for some time, i.e.
 * gst_object_sync_values() will do nothing.
 */
void
gst_control_binding_set_disabled (GstControlBinding * self, gboolean disabled)
{
  g_return_if_fail (GST_IS_CONTROL_BINDING (self));
  self->disabled = disabled;
}

/**
 * gst_control_binding_is_disabled:
 * @self: the control binding
 *
 * Check if the control binding is disabled.
 *
 * Returns: %TRUE if the binding is inactive
 */
gboolean
gst_control_binding_is_disabled (GstControlBinding * self)
{
  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), TRUE);
  return (self->disabled == TRUE);
}
