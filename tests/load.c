#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstElement *element;
  int i;

  gst_init(&argc,&argv);

  element = gst_elementfactory_make(argv[1],"element");
}
