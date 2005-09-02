#include <string.h>
#include <unistd.h>
#include <gst/gst.h>

static GstElement *src1, *sink1;
static gboolean need_src1 = TRUE;
static gint iter = 0;

static void
object_deep_notify (GObject * object, GstObject * orig,
    GParamSpec * pspec, gchar ** excluded_props)
{
  GValue value = { 0, };        /* the important thing is that value.type = 0 */
  gchar *str = NULL;

  if (strcmp (pspec->name, "last-message") != 0)
    return;

  if (GST_ELEMENT (orig) != src1 && GST_ELEMENT (orig) != sink1)
    return;

  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
  g_object_get_property (G_OBJECT (orig), pspec->name, &value);

  str = g_strdup_value_contents (&value);
  g_value_unset (&value);

  if (strstr (str, "E (type:") != NULL) {
    g_free (str);
    return;
  }

  if (iter++ == 100) {
    g_print (".");
    iter = 0;
  }
  g_free (str);
  if (need_src1 && GST_ELEMENT (orig) != src1) {
    g_assert_not_reached ();
  } else if (!need_src1 && GST_ELEMENT (orig) != sink1) {
    g_assert_not_reached ();
  }
  need_src1 = !need_src1;
}


int
main (int argc, char **argv)
{
  GstElement *thread1, *thread2, *pipeline;
  GstElement *src2, *sink2;

  gst_init (&argc, &argv);

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  thread1 = gst_element_factory_make ("thread", "thread1");
  g_assert (thread1);

  src1 = gst_element_factory_make ("fakesrc", "src1");
  g_assert (src1);
  sink1 = gst_element_factory_make ("fakesink", "sink1");
  g_assert (sink1);

  thread2 = gst_element_factory_make ("thread", "thread2");
  g_assert (thread2);

  src2 = gst_element_factory_make ("fakesrc", "src2");
  g_assert (src2);
  sink2 = gst_element_factory_make ("fakesink", "sink2");
  g_assert (sink2);

  gst_bin_add_many (GST_BIN (thread1), src1, sink1, NULL);
  gst_bin_add_many (GST_BIN (thread2), src2, sink2, NULL);

  gst_bin_add (GST_BIN (pipeline), thread1);
  gst_bin_add (GST_BIN (pipeline), thread2);

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (object_deep_notify), NULL);

  if (!gst_element_link_many (src1, sink1, NULL))
    g_assert_not_reached ();

  if (!gst_element_link_many (src2, sink2, NULL))
    g_assert_not_reached ();

  /* run a bit */
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();

  sleep (10000);
  g_print ("done\n");

  return 0;
}
