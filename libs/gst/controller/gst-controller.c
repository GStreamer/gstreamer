/*
 * gst-controller.c
 * 
 * New dynamic properties
 *
 */

/* What needs to be done in plugins?
Very little - it is just two steps to make a plugin controllable!

1) Just mark gobject-properties that make sense to be controlled,
   by GST_PARAM_CONTROLLABLE for a start.

2) When processing data (get, chain, loop function) at the beginning call
   gst_element_sink_values(element,timestamp).
   This will made the controller to update all gobject properties that are under
   control with the current values based on timestamp.
*/

/* What needs to be done in applications?

1) First put some properties under control, by calling 
   controller=g_object_control_properties(object, "prop1", "prop2",...);

2) Set how the controller will smooth inbetween values.
   gst_controller_set_interpolation_mode(controller,"prop1",mode);

3) Set key values
   gst_controller_set(controller,"prop1",0*GST_SECOND,value1);
   gst_controller_set(controller,"prop1",1*GST_SECOND,value2);

4) Start your pipeline ;-)

5) Live control params from the GUI
   g_object_set_live_value(object, "prop1", timestamp, value);
*/

#include "config.h"
#include "gst-controller.h"

#define GST_CAT_DEFAULT gst_controller_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_DEFAULT);

static GObjectClass *parent_class = NULL;
GQuark controller_key;


/* imports from gst-interpolation.c */

extern GList
    *gst_controlled_property_find_timed_value_node (GstControlledProperty *
    prop, GstClockTime timestamp);
extern GstInterpolateMethod *interpolation_methods[];

/* callbacks */

void
on_object_controlled_property_changed (const GObject * object, GParamSpec * arg,
    gpointer user_data)
{
  GstControlledProperty *prop = GST_CONTROLLED_PROPERTY (user_data);
  GstController *ctrl;

  GST_INFO ("notify for '%s'", prop->name);

  ctrl = g_object_get_qdata (G_OBJECT (object), controller_key);
  g_return_if_fail (ctrl);

  if (g_mutex_trylock (ctrl->lock)) {
    if (!G_IS_VALUE (&prop->live_value.value)) {
      //g_value_unset (&prop->live_value.value);
      g_value_init (&prop->live_value.value, prop->type);
    }
    g_object_get_property (G_OBJECT (object), prop->name,
        &prop->live_value.value);
    prop->live_value.timestamp = prop->last_value.timestamp;
    g_mutex_unlock (ctrl->lock);
    GST_DEBUG ("-> is live update : ts=%" G_GUINT64_FORMAT,
        prop->live_value.timestamp);
  }
  //else {
  //GST_DEBUG ("-> is control change");
  //}
}

/* helper */

/*
 * gst_timed_value_compare:
 * @p1: a pointer to a #GstTimedValue
 * @p2: a pointer to a #GstTimedValue
 *
 * Compare function for g_list operations that operates on two #GstTimedValue
 * parameters.
 */
static gint
gst_timed_value_compare (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstTimedValue *) p1)->timestamp;
  GstClockTime ct2 = ((GstTimedValue *) p2)->timestamp;

  return ((ct1 < ct2) ? -1 : ((ct1 == ct2) ? 0 : 1));
/* this does not produce an gint :(
  return ((ct1 - ct2));
*/
}

/*
 * gst_timed_value_find:
 * @p1: a pointer to a #GstTimedValue
 * @p2: a pointer to a #GstClockTime
 *
 * Compare function for g_list operations that operates on a #GstTimedValue and
 * a #GstClockTime.
 */
static gint
gst_timed_value_find (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstTimedValue *) p1)->timestamp;
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
 */
static gboolean
gst_controlled_property_set_interpolation_mode (GstControlledProperty * self,
    GstInterpolateMode mode)
{
  self->interpolation = mode;
  if (mode != GST_INTERPOLATE_USER) {
    switch (self->type) {
      case G_TYPE_INT:
      case G_TYPE_UINT:
        self->get = interpolation_methods[mode]->get_int;
        self->get_value_array =
            interpolation_methods[mode]->get_int_value_array;
        break;
      case G_TYPE_LONG:
      case G_TYPE_ULONG:
        self->get = interpolation_methods[mode]->get_long;
        self->get_value_array =
            interpolation_methods[mode]->get_long_value_array;
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
      default:
        self->get = NULL;
        self->get_value_array = NULL;
        GST_WARNING ("incomplete implementation for type '%d'", self->type);
    }
  } else {
    /* TODO shouldn't this also get a GstInterpolateMethod *user_method
       for the case mode==GST_INTERPOLATE_USER
     */
  }
  return (TRUE);
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

  // check if the object has a property of that name
  if ((pspec =
          g_object_class_find_property (G_OBJECT_GET_CLASS (object), name))) {
    GST_DEBUG ("  psec->flags : 0x%08x", pspec->flags);

    // check if this param is controlable
    g_return_val_if_fail (!(pspec->
            flags & (G_PARAM_CONSTRUCT | G_PARAM_CONSTRUCT_ONLY)), NULL);
    //g_return_val_if_fail((pspec->flags&GST_PARAM_CONTROLLABLE),NULL);
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

      prop->name = pspec->name; // so we don't use the same mem twice
      prop->object = object;
      prop->type = G_PARAM_SPEC_VALUE_TYPE (pspec);
      gst_controlled_property_set_interpolation_mode (prop,
          GST_INTERPOLATE_NONE);
      /* prepare our gvalues */
      g_value_init (&prop->default_value, prop->type);
      g_value_init (&prop->result_value, prop->type);
      g_value_init (&prop->last_value.value, prop->type);
      switch (prop->type) {
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
        case G_TYPE_DOUBLE:{
          GParamSpecDouble *tpspec = G_PARAM_SPEC_DOUBLE (pspec);

          g_value_set_double (&prop->default_value, tpspec->default_value);
        }
          break;
        default:
          GST_WARNING ("incomplete implementation for paramspec type '%s'",
              G_PARAM_SPEC_TYPE_NAME (pspec));
      }
      /* TODO what about adding a timedval with timestamp=0 and value=default
         + a bit easier for interpolators, example:
         * first timestamp is at 5
         * requested value if for timestamp=3
         * LINEAR and Co. would need to interpolate from default value
         to value at timestamp 5 
       */
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
  GList *node;

  g_signal_handler_disconnect (prop->object, prop->notify_handler_id);
  for (node = prop->values; node; node = g_list_next (node)) {
    g_free (node->data);
  }
  g_list_free (prop->values);
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
  GST_WARNING ("controller does not manage property '%s'", name);

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

  self = g_object_get_qdata (object, controller_key);
  if (!self) {
    self = g_object_new (GST_TYPE_CONTROLLER, NULL);
    self->lock = g_mutex_new ();
    // store the controller
    g_object_set_qdata (object, controller_key, self);
  }
  // create GstControlledProperty for each property
  while ((name = va_arg (var_args, gchar *))) {
    // create GstControlledProperty and add to self->propeties List
    if ((prop = gst_controlled_property_new (object, name)))
      self->properties = g_list_prepend (self->properties, prop);
  }
  va_end (var_args);

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
 * gst_controller_remove_properties:
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
    // find the property in the properties list of the controller, remove and free it
    g_mutex_lock (self->lock);
    if ((prop = gst_controller_find_controlled_property (self, name))) {
      self->properties = g_list_remove (self->properties, prop);
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
    if (G_VALUE_TYPE (value) == prop->type) {
      GstTimedValue *tv;
      GList *node;

      // check if a timed_value for the timestamp already exists
      if ((node = g_list_find_custom (prop->values, &timestamp,
                  gst_timed_value_find))) {
        tv = node->data;
        memcpy (&tv->value, value, sizeof (GValue));
      } else {
        // create a new GstTimedValue
        tv = g_new (GstTimedValue, 1);
        tv->timestamp = timestamp;
        memcpy (&tv->value, value, sizeof (GValue));
        // and sort it into the prop->values list
        prop->values =
            g_list_insert_sorted (prop->values, tv, gst_timed_value_compare);
      }
      res = TRUE;
    } else {
      GST_WARNING ("incompatible value type for property '%s'", prop->name);
    }
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
      if (G_VALUE_TYPE (&tv->value) == prop->type) {
        g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (tv->timestamp), FALSE);
        /* TODO copy the timed value or just link in? */
        prop->values =
            g_list_insert_sorted (prop->values, tv, gst_timed_value_compare);
        res = TRUE;
      } else {
        GST_WARNING ("incompatible value type for property '%s'", prop->name);
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
    prop->values = g_list_remove (prop->values, prop);
    res = TRUE;
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/**
 * gst_controller_get:
 * @self: the controller object which handles the properties
 * @property_name: the name of the property to get
 * timestamp: the time the control-change should be read from
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
    //get current value via interpolator
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
 * gst_controller_sink_values:
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
gst_controller_sink_values (GstController * self, GstClockTime timestamp)
{
  GstControlledProperty *prop;
  GList *node;
  GValue *value;
  gboolean live;

  g_return_val_if_fail (GST_IS_CONTROLLER (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);

  GST_INFO ("sink_values");

  g_mutex_lock (self->lock);
  // go over the controlled properties of the controller
  for (node = self->properties; node; node = g_list_next (node)) {
    prop = node->data;
    GST_DEBUG ("  property '%s' at ts=%" G_GUINT64_FORMAT, prop->name,
        timestamp);

    live = FALSE;
    if (G_IS_VALUE (&prop->live_value.value)) {
      GList *lnode =
          gst_controlled_property_find_timed_value_node (prop, timestamp);
      if (!lnode) {
        GST_DEBUG ("    no control changes in the queue");
        live = TRUE;
      } else {
        GstTimedValue *tv = lnode->data;

        //GST_DEBUG ("live.ts %"G_UINT64_FORMAT" <-> now %"G_UINT64_FORMAT, prop->live_value.timestamp, tv->timestamp);
        if (prop->live_value.timestamp < tv->timestamp) {
          g_value_unset (&prop->live_value.value);
          GST_DEBUG ("    live value resetted");
        } else if (prop->live_value.timestamp < timestamp) {
          live = TRUE;
        }
      }
    }
    if (!live) {
      //get current value via interpolator
      value = prop->get (prop, timestamp);
      prop->last_value.timestamp = timestamp;
      g_value_copy (value, &prop->last_value.value);
      g_object_set_property (prop->object, prop->name, value);
    }
  }
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
 * If the GstValueArray->values array in list nodes is NULL, it will be created 
 * by the function.
 * The type of the values in the array are the same as the property's type.
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
 * Function to be able to get an array of values for one element properties
 *
 * If the GstValueArray->values array is NULL, it will be created by the function.
 * The type of the values in the array are the same as the property's type.
 *
 * Returns: %TRUE if the given array(s) could be filled, %FALSE otherwise
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

  /* TODO and if GstValueArray->values is not NULL, the caller is resposible that
     is is big enough for nbsamples values, right?
   */

  g_mutex_lock (self->lock);
  if ((prop =
          gst_controller_find_controlled_property (self,
              value_array->property_name))) {
    if (!value_array->values) {
      /* TODO from where to get the base size
         value_array->values=g_new(sizeof(???),nbsamples);
       */
    }
    //get current value_array via interpolator
    res = prop->get_value_array (prop, timestamp, value_array);
  }
  g_mutex_unlock (self->lock);
  return (res);
}

/**
 * gst_controller_set_interpolation_mode:
 * @controller:
 * @property_name:
 * @mode: interpolation mode
 *
 * Sets the given interpolation mode on the given property.
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
    gst_controlled_property_set_interpolation_mode (prop, mode);
    res = TRUE;
  }
  g_mutex_unlock (self->lock);

  return (res);
}

/*
void
gst_controller_set_live_value(GstController * self, gchar *property_name,
    GstClockTime timestamp, GValue *value)
{
  GstControlledProperty *prop;

  g_return_if_fail (GST_IS_CONTROLLER (self));
  g_return_if_fail (property_name);

  g_mutex_lock (self->lock);
  if ((prop = gst_controller_find_controlled_property (self, property_name))) {
    g_value_unset (&prop->live_value.value);
    g_value_init (&prop->live_value.value, prop->type);
    g_value_copy (value, &prop->live_value.value);
    prop->live_value.timestamp = timestamp;
  }
  g_mutex_unlock (self->lock);
}

*/

/* gobject handling */

static void
_gst_controller_finalize (GObject * object)
{
  GstController *self = GST_CONTROLLER (object);
  GList *node;

  // free list of properties
  if (self->properties) {
    for (node = self->properties; node; node = g_list_next (node)) {
      gst_controlled_property_free (node->data);
    }
    g_list_free (self->properties);
    self->properties = NULL;
  }
  g_mutex_free (self->lock);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
_gst_controller_init (GTypeInstance * instance, gpointer g_class)
{
  GstController *self = GST_CONTROLLER (instance);

  self->lock = g_mutex_new ();

}

static void
_gst_controller_class_init (GstControllerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  gobject_class->finalize = _gst_controller_finalize;

  controller_key = g_quark_from_string ("gst::controller");

  // register properties
  // register signals
  // set defaults for overridable methods
  /* TODO which of theses do we need ? 
     BilboEd : none :)
   */
}

GType
gst_controller_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GstControllerClass),
      NULL,                     // base_init
      NULL,                     // base_finalize
      (GClassInitFunc) _gst_controller_class_init,      // class_init
      NULL,                     // class_finalize
      NULL,                     // class_data
      sizeof (GstController),
      0,                        // n_preallocs
      (GInstanceInitFunc) _gst_controller_init, // instance_init
      NULL                      // value_table
    };
    type = g_type_register_static (G_TYPE_OBJECT, "GstController", &info, 0);
  }
  return type;
}
