#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>

/* FIXME: WTF does this do? */

static guint64 max = 0, min = -1, total = 0;
static guint count = 0;
static guint print_del = 1;
static guint iterations = 0;
static guint mhz = 0;

void
handoff_src (GstElement * src, GstBuffer * buf, gpointer user_data)
{
  gst_trace_read_tsc (&GST_BUFFER_TIMESTAMP (buf));
}

void
handoff_sink (GstElement * sink, GstBuffer * buf, gpointer user_data)
{
  guint64 end, d, avg;
  guint avg_ns;

  gst_trace_read_tsc (&end);
  d = end - GST_BUFFER_TIMESTAMP (buf);
  if (d > max)
    max = d;
  if (d < min)
    min = d;
  total += d;
  count++;
  avg = total / count;
  avg_ns = (guint) (1000.0 * (double) avg / (double) mhz);

  if ((count % print_del) == 0) {
    g_print ("%07d:%08" G_GUINT64_FORMAT " min:%08" G_GUINT64_FORMAT " max:%08"
	G_GUINT64_FORMAT " avg:%08" G_GUINT64_FORMAT " avg-s:0.%09d\r", count,
	d, min, max, avg, avg_ns);
  }
}

GstElement *
identity_add (GstPipeline * pipeline, GstElement * first, int count)
{
  GstElement *last, *ident;
  int i;
  char buf[20];

  last = first;

  for (i = 0; i < count; i++) {
    snprintf (buf, 20, "identity_%03d", i);
    ident = gst_element_factory_make ("identity", buf);
    g_return_val_if_fail (ident != NULL, NULL);
    g_object_set (G_OBJECT (ident), "silent", TRUE, NULL);
    gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (ident));
    gst_pad_link (gst_element_get_pad (last, "src"),
	gst_element_get_pad (ident, "sink"));
    last = ident;
  }

  return last;
}

GstElement *
fakesrc (void)
{
  GstElement *src;

  src = gst_element_factory_make ("fakesrc", "src");
  g_return_val_if_fail (src != NULL, NULL);
  g_object_set (G_OBJECT (src), "silent", TRUE, NULL);
  g_object_set (G_OBJECT (src), "num_buffers", iterations, NULL);
  g_signal_connect (G_OBJECT (src), "handoff", G_CALLBACK (handoff_src), NULL);

  return src;
}

GstElement *
fakesink (void)
{
  GstElement *sink;

  sink = gst_element_factory_make ("fakesink", "fakesink");
  g_return_val_if_fail (sink != NULL, NULL);
  g_object_set (G_OBJECT (sink), "silent", TRUE, NULL);
  g_signal_connect (G_OBJECT (sink),
      "handoff", G_CALLBACK (handoff_sink), NULL);

  return sink;
}

GstPipeline *
simple (int argc, int argi, char *argv[])
{
  GstPipeline *pipeline;
  GstElement *last, *src, *sink;
  int idents;

  if ((argc - argi) < 1) {
    fprintf (stderr, "bad params");
    return NULL;
  }
  idents = atoi (argv[argi]);
  if ((argc - argi) == 2) {
    gst_scheduler_factory_set_default_name (argv[argi + 1]);
  }

  pipeline = GST_PIPELINE (gst_pipeline_new ("pipeline"));
  g_return_val_if_fail (pipeline != NULL, NULL);

  src = fakesrc ();
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (src));
  last = identity_add (pipeline, src, idents);
  sink = fakesink ();
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (sink));
  gst_pad_link (gst_element_get_pad (last, "src"),
      gst_element_get_pad (sink, "sink"));

  return pipeline;
}

GstPipeline *
queue (int argc, int argi, char *argv[])
{
  GstPipeline *pipeline;
  GstElement *last, *src, *sink, *src_thr, *src_q, *sink_q, *sink_thr;
  int idents;

  if ((argc - argi) < 1) {
    fprintf (stderr, "bad params");
    return NULL;
  }
  idents = atoi (argv[argi]);

  if ((argc - argi) == 2) {
    gst_scheduler_factory_set_default_name (argv[argi + 1]);
  }

  pipeline = GST_PIPELINE (gst_pipeline_new ("pipeline"));
  g_return_val_if_fail (pipeline != NULL, NULL);

  src_thr = GST_ELEMENT (gst_thread_new ("src_thread"));
  g_return_val_if_fail (src_thr != NULL, NULL);

  src = fakesrc ();
  g_return_val_if_fail (src != NULL, NULL);
  gst_bin_add (GST_BIN (src_thr), GST_ELEMENT (src));

  src_q = gst_element_factory_make ("queue", "src_q");
  g_return_val_if_fail (src_q != NULL, NULL);
  gst_bin_add (GST_BIN (src_thr), GST_ELEMENT (src_q));
  gst_pad_link (gst_element_get_pad (src, "src"),
      gst_element_get_pad (src_q, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (src_thr));

  last = identity_add (pipeline, src_q, idents);

  sink_q = gst_element_factory_make ("queue", "sink_q");
  g_return_val_if_fail (sink_q != NULL, NULL);
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (sink_q));
  gst_pad_link (gst_element_get_pad (last, "src"),
      gst_element_get_pad (sink_q, "sink"));

  sink_thr = GST_ELEMENT (gst_thread_new ("sink_thread"));
  g_return_val_if_fail (sink_thr != NULL, NULL);

  sink = fakesink ();
  g_return_val_if_fail (sink != NULL, NULL);
  gst_bin_add (GST_BIN (sink_thr), GST_ELEMENT (sink));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (sink_thr));

  gst_pad_link (gst_element_get_pad (sink_q, "src"),
      gst_element_get_pad (sink, "sink"));

  return pipeline;
}

struct test
{
  char *name;
  char *params;
  GstPipeline *(*func) (int argc, int argi, char *argv[]);
};

static struct test tests[] = {
  {"simple", "ident_count [scheduler_name]", simple},
  {"queue", "ident_count [scheduler_name]", queue},
  {NULL, NULL, NULL}
};

int
main (int argc, char *argv[])
{
  GstPipeline *pipeline;
  int i;
  char *name;

  gst_init (&argc, &argv);

  if (argc < 3) {
    fprintf (stderr,
	"usage: %s iterations print_del mhz test_name [test_params...]\n",
	argv[0]);
    for (i = 0; tests[i].name; i++) {
      fprintf (stderr, "  %s %s\n", tests[i].name, tests[i].params);
    }
    exit (1);
  } else {
    iterations = atoi (argv[1]);
    print_del = atoi (argv[2]);
    mhz = atoi (argv[3]);
    name = argv[4];
  }

  pipeline = NULL;
  for (i = 0; tests[i].name && !pipeline; i++) {
    if (!strcmp (name, tests[i].name)) {
      pipeline = tests[i].func (argc, 5, argv);
    }
  }
  g_return_val_if_fail (pipeline != NULL, -1);

  /*xmlSaveFile("lat.gst", gst_xml_write(GST_ELEMENT(pipeline))); */

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (count < iterations) {
    gst_bin_iterate (GST_BIN (pipeline));
  }
  g_print ("\n");

  return 0;
}
