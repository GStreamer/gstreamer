#include <glib.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>

typedef struct _launch_delayed_pad launch_delayed_pad;
struct _launch_delayed_pad {
  gchar *name;
  GstPad *peer;
};

void launch_newpad(GstElement *element,GstPad *pad,launch_delayed_pad *peer) {
  gst_info("have NEW_PAD signal\n");
  // if it matches, connect it
  if (!strcmp(gst_pad_get_name(pad),peer->name)) {
    gst_pad_connect(pad,peer->peer);
    gst_info("delayed connect of '%s' to '%s'\n",
             gst_pad_get_name(pad),gst_pad_get_name(peer->peer));
  }
}

void parse(int argc,char *argv[],GstElement *parent,gint offset,gchar endchar) {
  gint i = offset;
  gchar *plugin;
  GstElement *element = NULL, *prevelement;
  GstPad *prevpad = NULL,*nextpad;
  gchar *prevpadname = NULL,*nextpadname = NULL;
  gchar *ptr;
  gint len;
  launch_delayed_pad *delayed;

  gst_info("at offset %d, argc is %d\n",i,argc);

  // loop through all the arguments
  while (i < argc) {
    // first is the plugin name
    plugin = argv[i++];
    gst_info("plugin is \"%s\"\n",plugin);
    // record previous element
    prevelement = element;
    // create the element and add it to the parent
    element = gst_elementfactory_make(plugin,plugin);
    gst_bin_add(GST_BIN(parent),element);
    // connect it to the previous if there is one
    if (nextpadname != NULL) {
      // grab the pad of this element
      nextpad = gst_element_get_pad(element,nextpadname);
      g_return_if_fail(nextpad != NULL);
      // check to see if the pad exists yet, connect it if it does
      if (prevpad != NULL) {
        gst_pad_connect(prevpad,nextpad);
        gst_info("wired '%s' to '%s'\n",
                 gst_pad_get_name(prevpad),gst_pad_get_name(nextpad));
      }
      // otherwise we have to attach and wait for it to show
      else {
        delayed = g_new0(launch_delayed_pad,1);
        delayed->name = prevpadname;
        delayed->peer = nextpad;
        gtk_signal_connect(GTK_OBJECT(prevelement),"new_pad",
                           launch_newpad,delayed);
      }
    }
     // then come all the other things
    while (i < argc) {
      // snag the length in advance;
      len = strlen(argv[i]);
      // if it's just a connection, pick the 'src' pad and move on
      if ((ptr = strchr(argv[i],'|')) != 0) {
        // if there's a previous pad name
        if (ptr != argv[i]) {
          ptr[0] = '\0';
          prevpadname = argv[i];
          prevpad = gst_element_get_pad(element,prevpadname);
        } else
          prevpad = gst_element_get_pad(element,"src");
        // if there's a next pad name
        if (((ptr - argv[i]) + 1) < len) {
          nextpadname = ptr + 1;
        } else
          nextpadname = "sink";
        i++;
        break;
      } else {
        gst_info("have unknown argument '%s'\n",argv[i]);
        gtk_object_set(GTK_OBJECT(element),"location",argv[i],NULL);
        i++;
      }
    }
  }
}

int main(int argc,char *argv[]) {
  int t;
  GstElement *pipeline;

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  gst_info("\n\n");

  pipeline = gst_elementfactory_make("thread","launch");
  if ((t = atoi(argv[1])))
    parse(argc,argv,pipeline,2,0);
  else
    parse(argc,argv,pipeline,1,0);

  xmlSaveFile("launch.xml",gst_xml_write(pipeline));

  gst_element_set_state(pipeline,GST_STATE_READY);
  gst_element_set_state(pipeline,GST_STATE_PLAYING);

  if (t)
    sleep(t);
  else
    sleep(5);

  return 1;
}
