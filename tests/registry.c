#include <gst/gst.h>

static void 
dump_plugins (void)
{
  GList *plugins;
  plugins = gst_plugin_get_list ();

  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
    
    g_print ("plugin: %s, loaded %d\n", plugin->name, plugin->loaded);

    plugins = g_list_next (plugins);
  }
}

static void 
dump_factories (void)
{
  GList *factories;
  factories = gst_elementfactory_get_list ();

  while (factories) {
    GstElementFactory *factory = (GstElementFactory *)factories->data;
    
    g_print ("factory: %s\n", factory->name);

    factories = g_list_next (factories);
  }
}

static void 
dump_types (void)
{
  GList *types;
  types = gst_type_get_list ();

  while (types) {
    GstType *factory = (GstType *)types->data;
    
    g_print ("type: %s %d\n", factory->mime, factory->id);

    types = g_list_next (types);
  }
}

static void 
load_something (gchar *name)
{
  GstElementFactory *factory;
  GstElement *element;

  factory = gst_elementfactory_find ("foo");
  g_print ("factory \"foo\" %s\n", (factory?"found":"not found"));

  factory = gst_elementfactory_find (name);
  g_print ("factory \"%s\" %s\n", name, (factory?"found":"not found"));

  element = gst_elementfactory_create (factory, "test");

  g_print ("element \"%s\" %s\n", name, (element?"found":"not found"));
}

static void 
print_some_providers (gchar *mime)
{
  guint16 type;
  GList *srcs, *sinks;
  type = gst_type_find_by_mime (mime);

  srcs = gst_type_get_srcs (type);

  while (srcs) {
    GstElementFactory *factory;

    factory = (GstElementFactory *) srcs->data;

    g_print ("factory src: \"%s\"\n", factory->name);

    srcs = g_list_next (srcs);
  }

  sinks = gst_type_get_sinks (type);
  while (sinks) {
    GstElementFactory *factory;

    factory = (GstElementFactory *) sinks->data;

    g_print ("factory sink: \"%s\"\n", factory->name);

    sinks = g_list_next (sinks);
  }
}

int main(int argc,char *argv[]) 
{

  gst_init(&argc,&argv);

  dump_plugins ();
  dump_factories ();
  dump_types ();

  print_some_providers ("audio/mp3");

  load_something ("mpg123");

  print_some_providers ("audio/mp3");

  load_something ("mpg123");

  dump_plugins ();
  dump_factories ();
  dump_types ();

}
