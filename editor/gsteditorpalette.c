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


#include <gnome.h>
#include <gst/gst.h>

#include <sys/stat.h>
#include <unistd.h>
  
#include "gsteditorpalette.h"
#include "gsteditorimage.h"

/* class functions */
static void gst_editor_palette_class_init(GstEditorPaletteClass *klass);
static void gst_editor_palette_init(GstEditorPalette *palette);
static void gst_editor_palette_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_palette_get_arg(GtkObject *object,GtkArg *arg,guint id);

static void gst_editor_palette_make(GstEditorPalette *palette);

struct _palette_entry {
  gchar *tooltip;
  GtkType (*type) (void);
  gchar *factoryname;
};

#define CORE_ELEMENT_SIZE 4
struct _palette_entry _palette_contents_core[CORE_ELEMENT_SIZE] = {
  {"Bin", gst_bin_get_type, "bin" },
  {"Thread", gst_thread_get_type, "thread" },
  {"Pipeline", gst_pipeline_get_type, "pipeline" },
  {"Tee", gst_tee_get_type, "tee" },
};

enum {
  ARG_0,
};

enum {
  SIGNAL_ELEMENT_SELECTED,
  SIGNAL_IN_SELECTION_MODE,
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
static guint gst_editor_palette_signals[LAST_SIGNAL] = { 0 };

GtkType 
gst_editor_palette_get_type (void) 
{
  static GtkType palette_type = 0;

  if (!palette_type) {
    static const GtkTypeInfo palette_info = {
      "GstEditorPalette",
      sizeof(GstEditorPalette),
      sizeof(GstEditorPaletteClass),
      (GtkClassInitFunc)gst_editor_palette_class_init,
      (GtkObjectInitFunc)gst_editor_palette_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    palette_type = gtk_type_unique(gtk_object_get_type(),&palette_info);
  }
  return palette_type;
}

static void 
gst_editor_palette_class_init (GstEditorPaletteClass *klass) 
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gst_editor_palette_signals[SIGNAL_ELEMENT_SELECTED] =
    gtk_signal_new("element_selected",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorPaletteClass,element_selected),
                   gtk_marshal_NONE__INT,GTK_TYPE_NONE,1,
                   GTK_TYPE_INT);

  gst_editor_palette_signals[SIGNAL_IN_SELECTION_MODE] =
    gtk_signal_new("in_selection_mode",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorPaletteClass,in_selection_mode),
                   gtk_marshal_NONE__INT,GTK_TYPE_NONE,1,
                   GTK_TYPE_INT);

  gtk_object_class_add_signals(object_class,gst_editor_palette_signals,LAST_SIGNAL);

  object_class->set_arg = gst_editor_palette_set_arg;
  object_class->get_arg = gst_editor_palette_get_arg;
}

static void 
gst_editor_palette_init (GstEditorPalette *palette) 
{
  palette->tooltips = gtk_tooltips_new();
}

typedef struct {
  GstEditorPalette *palette;
  GModule *symbols;
} connect_struct;
  
/* we need more control here so... */
static void 
gst_editor_palette_connect_func (const gchar *handler_name,
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
    g_warning("gsteditorpalette: could not find signal handler '%s'.", handler_name);
  else {
    if (after)
      gtk_signal_connect_after(object, signal_name, func, (gpointer) data->palette);
    else
      gtk_signal_connect(object, signal_name, func, (gpointer) data->palette);
  }
}

GstEditorPalette*
gst_editor_palette_new() 
{
  GstEditorPalette *palette;
  GtkWidget *palette_window;
  connect_struct data;
  GModule *symbols;
  struct stat statbuf;

  palette = GST_EDITOR_PALETTE(gtk_type_new(GST_TYPE_EDITOR_PALETTE));

  symbols = g_module_open(NULL, 0);

  data.palette = palette;
  data.symbols = symbols;

  if (stat(DATADIR"editor.glade", &statbuf) == 0) {
    palette->xml = glade_xml_new(DATADIR"editor.glade", "palette_window");
  }
  else {
    palette->xml = glade_xml_new ("editor.glade", "palette_window");
  }
  g_assert (palette->xml != NULL);
      
  glade_xml_signal_autoconnect_full (palette->xml, gst_editor_palette_connect_func, &data);

  palette_window = glade_xml_get_widget(palette->xml, "palette_window");
  gtk_widget_show(palette_window);

  palette->table = gtk_table_new(1, 4, TRUE);

  gst_editor_palette_make(palette);

  return palette;
}

typedef struct {
  GstEditorPalette *palette;
  struct _palette_entry *entry;
} _signal_data;
  
static void 
gst_editor_palette_element_clicked(GtkButton *button, _signal_data *data) 
{
  GstElementFactory *factory;

  factory = gst_elementfactory_find (data->entry->factoryname);

  gtk_signal_emit(GTK_OBJECT(data->palette),gst_editor_palette_signals[SIGNAL_ELEMENT_SELECTED], factory);
}

static void 
gst_editor_palette_make (GstEditorPalette *palette) 
{
  GtkWidget *button;
  GstEditorImage *editimage;
  GtkWidget *image;
  GtkWidget *vbox;
  gint i, x, y;
  _signal_data *data;

  x=0;
  y=0;

  vbox = glade_xml_get_widget(palette->xml, "palette_vbox");
  gtk_box_pack_start(GTK_BOX(vbox), palette->table, FALSE, TRUE, 0);

  for (i=0; i<CORE_ELEMENT_SIZE; i++) {
    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    editimage = gst_editor_image_get_for_type(_palette_contents_core[i].type());
    image = gtk_pixmap_new(editimage->pixmap, editimage->bitmap);
    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_widget_show(image);

    gtk_table_attach(GTK_TABLE(palette->table), button, x, x+1, y, y+1, 0, 0, 0, 0);
    gtk_widget_show(palette->table);

    data = g_new0(_signal_data, 1);

    data->palette = palette;
    data->entry = &_palette_contents_core[i];

    gtk_signal_connect(GTK_OBJECT(button), "clicked", gst_editor_palette_element_clicked, data);

    gtk_tooltips_set_tip(palette->tooltips, button, _palette_contents_core[i].tooltip, NULL);

    gtk_widget_show(button);

    x++;
    if (x==4) {
      x=0;
      y++;
    }
  }
}

static void 
gst_editor_palette_set_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorPalette *palette;

  /* get the major types of this object */
  palette = GST_EDITOR_PALETTE(object);

  switch (id) {
    default:
      g_warning("gsteditorpalette: unknown arg!");
      break;
  }
}

static void 
gst_editor_palette_get_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorPalette *palette;

  /* get the major types of this object */
  palette = GST_EDITOR_PALETTE(object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}



