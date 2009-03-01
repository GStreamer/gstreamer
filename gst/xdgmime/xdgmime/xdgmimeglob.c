/* -*- mode: C; c-file-style: "gnu" -*- */
/* xdgmimeglob.c: Private file.  Datastructure for storing the globs.
 *
 * More info can be found at http://www.freedesktop.org/standards/
 *
 * Copyright (C) 2003  Red Hat, Inc.
 * Copyright (C) 2003  Jonathan Blandford <jrb@alum.mit.edu>
 *
 * Licensed under the Academic Free License version 2.0
 * Or under the following terms:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xdgmimeglob.h"
#include "xdgmimeint.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <glib.h>

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(!FALSE)
#endif

typedef struct XdgGlobHashNode XdgGlobHashNode;
typedef struct XdgGlobList XdgGlobList;

struct XdgGlobHashNode
{
  xdg_unichar_t character;
  const char *mime_type;
  int weight;
  XdgGlobHashNode *next;
  XdgGlobHashNode *child;
};
struct XdgGlobList
{
  const char *data;
  const char *mime_type;
  int weight;
  XdgGlobList *next;
};

struct XdgGlobHash
{
  XdgGlobList *literal_list;
  XdgGlobHashNode *simple_node;
  XdgGlobList *full_list;
};


/* XdgGlobList
 */
static XdgGlobList *
_xdg_glob_list_new (void)
{
  XdgGlobList *new_element;

  new_element = calloc (1, sizeof (XdgGlobList));

  return new_element;
}

/* Frees glob_list and all of it's children */
static void
_xdg_glob_list_free (XdgGlobList * glob_list)
{
  XdgGlobList *ptr, *next;

  ptr = glob_list;

  while (ptr != NULL) {
    next = ptr->next;

    if (ptr->data)
      free ((void *) ptr->data);
    if (ptr->mime_type)
      free ((void *) ptr->mime_type);
    free (ptr);

    ptr = next;
  }
}

static XdgGlobList *
_xdg_glob_list_append (XdgGlobList * glob_list,
    void *data, const char *mime_type, int weight)
{
  XdgGlobList *new_element;
  XdgGlobList *tmp_element;

  new_element = _xdg_glob_list_new ();
  new_element->data = data;
  new_element->mime_type = mime_type;
  new_element->weight = weight;
  if (glob_list == NULL)
    return new_element;

  tmp_element = glob_list;
  while (tmp_element->next != NULL)
    tmp_element = tmp_element->next;

  tmp_element->next = new_element;

  return glob_list;
}

/* XdgGlobHashNode
 */

static XdgGlobHashNode *
_xdg_glob_hash_node_new (void)
{
  XdgGlobHashNode *glob_hash_node;

  glob_hash_node = calloc (1, sizeof (XdgGlobHashNode));

  return glob_hash_node;
}

static void
_xdg_glob_hash_node_dump (XdgGlobHashNode * glob_hash_node, int depth)
{
  int i;
  for (i = 0; i < depth; i++)
    printf (" ");

  printf ("%c", (char) glob_hash_node->character);
  if (glob_hash_node->mime_type)
    printf (" - %s %d\n", glob_hash_node->mime_type, glob_hash_node->weight);
  else
    printf ("\n");
  if (glob_hash_node->child)
    _xdg_glob_hash_node_dump (glob_hash_node->child, depth + 1);
  if (glob_hash_node->next)
    _xdg_glob_hash_node_dump (glob_hash_node->next, depth);
}

static XdgGlobHashNode *
_xdg_glob_hash_insert_ucs4 (XdgGlobHashNode * glob_hash_node,
    xdg_unichar_t * text, const char *mime_type, int weight)
{
  XdgGlobHashNode *node;
  xdg_unichar_t character;

  character = text[0];

  if ((glob_hash_node == NULL) || (character < glob_hash_node->character)) {
    node = _xdg_glob_hash_node_new ();
    node->character = character;
    node->next = glob_hash_node;
    glob_hash_node = node;
  } else if (character == glob_hash_node->character) {
    node = glob_hash_node;
  } else {
    XdgGlobHashNode *prev_node;
    int found_node = FALSE;

    /* Look for the first character of text in glob_hash_node, and insert it if we
     * have to.*/
    prev_node = glob_hash_node;
    node = prev_node->next;

    while (node != NULL) {
      if (character < node->character) {
        node = _xdg_glob_hash_node_new ();
        node->character = character;
        node->next = prev_node->next;
        prev_node->next = node;

        found_node = TRUE;
        break;
      } else if (character == node->character) {
        found_node = TRUE;
        break;
      }
      prev_node = node;
      node = node->next;
    }

    if (!found_node) {
      node = _xdg_glob_hash_node_new ();
      node->character = character;
      node->next = prev_node->next;
      prev_node->next = node;
    }
  }

  text++;
  if (*text == 0) {
    if (node->mime_type) {
      if (strcmp (node->mime_type, mime_type)) {
        XdgGlobHashNode *child;
        int found_node = FALSE;

        child = node->child;
        while (child && child->character == 0) {
          if (strcmp (child->mime_type, mime_type) == 0) {
            found_node = TRUE;
            break;
          }
          child = child->next;
        }

        if (!found_node) {
          child = _xdg_glob_hash_node_new ();
          child->character = 0;
          child->mime_type = strdup (mime_type);
          child->weight = weight;
          child->child = NULL;
          child->next = node->child;
          node->child = child;
        }
      }
    } else {
      node->mime_type = strdup (mime_type);
      node->weight = weight;
    }
  } else {
    node->child =
        _xdg_glob_hash_insert_ucs4 (node->child, text, mime_type, weight);
  }
  return glob_hash_node;
}

/* glob must be valid UTF-8 */
static XdgGlobHashNode *
_xdg_glob_hash_insert_text (XdgGlobHashNode * glob_hash_node,
    const char *text, const char *mime_type, int weight)
{
  XdgGlobHashNode *node;
  xdg_unichar_t *unitext;
  int len;

  unitext = _xdg_convert_to_ucs4 (text, &len);
  _xdg_reverse_ucs4 (unitext, len);
  node =
      _xdg_glob_hash_insert_ucs4 (glob_hash_node, unitext, mime_type, weight);
  free (unitext);
  return node;
}

typedef struct
{
  const char *mime;
  int weight;
} MimeWeight;

static int
_xdg_glob_hash_node_lookup_file_name (XdgGlobHashNode * glob_hash_node,
    xdg_unichar_t * file_name,
    int len, int ignore_case, MimeWeight mime_types[], int n_mime_types)
{
  int n;
  XdgGlobHashNode *node;
  xdg_unichar_t character;

  if (glob_hash_node == NULL)
    return 0;

  character = file_name[len - 1];
  if (ignore_case)
    character = _xdg_ucs4_to_lower (character);

  for (node = glob_hash_node; node && character >= node->character;
      node = node->next) {
    if (character == node->character) {
      len--;
      n = 0;
      if (len > 0) {
        n = _xdg_glob_hash_node_lookup_file_name (node->child,
            file_name, len, ignore_case, mime_types, n_mime_types);
      }
      if (n == 0) {
        if (node->mime_type) {
          mime_types[n].mime = node->mime_type;
          mime_types[n].weight = node->weight;
          n++;
        }
        node = node->child;
        while (n < n_mime_types && node && node->character == 0) {
          if (node->mime_type) {
            mime_types[n].mime = node->mime_type;
            mime_types[n].weight = node->weight;
            n++;
          }
          node = node->next;
        }
      }
      return n;
    }
  }

  return 0;
}

static int
compare_mime_weight (const void *a, const void *b)
{
  const MimeWeight *aa = (const MimeWeight *) a;
  const MimeWeight *bb = (const MimeWeight *) b;

  return aa->weight - bb->weight;
}

int
_xdg_glob_hash_lookup_file_name (XdgGlobHash * glob_hash,
    const char *file_name, const char *mime_types[], int n_mime_types)
{
  XdgGlobList *list;
  int i, n;
  MimeWeight mimes[10];
  int n_mimes = 10;
  xdg_unichar_t *ucs4;
  int len;

  /* First, check the literals */

  assert (file_name != NULL && n_mime_types > 0);

  n = 0;

  for (list = glob_hash->literal_list; list; list = list->next) {
    if (strcmp ((const char *) list->data, file_name) == 0) {
      mime_types[0] = list->mime_type;
      return 1;
    }
  }

  ucs4 = _xdg_convert_to_ucs4 (file_name, &len);
  n = _xdg_glob_hash_node_lookup_file_name (glob_hash->simple_node, ucs4, len,
      FALSE, mimes, n_mimes);
  if (n == 0)
    n = _xdg_glob_hash_node_lookup_file_name (glob_hash->simple_node, ucs4, len,
        TRUE, mimes, n_mimes);
  free (ucs4);

  /* FIXME: Not UTF-8 safe */
  if (n == 0) {
    for (list = glob_hash->full_list; list && n < n_mime_types;
        list = list->next) {
      if (g_pattern_match_simple ((const char *) list->data, file_name) != 0) {
        mimes[n].mime = list->mime_type;
        mimes[n].weight = list->weight;
        n++;
      }
    }
  }

  qsort (mimes, n, sizeof (MimeWeight), compare_mime_weight);

  if (n_mime_types < n)
    n = n_mime_types;

  for (i = 0; i < n; i++)
    mime_types[i] = mimes[i].mime;

  return n;
}



/* XdgGlobHash
 */

XdgGlobHash *
_xdg_glob_hash_new (void)
{
  XdgGlobHash *glob_hash;

  glob_hash = calloc (1, sizeof (XdgGlobHash));

  return glob_hash;
}


static void
_xdg_glob_hash_free_nodes (XdgGlobHashNode * node)
{
  if (node) {
    if (node->child)
      _xdg_glob_hash_free_nodes (node->child);
    if (node->next)
      _xdg_glob_hash_free_nodes (node->next);
    if (node->mime_type)
      free ((void *) node->mime_type);
    free (node);
  }
}

void
_xdg_glob_hash_free (XdgGlobHash * glob_hash)
{
  _xdg_glob_list_free (glob_hash->literal_list);
  _xdg_glob_list_free (glob_hash->full_list);
  _xdg_glob_hash_free_nodes (glob_hash->simple_node);
  free (glob_hash);
}

XdgGlobType
_xdg_glob_determine_type (const char *glob)
{
  const char *ptr;
  int maybe_in_simple_glob = FALSE;
  int first_char = TRUE;

  ptr = glob;

  while (*ptr != '\0') {
    if (*ptr == '*' && first_char)
      maybe_in_simple_glob = TRUE;
    else if (*ptr == '\\' || *ptr == '[' || *ptr == '?' || *ptr == '*')
      return XDG_GLOB_FULL;

    first_char = FALSE;
    ptr = _xdg_utf8_next_char (ptr);
  }
  if (maybe_in_simple_glob)
    return XDG_GLOB_SIMPLE;
  else
    return XDG_GLOB_LITERAL;
}

/* glob must be valid UTF-8 */
void
_xdg_glob_hash_append_glob (XdgGlobHash * glob_hash,
    const char *glob, const char *mime_type, int weight)
{
  XdgGlobType type;

  assert (glob_hash != NULL);
  assert (glob != NULL);

  type = _xdg_glob_determine_type (glob);

  switch (type) {
    case XDG_GLOB_LITERAL:
      glob_hash->literal_list =
          _xdg_glob_list_append (glob_hash->literal_list, strdup (glob),
          strdup (mime_type), weight);
      break;
    case XDG_GLOB_SIMPLE:
      glob_hash->simple_node =
          _xdg_glob_hash_insert_text (glob_hash->simple_node, glob + 1,
          mime_type, weight);
      break;
    case XDG_GLOB_FULL:
      glob_hash->full_list =
          _xdg_glob_list_append (glob_hash->full_list, strdup (glob),
          strdup (mime_type), weight);
      break;
  }
}

void
_xdg_glob_hash_dump (XdgGlobHash * glob_hash)
{
  XdgGlobList *list;
  printf ("LITERAL STRINGS\n");
  if (!glob_hash || glob_hash->literal_list == NULL) {
    printf ("    None\n");
  } else {
    for (list = glob_hash->literal_list; list; list = list->next)
      printf ("    %s - %s %d\n", (char *) list->data, list->mime_type,
          list->weight);
  }
  printf ("\nSIMPLE GLOBS\n");
  if (!glob_hash || glob_hash->simple_node == NULL) {
    printf ("    None\n");
  } else {
    _xdg_glob_hash_node_dump (glob_hash->simple_node, 4);
  }

  printf ("\nFULL GLOBS\n");
  if (!glob_hash || glob_hash->full_list == NULL) {
    printf ("    None\n");
  } else {
    for (list = glob_hash->full_list; list; list = list->next)
      printf ("    %s - %s %d\n", (char *) list->data, list->mime_type,
          list->weight);
  }
}


void
_xdg_mime_glob_read_from_file (XdgGlobHash * glob_hash, const char *file_name)
{
  FILE *glob_file;
  char line[255];

  glob_file = fopen (file_name, "r");

  if (glob_file == NULL)
    return;

  /* FIXME: Not UTF-8 safe.  Doesn't work if lines are greater than 255 chars.
   * Blah */
  while (fgets (line, 255, glob_file) != NULL) {
    char *colon, *colon2;
    char *mimetype, *glob;
    int weight;

    if (line[0] == '#')
      continue;

    colon = strchr (line, ':');
    if (colon == NULL)
      continue;
    *(colon++) = '\0';
    colon[strlen (colon) - 1] = '\0';
    colon2 = strchr (colon, ':');
    if (colon2) {
      *(colon2++) = '\000';
      weight = atoi (line);
      mimetype = colon;
      glob = colon2;
    } else {
      weight = 50;
      mimetype = line;
      glob = colon;
    }
    _xdg_glob_hash_append_glob (glob_hash, glob, mimetype, weight);
  }

  fclose (glob_file);
}
