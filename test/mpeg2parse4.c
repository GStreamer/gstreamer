#include <gnome.h>
#include <gst/gst.h>

GstElement *pipeline, *src, *parse;
GstElement *v_decode_thread, *v_decode_queue, *v_decode, *v_color;
GstElement *v_show_thread, *v_show_queue, *v_show;
GstElement *a_decode_thread, *a_decode_queue, *a_decode;
GstElement *a_sink_thread, *a_sink_queue, *a_sink;
GtkWidget *appwindow;
GtkWidget  *gtk_socket;

void eof(GstElement *src) {
  fprintf(stderr,"have eos, quitting\n");
  exit(0);
}

gboolean idle_func(gpointer data) {
  gst_bin_iterate(GST_BIN(data));
  return TRUE;
}

int mpeg2parse_newpad(GstElement *parser,GstPad *pad, GstElement *pipeline) {

  fprintf(stderr,"***** a new pad %s was created\n", gst_pad_get_name(pad));

  if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {

    // build the decoder thread
    v_decode_thread = GST_ELEMENT(gst_thread_new("v_decode_thread"));
    g_return_val_if_fail(v_decode_thread != NULL, -1);

    v_decode_queue = gst_elementfactory_make("queue","v_decode_queue");
    g_return_val_if_fail(v_decode_queue != NULL, -1);

    v_decode = gst_elementfactory_make("mpeg2dec","v_decode");
    g_return_val_if_fail(v_decode != NULL, -1);

    v_color = gst_elementfactory_make("colorspace","v_color");
    g_return_val_if_fail(v_color != NULL, -1);

    gst_bin_add(GST_BIN(v_decode_thread),GST_ELEMENT(v_decode_queue));
    gst_bin_add(GST_BIN(v_decode_thread),GST_ELEMENT(v_decode));
    gst_bin_add(GST_BIN(v_decode_thread),GST_ELEMENT(v_color));

    gst_element_connect(v_decode_queue,"src",v_decode,"sink");
    gst_element_connect(v_decode,"src",v_color,"sink");

    gst_schedule_show(GST_ELEMENT_SCHED(v_decode_thread));


    // build the show thread
    v_show_thread = GST_ELEMENT(gst_thread_new("v_show_thread"));
    g_return_val_if_fail(v_show_thread != NULL, -1);

    v_show_queue = gst_elementfactory_make("queue","v_show_queue");
    g_return_val_if_fail(v_show_queue != NULL, -1);

    // v_show has ben created earlier

    gst_bin_add(GST_BIN(v_show_thread),GST_ELEMENT(v_show_queue));
    gst_bin_add(GST_BIN(v_show_thread),GST_ELEMENT(v_show));

    gst_element_connect(v_show_queue,"src",v_show,"sink");


    // now assemble the decoder threads
    gst_bin_add(GST_BIN(v_decode_thread),v_show_thread);
    gst_element_connect(v_color,"src",v_show_queue,"sink");

    gst_schedule_show(GST_ELEMENT_SCHED(v_decode_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(v_show_thread));

    // connect the whole thing to the main pipeline
    gst_pad_connect(pad, gst_element_get_pad(v_decode_queue,"sink"));
    gst_bin_add(GST_BIN(pipeline),v_decode_thread);

    gst_schedule_show(GST_ELEMENT_SCHED(v_decode_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(v_show_thread));

    // start it playing
    gst_element_set_state(v_decode_thread,GST_STATE_PLAYING);

  } else if (strcmp(gst_pad_get_name(pad), "private_stream_1.0") == 0) {
    // build the decoder thread
    a_decode_thread = GST_ELEMENT(gst_thread_new("a_decode_thread"));
    g_return_val_if_fail(a_decode_thread != NULL, -1);

    a_decode_queue = gst_elementfactory_make("queue","a_decode_queue");
    g_return_val_if_fail(a_decode_queue != NULL, -1);

    a_decode = gst_elementfactory_make("a52dec","a_decode");
    g_return_val_if_fail(a_decode != NULL, -1);

    gst_bin_add(GST_BIN(a_decode_thread),GST_ELEMENT(a_decode_queue));
    gst_bin_add(GST_BIN(a_decode_thread),GST_ELEMENT(a_decode));

    gst_element_connect(a_decode_queue,"src",a_decode,"sink");

    gst_schedule_show(GST_ELEMENT_SCHED(a_decode_thread));


    // build the sink thread
    a_sink_thread = GST_ELEMENT(gst_thread_new("a_sink_thread"));
    g_return_val_if_fail(a_sink_thread != NULL, -1);

    a_sink_queue = gst_elementfactory_make("queue","a_sink_queue");
    g_return_val_if_fail(a_sink_queue != NULL, -1);

    a_sink = gst_elementfactory_make("esdsink","a_sink");
    g_return_val_if_fail(a_sink != NULL, -1);

    gst_bin_add(GST_BIN(a_sink_thread),GST_ELEMENT(a_sink_queue));
    gst_bin_add(GST_BIN(a_sink_thread),GST_ELEMENT(a_sink));

    gst_element_connect(a_sink_queue,"src",a_sink,"sink");


    // now assemble the decoder threads
    gst_bin_add(GST_BIN(a_decode_thread),a_sink_thread);
    gst_element_connect(a_decode,"src",a_sink_queue,"sink");

    gst_schedule_show(GST_ELEMENT_SCHED(a_decode_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(a_sink_thread));

    // connect the whole thing to the main pipeline
    gst_pad_connect(pad, gst_element_get_pad(a_decode_queue,"sink"));
    gst_bin_add(GST_BIN(pipeline),a_decode_thread);

    gst_schedule_show(GST_ELEMENT_SCHED(a_decode_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(a_sink_thread));

    // start it playing
    gst_element_set_state(a_decode_thread,GST_STATE_PLAYING);

  }

  if (v_decode_thread && a_decode_thread) {
    xmlSaveFile("mpeg2parse4.gst", gst_xml_write(GST_ELEMENT(pipeline)));
fprintf(stderr,"DUMP OF ALL SCHEDULES!!!:\n");
    gst_schedule_show(GST_ELEMENT_SCHED(pipeline));
    gst_schedule_show(GST_ELEMENT_SCHED(v_decode_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(v_show_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(a_decode_thread));
    gst_schedule_show(GST_ELEMENT_SCHED(a_sink_thread));
  }

  return 0;
}

void mpeg2parse_have_size(GstElement *videosink,gint width,gint height) {
  gtk_widget_set_usize(gtk_socket,width,height);
  gtk_widget_show_all(appwindow);
}

int main(int argc,char *argv[]) {

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);
  gnome_init("MPEG2 Video player","0.0.1",argc,argv);

  // ***** construct the main pipeline *****
  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  if (strstr(argv[1],"video_ts")) {
    src = gst_elementfactory_make("dvdsrc","src");
    g_print("using DVD source\n");
  } else {
    src = gst_elementfactory_make("disksrc","src");
  }

  g_return_val_if_fail(src != NULL, -1);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  if (argc >= 3) {
    gtk_object_set(GTK_OBJECT(src),"bytesperread",atoi(argv[2]),NULL);
    g_print("block size is %d\n",atoi(argv[2]));
  }
  g_print("should be using file '%s'\n",argv[1]);

  parse = gst_elementfactory_make("mpeg2parse","parse");
  //parse = gst_elementfactory_make("mpeg1parse","parse");
  g_return_val_if_fail(parse != NULL, -1);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gst_element_connect(src,"src",parse,"sink");


  // create v_show early so we can get and connect stuff
  v_show = gst_elementfactory_make("xvideosink","v_show");
  g_return_val_if_fail(v_show != NULL, -1);



  // ***** construct the GUI *****
  appwindow = gnome_app_new("MPEG player","MPEG player");

  gtk_socket = gtk_socket_new ();
  gtk_widget_show (gtk_socket);

  gnome_app_set_contents(GNOME_APP(appwindow),
      	        GTK_WIDGET(gtk_socket));

  gtk_widget_realize (gtk_socket);
  gtk_socket_steal (GTK_SOCKET (gtk_socket), 
		  gst_util_get_int_arg (GTK_OBJECT(v_show), "xid"));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",mpeg2parse_newpad, pipeline);
  gtk_signal_connect(GTK_OBJECT(src),"eos",GTK_SIGNAL_FUNC(eof),NULL);
  gtk_signal_connect(GTK_OBJECT(v_show),"have_size",mpeg2parse_have_size, pipeline);

  fprintf(stderr,"setting to PLAYING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  gtk_idle_add(idle_func,pipeline);

  gdk_threads_enter();
  gtk_main();
  gdk_threads_leave();

  return 0;
}
