#include <glib.h>
#include <gst/gst.h>
#include <gst/gstparse.h>
#include <string.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
  GstElement *pipeline;
  char **argvn;
  gchar *cmdline;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("launch");

  // make a null-terminated version of argv
  argvn = g_new0 (char *,argc);
  memcpy (argvn, argv+1, sizeof (char*) * (argc-1));
  // join the argvs together
  cmdline = g_strjoinv (" ", argvn);
  // free the null-terminated argv
  g_free (argvn);

  gst_parse_launch (cmdline, GST_BIN (pipeline));

  fprintf(stderr,"RUNNING pipeline\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
