#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <gst/gst.h>

static gboolean
idle_func (gpointer data) 
{
  gst_bin_iterate (GST_BIN (data));

  return TRUE;
}

int 
main (int argc,char *argv[]) 
{
  GstElement *bin;
  GstElement *src;
  GstElement *dvdec;
  GstElement *cspace;
  GstElement *videoscale;
  GstElement *encoder;
  GstElement *fdsink;

  gint fd_video;

  gst_init (&argc, &argv);

  bin = gst_pipeline_new ("pipeline");

  src = gst_elementfactory_make ("disksrc", "src");
  gtk_object_set (GTK_OBJECT (src), "location", argv[1], "bytesperread", 480, NULL);

  dvdec = gst_elementfactory_make ("dvdec", "decoder");
  cspace = gst_elementfactory_make ("colorspace", "cspace");
  //videoscale = gst_elementfactory_make ("videoscale", "videoscale");
  //gtk_object_set (GTK_OBJECT (videoscale), "width", 352, "height",288, NULL);
  encoder = gst_elementfactory_make ("mpeg2enc", "mpeg2enc");
  fdsink = gst_elementfactory_make ("fdsink", "fdsink");

  fd_video = open (argv[2], O_CREAT|O_RDWR|O_TRUNC);
  gtk_object_set (GTK_OBJECT (fdsink), "fd", fd_video, NULL);

  gst_bin_add (GST_BIN (bin), GST_ELEMENT (src));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (dvdec));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (cspace));
  //gst_bin_add (GST_BIN (bin), GST_ELEMENT (videoscale));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (encoder));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (fdsink));

  gst_element_connect (src, "src", dvdec, "sink");
  gst_element_connect (cspace, "src", encoder, "sink");
  //gst_element_connect (videoscale, "src", encoder, "sink");
  gst_element_connect (encoder, "src", fdsink, "sink");
  gst_element_connect (dvdec, "video", cspace, "sink");

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  g_idle_add (idle_func, bin);

  gtk_main ();

  exit (0);
}

