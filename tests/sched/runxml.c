#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

static guint outcount, incount;

static void
buffer_handoff_sink (GstElement * src, GstBuffer * buf, GstElement * bin)
{
  g_print ("\n\n *** buffer arrived in sink ***\n\n");
  gst_element_set_state (bin, GST_STATE_NULL);

  outcount++;
}

static void
buffer_handoff_src (GstElement * src, GstBuffer * buf, GstElement * bin)
{
  g_print ("\n\n *** buffer started in src ***\n\n");
  incount++;
}

/* eos will be called when the src element has an end of stream */
void
eos (GstElement * element, gpointer data)
{
  g_print ("have eos, quitting\n");
}

int
main (int argc, char *argv[])
{
  GstXML *xml;
  GList *toplevelelements;
  gint i = 1;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <xml file>\n", argv[0]);
    exit (-1);
  }

  g_print ("\n *** using testfile %s\n", argv[1]);

  xml = gst_xml_new ();
  gst_xml_parse_file (xml, argv[1], NULL);

  toplevelelements = gst_xml_get_topelements (xml);

  while (toplevelelements) {
    GstElement *bin = (GstElement *) toplevelelements->data;
    GstElement *src, *sink;

    g_print ("\n ***** testcase %d\n", i++);

    src = gst_bin_get_by_name (GST_BIN (bin), "fakesrc");
    if (src) {
      g_signal_connect (G_OBJECT (src), "handoff",
          G_CALLBACK (buffer_handoff_src), bin);
    } else {
      g_print ("could not find src element\n");
      exit (-1);
    }

    sink = gst_bin_get_by_name (GST_BIN (bin), "fakesink");
    if (sink) {
      g_signal_connect (G_OBJECT (sink), "handoff",
          G_CALLBACK (buffer_handoff_sink), bin);
    } else {
      g_print ("could not find sink element\n");
      exit (-1);
    }

    incount = 0;
    outcount = 0;

/*    gst_element_set_state(bin, GST_STATE_READY); */
    gst_element_set_state (bin, GST_STATE_PLAYING);

    if (GST_IS_THREAD (bin)) {
      g_usleep (G_USEC_PER_SEC);
    } else {
      gst_bin_iterate (GST_BIN (bin));
    }

    if (outcount != 1 && incount != 1) {
      g_print ("test failed\n");
      exit (-1);
    }

    toplevelelements = g_list_next (toplevelelements);
  }

  exit (0);
}
