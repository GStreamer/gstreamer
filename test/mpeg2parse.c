#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

static int ac3fd;
static gchar *desired_stream;

void mpeg2parse_write_ac3(GstPad *pad,GstBuffer *buf) {
  g_print(".");
//  g_print("MPEG2PARSE: got AC3 buffer of size %d\n",GST_BUFFER_SIZE(buf));
  write(ac3fd,GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
  gst_buffer_unref(buf);
}

void mpeg2parse_info_chain(GstPad *pad,GstBuffer *buf) {
//  g_print("MPEG2PARSE: got buffer of size %d\n",GST_BUFFER_SIZE(buf));
  gst_buffer_unref(buf);
}

void mpeg2parse_newpad(GstElement *parser,GstPad *pad) {
  GstPad *infopad;

  g_print("MPEG2PARSE: have new pad \"%s\" from parser\n",
          gst_pad_get_name(pad));

  infopad = gst_pad_new("sink",GST_PAD_SINK);
  if (strcmp(gst_pad_get_name(pad),desired_stream) == 0)
    gst_pad_set_chain_function(infopad,mpeg2parse_write_ac3);
  else
    gst_pad_set_chain_function(infopad,mpeg2parse_info_chain);
  gst_pad_connect(pad,infopad);
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *src, *parse, *out;
  GstPad *infopad;
  int i,c;

  g_print("have %d args\n",argc);

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
//  gst_plugin_load("mpeg2parse");
  gst_plugin_load_all();

  ac3fd = creat("output.ac3",S_IREAD|S_IWRITE);

  pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(pipeline != NULL);

  if (strstr(argv[1],"video_ts")) {
    src = gst_elementfactory_make("dvdsrc","src");
    g_print("using DVD source\n");
  } else
    src = gst_elementfactory_make("disksrc","src");
  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  if (argc >= 3) {
    gtk_object_set(GTK_OBJECT(src),"bytesperread",atoi(argv[2]),NULL);
    g_print("block size is %d\n",atoi(argv[2]));
  }
  g_print("should be using file '%s'\n",argv[1]);

  parse = gst_elementfactory_make("mpeg2parse","parse");
  g_return_if_fail(parse != NULL);

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",mpeg2parse_newpad,NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);

  if (argc >= 4) c = atoi(argv[3]);
  else c = 4;
  g_print("c is %d\n",c);

  if (argc >= 5) desired_stream = argv[4];
  else desired_stream = "private_stream_1.0";

  g_print("\n");
  for (i=0;i<c;i++) {
    g_print("\n");
    gst_src_push(GST_SRC(src));
  }
}
