#include <gst/gst.h>

int main(int argc,char *argv[]) {
  gst_init(&argc,&argv);
  gst_plugin_load_all();
}
