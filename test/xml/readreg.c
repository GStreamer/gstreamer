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

gchar *getcontents(xmlDocPtr doc,xmlNodePtr cur) {
  return g_strdup(xmlNodeListGetString(doc,cur->childs,1));
}

int main(int argc,char *argv[]) {
  xmlDocPtr doc;
  xmlNodePtr cur;
  int i;

  GSList *plugins = NULL, *elements = NULL;

//  gst_init(&argc,&argv);

  doc = xmlParseFile("registry.xml");
  g_assert(doc != NULL);

  cur = doc->root;
  if (cur == NULL) {
    g_print("registry is empty\n");
    xmlFreeDoc(doc);
    exit(0);
  }

  if (strcmp(cur->name,"GST-PluginRegistry")) {
    g_print("document not the right type\n");
    xmlFreeDoc(doc);
    exit(1);
  }

  cur = cur->childs;	/* 'childs'???  He (Daniel) is Dutch, so... */
  while (cur != NULL) {
    if (!strcmp(cur->name,"plugin")) {
      xmlNodePtr field = cur->childs;
      GstRegistryPlugin *plugin = g_new0(GstRegistryPlugin,1);

      while (field) {
        if (!strcmp(field->name,"name"))
          plugin->name = getcontents(doc,field);
        else if (!strcmp(field->name,"filename"))
          plugin->filename = getcontents(doc,field);
        field = field->next;
      }
      g_print("new plugin '%s' at '%s'\n",plugin->name,plugin->filename);
      plugins = g_slist_prepend(plugins,plugin);
    } else if (!strcmp(cur->name,"element")) {
      xmlNodePtr field = cur->childs;
      GstRegistryElement *element = g_new0(GstRegistryElement,1);

      while (field) {
        if (!strcmp(field->name,"plugin")) {
          gchar *pluginname = getcontents(doc,field);
          GSList *list = plugins;
          element->plugin = NULL;
          while (list) {
            GstRegistryPlugin *plugin = (GstRegistryPlugin *)list->data;
            if (!strcmp(pluginname,plugin->name)) {
              element->plugin = plugin;
              break;
            }
            list = g_slist_next(list);
          }
        } else if (!strcmp(field->name,"name"))
          element->name = getcontents(doc,field);
        else if (!strcmp(field->name,"longname"))
          element->details.longname = getcontents(doc,field);
        else if (!strcmp(field->name,"class"))
          element->details.class = getcontents(doc,field);
        else if (!strcmp(field->name,"description"))
          element->details.description = getcontents(doc,field);
        else if (!strcmp(field->name,"version"))
          element->details.version = getcontents(doc,field);
        else if (!strcmp(field->name,"author"))
          element->details.author = getcontents(doc,field);
        else if (!strcmp(field->name,"copyright"))
          element->details.copyright = getcontents(doc,field);
        field = field->next;
      }
      g_print("new element '%s'in '%s'\n",element->name,element->plugin->name);
      elements = g_slist_prepend(elements,element);
    }
    cur = cur->next;
  }

  for (i=1;i<argc;i++) {
    GSList *list;
    g_print("searching for element '%s'\n",argv[i]);
    list = elements;
    while (list) {
      GstRegistryElement *element = (GstRegistryElement *)list->data;
//      g_print("comparing against '%s'\n",element->name);
      if (!strcmp(argv[i],element->name)) {
        g_print("Plugin name: %s\n",element->plugin->name);
        g_print("Plugin filename: %s\n",element->plugin->filename);
        g_print("Element name: %s\n",element->name);
        g_print("Element long name: %s\n",element->details.longname);
        g_print("Element class: %s\n",element->details.class);
        g_print("Element description: %s\n",element->details.description);
        g_print("Element version: %s\n",element->details.version);
        g_print("Element author: %s\n",element->details.author);
        g_print("Element copyright: %s\n",element->details.copyright);
//        gst_plugin_load_absolute(element->plugin->filename);
      }
      list = g_slist_next(list);
    }
  }

  exit(0);
}
