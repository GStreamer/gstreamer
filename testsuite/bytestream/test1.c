#include <string.h>
#include <stdlib.h>

#include <gst/gst.h>

#define VM_THRES 1000
#define MAX_CONFIG_LINE 255
#define MAX_CONFIG_PATTERN 64

typedef struct
{
  gint src_data;
  gint src_sizetype;

  gchar *bs_accesspattern;

  gboolean integrity_check;
}
TestParam;

static GSList *params = NULL;

static guint8 count;
static guint iterations;
static gboolean integrity_check = TRUE;
static gboolean verbose = FALSE;
static gboolean dump = FALSE;

static void
handoff (GstElement * element, GstBuffer * buf, GstPad * pad, gpointer data)
{
  if (GST_IS_BUFFER (buf)) {
    if (integrity_check) {
      gint i;
      guint8 *ptr = GST_BUFFER_DATA (buf);

      for (i = 0; i < GST_BUFFER_SIZE (buf); i++) {
        if (*ptr++ != count++) {
          g_print ("data error!\n");
          return;
        }
      }
    }
  } else {
    g_print ("not a buffer ! %p\n", buf);
  }
}
static gchar *
create_desc (TestParam * param)
{
  gchar *desc;

  desc =
      g_strdup_printf ("%s %s, pattern %s",
      (param->src_sizetype == 2 ? "fixed" : "random"),
      (param->src_data == 1 ? "src" : "subbuffer"), param->bs_accesspattern);
  return desc;
}

static gboolean
read_param_file (gchar * filename)
{
  FILE *fp;
  gchar line[MAX_CONFIG_LINE + 1];
  guint linenr = 0;
  gchar pattern[MAX_CONFIG_PATTERN];
  gint data, sizetype, integrity_check;
  gchar *scan_str;
  gboolean res = TRUE;

  fp = fopen (filename, "r");
  if (fp == NULL)
    return FALSE;

  scan_str = g_strdup_printf ("%%d %%d %%%ds %%d", MAX_CONFIG_PATTERN - 1);

  while (fgets (line, MAX_CONFIG_LINE, fp)) {
    linenr++;

    if (line[0] == '\n' || line[0] == '#')
      continue;

    if (sscanf (line, scan_str, &data, &sizetype, pattern,
            &integrity_check) != 4) {
      g_print ("error on line: %d\n", linenr);
      res = FALSE;
      break;
    } else {
      TestParam *param = g_malloc (sizeof (TestParam));

      param->src_data = data;
      param->src_sizetype = sizetype;
      param->bs_accesspattern = g_strdup (pattern);
      param->integrity_check = (integrity_check == 0 ? FALSE : TRUE);

      params = g_slist_append (params, param);
    }
  }
  g_free (scan_str);

  return res;
}

static void
run_test (GstBin * pipeline, gint iters)
{
  gint vm = 0;
  gint maxiters = iters;
  gint prev_percent = -1;

  count = 0;
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (iters) {
    gint newvm = gst_alloc_trace_live_all ();
    gint percent;

    percent = (gint) ((maxiters - iters + 1) * 100.0 / maxiters);

    if (percent != prev_percent || newvm - vm > VM_THRES) {
      g_print ("\r%d (delta %d) %.3d%%               ", newvm, newvm - vm,
          percent);
      prev_percent = percent;
      vm = newvm;
    }
    gst_bin_iterate (pipeline);

    if (iters > 0)
      iters--;
  }
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}

static void
usage (char *argv[])
{
  g_print ("usage: %s [--verbose] [--dump] <paramfile> <iterations>\n",
      argv[0]);
}

int
main (int argc, char *argv[])
{
  GstElement *src;
  GstElement *sink;
  GstElement *bs;
  GstElement *pipeline;
  gint testnum = 0;
  GSList *walk;
  gint arg_walk;

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);
  gst_init (&argc, &argv);

  arg_walk = 1;
  while ((arg_walk < argc) && (argv[arg_walk][0] == '-')) {
    if (!strncmp (argv[arg_walk], "--verbose", 9))
      verbose = TRUE;
    else if (!strncmp (argv[arg_walk], "--dump", 6))
      dump = TRUE;
    else {
      g_print ("unknown option %s (ignored)\n", argv[arg_walk]);
    }

    arg_walk++;
  }
  if (argc - arg_walk < 2) {
    usage (argv);
    return -1;
  }
  if (!read_param_file (argv[arg_walk])) {
    g_print ("error reading file %s\n", argv[arg_walk]);
    usage (argv);
    return -1;
  }
  arg_walk++;
  iterations = atoi (argv[arg_walk]);

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  g_assert (pipeline);

  src = gst_element_factory_make ("fakesrc", "src");
  g_assert (src);

  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink);
  g_object_set (sink, "signal-handoff", TRUE, NULL);
  g_signal_connect (G_OBJECT (sink), "handoff", G_CALLBACK (handoff), NULL);

  bs = gst_element_factory_make ("bstest", "bs");
  g_assert (bs);

  gst_element_link_many (src, bs, sink);

  gst_bin_add_many (GST_BIN (pipeline), src, bs, sink);

  walk = params;

  while (walk) {
    gchar *desc;
    TestParam *param = (TestParam *) (walk->data);

    integrity_check = param->integrity_check;

    g_print ("\n\nrunning test %d (%d iterations):\n", testnum + 1, iterations);
    desc = create_desc (param);
    g_print ("%s\n", desc);
    g_free (desc);

    g_object_set (G_OBJECT (src), "data", param->src_data,
        "sizetype", param->src_sizetype,
        "filltype", (integrity_check ? 5 : 0), "silent", !verbose, NULL);

    g_object_set (G_OBJECT (bs), "accesspattern", param->bs_accesspattern,
        "silent", !verbose, NULL);

    g_object_set (G_OBJECT (sink), "dump", dump, "silent", !verbose, NULL);

    run_test (GST_BIN (pipeline), iterations);

    testnum++;

    walk = g_slist_next (walk);
  }

  g_print ("\n\ndone\n");

  return 0;

}
