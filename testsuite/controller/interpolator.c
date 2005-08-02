/*
 * interpolator.c
 * 
 * test interpolator methods
 *
 */

#include <gst/gst.h>
#include <gst/controller/gst-controller.h>

extern GstInterpolateMethod *interpolation_methods[];

gint
main (gint argc, gchar ** argv)
{
  gint res = 1;
  GstControlledProperty *prop = NULL;
  GType type = G_TYPE_INT;
  GstTimedValue tv1 = { 0, }, tv2 = {
  0,}, tv3 = {
  0,};
  GValue *val;
  gint i;

  gst_init (&argc, &argv);
  gst_controller_init (&argc, &argv);

  // build fake controlled property

  if ((prop = g_new0 (GstControlledProperty, 1))) {
    prop->name = "test";
    //prop->parent_type = G_OBJECT_TYPE (object);
    prop->type = type;

    g_value_init (&prop->default_value, type);
    g_value_set_int (&prop->default_value, 0);
    g_value_init (&prop->result_value, type);

    // set timed values
    tv1.timestamp = 0;
    g_value_init (&tv1.value, type);
    g_value_set_int (&tv1.value, 0);
    prop->values = g_list_append (prop->values, &tv1);

    tv2.timestamp = 10 * GST_SECOND;
    g_value_init (&tv2.value, type);
    g_value_set_int (&tv2.value, 100);
    prop->values = g_list_append (prop->values, &tv2);

    tv3.timestamp = 20 * GST_SECOND;
    g_value_init (&tv3.value, type);
    g_value_set_int (&tv3.value, 50);
    prop->values = g_list_append (prop->values, &tv3);

    g_print ("# time trig none line\n");

    // test interpolator
    for (i = 0; i < 25; i++) {
      g_print ("  %4d", i);

      prop->interpolation = GST_INTERPOLATE_TRIGGER;
      prop->get = interpolation_methods[prop->interpolation]->get_int;
      prop->get_value_array =
          interpolation_methods[prop->interpolation]->get_int_value_array;
      val = prop->get (prop, i * GST_SECOND);
      g_print (" %4d", (val ? g_value_get_int (val) : 0));

      prop->interpolation = GST_INTERPOLATE_NONE;
      prop->get = interpolation_methods[prop->interpolation]->get_int;
      prop->get_value_array =
          interpolation_methods[prop->interpolation]->get_int_value_array;
      val = prop->get (prop, i * GST_SECOND);
      g_print (" %4d", (val ? g_value_get_int (val) : 0));

      prop->interpolation = GST_INTERPOLATE_LINEAR;
      prop->get = interpolation_methods[prop->interpolation]->get_int;
      prop->get_value_array =
          interpolation_methods[prop->interpolation]->get_int_value_array;
      val = prop->get (prop, i * GST_SECOND);
      g_print (" %4d", (val ? g_value_get_int (val) : 0));

      g_print ("\n");
    }

    g_list_free (prop->values);
    g_free (prop);
    res = 0;
  }
  return (res);
}
