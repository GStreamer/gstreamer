#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstElement *element;

  gst_init(&argc,&argv);

  element = gst_elementfactory_make(argv[1],"element");

  return 0;
}
