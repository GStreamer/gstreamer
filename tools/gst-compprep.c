#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <locale.h>

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (debug_compprep);
#define GST_CAT_DEFAULT debug_compprep
#define GST_COMPREG_FILE (GST_CACHE_DIR "/compreg.xml")

void
handle_xmlerror (void *userData, xmlErrorPtr error)
{
  g_print ("Error writing the completion registry: %s, %s\n", GST_COMPREG_FILE,
      error->message);
}

int
main (int argc, char *argv[])
{
  xmlDocPtr doc;
  xmlNodePtr factorynode, padnode, argnode, optionnode;
  GList *plugins, *features, *padtemplates;
  const GList *pads;
  GstElement *element;
  GstPad *pad;
  GstPadTemplate *padtemplate;
  GParamSpec **property_specs;
  guint num_properties, i;

  setlocale (LC_ALL, "");

  gst_init (&argc, &argv);
  GST_DEBUG_CATEGORY_INIT (debug_compprep, "compprep", GST_DEBUG_BOLD,
      "gst-compprep application");

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "GST-CompletionRegistry", NULL);

  plugins = g_list_copy (gst_registry_pool_plugin_list ());
  while (plugins) {
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);
    plugins = g_list_next (plugins);

    features = g_list_copy (gst_plugin_get_feature_list (plugin));
    while (features) {
      GstPluginFeature *feature;
      GstElementFactory *factory;

      feature = GST_PLUGIN_FEATURE (features->data);
      features = g_list_next (features);

      if (!GST_IS_ELEMENT_FACTORY (feature))
        continue;

      factory = GST_ELEMENT_FACTORY (feature);

      factorynode = xmlNewChild (doc->xmlRootNode, NULL, "element", NULL);
      xmlNewChild (factorynode, NULL, "name",
          GST_PLUGIN_FEATURE_NAME (factory));

      element = gst_element_factory_create (factory, NULL);
      GST_DEBUG ("adding factory %s", GST_PLUGIN_FEATURE_NAME (factory));
      if (element == NULL) {
        GST_ERROR ("couldn't construct element from factory %s\n",
            gst_object_get_name (GST_OBJECT (factory)));
        return 1;
      }

      /* write out the padtemplates */
      padtemplates = factory->padtemplates;
      while (padtemplates) {
        padtemplate = (GstPadTemplate *) (padtemplates->data);
        padtemplates = g_list_next (padtemplates);

        if (padtemplate->direction == GST_PAD_SRC)
          padnode =
              xmlNewChild (factorynode, NULL, "srcpadtemplate",
              padtemplate->name_template);
        else if (padtemplate->direction == GST_PAD_SINK)
          padnode =
              xmlNewChild (factorynode, NULL, "sinkpadtemplate",
              padtemplate->name_template);
      }

      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = (GstPad *) (pads->data);
        pads = g_list_next (pads);

        if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
          padnode =
              xmlNewChild (factorynode, NULL, "srcpad", GST_PAD_NAME (pad));
        else if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
          padnode =
              xmlNewChild (factorynode, NULL, "sinkpad", GST_PAD_NAME (pad));
      }

      /* write out the args */
      property_specs =
          g_object_class_list_properties (G_OBJECT_GET_CLASS (element),
          &num_properties);
      for (i = 0; i < num_properties; i++) {
        GParamSpec *param = property_specs[i];

        argnode = xmlNewChild (factorynode, NULL, "argument", param->name);
        if (param->value_type == GST_TYPE_URI) {
          xmlNewChild (argnode, NULL, "filename", NULL);
        } else if (G_IS_PARAM_SPEC_ENUM (param) == G_TYPE_ENUM) {
          GEnumValue *values;
          gint j;

          values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
          for (j = 0; values[j].value_name; j++) {
            gchar *value = g_strdup_printf ("%d", values[j].value);

            optionnode = xmlNewChild (argnode, NULL, "option", value);
            xmlNewChild (optionnode, NULL, "value_nick", values[j].value_nick);
            g_free (value);
          }
        }
      }
    }
  }

#ifdef HAVE_LIBXML2
  xmlSetStructuredErrorFunc (NULL, handle_xmlerror);
  xmlSaveFormatFile (GST_COMPREG_FILE, doc, 1);
#else
  xmlSaveFile (GST_COMPREG_FILE, doc);
#endif

  return 0;
}
