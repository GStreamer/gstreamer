#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <locale.h>

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (debug_compprep);
#define GST_CAT_DEFAULT debug_compprep
#define GST_COMPREG_FILE (GST_CACHE_DIR "/compreg.xml")

#ifdef HAVE_LIBXML2
#if LIBXML_VERSION >= 20600
void
handle_xmlerror (void *userData, xmlErrorPtr error)
{
  g_print ("Error writing the completion registry: %s, %s\n", GST_COMPREG_FILE,
      error->message);
}
#endif
#endif

int
main (int argc, char *argv[])
{
  xmlDocPtr doc;
  xmlNodePtr factorynode, padnode, argnode, optionnode;
  GList *element_factories, *padtemplates, *g;
  const GList *pads;
  GstElement *element;
  GstPad *pad;
  GstStaticPadTemplate *padtemplate;
  GParamSpec **property_specs;
  guint num_properties, i;

  setlocale (LC_ALL, "");

  gst_init (&argc, &argv);
  GST_DEBUG_CATEGORY_INIT (debug_compprep, "compprep", GST_DEBUG_BOLD,
      "gst-compprep application");

  doc = xmlNewDoc ((xmlChar *) "1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL,
      (xmlChar *) "GST-CompletionRegistry", NULL);

  element_factories =
      gst_registry_get_feature_list (gst_registry_get_default (),
      GST_TYPE_ELEMENT_FACTORY);
  for (g = element_factories; g; g = g_list_next (g)) {
    GstElementFactory *factory;

    factory = GST_ELEMENT_FACTORY (g->data);

    factorynode = xmlNewChild (doc->xmlRootNode, NULL, (xmlChar *) "element",
        NULL);
    xmlNewChild (factorynode, NULL, (xmlChar *) "name",
        (xmlChar *) GST_PLUGIN_FEATURE_NAME (factory));

    element = gst_element_factory_create (factory, NULL);
    GST_DEBUG ("adding factory %s", GST_PLUGIN_FEATURE_NAME (factory));
    if (element == NULL) {
      GST_ERROR ("couldn't construct element from factory %s\n",
          gst_object_get_name (GST_OBJECT (factory)));
      return 1;
    }

    /* write out the padtemplates */
    padtemplates = factory->staticpadtemplates;
    while (padtemplates) {
      padtemplate = (GstStaticPadTemplate *) (padtemplates->data);
      padtemplates = g_list_next (padtemplates);

      if (padtemplate->direction == GST_PAD_SRC)
        padnode =
            xmlNewChild (factorynode, NULL, (xmlChar *) "srcpadtemplate",
            (xmlChar *) padtemplate->name_template);
      else if (padtemplate->direction == GST_PAD_SINK)
        padnode =
            xmlNewChild (factorynode, NULL, (xmlChar *) "sinkpadtemplate",
            (xmlChar *) padtemplate->name_template);
    }

    pads = element->pads;
    while (pads) {
      pad = (GstPad *) (pads->data);
      pads = g_list_next (pads);

      if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
        padnode = xmlNewChild (factorynode, NULL, (xmlChar *) "srcpad",
            (xmlChar *) GST_PAD_NAME (pad));
      else if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
        padnode = xmlNewChild (factorynode, NULL, (xmlChar *) "sinkpad",
            (xmlChar *) GST_PAD_NAME (pad));
    }

    /* write out the args */
    property_specs =
        g_object_class_list_properties (G_OBJECT_GET_CLASS (element),
        &num_properties);
    for (i = 0; i < num_properties; i++) {
      GParamSpec *param = property_specs[i];

      argnode = xmlNewChild (factorynode, NULL, (xmlChar *) "argument",
          (xmlChar *) param->name);
      if (G_IS_PARAM_SPEC_ENUM (param) == G_TYPE_ENUM) {
        GEnumValue *values;
        gint j;

        values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
        for (j = 0; values[j].value_name; j++) {
          gchar *value = g_strdup_printf ("%d", values[j].value);

          optionnode = xmlNewChild (argnode, NULL, (xmlChar *) "option",
              (xmlChar *) value);
          xmlNewChild (optionnode, NULL, (xmlChar *) "value_nick",
              (xmlChar *) values[j].value_nick);
          g_free (value);
        }
      }
      g_free (property_specs);
    }
  }

#ifdef HAVE_LIBXML2
#if LIBXML_VERSION >= 20600
  xmlSetStructuredErrorFunc (NULL, handle_xmlerror);
#endif
  xmlSaveFormatFile (GST_COMPREG_FILE, doc, 1);
#else
  xmlSaveFile (GST_COMPREG_FILE, doc);
#endif

  return 0;
}
