#include <gst/gst.h>
#include <gst/control/control.h>
#include <string.h>

static void 
print_prop (GstPropsEntry *prop, gboolean showname, gchar *pfx) 
{
  GstPropsType type;

  if (showname)
    printf("%s%s: ", pfx, gst_props_entry_get_name (prop));
  else
    printf(pfx);

  type = gst_props_entry_get_type (prop);

  switch (type) {
    case GST_PROPS_INT_TYPE:
    {
      gint val;
      gst_props_entry_get_int (prop, &val);
      printf("Integer: %d\n", val);
      break;
    }
    case GST_PROPS_INT_RANGE_TYPE:
    {
      gint min, max;
      gst_props_entry_get_int_range (prop, &min, &max);
      printf("Integer range: %d - %d\n", min, max);
      break;
    }
    case GST_PROPS_FLOAT_TYPE:
    {
      gfloat val;
      gst_props_entry_get_float (prop, &val);
      printf("Float: %f\n", val);
      break;
    }
    case GST_PROPS_FLOAT_RANGE_TYPE:
    {
      gfloat min, max;
      gst_props_entry_get_float_range (prop, &min, &max);
      printf("Float range: %f - %f\n", min, max);
      break;
    }
    case GST_PROPS_BOOL_TYPE:
    {
      gboolean val;
      gst_props_entry_get_boolean (prop, &val);
      printf("Boolean: %s\n", val ? "TRUE" : "FALSE");
      break;
    }
    case GST_PROPS_STRING_TYPE:
    {
      const gchar *val;
      gst_props_entry_get_string (prop, &val);
      printf("String: %s\n", val);
      break;
    }
    case GST_PROPS_FOURCC_TYPE:
    {
      guint32 val;
      gst_props_entry_get_fourcc_int (prop, &val);
      printf("FourCC: '%c%c%c%c'\n",
             (gchar)( val        & 0xff), 
	     (gchar)((val >> 8)  & 0xff),
             (gchar)((val >> 16) & 0xff), 
	     (gchar)((val >> 24) & 0xff));
      break;
    }
    case GST_PROPS_LIST_TYPE:
    {
      const GList *list;
      gchar *longprefix;

      gst_props_entry_get_list (prop, &list);
      printf ("List:\n");
      longprefix = g_strdup_printf ("%s  ", pfx);
      while (list) {
        GstPropsEntry *listentry;

        listentry = (GstPropsEntry*) (list->data);
        print_prop (listentry, FALSE, longprefix);

        list = g_list_next (list);
      }
      g_free (longprefix);
      break;
    }
    default:
      printf("unknown props %d\n", type);
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

  element = gst_element_factory_create(factory,"element");
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

  printf("\nElement Flags:\n");
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

  if (GST_IS_BIN (element)) {
    printf("\nBin Flags:\n");
    if (GST_FLAG_IS_SET(element,GST_BIN_FLAG_MANAGER)) {
      printf("  GST_BIN_FLAG_MANAGER\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET(element,GST_BIN_SELF_SCHEDULABLE)) {
      printf("  GST_BIN_SELF_SCHEDULABLE\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET(element,GST_BIN_FLAG_PREFER_COTHREADS)) {
      printf("  GST_BIN_FLAG_PREFER_COTHREADS\n");
      have_flags = TRUE;
    }
    if (!have_flags)
      printf("  no flags set\n");
  }



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

  have_flags = FALSE;

  printf("\nClocking Interaction:\n");
  if (element->setclockfunc) {
    printf("  element requires a clock\n");
    have_flags = TRUE;
  }
  if (element->getclockfunc) {
    GstClock *clock;

    clock = gst_element_get_clock (element);
    printf("  element provides a clock: %s\n", GST_OBJECT_NAME(clock));
    have_flags = TRUE;
  }
  if (!have_flags) {
    printf("  none\n");
  }


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

    if (param->flags & G_PARAM_READABLE) {
      g_value_init (&value, param->value_type);
      g_object_get_property (G_OBJECT (element), param->name, &value);
    }

    printf("  %-40.40s: ",param->name);
    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING: 
        printf("String (Default \"%s\")", g_value_get_string (&value));
        break;
      case G_TYPE_POINTER: 
        printf("Pointer (Default \"%p\")", g_value_get_pointer (&value));
        break;
      case G_TYPE_BOOLEAN: 
        printf("Boolean (Default %s)", (g_value_get_boolean (&value)?"true":"false"));
        break;
      case G_TYPE_ULONG: 
        printf("Unsigned Long (Default %lu, Range %lu -> %lu)", g_value_get_ulong (&value), 
        ((GParamSpecULong*)param)->minimum, ((GParamSpecULong*)param)->maximum);
        break;
      case G_TYPE_LONG: 
        printf("Long (Default %ld, Range %ld -> %ld)", g_value_get_long (&value), 
        ((GParamSpecLong*)param)->minimum, ((GParamSpecLong*)param)->maximum);
        break;
      case G_TYPE_UINT: 
        printf("Unsigned Integer (Default %u, Range %u -> %u)", g_value_get_uint (&value), 
        ((GParamSpecUInt*)param)->minimum, ((GParamSpecUInt*)param)->maximum);
        break;
      case G_TYPE_INT: 
        printf("Integer (Default %d, Range %d -> %d)", g_value_get_int (&value), 
        ((GParamSpecInt*)param)->minimum, ((GParamSpecInt*)param)->maximum);
        break;
      case G_TYPE_INT64: 
        printf("64 Bit Integer (Default %lld, Range %lld -> %lld)", g_value_get_int64 (&value), 
        ((GParamSpecInt64*)param)->minimum, ((GParamSpecInt64*)param)->maximum);
        break;
      case G_TYPE_FLOAT: 
        printf("Float (Default %f, Range %f -> %f)", g_value_get_float (&value), 
        ((GParamSpecFloat*)param)->minimum, ((GParamSpecFloat*)param)->maximum);
        break;
      case G_TYPE_DOUBLE: 
        printf("Double (Default %f, Range %f -> %f)", g_value_get_double (&value), 
        ((GParamSpecDouble*)param)->minimum, ((GParamSpecDouble*)param)->maximum);
        break;
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
          printf("unknown %ld", param->value_type);
        break;
    }
    printf("\n");
  }
    /*
  g_free (args);
  */
  if (num_properties == 0) g_print ("  none\n");

  /* Dynamic Parameters block */
  {
    GstDParamManager* dpman;
    GParamSpec** specs;
    gint x;
    
    printf("\nDynamic Parameters:\n");
    if((dpman = gst_dpman_get_manager (element))){
      specs = gst_dpman_list_dparam_specs(dpman);
      for (x=0; specs[x] != NULL; x++){
        printf("  %-40.40s: ",g_param_spec_get_name(specs[x]));
        
        switch (G_PARAM_SPEC_VALUE_TYPE (specs[x])) {
          case G_TYPE_INT64: 
            printf("64 Bit Integer (Default %lld, Range %lld -> %lld)", 
            ((GParamSpecInt64*)specs[x])->default_value,
            ((GParamSpecInt64*)specs[x])->minimum, 
            ((GParamSpecInt64*)specs[x])->maximum);
            break;
          case G_TYPE_INT: 
            printf("Integer (Default %d, Range %d -> %d)", 
            ((GParamSpecInt*)specs[x])->default_value,
            ((GParamSpecInt*)specs[x])->minimum, 
            ((GParamSpecInt*)specs[x])->maximum);
            break;
          case G_TYPE_FLOAT: 
            printf("Float (Default %f, Range %f -> %f)", 
            ((GParamSpecFloat*)specs[x])->default_value,
            ((GParamSpecFloat*)specs[x])->minimum, 
            ((GParamSpecFloat*)specs[x])->maximum);
            break;
          default: printf("unknown %ld", G_PARAM_SPEC_VALUE_TYPE (specs[x]));
        }
        printf("\n");
      }
      g_free(specs);
    }
    else {
      g_print ("  none\n");
    }
  }

  /* Signals Block */  
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
    children = (GList *) gst_bin_get_list(GST_BIN(element));
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

  plugins = (GList *) gst_plugin_get_list();
  while (plugins) {
    GList *features;
    GstPlugin *plugin;
    
    plugin = (GstPlugin*)(plugins->data);
    plugins = g_list_next (plugins);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;

        factory = GST_ELEMENT_FACTORY (feature);
        printf("%s element:  %s: %s\n",plugin->name, GST_OBJECT_NAME (factory) ,factory->details->longname);
      }
      else if (GST_IS_AUTOPLUG_FACTORY (feature)) {
        GstAutoplugFactory *factory;

        factory = GST_AUTOPLUG_FACTORY (feature);
        printf("%s autoplug:  %s: %s\n", plugin->name, GST_OBJECT_NAME (factory), factory->longdesc);
      }
      else if (GST_IS_TYPE_FACTORY (feature)) {
        GstTypeFactory *factory;

        factory = GST_TYPE_FACTORY (feature);
        printf("%s type:  %s: %s\n", plugin->name, factory->mime, factory->exts);

        if (factory->typefindfunc)
          printf("      Has typefind function: %s\n",GST_DEBUG_FUNCPTR_NAME(factory->typefindfunc));
      }
      else if (GST_IS_SCHEDULER_FACTORY (feature)) {
        GstSchedulerFactory *factory;

        factory = GST_SCHEDULER_FACTORY (feature);
        printf("%s scheduler:  %s: %s\n", plugin->name, GST_OBJECT_NAME (factory), factory->longdesc);
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

    if (GST_IS_ELEMENT_FACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENT_FACTORY (feature);
      printf("  %s: %s\n", GST_OBJECT_NAME (factory) ,factory->details->longname);
    }
    else if (GST_IS_AUTOPLUG_FACTORY (feature)) {
      GstAutoplugFactory *factory;

      factory = GST_AUTOPLUG_FACTORY (feature);
      printf("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
    }
    else if (GST_IS_TYPE_FACTORY (feature)) {
      GstTypeFactory *factory;

      factory = GST_TYPE_FACTORY (feature);
      printf("  %s: %s\n", factory->mime, factory->exts);

      if (factory->typefindfunc)
        printf("      Has typefind function: %s\n",GST_DEBUG_FUNCPTR_NAME(factory->typefindfunc));
    }
    else if (GST_IS_SCHEDULER_FACTORY (feature)) {
      GstSchedulerFactory *factory;

      factory = GST_SCHEDULER_FACTORY (feature);
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
  gst_control_init(&argc,&argv);
  
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
      factory = gst_element_factory_find (argv[1]);

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
