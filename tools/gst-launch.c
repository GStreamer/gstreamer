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
property_change_callback (GObject *object, GstObject *orig, GParamSpec *pspec)
{
  GValue value = { 0, }; /* the important thing is that value.type = 0 */
  gchar *str = 0;
  
  if (pspec->flags & G_PARAM_READABLE) {
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);
    /* fix current bug with g_strdup_value_contents not working with gint64 */
    if (G_IS_PARAM_SPEC_INT64 (pspec))
      str = g_strdup_printf ("%lld", g_value_get_int64 (&value));
    else
      str = g_strdup_value_contents (&value);
    g_print ("%s: %s = %s\n", GST_OBJECT_NAME (orig), pspec->name, str);
    g_free (str);
    g_value_unset(&value);
  } else {
    g_warning ("Parameter not readable. What's up with that?");
  }
}

static void
error_callback (GObject *object, GstObject *orig, gchar *error)
{
  g_print ("ERROR: %s: %s\n", GST_OBJECT_NAME (orig), error);
}

static GstElement*
xmllaunch_parse_cmdline (const gchar **argv) 
{
  GstElement *pipeline = NULL, *e;
  GstXML *xml;
  gboolean err;
  const gchar *arg;
  gchar *element, *property, *value;
  GList *l;
  gint i = 0;
  
  if (!(arg = argv[0])) {
    g_print ("usage: gst-xmllaunch <file.xml> [ element.property=value ... ]\n");
    exit (1);
  }
  
  xml = gst_xml_new ();
  err = gst_xml_parse_file(xml, arg, NULL);
  
  if (err != TRUE) {
    fprintf (stderr, "ERROR: parse of xml file '%s' failed\n", arg);
    exit (1);
  }
  
  l = gst_xml_get_topelements (xml);
  if (!l) {
    fprintf (stderr, "ERROR: no toplevel pipeline element in file '%s'\n", arg);
    exit (1);
  }
    
  if (l->next)
    g_warning ("only one toplevel element is supported at this time");
  
  pipeline = GST_ELEMENT (l->data);
  
  while ((arg = argv[++i])) {
    element = g_strdup (arg);
    property = strchr (element, '.');
    value = strchr (element, '=');
    
    if (!(element < property && property < value)) {
      fprintf (stderr, "ERROR: could not parse command line argument %d: %s", i, element);
      g_free (element);
      exit (1);
    }
    
    *property++ = '\0';
    *value++ = '\0';
    
    e = gst_bin_get_by_name (GST_BIN (pipeline), element);
    if (!e) {
      g_warning ("element named '%s' not found", element);
    } else {
      gst_util_set_object_arg (G_OBJECT (e), property, value);
    }
    g_free (element);
  }
  
  if (!l)
    return NULL;
  else
    return l->data;
}

int
main(int argc, char *argv[])
{
/* options */
  gboolean silent = FALSE;
  struct poptOption options[] = {
    {"silent",	's',  POPT_ARG_NONE|POPT_ARGFLAG_STRIP,	&silent, 0, "do not output status information", NULL},
    POPT_TABLEEND
  };

  GstElement *pipeline;
  gchar **argvn;
  gboolean save_pipeline = FALSE;
  gboolean run_pipeline = TRUE;
  gchar *savefile = "";

  free (malloc (8)); /* -lefence */

  gst_init_with_popt_table (&argc, &argv, options);
  
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

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *,argc);
  memcpy (argvn, argv+1, sizeof (char*) * (argc-1));
  if (strstr (argv[0], "gst-xmllaunch")) {
    pipeline = xmllaunch_parse_cmdline ((const gchar **) argvn);
  } else {
    pipeline = (GstElement*) gst_parse_launchv ((const gchar **) argvn);
  }

  if (!pipeline) {
    fprintf(stderr, "ERROR: pipeline could not be constructed\n");
    exit(1);
  }
  
  if (!silent)
    g_signal_connect (pipeline, "deep_notify", G_CALLBACK (property_change_callback), NULL);
  g_signal_connect (pipeline, "error", G_CALLBACK (error_callback), NULL);
  
#ifndef GST_DISABLE_LOADSAVE
  if (save_pipeline) {
    gst_xml_write_file (GST_ELEMENT (pipeline), fopen (savefile, "w"));
  }
#endif
  
  if (run_pipeline) {
    fprintf(stderr,"RUNNING pipeline\n");
    if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS) {
      fprintf(stderr,"pipeline doesn't want to play\n");
      exit (-1);
    }

    g_idle_add (idle_func, pipeline);
    gst_main ();

    gst_element_set_state (pipeline, GST_STATE_NULL);

  }
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
