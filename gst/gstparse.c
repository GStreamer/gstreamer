/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2002 Andy Wingo <wingo@pobox.com>
 *
 * gstparse.c: get a pipeline from a text pipeline description
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

#include <string.h>

#include "gstparse.h"
#include "gstinfo.h"
#include "parse/types.h"

typedef struct _gst_parse_delayed_pad gst_parse_delayed_pad;
struct _gst_parse_delayed_pad
{
  gchar *name;
  GstPad *peer;
};

typedef struct
{
  gchar *srcpadname;
  GstPad *target_pad;
  GstElement *target_element;
  GstElement *pipeline;
}
dynamic_connection_t;


GQuark 
gst_parse_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gst_parse_error");
  return quark;
}

G_GNUC_UNUSED static void
dynamic_connect (GstElement * element, GstPad * newpad, gpointer data)
{
  dynamic_connection_t *dc = (dynamic_connection_t *) data;
  gboolean warn = TRUE;

  /* do we know the exact srcpadname? */
  if (dc->srcpadname) {
    /* see if this is the one */
    if (strcmp (gst_pad_get_name (newpad), dc->srcpadname)) {
      return;
    }
  }

  /* try to find a target pad if we don't know it yet */
  if (!dc->target_pad) {
    if (!GST_PAD_IS_CONNECTED (newpad)) {
      dc->target_pad = gst_element_get_compatible_pad (dc->target_element, newpad);
      warn = FALSE;
    }
    else {
      return;
    }
  }
  if (!GST_PAD_IS_CONNECTED (dc->target_pad) && !GST_PAD_IS_CONNECTED (newpad)) {
    gst_element_set_state (dc->pipeline, GST_STATE_PAUSED);
    if (!gst_pad_connect (newpad, dc->target_pad) && warn) {
      g_warning ("could not connect %s:%s to %s:%s", GST_DEBUG_PAD_NAME (newpad), 
                 GST_DEBUG_PAD_NAME (dc->target_pad));
    }
    gst_element_set_state (dc->pipeline, GST_STATE_PLAYING);
  }
}

static gboolean
make_elements (graph_t *g, GError **error) 
{
  GList *l = NULL;
  gchar *bin_type;
  element_t *e;
  
  if (!(g->bins || g->elements)) {
    g_set_error (error,
                 GST_PARSE_ERROR,
                 GST_PARSE_ERROR_SYNTAX,
                 "Empty bin");
    return FALSE;
  }

  if (g->current_bin_type)
    bin_type = g->current_bin_type;
  else
    bin_type = "pipeline";
  
  if (!(g->bin = gst_element_factory_make (bin_type, NULL))) {
    g_set_error (error,
                 GST_PARSE_ERROR,
                 GST_PARSE_ERROR_NO_SUCH_ELEMENT,
                 "No such bin type %s", bin_type);
    return FALSE;
  }
  
  l = g->elements;
  while (l) {
    e = (element_t*)l->data;
    if (!(e->element = gst_element_factory_make (e->type, NULL))) {
      g_set_error (error,
                   GST_PARSE_ERROR,
                   GST_PARSE_ERROR_NO_SUCH_ELEMENT,
                   "No such element %s", e->type);
      return FALSE;
    }
    gst_bin_add (GST_BIN (g->bin), e->element);
    l = g_list_next (l);
  }
  
  l = g->bins;
  while (l) {
    if (!make_elements ((graph_t*)l->data, error))
      return FALSE;
    gst_bin_add (GST_BIN (g->bin), ((graph_t*)l->data)->bin);
    l = g_list_next (l);
  }

  return TRUE;
}

static gboolean
set_properties (graph_t *g, GError **error)
{
  GList *l, *l2;
  element_t *e;
  property_t *p;
  GParamSpec *pspec;
  
  l = g->elements;
  while (l) {
    e = (element_t*)l->data;
    l2 = e->property_values;
    while (l2) {
      p = (property_t*)l2->data;
      if ((pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (e->element), p->name))) {
        g_object_set_property (G_OBJECT (e->element), p->name, p->value);
      } else {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_NO_SUCH_PROPERTY,
                     "No such property '%s' in element '%s'",
                     p->name, GST_OBJECT_NAME (GST_OBJECT (e->element)));
        return FALSE;
      }
      l2 = g_list_next (l2);
    }
    l = g_list_next (l);
  }
  
  l = g->bins;
  while (l) {
    if (!set_properties ((graph_t*)l->data, error))
      return FALSE;
    l = g_list_next (l);
  }
  
  return TRUE;
}

static GstElement*
find_element_by_index_recurse (graph_t *g, gint i)
{
  GList *l;
  element_t *e;
  GstElement *element;
  
  l = g->elements;
  while (l) {
    e = (element_t*)l->data;
    if (e->index == i) {
      return e->element;
    }
    l = g_list_next (l);
  }
  
  l = g->bins;
  while (l) {
    if ((element = find_element_by_index_recurse ((graph_t*)l->data, i)))
      return element;
    l = g_list_next (l);
  }
  
  return NULL;
}

static GstElement*
find_element_by_index (graph_t *g, gint i) 
{
  while (g->parent)
    g = g->parent;

  return find_element_by_index_recurse (g, i);
}

static gboolean
make_connections (graph_t *g, GError **error)
{
  GList *l, *a, *b;
  connection_t *c;
  dynamic_connection_t *dc;
  GstElement *src, *sink;
  GstPad *p1, *p2;
  GstPadTemplate *pt1, *pt2;
  
  l = g->connections;
  while (l) {
    c = (connection_t*)l->data;
    if (c->src_name) {
      if (!(src = gst_bin_get_by_name (GST_BIN (g->bin), c->src_name))) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_NO_SUCH_ELEMENT,
                     "No such element '%s'",
                     c->src_name);
        return FALSE;
      }
    } else {
      src = find_element_by_index (g, c->src_index);
      g_assert (src);
    }
    if (c->sink_name) {
      if (!(sink = gst_bin_get_by_name (GST_BIN (g->bin), c->sink_name))) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_NO_SUCH_ELEMENT,
                     "No such element '%s'",
                     c->sink_name);
        return FALSE;
      }
    } else {
      sink = find_element_by_index (g, c->sink_index);
      g_assert (sink);
    }
    
    a = c->src_pads;
    b = c->sink_pads;
    /* g_print ("a: %p, b: %p\n", a, b); */
    if (a && b) {
      /* balanced multipad connection */
      while (a && b) {
        p1 = gst_element_get_pad (src, (gchar*)a->data);
        p2 = gst_element_get_pad (sink, (gchar*)b->data);

        if (!p2)
          goto could_not_get_pad_b;
        
        if (!p1 && p2 && (pt1 = gst_element_get_pad_template (src, (gchar*)a->data)) &&
            pt1->presence == GST_PAD_SOMETIMES) {
          dc = g_new0 (dynamic_connection_t, 1);
          dc->srcpadname = (gchar*)a->data;
          dc->target_pad = p2;
          dc->target_element = sink;
          dc->pipeline = g->bin;
          
          GST_DEBUG (GST_CAT_PIPELINE, "setting up dynamic connection %s:%s and %s:%s",
                     GST_OBJECT_NAME (GST_OBJECT (src)),
                     (gchar*)a->data, GST_DEBUG_PAD_NAME (p2));
          
          g_signal_connect (G_OBJECT (src), "new_pad", G_CALLBACK (dynamic_connect), dc);
        } else if (!p1) {
          goto could_not_get_pad_a;
        } else if (!gst_pad_connect (p1, p2)) {
          goto could_not_connect_pads;
        }
        a = g_list_next (a);
        b = g_list_next (b);
      }
    } else if (a) {
      if ((pt1 = gst_element_get_pad_template (src, (gchar*)a->data))) {
        if ((p1 = gst_element_get_pad (src, (gchar*)a->data)) || pt1->presence == GST_PAD_SOMETIMES) {
          if (!p1) {
            /* sigh, a hack until i fix the gstelement api... */
            if ((pt2 = gst_element_get_compatible_pad_template (sink, pt1))) {
              if ((p2 = gst_element_get_pad (sink, pt2->name_template))) {
                dc = g_new0 (dynamic_connection_t, 1);
                dc->srcpadname = (gchar*)a->data;
                dc->target_pad = p2;
                dc->target_element = NULL;
                dc->pipeline = g->bin;
              
                GST_DEBUG (GST_CAT_PIPELINE, "setting up dynamic connection %s:%s and %s:%s",
                           GST_OBJECT_NAME (GST_OBJECT (src)),
                           (gchar*)a->data, GST_DEBUG_PAD_NAME (p2));
              
                g_signal_connect (G_OBJECT (src), "new_pad", G_CALLBACK (dynamic_connect), dc);
		goto next;
              } else {
                /* both pt1 and pt2 are sometimes templates. sheesh. */
                goto both_templates_have_sometimes_presence;
              }
            } else {
	      /* if the target pad has no padtemplate we will figure out a target 
	       * pad later on */
              dc = g_new0 (dynamic_connection_t, 1);
              dc->srcpadname = NULL;
              dc->target_pad = NULL;
              dc->target_element = sink;
              dc->pipeline = g->bin;
              
              GST_DEBUG (GST_CAT_PIPELINE, "setting up dynamic connection %s:%s, and some pad in %s",
                           GST_OBJECT_NAME (GST_OBJECT (src)),
                           (gchar*)a->data, GST_OBJECT_NAME (sink));
              
              g_signal_connect (G_OBJECT (src), "new_pad", G_CALLBACK (dynamic_connect), dc);
   	      goto next;
            }
          } else {
            goto could_not_get_compatible_to_a;
          }
        } else {
          goto could_not_get_pad_a;
        }
      } else {
        goto could_not_get_pad_a;
      }
      
      if (!gst_pad_connect (p1, p2)) {
        goto could_not_connect_pads;
      }
    } else if (b) {
      /* we don't support dynamic connections on this side yet, if ever */
      if (!(p2 = gst_element_get_pad (sink, (gchar*)b->data))) {
        goto could_not_get_pad_b;
      }
      if (!(p1 = gst_element_get_compatible_pad (src, p2))) {
        goto could_not_get_compatible_to_b;
      }
      if (!gst_pad_connect (p1, p2)) {
        goto could_not_connect_pads;
      }
    } else {
      if (!gst_element_connect (src, sink)) {
        goto could_not_connect_elements;
      }
    }
next:
    l = g_list_next (l);
  }
  
  l = g->bins;
  while (l) {
    if (!make_connections ((graph_t*)l->data, error))
      return FALSE;
    l = g_list_next (l);
  }
  
  return TRUE;

could_not_get_pad_a:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Could not get a pad %s from element %s",
               (gchar*)a->data, GST_OBJECT_NAME (src));
  return FALSE;
could_not_get_pad_b:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Could not get a pad %s from element %s",
               (gchar*)b->data, GST_OBJECT_NAME (sink));
  return FALSE;
could_not_get_compatible_to_a:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Could not find a compatible pad in element %s to for %s:%s",
               GST_OBJECT_NAME (sink), GST_OBJECT_NAME (src), (gchar*)a->data);
  return FALSE;
could_not_get_compatible_to_b:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Could not find a compatible pad in element %s to for %s:%s",
               GST_OBJECT_NAME (src), GST_OBJECT_NAME (sink), (gchar*)b->data);
  return FALSE;
both_templates_have_sometimes_presence:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Both %s:%s and %s:%s have GST_PAD_SOMETIMES presence, operation not supported",
               GST_OBJECT_NAME (src), pt1->name_template, GST_OBJECT_NAME (sink), pt2->name_template);
  return FALSE;
could_not_connect_pads:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Could not connect %s:%s to %s:%s",
               GST_DEBUG_PAD_NAME (p1),
               GST_DEBUG_PAD_NAME (p2));
  return FALSE;
could_not_connect_elements:
  g_set_error (error,
               GST_PARSE_ERROR,
               GST_PARSE_ERROR_CONNECT,
               "Could not connect element %s to %s",
               GST_OBJECT_NAME (src),
               GST_OBJECT_NAME (sink));
  return FALSE;
}

static GstBin*
pipeline_from_graph (graph_t *g, GError **error)
{
  if (!make_elements (g, error))
    return NULL;
  
  if (!set_properties (g, error))
    return NULL;
  
  if (!make_connections (g, error))
    return NULL;
  
  return (GstBin*)g->bin;
}

/**
 * gst_parse_launchv:
 * @argv: null-terminated array of arguments
 *
 * Create a new pipeline based on command line syntax.
 *
 * Returns: a new pipeline on success, NULL on failure
 */
GstBin *
gst_parse_launchv (const gchar **argv, GError **error)
{
  GstBin *pipeline;
  GString *str;
  const gchar **argvp, *arg;
  gchar *tmp;

  /* let's give it a nice size. */
  str = g_string_sized_new (1024);

  argvp = argv;
  while (*argvp) {
    arg = *argvp;
    tmp = _gst_parse_escape (arg);
    g_string_append (str, tmp);
    g_free (tmp);
    g_string_append (str, " ");
    argvp++;
  }
  
  pipeline = gst_parse_launch (str->str, error);

  g_string_free (str, TRUE);

  return pipeline;
}

gchar *_gst_parse_escape (const gchar *str)
{
  GString *gstr = NULL;
  
  g_return_val_if_fail (str != NULL, NULL);
  
  gstr = g_string_sized_new (strlen (str));
  
  while (*str) {
    if (*str == ' ')
      g_string_append_c (gstr, '\\');
    g_string_append_c (gstr, *str);
    str++;
  }
  
  return gstr->str;
}

void _gst_parse_unescape (gchar *str)
{
  gchar *walk;
  
  g_return_if_fail (str != NULL);
  
  walk = str;
  
  while (*walk) {
    if (*walk == '\\')
      walk++;
    *str = *walk;
    str++;
    walk++;
  }
  *str = '\0';
}

/**
 * gst_parse_launch:
 * @pipeline_description: the command line describing the pipeline
 *
 * Create a new pipeline based on command line syntax.
 *
 * Returns: a new bin on success, NULL on failure. By default the bin is
 * a GstPipeline, but it depends on the pipeline_description.
 */
GstBin *
gst_parse_launch (const gchar * pipeline_description, GError **error)
{
  graph_t *graph;
  static GStaticMutex flex_lock = G_STATIC_MUTEX_INIT;

  g_return_val_if_fail (pipeline_description != NULL, NULL);

  GST_INFO (GST_CAT_PIPELINE, "parsing pipeline description %s",
            pipeline_description);

  /* the need for the mutex will go away with flex 2.5.6 */
  g_static_mutex_lock (&flex_lock);
  graph = _gst_parse_launch (pipeline_description, error);
  g_static_mutex_unlock (&flex_lock);

  if (!graph)
    return NULL;
  
  return pipeline_from_graph (graph, error);
}
