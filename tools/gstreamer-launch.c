#include <glib.h>
#include <gst/gst.h>
#include <gst/gstparse.h>
#include <string.h>
#include <stdlib.h>

static int launch_argc;
static char **launch_argv;
int xid = 0;

GtkWidget *window;
GtkWidget *hbox;
GtkWidget *gtk_socket;


typedef void (*found_handler) (GstElement *element, GtkArg *arg, void *priv);

void
arg_search (GstBin *bin, gchar *argname, found_handler handler, void *priv)
{
  GList *children;
  GstElement *child;
  GtkType type;
  GtkArg *args;
  guint32 *flags;
  guint num_args,i;
  gchar *ccargname;

  ccargname = g_strdup_printf("::%s",argname);

  children = gst_bin_get_list(bin);

  while (children) {
    child = GST_ELEMENT(children->data);
    children = g_list_next(children);
//    fprintf(stderr,"have child \"%s\"\n",gst_object_get_path_string(GST_OBJECT(child)));

    if (GST_IS_BIN(child)) arg_search(GST_BIN(child),argname,handler,priv);
    else {
      type = GTK_OBJECT_TYPE(child);
      while (type != GTK_TYPE_INVALID) {
        args = gtk_object_query_args(type,&flags,&num_args);
        for (i=0;i<num_args;i++) {
//fprintf(stderr,"arg is \"%s\"\n",args[i].name);
          if (strstr(args[i].name,ccargname))
            (handler)(child,&args[i],priv);
        }
        type = gtk_type_parent(type);
      }
    }
  }

  g_free(ccargname);
}

void handle_have_size(GstElement *element,int width,int height) {
  fprintf(stderr,"have size from xvideosink: %dx%d\n",width,height);
  gtk_widget_set_usize(gtk_socket,width,height);
}

void xid_handler(GstElement *element, GtkArg *arg, void *priv) {
  fprintf(stderr,"have xid\n");

  xid = GTK_VALUE_INT(*arg);

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new(TRUE,10);
  gtk_socket = gtk_socket_new ();
  gtk_widget_set_usize(gtk_socket,720,480);
  gtk_widget_show(gtk_socket);
  gtk_container_add(GTK_CONTAINER(window),hbox);
  gtk_box_pack_end(GTK_BOX(hbox),gtk_socket,TRUE,TRUE,0);
  gtk_widget_realize(gtk_socket);
  gtk_socket_steal (GTK_SOCKET (gtk_socket), xid);
  gtk_object_set(GTK_OBJECT(window),"allow_grow",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(window),"allow_shrink",TRUE,NULL);
  gtk_widget_show_all(window);

  gtk_signal_connect (GTK_OBJECT (element), "have_size",
                      GTK_SIGNAL_FUNC (handle_have_size), element);
}

gboolean
idle_func (gpointer data)
{
  return gst_bin_iterate (GST_BIN (data));
}

int
main(int argc, char *argv[])
{
  GstElement *pipeline;
  char **argvn;
  gchar *cmdline;
  int i;

  gst_init (&argc, &argv);

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

  gst_parse_launch (cmdline, GST_BIN (pipeline));

  arg_search(GST_BIN(pipeline),"xid",xid_handler,NULL);

//  xmlSaveFile("gstreamer-launch.gst",gst_xml_write(pipeline));

  fprintf(stderr,"RUNNING pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

//  if (have_window) {
    gtk_idle_add(idle_func,pipeline);
fprintf(stderr,"going into gtk_main()\n");
    gtk_main();
//  } else {
//    while (gst_bin_iterate (GST_BIN (pipeline)));
//  }

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
