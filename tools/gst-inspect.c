#include <gst/gst.h>
#include <string.h>

/* this must be built within the gstreamer dir, else this will fail */
#include <gst/gstpropsprivate.h>

static void 
print_prop (GstPropsEntry *prop, gboolean showname, gchar *pfx) 
{
  GList *list;
  GstPropsEntry *listentry;
  gchar *longprefix;

  if (showname)
    printf("%s%s: ",pfx,g_quark_to_string(prop->propid));
  else
    printf(pfx);

  switch (prop->propstype) {
    case GST_PROPS_INT_ID:
      printf("Integer: %d\n",prop->data.int_data);
      break;
    case GST_PROPS_INT_RANGE_ID:
      printf("Integer range: %d - %d\n",prop->data.int_range_data.min,
             prop->data.int_range_data.max);
      break;
    case GST_PROPS_FLOAT_ID:
      printf("Float: %f\n",prop->data.float_data);
      break;
    case GST_PROPS_FLOAT_RANGE_ID:
      printf("Float range: %f - %f\n",prop->data.float_range_data.min,
             prop->data.float_range_data.max);
      break;
    case GST_PROPS_BOOL_ID:
      printf("Boolean: %s\n",prop->data.bool_data ? "TRUE" : "FALSE");
      break;
    case GST_PROPS_STRING_ID:
      printf("String: %s\n",prop->data.string_data.string);
      break;
    case GST_PROPS_FOURCC_ID:
      printf("FourCC: '%c%c%c%c'\n",
             prop->data.fourcc_data & 0xff,prop->data.fourcc_data>>8 & 0xff,
             prop->data.fourcc_data>>16 & 0xff,prop->data.fourcc_data>>24 & 0xff);
      break;
    case GST_PROPS_LIST_ID:
      printf("List:\n");
      longprefix = g_strdup_printf("%s  ",pfx);
      list = prop->data.list_data.entries;
      while (list) {
        listentry = (GstPropsEntry*)(list->data);
        list = g_list_next(list);
        print_prop(listentry,FALSE,longprefix);
      }
      g_free(longprefix);
      break;
    default:
      printf("unknown props %d\n", prop->propstype);
  }
}

static void 
print_props (GstProps *properties, gchar *pfx) 
{
  GList *props;
  GstPropsEntry *prop;

  props = properties->properties;
  while (props) {
    prop = (GstPropsEntry*)(props->data);
    props = g_list_next(props);

    print_prop(prop,TRUE,pfx);
  }
}

static void
output_hierarchy (GType type, gint level, gint *maxlevel)
{
  GType parent;
  gint i;

  parent = g_type_parent (type);

  *maxlevel = *maxlevel + 1;
  level++;

  if (parent)
    output_hierarchy (parent, level, maxlevel);
  
  for (i=1; i<*maxlevel-level; i++)
   g_print ("      ");
  if (*maxlevel-level)
    g_print (" +----");

  g_print ("%s\n", g_type_name (type));
	  
  if (level == 1)
    g_print ("\n");
}

static gint
print_element_info (GstElementFactory *factory)
{
  GstElement *element;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;
  GList *pads;
  GstCaps *caps;
  GstPad *pad;
  GstRealPad *realpad;
  GstPadTemplate *padtemplate;
  gint num_properties,i;
  GParamSpec **property_specs;
  GList *children;
  GstElement *child;
  gboolean have_flags;
  gint maxlevel = 0;

  element = gst_elementfactory_create(factory,"element");
  if (!element) {
    g_print ("couldn't construct element for some reason\n");
    return -1;
  }

  gstobject_class = GST_OBJECT_CLASS (G_OBJECT_GET_CLASS (element));
  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  printf("Factory Details:\n");
  printf("  Long name:\t%s\n",factory->details->longname);
  printf("  Class:\t%s\n",factory->details->klass);
  printf("  Description:\t%s\n",factory->details->description);
  printf("  Version:\t%s\n",factory->details->version);
  printf("  Author(s):\t%s\n",factory->details->author);
  printf("  Copyright:\t%s\n",factory->details->copyright);
  printf("\n");

  output_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);

  printf("Pad Templates:\n");
  if (factory->numpadtemplates) {
    pads = factory->padtemplates;
    while (pads) {
      padtemplate = (GstPadTemplate*)(pads->data);
      pads = g_list_next(pads);

      if (padtemplate->direction == GST_PAD_SRC)
        printf("  SRC template: '%s'\n",padtemplate->name_template);
      else if (padtemplate->direction == GST_PAD_SINK)
        printf("  SINK template: '%s'\n",padtemplate->name_template);
      else
        printf("  UNKNOWN!!! template: '%s'\n",padtemplate->name_template);

      if (padtemplate->presence == GST_PAD_ALWAYS)
        printf("    Availability: Always\n");
      else if (padtemplate->presence == GST_PAD_SOMETIMES)
        printf("    Availability: Sometimes\n");
      else if (padtemplate->presence == GST_PAD_REQUEST) {
        printf("    Availability: On request\n");
        printf("      Has request_new_pad() function: %s\n",
             GST_DEBUG_FUNCPTR_NAME(gstelement_class->request_new_pad));
      }
      else
        printf("    Availability: UNKNOWN!!!\n");

      if (padtemplate->caps) {
        printf("    Capabilities:\n");
        caps = padtemplate->caps;
        while (caps) {
 	  GstType *type;

          printf("      '%s':\n",caps->name);

	  type = gst_type_find_by_id (caps->id);
	  if (type) 
            printf("        MIME type: '%s':\n",type->mime);
	  else
            printf("        MIME type: 'unknown/unknown':\n");

	  if (caps->properties)
            print_props(caps->properties,"        ");

	  caps = caps->next;
        }
      }

      printf("\n");
    }
  } else
    printf("  none\n");

  have_flags = FALSE;

  printf("Element Flags:\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_COMPLEX)) {
    printf("  GST_ELEMENT_COMPLEX\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_DECOUPLED)) {
    printf("  GST_ELEMENT_DECOUPLED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_THREAD_SUGGESTED)) {
    printf("  GST_ELEMENT_THREADSUGGESTED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_NO_SEEK)) {
    printf("  GST_ELEMENT_NO_SEEK\n");
    have_flags = TRUE;
  }
  if (!have_flags)
    printf("  no flags set\n");



  printf("\nElement Implementation:\n");

  if (element->loopfunc)
    printf("  loopfunc()-based element: %s\n",GST_DEBUG_FUNCPTR_NAME(element->loopfunc));
  else
    printf("  No loopfunc(), must be chain-based or not configured yet\n");

  printf("  Has change_state() function: %s\n",
         GST_DEBUG_FUNCPTR_NAME(gstelement_class->change_state));
#ifndef GST_DISABLE_LOADSAVE
  printf("  Has custom save_thyself() function: %s\n",
         GST_DEBUG_FUNCPTR_NAME(gstobject_class->save_thyself));
  printf("  Has custom restore_thyself() function: %s\n",
         GST_DEBUG_FUNCPTR_NAME(gstobject_class->restore_thyself));
#endif



  printf("\nPads:\n");
  if (element->numpads) {
    pads = gst_element_get_pad_list(element);
    while (pads) {
      pad = GST_PAD(pads->data);
      pads = g_list_next(pads);
      realpad = GST_PAD_REALIZE(pad);

      if (gst_pad_get_direction(pad) == GST_PAD_SRC)
        printf("  SRC: '%s'",gst_pad_get_name(pad));
      else if (gst_pad_get_direction(pad) == GST_PAD_SINK)
        printf("  SINK: '%s'",gst_pad_get_name(pad));
      else
        printf("  UNKNOWN!!!: '%s'\n",gst_pad_get_name(pad));

      if (GST_IS_GHOST_PAD(pad))
        printf(", ghost of real pad %s:%s\n",GST_DEBUG_PAD_NAME(realpad));
      else
        printf("\n");

      printf("    Implementation:\n");
      if (realpad->chainfunc)
        printf("      Has chainfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(realpad->chainfunc));
      if (realpad->getfunc)
        printf("      Has getfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(realpad->getfunc));
      if (realpad->getregionfunc)
        printf("      Has getregionfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(realpad->getregionfunc));

      if (pad->padtemplate)
        printf("    Pad Template: '%s'\n",pad->padtemplate->name_template);

      if (realpad->caps) {
        printf("    Capabilities:\n");
        caps = realpad->caps;
        while (caps) {
	  GstType *type;

          printf("      '%s':\n",caps->name);

	  type = gst_type_find_by_id (caps->id);
	  if (type) 
            printf("        MIME type: '%s':\n",type->mime);
	  else
            printf("        MIME type: 'unknown/unknown':\n");

	  if (caps->properties)
            print_props(caps->properties,"        ");

	  caps = caps->next;
        }
      }
    }
  } else
    printf("  none\n");

  property_specs = g_object_class_list_properties(G_OBJECT_GET_CLASS (element), &num_properties);
  printf("\nElement Arguments:\n");

  for (i=0;i<num_properties;i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];

    g_value_init (&value, param->value_type);
    g_object_get_property (G_OBJECT (element), param->name, &value);

    printf("  %-40.40s: ",param->name);
    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING: printf("String (Default \"%s\")", g_value_get_string (&value));break;
      case G_TYPE_BOOLEAN: printf("Boolean (Default %s)", (g_value_get_boolean (&value)?"true":"false"));break;
      case G_TYPE_ULONG: printf("Unsigned Long (Default %lu)", g_value_get_ulong (&value));break;
      case G_TYPE_LONG: printf("Long (Default %ld)", g_value_get_long (&value));break;
      case G_TYPE_UINT: printf("Unsigned Integer (Default %u)", g_value_get_uint (&value));break;
      case G_TYPE_INT: printf("Integer (Default %d)", g_value_get_int (&value));break;
      case G_TYPE_FLOAT: printf("Float (Default %f)", g_value_get_float (&value));break;
      case G_TYPE_DOUBLE: printf("Double (Default %f)", g_value_get_double (&value));break;
      default:
        if (param->value_type == GST_TYPE_FILENAME)
          printf("Filename");
        else if (G_IS_PARAM_SPEC_ENUM (param)) {
          GEnumValue *values;
	  guint j = 0;

          printf("Enum \"%s\" (default %d)", g_type_name (G_VALUE_TYPE (&value)),
				  g_value_get_enum (&value));
	  values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;

	  while (values[j].value_name) {
            printf("\n    (%d): \t%s", values[j].value, values[j].value_nick);
	    j++; 
	  }
	  /* g_type_class_unref (ec); */
	}
        else
          printf("unknown %d", param->value_type);
        break;
    }
    printf("\n");
  }
    /*
  g_free (args);
  */
  if (num_properties == 0) g_print ("  none\n");

  {
    guint *signals;
    guint nsignals;
    gint i;
    GSignalQuery *query;

    printf("\nElement Signals:\n");
    
    signals = g_signal_list_ids (G_OBJECT_TYPE (element), &nsignals);

    for (i=0; i<nsignals; i++) {
      gint n_params;
      GType return_type;
      const GType *param_types;
      gint j;
      
      query = g_new0(GSignalQuery,1);
      g_signal_query (signals[i], query);
      n_params = query->n_params;
      return_type = query->return_type;
      param_types = query->param_types;

      printf ("  \"%s\" :\t %s user_function (%s* object, \n", query->signal_name, g_type_name (return_type),
		      g_type_name (G_OBJECT_TYPE (element)));

      for (j=0; j<n_params; j++) {
        printf ("    \t\t\t\t%s arg%d,\n", g_type_name (param_types[j]), j);
      }
      printf ("    \t\t\t\tgpointer user_data);\n");
    }
    if (nsignals == 0) g_print ("  none\n");
  }
  

  /* for compound elements */
  if (GST_IS_BIN(element)) {
    printf("\nChildren:\n");
    children = gst_bin_get_list(GST_BIN(element));
    if (!children) 
      g_print ("  none\n");
    else {
      while (children) {
        child = GST_ELEMENT (children->data);
        children = g_list_next (children);

        g_print("  %s\n",GST_ELEMENT_NAME(child));
      }
    }
  }

  return 0;
}

static void 
print_element_list (void) 
{
  GList *plugins;

  plugins = gst_plugin_get_list();
  while (plugins) {
    GList *features;
    GstPlugin *plugin;
    
    plugin = (GstPlugin*)(plugins->data);
    plugins = g_list_next (plugins);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENTFACTORY (feature)) {
        GstElementFactory *factory;

        factory = GST_ELEMENTFACTORY (feature);
        printf("%s:  %s: %s\n",plugin->name, GST_OBJECT_NAME (factory) ,factory->details->longname);
      }
      else if (GST_IS_AUTOPLUGFACTORY (feature)) {
        GstAutoplugFactory *factory;

        factory = GST_AUTOPLUGFACTORY (feature);
        printf("%s:  %s: %s\n", plugin->name, GST_OBJECT_NAME (factory), factory->longdesc);
      }
      else if (GST_IS_TYPEFACTORY (feature)) {
        GstTypeFactory *factory;

        factory = GST_TYPEFACTORY (feature);
        printf("%s:  %s: %s\n", plugin->name, factory->mime, factory->exts);

        if (factory->typefindfunc)
          printf("      Has typefind function: %s\n",GST_DEBUG_FUNCPTR_NAME(factory->typefindfunc));
      }
      else if (GST_IS_SCHEDULERFACTORY (feature)) {
        GstSchedulerFactory *factory;

        factory = GST_SCHEDULERFACTORY (feature);
        printf("%s:  %s: %s\n", plugin->name, GST_OBJECT_NAME (factory), factory->longdesc);
      }
      else {
        printf("%s:  %s (%s)\n", plugin->name, gst_object_get_name (GST_OBJECT (feature)), 
	  	      g_type_name (G_OBJECT_TYPE (feature)));
      }

      features = g_list_next (features);
    }
  }
}

static void
print_plugin_info (GstPlugin *plugin)
{
  GList *features;
  
  printf("Plugin Details:\n");
  printf("  Name:\t\t%s\n",plugin->name);
  printf("  Long Name:\t%s\n",plugin->longname);
  printf("  Filename:\t%s\n",plugin->filename);
  printf("\n");

  features = gst_plugin_get_feature_list (plugin);

  while (features) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (features->data);

    if (GST_IS_ELEMENTFACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENTFACTORY (feature);
      printf("  %s: %s\n", GST_OBJECT_NAME (factory) ,factory->details->longname);
    }
    else if (GST_IS_AUTOPLUGFACTORY (feature)) {
      GstAutoplugFactory *factory;

      factory = GST_AUTOPLUGFACTORY (feature);
      printf("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
    }
    else if (GST_IS_TYPEFACTORY (feature)) {
      GstTypeFactory *factory;

      factory = GST_TYPEFACTORY (feature);
      printf("  %s: %s\n", factory->mime, factory->exts);

      if (factory->typefindfunc)
        printf("      Has typefind function: %s\n",GST_DEBUG_FUNCPTR_NAME(factory->typefindfunc));
    }
    else if (GST_IS_SCHEDULERFACTORY (feature)) {
      GstSchedulerFactory *factory;

      factory = GST_SCHEDULERFACTORY (feature);
      printf("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
    }
    else {
      printf("  %s (%s)\n", gst_object_get_name (GST_OBJECT (feature)), 
		      g_type_name (G_OBJECT_TYPE (feature)));
    }


    features = g_list_next (features);
  }
  printf("\n");
}


int 
main (int argc, char *argv[]) 
{
  GstElementFactory *factory;
  GstPlugin *plugin;
  gchar *so;

  gst_init(&argc,&argv);

  /* if no arguments, print out list of elements */
  if (argc == 1) {
    print_element_list();

  /* else we try to get a factory */
  } else {
    /* first check for help */
    if (strstr(argv[1],"-help")) {
      printf("Usage: %s\t\t\tList all registered elements\n",argv[0]);
      printf("       %s element-name\tShow element details\n",argv[0]);
      printf("       %s plugin-name[.so]\tShow information about plugin\n",argv[0]);
      return 0;
    }

    /* only search for a factory if there's not a '.so' */
    if (! strstr(argv[1],".so")) {
      factory = gst_elementfactory_find (argv[1]);

      /* if there's a factory, print out the info */
      if (factory)
        return print_element_info(factory);
    } else {
      /* strip the .so */
      so = strstr(argv[1],".so");
      so[0] = '\0';
    }

    /* otherwise assume it's a plugin */
    plugin = gst_plugin_find (argv[1]);

    /* if there is such a plugin, print out info */

    if (plugin) {
      print_plugin_info(plugin);

    } else {
      printf("no such element or plugin '%s'\n",argv[1]);
      return -1;
    }
  }

  return 0;
}
