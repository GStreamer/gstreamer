#include <glib.h>
#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>

guint bincount = 0;
guint threadcount = 0;
gint binlevel = 0;
GHashTable *elementcounts;
gboolean verbose = FALSE;
gboolean debug = FALSE;

#define DEBUG(format,args...) G_STMT_START{			\
  if (debug) {							\
    int ___i;							\
    for (___i=0;___i<binlevel*2;___i++) fprintf(stderr," ");	\
    fprintf(stderr, format , ## args );				\
  }								\
}G_STMT_END
#define DEBUG_NOPREFIX(format,args...) G_STMT_START{ if (debug) fprintf(stderr, format , ## args ); }G_STMT_END
#define VERBOSE(format,args...) G_STMT_START{			\
  if (verbose) {						\
    int ___i;							\
    for (___i=0;___i<binlevel*2;___i++) fprintf(stderr," ");	\
    fprintf(stderr, format , ## args );				\
  }								\
}G_STMT_END

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

gchar *unique_name(gchar *type) {
  gint count;

  count = GPOINTER_TO_INT(g_hash_table_lookup(elementcounts,type));
  count++;
  g_hash_table_insert(elementcounts,type,GINT_TO_POINTER(count));

  return g_strdup_printf("%s%d",type,count-1);
}

// element ! [ element ! (element ! element )] ! element
// 0       1 2 3       4 5        6 7       8  9 10		11
//             0       1 2        3 4       5			8
//                       0        1 2       3			6

gint parse_cmdline(int argc,char *argv[],GstBin *parent) {
  gint i = 0;
  gchar *arg;
  GstElement *element = NULL, *previous = NULL, *prevelement = NULL;
  gchar closingchar;
  gint len;
  gchar *ptr;
  gchar *sinkpadname = NULL, *srcpadname = NULL;
  GstPad *sinkpad = NULL, *srcpad = NULL;
  GList *pads;
  gint elementcount = 0;
  gint retval = 0;

  binlevel++;

  if (GST_IS_PIPELINE(parent)) closingchar = '\0',DEBUG("in pipeline ");
  else if (GST_IS_THREAD(parent)) closingchar = '}',DEBUG("in thread ");
  else closingchar = ')',DEBUG("in bin ");
  DEBUG_NOPREFIX("%s\n",gst_element_get_name (GST_ELEMENT (parent)));

  while (i < argc) {
    arg = argv[i];
    len = strlen(arg);
    element = NULL;
    DEBUG("** ARGUMENT is '%s'\n",arg);

    // a null that slipped through the reconstruction
    if (len == 0) {
      DEBUG("random arg, FIXME\n");
      i++;
      continue;

    // end of the container
    } else if (arg[0] == closingchar) {
      // time to finish off this bin
      DEBUG("exiting container %s\n",gst_element_get_name (GST_ELEMENT (parent)));
      retval = i+1;
      break;

    // a pad connection
    } else if ((ptr = strchr(arg,'!'))) {
      DEBUG("attempting to connect pads together....\n");

      // if it starts with the !
      if (arg[0] == '!') {
        srcpadname = NULL;
        // if there's a sinkpad...
        if (len > 1)
          sinkpadname = &arg[1];
        else
          sinkpadname = NULL;
      } else {
        srcpadname = g_strndup(arg,(ptr-arg));
        // if there's a sinkpad
        if (len > (ptr-arg)+1)
          sinkpadname = &ptr[1];
        else
          sinkpadname = NULL;
      }

      DEBUG("have sinkpad %s, srcpad %s\n",sinkpadname,srcpadname);

      srcpad = NULL;

      // if the srcpadname doesn't have any commas in it, find an actual pad
      if (!srcpadname || !strchr(srcpadname,',')) {
        if (srcpadname != NULL) {
          srcpad = gst_element_get_pad(previous,srcpadname);
          if (!srcpad)
            VERBOSE("NO SUCH pad %s in element %s\n",srcpadname,gst_element_get_name(previous));
        }

        if (srcpad == NULL) {
          // check through the list to find the first sink pad
          pads = gst_element_get_pad_list(previous);
          while (pads) {
            srcpad = GST_PAD(pads->data);
            pads = g_list_next (pads);
            if (gst_pad_get_direction (srcpad) == GST_PAD_SRC) break;
            srcpad = NULL;
          }
        }

        if (!srcpad) DEBUG("error, can't find a src pad!!!\n");
        else DEBUG("have src pad %s:%s\n",GST_DEBUG_PAD_NAME(srcpad));
      }

    // argument with = in it
    } else if (strstr(arg, "=")) {
      gchar * argname;
      gchar * argval;
      gchar * pos = strstr(arg, "=");
      // we have an argument
      argname = g_strndup(arg, pos - arg);
      argval = pos+1;
      DEBUG("attempting to set argument '%s'\n", arg);
      gtk_object_set(GTK_OBJECT(previous),argname,argval,NULL);
      g_free(argname);

    // element or argument, or beginning of bin or thread
    } else {
      DEBUG("have element or bin/thread\n");
      // if we have a bin or thread starting
      if (strchr("({",arg[0])) {
        if (arg[0] == '(') {
          // create a bin and add it to the current parent
          element = gst_bin_new(g_strdup_printf("bin%d",bincount++));
          VERBOSE("CREATED bin %s\n",gst_element_get_name(element));
        } else if (arg[0] == '{') {
          // create a thread and add it to the current parent
          element = gst_thread_new(g_strdup_printf("thread%d",threadcount++));
          VERBOSE("CREATED thread %s\n",gst_element_get_name(element));
        }

        i += parse_cmdline(argc - i, argv + i + 1, GST_BIN (element));

      } else {
	// we have an element
        DEBUG("attempting to create element '%s'\n",arg);
        element = gst_elementfactory_make(arg,unique_name(arg));
        VERBOSE("CREATED element %s\n",gst_element_get_name(element));
        DEBUG("created element %s\n",gst_element_get_name(element));
      }

      gst_bin_add (GST_BIN (parent), element);
      elementcount++;

      if (srcpad != NULL) { 
        DEBUG("need to connect to sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(srcpad));

        sinkpad = NULL;

        if (sinkpadname != NULL)
          sinkpad = gst_element_get_pad(previous,sinkpadname);

        if (!sinkpad) {
          // check through the list to find the first sink pad
          pads = gst_element_get_pad_list(element);
          while (pads) {
            sinkpad = GST_PAD(pads->data);
            pads = g_list_next (pads);
            if (gst_pad_get_direction (sinkpad) == GST_PAD_SINK) break;
            sinkpad = NULL;
          }
        }

        if (!sinkpad) DEBUG("error, can't find a sink pad!!!\n");
        else DEBUG("have sink pad %s:%s\n",GST_DEBUG_PAD_NAME(sinkpad));

        VERBOSE("CONNECTING %s:%s and %s:%s\n",GST_DEBUG_PAD_NAME(srcpad),GST_DEBUG_PAD_NAME(sinkpad));
        gst_pad_connect(srcpad,sinkpad);

        sinkpad = NULL;
        srcpad = NULL;
      }

      // if we're the first element, ghost all the sinkpads
      if (elementcount == 1) {
        DEBUG("first element, ghosting all of %s's sink pads to parent %s\n",
              gst_element_get_name(element),gst_element_get_name(GST_ELEMENT(parent)));
        pads = gst_element_get_pad_list (element);
        while (pads) {
          sinkpad = GST_PAD (pads->data);
          pads = g_list_next (pads);
          if (!sinkpad) DEBUG("much oddness, pad doesn't seem to exist\n");
          else if (gst_pad_get_direction (sinkpad) == GST_PAD_SINK) {
            gst_element_add_ghost_pad (GST_ELEMENT (parent), sinkpad);
            DEBUG("ghosted %s:%s\n",GST_DEBUG_PAD_NAME(sinkpad));
          }
        }
      }
    }

    i++;
    previous = element;
    if (!GST_IS_BIN(element)) prevelement = element;
  }

  // ghost all the src pads of the bin
  if (prevelement != NULL) {
    DEBUG("last element, ghosting all of %s's src pads to parent %s\n",
          gst_element_get_name(prevelement),gst_element_get_name(GST_ELEMENT(parent)));
    pads = gst_element_get_pad_list (prevelement);
    while (pads) {
      srcpad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!srcpad) DEBUG("much oddness, pad doesn't seem to exist\n");
      else if (gst_pad_get_direction (srcpad) == GST_PAD_SRC) {
        gst_element_add_ghost_pad (GST_ELEMENT (parent), srcpad);
        DEBUG("ghosted %s:%s\n",GST_DEBUG_PAD_NAME(srcpad));
      }
    }
  }

  binlevel--;

  if (retval) return retval;

  if (closingchar != '\0')
    DEBUG("returning IN THE WRONG PLACE\n");
  else DEBUG("ending pipeline\n");
  return i+1;
}

gint parse(int argc,char *argv[],GstBin *parent) {
  char **argvn;
  gchar *cmdline;
  gint newargc;
  gint len;
  int i,j,k;

  // make a null-terminated version of argv
  argvn = g_new0(char *,argc+1);
  memcpy(argvn,argv,sizeof(char*)*argc);
  // join the argvs together
  cmdline = g_strjoinv(" ",argvn);
  // free the null-terminated argv
  g_free(argvn);

  // first walk through quickly and see how many more slots we need
  len = strlen(cmdline);
  newargc = 1;
  for (i=0;i<len;i++) {
    // if it's a space, it denotes a new arg
    if (cmdline[i] == ' ') newargc++;
    // if it's a brace and isn't followed by a space, give it an arg
    if (strchr("([{}])",cmdline[i])) {
      // not followed by space, gets one
      if (cmdline[i+1] != ' ') newargc++;
    }
  }

  // now allocate the new argv array
  argvn = g_new0(char *,newargc+1);
  DEBUG("supposed to have %d args\n",newargc);

  // now attempt to construct the new arg list
  j = 0;k = 0;
  for (i=0;i<len+1;i++) {
    // if it's a delimiter
    if (strchr("([{}]) ",cmdline[i]) || (cmdline[i] == '\0')) {
      // extract the previous arg
      if (i-k > 0) {
        if (cmdline[k] == ' ') k++;
        argvn[j] = g_new0(char,(i-k)+1);
        memcpy(argvn[j],&cmdline[k],i-k);

        // catch misparses
        if (strlen(argvn[j]) > 0) j++;
      }
      k = i;

      // if this is a bracket, construct a word
      if ((cmdline[i] != ' ') && (cmdline[i] != '\0')) {
        argvn[j++] = g_strdup_printf("%c",cmdline[i]);
        k++;
      }
    }
  }

  // print them out
  for (i=0;i<newargc;i++) {
    DEBUG("arg %d is: %s\n",i,argvn[i]);
  }

  // set up the elementcounts hash
  elementcounts = g_hash_table_new(g_str_hash,g_str_equal);

  return parse_cmdline(newargc,argvn,parent);
}

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  int firstarg;
  guint i;

  gst_init(&argc,&argv);

  firstarg = 1;
  while ((argv[firstarg][0] == '-') && (argv[firstarg][1] == '-')) {
    if (strcmp(&argv[firstarg][2],"verbose") == 0)
      verbose = TRUE;
    else if (strcmp(&argv[firstarg][2],"debug") == 0)
      debug = TRUE;
    firstarg++;
  }

  pipeline = gst_pipeline_new("launch");

  VERBOSE("CREATED pipeline %s\n",gst_element_get_name(pipeline));
  parse(argc - firstarg,argv + firstarg,GST_BIN (pipeline));

  VERBOSE("RUNNING pipeline\n");
  gst_element_set_state(pipeline,GST_STATE_PLAYING);

  while(1)
    gst_bin_iterate (GST_BIN (pipeline));

  fprintf(stderr,"\n");

  return 0;
}
