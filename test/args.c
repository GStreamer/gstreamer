#include <glib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstElement *src;
  GList *padlist;
  GtkArg arg;

  gst_init(&argc,&argv);

  src = gst_disksrc_new("fakesrc");
  gtk_object_set(GTK_OBJECT(src),"location","demo.mp3",NULL);

  arg.name = "location";
  gtk_object_getv(GTK_OBJECT(src),1,&arg);
  g_print("location is %s\n",GTK_VALUE_STRING(arg));

  gst_object_destroy(GST_OBJECT(src));
}

