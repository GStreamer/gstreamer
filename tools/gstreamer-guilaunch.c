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
#include <gst/gst.h>

GtkWidget *start_but, *pause_but, *parse_but, *status;
GtkWidget *window;
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
	gchar *map, *current, *next, *entry;
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
				
				// show entries_limit of the newest entries
				if (num_entries-- < entries_limit){
					entry = g_strndup(current, next - current);
					history = g_list_prepend(history, entry);
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
	gtk_box_pack_start(GTK_BOX(debug_box),debug_buts,TRUE,TRUE,0);
	
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
parse_callback( GtkWidget *widget,
                GtkWidget *pipe_combo)
{
	GList *history = (GList*)gtk_object_get_data(GTK_OBJECT(pipe_combo), "history");
	FILE *history_file = (FILE*)gtk_object_get_data(GTK_OBJECT(pipe_combo), "history_file");
	
	gchar *try_pipe = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(pipe_combo)->entry));
	gchar *write_pipe = g_strdup_printf("%s\n", try_pipe);

	fwrite(write_pipe, sizeof(gchar), strlen(write_pipe), history_file);
	fflush(history_file);

	if (pipeline){
		g_print("unreffing\n");
		gst_object_unref (GST_OBJECT (pipeline));
	}
	g_print ("trying pipeline: %s\n", try_pipe);
	
	pipeline = gst_pipeline_new ("launch");
	gst_parse_launch (try_pipe, GST_BIN (pipeline));
	gtk_widget_set_sensitive(GTK_WIDGET(start_but), TRUE);

	history = g_list_prepend(history, try_pipe);
	gtk_combo_set_popdown_strings(GTK_COMBO(pipe_combo), history);	
	g_free(write_pipe);
}

void
start_callback( GtkWidget *widget,
                gpointer   data )
{
	GtkWidget *pipe_combo = gtk_object_get_data(GTK_OBJECT(widget), "pipe_combo");
	gchar *try_pipe = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(pipe_combo)->entry));
	
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

int main(int argc,char *argv[]) {
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *parse_line, *pipe_combo, *notebook;

	gst_init(&argc,&argv);
	
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
	
	gtk_object_set_data(GTK_OBJECT(start_but), "pipe_combo", pipe_combo);
	
	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox),notebook,TRUE,TRUE,0);
	
	build_debug_page(notebook);
	
	status = gtk_label_new("stopped");
	gtk_box_pack_start(GTK_BOX(vbox),status,FALSE,FALSE,0);
	
	gtk_widget_show_all(window);
	gtk_main();

	return 0;
}
