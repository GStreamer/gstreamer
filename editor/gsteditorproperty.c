/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include <ctype.h>
#include <gnome.h>
#include <gst/gst.h>
#include <gst/gstpropsprivate.h>

#include "gsteditorproperty.h"
#include "gsteditorimage.h"

/* class functions */
static void 		gst_editor_property_class_init	(GstEditorPropertyClass *klass);
static void 		gst_editor_property_init	(GstEditorProperty *property);

static void 		gst_editor_property_set_arg	(GtkObject *object,GtkArg *arg,guint id);
static void 		gst_editor_property_get_arg	(GtkObject *object,GtkArg *arg,guint id);

static GtkWidget*	create_property_entry		(GtkArg *arg, GstElement *element);

static GtkWidget* 	gst_editor_property_create 	(GstEditorProperty *property, GstEditorElement *element);


enum {
  ARG_0,
};

enum {
  SIGNAL_ELEMENT_SELECTED,
  SIGNAL_IN_SELECTION_MODE,
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
static guint gst_editor_property_signals[LAST_SIGNAL] = { 0 };

static GstEditorProperty *_the_property = NULL;

GtkType 
gst_editor_property_get_type (void) 
{
  static GtkType property_type = 0;

  if (!property_type) {
    static const GtkTypeInfo property_info = {
      "GstEditorProperty",
      sizeof(GstEditorProperty),
      sizeof(GstEditorPropertyClass),
      (GtkClassInitFunc)gst_editor_property_class_init,
      (GtkObjectInitFunc)gst_editor_property_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    property_type = gtk_type_unique(gtk_object_get_type(),&property_info);
  }
  return property_type;
}

static void 
gst_editor_property_class_init (GstEditorPropertyClass *klass) 
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gst_editor_property_signals[SIGNAL_ELEMENT_SELECTED] =
    gtk_signal_new("element_selected",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorPropertyClass,element_selected),
                   gtk_marshal_NONE__INT,GTK_TYPE_NONE,1,
                   GTK_TYPE_INT);

  gst_editor_property_signals[SIGNAL_IN_SELECTION_MODE] =
    gtk_signal_new("in_selection_mode",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorPropertyClass,in_selection_mode),
                   gtk_marshal_NONE__INT,GTK_TYPE_NONE,1,
                   GTK_TYPE_INT);

  gtk_object_class_add_signals(object_class,gst_editor_property_signals,LAST_SIGNAL);

  object_class->set_arg = gst_editor_property_set_arg;
  object_class->get_arg = gst_editor_property_get_arg;
}

static void 
gst_editor_property_init (GstEditorProperty *property) 
{
  property->shown_element = NULL;
}

typedef struct {
  GstEditorProperty *property;
  GModule *symbols;
} connect_struct;
  
/* we need more control here so... */
static void 
gst_editor_property_connect_func (const gchar *handler_name,
		             GtkObject *object,
			     const gchar *signal_name,
			     const gchar *signal_data,
			     GtkObject *connect_object,
			     gboolean after,
			     gpointer user_data) 
{
  GtkSignalFunc func;
  connect_struct *data = (connect_struct *)user_data;

  if (!g_module_symbol(data->symbols, handler_name, (gpointer *)&func))
    g_warning("gsteditorproperty: could not find signal handler '%s'.", handler_name);
  else {
    if (after)
      gtk_signal_connect_after(object, signal_name, func, (gpointer) data->property);
    else
      gtk_signal_connect(object, signal_name, func, (gpointer) data->property);
  }
}

static GstEditorProperty*
gst_editor_property_new (void) 
{
  GstEditorProperty *property;
  GtkWidget *property_window;
  connect_struct data;
  GModule *symbols;

  property = GST_EDITOR_PROPERTY(gtk_type_new(GST_TYPE_EDITOR_PROPERTY));

  symbols = g_module_open(NULL, 0);

  data.property = property;
  data.symbols = symbols;

  property->xml = glade_xml_new("editor.glade", "property_window");
  glade_xml_signal_autoconnect_full (property->xml, gst_editor_property_connect_func, &data);

  property_window = glade_xml_get_widget(property->xml, "property_window");
  gtk_widget_show(property_window);

  return property;
}

GstEditorProperty*
gst_editor_property_get (void) 
{
  if (!_the_property) {
    _the_property = gst_editor_property_new();
  }
  return _the_property;
}

static void 
gst_editor_property_set_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorProperty *property;

  /* get the major types of this object */
  property = GST_EDITOR_PROPERTY(object);

  switch (id) {
    default:
      g_warning("gsteditorproperty: unknown arg!");
      break;
  }
}

static void 
gst_editor_property_get_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorProperty *property;

  /* get the major types of this object */
  property = GST_EDITOR_PROPERTY(object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static void 
on_name_changed (GtkEntry *entry, GstEditorElement *element) 
{
  gst_editor_element_set_name(element, gtk_entry_get_text(GTK_ENTRY(entry)));
}

static gchar*
make_readable_name (gchar *name) 
{
  gchar *new;
  gchar *colon;
  gboolean inupper;
  gint len, i;

  colon = strstr(name, "::");

  if (!colon)
    new = g_strdup(name);
  else
    new = g_strdup(&colon[2]);

  new = g_strdelimit(new, G_STR_DELIMITERS, ' ');

  len = strlen(new);
  inupper = TRUE;
  for (i=0; i<len; i++) {
    if (inupper) new[i] = toupper(new[i]);
    inupper = FALSE;
    if (new[i] == ' ')
      inupper = TRUE;
  }

  return new;
}

static gchar* 
gst_editor_props_show_func (GstPropsEntry *entry) 
{
  switch (entry->propstype) {
    case GST_PROPS_INT_ID_NUM:
      return g_strdup_printf ("%d", entry->data.int_data);
      break;
    case GST_PROPS_INT_RANGE_ID_NUM:
      return g_strdup_printf ("%d-%d", entry->data.int_range_data.min, entry->data.int_range_data.max);
      break;
    case GST_PROPS_FOURCC_ID_NUM:
      return g_strdup_printf ("%4.4s", (gchar *)&entry->data.fourcc_data);
      break;
    case GST_PROPS_BOOL_ID_NUM:
      return g_strdup_printf ("%s", (entry->data.bool_data ? "TRUE" : "FALSE"));
      break;
    default:
      break;
  }
  return g_strdup ("unknown");
}

static void
gst_editor_add_caps_to_tree (GstCaps *caps, GtkWidget *tree, GtkCTreeNode *padnode) 
{
  if (caps) {
    GstProps *props = gst_caps_get_props (caps);
    if (props) {
      GSList *propslist = props->properties;

      while (propslist) {
        gchar *data[2];
        GstPropsEntry *entry = (GstPropsEntry *)propslist->data;

        data[0] = g_quark_to_string (entry->propid);

	switch (entry->propstype) {
 	  case GST_PROPS_LIST_ID_NUM:
	  {
	    GList *list;
	    guint count = 0;
	    data[1] = "";

	    list = entry->data.list_data.entries;

	    while (list) {
	      data[1] = g_strconcat (data[1], (count++?", ":""),
		      gst_editor_props_show_func ((GstPropsEntry *)list->data), NULL);
	      list = g_list_next (list);
	    }
	    break;
	  }
	  default:
	    data[1] = gst_editor_props_show_func (entry);
	    break;
	}
        gtk_ctree_insert_node (GTK_CTREE (tree), padnode, NULL, data, 0, 
	      NULL, NULL, NULL, NULL, TRUE, TRUE);
	
	propslist = g_slist_next (propslist);
      }
    }
  }
}

static GtkWidget* 
gst_editor_pads_create (GstEditorProperty *property, GstEditorElement *element) 
{
  GstElement *realelement = element->element;
  GList *pads;
  GtkWidget *tree;
  gchar *columns[2];

  columns[0] = "name";
  columns[1] = "info";

  tree = gtk_ctree_new_with_titles (2, 0, columns);
  gtk_clist_set_column_width (GTK_CLIST (tree), 0, 150);
  
  gtk_clist_freeze (GTK_CLIST (tree));

  pads = gst_element_get_pad_list(realelement);

  while (pads) {
    GstPad *pad = (GstPad *)pads->data;
    GstCaps *caps = gst_pad_get_caps (pad);
    gchar *mime;
    gchar *data[2];
    GtkCTreeNode *padnode;

    if (caps) {
      GstType *type;
      type = gst_type_find_by_id (caps->id);
      mime = type->mime;
    }
    else {
      mime = "unknown/unknown";
    }

    data[0] = g_strdup (gst_pad_get_name (pad));
    data[1] = mime;
    padnode = gtk_ctree_insert_node (GTK_CTREE (tree), NULL, NULL, data, 0, 
		    NULL, NULL, NULL, NULL, FALSE, TRUE);

    gst_editor_add_caps_to_tree (caps, tree, padnode);

    pads = g_list_next (pads);
  }

  pads = gst_element_get_padtemplate_list(realelement);
  while (pads) {
    GstPadTemplate *templ = (GstPadTemplate *)pads->data;
    GstCaps *caps = templ->caps;
    gchar *mime;
    gchar *data[2];
    GtkCTreeNode *padnode;

    if (caps) {
      GstType *type;
      type = gst_type_find_by_id (caps->id);
      mime = type->mime;
    }
    else {
      mime = "unknown/unknown";
    }

    data[0] = g_strdup (templ->name_template);
    data[1] = mime;
    padnode = gtk_ctree_insert_node (GTK_CTREE (tree), NULL, NULL, data, 0, 
		    NULL, NULL, NULL, NULL, FALSE, TRUE);

    gst_editor_add_caps_to_tree (caps, tree, padnode);

    pads = g_list_next (pads);
  }

  gtk_clist_thaw (GTK_CLIST (tree));

  gtk_widget_show(tree);
  gtk_object_ref(GTK_OBJECT(tree));
  return tree;
}

typedef struct {
  GtkWidget *properties;
  GtkWidget *pads;
  GtkWidget *signals;
} properties_widgets;
  
void 
gst_editor_property_show (GstEditorProperty *property, GstEditorElement *element) 
{
  GtkType type;

  if (property->shown_element != element) {
    gtk_object_set (GTK_OBJECT (element), "active",TRUE,  NULL);
    if (property->shown_element) {
      gtk_object_set (GTK_OBJECT (property->shown_element), "active",FALSE,  NULL);
    }
  }
  else return;

  type = GTK_OBJECT_TYPE(element->element);
  if (type != GTK_TYPE_INVALID) {
    GtkWidget *property_box, *pads_window;
    properties_widgets *widgets;

    property_box = glade_xml_get_widget(property->xml, "property_vbox");
    pads_window = glade_xml_get_widget(property->xml, "pads_window");

    if (property->shown_element) {
      properties_widgets *oldwidgets;

      oldwidgets = (properties_widgets *) GST_EDITOR_PROPERTY_GET_OBJECT (property->shown_element);

      gtk_container_remove(GTK_CONTAINER(property_box), oldwidgets->properties);
      gtk_container_remove(GTK_CONTAINER(pads_window), oldwidgets->pads);
    }

    widgets = (properties_widgets *)GST_EDITOR_PROPERTY_GET_OBJECT(element);

    if (!widgets) {
      widgets = g_new0 (properties_widgets, 1);

      widgets->properties = gst_editor_property_create (property, element);
      widgets->pads = gst_editor_pads_create (property, element);

      GST_EDITOR_PROPERTY_SET_OBJECT(element, widgets);
    }

    gtk_box_pack_start(GTK_BOX(property_box), widgets->properties, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(pads_window), widgets->pads);

    property->shown_element = element;
  }
}

static GtkWidget* 
gst_editor_property_create (GstEditorProperty *property, GstEditorElement *element) 
{
  GtkWidget *table;
  GtkType type;
  GtkArg *args;
  guint32 *flags;
  guint num_args, i, count;
  GtkWidget *label, *entry;

  type = GTK_OBJECT_TYPE(element->element);

  table = gtk_table_new(1, 2, FALSE);
  gtk_table_set_row_spacings(GTK_TABLE(table), 2);

  label = gtk_label_new(_("Name:"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  gtk_object_set(GTK_OBJECT(label), "width", 100, NULL);
  gtk_widget_show(label);
  entry = gtk_entry_new();
  gtk_widget_show(entry);
  gtk_entry_set_text(GTK_ENTRY(entry), gst_element_get_name(element->element));
  gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, 0, 0, 0);

  gtk_signal_connect(GTK_OBJECT(entry), "changed", on_name_changed, element);

  args = gtk_object_query_args(type, &flags, &num_args);
  count = 1;
  for (i=0; i<num_args; i++) {
    if (flags[i] & GTK_ARG_READABLE) {
      gtk_object_getv(GTK_OBJECT(element->element), 1, &args[i]);

      entry = create_property_entry(&args[i], element->element);

      if (entry) {
        label = gtk_label_new(g_strconcat(make_readable_name(args[i].name), ":", NULL));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
	gtk_object_set(GTK_OBJECT(label), "width", 100, NULL);
	gtk_widget_show(label);

        gtk_table_attach(GTK_TABLE(table), label, 0, 1, count, count+1, GTK_FILL, 0, 0, 0);
        gtk_table_attach(GTK_TABLE(table), entry, 1, 2, count, count+1, GTK_FILL|GTK_EXPAND, 0, 0, 0);

	count++;
      }
    }
  }

  gtk_widget_show(table);
  gtk_object_ref(GTK_OBJECT(table));
  return table;
}

static void 
widget_show_toggled (GtkToggleButton *button, GtkArg *arg) 
{
  GtkWidget *window;

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(GTK_VALUE_OBJECT(*arg)));

  gtk_widget_show(window);
}

typedef struct {
  GtkArg *arg;
  GstElement *element;
} arg_data;

static void 
widget_bool_toggled (GtkToggleButton *button, arg_data *arg) 
{
  gboolean toggled;

  toggled = gtk_toggle_button_get_active(button);
  gtk_object_set (GTK_OBJECT (button), "label", (toggled? _("Yes"):_("No")), NULL);

  gdk_threads_leave ();
  gtk_object_set (GTK_OBJECT (arg->element), arg->arg->name, toggled, NULL);
  gdk_threads_enter ();
}
  
static void 
widget_adjustment_value_changed (GtkAdjustment *adjustment,
		                 arg_data *arg)
{
  gdk_threads_leave ();
  gtk_object_set (GTK_OBJECT (arg->element), arg->arg->name, (gint) adjustment->value, NULL);
  gdk_threads_enter ();
}

static void 
on_file_selected (GtkEditable *entry, arg_data *fs)
{
  gtk_object_set(GTK_OBJECT(fs->element), fs->arg->name, 
		  gtk_entry_get_text(GTK_ENTRY(entry)), NULL);
}

static GtkWidget*
create_property_entry (GtkArg *arg, GstElement *element) 
{
  GtkWidget *entry = NULL;

  // basic types
  switch (arg->type) {
    case GTK_TYPE_STRING: 
    {
      gchar *text;
      entry = gtk_entry_new();
      text = GTK_VALUE_STRING(*arg);
      if (text)
        gtk_entry_set_text(GTK_ENTRY(entry), text);
      break;
    }
    case GTK_TYPE_BOOL:
    {
      gboolean toggled;
      arg_data *data = g_new0(arg_data, 1);

      data->element = element;
      data->arg = arg;

      toggled = GTK_VALUE_BOOL(*arg);
      entry = gtk_toggle_button_new_with_label((toggled? _("Yes"):_("No")));
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(entry), toggled);
      gtk_signal_connect(GTK_OBJECT(entry), "toggled", widget_bool_toggled, data);
      break;
    }
    case GTK_TYPE_ULONG:
    case GTK_TYPE_LONG:
    case GTK_TYPE_UINT:
    case GTK_TYPE_INT:
    {
      gint value;
      GtkAdjustment *spinner_adj;
      arg_data *data = g_new0(arg_data, 1);

      data->element = element;
      data->arg = arg;

      value = GTK_VALUE_INT(*arg);
      spinner_adj = (GtkAdjustment *) gtk_adjustment_new(50.0, 0.0, 10000000.0, 1.0, 5.0, 5.0);
      entry = gtk_spin_button_new(spinner_adj, 1.0, 0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry), (gfloat) value);
      gtk_signal_connect(GTK_OBJECT(spinner_adj), "value_changed", widget_adjustment_value_changed, data);
      break;
    }
    case GTK_TYPE_FLOAT:
    case GTK_TYPE_DOUBLE:
    {
      gdouble value;
      GtkAdjustment *spinner_adj;

      value = GTK_VALUE_DOUBLE(*arg);
      spinner_adj = (GtkAdjustment *) gtk_adjustment_new(50.0, 0.0, 10000000.0, 0.1, 5.0, 5.0);
      entry = gtk_spin_button_new(spinner_adj, 1.0, 3);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry), (gfloat) value);
      break;
    }
    default:
      break;
  }
  // more extensive testing here
  if (!entry) {
    if (arg->type == GTK_TYPE_WIDGET) 
    {
      entry = gtk_toggle_button_new_with_label(_("Show..."));
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(entry), FALSE);
      gtk_signal_connect(GTK_OBJECT(entry), "toggled", widget_show_toggled, arg);
    }
    else if (GTK_FUNDAMENTAL_TYPE(arg->type) == GTK_TYPE_ENUM) {
      GtkEnumValue *values;
      guint i = 0;
      GtkWidget *menu;
      guint value = GTK_VALUE_ENUM (*arg);
      guint active = 0;

      entry = gtk_option_menu_new();
      menu = gtk_menu_new();

      values = gtk_type_enum_get_values(arg->type);
      while (values[i].value_name) {
	GtkWidget *menuitem = gtk_menu_item_new_with_label(values[i].value_nick);

	gtk_menu_append(GTK_MENU(menu), menuitem);
	gtk_widget_show(menuitem);

	if (value == values[i].value) active = i;
	i++;
      }
      gtk_menu_set_active(GTK_MENU(menu), active);
      gtk_option_menu_set_menu(GTK_OPTION_MENU(entry), menu);
    }
    else if (arg->type == GST_TYPE_FILENAME) {
      arg_data *fs = g_new0(arg_data, 1);

      entry = gnome_file_entry_new(NULL, NULL);

      fs->element = element;
      fs->arg = arg;

      gtk_signal_connect(GTK_OBJECT(gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(entry))),
                         "changed",
                         GTK_SIGNAL_FUNC(on_file_selected),
                         fs);
    }
    else {
      g_print("unknown type: %d\n", arg->type);
    }
  }
  gtk_widget_show(entry);

  return entry;
}

