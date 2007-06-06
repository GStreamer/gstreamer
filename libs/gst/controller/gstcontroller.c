/* GStreamer
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dot net>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstcontroller.c: dynamic parameter control subsystem
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
 * SECTION:gstcontroller
 * @short_description: dynamic parameter control subsystem
 *
 * The controller subsystem offers a lightweight way to adjust gobject
 * properties over stream-time. It works by using time-stamped value pairs that
 * are queued for element-properties. At run-time the elements continously pull
 * values changes for the current stream-time.
 *
 * What needs to be changed in a #GstElement?
 * Very little - it is just two steps to make a plugin controllable!
 * <orderedlist>
 *   <listitem><para>
 *     mark gobject-properties paramspecs that make sense to be controlled,
 *     by GST_PARAM_CONTROLLABLE.
 *   </para></listitem>
 *   <listitem><para>
 *     when processing data (get, chain, loop function) at the beginning call
 *     gst_object_sync_values(element,timestamp).
 *     This will made the controller to update all gobject properties that are under
 *     control with the current values based on timestamp.
 *   </para></listitem>
 * </orderedlist>
 *
 * What needs to be done in applications?
 * Again its not a lot to change.
 * <orderedlist>
 *   <listitem><para>
 *     first put some properties under control, by calling
 *     controller = g_object_control_properties(object, "prop1", "prop2",...);
 *   </para></listitem>
 *   <listitem><para>
 *     set how the controller will smooth inbetween values.
 *     gst_controller_set_interpolation_mode(controller,"prop1",mode);
 *   </para></listitem>
 *   <listitem><para>
 *     set key values
 *     gst_controller_set (controller, "prop1" ,0 * GST_SECOND, value1);
 *     gst_controller_set (controller, "prop1" ,1 * GST_SECOND, value2);
 *   </para></listitem>
 *   <listitem><para>
 *     start your pipeline
 *   </para></listitem>
 * </orderedlist>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include "gstcontrollerprivate.h"
#include "gstcontroller.h"

#define GST_CAT_DEFAULT gst_controller_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_DEFAULT);

static GObjectClass *parent_class = NULL;
GQuark __gst_controller_key;

/* property ids */
enum
{
  PROP_CONTROL_RATE = 1
};

struct _GstControllerPrivate
{
  GstClockTime control_rate;
  GstClockTime last_sync;
};

/* imports from gst-interpolation.c */

extern GList
    * gst_controlled_property_find_control_point_node (GstControlledProperty *
    prop, GstClockTime timestamp);
extern GstInterpolateMethod *interpolation_methods[];
extern guint num_interpolation_methods;

/* callbacks */

void
on_object_controlled_property_changed (const GObject * object, GParamSpec * arg,
    gpointer user_data)
{
  GstControlledProperty *prop = GST_CONTROLLED_PROPERTY (user_data);
  GstController *ctrl;

  GST_LOG ("notify for '%s'", prop->name);

  ctrl = g_object_get_qdata (G_OBJECT (object), __gst_controller_key);
  g_return_if_fail (ctrl);

  if (g_mutex_trylock (ctrl->lock)) {
    if (!G_IS_VALUE (&prop->live_value.value)) {
      g_value_init (&prop->live_value.value, prop->type);
    }
    g_object_get_property (G_OBJECT (object), prop->name,
        &prop->live_value.value);
    prop->live_value.timestamp = prop->last_value.timestamp;
    g_mutex_unlock (ctrl->lock);
    GST_DEBUG ("-> is live update : ts=%" G_GUINT64_FORMAT,
        prop->live_value.timestamp);
  }
}

/* helper */

/*
 * gst_control_point_compare:
 * @p1: a pointer to a #GstControlPoint
 * @p2: a pointer to a #GstControlPoint
 *
 * Compare function for g_list operations that operates on two #GstControlPoint
 * parameters.
 */
static gint
gst_control_point_compare (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstControlPoint *) p1)->timestamp;
  GstClockTime ct2 = ((GstControlPoint *) p2)->timestamp;

  return ((ct1 < ct2) ? -1 : ((ct1 == ct2) ? 0 : 1));
/* this does not produce an gint :(
  return ((ct1 - ct2));
*/
}

/*
 * gst_control_point_find:
 * @p1: a pointer to a #GstControlPoint
 * @p2: a pointer to a #GstClockTime
 *
 * Compare function for g_list operations that operates on a #GstControlPoint and
 * a #GstClockTime.
 */
static gint
gst_control_point_find (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstControlPoint *) p1)->timestamp;
  GstClockTime ct2 = *(GstClockTime *) p2;

  return ((ct1 < ct2) ? -1 : ((ct1 == ct2) ? 0 : 1));
/* this does not produce an gint :(
  return ((ct1 - ct2));
*/
}

/*
 * gst_controlled_property_set_interpolation_mode:
 * @self: the controlled property object to change
 * @mode: the new interpolation mode
 *
 * Sets the given Interpolation mode for the controlled property and activates
 * the respective interpolation hooks.
 *
 * Returns: %TRUE for success
 */
static gboolean
gst_controlled_property_set_interpolation_mode (GstControlledProperty * self,
    GstInterpolateMode mode)
{
  gboolean res = TRUE;

  if (mode >= num_interpolation_methods || interpolation_methods[mode] == NULL) {
    GST_WARNING ("interpolation mode %d invalid or not implemented yet", mode);
    return FALSE;
  }

  self->interpolation = mode;
  if (mode != GST_INTERPOLATE_USER) {
    switch (self->base) {
      case G_TYPE_INT:
        self->get = interpolation_methods[mode]->get_int;
        self->get_value_array =
            interpolation_methods[mode]->get_int_value_array;
        break;
      case G_TYPE_UINT:
        self->get = interpolation_methods[mode]->get_uint;
        self->get_value_array =
            interpolation_methods[mode]->get_uint_value_array;
        break;
      case G_TYPE_LONG:
        self->get = interpolation_methods[mode]->get_long;
        self->get_value_array =
            interpolation_methods[mode]->get_long_value_array;
        break;
      case G_TYPE_ULONG:
        self->get = interpolation_methods[mode]->get_ulong;
        self->get_value_array =
            interpolation_methods[mode]->get_ulong_value_array;
        break;
      case G_TYPE_FLOAT:
        self->get = interpolation_methods[mode]->get_float;
        self->get_value_array =
            interpolation_methods[mode]->get_float_value_array;
        break;
      case G_TYPE_DOUBLE:
        self->get = interpolation_methods[mode]->get_double;
        self->get_value_array =
            interpolation_methods[mode]->get_double_value_array;
        break;
      case G_TYPE_BOOLEAN:
        self->get = interpolation_methods[mode]->get_boolean;
        self->get_value_array =
            interpolation_methods[mode]->get_boolean_value_array;
        break;
      case G_TYPE_ENUM:
        self->get = interpolation_methods[mode]->get_uint;
        self->get_value_array =
            interpolation_methods[mode]->get_enum_value_array;
        break;
      case G_TYPE_STRING:
        self->get = interpolation_methods[mode]->get_string;
        self->get_value_array =
            interpolation_methods[mode]->get_string_value_array;
        break;
      default:
        self->get = NULL;
        self->get_value_array = NULL;
    }
    if (!self->get || !self->get_value_array) {
      GST_WARNING ("incomplete implementation for type %lu/%lu:'%s'/'%s'",
          self->type, self->base,
          g_type_name (self->type), g_type_name (self->base));
      res = FALSE;
    }
    if (mode == GST_INTERPOLATE_QUADRATIC) {
      GST_WARNING ("Quadratic interpolation mode is deprecated, using cubic"
          "interpolation mode");
    }
  } else {
    /* TODO shouldn't this also get a GstInterpolateMethod *user_method
       for the case mode==GST_INTERPOLATE_USER
     */
    res = FALSE;
  }

  self->valid_cache = FALSE;

  return (res);
}

static void
gst_controlled_property_prepend_default (GstControlledProperty * prop)
{
  GstControlPoint *cp = g_new0 (GstControlPoint, 1);

  cp->timestamp = 0;
  g_value_init (&cp->value, prop->type);
  g_value_copy (&prop->default_value, &cp->value);
  prop->values = g_list_prepend (prop->values, cp);
  prop->nvalues++;
}

/*
 * gst_controlled_property_new:
 * @object: for which object the controlled property should be set up
 * @name: the name of the property to be controlled
 *
 * Private method which initializes the fields of a new controlled property
 * structure.
 *
 * Returns: a freshly allocated structure or %NULL
 */
static GstControlledProperty *
gst_controlled_property_new (GObject * object, const gchar * name)
{
  GstControlledProperty *prop = NULL;
  GParamSpec *pspec;

  GST_INFO ("trying to put property '%s' under control", name);

  /* check if the object has a property of that name */
  if ((pspec =
          g_object_class_find_property (G_OBJECT_GET_CLASS (object), name))) {
    GST_DEBUG ("  psec->flags : 0x%08x", pspec->flags);

    /* check if this param is witable */
    g_return_val_if_fail ((pspec->flags & G_PARAM_WRITABLE), NULL);
    /* check if property is controlable */
    g_return_val_if_fail ((pspec->flags & GST_PARAM_CONTROLLABLE), NULL);
    /* check if this param is not construct-only */
    g_return_val_if_fail (!(pspec->flags & G_PARAM_CONSTRUCT_ONLY), NULL);

    /* TODO do sanity checks
       we don't control some pspec->value_type:
       G_TYPE_PARAM_BOXED
       G_TYPE_PARAM_ENUM
       G_TYPE_PARAM_FLAGS
       G_TYPE_PARAM_OBJECT
       G_TYPE_PARAM_PARAM
       G_TYPE_PARAM_POINTER
       G_TYPE_PARAM_STRING
     */

    if ((prop = g_new0 (GstControlledProperty, 1))) {
      gchar *signal_name;
      GType base;

      prop->name = pspec->name; /* so we don't use the same mem twice */
      prop->type = G_PARAM_SPEC_VALUE_TYPE (pspec);
      /* get the fundamental base type */
      prop->base = prop->type;
      while ((base = g_type_parent (prop->base))) {
        prop->base = base;
      }
      /* initialize mode specific accessor callbacks */
      if (!gst_controlled_property_set_interpolation_mode (prop,
              GST_INTERPOLATE_NONE))
        goto Error;
      /* prepare our gvalues */
      g_value_init (&prop->default_value, prop->type);
      g_value_init (&prop->result_value, prop->type);
      g_value_init (&prop->last_value.value, prop->type);
      switch (prop->base) {
        case G_TYPE_INT:{
          GParamSpecInt *tpspec = G_PARAM_SPEC_INT (pspec);

          g_value_set_int (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_UINT:{
          GParamSpecUInt *tpspec = G_PARAM_SPEC_UINT (pspec);

          g_value_set_uint (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_LONG:{
          GParamSpecLong *tpspec = G_PARAM_SPEC_LONG (pspec);

          g_value_set_long (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_ULONG:{
          GParamSpecULong *tpspec = G_PARAM_SPEC_ULONG (pspec);

          g_value_set_ulong (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_FLOAT:{
          GParamSpecFloat *tpspec = G_PARAM_SPEC_FLOAT (pspec);

          g_value_set_float (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_DOUBLE:{
          GParamSpecDouble *tpspec = G_PARAM_SPEC_DOUBLE (pspec);

          g_value_set_double (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_BOOLEAN:{
          GParamSpecBoolean *tpspec = G_PARAM_SPEC_BOOLEAN (pspec);

          g_value_set_boolean (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_ENUM:{
          GParamSpecEnum *tpspec = G_PARAM_SPEC_ENUM (pspec);

          g_value_set_enum (&prop->default_value, tpspec->default_value);
        }
          break;
        case G_TYPE_STRING:{
          GParamSpecString *tpspec = G_PARAM_SPEC_STRING (pspec);

          g_value_set_string (&prop->default_value, tpspec->default_value);
        }
          break;
        default:
          GST_WARNING ("incomplete implementation for paramspec type '%s'",
              G_PARAM_SPEC_TYPE_NAME (pspec));
      }

      prop->valid_cache = FALSE;
      prop->nvalues = 0;

      /* Add a control point at timestamp 0 with the default value
       * to make the life of interpolators easier. */
      gst_controlled_property_prepend_default (prop);

      signal_name = g_alloca (8 + 1 + strlen (name));
      g_sprintf (signal_name, "notify::%s", name);
      prop->notify_handler_id =
          g_signal_connect (object, signal_name,
          G_CALLBACK (on_object_controlled_property_changed), (gpointer) prop);
    }
  } else {
    GST_WARNING ("class '%s' has no property '%s'", G_OBJECT_TYPE_NAME (object),
        name);
  }
  return (prop);
Error:
  if (prop)
    g_free (prop);
  return (NULL);
}

/*
 * gst_control_point_free:
 * @prop: the object to free
 *
 * Private method which frees all data allocated by a #GstControlPoint
 * instance.
 */
static void
gst_control_point_free (GstControlPoint * cp)
{
  g_return_if_fail (cp);

  g_value_unset (&cp->value);
  g_free (cp);
}

/*

 * gst_controlled_property_free:
 * @prop: the object to free
 *
 * Private method which frees all data allocated by a #GstControlledProperty
 * instance.
 */
static void
gst_controlled_property_free (GstControlledProperty * prop)
{
  g_list_foreach (prop->values, (GFunc) gst_control_point_free, NULL);
  g_list_free (prop->values);

  g_value_unset (&prop->default_value);
  g_value_unset (&prop->result_value);
  g_value_unset (&prop->last_value.value);
  if (G_IS_VALUE (&prop->live_value.value))
    g_value_unset (&prop->live_value.value);

  g_free (prop);
}

/*
 * gst_controller_find_controlled_property:
 * @self: the controller object to search for a property in
 * @name: the gobject property name to look for
 *
 * Searches the list of properties under control.
 *
 * Returns: a #GstControlledProperty object of %NULL if the property is not
 * being controlled.
 */
static GstControlledProperty *
gst_controller_find_controlled_property (GstController * self,
    const gchar * name)
{
  GstControlledProperty *prop;
  GList *node;

  for (node = self->properties; node; node = g_list_next (node)) {
    prop = node->data;
    if (!strcmp (prop->name, name)) {
      return (prop);
    }
  }
  GST_DEBUG ("controller does not (yet) manage property '%s'", name);

  return (NULL);
}

/* methods */

/**
 * gst_controller_new_valist:
 * @object: the object of which some properties should be controlled
 * @var_args: %NULL terminated list of property names that should be controlled
 *
 * Creates a new GstController for the given object's properties
 *
 * Returns: the new controller.
 */
GstController *
gst_controller_new_valist (GObject * object, va_list var_args)
{
  GstController *self;
  GstControlledProperty *prop;
  gboolean ref_existing = TRUE;
  gchar *name;

  g_return_val_if_fail (G_IS_OBJECT (object), NULL);

  GST_INFO ("setting up a new controller");

  /* TODO should this method check if the given object implements GstParent and
     if so instantiate a GstParentController ?

     BilboEd: This is too specific to be put here, don't we want
     GstController to be as generic as possible ?

     Ensonic: So we will have gst_parent_controller_new as well and maybe a
     convinience function that automatically chooses the right one (how to name it)?
     GstParent will be in core after all.
   */

  self = g_object_get_qdata (object, __gst_controller_key);
  /* create GstControlledProperty for each property */
  while ((name = va_arg (var_args, gchar *))) {
    /* test if this property isn't yet controlled */
    if (!self || !(prop = gst_controller_find_controlled_property (self, name))) {
      /* create GstControlledProperty and add to self->propeties List */
      if ((prop = gst_controlled_property_new (object, name))) {
        /* if we don't have a controller object yet, now is the time to create one */
        if (!self) {
          self = g_object_new (GST_TYPE_CONTROLLER, NULL);
          self->object = g_object_ref (object);
          /* store the controller */
          g_object_set_qdata (object, __gst_controller_key, self);
          ref_existing = FALSE;
        } else {
          /* only want one single _ref(), even for multiple properties */
          if (ref_existing) {
            g_object_ref (self);
            ref_existing = FALSE;
            GST_INFO ("returning existing controller");
          }
        }
        self->properties = g_list_prepend (self->properties, prop);
      }
    } else {
      GST_WARNING ("trying to control property again");
      if (ref_existing) {
        g_object_ref (self);
        ref_existing = FALSE;
      }
    }
  }
  va_end (var_args);

  if (self)
    GST_INFO ("controller->ref_count=%d", G_OBJECT (self)->ref_count);
  return (self);
}

/**
 * gst_controller_new_list:
 * @object: the object of which some properties should be controlled
 * @list: list of property names that should be controlled
 *
 * Creates a new GstController for the given object's properties
 *
 * Returns: the new controller.
 */
GstController *
gst_controller_new_list (GObject * object, GList * list)
{
  GstController *self;
  GstControlledProperty *prop;
  gboolean ref_existing = TRUE;
  gchar *name;
  GList *node;

  g_return_val_if_fail (G_IS_OBJECT (object), NULL);

  GST_INFO ("setting up a new controller");

  self = g_object_get_qdata (object, __gst_controller_key);
  /* create GstControlledProperty for each property */
  for (node = list; node; node = g_list_next (node)) {
    name = (gchar *) node->data;
    /* test if this property isn't yet controlled */
    if (!self || !(prop = gst_controller_find_controlled_property (self, name))) {
      /* create GstControlledProperty and add to self->propeties List */
      if ((prop = gst_controlled_property_new (object, name))) {
        /* if we don't have a controller object yet, now is the time to create one */
        if (!self) {
          self = g_object_new (GST_TYPE_CONTROLLER, NULL);
          self->object = g_object_ref (object);
          /* store the controller */
          g_object_set_qdata (object, __gst_controller_key, self);
          ref_existing = FALSE;
        } else {
          /* only want one single _ref(), even for multiple properties */
          if (ref_existing) {
            g_object_ref (self);
            ref_existing = FALSE;
            GST_INFO ("returning existing controller");
          }
        }
        self->properties = g_list_prepend (self->properties, prop);
      }
    } else {
      GST_WARNING ("trying to control property again");
      if (ref_existing) {
        g_object_ref (self);
        ref_existing = FALSE;
      }
    }
  }

  if (self)
    GST_INFO ("controller->ref_count=%d", G_OBJECT (self)->ref_count);
  return (self);
}

/**
 * gst_controller_new:
 * @object: the object of which some properties should be controlled
 * @...: %NULL terminated list of property names that should be controlled
 *
 * Creates a new GstController for the given object's properties
 *
 * Returns: the new controller.
 */
GstController *
gst_controller_new (GObject * object, ...)
{
  GstController *self;
  va_list var_args;

  g_return_val_if_fail (G_IS_OBJECT (object), NULL);

  va_start (var_args, object);
  self = gst_controller_new_valist (object, var_args);
  va_end (var_args);

  return (self);
}

/**
 * gst_controller_remove_properties_valist:
 * @self: the controller object from which some properties should be removed
 * @var_args: %NULL terminated list of property names that should be removed
 *
 * Removes the given object properties from the controller
 *
 * Returns: %FALSE if one of the given property isn't handled by the controller, %TRUE otherwise
 */
gboolean
gst_controller_remove_properties_valist (GstController * self, va_list var_args)
{
  gboolean res = TRUE;
  GstControlledProperty *prop;
  gchar *name;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);

  while ((name = va_arg (var_args, gchar *))) {
    /* find the property in the properties list of the controller, remove and free it */
    g_mutex_lock (self->lock);
    if ((prop = gst_controller_find_controlled_property (self, name))) {
      self->properties = g_list_remove (self->properties, prop);
      g_signal_handler_disconnect (self->object, prop->notify_handler_id);
      gst_controlled_property_free (prop);
    } else {
      res = FALSE;
    }
    g_mutex_unlock (self->lock);
  }

  return (res);
}

/**
 * gst_controller_remove_properties_list:
 * @self: the controller object from which some properties should be removed
 * @list: #GList of property names that should be removed
 *
 * Removes the given object properties from the controller
 *
 * Returns: %FALSE if one of the given property isn't handled by the controller, %TRUE otherwise
 */
gboolean
gst_controller_remove_properties_list (GstController * self, GList * list)
{
  gboolean res = TRUE;
  GstControlledProperty *prop;
  gchar *name;
  GList *tmp;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);

  for (tmp = list; tmp; tmp = g_list_next (tmp)) {
    name = (gchar *) tmp->data;

    /* find the property in the properties list of the controller, remove and free it */
    g_mutex_lock (self->lock);
    if ((prop = gst_controller_find_controlled_property (self, name))) {
      self->properties = g_list_remove (self->properties, prop);
      g_signal_handler_disconnect (self->object, prop->notify_handler_id);
      gst_controlled_property_free (prop);
    } else {
      res = FALSE;
    }
    g_mutex_unlock (self->lock);
  }

  return (res);
}

/**
 * gst_controller_remove_properties:
 * @self: the controller object from which some properties should be removed
 * @...: %NULL terminated list of property names that should be removed
 *
 * Removes the given object properties from the controller
 *
 * Returns: %FALSE if one of the given property isn't handled by the controller, %TRUE otherwise
 */
gboolean
gst_controller_remove_properties (GstController * self, ...)
{
  gboolean res;
  va_list var_args;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);

  va_start (var_args, self);
  res = gst_controller_remove_properties_valist (self, var_args);
  va_end (var_args);

  return (res);
}

static gboolean
gst_controller_set_unlocked (GstController * self, GstControlledProperty * prop,
    GstClockTime timestamp, GValue * value)
{
  gboolean res = FALSE;

  if (G_VALUE_TYPE (value) == prop->type) {
    GstControlPoint *cp;
    GList *node;

    /* check if a control point for the timestamp already exists */
    if ((node = g_list_find_custom (prop->values, &timestamp,
                gst_control_point_find))) {
      cp = node->data;
      g_value_reset (&cp->value);
      g_value_copy (value, &cp->value);
    } else {
      /* create a new GstControlPoint */
      cp = g_new0 (GstControlPoint, 1);
      cp->timestamp = timestamp;
      g_value_init (&cp->value, prop->type);
      g_value_copy (value, &cp->value);
      /* and sort it into the prop->values list */
      prop->values =
          g_list_insert_sorted (prop->values, cp, gst_control_point_compare);
      prop->nvalues++;
    }
    prop->valid_cache = FALSE;
    res = TRUE;
  } else {
    GST_WARNING ("incompatible value type for property '%s'", prop->name);
  }

  return res;
}

/**
 * gst_controller_set:
 * @self: the controller object which handles the properties
 * @property_name: the name of the property to set
 * @timestamp: the time the control-change is schedules for
 * @value: the control-value
 *
 * Set the value of given controller-handled property at a certain time.
 *
 * Returns: FALSE if the values couldn't be set (ex : properties not handled by controller), TRUE otherwise
 */
gboolean
gst_controller_set (GstController * self, gchar * property_name,
    GstClockTime timestamp, GValue * value)
{
  gboolean res = FALSE;
  GstControlledProperty *prop;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (property_name, FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    res = gst_controller_set_unlocked (self, prop, timestamp, value);
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/**
 * gst_controller_set_from_list:
 * @self: the controller object which handles the properties
 * @property_name: the name of the property to set
 * @timedvalues: a list with #GstTimedValue items
 *
 * Sets multiple timed values at once.
 *
 * Returns: %FALSE if the values couldn't be set (ex : properties not handled by controller), %TRUE otherwise
 */

gboolean
gst_controller_set_from_list (GstController * self, gchar * property_name,
    GSList * timedvalues)
{
  gboolean res = FALSE;
  GstControlledProperty *prop;
  GSList *node;
  GstTimedValue *tv;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (property_name, FALSE);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    for (node = timedvalues; node; node = g_slist_next (node)) {
      tv = node->data;
      if (!GST_CLOCK_TIME_IS_VALID (tv->timestamp)) {
        GST_WARNING ("GstTimedValued with invalid timestamp passed to %s "
            "for property '%s'", GST_FUNCTION, property_name);
      } else if (!G_IS_VALUE (&tv->value)) {
        GST_WARNING ("GstTimedValued with invalid value passed to %s "
            "for property '%s'", GST_FUNCTION, property_name);
      } else {
        res =
            gst_controller_set_unlocked (self, prop, tv->timestamp, &tv->value);
      }
    }
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/**
 * gst_controller_unset:
 * @self: the controller object which handles the properties
 * @property_name: the name of the property to unset
 * @timestamp: the time the control-change should be removed from
 *
 * Used to remove the value of given controller-handled property at a certain
 * time.
 *
 * Returns: %FALSE if the values couldn't be unset (ex : properties not handled by controller), %TRUE otherwise
 */
gboolean
gst_controller_unset (GstController * self, gchar * property_name,
    GstClockTime timestamp)
{
  gboolean res = FALSE;
  GstControlledProperty *prop;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (property_name, FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    GList *node;

    /* check if a control point for the timestamp exists */
    if ((node = g_list_find_custom (prop->values, &timestamp,
                gst_control_point_find))) {
      GstControlPoint *cp = node->data;

      if (cp->timestamp == 0) {
        /* Restore the default node */
        g_value_reset (&cp->value);
        g_value_copy (&prop->default_value, &cp->value);
      } else {
        if (node == prop->last_requested_value)
          prop->last_requested_value = NULL;
        gst_control_point_free (node->data);    /* free GstControlPoint */
        prop->values = g_list_delete_link (prop->values, node);
        prop->nvalues--;
      }
      prop->valid_cache = FALSE;
      res = TRUE;
    }
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/**
 * gst_controller_unset_all:
 * @self: the controller object which handles the properties
 * @property_name: the name of the property to unset
 *
 * Used to remove all time-stamped values of given controller-handled property
 *
 * Returns: %FALSE if the values couldn't be unset (ex : properties not handled
 * by controller), %TRUE otherwise
 * Since: 0.10.5
 */
gboolean
gst_controller_unset_all (GstController * self, gchar * property_name)
{
  gboolean res = FALSE;
  GstControlledProperty *prop;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (property_name, FALSE);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    /* free GstControlPoint structures */
    g_list_foreach (prop->values, (GFunc) gst_control_point_free, NULL);
    g_list_free (prop->values);
    prop->last_requested_value = NULL;
    prop->values = NULL;
    prop->nvalues = 0;
    prop->valid_cache = FALSE;

    /* Insert the default control point again */
    gst_controlled_property_prepend_default (prop);

    res = TRUE;
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/**
 * gst_controller_get:
 * @self: the controller object which handles the properties
 * @property_name: the name of the property to get
 * @timestamp: the time the control-change should be read from
 *
 * Gets the value for the given controller-handled property at the requested
 * time.
 *
 * Returns: the GValue of the property at the given time, or %NULL if the property isn't handled by the controller
 */
GValue *
gst_controller_get (GstController * self, gchar * property_name,
    GstClockTime timestamp)
{
  GstControlledProperty *prop;
  GValue *val = NULL;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), NULL);
  g_return_val_if_fail (property_name, NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), NULL);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    /* get current value via interpolator */
    val = prop->get (prop, timestamp);
  }
  g_mutex_unlock (self->lock);

  return (val);
}

/**
 * gst_controller_get_all:
 * @self: the controller to get the list from
 * @property_name: the name of the property to get the list for
 *
 * Returns a read-only copy of the list of GstTimedValue for the given property.
 * Free the list after done with it.
 *
 * <note><para>This doesn't modify the controlled GObject property!</para></note>
 *
 * Returns: a copy of the list, or %NULL if the property isn't handled by the controller
 */
const GList *
gst_controller_get_all (GstController * self, gchar * property_name)
{
  GList *res = NULL;
  GstControlledProperty *prop;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), NULL);
  g_return_val_if_fail (property_name, NULL);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    res = g_list_copy (prop->values);
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/**
 * gst_controller_suggest_next_sync:
 * @self: the controller that handles the values
 *
 * Returns a suggestion for timestamps where buffers should be split
 * to get best controller results.
 *
 * Returns: Returns the suggested timestamp or %GST_CLOCK_TIME_NONE
 * if no control-rate was set.
 *
 * Since: 0.10.13
 */
GstClockTime
gst_controller_suggest_next_sync (GstController * self)
{
  GstClockTime ret;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (self->priv->control_rate != GST_CLOCK_TIME_NONE,
      GST_CLOCK_TIME_NONE);

  g_mutex_lock (self->lock);

  /* TODO: Implement more logic, depending on interpolation mode
   * and control points */
  ret = self->priv->last_sync + self->priv->control_rate;

  g_mutex_unlock (self->lock);

  return ret;
}

/**
 * gst_controller_sync_values:
 * @self: the controller that handles the values
 * @timestamp: the time that should be processed
 *
 * Sets the properties of the element, according to the controller that (maybe)
 * handles them and for the given timestamp.
 *
 * Returns: %TRUE if the controller values could be applied to the object
 * properties, %FALSE otherwise
 */
gboolean
gst_controller_sync_values (GstController * self, GstClockTime timestamp)
{
  GstControlledProperty *prop;
  GList *node;
  GValue *value;
  gboolean live = FALSE;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);

  GST_LOG ("sync_values");

  g_mutex_lock (self->lock);
  /* go over the controlled properties of the controller */
  for (node = self->properties; node; node = g_list_next (node)) {
    prop = node->data;
    GST_DEBUG ("  property '%s' at ts=%" G_GUINT64_FORMAT, prop->name,
        timestamp);

    live = FALSE;
    if (G_UNLIKELY (G_IS_VALUE (&prop->live_value.value))) {
      GList *lnode =
          gst_controlled_property_find_control_point_node (prop, timestamp);
      if (G_UNLIKELY (!lnode)) {
        GST_DEBUG ("    no control changes in the queue");
        live = TRUE;
      } else {
        GstControlPoint *cp = lnode->data;

        if (prop->live_value.timestamp < cp->timestamp) {
          g_value_unset (&prop->live_value.value);
          GST_DEBUG ("    live value resetted");
        } else if (prop->live_value.timestamp < timestamp) {
          live = TRUE;
        }
      }
    }
    if (G_LIKELY (!live)) {
      /* get current value via interpolator */
      value = prop->get (prop, timestamp);
      prop->last_value.timestamp = timestamp;
      g_value_copy (value, &prop->last_value.value);
      g_object_set_property (self->object, prop->name, value);
    }
  }
  if (G_LIKELY (!live))
    self->priv->last_sync = timestamp;

  g_mutex_unlock (self->lock);
  /* TODO what can here go wrong, to return FALSE ?
     BilboEd : Nothing I guess, as long as all the checks are made when creating the controller,
     adding/removing controlled properties, etc...
   */

  return (TRUE);
}

/**
 * gst_controller_get_value_arrays:
 * @self: the controller that handles the values
 * @timestamp: the time that should be processed
 * @value_arrays: list to return the control-values in
 *
 * Function to be able to get an array of values for one or more given element
 * properties.
 *
 * All fields of the %GstValueArray in the list must be filled correctly.
 * Especially the GstValueArray->values arrays must be big enough to keep
 * the requested amount of values.
 *
 * The types of the values in the array are the same as the property's type.
 *
 * <note><para>This doesn't modify the controlled GObject properties!</para></note>
 *
 * Returns: %TRUE if the given array(s) could be filled, %FALSE otherwise
 */
gboolean
gst_controller_get_value_arrays (GstController * self,
    GstClockTime timestamp, GSList * value_arrays)
{
  gboolean res = TRUE;
  GSList *node;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (value_arrays, FALSE);

  for (node = value_arrays; (res && node); node = g_slist_next (node)) {
    res = gst_controller_get_value_array (self, timestamp, node->data);
  }

  return (res);
}

/**
 * gst_controller_get_value_array:
 * @self: the controller that handles the values
 * @timestamp: the time that should be processed
 * @value_array: array to put control-values in
 *
 * Function to be able to get an array of values for one element property.
 *
 * All fields of @value_array must be filled correctly. Especially the
 * @value_array->values array must be big enough to keep the requested amount
 * of values.
 *
 * The type of the values in the array is the same as the property's type.
 *  
 * <note><para>This doesn't modify the controlled GObject property!</para></note>
 *
 * Returns: %TRUE if the given array could be filled, %FALSE otherwise
 */
gboolean
gst_controller_get_value_array (GstController * self, GstClockTime timestamp,
    GstValueArray * value_array)
{
  gboolean res = FALSE;
  GstControlledProperty *prop;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (value_array, FALSE);
  g_return_val_if_fail (value_array->property_name, FALSE);
  g_return_val_if_fail (value_array->values, FALSE);

  g_mutex_lock (self->lock);

  if ((prop =
          gst_controller_find_controlled_property (self,
              value_array->property_name))) {
    /* get current value_array via interpolator */
    res = prop->get_value_array (prop, timestamp, value_array);
  }

  g_mutex_unlock (self->lock);
  return (res);
}

/**
 * gst_controller_set_interpolation_mode:
 * @self: the controller object
 * @property_name: the name of the property for which to change the interpolation
 * @mode: interpolation mode
 *
 * Sets the given interpolation mode on the given property.
 *
 * <note><para>User interpolation is not yet available and quadratic interpolation
 * is deprecated and maps to cubic interpolation.</para></note>
 *
 * Returns: %TRUE if the property is handled by the controller, %FALSE otherwise
 */
gboolean
gst_controller_set_interpolation_mode (GstController * self,
    gchar * property_name, GstInterpolateMode mode)
{
  gboolean res = FALSE;
  GstControlledProperty *prop;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (property_name, FALSE);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    /* TODO shouldn't this also get a GstInterpolateMethod *user_method
       for the case mode==GST_INTERPOLATE_USER
     */
    res = gst_controlled_property_set_interpolation_mode (prop, mode);
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/* gobject handling */

static void
_gst_controller_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstController *self = GST_CONTROLLER (object);

  switch (property_id) {
    case PROP_CONTROL_RATE:{
      /* FIXME: don't change if element is playing, controller works for GObject
         so this wont work

         GstState c_state, p_state;
         GstStateChangeReturn ret;

         ret = gst_element_get_state (self->object, &c_state, &p_state, 0);
         if ((ret == GST_STATE_CHANGE_SUCCESS &&
         (c_state == GST_STATE_NULL || c_state == GST_STATE_READY)) ||
         (ret == GST_STATE_CHANGE_ASYNC &&
         (p_state == GST_STATE_NULL || p_state == GST_STATE_READY))) {
       */
      g_value_set_uint64 (value, self->priv->control_rate);
      /*
         }
         else {
         GST_WARNING ("Changing the control rate is only allowed if the elemnt"
         " is in NULL or READY");
         }
       */
    }
      break;
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
      break;
  }
}

/* sets the given properties for this object */
static void
_gst_controller_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstController *self = GST_CONTROLLER (object);

  switch (property_id) {
    case PROP_CONTROL_RATE:{
      self->priv->control_rate = g_value_get_uint64 (value);
    }
      break;
    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
      break;
  }
}

static void
_gst_controller_dispose (GObject * object)
{
  GstController *self = GST_CONTROLLER (object);

  if (self->object != NULL) {
    g_mutex_lock (self->lock);
    /* free list of properties */
    if (self->properties) {
      GList *node;

      for (node = self->properties; node; node = g_list_next (node)) {
        GstControlledProperty *prop = node->data;

        g_signal_handler_disconnect (self->object, prop->notify_handler_id);
        gst_controlled_property_free (prop);
      }
      g_list_free (self->properties);
      self->properties = NULL;
    }

    /* remove controller from object's qdata list */
    g_object_set_qdata (self->object, __gst_controller_key, NULL);
    g_object_unref (self->object);
    self->object = NULL;
    g_mutex_unlock (self->lock);
  }

  if (G_OBJECT_CLASS (parent_class)->dispose)
    (G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
_gst_controller_finalize (GObject * object)
{
  GstController *self = GST_CONTROLLER (object);

  g_mutex_free (self->lock);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
_gst_controller_init (GTypeInstance * instance, gpointer g_class)
{
  GstController *self = GST_CONTROLLER (instance);

  self->lock = g_mutex_new ();
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_CONTROLLER,
      GstControllerPrivate);
  self->priv->last_sync = GST_CLOCK_TIME_NONE;
  self->priv->control_rate = 100 * GST_MSECOND;
}

static void
_gst_controller_class_init (GstControllerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (GstControllerPrivate));

  gobject_class->set_property = _gst_controller_set_property;
  gobject_class->get_property = _gst_controller_get_property;
  gobject_class->dispose = _gst_controller_dispose;
  gobject_class->finalize = _gst_controller_finalize;

  __gst_controller_key = g_quark_from_string ("gst::controller");

  /* register properties */
  g_object_class_install_property (gobject_class, PROP_CONTROL_RATE,
      g_param_spec_uint64 ("control-rate",
          "control rate",
          "Controlled properties will be updated at least every control-rate nanoseconds",
          1, G_MAXUINT, 100 * GST_MSECOND, G_PARAM_READWRITE));

  /* register signals */
  /* set defaults for overridable methods */
}

GType
gst_controller_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GstControllerClass),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) _gst_controller_class_init,      /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (GstController),
      0,                        /* n_preallocs */
      (GInstanceInitFunc) _gst_controller_init, /* instance_init */
      NULL                      /* value_table */
    };
    type = g_type_register_static (G_TYPE_OBJECT, "GstController", &info, 0);
  }
  return type;
}
