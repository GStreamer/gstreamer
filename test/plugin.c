#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstElementFactory *parseau_factory;
  GstElement *parseau;

  gst_init(&argc,&argv);

  gst_plugin_load_all();

  parseau_factory = gst_plugin_find_elementfactory("parseau");
  g_print("parseau_factory is %p\n",parseau_factory);

  parseau = gst_elementfactory_create(parseau_factory,"parser");
  g_print("got parseau '%s' from plugin!!!\n",
          gst_object_get_name(GST_OBJECT(parseau)));
}
