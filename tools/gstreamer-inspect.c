#include <gst/gst.h>

// this must be built within the gstreamer dir, else this will fail
#include <gst/gstpropsprivate.h>

void print_prop(GstPropsEntry *prop,gboolean showname,gchar *pfx) {
  GList *list;
  GstPropsEntry *listentry;
  gchar *longprefix;

  if (showname)
    printf("%s%s: ",pfx,g_quark_to_string(prop->propid));
  else
    printf(pfx);

  switch (prop->propstype) {
    case GST_PROPS_INT_ID_NUM:
      printf("Integer: %d\n",prop->data.int_data);
      break;
    case GST_PROPS_INT_RANGE_ID_NUM:
      printf("Integer range: %d - %d\n",prop->data.int_range_data.min,
             prop->data.int_range_data.max);
      break;
    case GST_PROPS_BOOL_ID_NUM:
      printf("Boolean: %s\n",prop->data.bool_data ? "TRUE" : "FALSE");
      break;
    case GST_PROPS_FOURCC_ID_NUM:
      printf("FourCC: %c%c%c%c\n",
             prop->data.fourcc_data>>24 & 0xff,prop->data.fourcc_data>>16 & 0xff,
             prop->data.fourcc_data>>8 & 0xff,prop->data.fourcc_data & 0xff);
      break;
    case GST_PROPS_LIST_ID_NUM:
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
      printf("\n");
  }
}

void print_props(GstProps *properties,gchar *pfx) {
  GList *props;
  GstPropsEntry *prop;

  props = properties->properties;
  while (props) {
    prop = (GstPropsEntry*)(props->data);
    props = g_list_next(props);

    print_prop(prop,TRUE,pfx);
  }
}

/*
struct _GstPropsEntry {
  GQuark    propid;
  GstPropsId propstype;

  union {
    // flat values
    gboolean bool_data;
    guint32  fourcc_data;
    gint     int_data;

    // structured values
    struct {
      GList *entries;
    } list_data;
    struct {
      gint min;
      gint max;
    } int_range_data;
  } data;
};
*/

int main(int argc,char *argv[]) {
  GstElementFactory *factory;
  GstElement *element;
  GstElementClass *gstelement_class;
  GList *pads, *caps;
  GstPad *pad;
  GstPadTemplate *padtemplate;
  GstCaps *cap;
  GtkArg *args;
  guint32 *flags;
  gint num_args,i;

  gst_init(&argc,&argv);

  factory = gst_elementfactory_find(argv[1]);
  if (!factory) {
    g_print ("no elementfactory for element '%s' exists\n", argv[1]);
    return -1;
  }
	  
  element = gst_elementfactory_create(factory,argv[1]);
  gstelement_class = GST_ELEMENT_CLASS (GTK_OBJECT (element)->klass);

  printf("Factory Details:\n");
  printf("  Long name:\t%s\n",factory->details->longname);
  printf("  Class:\t%s\n",factory->details->klass);
  printf("  Description:\t%s\n",factory->details->description);
  printf("  Version:\t%s\n",factory->details->version);
  printf("  Author(s):\t%s\n",factory->details->author);
  printf("  Copyright:\t%s\n",factory->details->copyright);
  printf("\n");

  printf("Pad Templates:\n");
  if (g_list_length (factory->padtemplates) > 0) {
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
        printf("    Exists: Always\n");
      else if (padtemplate->presence == GST_PAD_SOMETIMES)
        printf("    Exists: Sometimes\n");
      else
        printf("    Exists: UNKNOWN!!!\n");

      if (padtemplate->caps) {
        printf("    Capabilities:\n");
        caps = padtemplate->caps;
        while (caps) {
 	  GstType *type;

          cap = (GstCaps*)(caps->data);
          caps = g_list_next(caps);

          printf("      '%s':\n",cap->name);

	  type = gst_type_find_by_id (cap->id);
	  if (type) 
            printf("        MIME type: '%s':\n",type->mime);
	  else
            printf("        MIME type: 'unknown/unknown':\n");

	  if (cap->properties)
            print_props(cap->properties,"        ");
        }
      }

      printf("\n");
    }
  } else
    printf("  none\n\n");

  printf("Element Flags:\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_COMPLEX))
    printf("  GST_ELEMENT_COMPLEX\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_DECOUPLED))
    printf("  GST_ELEMENT_DECOUPLED\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_THREAD_SUGGESTED))
    printf("  GST_ELEMENT_THREADSUGGESTED\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_NO_SEEK))
    printf("  GST_ELEMENT_NO_SEEK\n");
  if (! GST_FLAG_IS_SET(element, GST_ELEMENT_COMPLEX | GST_ELEMENT_DECOUPLED |
                                 GST_ELEMENT_THREAD_SUGGESTED | GST_ELEMENT_NO_SEEK))
    printf("  no flags set\n");
  printf("\n");


  printf("Element Implementation:\n");
  if (element->loopfunc)
    printf("  loopfunc()-based element\n");
  else
    printf("  No loopfunc(), must be chain-based or not configured yet\n");
  if (gstelement_class->change_state)
    printf("  Has change_state() function\n");
  else
    printf("  No change_state() class function\n");
  if (gstelement_class->save_thyself)
    printf("  Has custom save_thyself() class function\n");
  if (gstelement_class->restore_thyself)
    printf("  Has custom restore_thyself() class function\n");
  printf("\n");


  printf("Pads:\n");
  if (element->numpads) {
    pads = gst_element_get_pad_list(element);
    while (pads) {
      pad = GST_PAD(pads->data);
      pads = g_list_next(pads);

      if (gst_pad_get_direction(pad) == GST_PAD_SRC)
        printf("  SRC: '%s'\n",gst_pad_get_name(pad));
      else if (gst_pad_get_direction(pad) == GST_PAD_SINK)
        printf("  SINK: '%s'\n",gst_pad_get_name(pad));
      else
        printf("  UNKNOWN!!!: '%s'\n",gst_pad_get_name(pad));

      printf("    Implementation:\n");
      if (pad->chainfunc)
        printf("      Has chainfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->chainfunc));
      if (pad->getfunc)
        printf("      Has getfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->getfunc));
      if (pad->getregionfunc)
        printf("      Has getregionfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->getregionfunc));
      if (pad->qosfunc)
        printf("      Has qosfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->qosfunc));
      //if (pad->eosfunc) {
        //if (pad->eosfunc == gst_pad_eos_func)
        //  printf("      Has default eosfunc() gst_pad_eos_func()\n");
        //else
        //  printf("      Has eosfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->eosfunc));
      //}

      if (pad->padtemplate)
        printf("    Pad Template: '%s'\n",pad->padtemplate->name_template);

      if (pad->caps) {
        printf("    Capabilities:\n");
        caps = pad->caps;
        while (caps) {
          cap = (GstCaps*)(caps->data);
          caps = g_list_next(caps);
          printf("      '%s':\n",cap->name);
	  if (cap->properties)
            print_props(cap->properties,"        ");
        }
      }

      printf("\n");
    }
  } else
    printf("  none\n\n");

  printf("Element Arguments:\n");
  args = gtk_object_query_args(GTK_OBJECT_TYPE(element), &flags, &num_args);
  for (i=0;i<num_args;i++) {
    gtk_object_getv(GTK_OBJECT(element), 1, &args[i]);

    printf("  %s: ",args[i].name);
    switch (args[i].type) {
      case GTK_TYPE_STRING: printf("String");break;
      case GTK_TYPE_BOOL: printf("Boolean");break;
      case GTK_TYPE_ULONG:
      case GTK_TYPE_LONG:
      case GTK_TYPE_UINT:
      case GTK_TYPE_INT: printf("Integer");break;
      case GTK_TYPE_FLOAT:
      case GTK_TYPE_DOUBLE: printf("Float");break;
      default:
        if (args[i].type == GST_TYPE_FILENAME)
          printf("Filename");
        else
          printf("unknown");
        break;
    }
    printf("\n");
  }
  if (num_args == 0) g_print ("  none");
  printf("\n");

  return 0;
}
