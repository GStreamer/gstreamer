#include <glib.h>
#include <gst/gst.h>
#include <ghttp.h>

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  guint16 mp3type;
  GList *factories;
  GstElementFactory *parsefactory;
  GstElement *bin, *src, *parse, *audiosink;
  GList *padlist;
  ghttp_request *pls;
  guchar *plsbuf;
  gint plsbuflen,parsedlen = 0;
  guchar *url,*local;
  GSList *urls = NULL;

  pls = ghttp_request_new();
  if (argc >= 2)
    ghttp_set_uri(pls,argv[1]);
  else
    ghttp_set_uri(pls,"http://209.127.18.4:9000");
  ghttp_prepare(pls);
  ghttp_process(pls);
  plsbuf = ghttp_get_body(pls);
  plsbuflen = ghttp_get_body_len(pls);

  while (parsedlen < plsbuflen) {
    local = plsbuf + parsedlen;
    if ((*local != '[') && (*local != '\n')) {		/* t/v pair */
      if (!strncmp(local,"File1=",4)) {			/* if file */
        url = strchr(local,'=') + 1;			/* url after = */
        local = strchr(url,'\n');			/* ffwd after = */
        *(local)++ = 0;					/* nullz url */
        g_print("prepending '%s' to list\n",url);
        urls = g_slist_prepend(urls,g_strdup(url));
	/* local should point to next line now */
      } else {
        local = strchr(local,'\n') + 1;			/* skip line */
      }
    } else {
      local = strchr(local,'\n') + 1;			/* skip line */
    }
    /* we can consider that line parsed... */
    parsedlen = local - plsbuf;
  }
  if (urls == NULL) {
    g_print("couldn't find any streams\n");
    exit(1);
  }
  ghttp_request_destroy(pls);

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  src = gst_httpsrc_new("src");
  if (argc == 3) {
    int i;
    for (i=1;i<atoi(argv[2]);i++)
      urls = g_slist_next(urls);
  }
  g_print("loading shoutcast server %s\n",urls->data);
  gtk_object_set(GTK_OBJECT(src),"location",urls->data,NULL);
  g_print("created src\n");

  /* now it's time to get the parser */
  mp3type = gst_type_find_by_mime("audio/mpeg");
  factories = gst_type_get_sinks(mp3type);
  if (factories != NULL)
    parsefactory = GST_ELEMENTFACTORY(factories->data);
  else {
    g_print("sorry, can't find anyone registered to sink 'mp3'\n");
    return 1;
  }
  parse = gst_elementfactory_create(parsefactory,"parser");
  if (parse == NULL) {
    g_print("sorry, couldn't create parser\n");
    return 1;
  }


  audiosink = gst_audiosink_new("audiosink");

  gtk_signal_connect(GTK_OBJECT(src),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(src));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(parse));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(audiosink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(audiosink,"sink"));


  sleep(5); /* to let the network buffer fill a bit */
  while(1) {
    g_print("calling gst_httpsrc_push\n");
    gst_httpsrc_push(GST_SRC(src));
  }

  gst_object_destroy(GST_OBJECT(audiosink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(src));
  gst_object_destroy(GST_OBJECT(bin));
}

