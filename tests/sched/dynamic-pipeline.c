#include <gst/gst.h>

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
    
    // make the first pipeline
    gst_bin_add (GST_BIN(pipe1), filesrc);
    gst_bin_add (GST_BIN(pipe1), fakesink);
    gst_element_connect(filesrc, "src", fakesink, "sink");
    
    // initialize cothreads
    gst_element_set_state(pipe1, GST_STATE_PLAYING);
    gst_element_set_state(pipe1, GST_STATE_READY);
    
    // destroy the fakesink, but keep filesrc (its state is GST_STATE_READY)
    gst_element_disconnect(filesrc, "src", fakesink, "sink");
    gst_object_ref(GST_OBJECT(filesrc));
    gst_bin_remove(GST_BIN(pipe1), filesrc);
    gst_bin_remove(GST_BIN(pipe1), fakesink);
    
    // make a new pipeline
    gst_bin_add (GST_BIN(pipe2), mad);
    gst_bin_add (GST_BIN(pipe2), osssink);
    gst_element_connect(mad, "src", osssink, "sink");
    
    // change the new pipeline's state to READY (is this necessary?)
    gst_element_set_state(pipe2, GST_STATE_READY);
    gst_bin_add (GST_BIN(pipe2), filesrc);
    gst_element_connect(filesrc, "src", mad, "sink");
    
    // show the pipeline state
    xmlDocDump(stdout, gst_xml_write(pipe2));
    
    // try to iterate the pipeline
    gst_element_set_state(pipe2, GST_STATE_PLAYING);
    gst_bin_iterate(GST_BIN(pipe2));
    gst_element_set_state(pipe2, GST_STATE_NULL);
    
    return 0;
}

                    
