
#include <gst/gst.h>


static GstCaps *
handler (GObject * object, GstCaps * caps, gpointer user_data)
{
  g_print ("in handler %p, %p, %p\n", object, caps, user_data);

  g_assert (GST_IS_PAD (object));

  g_print ("caps: %s\n", gst_caps_to_string (caps));

  if (gst_caps_is_any (caps)) {
    return gst_caps_new_simple ("application/x-foo",
	"field", GST_TYPE_INT_RANGE, 1, 10, NULL);
  }

  return NULL;
}


int
main (int argc, char *argv[])
{
  GstElement *a;
  GstElement *b;
  GstElement *pipeline;
  GstPad *pad;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new (NULL);

  a = gst_element_factory_make ("fakesrc", NULL);
  g_assert (a);
  b = gst_element_factory_make ("fakesink", NULL);
  g_assert (b);

  gst_bin_add_many (GST_BIN (pipeline), a, b, NULL);
  gst_element_link (a, b);

  pad = gst_element_get_pad (a, "src");
  g_signal_connect (G_OBJECT (pad), "fixate", G_CALLBACK (handler),
      (void *) 0xdeadbeef);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);


  return 0;
}
