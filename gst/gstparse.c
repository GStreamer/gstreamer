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

/* #define DEBUG(format,args...) g_print (format, ##args) */
#define DEBUG(format,args...)
#define DEBUG_NOPREFIX(format,args...)
#define VERBOSE(format,args...)

#include <string.h>

#include "gst_private.h"
#include "gstparse.h"
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
  GstPad *target;
  GstElement *pipeline;
}
dyn_connect;


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
  dyn_connect *connect = (dyn_connect *) data;

  if (!strcmp (gst_pad_get_name (newpad), connect->srcpadname)) {
    gst_element_set_state (connect->pipeline, GST_STATE_PAUSED);
    if (!gst_pad_connect (newpad, connect->target))
      g_warning ("could not connect %s:%s to %s:%s", GST_DEBUG_PAD_NAME (newpad), 
                 GST_DEBUG_PAD_NAME (connect->target));
    gst_element_set_state (connect->pipeline, GST_STATE_PLAYING);
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
  
  if (!(g->bin = gst_elementfactory_make (bin_type, NULL))) {
    g_set_error (error,
                 GST_PARSE_ERROR,
                 GST_PARSE_ERROR_NO_SUCH_ELEMENT,
                 "No such bin type %s", bin_type);
    return FALSE;
  }
  
  l = g->elements;
  while (l) {
    e = (element_t*)l->data;
    if (!(e->element = gst_elementfactory_make (e->type, NULL))) {
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
  GstElement *src, *sink;
  GstPad *p1, *p2;
  
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
    if (a && b) {
      /* balanced multipad connection */
      while (a && b) {
        if (!gst_element_connect_pads (src, (gchar*)a->data, sink, (gchar*)b->data)) {
          g_set_error (error,
                       GST_PARSE_ERROR,
                       GST_PARSE_ERROR_CONNECT,
                       "Could not connect %s:%s to %s:%s",
                       GST_OBJECT_NAME (src), (gchar*)a->data,
                       GST_OBJECT_NAME (sink), (gchar*)b->data);
          return FALSE;
        }
        a = g_list_next (a);
        b = g_list_next (b);
      }
    } else if (a) {
      if (!(p1 = gst_element_get_pad (src, (gchar*)a->data))) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not get a pad %s from element %s",
                     (gchar*)a->data, GST_OBJECT_NAME (src));
        return FALSE;
      }
      if (!(p2 = gst_element_get_compatible_pad (sink, p1))) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not find a compatible pad in element %s to for %s:%s",
                     GST_OBJECT_NAME (sink), GST_OBJECT_NAME (src), (gchar*)a->data);
        return FALSE;
      }
      if (!gst_pad_connect (p1, p2)) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not connect %s:%s to %s:%s",
                     GST_OBJECT_NAME (src), GST_OBJECT_NAME (p1),
                     GST_OBJECT_NAME (sink), GST_OBJECT_NAME (p1));
        return FALSE;
      }
    } else if (b) {
      if (!(p2 = gst_element_get_pad (sink, (gchar*)b->data))) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not get a pad %s from element %s",
                     (gchar*)b->data, GST_OBJECT_NAME (sink));
        return FALSE;
      }
      if (!(p1 = gst_element_get_compatible_pad (src, p2))) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not find a compatible pad in element %s to for %s:%s",
                     GST_OBJECT_NAME (src), GST_OBJECT_NAME (sink), (gchar*)b->data);
        return FALSE;
      }
      if (!gst_pad_connect (p1, p2)) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not connect %s:%s to %s:%s",
                     GST_OBJECT_NAME (src), GST_OBJECT_NAME (p1),
                     GST_OBJECT_NAME (sink), GST_OBJECT_NAME (p1));
        return FALSE;
      }
    } else {
      if (!gst_element_connect (src, sink)) {
        g_set_error (error,
                     GST_PARSE_ERROR,
                     GST_PARSE_ERROR_CONNECT,
                     "Could not connect %s to %s",
                     GST_OBJECT_NAME (src), GST_OBJECT_NAME (sink));
        return FALSE;
      }
    }
    l = g_list_next (l);
  }
  
  l = g->bins;
  while (l) {
    if (!make_connections ((graph_t*)l->data, error))
      return FALSE;
    l = g_list_next (l);
  }
  
  return TRUE;
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
  gchar *pipeline_description;

  /* i think this cast works out ok... */
  pipeline_description = g_strjoinv (" ", (gchar**)argv);
  
  pipeline = gst_parse_launch (pipeline_description, error);

  return pipeline;
}

/**
 * gst_parse_launch:
 * @pipeline_description: the command line describing the pipeline
 *
 * Create a new pipeline based on command line syntax.
 *
 * Returns: a new GstPipeline (cast to a Bin) on success, NULL on failure
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
