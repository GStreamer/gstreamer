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


#include <gst/gst.h>
#include <gnome.h>

struct element_select_classlist {
  gchar *name;
  GSList *subclasses;
  GSList *factories;
};

struct element_select_details {
  GstElementFactory *factory;
  GtkWidget *longname, *description, *version, *author, *copyright;
};

static gint compare_name(gconstpointer a,gconstpointer b) {
  return (strcmp(((GstElementFactory *)a)->name,
                 ((GstElementFactory *)b)->name));
}

gint str_compare(gconstpointer a,gconstpointer b) {
  return (strcmp((gchar *)a,(gchar *)b));
}

/* this function creates a GtkCTreeNode with the contents of the classtree */
static void make_ctree(GtkCTree *tree,GtkCTreeNode *parent,
                       struct element_select_classlist *class) {
  GSList *traverse;
  GtkCTreeNode *classnode, *node = NULL;
  gchar *data[2];

  data[0] = g_strdup(class->name);
  data[1] = NULL;
  classnode = gtk_ctree_insert_node(tree,parent,NULL,data,0,
                                    NULL,NULL,NULL,NULL,FALSE,TRUE);
  gtk_ctree_node_set_selectable(tree,classnode,FALSE);

  traverse = class->subclasses;
  while (traverse) {
    make_ctree(tree,classnode,
               (struct element_select_classlist *)(traverse->data));
    traverse = g_slist_next(traverse);
  }

  traverse = class->factories;
  while (traverse) {
    GstElementFactory *factory = (GstElementFactory *)(traverse->data);
    data[0] = g_strdup(factory->name);
    data[1] = g_strdup(factory->details->description);
    node = gtk_ctree_insert_node(tree,classnode,NULL,data,0,
                                 NULL,NULL,NULL,NULL,TRUE,FALSE);
    gtk_ctree_node_set_row_data_full(tree,node,factory,NULL);
    traverse = g_slist_next(traverse);
  }
}

static void ctree_select(GtkWidget *widget,gint row,gint column,
                         GdkEventButton *bevent,gpointer data) {
  GtkCTree *tree = GTK_CTREE(widget);
  GtkCTreeNode *node;
  GstElementFactory *factory;
  struct element_select_details *details;
  node = gtk_ctree_node_nth(tree,row);
  factory = (GstElementFactory *)gtk_ctree_node_get_row_data(tree,node);
  if (!factory)
    return;
  details = (struct element_select_details *)data;
  details->factory = factory;

  gtk_entry_set_text(GTK_ENTRY(details->longname),
                     factory->details->longname);
  gtk_entry_set_text(GTK_ENTRY(details->description),
                     factory->details->description);
  gtk_entry_set_text(GTK_ENTRY(details->version),
                     factory->details->version);
  gtk_entry_set_text(GTK_ENTRY(details->author),
                     factory->details->author);
  gtk_entry_set_text(GTK_ENTRY(details->copyright),
                     factory->details->copyright);

  if (bevent && bevent->type == GDK_2BUTTON_PRESS)
    gtk_main_quit();
}


GstElementFactory *element_select_dialog() {
  GtkWidget *dialog;
  gchar *titles[2];
  GtkWidget *ctree;
  GtkWidget *scroller;
  GtkTable *table;
  GtkWidget *detailslabel;
  GtkWidget *detailshsep;
  GtkWidget *longname, *description, *version, *author, *copyright;

  GList *elements;
  GstElementFactory *element;
  gchar **classes, **class;
  GSList *classlist;
  GSList *classtree, *treewalk;
  GSList **curlist;
  struct element_select_classlist *branch;
  struct element_select_details details;

  /* first create the dialog and associated stuff */
  dialog = gnome_dialog_new("Select Element",
                            GNOME_STOCK_BUTTON_OK,
                            GNOME_STOCK_BUTTON_CANCEL,
                            NULL);
  gnome_dialog_set_close(GNOME_DIALOG(dialog),TRUE);
  gnome_dialog_close_hides(GNOME_DIALOG(dialog),TRUE);
  gnome_dialog_set_default(GNOME_DIALOG(dialog),GNOME_OK);

  titles[0] = "Element                               ";
  titles[1] = "Description";
  ctree = gtk_ctree_new_with_titles(2,0,titles);
  gtk_widget_set_usize(ctree,400,350);

  scroller = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(scroller),ctree);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox),scroller,
                             TRUE,TRUE,0);

  /* create the details table and put a title on it */
  table = GTK_TABLE(gtk_table_new(2,7,FALSE));
  detailslabel = gtk_label_new("Element Details:");
  gtk_misc_set_alignment(GTK_MISC(detailslabel),0.0,0.5);
  gtk_table_attach(table,detailslabel,0,2,0,1,GTK_FILL|GTK_EXPAND,0,0,0);

  /* then a separator to keep the title separate */
  detailshsep = gtk_hseparator_new();
  gtk_table_attach(table,detailshsep,0,2,1,2,GTK_FILL|GTK_EXPAND,0,0,0);

  /* the long name of the element */
  longname = gtk_label_new("Name:");
  gtk_misc_set_alignment(GTK_MISC(longname),1.0,0.5);
  gtk_table_attach(table,longname,0,1,2,3,GTK_FILL,0,5,0);
  details.longname = gtk_entry_new();
  gtk_entry_set_editable(GTK_ENTRY(details.longname),FALSE);
  gtk_table_attach(table,details.longname,1,2,2,3,GTK_FILL|GTK_EXPAND,0,0,0);

  /* the description */
  description = gtk_label_new("Description:");
  gtk_misc_set_alignment(GTK_MISC(description),1.0,0.5);
  gtk_table_attach(table,description,0,1,3,4,GTK_FILL,0,5,0);
  details.description = gtk_entry_new();
  gtk_entry_set_editable(GTK_ENTRY(details.description),FALSE);
  gtk_table_attach(table,details.description,1,2,3,4,GTK_FILL|GTK_EXPAND,0,0,0);

  /* the version */
  version = gtk_label_new("Version:");
  gtk_misc_set_alignment(GTK_MISC(version),1.0,0.5);
  gtk_table_attach(table,version,0,1,4,5,GTK_FILL,0,5,0);
  details.version = gtk_entry_new();
  gtk_entry_set_editable(GTK_ENTRY(details.version),FALSE);
  gtk_table_attach(table,details.version,1,2,4,5,GTK_FILL|GTK_EXPAND,0,0,0);

  /* the author */
  author = gtk_label_new("Author:");
  gtk_misc_set_alignment(GTK_MISC(author),1.0,0.5);
  gtk_table_attach(table,author,0,1,6,7,GTK_FILL,0,5,0);
  details.author = gtk_entry_new();
  gtk_entry_set_editable(GTK_ENTRY(details.author),FALSE);
  gtk_table_attach(table,details.author,1,2,6,7,GTK_FILL|GTK_EXPAND,0,0,0);

  /* the copyright */
  copyright = gtk_label_new("Copyright:");
  gtk_misc_set_alignment(GTK_MISC(copyright),1.0,0.5);
  gtk_table_attach(table,copyright,0,1,7,8,GTK_FILL,0,5,0);
  details.copyright = gtk_entry_new();
  gtk_entry_set_editable(GTK_ENTRY(details.copyright),FALSE);
  gtk_table_attach(table,details.copyright,1,2,7,8,GTK_FILL|GTK_EXPAND,0,0,0);

  gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox),GTK_WIDGET(table),
                             TRUE,TRUE,0);


  /* first create a sorted (by class) tree of all the factories */
  classtree = NULL;
  elements = gst_elementfactory_get_list();
  while (elements) {
    element = (GstElementFactory *)(elements->data);
    /* split up the factory's class */
    classes = g_strsplit(element->details->class,"/",0);
    class = classes;
    curlist = &classtree;
    /* walk down the class tree to find where to place this element */
    /* the goal is for treewalk to point to the right class branch */
    /* when we exit this thing, branch is pointing where we want */
    while (*class) {
      treewalk = *curlist;
      /* walk the current level of class to see if we have the class */
      while (treewalk) {
        branch = (struct element_select_classlist *)(treewalk->data);
        /* see if this class matches what we're looking for */
        if (!strcmp(branch->name,*class)) {
          /* if so, we progress down the list into this one's list */
          curlist = &branch->subclasses;
          break;
        }
        treewalk = g_slist_next(treewalk);
      }
      /* if treewalk == NULL, it wasn't in the list. add one */
      if (treewalk == NULL) {
        /* curlist is pointer to list */
        branch = g_new0(struct element_select_classlist,1);
        branch->name = g_strdup(*class);
        *curlist = g_slist_insert_sorted(*curlist,branch,str_compare);
        curlist = &branch->subclasses;
      }
      class++;
    }
    /* theoretically branch points where we want now */
    branch->factories = g_slist_insert_sorted(branch->factories,element,
                                              compare_name);
    elements = g_list_next(elements);
  }

  /* now fill in the ... */
  gtk_clist_freeze(GTK_CLIST(ctree));
  treewalk = classtree;
  while (treewalk) {
    make_ctree(GTK_CTREE(ctree),NULL,
               (struct element_select_classlist *)(treewalk->data));
    treewalk = g_slist_next(treewalk);
  }
  gtk_clist_thaw(GTK_CLIST(ctree));

  gtk_signal_connect(GTK_OBJECT(ctree),"select_row",
                     GTK_SIGNAL_FUNC(ctree_select),&details);

  gtk_widget_show_all(GTK_WIDGET(dialog));

  details.factory = NULL;
  if (gnome_dialog_run_and_close(GNOME_DIALOG(dialog)) == GNOME_CANCEL)
    return NULL;
  else
    return details.factory;
};


/* this is available so we can do a quick test of this thing */
#ifdef ELEMENTSELECT_MAIN
int main(int argc,char *argv[]) {
  GstElementFactory *chosen;

  gst_init(&argc,&argv);
  gst_plugin_load_all();
  gnome_init("elementselect_test","0.0.0",argc,argv);
  chosen = element_select_dialog();
  if (chosen)
    g_print("selected '%s'\n",chosen->name);
  else
    g_print("didn't choose any\n");
  exit(0);
}
#endif /* ELEMENTSELECT_MAIN */
