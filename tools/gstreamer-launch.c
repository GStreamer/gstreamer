#include <glib.h>
#include <gst/gst.h>
#include <gst/gstparse.h>
#include <string.h>
#include <stdlib.h>

static int launch_argc;
static char **launch_argv;

#ifndef USE_GLIB2
GtkWidget *window;
GtkWidget *gtk_socket;
#endif

typedef void (*found_handler) (GstElement *element, gint xid, void *priv);

void
arg_search (GstBin *bin, gchar *argname, found_handler handler, void *priv)
{
  GList *children;
  gchar *ccargname;

  ccargname = g_strdup_printf("::%s",argname);

  children = gst_bin_get_list(bin);

#ifndef USE_GLIB2
  while (children) {
    GstElement *child;
     
    child = GST_ELEMENT (children->data);
    children = g_list_next (children);

    if (GST_IS_BIN (child)) arg_search (GST_BIN (child), argname, handler, priv);
    else {
      GtkType type;

      type = GTK_OBJECT_TYPE (child);

      while (type != GTK_TYPE_INVALID) {
        GtkArg *args;
        guint32 *flags;
        guint num_args,i;

        args = gtk_object_query_args(type,&flags,&num_args);

        for (i=0;i<num_args;i++) {
          if (strstr(args[i].name,ccargname)) {
            (handler)(child, gst_util_get_int_arg (GTK_OBJECT (child), argname) ,priv);
	  }
        }
        type = gtk_type_parent(type);
      }
    }
  }
#endif

  g_free(ccargname);
}

gboolean
idle_func (gpointer data)
{
  if (!gst_bin_iterate (GST_BIN (data))) {
#ifndef USE_GLIB2
    gtk_main_quit ();
#endif
    g_print ("iteration ended\n");
    return FALSE;
  }
  return TRUE;
}

void 
handle_have_size (GstElement *element,int width,int height) 
{
#ifndef USE_GLIB2
  gtk_widget_set_usize(gtk_socket,width,height);
  gtk_widget_show_all(window);
#endif
}

void 
xid_handler (GstElement *element, gint xid, void *priv) 
{
#ifndef USE_GLIB2
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_socket = gtk_socket_new ();
  gtk_widget_show(gtk_socket);

  gtk_container_add(GTK_CONTAINER(window),gtk_socket);

  gtk_widget_realize(gtk_socket);
  gtk_socket_steal (GTK_SOCKET (gtk_socket), xid);

  gtk_object_set(GTK_OBJECT(window),"allow_grow",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(window),"allow_shrink",TRUE,NULL);

  gtk_signal_connect (GTK_OBJECT (element), "have_size",
                      GTK_SIGNAL_FUNC (handle_have_size), element);
#endif
}

int
main(int argc, char *argv[])
{
  GstElement *pipeline;
  char **argvn;
  gchar *cmdline;
  int i;
  gboolean save_pipeline = FALSE;
  gboolean run_pipeline = TRUE;
  gchar *savefile = "";

  gst_init (&argc, &argv);

  if (argc >= 3 && !strcmp(argv[1], "-o")) {
    save_pipeline = TRUE;
    run_pipeline = FALSE;
    savefile = argv[2];
    argv[2] = argv[0];
    argv+=2;
    argc-=2;
  }

  launch_argc = argc;
  launch_argv = argv;

  pipeline = gst_pipeline_new ("launch");

  // make a null-terminated version of argv
  argvn = g_new0 (char *,argc);
  memcpy (argvn, argv+1, sizeof (char*) * (argc-1));

  // escape spaces
  for (i=0; i<argc-1; i++) {
    gchar **split;

    split = g_strsplit (argvn[i], " ", 0);

    argvn[i] = g_strjoinv ("\\ ", split);
    g_strfreev (split);
  }
  // join the argvs together
  cmdline = g_strjoinv (" ", argvn);
  // free the null-terminated argv
  g_free (argvn);

  // fail if there are no pipes in it (needs pipes for a pipeline
  if (!strchr(cmdline,'!')) {
    fprintf(stderr,"ERROR: no pipeline description found on commandline\n");
    exit(1);
  }

  if (gst_parse_launch (cmdline, GST_BIN (pipeline)) < 0){
    fprintf(stderr,"ERROR: pipeline description could not be parsed\n");
    exit(1);
  }

#ifndef GST_DISABLE_LOADSAVE
  if (save_pipeline) {
    xmlSaveFile (savefile, gst_xml_write (pipeline));
  }
#endif
  if (run_pipeline) {
    arg_search(GST_BIN(pipeline),"xid",xid_handler,NULL);

    fprintf(stderr,"RUNNING pipeline\n");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    g_idle_add(idle_func,pipeline);
#ifdef USE_GLIB2
    g_main_loop_run (g_main_loop_new (NULL, FALSE));
#else
    gtk_main();
#endif

    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  return 0;
}
