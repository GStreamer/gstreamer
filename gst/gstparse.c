/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * :
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#define DEBUG(format,args...)
#define DEBUG_NOPREFIX(format,args...)
#define VERBOSE(format,args...)

#define GST_PARSE_LISTPAD(list)   ((GstPad*)(list->data))

#include <string.h>

#include "gst_private.h"
#include "gstparse.h"
#include "gstpipeline.h"
#include "gstthread.h"
#include "gstutils.h"

typedef struct _gst_parse_priv gst_parse_priv;
struct _gst_parse_priv
{
  guint bincount;
  guint threadcount;
  gint binlevel;
  GHashTable *elementcounts;
  gboolean verbose;
  gboolean debug;
};

typedef struct _gst_parse_delayed_pad gst_parse_delayed_pad;
struct _gst_parse_delayed_pad
{
  gchar *name;
  GstPad *peer;
};

typedef struct
{
  gchar *srcpadname;
  GstPad *target;
  GstElement *pipeline;
}
dyn_connect;

static void
dynamic_connect (GstElement * element, GstPad * newpad, gpointer data)
{
  dyn_connect *connect = (dyn_connect *) data;

  if (!strcmp (gst_pad_get_name (newpad), connect->srcpadname)) {
    gst_element_set_state (connect->pipeline, GST_STATE_PAUSED);
    gst_pad_connect (newpad, connect->target);
    gst_element_set_state (connect->pipeline, GST_STATE_PLAYING);
  }
}

static gchar *
gst_parse_unique_name (gchar * type, gst_parse_priv * priv)
{
  gpointer tmp;
  gint count;

  tmp = g_hash_table_lookup (priv->elementcounts, type);
  count = GPOINTER_TO_INT (tmp);
  count++;
  g_hash_table_insert (priv->elementcounts, type, GINT_TO_POINTER (count));

  return g_strdup_printf ("%s%d", type, count - 1);
}



static gint
gst_parse_launch_cmdline (int argc, char *argv[], GstBin * parent, gst_parse_priv * priv)
{
  gint i = 0, j = 0;
  gchar *arg;
  GstElement *element = NULL, *previous = NULL, *prevelement = NULL;
  gchar closingchar = '\0';
  gint len;
  gchar *ptr;
  gchar *sinkpadname = NULL, *srcpadname = NULL, *tempname;
  GstPad *temppad;
  GSList *sinkpads = NULL, *srcpads = NULL;
  gint numsrcpads = 0, numsinkpads = 0;
  GList *pads;
  gint elementcount = 0;
  gint retval = 0;
  gboolean backref = FALSE;

  priv->binlevel++;

  if (GST_IS_PIPELINE (parent)) {
    closingchar = '\0';
    DEBUG ("in pipeline ");
  }
  else if (GST_IS_THREAD (parent)) {
    closingchar = '}';
    DEBUG ("in thread ");
  }
  else {
    closingchar = ')';
    DEBUG ("in bin ");
  }
  DEBUG_NOPREFIX ("%s\n", GST_ELEMENT_NAME (GST_ELEMENT (parent)));

  while (i < argc) {
    arg = argv[i];
    /* FIXME this is a lame solution for problems with the first parser */
    if (arg == NULL) {
      i++;
      continue;
    }
    len = strlen (arg);
    element = NULL;
    DEBUG ("** ARGUMENT is '%s'\n", arg);

    /* a null that slipped through the reconstruction */
    if (len == 0) {
      DEBUG ("random arg, FIXME\n");
      i++;
      continue;

      /* end of the container */
    }
    else if (arg[0] == closingchar) {
      /* time to finish off this bin */
      DEBUG ("exiting container %s\n", GST_ELEMENT_NAME (GST_ELEMENT (parent)));
      retval = i + 1;
      break;

      /* a pad connection */
    }
    else if ((ptr = strchr (arg, '!'))) {
      DEBUG ("attempting to connect pads together....\n");

      /* if it starts with the ! */
      if (arg[0] == '!') {
	srcpadname = NULL;
	/* if there's a sinkpad... */
	if (len > 1)
	  sinkpadname = &arg[1];
	else
	  sinkpadname = NULL;
      }
      else {
	srcpadname = g_strndup (arg, (ptr - arg));
	/* if there's a sinkpad */
	if (len > (ptr - arg) + 1)
	  sinkpadname = &ptr[1];
	else
	  sinkpadname = NULL;
      }

      if (srcpadname && (ptr = strchr (srcpadname, '.'))) {
	gchar *element_name = g_strndup (arg, (ptr - srcpadname));
	GstElement *new;

	GST_DEBUG (0, "have pad for element %s\n", element_name);
	new = gst_bin_get_by_name_recurse_up (parent, element_name);
	if (!new) {
          GST_DEBUG (0, "element %s does not exist! trying to continue\n", element_name);
	}
	else {
          previous = new;
	  srcpadname = ptr + 1;
	  backref = TRUE;
	}
      }
      
      GST_DEBUG (0, "have srcpad %s, sinkpad %s\n", srcpadname, sinkpadname);

      g_slist_free (srcpads);
      srcpads = NULL;
      numsrcpads = 0;
      tempname = NULL;

      /* find src pads */
      if (srcpadname != NULL) {
	while (1) {
	  /* split name at commas */
	  if ((ptr = strchr (srcpadname, ','))) {
	    tempname = g_strndup (srcpadname, (ptr - srcpadname));
	    srcpadname = &ptr[1];
	  }
	  else {
	    tempname = srcpadname;
	  }

	  /* look for pad with that name */
	  if ((temppad = gst_element_get_pad (previous, tempname))) {
	    srcpads = g_slist_append (srcpads, temppad);
	    numsrcpads++;
	  }

	  /* try to create a pad using that padtemplate name */
	  else if ((temppad = gst_element_request_pad_by_name (previous, tempname))) {
	    srcpads = g_slist_append (srcpads, temppad);
	    numsrcpads++;
	  }
	  if (!temppad) {
	    GST_DEBUG (0, "NO SUCH pad %s in element %s\n", tempname, GST_ELEMENT_NAME (previous));
	  }
	  else {
	    GST_DEBUG (0, "have src pad %s:%s\n", GST_DEBUG_PAD_NAME (temppad));
	  }

	  /* if there is no more commas in srcpadname then we're done */
	  if (tempname == srcpadname)
	    break;
	  g_free (tempname);
	}
      }
      else {
	/* check through the list to find the first sink pad */
	GST_DEBUG (0, "CHECKING through element %s for pad named %s\n", GST_ELEMENT_NAME (previous),
		   srcpadname);
	pads = gst_element_get_pad_list (previous);
	while (pads) {
	  temppad = GST_PARSE_LISTPAD (pads);
	  GST_DEBUG (0, "have pad %s:%s\n", GST_DEBUG_PAD_NAME (temppad));
	  if (GST_IS_GHOST_PAD (temppad))
	    GST_DEBUG (0, "it's a ghost pad\n");
	  if (gst_pad_get_direction (temppad) == GST_PAD_SRC) {
	    srcpads = g_slist_append (srcpads, temppad);
	    numsrcpads++;
	    break;
	  }
	  pads = g_list_next (pads);
	}
	if (!srcpads)
	  GST_DEBUG (0, "error, can't find a src pad!!!\n");
	else
	  GST_DEBUG (0, "have src pad %s:%s\n", GST_DEBUG_PAD_NAME (GST_PARSE_LISTPAD (srcpads)));
      }

      /* argument with = in it */
    }
    else if (strstr (arg, "=")) {
      gchar *argname;
      gchar *argval;
      gchar *pos = strstr (arg, "=");

      /* we have an argument */
      argname = arg;
      pos[0] = '\0';
      argval = pos + 1;

      GST_DEBUG (0, "attempting to set argument '%s' to '%s' on element '%s'\n",
		 argname, argval, GST_ELEMENT_NAME (previous));
      gst_util_set_object_arg (G_OBJECT (previous), argname, argval);
      g_free (argname);

      /* element or argument, or beginning of bin or thread */
    }
    else if (arg[0] == '[') {
      /* we have the start of a name of the preceding element. */
      /* rename previous element to next arg. */
      if (arg[1] != '\0') {
	fprintf (stderr, "error, unexpected junk after [\n");
	return GST_PARSE_ERROR_SYNTAX;
      }
      i++;
      if (i < argc) {
	gst_element_set_name (previous, argv[i]);
      }
      else {
	fprintf (stderr, "error, expected element name, found end of arguments\n");
	return GST_PARSE_ERROR_SYNTAX;
      }
      i++;
      if (i >= argc) {
	fprintf (stderr, "error, expected ], found end of arguments\n");
	return GST_PARSE_ERROR_SYNTAX;
      }
      else if (strcmp (argv[i], "]") != 0) {
	fprintf (stderr, "error, expected ], found '%s'\n", argv[i]);
	return GST_PARSE_ERROR_SYNTAX;
      }
    }
    else {
      DEBUG ("have element or bin/thread\n");
      /* if we have a bin or thread starting */
      if (strchr ("({", arg[0])) {
	if (arg[0] == '(') {
	  /* create a bin and add it to the current parent */
	  element = gst_bin_new (g_strdup_printf ("bin%d", priv->bincount++));
	  if (!element) {
	    fprintf (stderr, "Couldn't create a bin!\n");
	    return GST_PARSE_ERROR_CREATING_ELEMENT;
	  }
	  GST_DEBUG (0, "CREATED bin %s\n", GST_ELEMENT_NAME (element));
	}
	else if (arg[0] == '{') {
	  /* create a thread and add it to the current parent */
	  element = gst_thread_new (g_strdup_printf ("thread%d", priv->threadcount++));
	  if (!element) {
	    fprintf (stderr, "Couldn't create a thread!\n");
	    return GST_PARSE_ERROR_CREATING_ELEMENT;
	  }
	  GST_DEBUG (0, "CREATED thread %s\n", GST_ELEMENT_NAME (element));
	}
	else {
	  DEBUG ("error in parser, unexpected symbol, FIXME\n");
	  i++;
	  continue;
	}
        gst_bin_add (GST_BIN (parent), element);

	j = gst_parse_launch_cmdline (argc - i, argv + i + 1, GST_BIN (element), priv);
	/* check for parse error */
	if (j < 0)
	  return j;
	i += j;

      }
      else {
	/* we have an element */
	DEBUG ("attempting to create element '%s'\n", arg);
	ptr = gst_parse_unique_name (arg, priv);
	element = gst_elementfactory_make (arg, ptr);
	g_free (ptr);
	if (!element) {
#ifndef GST_DISABLE_REGISTRY
	  fprintf (stderr,
		   "Couldn't create a '%s', no such element or need to run gst-register?\n",
		   arg);
#else
	  fprintf (stderr, "Couldn't create a '%s', no such element or need to load pluginn?\n",
		   arg);
#endif
	  return GST_PARSE_ERROR_NOSUCH_ELEMENT;
	}
	GST_DEBUG (0, "CREATED element %s\n", GST_ELEMENT_NAME (element));
        gst_bin_add (GST_BIN (parent), element);
      }

      elementcount++;

      g_slist_free (sinkpads);
      sinkpads = NULL;
      numsinkpads = 0;
      tempname = NULL;

      /* find sink pads */
      if (sinkpadname != NULL) {
	while (1) {
	  /* split name at commas */
	  if ((ptr = strchr (sinkpadname, ','))) {
	    tempname = g_strndup (sinkpadname, (ptr - sinkpadname));
	    sinkpadname = &ptr[1];
	  }
	  else {
	    tempname = sinkpadname;
	  }

	  /* look for pad with that name */
	  if ((temppad = gst_element_get_pad (element, tempname))) {
	    sinkpads = g_slist_append (sinkpads, temppad);
	    numsinkpads++;
	  }

	  /* try to create a pad using that padtemplate name */
	  else if ((temppad = gst_element_request_pad_by_name (element, tempname))) {
	    sinkpads = g_slist_append (sinkpads, temppad);
	    numsinkpads++;
	  }
	  if (!temppad) {
	    GST_DEBUG (0, "NO SUCH pad %s in element %s\n", tempname, GST_ELEMENT_NAME (element));
	  }
	  else {
	    GST_DEBUG (0, "have sink pad %s:%s\n", GST_DEBUG_PAD_NAME (temppad));
	  }

	  /* if there is no more commas in sinkpadname then we're done */
	  if (tempname == sinkpadname)
	    break;
	  g_free (tempname);
	}
      }
      else {
	/* check through the list to find the first sink pad */
	pads = gst_element_get_pad_list (element);
	while (pads) {
	  temppad = GST_PAD (pads->data);
	  pads = g_list_next (pads);
	  if (gst_pad_get_direction (temppad) == GST_PAD_SINK) {
	    sinkpads = g_slist_append (sinkpads, temppad);
	    numsinkpads++;
	    break;
	  }
	}
      }

      if (!sinkpads)
	GST_DEBUG (0, "can't find a sink pad for element\n");
      else
	GST_DEBUG (0, "have sink pad %s:%s\n", GST_DEBUG_PAD_NAME (GST_PARSE_LISTPAD (sinkpads)));

      if (!srcpads && sinkpads && previous && srcpadname) {
	dyn_connect *connect = g_malloc (sizeof (dyn_connect));

	connect->srcpadname = srcpadname;
	connect->target = GST_PARSE_LISTPAD (sinkpads);
	connect->pipeline = GST_ELEMENT (parent);

	GST_DEBUG (0, "SETTING UP dynamic connection %s:%s and %s:%s\n",
		   gst_element_get_name (previous),
		   srcpadname, GST_DEBUG_PAD_NAME (GST_PARSE_LISTPAD (sinkpads)));

	g_signal_connect (G_OBJECT (previous), "new_pad", G_CALLBACK (dynamic_connect), connect);
      }
      else {
	for (j = 0; (j < numsrcpads) && (j < numsinkpads); j++) {
	  GST_DEBUG (0, "CONNECTING %s:%s and %s:%s\n",
		     GST_DEBUG_PAD_NAME (GST_PARSE_LISTPAD (g_slist_nth (srcpads, j))),
		     GST_DEBUG_PAD_NAME (GST_PARSE_LISTPAD (g_slist_nth (sinkpads, j))));
	  gst_pad_connect (GST_PARSE_LISTPAD (g_slist_nth (srcpads, j)),
			   GST_PARSE_LISTPAD (g_slist_nth (sinkpads, j)));
	}
      }

      g_slist_free (srcpads);
      srcpads = NULL;
      
      g_slist_free (sinkpads);
      sinkpads = NULL;

      /* if we're the first element, ghost all the sinkpads */
      if (elementcount == 1 && !backref) {
	DEBUG ("first element, ghosting all of %s's sink pads to parent %s\n",
	       GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (GST_ELEMENT (parent)));
	pads = gst_element_get_pad_list (element);
	while (pads) {
	  temppad = GST_PAD (pads->data);
	  pads = g_list_next (pads);
	  if (!temppad)
	    DEBUG ("much oddness, pad doesn't seem to exist\n");
	  else if (gst_pad_get_direction (temppad) == GST_PAD_SINK) {
	    gst_element_add_ghost_pad (GST_ELEMENT (parent), temppad,
				       g_strdup_printf ("%s-ghost", GST_PAD_NAME (temppad)));
	    GST_DEBUG (0, "GHOSTED %s:%s to %s as %s-ghost\n",
		       GST_DEBUG_PAD_NAME (temppad), GST_ELEMENT_NAME (GST_ELEMENT (parent)),
		       GST_PAD_NAME (temppad));
	  }
	}
      }

      previous = element;
      if (!GST_IS_BIN (element))
	prevelement = element;
    }

    i++;
  }

  /* ghost all the src pads of the bin */
  if (prevelement != NULL) {
    DEBUG ("last element, ghosting all of %s's src pads to parent %s\n",
	   GST_ELEMENT_NAME (prevelement), GST_ELEMENT_NAME (GST_ELEMENT (parent)));
    pads = gst_element_get_pad_list (prevelement);
    while (pads) {
      temppad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!temppad)
	DEBUG ("much oddness, pad doesn't seem to exist\n");
      else if (gst_pad_get_direction (temppad) == GST_PAD_SRC) {
	gst_element_add_ghost_pad (GST_ELEMENT (parent), temppad,
				   g_strdup_printf ("%s-ghost", GST_PAD_NAME (temppad)));
	GST_DEBUG (0, "GHOSTED %s:%s to %s as %s-ghost\n",
		   GST_DEBUG_PAD_NAME (temppad), GST_ELEMENT_NAME (parent), GST_PAD_NAME (temppad));
      }
    }
  }

  priv->binlevel--;

  if (retval)
    return retval;

  DEBUG (closingchar != '\0' ? "returning IN THE WRONG PLACE\n" : "ending pipeline\n");

  return i + 1;
}

/**
 * gst_parse_launch:
 * @cmdline: the command line describing the pipeline
 * @parent: the parent bin for the resulting pipeline
 *
 * Create a new pipeline based on command line syntax.
 *
 * Returns: ?
 */
gint
gst_parse_launch (const gchar * cmdline, GstBin * parent)
{
  gst_parse_priv priv;
  gchar **argvn;
  gint newargc;
  gint i;
  const gchar *cp, *start, *end;
  gchar *temp;
  GSList *string_list = NULL, *slist;

  priv.bincount = 0;
  priv.threadcount = 0;
  priv.binlevel = 0;
  priv.elementcounts = NULL;
  priv.verbose = FALSE;
  priv.debug = FALSE;

  end = cmdline + strlen (cmdline);
  newargc = 0;

  temp = "";

  /* Extract the arguments to a gslist in reverse order */
  for (cp = cmdline; cp < end;) {
    i = strcspn (cp, "([{}]) \"\\");

    if (i > 0) {
      temp = g_strconcat (temp, g_strndup (cp, i), NULL);

      /* see if we have an escape char */
      if (cp[i] != '\\') {
	/* normal argument - copy and add to the list */
	string_list = g_slist_prepend (string_list, temp);
	newargc++;
	temp = "";
      }
      else {
	temp = g_strconcat (temp, g_strndup (&cp[++i], 1), NULL);
      }
      cp += i;
    }

    /* skip spaces */
    while (cp < end && *cp == ' ') {
      cp++;
    }

    /* handle quoted arguments */
    if (*cp == '"') {
      start = ++cp;

      /* find matching quote */
      while (cp < end && *cp != '"')
	cp++;

      /* make sure we got it */
      if (cp == end) {
	g_warning ("gst_parse_launch: Unbalanced quote in command line");
	/* FIXME: The list leaks here */
	return 0;
      }

      /* copy the string sans quotes */
      string_list = g_slist_prepend (string_list, g_strndup (start, cp - start));
      newargc++;
      cp += 2;			/* skip the quote aswell */
    }

    /* brackets exist in a separate argument slot */
    if (*cp && strchr ("([{}])", *cp)) {
      string_list = g_slist_prepend (string_list, g_strndup (cp, 1));
      newargc++;
      cp++;
    }
  }

  /* now allocate the new argv array */
  argvn = g_new0 (char *, newargc);

  GST_DEBUG (0, "got %d args\n", newargc);

  /* reverse the list and put the strings in the new array */
  i = newargc;

  for (slist = string_list; slist; slist = slist->next)
    argvn[--i] = slist->data;

  g_slist_free (string_list);

  /* print them out */
  for (i = 0; i < newargc; i++) {
    GST_DEBUG (0, "arg %d is: %s\n", i, argvn[i]);
  }

  /* set up the elementcounts hash */
  priv.elementcounts = g_hash_table_new (g_str_hash, g_str_equal);

  /* do it! */
  i = gst_parse_launch_cmdline (newargc, argvn, parent, &priv);

/*  GST_DEBUG(0, "Finished - freeing temporary argument array"); */
/*  g_strfreev(argvn); */

  return i;
}
