#include <config.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>
#include <gtk/gtk.h>
#ifdef USE_GLIB2
#define GTK_ENABLE_BROKEN
#include <gtk/gtktree.h>
#include <gtk/gtktreeitem.h>
#undef GTK_ENABLE_BROKEN
#endif
#include <gst/gst.h>

GtkWidget *start_but, *pause_but, *parse_but, *status;
GtkWidget *window;
GtkWidget *prop_box, *dparam_box;
GstElement *pipeline;
typedef void (*found_handler) (GstElement *element, gint xid, void *priv);

static gint quit_live(GtkWidget *window, GdkEventAny *e, gpointer data) {
	gtk_main_quit();
	return FALSE;
}

gboolean
idle_func (gpointer data)
{
	return gst_bin_iterate (GST_BIN (pipeline));
}

void
load_history(GtkWidget *pipe_combo)
{

	int history_fd;
	struct stat statbuf;
	gchar *map, *current, *next, *entry, *last_entry = "";
	gchar *history_filename;
	gint num_entries = 0, entries_limit = 50;
	GList *history = NULL;
	FILE *history_file;

	history_filename = g_strdup_printf("%s/.gstreamer-guilaunch.history", g_get_home_dir());

	
	if ((history_fd = open(history_filename, O_RDONLY)) > -1){
		fstat(history_fd, &statbuf);
		map = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, history_fd, 0);

		// scan it once to find out how many entries there are
		current = map;
		while (current){
			if ((next = strstr(current, "\n"))){
				num_entries++;
				current = next + 1;
			}
			else {
				current = NULL;
			}
		}
		
		current = map;
		while (current){
			if ((next = strstr(current, "\n"))){
				
				entry = g_strndup(current, next - current);
				if ((strlen(entry) == strlen(last_entry)) && strstr(entry, last_entry)){
					g_free(entry);
				}
				else{
					// show entries_limit of the newest entries
					if (num_entries-- < entries_limit){
						entry = g_strndup(current, next - current);
						last_entry = entry;
						history = g_list_prepend(history, entry);
					}
				}
				current = next + 1;
			}
			else {
				current = NULL;
			}
		}
		close(history_fd);
		munmap(map, statbuf.st_size);
	}
	gtk_combo_set_popdown_strings(GTK_COMBO(pipe_combo), history);

	history_file = fopen(history_filename, "a");
	if (history_file == NULL){
		perror("couldn't open history file");
	}
	gtk_object_set_data(GTK_OBJECT(pipe_combo), "history", history);
	gtk_object_set_data(GTK_OBJECT(pipe_combo), "history_file", history_file);
}

void
debug_toggle_callback(GtkWidget *widget, gpointer mode_ptr){
	guint32 debug_mode = GPOINTER_TO_INT(mode_ptr);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))){
		gst_info_enable_category(debug_mode);
		gst_debug_enable_category(debug_mode);
	}
	else {
		gst_info_disable_category(debug_mode);
		gst_debug_disable_category(debug_mode);
	}
}

void
debug_select_callback(GtkWidget *widget, GSList *debug_list)
{
	gchar *name = gtk_object_get_data(GTK_OBJECT(widget), "name");
	gboolean select = (strstr(name, "deselect all") == NULL);
	while (debug_list){
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(debug_list->data), select);
		debug_list = g_slist_next(debug_list);
	}
}

void
build_debug_page(GtkWidget *notebook)
{
	guint32 debug_mode;
	GtkWidget *debug_box, *debug_buts, *debug_but, *scrolled;
	GSList *debug_list = NULL;
	
	scrolled = gtk_scrolled_window_new(NULL,NULL);
	
	debug_box = gtk_vbox_new(TRUE, 0);
	debug_buts = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(debug_box),debug_buts,FALSE,FALSE,0);
	
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), debug_box);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("Debug"));
	
	for (debug_mode = 0 ; (debug_mode < GST_CAT_MAX_CATEGORY) && 
	                      (gst_get_category_name(debug_mode) != NULL); debug_mode++){
		GtkWidget *debug_enable = gtk_check_button_new_with_label (gst_get_category_name(debug_mode));
		
		debug_list = g_slist_append(debug_list, debug_enable);

		gtk_box_pack_start(GTK_BOX(debug_box),debug_enable,TRUE,TRUE,0);
		gtk_signal_connect (GTK_OBJECT (debug_enable), "toggled",
		                    GTK_SIGNAL_FUNC (debug_toggle_callback), GINT_TO_POINTER(debug_mode));
	}

	debug_but = gtk_button_new_with_label("select all");
	gtk_box_pack_start(GTK_BOX(debug_buts),debug_but,TRUE,TRUE,0);
	gtk_object_set_data(GTK_OBJECT(debug_but), "name", "select all");
	gtk_signal_connect (GTK_OBJECT (debug_but), "clicked",
	                    GTK_SIGNAL_FUNC (debug_select_callback), debug_list);

	debug_but = gtk_button_new_with_label("deselect all");
	gtk_box_pack_start(GTK_BOX(debug_buts),debug_but,TRUE,TRUE,0);
	gtk_object_set_data(GTK_OBJECT(debug_but), "name", "deselect all");
	gtk_signal_connect (GTK_OBJECT (debug_but), "clicked",
	                    GTK_SIGNAL_FUNC (debug_select_callback), debug_list);


}

void
arg_search (GstBin *bin, gchar *argname, found_handler handler, void *priv)
{
	GList *children;
	GValue *value;
	GParamSpec **property_specs;
	
	value = g_new0(GValue,1);
	g_value_init (value, G_TYPE_INT);
	children = gst_bin_get_list(bin);

	while (children) {
		GstElement *child;
     
		child = GST_ELEMENT (children->data);
		children = g_list_next (children);

		if (GST_IS_BIN (child)) arg_search (GST_BIN (child), argname, handler, priv);
		else {
			gint num_properties,i;
			
			property_specs = g_object_class_list_properties(G_OBJECT_GET_CLASS (child), &num_properties);
			
			for (i=0;i<num_properties;i++) {
				if (strstr(property_specs[i]->name,argname)) {
					g_object_get_property(G_OBJECT(child),argname,value);

					(handler)(child, g_value_get_int(value) ,priv);
				}
			}
		}
	}
	g_free(value);
}

void 
handle_have_size (GstElement *element,int width,int height) 
{
	g_print("setting window size\n");
	gtk_widget_set_usize(GTK_WIDGET(gtk_object_get_data(GTK_OBJECT(element), "gtk_socket")),width,height);
	gtk_widget_show_all(GTK_WIDGET(gtk_object_get_data(GTK_OBJECT(element), "vid_window")));
}

void 
xid_handler (GstElement *element, gint xid, void *priv) 
{
	GtkWidget *gtk_socket, *vid_window;
	
	g_print("handling xid %d\n", xid);
	vid_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_socket = gtk_socket_new ();
	gtk_widget_show(gtk_socket);

	gtk_container_add(GTK_CONTAINER(vid_window),gtk_socket);

	gtk_widget_realize(gtk_socket);
	gtk_socket_steal (GTK_SOCKET (gtk_socket), xid);

	gtk_object_set(GTK_OBJECT(vid_window),"allow_grow",TRUE,NULL);
	gtk_object_set(GTK_OBJECT(vid_window),"allow_shrink",TRUE,NULL);

	gtk_signal_connect (GTK_OBJECT (element), "have_size",
	                    GTK_SIGNAL_FUNC (handle_have_size), element);
	gtk_object_set_data(GTK_OBJECT(element), "vid_window", vid_window);
	gtk_object_set_data(GTK_OBJECT(element), "gtk_socket", gtk_socket);
}

void 
prop_change_callback(GtkWidget *widget, GstElement *element)
{
	gchar *prop_name = gtk_object_get_data(GTK_OBJECT(widget), "prop_name");
	GType prop_type = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(widget), "prop_type"));	
	GValue *value = g_new0(GValue,1);
	g_value_init (value, prop_type);

	g_print("prop %s changed in %s\n", prop_name, gst_element_get_name(element));


	switch (prop_type) {
		case G_TYPE_STRING:
			g_print("setting string\n");
			g_object_set(G_OBJECT(element),prop_name,gtk_entry_get_text(GTK_ENTRY(widget)), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_entry_set_text(GTK_ENTRY(widget), g_value_get_string(value));
			break;
		case G_TYPE_BOOLEAN:
			g_print("setting bool\n");
			g_object_set(G_OBJECT(element),prop_name,gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widget)), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(widget), g_value_get_boolean(value));
			break;
		case G_TYPE_ULONG:
			g_print("setting ulong\n");
			g_object_set(G_OBJECT(element),prop_name,(gulong)(GTK_ADJUSTMENT(widget)->value), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(widget), (gfloat)g_value_get_ulong(value));
			break;
		case G_TYPE_LONG:
			g_print("setting long\n");
			g_object_set(G_OBJECT(element),prop_name,(glong)(GTK_ADJUSTMENT(widget)->value), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(widget), (gfloat)g_value_get_long(value));
			break;
		case G_TYPE_UINT:
			g_print("setting uint\n");
			g_object_set(G_OBJECT(element),prop_name,(guint)(GTK_ADJUSTMENT(widget)->value), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(widget), (gfloat)g_value_get_uint(value));
			break;
		case G_TYPE_INT:
			g_print("setting int\n");
			g_object_set(G_OBJECT(element),prop_name,(gint)(GTK_ADJUSTMENT(widget)->value), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(widget), (gfloat)g_value_get_int(value));
			break;
		case G_TYPE_FLOAT:
			g_print("setting float\n");
			g_object_set(G_OBJECT(element),prop_name,(GTK_ADJUSTMENT(widget)->value), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(widget), g_value_get_float(value));
			break;
		case G_TYPE_DOUBLE:
			g_print("setting double\n");
			g_object_set(G_OBJECT(element),prop_name,(gdouble)(GTK_ADJUSTMENT(widget)->value), NULL);
			g_object_get_property(G_OBJECT(element),prop_name,value);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(widget), (gfloat)g_value_get_double(value));
			break;
		case G_TYPE_ENUM:{
			GtkWidget *menu_item = gtk_menu_get_active(GTK_MENU(widget));
			g_print("setting enum\n");
			g_object_set(G_OBJECT(element),prop_name,
			             GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menu_item), "enum_val")),
			             NULL);
			}
			break;
		default:
			break;
	}
	g_free(value);
}

void
build_props_box(GstElement *element)
{
	GParamSpec **property_specs;
	GValue *value;
	gint num_properties,i;
	GtkWidget *prop_table;
	GtkWidget *prop_label, *prop_align;
	GtkWidget *prop_attach_widget;
	GtkObject *prop_object;
	
	const gchar *element_name, *prop_name, *short_name;
	
	element_name = gst_element_get_name(element);
	
	property_specs = g_object_class_list_properties(G_OBJECT_GET_CLASS (element), &num_properties);
	
	prop_table = gtk_table_new(num_properties, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(prop_box),prop_table,FALSE,FALSE,0);
	
	for (i=0;i<num_properties;i++) {
		gchar *signal_name = "";
		value = g_new0(GValue,1);
		g_value_init (value, property_specs[i]->value_type);
		prop_name = property_specs[i]->name;
		if (strstr(prop_name, "::")){
			short_name = strstr(prop_name, "::") + 2;
		}
		else {
			short_name = prop_name;
		}
		prop_label = gtk_label_new(short_name);

		
		g_object_get_property(G_OBJECT(element),prop_name,value);
		prop_align = gtk_alignment_new(1, 0, 0, 0);
		gtk_container_add(GTK_CONTAINER(prop_align), prop_label);
		gtk_table_attach(GTK_TABLE(prop_table), prop_align, 0, 1, i, i+1, GTK_FILL, GTK_SHRINK, 5, 3);
		prop_object = NULL;
		prop_attach_widget = NULL;
		
		switch (G_VALUE_TYPE (value)) {
			case G_TYPE_STRING:
				g_print("got string %s for %s\n", prop_name, element_name);
				prop_object = GTK_OBJECT(gtk_entry_new());
				if (g_value_get_string(value)){
					gtk_entry_set_text(GTK_ENTRY(prop_object), g_value_get_string(value));
				}
				signal_name = "activate";
				prop_attach_widget = GTK_WIDGET(prop_object);
				break;
			case G_TYPE_BOOLEAN:
				g_print("got bool %s for %s\n", prop_name, element_name);
				prop_object = GTK_OBJECT(gtk_check_button_new());
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(prop_object), g_value_get_boolean(value));
				signal_name = "toggled";
				prop_attach_widget = GTK_WIDGET(prop_object);				
				break;
			case G_TYPE_ULONG:
				g_print("got ulong %s for %s\n", prop_name, element_name);
				prop_object = gtk_adjustment_new(g_value_get_ulong(value), 0, (gfloat)G_MAXLONG * 2.0, 1, 10, 10);
				signal_name = "value-changed";
				prop_attach_widget = gtk_spin_button_new(GTK_ADJUSTMENT(prop_object), 1, 0);
				break;
			case G_TYPE_LONG:
				g_print("got long %s for %s\n", prop_name, element_name);
				prop_object = gtk_adjustment_new(g_value_get_long(value), G_MINLONG, G_MAXLONG, 1, 10, 10);
				signal_name = "value-changed";
				prop_attach_widget = gtk_spin_button_new(GTK_ADJUSTMENT(prop_object), 1, 0);
				break;
			case G_TYPE_UINT:
				g_print("got uint %s for %s\n", prop_name, element_name);
				prop_object = gtk_adjustment_new(g_value_get_uint(value), 0, (gfloat)G_MAXINT * 2.0, 1, 10, 10);
				prop_attach_widget = gtk_spin_button_new(GTK_ADJUSTMENT(prop_object), 1, 0);
				signal_name = "value-changed";
				break;
			case G_TYPE_INT:
				g_print("got int %s for %s\n", prop_name, element_name);
				prop_object = gtk_adjustment_new(g_value_get_int(value), G_MININT, G_MAXINT, 1, 10, 10);
				prop_attach_widget = gtk_spin_button_new(GTK_ADJUSTMENT(prop_object), 1, 0);
				signal_name = "value-changed";
				break;
			case G_TYPE_FLOAT:
				g_print("got float %s for %s\n", prop_name, element_name);
				prop_object = gtk_adjustment_new(g_value_get_float(value), G_MINFLOAT, G_MAXFLOAT, 1, 10, 10);
				prop_attach_widget = gtk_spin_button_new(GTK_ADJUSTMENT(prop_object), 1, 3);
				signal_name = "value-changed";
				break;
			case G_TYPE_DOUBLE:
				g_print("got double %s for %s\n", prop_name, element_name);
				prop_object = gtk_adjustment_new(g_value_get_double(value), G_MINDOUBLE, G_MAXDOUBLE, 1, 10, 10);
				prop_attach_widget = gtk_spin_button_new(GTK_ADJUSTMENT(prop_object), 1, 3);
				signal_name = "value-changed";
				break;
			default:
				if (G_IS_PARAM_SPEC_ENUM (property_specs[i])) {
					GEnumValue *values;
					guint j = 0;
					g_print("got enum %s for %s\n", prop_name, element_name);
					g_value_init (value, G_TYPE_ENUM);
					prop_attach_widget = gtk_option_menu_new();
					prop_object = GTK_OBJECT(gtk_menu_new());
					gtk_option_menu_set_menu(GTK_OPTION_MENU(prop_attach_widget), GTK_WIDGET(prop_object));
					signal_name = "selection-done";
					//FIXME when the shim is dead
					values = gtk_type_enum_get_values (property_specs[i]->value_type);

					while (values[j].value_name) {
						GtkWidget *menu_item;
						menu_item = gtk_menu_item_new_with_label(values[j].value_nick);						
						gtk_menu_append(GTK_MENU(prop_object), menu_item);
						gtk_object_set_data(GTK_OBJECT(menu_item), "enum_val", GINT_TO_POINTER(values[j].value));
						if (values[j].value == g_value_get_enum(value)){
							gtk_menu_shell_select_item(GTK_MENU_SHELL(prop_object), menu_item);
							gtk_menu_shell_activate_item(GTK_MENU_SHELL(prop_object), menu_item, FALSE);
						}
						j++; 
					}
	  			}
				break;
		}
		if (prop_attach_widget){
			gtk_table_attach_defaults(GTK_TABLE(prop_table), prop_attach_widget, 1, 2, i, i+1);
			gtk_widget_show(prop_attach_widget);
		}
		if (property_specs[i]->flags & G_PARAM_WRITABLE){
			if (prop_object){
				gtk_signal_connect (prop_object, signal_name,
					                GTK_SIGNAL_FUNC (prop_change_callback), element);
				gtk_object_set_data(prop_object, "prop_name", g_strdup(short_name));
				gtk_object_set_data(prop_object, "prop_type", GINT_TO_POINTER(G_VALUE_TYPE (value)));
			}
		}
		else {
			gtk_widget_set_sensitive(GTK_WIDGET(prop_attach_widget), FALSE);
		}
		g_free(value);
		gtk_widget_show_all(prop_table);
	}
}
void
select_child_callback(GtkWidget *tree_item, GstElement *element)
{
	build_props_box(element);
}

void
clear_edit_panes(GtkWidget *widget, gpointer *data)
{
	GList *children;
	
	children = gtk_container_children(GTK_CONTAINER(prop_box));
	while (children){
		gtk_container_remove(GTK_CONTAINER(prop_box), GTK_WIDGET(children->data));
		children = g_list_next (children);
	}
/*
	children = gtk_container_children(GTK_CONTAINER(dparam_box));
	while (children){
		gtk_container_remove(GTK_CONTAINER(prop_box), GTK_WIDGET(children->data));
		children = g_list_next (children);
	}
*/
}

void
build_tree(GtkWidget *tree_item, GstBin *bin){
	GList *children;
	GtkWidget *tree, *item;

	if (GTK_TREE_ITEM_SUBTREE(tree_item) != NULL){
		gtk_tree_item_remove_subtree(GTK_TREE_ITEM(tree_item));
	}
	
	tree = gtk_tree_new();
	gtk_tree_item_set_subtree(GTK_TREE_ITEM(tree_item), tree);
	gtk_widget_show(tree);

	children = gst_bin_get_list(bin);

	while (children) {
		GstElement *child;
     
		child = GST_ELEMENT (children->data);
		children = g_list_next (children);
		
		item = gtk_tree_item_new_with_label(gst_element_get_name(child));
		gtk_object_set_data(GTK_OBJECT(item), "tree", tree);
		gtk_signal_connect (GTK_OBJECT (item), "select",
	                        GTK_SIGNAL_FUNC (select_child_callback), child);
		gtk_signal_connect (GTK_OBJECT (item), "deselect",
	                        GTK_SIGNAL_FUNC (clear_edit_panes), child);
	

		gtk_tree_append(GTK_TREE(tree), item);
		gtk_widget_show(item);

		if (GST_IS_BIN (child)){
			build_tree(item, GST_BIN(child));
			gtk_tree_item_expand(GTK_TREE_ITEM(item));
		}
	}
}

void
parse_callback( GtkWidget *widget,
                GtkWidget *pipe_combo)
{
	GList *history = (GList*)gtk_object_get_data(GTK_OBJECT(pipe_combo), "history");
	FILE *history_file = (FILE*)gtk_object_get_data(GTK_OBJECT(pipe_combo), "history_file");
	GtkWidget *tree_item = (GtkWidget*)gtk_object_get_data(GTK_OBJECT(widget), "tree_item");
	gchar *last_pipe = (gchar*)gtk_object_get_data(GTK_OBJECT(widget), "last_pipe");
	gchar *try_pipe = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(pipe_combo)->entry));
	gint parse_result;
		
	if (pipeline){
		g_print("unreffing\n");
		gst_object_unref (GST_OBJECT (pipeline));
	}
	g_print ("trying pipeline: %s\n", try_pipe);
	
	pipeline = gst_pipeline_new ("launch");
	parse_result = gst_parse_launch (try_pipe, GST_BIN (pipeline));
	if (parse_result < 0){
		switch(parse_result){
			case GST_PARSE_ERROR_SYNTAX:
				gtk_label_set_text(GTK_LABEL(status), "error parsing syntax of pipeline");
				break;
			case GST_PARSE_ERROR_CREATING_ELEMENT:
				gtk_label_set_text(GTK_LABEL(status), "error creating a core element");
				break;
			case GST_PARSE_ERROR_NOSUCH_ELEMENT:
				gtk_label_set_text(GTK_LABEL(status), "error finding an element which was requested");
				break;
			default:
				gtk_label_set_text(GTK_LABEL(status), "unknown error parsing pipeline");
				break;
		}
		gst_object_unref (GST_OBJECT (pipeline));
		return;
	}			
	gtk_widget_set_sensitive(GTK_WIDGET(start_but), TRUE);

	build_tree(tree_item, GST_BIN(pipeline));

	if (last_pipe==NULL || !((strlen(try_pipe) == strlen(last_pipe)) && strstr(try_pipe, last_pipe))){
		gchar *write_pipe = g_strdup_printf("%s\n", try_pipe);
		gtk_object_set_data(GTK_OBJECT(widget), "last_pipe", try_pipe);
		fwrite(write_pipe, sizeof(gchar), strlen(write_pipe), history_file);
		fflush(history_file);
		history = g_list_prepend(history, try_pipe);
		gtk_combo_set_popdown_strings(GTK_COMBO(pipe_combo), history);	
		g_free(write_pipe);
		g_free(last_pipe);
	}

}

void
start_callback( GtkWidget *widget,
                gpointer   data )
{
	GtkWidget *pipe_combo = gtk_object_get_data(GTK_OBJECT(widget), "pipe_combo");
	
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))){
		gtk_widget_set_sensitive(GTK_WIDGET(pause_but), TRUE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_but), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(parse_but), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(pipe_combo), FALSE);
		gtk_label_set_text(GTK_LABEL(status), "playing");
		
		arg_search(GST_BIN(pipeline),"xid",xid_handler,NULL);
		gst_element_set_state (pipeline, GST_STATE_PLAYING);
		g_idle_add(idle_func,pipeline);
	}
	else {
		gtk_widget_set_sensitive(GTK_WIDGET(pause_but), FALSE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pause_but), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(parse_but), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(pipe_combo), TRUE);
		gst_element_set_state (pipeline, GST_STATE_NULL);
		gtk_label_set_text(GTK_LABEL(status), "stopped");
		
		g_idle_remove_by_data(pipeline);
	}
}

void
pause_callback( GtkWidget *widget,
                gpointer   data )
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))){
		gst_element_set_state (pipeline, GST_STATE_PAUSED);
		gtk_label_set_text(GTK_LABEL(status), "paused");
	}
	else {
		gst_element_set_state (pipeline, GST_STATE_PLAYING);
		gtk_label_set_text(GTK_LABEL(status), "playing");
	}
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *parse_line, *pipe_combo, *notebook, *pane;
	GtkWidget *tree_root, *tree_root_item, *page_scroll;

#ifdef USE_GLIB2
	gtk_init (&argc, &argv);
#endif
	gst_init (&argc, &argv);
	
	/***** set up the GUI *****/
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window,"delete_event",GTK_SIGNAL_FUNC(quit_live),NULL);
	vbox = gtk_vbox_new(FALSE,0);
	gtk_container_add(GTK_CONTAINER(window),vbox);

	parse_line = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(vbox),parse_line,FALSE,FALSE,0);

	pipe_combo = gtk_combo_new();
	gtk_combo_set_value_in_list(GTK_COMBO(pipe_combo), FALSE, FALSE);
	load_history(pipe_combo);
	
	parse_but = gtk_button_new_with_label("Parse");
	gtk_box_pack_start(GTK_BOX(parse_line),pipe_combo,TRUE,TRUE,0);
	gtk_box_pack_start(GTK_BOX(parse_line),parse_but,FALSE,FALSE,0);

	start_but = gtk_toggle_button_new_with_label("Play");
	pause_but = gtk_toggle_button_new_with_label("Pause");

	gtk_box_pack_start(GTK_BOX(parse_line),start_but,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(parse_line),pause_but,FALSE,FALSE,0);
	
	gtk_widget_set_sensitive(GTK_WIDGET(start_but), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(pause_but), FALSE);

	gtk_signal_connect (GTK_OBJECT (start_but), "clicked",
	                    GTK_SIGNAL_FUNC (start_callback), NULL);
	gtk_signal_connect (GTK_OBJECT (pause_but), "clicked",
	                    GTK_SIGNAL_FUNC (pause_callback), NULL);
	gtk_signal_connect (GTK_OBJECT (parse_but), "clicked",
	                    GTK_SIGNAL_FUNC (parse_callback), pipe_combo);
	gtk_signal_connect (GTK_OBJECT (parse_but), "clicked",
	                    GTK_SIGNAL_FUNC (clear_edit_panes), NULL);


	gtk_object_set_data(GTK_OBJECT(start_but), "pipe_combo", pipe_combo);
	
	tree_root = gtk_tree_new();

	tree_root_item = gtk_tree_item_new_with_label("pipe");
	gtk_tree_append(GTK_TREE(tree_root), tree_root_item);
	gtk_object_set_data(GTK_OBJECT(parse_but), "tree_item", tree_root_item);
	gtk_tree_item_expand(GTK_TREE_ITEM(tree_root_item));

	prop_box = gtk_vbox_new(FALSE, 0);
	//dparam_box = gtk_vbox_new(FALSE, 0);
	
	notebook = gtk_notebook_new();

	page_scroll = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(page_scroll), prop_box);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_scroll, gtk_label_new("Properties"));
/*
	page_scroll = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(page_scroll), dparam_box);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page_scroll, gtk_label_new("Dynamic Params"));
*/
	build_debug_page(notebook);

	page_scroll = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(page_scroll), tree_root);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	pane = gtk_hpaned_new();
	gtk_paned_pack1 (GTK_PANED (pane), page_scroll, TRUE, TRUE);
	gtk_paned_pack2 (GTK_PANED (pane), notebook, TRUE, TRUE);

	gtk_box_pack_start(GTK_BOX(vbox),pane,TRUE,TRUE,0);
	
	status = gtk_label_new("stopped");
	gtk_box_pack_start(GTK_BOX(vbox),status,FALSE,FALSE,0);
	
	gtk_widget_show_all(window);
	gtk_main();

	return 0;
}
