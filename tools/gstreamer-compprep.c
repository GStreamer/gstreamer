#include <gst/gst.h>
#include "config.h"

int main(int argc,char *argv[]) {
  xmlDocPtr doc;
  xmlNodePtr factorynode, padnode, argnode, optionnode;
  GList *plugins, *factories, *padtemplates, *pads;
  GstPlugin *plugin;
  GstElementFactory *factory;
  GstElement *element;
  GstPad *pad;
  GstPadTemplate *padtemplate;
  GParamSpec **property_specs;
  guint32 *flags;
  gint num_properties,i;

  gst_debug_set_categories(0);
  gst_info_set_categories(0);
  gst_init(&argc,&argv);

  doc = xmlNewDoc("1.0");
  doc->xmlRootNode = xmlNewDocNode(doc, NULL, "GST-CompletionRegistry", NULL);

  plugins = gst_plugin_get_list();
  while (plugins) {
    plugin = (GstPlugin *)(plugins->data);
    plugins = g_list_next (plugins);

    factories = gst_plugin_get_factory_list(plugin);
    while (factories) {
      factory = (GstElementFactory *)(factories->data);
      factories = g_list_next (factories);

      factorynode = xmlNewChild (doc->xmlRootNode, NULL, "element", NULL);
      xmlNewChild (factorynode, NULL, "name", factory->name);

      element = gst_elementfactory_create(factory,"element");
      if (element == NULL) {
        fprintf(stderr,"couldn't construct element from factory %s\n",factory->name);
        return 1;
      }

      // write out the padtemplates
      padtemplates = factory->padtemplates;
      while (padtemplates) {
        padtemplate = (GstPadTemplate *)(padtemplates->data);
        padtemplates = g_list_next (padtemplates);

        if (padtemplate->direction == GST_PAD_SRC)
          padnode = xmlNewChild (factorynode, NULL, "srcpadtemplate", padtemplate->name_template);
        else if (padtemplate->direction == GST_PAD_SINK)
          padnode = xmlNewChild (factorynode, NULL, "sinkpadtemplate", padtemplate->name_template);
      }

      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = (GstPad *)(pads->data);
        pads = g_list_next (pads);

        if (GST_PAD_DIRECTION(pad) == GST_PAD_SRC)
          padnode = xmlNewChild (factorynode, NULL, "srcpad", GST_PAD_NAME(pad));
        else if (GST_PAD_DIRECTION(pad) == GST_PAD_SINK)
          padnode = xmlNewChild (factorynode, NULL, "sinkpad", GST_PAD_NAME(pad));
      }

      // write out the args
#ifdef USE_GLIB2
      // FIXME accessing private data of GObjectClass !!!
      num_properties = G_OBJECT_GET_CLASS (element)->n_property_specs;
      property_specs = G_OBJECT_GET_CLASS (element)->property_specs;
#else
      property_specs = (GParamSpec **)gtk_object_query_args (GTK_OBJECT_TYPE (element), &flags, &num_properties);
#endif
      for (i=0;i<num_properties;i++) {
#ifdef USE_GLIB2
        GParamSpec *param = property_specs[i];
#else
        // gtk doesn't have a paramspec, so we create one here
        GParamSpec rparm, *param = &rparm;
	GtkArg *args = (GtkArg *)property_specs; // ugly typecast here 
	
	param->value_type = args[i].type;
	param->name = args[i].name;
#endif
        argnode = xmlNewChild (factorynode, NULL, "argument", param->name);
        if (param->value_type == GST_TYPE_FILENAME) {
          xmlNewChild (argnode, NULL, "filename", NULL);
        } else if (G_IS_PARAM_SPEC_ENUM (param) == G_TYPE_ENUM) {
          GEnumValue *values;
          gint j;

#ifdef USE_GLIB2
          values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
#else
          values = gtk_type_enum_get_values (param->value_type);
#endif
          for (j=0;values[j].value_name;j++) {
            gchar *value = g_strdup_printf("%d",values[j].value);
            optionnode = xmlNewChild (argnode, NULL, "option", value);
            xmlNewChild (optionnode, NULL, "value_nick", values[j].value_nick);
            g_free(value);
          }
        }
      }
    }
  }

  xmlSaveFile(GST_CONFIG_DIR "/compreg.xml",doc);

  return 0;
}
