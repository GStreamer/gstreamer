#include <gst/gst.h>
#include "config.h"

int main(int argc,char *argv[]) {
  xmlDocPtr doc;
  xmlNodePtr factorynode, padnode, argnode, optionnode;
  GList *plugins, *features, *padtemplates, *pads;
  GstElement *element;
  GstPad *pad;
  GstPadTemplate *padtemplate;
  GParamSpec **property_specs;
  gint num_properties,i;

  gst_debug_set_categories(0);
  gst_info_set_categories(0);
  gst_init(&argc,&argv);

  doc = xmlNewDoc("1.0");
  doc->xmlRootNode = xmlNewDocNode(doc, NULL, "GST-CompletionRegistry", NULL);

  plugins = gst_plugin_get_list();
  while (plugins) {
    GstPlugin *plugin;

    plugin = (GstPlugin *)(plugins->data);
    plugins = g_list_next (plugins);

    features = gst_plugin_get_feature_list(plugin);
    while (features) {
      GstPluginFeature *feature;
      GstElementFactory *factory;

      feature = GST_PLUGIN_FEATURE (features->data);
      features = g_list_next (features);

      if (!GST_IS_ELEMENTFACTORY (feature))
	continue;

      factory = GST_ELEMENTFACTORY (feature);

      factorynode = xmlNewChild (doc->xmlRootNode, NULL, "element", NULL);
      xmlNewChild (factorynode, NULL, "name", gst_object_get_name (GST_OBJECT (factory)));

      element = gst_elementfactory_create(factory,"element");
      if (element == NULL) {
        fprintf(stderr,"couldn't construct element from factory %s\n", 
			gst_object_get_name (GST_OBJECT (factory)));
        return 1;
      }

      /* write out the padtemplates */
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

      /* write out the args */
      property_specs = g_object_class_list_properties(G_OBJECT_GET_CLASS (element), &num_properties);
      for (i=0;i<num_properties;i++) {
        GParamSpec *param = property_specs[i];
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
