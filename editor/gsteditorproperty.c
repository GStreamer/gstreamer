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

#include "gsteditorproperty.h"
#include "gsteditorimage.h"

/* class functions */
static void gst_editor_property_class_init(GstEditorPropertyClass *klass);
static void gst_editor_property_init(GstEditorProperty *property);
static void gst_editor_property_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_property_get_arg(GtkObject *object,GtkArg *arg,guint id);

static GtkWidget *create_property_entry(GtkArg *arg, GstElement *element);

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

GtkType gst_editor_property_get_type() {
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

static void gst_editor_property_class_init(GstEditorPropertyClass *klass) {
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

static void gst_editor_property_init(GstEditorProperty *property) {

  property->panels = g_hash_table_new(NULL, NULL);
  property->current = NULL;
}

typedef struct {
  GstEditorProperty *property;
  GModule *symbols;
} connect_struct;
  
/* we need more control here so... */
static void gst_editor_property_connect_func (const gchar *handler_name,
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

static GstEditorProperty *gst_editor_property_new() {
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

GstEditorProperty *gst_editor_property_get() {
  if (!_the_property) {
    _the_property = gst_editor_property_new();
  }
  return _the_property;
}

static void gst_editor_property_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorProperty *property;

  /* get the major types of this object */
  property = GST_EDITOR_PROPERTY(object);

  switch (id) {
    default:
      g_warning("gsteditorproperty: unknown arg!");
      break;
  }
}

static void gst_editor_property_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorProperty *property;

  /* get the major types of this object */
  property = GST_EDITOR_PROPERTY(object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static void on_name_changed(GtkEntry *entry, GstEditorElement *element) {
  gst_editor_element_set_name(element, gtk_entry_get_text(GTK_ENTRY(entry)));
}

static gchar *make_readable_name(gchar *name) {
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

void gst_editor_property_show(GstEditorProperty *property, GstEditorElement *element) {
  GtkType type;
  GtkWidget *table, *vbox;
  GtkWidget *label, *entry, *panel = NULL;

  type = GTK_OBJECT_TYPE(element->element);
  if (type != GTK_TYPE_INVALID) {
    panel = (GtkWidget *)g_hash_table_lookup(property->panels, GINT_TO_POINTER(type));
    vbox = glade_xml_get_widget(property->xml, "property_vbox");

    if (panel && property->current == (gpointer) panel) return;

    if (property->current)
      gtk_container_remove(GTK_CONTAINER(vbox), GTK_WIDGET(property->current));

    if (panel) {
      gtk_box_pack_start(GTK_BOX(vbox), panel, FALSE, TRUE, 0);
      property->current = (gpointer) panel;
    }
    else {
      GtkArg *args;
      guint32 *flags;
      guint num_args, i, count;
    
      table = gtk_table_new(1, 2, FALSE);
      gtk_table_set_row_spacings(GTK_TABLE(table), 2);
      gtk_widget_show(table);

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
      gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, TRUE, 2);
      g_hash_table_insert(property->panels, GINT_TO_POINTER(type), table);
      gtk_object_ref(GTK_OBJECT(table));
      property->current = (gpointer) table;
    }
  }
}

static void widget_show_toggled(GtkToggleButton *button, GtkArg *arg) {
  GtkWidget *window;

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(GTK_VALUE_OBJECT(*arg)));

  gtk_widget_show(window);
}

typedef struct {
  GtkArg *arg;
  GstElement *element;
} file_select;
  
static void on_file_selected(GtkEditable *entry, file_select *fs)
{
  gtk_object_set(GTK_OBJECT(fs->element), fs->arg->name, 
		  gtk_entry_get_text(GTK_ENTRY(entry)), NULL);
}

static GtkWidget *create_property_entry(GtkArg *arg, GstElement *element) {
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
      gboolean toggle;
      toggle = GTK_VALUE_BOOL(*arg);
      entry = gtk_toggle_button_new_with_label((toggle? _("Yes"):_("No")));
      gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(entry), toggle);
      break;
    }
    case GTK_TYPE_ULONG:
    case GTK_TYPE_LONG:
    case GTK_TYPE_UINT:
    case GTK_TYPE_INT:
    {
      gint value;
      GtkAdjustment *spinner_adj;

      value = GTK_VALUE_INT(*arg);
      spinner_adj = (GtkAdjustment *) gtk_adjustment_new(50.0, 0.0, 10000000.0, 1.0, 5.0, 5.0);
      entry = gtk_spin_button_new(spinner_adj, 1.0, 0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry), (gfloat) value);
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
      gint i=0;
      GtkWidget *menu;

      entry = gtk_option_menu_new();
      menu = gtk_menu_new();

      values = gtk_type_enum_get_values(arg->type);
      while (values[i].value_name) {
	GtkWidget *menuitem = gtk_menu_item_new_with_label(values[i].value_nick);

	gtk_menu_append(GTK_MENU(menu), menuitem);
	gtk_widget_show(menuitem);
	i++;
      }
      gtk_option_menu_set_menu(GTK_OPTION_MENU(entry), menu);
    }
    else if (arg->type == GST_TYPE_FILENAME) {
      file_select *fs = g_new0(file_select, 1);

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
















