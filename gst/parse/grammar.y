%{
#include <glib-object.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../gst_private.h"
#include "../gst-i18n-lib.h"

#include "../gstconfig.h"
#include "../gstparse.h"
#include "../gstinfo.h"
#include "../gsturi.h"
#include "types.h"

/* All error messages in this file are user-visible and need to be translated.
 * Don't start the message with a capital, and don't end them with a period,
 * as they will be presented inside a sentence/error.
 */
  
#define YYERROR_VERBOSE 1
#define YYPARSE_PARAM graph

#ifdef __GST_PARSE_TRACE
static guint __strings;
static guint __links;
static guint __chains;
gchar *
__gst_parse_strdup (gchar *org)
{
  gchar *ret; 
  __strings++;
  ret = g_strdup (org);
  /* g_print ("ALLOCATED STR   (%3u): %p %s\n", __strings, ret, ret); */
  return ret;
}
void
__gst_parse_strfree (gchar *str)
{
  if (str) {
    /* g_print ("FREEING STR     (%3u): %p %s\n", __strings - 1, str, str); */
    g_free (str);
    g_return_if_fail (__strings > 0);
    __strings--;
  }
}
link_t *__gst_parse_link_new ()
{
  link_t *ret;
  __links++;
  ret = g_new0 (link_t, 1);
  /* g_print ("ALLOCATED LINK  (%3u): %p\n", __links, ret); */
  return ret;
}
void
__gst_parse_link_free (link_t *data)
{
  if (data) {
    /* g_print ("FREEING LINK    (%3u): %p\n", __links - 1, data); */
    g_free (data);
    g_return_if_fail (__links > 0);
    __links--;
  }
}
chain_t *
__gst_parse_chain_new ()
{
  chain_t *ret;
  __chains++;
  ret = g_new0 (chain_t, 1);
  /* g_print ("ALLOCATED CHAIN (%3u): %p\n", __chains, ret); */
  return ret;
}
void
__gst_parse_chain_free (chain_t *data)
{
  if (data) {
    /* g_print ("FREEING CHAIN   (%3u): %p\n", __chains - 1, data); */
    g_free (data);
    g_return_if_fail (__chains > 0);
    __chains--;
  }
}

#endif /* __GST_PARSE_TRACE */

typedef struct {
  gchar *src_pad;
  gchar *sink_pad;
  GstElement *sink;
  GstCaps *caps;
  gulong signal_id;
  /* FIXME: need to connect to "disposed" signal to clean up, but there is no such signal */
} DelayedLink;

#ifdef G_HAVE_ISO_VARARGS
#define SET_ERROR(error, type, ...) G_STMT_START{ \
  if (error) { \
    if (*(error)) { \
      g_warning (__VA_ARGS__); \
    } else { \
      g_set_error ((error), GST_PARSE_ERROR, (type), __VA_ARGS__); \
    }\
  } \
}G_STMT_END
#define ERROR(type, ...) SET_ERROR (((graph_t *) graph)->error, (type), __VA_ARGS__ )
#ifndef GST_DISABLE_GST_DEBUG
#  define YYDEBUG 1
   /* bison 1.35 calls this macro with side effects, we need to make sure the
      side effects work - crappy bison
#  define YYFPRINTF(a, ...) GST_CAT_DEBUG (GST_CAT_PIPELINE, __VA_ARGS__)
 */
#  define YYFPRINTF(a, ...) G_STMT_START{ \
     gchar *temp = g_strdup_printf (__VA_ARGS__); \
     GST_CAT_DEBUG (GST_CAT_PIPELINE, temp); \
     g_free (temp); \
   }G_STMT_END
#endif

#elif defined(G_HAVE_GNUC_VARARGS)

#define SET_ERROR(error, type, args...) G_STMT_START{ \
  if (error) { \
    if (*(error)) { \
      g_warning ( ## args ); \
    } else { \
      g_set_error ((error), GST_PARSE_ERROR, (type), ## args ); \
    }\
  } \
}G_STMT_END
#define ERROR(type, args...) SET_ERROR (((graph_t *) graph)->error, (type), ## args )
#ifndef GST_DISABLE_GST_DEBUG
#  define YYDEBUG 1
   /* bison 1.35 calls this macro with side effects, we need to make sure the
      side effects work - crappy bison
#  define YYFPRINTF(a, args...) GST_CAT_DEBUG (GST_CAT_PIPELINE, ## args )
 */
#  define YYFPRINTF(a, args...) G_STMT_START{ \
     gchar *temp = g_strdup_printf ( ## args ); \
     GST_CAT_DEBUG (GST_CAT_PIPELINE, temp); \
     g_free (temp); \
   }G_STMT_END
#endif

#else

#define SET_ERROR(error, type, ...) G_STMT_START{ \
  if (error) { \
    if (*(error)) { \
      g_warning ("error while parsing"); \
    } else { \
      g_set_error ((error), GST_PARSE_ERROR, (type), "error while parsing"); \
    }\
  } \
}G_STMT_END
#define ERROR(type, ...) SET_ERROR (((graph_t *) graph)->error, (type), "error while parsing")
#ifndef GST_DISABLE_GST_DEBUG
#  define YYDEBUG 1
#endif

#endif /* G_HAVE_ISO_VARARGS */

#define GST_BIN_MAKE(res, type, chainval, assign) G_STMT_START{ \
  chain_t *chain = chainval; \
  GSList *walk; \
  GstBin *bin = (GstBin *) gst_element_factory_make (type, NULL); \
  if (!chain) { \
    ERROR (GST_PARSE_ERROR_EMPTY_BIN, _("specified empty bin \"%s\", not allowed"), type); \
    g_slist_foreach (assign, (GFunc) gst_parse_strfree, NULL); \
    g_slist_free (assign); \
    YYERROR; \
  } else if (!bin) { \
    ERROR (GST_PARSE_ERROR_NO_SUCH_ELEMENT, _("no bin \"%s\", skipping"), type); \
    g_slist_foreach (assign, (GFunc) gst_parse_strfree, NULL); \
    g_slist_free (assign); \
    res = chain; \
  } else { \
    walk = chain->elements; \
    while (walk) { \
      gst_bin_add (bin, GST_ELEMENT (walk->data)); \
      walk = walk->next; \
    } \
    g_slist_free (chain->elements); \
    chain->elements = g_slist_prepend (NULL, bin); \
    res = chain; \
    /* set the properties now */ \
    walk = assign; \
    while (walk) { \
      gst_parse_element_set ((gchar *) walk->data, GST_ELEMENT (bin), graph); \
      walk = g_slist_next (walk); \
    } \
    g_slist_free (assign); \
  } \
}G_STMT_END

#define MAKE_LINK(link, _src, _src_name, _src_pads, _sink, _sink_name, _sink_pads) G_STMT_START{ \
  link = gst_parse_link_new (); \
  link->src = _src; \
  link->sink = _sink; \
  link->src_name = _src_name; \
  link->sink_name = _sink_name; \
  link->src_pads = _src_pads; \
  link->sink_pads = _sink_pads; \
  link->caps = NULL; \
}G_STMT_END

#define MAKE_REF(link, _src, _pads) G_STMT_START{ \
  gchar *padname = _src; \
  GSList *pads = _pads; \
  if (padname) { \
    while (*padname != '.') padname++; \
    *padname = '\0'; \
    padname++; \
    if (*padname != '\0') \
      pads = g_slist_prepend (pads, gst_parse_strdup (padname)); \
  } \
  MAKE_LINK (link, NULL, _src, pads, NULL, NULL, NULL); \
}G_STMT_END

static void
gst_parse_element_set (gchar *value, GstElement *element, graph_t *graph)
{
  GParamSpec *pspec;
  gchar *pos = value;
  /* parse the string, so the property name is null-terminated an pos points
     to the beginning of the value */
  while (!g_ascii_isspace (*pos) && (*pos != '=')) pos++; 
  if (*pos == '=') { 
    *pos = '\0'; 
  } else { 
    *pos = '\0'; 
    pos++;
    while (g_ascii_isspace (*pos)) pos++; 
  } 
  pos++; 
  while (g_ascii_isspace (*pos)) pos++; 
  if (*pos == '"') {
    pos++;
    pos[strlen (pos) - 1] = '\0';
  }
  gst_parse_unescape (pos); 
  if ((pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (element), value))) { 
    GValue v = { 0, }; 
    GValue v2 = { 0, };
    g_value_init (&v, G_PARAM_SPEC_VALUE_TYPE(pspec)); 
    switch (G_TYPE_FUNDAMENTAL (G_PARAM_SPEC_VALUE_TYPE (pspec))) {
    case G_TYPE_STRING:
      g_value_set_string (&v, pos);
      break;      
    case G_TYPE_BOOLEAN:
      if (g_ascii_strcasecmp (pos, "true") && g_ascii_strcasecmp (pos, "yes") && g_ascii_strcasecmp (pos, "1")) {
        g_value_set_boolean (&v, FALSE);
      } else {
        g_value_set_boolean (&v, TRUE);
      }
      break;
    case G_TYPE_ENUM: {
      GEnumValue *en;
      gchar *endptr = NULL;
      GEnumClass *klass = (GEnumClass *) g_type_class_peek (G_PARAM_SPEC_VALUE_TYPE (pspec));
      if (klass == NULL) goto error;
      if (!(en = g_enum_get_value_by_name (klass, pos))) {
        if (!(en = g_enum_get_value_by_nick (klass, pos))) {
          gint i = strtol (pos, &endptr, 0);
	  if (endptr && *endptr == '\0') {
	    en = g_enum_get_value (klass, i);
	  }
        }
      }
      if (!en)
	goto error;
      g_value_set_enum (&v, en->value);
      break;
    }
    case G_TYPE_INT:
    case G_TYPE_LONG:
    case G_TYPE_INT64: {
      gchar *endptr;
      glong l;
      g_value_init (&v2, G_TYPE_LONG); 
      l = strtol (pos, &endptr, 0);
      if (*endptr != '\0') goto error_conversion;
      g_value_set_long (&v2, l);
      if (!g_value_transform (&v2, &v)) goto error_conversion;
      break;      
    }
    case G_TYPE_UINT:
    case G_TYPE_ULONG:
    case G_TYPE_UINT64: {
      gchar *endptr;
      gulong ul;
      g_value_init (&v2, G_TYPE_ULONG); 
      ul = strtoul (pos, &endptr, 0);
      if (*endptr != '\0') goto error_conversion;
      g_value_set_ulong (&v2, ul);
      if (!g_value_transform (&v2, &v)) goto error_conversion;
      break;      
    }
    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE: {
      gchar *endptr;
      gdouble d;
      g_value_init (&v2, G_TYPE_DOUBLE); 
      d = g_ascii_strtod (pos, &endptr);
      if (*endptr != '\0') goto error_conversion;
      g_value_set_double (&v2, d);
      if (!g_value_transform (&v2, &v)) goto error_conversion;
      break;      
    }
    default:
      /* add more */
      g_warning ("property \"%s\" in element %s cannot be set", value, GST_ELEMENT_NAME (element)); 
      goto error;
    }
    g_object_set_property (G_OBJECT (element), value, &v); 
  } else { 
    ERROR (GST_PARSE_ERROR_NO_SUCH_PROPERTY, _("no property \"%s\" in element \"%s\""), value, GST_ELEMENT_NAME (element)); 
  }

out:
  gst_parse_strfree (value);
  return;
  
error:
  ERROR (GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY,
         _("could not set property \"%s\" in element \"%s\" to \"%s\""), 
	 value, GST_ELEMENT_NAME (element), pos); 
  goto out;
error_conversion:
  ERROR (GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY,
         _("could not convert \"%s\" so that it fits property \"%s\" in element \"%s\""),
         pos, value, GST_ELEMENT_NAME (element)); 
  goto out;
}
static inline void
gst_parse_free_link (link_t *link)
{
  gst_parse_strfree (link->src_name);
  gst_parse_strfree (link->sink_name);
  g_slist_foreach (link->src_pads, (GFunc) gst_parse_strfree, NULL);
  g_slist_foreach (link->sink_pads, (GFunc) gst_parse_strfree, NULL);
  g_slist_free (link->src_pads);
  g_slist_free (link->sink_pads);
  if (link->caps) gst_caps_free (link->caps);
  gst_parse_link_free (link);  
}
static void
gst_parse_element_lock (GstElement *element, gboolean lock)
{
  GstPad *pad;
  GList *walk = (GList *) gst_element_get_pad_list (element);
  gboolean unlocked_peer = FALSE;
  
  if (gst_element_is_locked_state (element) == lock)
    return;
  /* check if we have an unlocked peer */
  while (walk) {
    pad = (GstPad *) GST_PAD_REALIZE (walk->data);
    walk = walk->next;
    if (GST_PAD_IS_SINK (pad) && GST_PAD_PEER (pad) &&
        !gst_element_is_locked_state (GST_PAD_PARENT (GST_PAD_PEER (pad)))) {
      unlocked_peer = TRUE;
      break;
    }
  }  
  
  if (!(lock && unlocked_peer)) {
    gst_element_set_locked_state (element, lock);
    if (!lock)
      gst_element_sync_state_with_parent (element);
  } else {
    return;
  }
  
  /* check if there are other pads to (un)lock */
  walk = (GList *) gst_element_get_pad_list (element);
  while (walk) {
    pad = (GstPad *) GST_PAD_REALIZE (walk->data);
    walk = walk->next;
    if (GST_PAD_IS_SRC (pad) && GST_PAD_PEER (pad)) {
      GstElement *next = GST_ELEMENT (GST_OBJECT_PARENT (GST_PAD_PEER (pad)));
      if (gst_element_is_locked_state (next) != lock)
        gst_parse_element_lock (next, lock);
    }
  }
}
static void
gst_parse_found_pad (GstElement *src, GstPad *pad, gpointer data)
{
  DelayedLink *link = (DelayedLink *) data;
  
  GST_CAT_INFO (GST_CAT_PIPELINE, "trying delayed linking %s:%s to %s:%s", 
                GST_ELEMENT_NAME (src), link->src_pad,
                GST_ELEMENT_NAME (link->sink), link->sink_pad);

  if (gst_element_link_pads_filtered (src, link->src_pad, link->sink, link->sink_pad, link->caps)) {
    /* do this here, we don't want to get any problems later on when unlocking states */
    GST_CAT_DEBUG (GST_CAT_PIPELINE, "delayed linking %s:%s to %s:%s worked", 
               	   GST_ELEMENT_NAME (src), link->src_pad,
               	   GST_ELEMENT_NAME (link->sink), link->sink_pad);
    g_signal_handler_disconnect (src, link->signal_id);
    g_free (link->src_pad);
    g_free (link->sink_pad);
    if (link->caps) gst_caps_free (link->caps);
    if (!gst_element_is_locked_state (src))
      gst_parse_element_lock (link->sink, FALSE);
    g_free (link);
  }
}
/* both padnames and the caps may be NULL */
static gboolean
gst_parse_perform_delayed_link (GstElement *src, const gchar *src_pad, 
                                GstElement *sink, const gchar *sink_pad, GstCaps *caps)
{
  GList *templs = gst_element_get_pad_template_list (src);
	 
  while (templs) {
    GstPadTemplate *templ = (GstPadTemplate *) templs->data;
    if ((GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) && (GST_PAD_TEMPLATE_PRESENCE(templ) == GST_PAD_SOMETIMES))
    {
      DelayedLink *data = g_new (DelayedLink, 1); 
      
      /* TODO: maybe we should check if src_pad matches this template's names */

      GST_CAT_DEBUG (GST_CAT_PIPELINE, "trying delayed link %s:%s to %s:%s", 
                     GST_ELEMENT_NAME (src), src_pad, GST_ELEMENT_NAME (sink), sink_pad);

      data->src_pad = g_strdup (src_pad);
      data->sink = sink;
      data->sink_pad = g_strdup (sink_pad);
      if (caps) {
      	data->caps = gst_caps_copy (caps);
      } else {
      	data->caps = NULL;
      }
      data->signal_id = g_signal_connect (G_OBJECT (src), "new_pad", 
					  G_CALLBACK (gst_parse_found_pad), data);
      return TRUE;
    }
    templs = g_list_next (templs);
  }
  return FALSE;
}
/*
 * performs a link and frees the struct. src and sink elements must be given
 * return values   0 - link performed
 *                 1 - link delayed
 *                <0 - error
 */
static gint
gst_parse_perform_link (link_t *link, graph_t *graph)
{
  GstElement *src = link->src;
  GstElement *sink = link->sink;
  GSList *srcs = link->src_pads;
  GSList *sinks = link->sink_pads;
  g_assert (GST_IS_ELEMENT (src));
  g_assert (GST_IS_ELEMENT (sink));
  
  GST_CAT_INFO (GST_CAT_PIPELINE, "linking %s(%s):%u to %s(%s):%u with caps \"%" GST_PTR_FORMAT "\"", 
                GST_ELEMENT_NAME (src), link->src_name ? link->src_name : "---", g_slist_length (srcs),
                GST_ELEMENT_NAME (sink), link->sink_name ? link->sink_name : "---", g_slist_length (sinks),
	        link->caps);

  if (!srcs || !sinks) {
    if (gst_element_link_pads_filtered (src, srcs ? (const gchar *) srcs->data : NULL,
                                        sink, sinks ? (const gchar *) sinks->data : NULL,
					link->caps)) {
      gst_parse_element_lock (sink, gst_element_is_locked_state (src));
      goto success;
    } else {
      if (gst_parse_perform_delayed_link (src, srcs ? (const gchar *) srcs->data : NULL,
                                          sink, sinks ? (const gchar *) sinks->data : NULL,
					  link->caps)) {
        gst_parse_element_lock (sink, TRUE);
	goto success;
      } else {
        goto error;
      }
    }
  }
  if (g_slist_length (link->src_pads) != g_slist_length (link->src_pads)) {
    goto error;
  }
  while (srcs && sinks) {
    const gchar *src_pad = (const gchar *) srcs->data;
    const gchar *sink_pad = (const gchar *) sinks->data;
    srcs = g_slist_next (srcs);
    sinks = g_slist_next (sinks);
    if (gst_element_link_pads_filtered (src, src_pad, sink, sink_pad, link->caps)) {
      gst_parse_element_lock (sink, gst_element_is_locked_state (src));
      continue;
    } else {
      if (gst_parse_perform_delayed_link (src, src_pad,
                                          sink, sink_pad,
					  link->caps)) {
        gst_parse_element_lock (sink, TRUE);
	continue;
      } else {
        goto error;
      }
    }
  }
  
success:
  gst_parse_free_link (link);
  return 0;
  
error:
  ERROR (GST_PARSE_ERROR_LINK, _("could not link %s to %s"), GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink));
  gst_parse_free_link (link);
  return -1;
}


static int yylex (void *lvalp);
static int yyerror (const char *s);
%}

%union {
    gchar *s;
    chain_t *c;
    link_t *l;
    GstElement *e;
    GSList *p;
    graph_t *g;
}

%token <s> PARSE_URL
%token <s> IDENTIFIER
%left <s> REF PADREF BINREF
%token <s> ASSIGNMENT
%token <s> LINK

%type <g> graph
%type <c> chain bin
%type <l> reference
%type <l> linkpart link
%type <p> linklist
%type <e> element 
%type <p> padlist pads assignments

%left '{' '}' '(' ')'
%left ','
%right '.'
%left '!' '='

%pure_parser

%start graph
%%

element:	IDENTIFIER     		      { $$ = gst_element_factory_make ($1, NULL); 
						if (!$$)
						  ERROR (GST_PARSE_ERROR_NO_SUCH_ELEMENT, _("no element \"%s\""), $1);
						gst_parse_strfree ($1);
						if (!$$)
						  YYERROR;
                                              }
	|	element ASSIGNMENT	      { gst_parse_element_set ($2, $1, graph);
						$$ = $1;
	                                      }
	;
assignments:	/* NOP */		      { $$ = NULL; }
	|	assignments ASSIGNMENT	      { $$ = g_slist_prepend ($1, $2); }
	;		
bin:	        '{' assignments chain '}' { GST_BIN_MAKE ($$, "thread", $3, $2); }
        |       '(' assignments chain ')' { GST_BIN_MAKE ($$, "bin", $3, $2); }
        |       BINREF assignments chain ')'  { GST_BIN_MAKE ($$, $1, $3, $2); 
						gst_parse_strfree ($1);
					      }
	|	'{' assignments '}'	      { GST_BIN_MAKE ($$, "thread", NULL, $2); }
	|	'(' assignments '}'	      { GST_BIN_MAKE ($$, "thread", NULL, $2); }
        |       BINREF assignments ')'	      { GST_BIN_MAKE ($$, $1, NULL, $2); 
						gst_parse_strfree ($1);
					      }
	|	'{' assignments error '}'     { GST_BIN_MAKE ($$, "thread", NULL, $2); }
	|	'(' assignments error '}'     { GST_BIN_MAKE ($$, "thread", NULL, $2); }
        |       BINREF assignments error ')'  { GST_BIN_MAKE ($$, $1, NULL, $2); 
						gst_parse_strfree ($1);
					      }
	;
	
pads:		PADREF 			      { $$ = g_slist_prepend (NULL, $1); }
	|	PADREF padlist		      { $$ = $2;
						$$ = g_slist_prepend ($$, $1);
					      }				     
	;
padlist:	',' IDENTIFIER		      { $$ = g_slist_prepend (NULL, $2); }
	|	',' IDENTIFIER padlist	      { $$ = g_slist_prepend ($3, $2); }
	;
	
reference:	REF 			      { MAKE_REF ($$, $1, NULL); }
	|	REF padlist		      { MAKE_REF ($$, $1, $2); }
	;

linkpart:	reference		      { $$ = $1; }
	|	pads			      { MAKE_REF ($$, NULL, $1); }
	|	/* NOP */		      { MAKE_REF ($$, NULL, NULL); }
	;
	
link:		linkpart LINK linkpart	      { $$ = $1;
						if ($2) {
						  $$->caps = gst_caps_from_string ($2);
						  if (!$$->caps)
						    ERROR (GST_PARSE_ERROR_LINK, _("could not parse caps \"%s\""), $2);
						  gst_parse_strfree ($2);
						}
						$$->sink_name = $3->src_name;
						$$->sink_pads = $3->src_pads;
						gst_parse_link_free ($3);
					      }
	;
	
linklist:	link			      { $$ = g_slist_prepend (NULL, $1); }
	|	link linklist		      { $$ = g_slist_prepend ($2, $1); }
	|	linklist error		      { $$ = $1; }
	;	

chain:   	element			      { $$ = gst_parse_chain_new ();
						$$->first = $$->last = $1;
						$$->front = $$->back = NULL;
						$$->elements = g_slist_prepend (NULL, $1);
					      }
	|	bin			      { $$ = $1; }
	|	chain chain		      { if ($1->back && $2->front) {
						  if (!$1->back->sink_name) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without source element"));
						    gst_parse_free_link ($1->back);
						  } else {
						    ((graph_t *) graph)->links = g_slist_prepend (((graph_t *) graph)->links, $1->back);
						  }
						  if (!$2->front->src_name) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without sink element"));
						    gst_parse_free_link ($2->front);
						  } else {
						    ((graph_t *) graph)->links = g_slist_prepend (((graph_t *) graph)->links, $2->front);
						  }
						  $1->back = NULL;
						} else if ($1->back) {
						  if (!$1->back->sink_name) {
						    $1->back->sink = $2->first;
						  }
						} else if ($2->front) {
						  if (!$2->front->src_name) {
						    $2->front->src = $1->last;
						  }
						  $1->back = $2->front;
						}
						
						if ($1->back) {
						  if (!$1->back->sink || !$1->back->src) {
						    ((graph_t *) graph)->links = g_slist_prepend (((graph_t *) graph)->links, $1->back);
						    $1->back = NULL;
						  } else {
						    gst_parse_perform_link ($1->back, (graph_t *) graph);
						  }
						}
						$1->last = $2->last;
						$1->back = $2->back;
						$1->elements = g_slist_concat ($1->elements, $2->elements);
						gst_parse_chain_free ($2);
						$$ = $1;
					      }
	|	chain linklist		      { GSList *walk;
						if ($1->back) {
						  $2 = g_slist_prepend ($2, $1->back);
						  $1->back = NULL;
						} else {
						  if (!((link_t *) $2->data)->src_name) {
						    ((link_t *) $2->data)->src = $1->last;
						  }						  
						}
						walk = $2;
						while (walk) {
						  link_t *link = (link_t *) walk->data;
						  walk = walk->next;
						  if (!link->sink_name && walk) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without sink element"));
						    gst_parse_free_link (link);
						  } else if (!link->src_name && !link->src) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without source element"));
						    gst_parse_free_link (link);
						  } else {
						    if (walk) {
						      ((graph_t *) graph)->links = g_slist_prepend (((graph_t *) graph)->links, link);
						    } else {
						      $1->back = link;
						    }
						  }
						}
						g_slist_free ($2);
						$$ = $1;
					      }
	|	chain error		      { $$ = $1; }
	|	link chain		      { if ($2->front) {
						  if (!$2->front->src_name) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without source element"));
						    gst_parse_free_link ($2->front);
						  } else {
						    ((graph_t *) graph)->links = g_slist_prepend (((graph_t *) graph)->links, $2->front);
						  }
						}
						if (!$1->sink_name) {
						  $1->sink = $2->first;
						}
						$2->front = $1;
						$$ = $2;
					      }
	|	PARSE_URL chain		      { $$ = $2;
						if ($$->front) {
						  GstElement *element = 
							  gst_element_make_from_uri (GST_URI_SRC, $1, NULL);
						  if (!element) {
						    ERROR (GST_PARSE_ERROR_NO_SUCH_ELEMENT, 
							    _("no source element for URI \"%s\""), $1);
						  } else {
						    $$->front->src = element;
						    ((graph_t *) graph)->links = g_slist_prepend (
							    ((graph_t *) graph)->links, $$->front);
						    $$->front = NULL;
						    $$->elements = g_slist_prepend ($$->elements, element);
						  }
						} else {
						  ERROR (GST_PARSE_ERROR_LINK, 
							  _("no element to link URI \"%s\" to"), $1);
						}
						g_free ($1);
					      }
	|	link PARSE_URL		      { GstElement *element =
							  gst_element_make_from_uri (GST_URI_SINK, $2, NULL);
						if (!element) {
						  ERROR (GST_PARSE_ERROR_NO_SUCH_ELEMENT, 
							  _("no sink element for URI \"%s\""), $2);
						  YYERROR;
						} else if ($1->sink_name || $1->sink_pads) {
						  ERROR (GST_PARSE_ERROR_LINK, 
							  _("could not link sink element for URI \"%s\""), $2);
						  YYERROR;
						} else {
						  $$ = gst_parse_chain_new ();
						  $$->first = $$->last = element;
						  $$->front = $1;
						  $$->front->sink = element;
						  $$->elements = g_slist_prepend (NULL, element);
						}
						g_free ($2);
					      }
	;
graph:		/* NOP */		      { ERROR (GST_PARSE_ERROR_EMPTY, _("empty pipeline not allowed"));
						$$ = (graph_t *) graph;
					      }
	|	chain			      { $$ = (graph_t *) graph;
						if ($1->front) {
						  if (!$1->front->src_name) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without source element"));
						    gst_parse_free_link ($1->front);
						  } else {
						    $$->links = g_slist_prepend ($$->links, $1->front);
						  }
						  $1->front = NULL;
						}
						if ($1->back) {
						  if (!$1->back->sink_name) {
						    ERROR (GST_PARSE_ERROR_LINK, _("link without sink element"));
						    gst_parse_free_link ($1->back);
						  } else {
						    $$->links = g_slist_prepend ($$->links, $1->back);
						  }
						  $1->back = NULL;
						}
						$$->chain = $1;
					      }
	;

%%

extern FILE *_gst_parse_yyin;
int _gst_parse_yylex (YYSTYPE *lvalp);

static int yylex (void *lvalp) {
    return _gst_parse_yylex ((YYSTYPE*) lvalp);
}

static int
yyerror (const char *s)
{
  /* FIXME: This should go into the GError somehow, but how? */
  g_warning ("error: %s\n", s);
  return -1;
}

int _gst_parse_yy_scan_string (char*);
GstElement *
_gst_parse_launch (const gchar *str, GError **error)
{
  graph_t g;
  gchar *dstr;
  GSList *walk;
  GstBin *bin = NULL;
  GstElement *ret;
  
  g_return_val_if_fail (str != NULL, NULL);

  g.chain = NULL;
  g.links = NULL;
  g.error = error;
  
#ifdef __GST_PARSE_TRACE
  GST_CAT_DEBUG (GST_CAT_PIPELINE, "TRACE: tracing enabled");
  __strings = __chains = __links = 0;
#endif /* __GST_PARSE_TRACE */

  dstr = g_strdup (str);
  _gst_parse_yy_scan_string (dstr);

#ifndef GST_DISABLE_GST_DEBUG
  yydebug = 1;
#endif

  if (yyparse (&g) != 0) {
    SET_ERROR (error, GST_PARSE_ERROR_SYNTAX, "Unrecoverable syntax error while parsing pipeline");
    
    goto error1;
  }
  g_free (dstr);
  
  GST_CAT_INFO (GST_CAT_PIPELINE, "got %u elements and %u links", g.chain ? g_slist_length (g.chain->elements) : 0, g_slist_length (g.links));
  
  if (!g.chain) {
    ret = NULL;
  } else if (!(((chain_t *) g.chain)->elements->next)) {
    /* only one toplevel element */  
    ret = (GstElement *) ((chain_t *) g.chain)->elements->data;
    g_slist_free (((chain_t *) g.chain)->elements);
    if (GST_IS_BIN (ret))
      bin = GST_BIN (ret);
  } else {  
    /* put all elements in our bin */
    bin = GST_BIN (gst_element_factory_make ("pipeline", NULL));
    g_assert (bin);
    walk = g.chain->elements;
    while (walk) {
      gst_bin_add (bin, GST_ELEMENT (walk->data));
      walk = g_slist_next (walk);  
    }
    g_slist_free (g.chain->elements);
    ret = GST_ELEMENT (bin);
  }
  gst_parse_chain_free (g.chain);
  
  /* remove links */
  walk = g.links;
  while (walk) {
    link_t *l = (link_t *) walk->data;
    GstElement *sink;
    walk = g_slist_next (walk);
    if (!l->src) {
      if (l->src_name) {
        if (bin) {
          l->src = gst_bin_get_by_name_recurse_up (bin, l->src_name);
        } else {
          l->src = strcmp (GST_ELEMENT_NAME (ret), l->src_name) == 0 ? ret : NULL;
        }
      }
      if (!l->src) {
        SET_ERROR (error, GST_PARSE_ERROR_NO_SUCH_ELEMENT, "No element named \"%s\" - omitting link", l->src_name);
        gst_parse_free_link (l);
        continue;
      }
    }
    if (!l->sink) {
      if (l->sink_name) {
        if (bin) {
          l->sink = gst_bin_get_by_name_recurse_up (bin, l->sink_name);
        } else {
          l->sink = strcmp (GST_ELEMENT_NAME (ret), l->sink_name) == 0 ? ret : NULL;
        }
      }
      if (!l->sink) {
        SET_ERROR (error, GST_PARSE_ERROR_NO_SUCH_ELEMENT, "No element named \"%s\" - omitting link", l->sink_name);
        gst_parse_free_link (l);
        continue;
      }
    }
    sink = l->sink;
    gst_parse_perform_link (l, &g);
  }
  g_slist_free (g.links);

out:
#ifdef __GST_PARSE_TRACE
  GST_CAT_DEBUG (GST_CAT_PIPELINE, "TRACE: %u strings, %u chains and %u links left", __strings, __chains, __links);
  if (__strings || __chains || __links) {
    g_warning ("TRACE: %u strings, %u chains and %u links left", __strings, __chains, __links);
  }
#endif /* __GST_PARSE_TRACE */

  return ret;
  
error1:
  g_free (dstr);
  
  if (g.chain) {
    walk = g.chain->elements;
    while (walk) {
      gst_object_unref (GST_OBJECT (walk->data));
      walk = walk->next;
    }
    g_slist_free (g.chain->elements);
  }
  gst_parse_chain_free (g.chain);
  
  walk = g.links;
  while (walk) {
    gst_parse_free_link ((link_t *) walk->data);
    walk = walk->next;
  }
  g_slist_free (g.links);
  
  g_assert (*error);
  ret = NULL;
  
  goto out;
}
