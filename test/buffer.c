#include <stdlib.h>
#include <glib.h>
#include <gst/gst.h>

#include "mem.h"

int main(int argc,char *argv[]) {
  GstBuffer *buf;
  GstBuffer **buffers;
  gpointer dummy;
  int i,max;
  long usage1,usage2;

  gst_init(&argc,&argv);

  max = atoi(argv[1]);

  g_print("creating and destroying a buffer %d times...",max);
  usage1 = vmsize();
  for (i=0;i<max;i++) {
    buf = gst_buffer_new();
    gst_buffer_unref(buf);
  }
  usage2 = vmsize();
  g_print(" used %d more bytes\n",usage2-usage1);

//  g_print("pre-allocating space...");
//  usage1 = vmsize();
//  dummy = g_malloc(100*i);
//  usage2 = vmsize();
//  g_print(" (+%d)\n",usage2-usage1);

  g_print("creating %d buffers...",max);
  buffers = g_new(GstBuffer,i);
  usage1 = vmsize();
  for (i=0;i<max;i++)
    buffers[i] = gst_buffer_new();
//    buffers[i] = (GstBuffer *)g_malloc(1024);
  usage2 = vmsize();
  g_print(" (+%d bytes), and destroying them...",usage2-usage1);
  usage1 = vmsize();
  for (i=0;i<max;i++)
    gst_buffer_unref(buffers[i]);
//    g_free(buffers[i]);
  usage2 = vmsize();
  g_print("(-%d)\n",usage1-usage2);
  g_free(buffers);

  g_print("buffer is %d bytes, list is %d bytes\n",
          sizeof(GstBuffer),sizeof(GList));

  g_print("memory usage is %d\n",vmsize());
}
