#include <gst/gst.h>

int main(int argc,char *argv[]) 
{
  GstElement *bin, *decoder;

  gst_init(&argc,&argv);

  bin = gst_pipeline_new("pipeline");

  while (TRUE)
  {
    decoder = gst_elementfactory_make("mpeg2dec","mpeg2dec");
    if (!decoder) 
      break;

    gst_bin_add(GST_BIN(bin), decoder);
    gst_bin_remove(GST_BIN(bin), decoder);
  }
}
