#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <locale.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>


typedef struct
{
  gchar *name;
  GSList *srcpads;
  GSList *sinkpads;
  GSList *srcpadtemplates;
  GSList *sinkpadtemplates;
  GSList *arguments;
} comp_element;

enum
{
  ARG_INT,
  ARG_FILENAME,
  ARG_ENUM
};

typedef struct
{
  gchar *name;
  int type;
  GSList *enums;
} comp_argument;

typedef struct
{
  gint value;
  gchar *nick;
} enum_value;


void
print_match_list (gchar * prefix, int len, GSList * wordlist)
{
  GSList *words = wordlist;

  while (words) {
    if (!len || !strncmp ((gchar *) (words->data), prefix, len))
      printf ("%s\n", (gchar *) (words->data));
    words = g_slist_next (words);
  }
}

int
match_element (comp_element * element, gchar * name)
{
  return strcmp (element->name, name);
}

int
main (int argc, char *argv[])
{
  xmlDocPtr doc;
  xmlNodePtr rootnode, elementnode, propnode, argnode;
  GList *element_list = NULL;
  comp_element *element;
  GSList *element_names = NULL;
  comp_argument *argument;
  enum_value *option;

  gchar *prev_word;
  gchar *partial_word;
  int partial_len;
  GList *elements;
  GSList *pads;
  int num_pads;
  GSList *args;
  gchar *word;
  GSList *words = NULL;

  struct stat stat_buf;

  setlocale (LC_ALL, "");

  if (argc < 4) {
    fprintf (stderr, "gst-complete called with invalid arguments\n");
    exit (1);
  }

  prev_word = argv[3];
  partial_word = argv[2];

  partial_len = strlen (partial_word);

  /***** Loading the completion information from the registry *****/

  if (stat (GST_CACHE_DIR "/compreg.xml", &stat_buf) == 0) {
    doc = xmlParseFile (GST_CACHE_DIR "/compreg.xml");
  } else {
    exit (1);
  }
  rootnode = doc->xmlRootNode;

  elementnode = rootnode->xmlChildrenNode;
  while (elementnode) {
    if (!strcmp (elementnode->name, "element")) {
      element = g_new0 (comp_element, 1);
      propnode = elementnode->xmlChildrenNode;
      while (propnode) {

	if (!strcmp (propnode->name, "name")) {
	  element->name = xmlNodeGetContent (propnode);
/* fprintf(stderr,element->name); */
	} else if (!strcmp (propnode->name, "srcpad")) {
	  element->srcpads =
	      g_slist_prepend (element->srcpads, xmlNodeGetContent (propnode));
/* fprintf(stderr,"."); */
	} else if (!strcmp (propnode->name, "sinkpad")) {
	  element->sinkpads =
	      g_slist_prepend (element->sinkpads, xmlNodeGetContent (propnode));
	} else if (!strcmp (propnode->name, "srcpadtemplate")) {
	  element->srcpadtemplates =
	      g_slist_prepend (element->srcpadtemplates,
	      xmlNodeGetContent (propnode));
/* fprintf(stderr,"."); */
	} else if (!strcmp (propnode->name, "sinkpad")) {
	  element->sinkpadtemplates =
	      g_slist_prepend (element->sinkpadtemplates,
	      xmlNodeGetContent (propnode));
	} else if (!strcmp (propnode->name, "argument")) {
	  argument = g_new0 (comp_argument, 1);
	  argument->name = xmlNodeGetContent (propnode);
	  argument->type = ARG_INT;

	  /* walk through the values data */
	  argnode = propnode->xmlChildrenNode;
	  while (argnode) {
	    if (!strcmp (argnode->name, "filename")) {
	      argument->type = ARG_FILENAME;
	    } else if (!strcmp (argnode->name, "option")) {
	      argument->type = ARG_ENUM;
	      option = g_new0 (enum_value, 1);
	      sscanf (xmlNodeGetContent (argnode), "%d", &option->value);
	      argument->enums = g_slist_prepend (argument->enums, option);
	    }
	    argnode = argnode->next;
	  }

	  element->arguments = g_slist_prepend (element->arguments, argument);
	}

	propnode = propnode->next;
      }
      element_list = g_list_prepend (element_list, element);
      element_names = g_slist_prepend (element_names, element->name);
    }
    elementnode = elementnode->next;
  }



  /***** Completion *****/

  /* The bulk of the work is in deciding exactly which words are an option. */

  /* if we're right at the beginning, with -launch in the first word */
  if (strstr (prev_word, "-launch")) {
    /* print out only elements with no sink pad or padtemplate */
    elements = element_list;
    while (elements) {
      element = (comp_element *) (elements->data);
      if (!element->sinkpads && !element->sinkpadtemplates)
	words = g_slist_prepend (words, element->name);
      elements = g_list_next (elements);
    }
  }

  /* if the previous word is a connection */
  if (strchr (prev_word, '!')) {
    /* print out oly elements with a sink pad or template */
    elements = element_list;
    while (elements) {
      element = (comp_element *) (elements->data);
      if (element->sinkpads || element->sinkpadtemplates)
	words = g_slist_prepend (words, element->name);
      elements = g_list_next (elements);
    }
  }

  /* if the partial word is an argument, and it's an enum */
  if (strchr (prev_word, '=')) {
    fprintf (stderr, "it's an arg, but dunno what element yet\n");
  }

  /* if the previous word is an element, we need to list both pads and arguments */
  if ((elements =
	  g_list_find_custom (element_list, prev_word,
	      (GCompareFunc) match_element))) {
    element = elements->data;
    /* zero the numpads list so we can count them */
    num_pads = 0;

    /* pads */
    pads = element->srcpads;
    while (pads) {
      num_pads++;
      words =
	  g_slist_prepend (words, g_strdup_printf ("%s!",
	      (gchar *) (pads->data)));
      pads = g_slist_next (pads);
    }

    /* padtemplates */
    pads = element->srcpadtemplates;
    while (pads) {
      num_pads++;
      word = g_strdup_printf ("%s!", (gchar *) (pads->data));
      if (!g_slist_find_custom (words, word, (GCompareFunc) strcmp))
	words = g_slist_prepend (words, word);
      pads = g_slist_next (pads);
    }

    /* if there is only one pad, add '!' to the list of completions */
    if (num_pads == 1) {
      words = g_slist_prepend (words, "!");
    }

    /* arguments */
    args = element->arguments;
    while (args) {
      argument = (comp_argument *) (args->data);
      word = strstr (argument->name, "::") + 2;
      words = g_slist_prepend (words, g_strdup_printf ("%s=", word));
      words = g_slist_prepend (words, g_strdup_printf ("%s=...", word));
      args = g_slist_next (args);
    }
  }


  /* The easy part is ouptuting the correct list of possibilities. */
  print_match_list (partial_word, partial_len, words);

  return 0;
}
