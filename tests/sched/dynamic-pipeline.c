#include <gst/gst.h>

gboolean idle_func (gpointer data);

gboolean 
idle_func (gpointer data)
{
  return gst_bin_iterate ((GstBin*) (data));
}

int main (int argc, char *argv[]) 
{
    GstElement *osssink, *pipe1, *pipe2, *bin, *filesrc, *mad, *fakesink;
    
    gst_init(&argc, &argv);
    
    if (argc!=2) {
        g_print("usage: %s file.mp3\n", argv[0]);
        exit(-1);
    }
    
    filesrc = gst_elementfactory_make("filesrc", "filesrc");
    mad = gst_elementfactory_make("mad", "mad");
    bin = gst_bin_new("bin");
    pipe1 = gst_pipeline_new("pipe1");
    pipe2 = gst_pipeline_new("pipe2");
    osssink = gst_elementfactory_make("osssink", "osssink");
    fakesink = gst_elementfactory_make("fakesink", "fakesink");
    
    g_object_set(G_OBJECT(filesrc), "location", argv[1], NULL);
    
    gst_bin_add (GST_BIN(pipe1), filesrc);
    gst_bin_add (GST_BIN(pipe1), fakesink);
    gst_element_connect(filesrc, "src", fakesink, "sink");
    
    gst_element_set_state(pipe1, GST_STATE_PLAYING);
    gst_bin_iterate(GST_BIN(pipe1));
    gst_element_set_state(pipe1, GST_STATE_READY);
    
    gst_element_disconnect(filesrc, "src", fakesink, "sink");
    gst_object_ref(GST_OBJECT(filesrc));
    gst_bin_remove(GST_BIN(pipe1), filesrc);
    gst_bin_remove(GST_BIN(pipe1), fakesink);
    
    gst_bin_add (GST_BIN(pipe2), filesrc);
    gst_bin_add (GST_BIN(pipe2), mad);
    gst_element_connect(filesrc, "src", mad, "sink");
    gst_bin_add (GST_BIN(pipe2), osssink);
    gst_element_connect(mad, "src", osssink, "sink");
    
    xmlDocDump(stdout, gst_xml_write(pipe2));
    
    gst_element_set_state(pipe2, GST_STATE_PLAYING);
    g_idle_add(idle_func, pipe2);
#ifdef USE_GLIB2
    g_main_loop_run (g_main_loop_new (NULL, FALSE));
#else
    gst_main();
#endif
    gst_element_set_state(pipe2, GST_STATE_NULL);
    
    return 0;
}

                    
