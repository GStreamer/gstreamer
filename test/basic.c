#include <glib.h>
#include <gst/gst.h>

void added_child(GstObject *object,GstObject *child) {
  g_print("added_child(): added child '%s' to '%s'\n",
          gst_object_get_name(child),
          gst_object_get_name(object));
}

void added_parent(GstObject *object,GstObject *parent) {
  g_print("added_parent(): object '%s' has new parent '%s'\n",
          gst_object_get_name(object),
          gst_object_get_name(parent));
}

void list_pads(GstElement *element) {
  GList *padlist;

  padlist = gst_element_get_pad_list(element);
  if (padlist == NULL)
    g_print("%s has no pads...\n",gst_object_get_name(element));
  else {
    while (padlist) {
      GstPad *pad = GST_PAD(padlist->data);
      if (gst_pad_get_ghost_parent(pad) == GST_OBJECT(element))
        g_print("'%s' had %s ghost pad '%s'\n",gst_object_get_name(element),
                (gst_pad_get_direction(pad) == GST_PAD_SRC) ? "SRC" : "SINK",
                gst_pad_get_name(pad));
      else
        g_print("'%s' had %s pad '%s'\n",gst_object_get_name(element),
                (gst_pad_get_direction(pad) == GST_PAD_SRC) ? "SRC" : "SINK",
                gst_pad_get_name(pad));
      padlist = g_list_next(padlist);
    }
  }
}

int main(int argc,char *argv[]) {
  GstElement *bin, *src, *binf, *filter1, *filter2, *sink;
  GList *padlist;

  gtk_init(&argc,&argv);

  bin = gst_bin_new("bin");

  src = gst_disksrc_new("fakesrc");
  gst_disksrc_set_filename(src,"demo.mp3");
  list_pads(src);

  binf = gst_bin_new("binf");

  filter1 = gst_fakefilter_new("filter1");
  list_pads(filter1);

  filter2 = gst_fakefilter_new("filter2");
  list_pads(filter2);

  sink = gst_fakesink_new("fakesink");
  list_pads(sink);

  gtk_signal_connect(GTK_OBJECT(bin),"object_added",
                     GTK_SIGNAL_FUNC(added_child),NULL);
  gtk_signal_connect(GTK_OBJECT(binf),"object_added",
                     GTK_SIGNAL_FUNC(added_child),NULL);

  gtk_signal_connect(GTK_OBJECT(binf),"parent_set",
                     GTK_SIGNAL_FUNC(added_parent),NULL);
  gtk_signal_connect(GTK_OBJECT(src),"parent_set",
                     GTK_SIGNAL_FUNC(added_parent),NULL);
  gtk_signal_connect(GTK_OBJECT(filter1),"parent_set",
                     GTK_SIGNAL_FUNC(added_parent),NULL);
  gtk_signal_connect(GTK_OBJECT(filter2),"parent_set",
                     GTK_SIGNAL_FUNC(added_parent),NULL);
  gtk_signal_connect(GTK_OBJECT(sink),"parent_set",
                     GTK_SIGNAL_FUNC(added_parent),NULL);

  /* add filter1 to the subbin */
  gst_bin_add(GST_BIN(binf),GST_ELEMENT(filter1));
  gst_bin_add(GST_BIN(binf),GST_ELEMENT(filter2));
  /* connect the two together */
  gst_pad_connect(gst_element_get_pad(filter1,"src"),
                  gst_element_get_pad(filter2,"sink"));
  /* export the pads */
  gst_element_add_ghost_pad(binf,gst_element_get_pad(filter1,"sink"));
  gst_element_add_ghost_pad(binf,gst_element_get_pad(filter2,"src"));
  list_pads(binf);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(src));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(binf));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(sink));

  /* connect src to binf */
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(binf,"sink"));
  /* connect binf to sink */
  gst_pad_connect(gst_element_get_pad(binf,"src"),
                  gst_element_get_pad(sink,"sink"));

  gst_disksrc_push(GST_SRC(src));

  gst_object_destroy(GST_OBJECT(src));
  gst_object_destroy(GST_OBJECT(filter1));
  gst_object_destroy(GST_OBJECT(filter2));
  gst_object_destroy(GST_OBJECT(binf));
  gst_object_destroy(GST_OBJECT(sink));
  gst_object_destroy(GST_OBJECT(bin));
}

