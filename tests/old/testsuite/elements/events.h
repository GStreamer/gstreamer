#include <gst/gst.h>
#include <gst/gstpropsprivate.h>

/*
 * no need to librify this simple function set
 */

static void
print_props (gpointer data, gpointer user_data)
{
  GstPropsEntry *entry = (GstPropsEntry *)data;
  GstElement *element = GST_ELEMENT (user_data);

  g_print ("%s: %s: ", gst_element_get_name (element),
                  g_quark_to_string (entry->propid));
  switch (entry->propstype) {
    case GST_PROPS_INT_ID:
      g_print ("%d\n", entry->data.int_data);
      break;
    case GST_PROPS_STRING_ID:
      g_print ("%s\n", entry->data.string_data.string);
      break;
    case GST_PROPS_FLOAT_ID:
      g_print ("%f\n", entry->data.float_data);
      break;
    default:
      g_print ("unknown\n");
  }
}

static void
event_func (GstElement *element, GstEvent *event)
{
  GstProps *props;

  if (event == NULL)
    return;

  if (GST_EVENT_TYPE (event) == GST_EVENT_INFO) {
    props = GST_EVENT_INFO_PROPS (event);

    g_list_foreach (props->properties, print_props, GST_EVENT_SRC (event));
  }
}

