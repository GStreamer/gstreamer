#include <glib.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

GstTypeFactory testfactory = { "test/test", ".tst", NULL };

int main(int argc,char *argv[]) {
  guint16 id;
  GstType *type;
  GstElementFactory *element;
  GList *types, *elements;

//  _gst_plugin_spew = TRUE;

  gst_init(&argc,&argv);
//  gst_plugin_load_all();
  gst_plugin_load("libgstparseau.so");
  gst_plugin_load("libgstparsewav.so");
  gst_plugin_load("libgstxa.so");
  gst_plugin_load("libstereo.so");
  gst_plugin_load("libvolume.so");
  gst_plugin_load("libsmoothwave.so");
  gst_plugin_load("libgstspectrum.so");
  gst_plugin_load("libsynaesthesia.so");
  gst_plugin_load("libvumeter.so");

  id = gst_type_register(&testfactory);

  types = gst_type_get_list();
  while (types) {
    type = (GstType *)types->data;
    g_print("%d: have type '%s'\n",type->id,type->mime);
    types = g_list_next(types);
  }

  elements = gst_elementfactory_get_list();
  while (elements) {
    element = (GstElementFactory *)elements->data;
    g_print("%d: have elementfactory '%s': \"%s\"\n",element->type,
            element->name,element->details->longname);
    elements = g_list_next(elements);
  }
}
