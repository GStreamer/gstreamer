#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

/* example based on helloworld by thomas@apestaart.org
   demonstrates the adder plugin and the volume envelope plugin */

/* eos will be called when the src element has an end of stream */
void eos(GstElement *element) 
{
  g_print("have eos, quitting\n");

  playing = FALSE;
}

int main(int argc,char *argv[]) 
{
  GstElement *bin_in1, *bin_in2, *main_bin;
  GstElement *disksrc1, *decoder1, *volenv1;
  GstElement *disksrc2, *decoder2, *volenv2;
  GstElement *adder;
  GstElement *audiosink;

  GstPad *pad; /* to request pads for the adder */

  gst_init(&argc,&argv);

  if (argc != 3) {
    g_print("usage: %s <filename1> <filename2>\n", argv[0]);
    exit(-1);
  }

  /* create input bins */

  /* create bins to hold the input elements */
  bin_in1 = gst_bin_new("bin");
  bin_in2 = gst_bin_new("bin");

  /* first input bin */

  /* create elements */

  disksrc1 = gst_elementfactory_make("disksrc", "disk_source1");
  gtk_object_set(GTK_OBJECT(disksrc1),"location", argv[1],NULL);
  gtk_signal_connect(GTK_OBJECT(disksrc1),"eos",
                     GTK_SIGNAL_FUNC(eos),NULL);
  decoder1 = gst_elementfactory_make("mad","decoder1");
  volenv1 = gst_elementfactory_make("volenv", "volume1");

  /* add to the bin */

  gst_bin_add(GST_BIN(bin_in1), disksrc1);
  gst_bin_add(GST_BIN(bin_in1), decoder1);
  gst_bin_add(GST_BIN(bin_in1), volenv1);

  /* connect elements */
  gst_pad_connect(gst_element_get_pad(disksrc1,"src"),
                  gst_element_get_pad(decoder1,"sink"));
  gst_pad_connect(gst_element_get_pad(decoder1,"src"),
                  gst_element_get_pad(volenv1,"sink"));

  /* add a ghost pad */
  gst_element_add_ghost_pad (bin_in1, gst_element_get_pad (volenv1, "src"), "channel1");

  /* second input bin */

  /* create elements */

  disksrc2 = gst_elementfactory_make("disksrc", "disk_source2");
  gtk_object_set(GTK_OBJECT(disksrc2),"location", argv[2],NULL);
  gtk_signal_connect(GTK_OBJECT(disksrc2),"eos",
                     GTK_SIGNAL_FUNC(eos),NULL);
  decoder2 = gst_elementfactory_make("mad","decoder2");
  volenv2 = gst_elementfactory_make("volenv", "volume2");

  /* add to the bin */

  gst_bin_add(GST_BIN(bin_in2), disksrc2);
  gst_bin_add(GST_BIN(bin_in2), decoder2);
  gst_bin_add(GST_BIN(bin_in2), volenv2);

  /* connect elements */
  gst_pad_connect(gst_element_get_pad(disksrc2,"src"),
                  gst_element_get_pad(decoder2,"sink"));
  gst_pad_connect(gst_element_get_pad(decoder2,"src"),
                  gst_element_get_pad(volenv2,"sink"));

  /* add a ghost pad */
  gst_element_add_ghost_pad (bin_in2, gst_element_get_pad (volenv2, "src"), "channel2");
  

  /* create adder */
  adder = gst_elementfactory_make("adder", "adderel");

  /* create an audio sink */
  audiosink = gst_elementfactory_make("esdsink", "play_audio");

  /* now create main bin */
  main_bin = gst_bin_new("bin");

  gst_bin_add(GST_BIN(main_bin), bin_in1);
  gst_bin_add(GST_BIN(main_bin), bin_in2);
  gst_bin_add(GST_BIN(main_bin), adder);
  gst_bin_add(GST_BIN(main_bin), audiosink);

  /* request pads and connect to adder */

  pad = gst_element_request_pad_by_name (adder, "sink%d");
  g_print ("new pad %s\n", gst_pad_get_name (pad));
  gst_pad_connect (gst_element_get_pad (bin_in1, "channel1"), pad);
  pad = gst_element_request_pad_by_name (adder, "sink%d");
  g_print ("new pad %s\n", gst_pad_get_name (pad));
  gst_pad_connect (gst_element_get_pad (bin_in2, "channel2"), pad);

  /* connect adder and audiosink */

  gst_pad_connect(gst_element_get_pad(adder,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  /* register the volume envelope */

  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "0:0.0001", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "2:1", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "4:1", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "5:.1", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "6:1", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "7:.1", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "8:1", NULL);
  gtk_object_set(GTK_OBJECT(volenv1), "controlpoint", "15:0.8", NULL);

  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "0:0.001", NULL);
  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "4:0.001", NULL);
  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "5:1", NULL);
  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "6:.1", NULL);
  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "7:1", NULL);
  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "8:.1", NULL);
  gtk_object_set(GTK_OBJECT(volenv2), "controlpoint", "15:0.001", NULL);

  /* start playing */
  gst_element_set_state(main_bin, GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate(GST_BIN(main_bin));
  }

  /* stop the bin */
  gst_element_set_state(main_bin, GST_STATE_NULL);

  gst_object_destroy(GST_OBJECT(audiosink));

  gst_object_destroy(GST_OBJECT(disksrc1));
  gst_object_destroy(GST_OBJECT(decoder1));
  gst_object_destroy(GST_OBJECT(volenv1));

  gst_object_destroy(GST_OBJECT(disksrc2));
  gst_object_destroy(GST_OBJECT(decoder2));
  gst_object_destroy(GST_OBJECT(volenv2));

  gst_object_destroy(GST_OBJECT(bin_in1));
  gst_object_destroy(GST_OBJECT(bin_in2));
  gst_object_destroy(GST_OBJECT(main_bin));

  exit(0);
}

