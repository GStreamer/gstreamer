#include <glib.h>
#include <gnome-xml/parser.h>
#include <gst/gst.h>

typedef struct _GstRegistryPlugin GstRegistryPlugin;
typedef struct _GstRegistryElement GstRegistryElement;

struct _GstRegistryPlugin {
  gchar *name;
  gchar *filename;
};

struct _GstRegistryElement {
  GstRegistryPlugin *plugin;
  gchar *name;
  GstElementDetails details;
};

int main(int argc,char *argv[]) {
  xmlDocPtr doc;
  xmlNodePtr tree, subtree;
  GList *plugins = NULL, *features = NULL;

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  doc = xmlNewDoc("1.0");
  doc->root = xmlNewDocNode(doc,NULL,"GST-PluginRegistry",NULL);
  plugins = gst_plugin_get_list();
  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
    tree = xmlNewChild(doc->root,NULL,"plugin",NULL);
    subtree = xmlNewChild(tree,NULL,"name",plugin->name);
    subtree = xmlNewChild(tree,NULL,"longname",plugin->longname);
    subtree = xmlNewChild(tree,NULL,"filename",plugin->filename);
    features = plugin->features;
    while (features) {
      GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENTFACTORY (feature)) {
	GstElementFactory *element = GST_ELEMENTFACTORY (feature);
	
        tree = xmlNewChild(doc->root,NULL, "element", NULL);
        subtree = xmlNewChild (tree, NULL, "plugin", plugin->name);
        subtree = xmlNewChild (tree, NULL, "name", gst_object_get_name (GST_OBJECT (element)));
        subtree = xmlNewChild (tree, NULL, "longname",
                               element->details->longname);
        subtree = xmlNewChild (tree, NULL, "class",
                               element->details->klass);
        subtree = xmlNewChild (tree, NULL, "description",
                               element->details->description);
        subtree = xmlNewChild (tree, NULL, "version",
                               element->details->version);
        subtree = xmlNewChild (tree, NULL, "author",
                               element->details->author);
        subtree = xmlNewChild (tree, NULL, "copyright",
                               element->details->copyright);
      }
      features = g_list_next (features);
    }
    plugins = g_list_next(plugins);
  }

  xmlSaveFile("newreg.xml",doc);

  exit(0);
}
