#include <glib.h>
#include <gst/gst.h>
#include <gst/gstparse.h>
#include <string.h>
#include <stdlib.h>
#include <gst/gstpropsprivate.h>
#include <sys/time.h>

static int    launch_argc;
static char **launch_argv;

static guint64 iterations = 0;
static guint64 sum = 0;
static guint64 min = G_MAXINT;
static guint64 max = 0;

gboolean
idle_func (gpointer data)
{
  gboolean busy;
  struct timeval tfthen, tfnow;
  guint64 diff;

  gettimeofday (&tfthen, (struct timezone *)NULL);
  busy = gst_bin_iterate (GST_BIN (data));
  iterations++;
  gettimeofday (&tfnow, (struct timezone *)NULL);

  diff = ((guint64)tfnow.tv_sec*1000000LL+tfnow.tv_usec) - 
         ((guint64)tfthen.tv_sec*1000000LL+tfthen.tv_usec); 

  sum += diff; 
  min = MIN (min, diff);
  max = MAX (max, diff);

  if (!busy) {
    gst_main_quit ();
    g_print ("execution ended after %llu iterations (sum %llu us, average %llu us, min %llu us, max %llu us)\n", 
		    iterations, sum, sum/iterations, min, max);
  }

  return busy;
}

static void 
print_props (gpointer data, gpointer user_data)
{
  GstPropsEntry *entry = (GstPropsEntry *)data;
  GstElement *element = GST_ELEMENT (user_data);

  g_print ("%s: %s: ", gst_element_get_name (element), 
		  g_quark_to_string (entry->propid));
  switch (entry->propstype) {
    case GST_PROPS_INT_ID:
      g_print ("%d\n", entry->data.int_data);
      break;
    case GST_PROPS_STRING_ID:
      g_print ("%s\n", entry->data.string_data.string);
      break;
    case GST_PROPS_FLOAT_ID:
      g_print ("%f\n", entry->data.float_data);
      break;
    default:
      g_print ("unknown\n");
  }
}

static void 
event_func (GstElement *element, GstEvent *event)
{
  GstProps *props;

  if (event == NULL)
    return;
  
  if (GST_EVENT_TYPE (event) == GST_EVENT_INFO) {
    props = GST_EVENT_INFO_PROPS (event);

    g_list_foreach (props->properties, print_props, GST_EVENT_SRC (event));
  }
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

  free (malloc (8)); /* -lefence */

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

  g_signal_connect (G_OBJECT (pipeline), "event", G_CALLBACK (event_func), NULL);
  /* make a null-terminated version of argv */
  argvn = g_new0 (char *,argc);
  memcpy (argvn, argv+1, sizeof (char*) * (argc-1));

  /* escape spaces */
  for (i=0; i<argc-1; i++) {
    gchar **split;

    split = g_strsplit (argvn[i], " ", 0);

    argvn[i] = g_strjoinv ("\\ ", split);
    g_strfreev (split);
  }
  /* join the argvs together */
  cmdline = g_strjoinv (" ", argvn);
  /* free the null-terminated argv */
  g_free (argvn);

  /* fail if there are no pipes in it (needs pipes for a pipeline */
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
    gst_buffer_print_stats();
    fprintf(stderr,"RUNNING pipeline\n");
    if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS) {
      fprintf(stderr,"pipeline doesn't want to play\n");
      exit (-1);
    }

    g_idle_add (idle_func, pipeline);
    gst_main ();

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_buffer_print_stats();

  }
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
